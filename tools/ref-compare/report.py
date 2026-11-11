"""SynthyBoy - ref-compare: report, baseline and A/B files from results.json (see ref_compare.py compare).

  report.py --results DIR/results.json --ref DIR --ours DIR --out DIR [--baseline FILE [--update-baseline]] [--ab 30,20]

Writes into --out: results.csv, summary.json, report.md, ab/<id>.<script>.ab.wav (reference, 1 s gap, ours) and ab/<id>.<script>.lr.wav (reference in the left channel, ours in the
right one, both mono, time aligned) for the worst N and a seeded random sample of M presets. The baseline (docs/ui-design/refcompare-baseline.json) is a compact map of
"<id>.<script>" to [score, class]; a regression is a class that got worse or a score that dropped by more than the tolerance (default 2.0). Exit code 1 on a regression.
"""
import argparse
import collections
import csv
import json
import os
import random
import sys

import numpy as np  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import metrics  # noqa: E402

RANK = {'MATCH': 3, 'CLOSE': 2, 'DIFFERENT': 1, 'FAILED': 0}
FAMILIES = collections.OrderedDict([
    ('spectrum', ['mel', 'stft', 'centroid', 'rolloff']),
    ('envelope', ['env_corr', 'env_db', 'seg_attack', 'seg_sustain', 'seg_release', 'tail']),
    ('loudness', ['lufs', 'peak', 'floor']),
    ('pitch', ['f0']),
    ('stereo', ['balance', 'sidemid', 'corr']),
])


def category(rel):
    parts = rel.replace('\\', '/').split('/')
    if parts[0] == 'Factory' and len(parts) > 2:
        return parts[1]
    return 'User'


def load(path):
    return json.load(open(path))


def histogram(scores, step=10):
    h = collections.OrderedDict()
    for lo in range(0, 100, step):
        h['%d-%d' % (lo, lo + step - (1 if lo + step < 100 else 0))] = 0
    for s in scores:
        i = min(int(s // step), 100 // step - 1)
        lo = i * step
        h['%d-%d' % (lo, lo + step - (1 if lo + step < 100 else 0))] += 1
    return h


def ab_files(rec, ref_dir, ours_dir, out_dir):
    from scipy.io import wavfile
    k = '%s.%s' % (rec['id'], rec['script'])
    sr, ref = wavfile.read(os.path.join(ref_dir, k + '.wav'))
    _, ours = wavfile.read(os.path.join(ours_dir, k + '.wav'))
    n = min(len(ref), len(ours))
    off = rec.get('offset_samples', 0)
    ours_a = metrics.shift(ours, off, n)
    ref = ref[:n]
    gap = np.zeros((sr, 2), dtype=np.float32)
    os.makedirs(out_dir, exist_ok=True)
    wavfile.write(os.path.join(out_dir, k + '.ab.wav'), sr, np.concatenate([ref, gap, ours_a]).astype(np.float32))
    lr = np.stack([metrics.mono(ref), metrics.mono(ours_a)], 1).astype(np.float32)
    wavfile.write(os.path.join(out_dir, k + '.lr.wav'), sr, lr)


def fmt_off(rec):
    parts = []
    for o in rec.get('off', [])[:3]:
        v = o['value']
        parts.append('%s %s' % (o['metric'], ('%.2f' % v) if isinstance(v, (int, float)) else '-'))
    return ', '.join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--results', required=True)
    ap.add_argument('--ref', required=True)
    ap.add_argument('--ours', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--baseline', default='')
    ap.add_argument('--update-baseline', action='store_true')
    ap.add_argument('--tolerance', type=float, default=2.0)
    ap.add_argument('--ab', default='30,20')
    ap.add_argument('--seed', type=int, default=20261111)
    a = ap.parse_args()
    data = load(a.results)
    recs = data['records']
    cfg = data['scoring']
    os.makedirs(a.out, exist_ok=True)
    ok = [r for r in recs if r.get('class') != 'FAILED']
    failed = [r for r in recs if r.get('class') == 'FAILED']
    counts = collections.OrderedDict((c, sum(1 for r in recs if r['class'] == c)) for c in ('MATCH', 'CLOSE', 'DIFFERENT', 'FAILED'))
    scores = [r['score'] for r in ok]

    # ---- csv ----
    dkeys = sorted({k for r in ok for k in r if k.startswith('d_')})
    with open(os.path.join(a.out, 'results.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['id', 'script', 'category', 'class', 'score', 'failed', 'offset_ms'] + dkeys)
        for r in recs:
            w.writerow([r['id'], r['script'], category(r['rel']), r['class'], r.get('score'), r.get('failed', ''), ('%.1f' % r['offset_ms']) if 'offset_ms' in r else ''] +
                       [('%.4f' % r[k]) if isinstance(r.get(k), (int, float)) else '' for k in dkeys])

    # ---- summary ----
    fam_mean = {}
    for fam, names in FAMILIES.items():
        vals = [r['sub'][n] for r in ok for n in names if n in r.get('sub', {})]
        fam_mean[fam] = round(float(np.mean(vals)), 1) if vals else None
    cat = collections.OrderedDict()
    for r in ok:
        cat.setdefault(category(r['rel']), []).append(r)
    cat_rows = [(c, len(v), float(np.mean([x['score'] for x in v])), sum(1 for x in v if x['class'] == 'MATCH'), sum(1 for x in v if x['class'] == 'CLOSE'),
                 sum(1 for x in v if x['class'] == 'DIFFERENT')) for c, v in sorted(cat.items())]
    cause = collections.Counter()
    for r in ok:
        if r['class'] == 'DIFFERENT' and r.get('off'):
            cause[r['off'][0]['metric']] += 1
    failures = collections.Counter(r['failed'].split(':')[0] + (':' + r['failed'].split(':', 1)[1] if ':' in r['failed'] else '') for r in failed)
    summary = {
        'renders': len(recs), 'counts': counts, 'mean_score': round(float(np.mean(scores)), 2) if scores else None, 'median_score': round(float(np.median(scores)), 2) if scores else None,
        'histogram': histogram(scores), 'families': fam_mean, 'categories': [{'category': c, 'n': n, 'mean': round(m, 1), 'match': x, 'close': y, 'different': z} for c, n, m, x, y, z in cat_rows],
        'first_cause_of_different': dict(cause.most_common()), 'failures': dict(failures),
    }
    json.dump(summary, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)

    # ---- A/B files ----
    nw, nr = (int(x) for x in a.ab.split(','))
    worst = sorted(ok, key=lambda r: (r['score'], r['id'], r['script']))
    rng = random.Random(a.seed)
    pool = sorted(ok, key=lambda r: (r['id'], r['script']))
    rand = rng.sample(pool, min(nr, len(pool)))
    ab_dir = os.path.join(a.out, 'ab')
    for r in worst[:nw] + rand:
        ab_files(r, a.ref, a.ours, ab_dir)

    # ---- report.md ----
    L = []
    L.append('# Reference comparison report\n')
    L.append('Renders compared: %d (%d with a score, %d failed). Score = 0..100 (tools/ref-compare/score.py, weights in scoring.json); MATCH >= %.0f, CLOSE >= %.0f.\n' %
             (len(recs), len(ok), len(failed), cfg['classes']['match'], cfg['classes']['close']))
    L.append('## Classes\n\n| class | renders |\n|---|---:|')
    for c, n in counts.items():
        L.append('| %s | %d |' % (c, n))
    L.append('\nMean score %.1f, median %.1f.\n' % (summary['mean_score'] or 0, summary['median_score'] or 0))
    L.append('## Score histogram\n\n| score | renders |\n|---|---:|')
    for k, v in summary['histogram'].items():
        L.append('| %s | %d |' % (k, v))
    L.append('\n## Feature families (mean sub-score 0..100)\n\n| family | mean |\n|---|---:|')
    for k, v in fam_mean.items():
        L.append('| %s | %s |' % (k, v))
    L.append('\n## Categories\n\n| category | n | mean | MATCH | CLOSE | DIFFERENT |\n|---|---:|---:|---:|---:|---:|')
    for c, n, m, x, y, z in cat_rows:
        L.append('| %s | %d | %.1f | %d | %d | %d |' % (c, n, m, x, y, z))
    L.append('\n## Metric that costs the most in a DIFFERENT render\n\n| metric | renders |\n|---|---:|')
    for k, v in cause.most_common():
        L.append('| %s | %d |' % (k, v))
    L.append('\n## Failed\n')
    for k, v in failures.most_common():
        L.append('- %s: %d' % (k, v))
    L.append('\n## Worst 50\n\n| # | id | script | score | class | offset ms | metrics off (distance) |\n|--:|---|---|---:|---|---:|---|')
    for i, r in enumerate(worst[:50], 1):
        L.append('| %d | %s | %s | %.1f | %s | %.1f | %s |' % (i, r['id'], r['script'], r['score'], r['class'], r.get('offset_ms', 0.0), fmt_off(r)))
    L.append('\nA/B files (reference, 1 s gap, ours) of the worst %d and %d random renders are in `ab/` (`.ab.wav`; `.lr.wav` = reference left, ours right).\n' % (nw, len(rand)))
    open(os.path.join(a.out, 'report.md'), 'w').write('\n'.join(L) + '\n')

    # ---- baseline ----
    cur = {'%s.%s' % (r['id'], r['script']): [r['score'], r['class']] for r in recs}
    regress = []
    if a.baseline and os.path.exists(a.baseline) and not a.update_baseline:
        base = json.load(open(a.baseline)).get('presets', {})
        for k, (s, c) in sorted(cur.items()):
            if k not in base:
                continue
            bs, bc = base[k]
            if RANK[c] < RANK[bc] or (s is not None and bs is not None and s < bs - a.tolerance):
                regress.append((k, bs, bc, s, c))
        for k, bs, bc, s, c in regress:
            print('REGRESSION %s: %s %s -> %s %s' % (k, bs, bc, s, c))
    if a.update_baseline and a.baseline:
        merged = {}
        if os.path.exists(a.baseline):
            merged = json.load(open(a.baseline)).get('presets', {})   # a partial run (--preset) only replaces its own renders
        merged.update(cur)
        json.dump({'schema': 'synthyboy-refcompare-baseline', 'version': 1, 'tolerance': a.tolerance, 'presets': dict(sorted(merged.items()))}, open(a.baseline, 'w'), separators=(',', ':'))
        open(a.baseline, 'a').write('\n')
        print('baseline written: %s (%d renders)' % (a.baseline, len(cur)))
    print('classes: ' + ', '.join('%s %d' % (c, n) for c, n in counts.items()) + ';  mean score %s;  report: %s' % (summary['mean_score'], os.path.join(a.out, 'report.md')))
    return 1 if regress else 0


if __name__ == '__main__':
    sys.exit(main())
