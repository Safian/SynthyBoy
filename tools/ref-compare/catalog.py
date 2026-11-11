#!/usr/bin/env python3
"""SynthyBoy - ref-compare: module catalog. Crafts small presets (the init state + one module setting) and scores our render against the real Serum 2 with the metrics of
ref_compare, so that the worst modules show up in one table. Cases: filter types, warp modes, effect modules (with the parameters real presets use), unison, envelopes.

  catalog.py --root REPO --build BUILD [--group filters|warps|fx|unison|env|all] [--only REGEX] [--json OUT]
"""
import argparse
import json
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import experiment as ex  # noqa: E402
import metrics  # noqa: E402
import score  # noqa: E402

FX_TYPE = {'FXFlanger': 1, 'FXPhaser': 2, 'FXChorus': 3, 'FXDelay': 4, 'FXComp': 5, 'FXReverb': 6, 'FXEQ': 7, 'FXFilter': 8, 'FXHyperD': 9, 'FXBode': 10, 'FXConv': 11, 'FXUtils': 12,
           'FXDistortion': 0, 'FXSplit': 13, 'FXSplit3': 14, 'FXSplitMS': 15}   # the "type" member of the effect entries of the real presets
BASE = {'Global0.plainParams.kParamMasterVolume': 0.25}
FILTERS = ['MgL12', 'MgL24', 'LadderEMS', 'H18', 'LadderMg', 'MgL18', 'H12', 'Diffuser', 'MgL6', 'H24', 'LH12', 'LadderAcid', 'HP12', 'DirtyMg', 'L12', 'L24', 'B12', 'N12', 'AP12']
WARPS = ['kPD_OSC', 'kSync', 'kFM_OSC', 'kPD_SUB', 'kDistTube', 'kFM_SUB', 'kFilterLPF', 'kBendPos', 'kRM_OSC', 'kPWM', 'kSelfPD', 'kQuantize', 'kFlip', 'kDistSoftClip', 'kDistHardClip',
         'kAM_OSC', 'kDistLinFold', 'kDistSinFold', 'kASYMPos', 'kBendNeg', 'kRemap_1', 'kEvenOdd']
FX_CASES = {
    'FXReverb': [{'kParamType': 'kPlate', 'kParamWet': 30.0, 'kParamSize': 50.0}, {'kParamType': 'kHall', 'kParamWet': 40.0, 'kParamSize': 70.0}, {'kParamType': 'kVintage', 'kParamWet': 35.0, 'kParamSize': 55.0}],
    'FXDelay': [{'kParamWet': 40.0, 'kParamFeedback': 40.0, 'kParamTimeL': 0.2, 'kParamTimeR': 0.3}, {'kParamWet': 100.0, 'kParamFeedback': 0.0, 'kParamTimeL': 0.1, 'kParamTimeR': 0.1}],
    'FXComp': [{'kParamThresh': -20.0, 'kParamRatio': 4.0, 'kParamAttack': 5.0, 'kParamRelease': 100.0}],
    'FXDistortion': [{'kParamDrive': 40.0, 'kParamWet': 100.0}, {'kParamDrive': 80.0, 'kParamWet': 100.0}],
    'FXEQ': [{}],
    'FXFilter': [{'kParamFreq': 0.5, 'kParamWet': 100.0}],
    'FXChorus': [{'kParamWet': 50.0}],
    'FXHyperD': [{'kParamWet': 50.0}],
    'FXPhaser': [{'kParamWet': 50.0}],
    'FXFlanger': [{'kParamWet': 50.0}],
    'FXUtils': [{}],
    'FXBode': [{'kParamWet': 50.0}],
    'FXConv': [{'kParamWet': 50.0}],
}


def fx_edit(name, params, rack=0):
    return {'FXRack%d.FX' % rack: [{name: {'plainParams': dict(params)}, 'kUIParamMixOrGain': 0.0, 'type': FX_TYPE[name]}]}


def real_fx_cases(root, per_type=6, seed=11):
    """Effect entries of the real presets, stratified: per module type up to `per_type` instances with an audible mix, different in their parameters. Realistic units, by construction."""
    import copy
    import random
    sys.path.insert(0, os.path.join(root, 'scripts'))
    rnd = random.Random(seed)
    found = {}
    src = os.path.join(root, 'tests', 'serum-presets', 'Presets')
    paths = []
    for dp, _, fs in os.walk(src):
        paths += [os.path.join(dp, f) for f in fs if f.endswith('.SerumPreset')]
    paths.sort()
    rnd.shuffle(paths)
    for pth in paths[:260]:
        try:
            _, v = ex._audit.load_serum(pth)
        except Exception:  # noqa: BLE001
            continue
        for r in range(3):
            rk = v.get('FXRack%d' % r)
            for f in (rk.get('FX', []) if isinstance(rk, dict) else []):
                for k in f:
                    if k.startswith('FX') and k in FX_TYPE and isinstance(f[k], dict):
                        pp = f[k].get('plainParams', {})
                        if not isinstance(pp, dict) or float(pp.get('kParamWet', 100.0)) < 20.0 and k not in ('FXEQ', 'FXComp', 'FXUtils', 'FXFilter'):
                            continue
                        sig = json.dumps(pp, sort_keys=True)
                        lst = found.setdefault(k, [])
                        if len(lst) < per_type and sig not in [x[0] for x in lst]:
                            lst.append((sig, copy.deepcopy(f)))
    return found


def cases(group, root=''):
    out = []
    if group in ('filters', 'all'):
        for t in FILTERS:
            for fin in (0.45, 0.7):
                out.append(('filter %s f=%.2f r=30' % (t, fin), {**BASE, 'VoiceFilter0.plainParams.kParamEnable': 1.0, 'VoiceFilter0.plainParams.kParamType': t,
                                                                 'VoiceFilter0.plainParams.kParamFreq': fin, 'VoiceFilter0.plainParams.kParamReso': 30.0}))
    if group in ('warps', 'all'):
        for w in WARPS:
            for amt in (30.0, 70.0):
                out.append(('warp %s %.0f' % (w, amt), {**BASE, 'Oscillator0.WTOsc0.plainParams.kParamWarpMenu': w, 'Oscillator0.WTOsc0.plainParams.kParamWarp': amt / 100.0}))
    if group in ('fx', 'all'):
        for name, lst in sorted(real_fx_cases(root).items()):
            for i, (sig, entry) in enumerate(lst):
                out.append(('fx %s #%d %s' % (name, i, sig[:70]), {**BASE, 'FXRack0.FX': [entry]}))
    if group in ('unison', 'all'):
        for n in (2, 4, 7, 16):
            for dt in (0.1, 0.4, 0.8):
                out.append(('unison n=%d detune=%.1f' % (n, dt), {**BASE, 'Oscillator0.plainParams.kParamUnison': float(n), 'Oscillator0.plainParams.kParamDetune': dt}))
        for bl in (0, 40, 100):
            out.append(('unison n=5 blend=%d' % bl, {**BASE, 'Oscillator0.plainParams.kParamUnison': 5.0, 'Oscillator0.plainParams.kParamDetuneWid': float(bl)}))
    if group in ('osc', 'all'):
        for k, vals in (('kParamOctave', (-2.0, -1.0, 1.0, 2.0)), ('kParamPitch', (-7.0, 3.0, 12.0)), ('kParamFine', (-50.0, 25.0, 100.0)), ('kParamPan', (-100.0, -40.0, 60.0)),
                        ('kParamCoarsePit', (7.0, -12.0))):
            for v in vals:
                out.append(('osc %s=%g' % (k, v), {**BASE, 'Oscillator0.plainParams.' + k: v}))
        for v in (0.25, 0.5, 0.9):
            out.append(('osc WT pos %g' % v, {**BASE, 'Oscillator0.WTOsc0.plainParams.kParamTablePos': v * 256.0}))
        for v in (0.0, 0.5, 1.0):
            out.append(('osc phase=%g' % v, {**BASE, 'Oscillator0.WTOsc0.plainParams.kParamInitialPhase': v * 360.0, 'Oscillator0.WTOsc0.plainParams.kParamRandomPhase': 0.0}))
    if group in ('env', 'all'):
        for a, d, s, r in ((0.2, 0.5, 0.5, 0.5), (0.01, 0.3, 0.0, 0.2), (0.05, 1.0, 0.7, 1.0), (0.0, 0.1, 1.0, 0.05), (1.0, 0.5, 1.0, 1.2)):
            out.append(('env1 a=%g d=%g s=%g r=%g' % (a, d, s, r), {**BASE, 'Env0.plainParams.kParamAttack': a, 'Env0.plainParams.kParamDecay': d, 'Env0.plainParams.kParamSustain': s,
                                                                     'Env0.plainParams.kParamRelease': r}))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--group', default='all')
    ap.add_argument('--only', default='')
    ap.add_argument('--json', default='')
    ap.add_argument('--notes', default='60:100:0:2.5')
    a = ap.parse_args()
    e = ex.Experiment(a.root, a.build)
    cfg = score.load_config(os.path.join(HERE, 'scoring.json'))
    rx = re.compile(a.only) if a.only else None
    t_on, t_off = 0.0, 2.5
    rows = []
    for name, edits in cases(a.group, a.root):
        if rx and not rx.search(name):
            continue
        p = e.preset('cat_' + re.sub(r'[^A-Za-z0-9]+', '_', name)[:60], edits)
        try:
            s, o = e.render_both(p, a.notes, 4.0)
        except Exception as err:  # noqa: BLE001
            print('ERR %s %s' % (name, str(err)[:80]))
            continue
        n = min(len(s), len(o))
        off, ok = metrics.estimate_offset(s[:n], o[:n])
        o2 = metrics.shift(o[:n], off, n)
        rec = {'id': name, 'script': 'c3', 'rel': name}
        rec.update(metrics.compare_pair(s[:n], o2, t_on, t_off, 4.0))
        rec['ours_silent'] = metrics.peak_db(o) < -80.0
        score.apply(rec, cfg)
        rows.append(rec)
        print('%5.1f %-9s %-56s lufs %+5.1f  mel %5.1f  %s' % (rec['score'], rec['class'], name[:56], rec['r_lufs_ours'] - rec['r_lufs_ref'], rec['d_mel'], ','.join(o_['metric'] for o_ in rec['off'])), flush=True)
    if rows:
        print('mean score %.1f over %d cases' % (np.mean([r['score'] for r in rows]), len(rows)))
    if a.json:
        json.dump(rows, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
