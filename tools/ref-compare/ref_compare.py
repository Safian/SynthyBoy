#!/usr/bin/env python3
"""SynthyBoy - ref-compare: compares our renders of imported Serum 2 presets with the reference renders of the real Serum 2. Called by scripts/run-ref-compare.sh.

  ref_compare.py render-ours  --presets DIR --ref DIR --imported DIR --ours DIR --render-bin BIN [--filter RX] [--jobs N]
  ref_compare.py compare      --presets DIR --ref DIR --ours DIR --out DIR [--filter RX] [--jobs N] [--ref2 DIR]
  ref_compare.py variants     --presets DIR --ref DIR --imported DIR --out DIR --render-bin BIN [--bases FILE]
  ref_compare.py noise        --presets DIR --ref DIR --ref2 DIR --out DIR

Deterministic: the same inputs give the same numbers (numpy / scipy, fixed sizes, results sorted by id, a fixed seed for the random sample of the A/B files).
"""
import os

for _v in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS'):
    os.environ.setdefault(_v, '1')

import argparse
import concurrent.futures as cf
import csv
import importlib.util
import json
import os
import random
import re
import subprocess
import sys
import time

import numpy as np  # noqa: E402
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import metrics  # noqa: E402
import score as scoring  # noqa: E402

_spec = importlib.util.spec_from_file_location('render_all', os.path.join(HERE, '..', 'serum-reference', 'render_all.py'))
render_all = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(render_all)

SR = 48000


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# jobs
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def discover(presets, flt, scripts):
    rx = re.compile(flt) if flt else None
    jobs = []
    for root, _, files in os.walk(presets):
        for f in files:
            if not f.endswith('.SerumPreset'):
                continue
            rel = os.path.relpath(os.path.join(root, f), presets)
            if rx and not rx.search(rel):
                continue
            for s in render_all.scripts_for(rel, scripts):
                spec, length = render_all.SCRIPTS[s]
                jobs.append({'id': render_all.preset_id(rel), 'script': s, 'rel': rel, 'notes': spec, 'length': length})
    jobs.sort(key=lambda j: (j['id'], j['script']))
    return jobs


def key(j):
    return '%s.%s' % (j['id'], j['script'])


def load_wav(path):
    sr, data = wavfile.read(path)
    if data.dtype != np.float32:
        data = data.astype(np.float32) / (32768.0 if data.dtype == np.int16 else 1.0)
    if data.ndim == 1:
        data = np.stack([data, data], 1)
    return sr, data


def write_wav(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    wavfile.write(path, SR, np.ascontiguousarray(data, dtype=np.float32))


def note_times(spec):
    notes = [tuple(float(v) for v in n.split(':')) for n in spec.split(',')]
    return min(n[2] for n in notes), max(n[2] + n[3] for n in notes)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# render ours
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def sbpreset_of(imported, rel):
    return os.path.join(imported, os.path.splitext(rel)[0] + '.sbpreset')


def cmd_render_ours(a):
    jobs = discover(a.presets, a.filter, a.scripts.split(','))
    os.makedirs(a.ours, exist_ok=True)
    rows = []
    for j in jobs:
        rows.append('\t'.join([key(j), sbpreset_of(a.imported, j['rel']), j['notes'], str(j['length']), os.path.join(a.ours, key(j) + '.wav')]))
    shards = max(1, a.jobs)
    procs = []
    for s in range(shards):
        mf = os.path.join(a.ours, 'manifest.%d.tsv' % s)
        with open(mf, 'w') as f:
            f.write('\n'.join(rows[s::shards]) + '\n')
        cmd = ['nice', '-n', '10', a.render_bin, '--manifest', mf, '--user-dir', a.imported]
        procs.append((s, subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)))
    status = {}
    for s, p in procs:
        out, err = p.communicate()
        for line in out.splitlines():
            parts = line.split(' ', 2)
            if parts[0] == 'ok':
                status[parts[1]] = {'ok': True}
            elif parts[0] == 'FAIL':
                status[parts[1]] = {'ok': False, 'reason': parts[2] if len(parts) > 2 else 'failed'}
        if p.returncode not in (0, 1):
            for r in rows[s::shards]:
                k = r.split('\t')[0]
                status.setdefault(k, {'ok': False, 'reason': 'renderer crashed (exit %d): %s' % (p.returncode, err.strip()[-200:])})
    with open(os.path.join(a.ours, 'status.json'), 'w') as f:
        json.dump(status, f, indent=0, sort_keys=True)
    bad = sum(1 for v in status.values() if not v['ok'])
    print('ours: %d renders, %d failed' % (len(rows), bad))
    return 0


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# compare one pair
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def prepare_pair(ref, ours, total):
    """Aligns `ours` to `ref` (first transient), returns (ref, ours, offset_samples, aligned flag), both exactly `total` frames."""
    n = total
    ref = ref[:n] if len(ref) >= n else np.pad(ref, ((0, n - len(ref)), (0, 0)))
    off, ok = metrics.estimate_offset(ref, ours)
    ours_a = metrics.shift(ours, off, n)
    return ref, ours_a, off, ok


def compare_one(args):
    j, ref_dir, ours_dir, ours_status, ref2_dir = args
    k = key(j)
    rec = {'id': j['id'], 'script': j['script'], 'rel': j['rel']}
    rw, rj, rf = (os.path.join(ref_dir, k + e) for e in ('.wav', '.json', '.fail.json'))
    ow = os.path.join(ours_dir, k + '.wav')
    if not os.path.exists(rw):
        reason = 'reference_missing'
        if os.path.exists(rf):
            reason = 'reference_failed:' + json.load(open(rf)).get('reason', '?')
        rec['failed'] = reason
        return rec
    st = ours_status.get(k)
    if st is None or not st.get('ok') or not os.path.exists(ow):
        rec['failed'] = 'ours_failed:' + (st.get('reason', 'no render') if st else 'no render')
        return rec
    sr, ref = load_wav(rw)
    sr2, ours = load_wav(ow)
    if sr != SR or sr2 != SR:
        rec['failed'] = 'sample_rate'
        return rec
    t_on, t_off = note_times(j['notes'])
    total = j['length']
    if not np.isfinite(ref).all() or not np.isfinite(ours).all():
        rec['failed'] = 'non_finite'
        return rec
    rec['ref_peak_db'] = metrics.peak_db(ref)
    rec['ours_peak_db'] = metrics.peak_db(ours)
    if rec['ref_peak_db'] < -80.0:
        rec['failed'] = 'reference_silent'
        rec['ours_peak_db'] = metrics.peak_db(ours)
        return rec
    n = int(round(total * SR))
    ref_p, ours_p, off, ok = prepare_pair(ref, ours, n)
    rec['offset_samples'] = int(off)
    rec['offset_ms'] = off * 1000.0 / SR
    rec['aligned'] = bool(ok)
    rec.update(metrics.compare_pair(ref_p, ours_p, t_on, t_off, total))
    rec['ours_silent'] = rec['ours_peak_db'] < -80.0
    if ref2_dir:
        r2 = os.path.join(ref2_dir, k + '.wav')
        if os.path.exists(r2):
            _, ref2 = load_wav(r2)
            if np.isfinite(ref2).all() and len(ref2) >= n:
                ref2 = ref2[:n]
                m2 = metrics.compare_pair(ref_p, ref2, t_on, t_off, total)
                rec['floor'] = {kk: vv for kk, vv in m2.items() if kk.startswith('d_') and vv is not None}
                rec['floor_identical'] = bool(np.array_equal(ref_p, ref2))
    return rec


def cmd_compare(a):
    jobs = discover(a.presets, a.filter, a.scripts.split(','))
    status = {}
    sp = os.path.join(a.ours, 'status.json')
    if os.path.exists(sp):
        status = json.load(open(sp))
    t0 = time.time()
    work = [(j, a.ref, a.ours, status, a.ref2) for j in jobs]
    with cf.ProcessPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        recs = list(ex.map(compare_one, work, chunksize=4))
    recs.sort(key=lambda r: (r['id'], r['script']))
    os.makedirs(a.out, exist_ok=True)
    cfg = scoring.load_config(a.scoring)
    for r in recs:
        scoring.apply(r, cfg)
    with open(os.path.join(a.out, 'results.json'), 'w') as f:
        json.dump({'schema': 'synthyboy-ref-compare', 'version': 1, 'scoring': cfg, 'records': recs}, f, indent=1, sort_keys=True)
    print('compared %d renders in %.0fs' % (len(recs), time.time() - t0))
    return 0


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# the noise floor: two renders of the same preset by the real Serum 2
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def noise_one(args):
    j, ref_dir, ref2_dir = args
    k = key(j)
    a, b = os.path.join(ref_dir, k + '.wav'), os.path.join(ref2_dir, k + '.wav')
    if not (os.path.exists(a) and os.path.exists(b)):
        return None
    _, x = load_wav(a)
    _, y = load_wav(b)
    n = min(len(x), len(y))
    if not (np.isfinite(x).all() and np.isfinite(y).all()) or metrics.peak_db(x) < -80.0:
        return None
    t_on, t_off = note_times(j['notes'])
    rec = {'id': j['id'], 'script': j['script'], 'identical': bool(np.array_equal(x[:n], y[:n]))}
    rec.update({kk: vv for kk, vv in metrics.compare_pair(x[:n], y[:n], t_on, t_off, j['length']).items() if kk.startswith('d_') and vv is not None})
    return rec


def cmd_noise(a):
    jobs = discover(a.presets, a.filter, a.scripts.split(','))
    with cf.ProcessPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        recs = [r for r in ex.map(noise_one, [(j, a.ref, a.ref2) for j in jobs], chunksize=4) if r]
    recs.sort(key=lambda r: (r['id'], r['script']))
    names = sorted({k for r in recs for k in r if k.startswith('d_')})
    stats = {}
    for k in names:
        v = np.array([r[k] for r in recs if k in r])
        stats[k] = {'n': int(len(v)), 'p50': float(np.percentile(v, 50)), 'p95': float(np.percentile(v, 95)), 'p99': float(np.percentile(v, 99)), 'max': float(v.max())}
    ident = sum(1 for r in recs if r['identical'])
    os.makedirs(a.out, exist_ok=True)
    json.dump({'pairs': len(recs), 'bit_identical': ident, 'stats': stats, 'records': recs}, open(os.path.join(a.out, 'noise.json'), 'w'), indent=1, sort_keys=True)
    print('serum vs serum: %d pairs, %d bit identical (%.1f %%)' % (len(recs), ident, 100.0 * ident / max(1, len(recs))))
    for k in names:
        print('  %-16s p50 %.4f  p95 %.4f  p99 %.4f  max %.4f' % (k, stats[k]['p50'], stats[k]['p95'], stats[k]['p99'], stats[k]['max']))
    return 0


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# deliberately broken variants (calibration of the score)
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
VARIANTS = [
    ('tune50', 'master.tune+=50', 'the whole preset 50 cents sharp'),
    ('osc1_muted', 'osc1.on=0', 'oscillator A switched off'),
    ('cutoff', 'filter1.cutoff*=0.125', 'filter 1 cutoff three octaves down (only where filter 1 is on)'),
    ('level6', 'master.volume+=-6', 'level 6 dB down'),
    ('detune', 'osc1.unison.detune*=4', 'unison detune of oscillator A four times wider'),
]


def sbpreset_params(path):
    try:
        return json.load(open(path))['state']['params']
    except (OSError, ValueError, KeyError):
        return {}


def cmd_variants(a):
    res = json.load(open(a.results))['records']
    byk = {'%s.%s' % (r['id'], r['script']): r for r in res if r.get('score') is not None and r.get('script') == 'c3'}
    cand = sorted(byk.values(), key=lambda r: (-r['score'], r['id']))
    bases = []
    for r in cand:
        p = sbpreset_params(sbpreset_of(a.imported, r['rel']))
        # a base must be a render that matches in level (a 6 dB drop of a render that is 6 dB too loud would improve it) and must use the parts the breaks hit
        if (abs(r['r_lufs_ours'] - r['r_lufs_ref']) < 2.0 and p.get('osc1.on', 0) >= 0.5 and p.get('filter1.on', 0) >= 0.5 and p.get('osc1.level', 0) > 5
                and p.get('filter1.cutoff', 0) >= 800.0):
            bases.append(r)
        if len(bases) >= a.bases:
            break
    jobs_by_key = {key(j): j for j in discover(a.presets, '', ['c3'])}
    os.makedirs(a.out, exist_ok=True)
    vdir = os.path.join(a.out, 'variants')
    rows, names = [], []
    for r in bases:
        k = '%s.%s' % (r['id'], r['script'])
        j = jobs_by_key[k]
        for vname, ov, _ in VARIANTS[:4]:
            kk = '%s.%s' % (k, vname)
            rows.append('\t'.join([kk, sbpreset_of(a.imported, j['rel']), j['notes'], str(j['length']), os.path.join(vdir, kk + '.wav'), ov]))
            names.append((k, vname, kk))
    mf = os.path.join(a.out, 'variants.tsv')
    open(mf, 'w').write('\n'.join(rows) + '\n')
    p = subprocess.run([a.render_bin, '--manifest', mf, '--user-dir', a.imported], capture_output=True, text=True)
    if p.returncode not in (0, 1):
        print('variant render failed: ' + p.stderr[-300:])
        return 3
    cfg = scoring.load_config(a.scoring)
    results = []
    for k, vname, kk in names:
        j = jobs_by_key[k]
        rw = os.path.join(a.ref, k + '.wav')
        ow = os.path.join(vdir, kk + '.wav')
        if not (os.path.exists(rw) and os.path.exists(ow)):
            continue
        _, ref = load_wav(rw)
        _, ours = load_wav(ow)
        n = int(round(j['length'] * SR))
        ref_p, ours_p, off, ok = prepare_pair(ref, ours, n)
        t_on, t_off = note_times(j['notes'])
        rec = {'id': kk, 'script': 'c3', 'rel': j['rel']}
        rec.update(metrics.compare_pair(ref_p, ours_p, t_on, t_off, j['length']))
        rec['ours_silent'] = metrics.peak_db(ours) < -80.0
        scoring.apply(rec, cfg)
        results.append((k, vname, rec))
    base_score = {r['id'] + '.' + r['script']: r['score'] for r in bases}
    table = []
    for k, vname, rec in results:
        table.append({'base': k, 'variant': vname, 'base_score': base_score[k], 'score': rec['score'], 'drop': round(base_score[k] - rec['score'], 2), 'class': rec['class'],
                      'worst_metrics': [o['metric'] for o in rec.get('off', [])]})
    json.dump({'bases': [r['id'] + '.' + r['script'] for r in bases], 'variants': table}, open(os.path.join(a.out, 'variants.json'), 'w'), indent=1)
    drops = [t['drop'] for t in table]
    print('%d variants of %d bases: drop min %.1f  median %.1f  max %.1f' % (len(table), len(bases), min(drops), float(np.median(drops)), max(drops)))
    for vname, _, desc in VARIANTS[:4]:
        d = [t['drop'] for t in table if t['variant'] == vname]
        print('  %-11s drop min %.1f  median %.1f  (%s)' % (vname, min(d), float(np.median(d)), desc))
    need = cfg.get('variants', {}).get('min_drop', 10.0)
    bad = [t for t in table if t['drop'] < need]
    for t in bad:
        print('  NOT LOWER ENOUGH %s %s: %.1f -> %.1f' % (t['base'], t['variant'], t['base_score'], t['score']))
    return 1 if bad else 0


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)

    def common(p):
        p.add_argument('--presets', required=True)
        p.add_argument('--ref', required=True)
        p.add_argument('--filter', default='')
        p.add_argument('--scripts', default='c3,c1,c5')
        p.add_argument('--jobs', type=int, default=4)

    p = sub.add_parser('render-ours')
    common(p)
    p.add_argument('--imported', required=True)
    p.add_argument('--ours', required=True)
    p.add_argument('--render-bin', required=True)
    p.set_defaults(fn=cmd_render_ours)
    p = sub.add_parser('compare')
    common(p)
    p.add_argument('--ours', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--ref2', default='')
    p.add_argument('--scoring', default=os.path.join(HERE, 'scoring.json'))
    p.set_defaults(fn=cmd_compare)
    p = sub.add_parser('noise')
    common(p)
    p.add_argument('--ref2', required=True)
    p.add_argument('--out', required=True)
    p.set_defaults(fn=cmd_noise)
    p = sub.add_parser('variants')
    common(p)
    p.add_argument('--imported', required=True)
    p.add_argument('--results', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--render-bin', required=True)
    p.add_argument('--bases', type=int, default=5)
    p.add_argument('--scoring', default=os.path.join(HERE, 'scoring.json'))
    p.set_defaults(fn=cmd_variants)
    a = ap.parse_args()
    return a.fn(a)


if __name__ == '__main__':
    sys.exit(main())
