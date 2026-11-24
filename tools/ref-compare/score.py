"""SynthyBoy - ref-compare: from the distances of metrics.py to a 0..100 similarity score and a class.

score = sum(weight_i * sub_i) / sum(weight_i of the applicable features), sub_i = 100 * clip(1 - max(0, d_i - floor_i) / span_i, 0, 1)
  floor_i  the tolerance: the distance two renders of the SAME preset by the real Serum 2 have (scoring.json "floors": the 95th percentile of the measured Serum-vs-Serum
           distances; of a preset that has a second reference render, its own measured distance when larger). Below the floor nothing is lost.
  span_i   the distance above the floor at which the feature scores 0 (set so that the deliberately broken variants of the calibration score clearly lower, see
           docs/ui-design/REFCOMPARE-REPORT.md).
Classes: MATCH score >= classes.match, CLOSE >= classes.close, DIFFERENT below; FAILED when a render is missing / non finite / the reference is silent (record["failed"]).
Nothing here depends on a preset name; the thresholds are not changed to make presets pass (they were calibrated once on the noise floor and the broken variants).
"""
import json


def load_config(path):
    with open(path) as f:
        return json.load(f)


def sub_score(d, floor, span):
    if d is None:
        return None
    x = max(0.0, d - floor) / span
    return 100.0 * max(0.0, 1.0 - x)


def apply(rec, cfg):
    """Adds score, class and the reasons to a record produced by ref_compare.compare_one."""
    if rec.get('failed'):
        rec['class'] = 'FAILED'
        rec['score'] = None
        return rec
    feats = cfg['features']
    tot_w = 0.0
    acc = 0.0
    parts = {}
    for name, f in feats.items():
        d = rec.get(f['d'])
        if d is None:
            continue
        floor = f['floor']
        per = rec.get('floor', {}).get(f['d'])
        if per is not None:
            floor = max(floor, per)
        s = sub_score(d, floor, f['span'])
        parts[name] = s
        tot_w += f['weight']
        acc += f['weight'] * s
    score = acc / tot_w if tot_w > 0 else 0.0
    if rec.get('ours_silent'):
        score = 0.0
    rec['score'] = round(score, 2)
    rec['sub'] = {k: round(v, 1) for k, v in parts.items()}
    losses = sorted(((feats[k]['weight'] * (100.0 - v) / 100.0, k) for k, v in parts.items()), reverse=True)
    rec['off'] = [{'metric': k, 'loss': round(l, 2), 'value': rec.get(feats[k]['d'])} for l, k in losses[:3] if l > 0.05]
    c = cfg['classes']
    rec['class'] = 'MATCH' if score >= c['match'] else 'CLOSE' if score >= c['close'] else 'DIFFERENT'
    return rec
