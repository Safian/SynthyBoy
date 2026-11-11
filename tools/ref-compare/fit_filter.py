#!/usr/bin/env python3
"""SynthyBoy - ref-compare: fits our filter to the real Serum 2 filter, type by type (controlled single-parameter experiments, nothing committed with audio).

  fit_filter.py --root REPO --build BUILD --type MgL12 [--freq 0.5,0.65] [--reso 0,15,30,50,75,100] [--drive 0] [--json OUT]

A crafted preset (the init state of the plug-in, filter 1 on, the given type / frequency / resonance / drive) is rendered by the real Serum 2; ours is rendered once per candidate
of `filter1.resonance` (0 .. 100 step 5, then 1 step around the best) in one manifest, and the candidate whose harmonic profile (the first 40 harmonics of the C3 saw) is closest
after removing the mean level is the answer. Printed per point: the Serum resonance, the best resonance of ours, the level offset (dB, Serum - ours) and the residual (dB) of the best
candidate and of the importer's current mapping (resonance 1:1). A residual over 3 dB means the filter structure itself differs (not only a scale).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import experiment as ex  # noqa: E402

NOTES = '60:100:0:2.5'
LENGTH = 3.0


def profile(x, n=40):
    return ex.harmonics_db(x, n=n)


def residual(a, b):
    """(level offset a - b in dB, mean absolute residual) over the harmonics that are audible in both."""
    ok = (a > -95) & (b > -95)
    if ok.sum() < 6:
        return 0.0, 99.0
    d = a[ok] - b[ok]
    off = float(np.median(d))
    return off, float(np.mean(np.abs(d - off)))


def render_ours_many(e, sb, rows):
    """rows = [(tag, override string)] -> {tag: audio}"""
    outdir = os.path.join(e.work, 'wav', 'fit')
    os.makedirs(outdir, exist_ok=True)
    mf = tempfile.NamedTemporaryFile('w', suffix='.tsv', delete=False)
    for tag, ov in rows:
        mf.write('\t'.join([tag, sb, NOTES, str(LENGTH), os.path.join(outdir, tag + '.wav')] + ([ov] if ov else [])) + '\n')
    mf.close()
    r = subprocess.run([e.ours_bin, '--manifest', mf.name, '--user-dir', os.path.join(e.work, 'imported')], capture_output=True, text=True)
    os.unlink(mf.name)
    if 'FAIL' in r.stdout:
        raise RuntimeError(r.stdout[-300:])
    return {tag: ex.read(os.path.join(outdir, tag + '.wav')) for tag, _ in rows}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--type', required=True, help='one Serum filter type name, or a comma separated list')
    ap.add_argument('--freq', default='0.5,0.65')
    ap.add_argument('--reso', default='0,15,30,50,75,100')
    ap.add_argument('--drive', default='0')
    ap.add_argument('--json', default='')
    ap.add_argument('--identity-only', action='store_true', help='only the level offset and the residual of the importer mapping as it is (resonance 1:1)')
    a = ap.parse_args()
    e = ex.Experiment(a.root, a.build)
    results = []
    for typ in a.type.split(','):
        for f in [float(x) for x in a.freq.split(',')]:
            for d in [float(x) for x in a.drive.split(',')]:
                for r in [float(x) for x in a.reso.split(',')]:
                    edits = {'Global0.plainParams.kParamMasterVolume': 0.25, 'VoiceFilter0.plainParams.kParamEnable': 1.0, 'VoiceFilter0.plainParams.kParamType': typ,
                             'VoiceFilter0.plainParams.kParamFreq': f, 'VoiceFilter0.plainParams.kParamReso': r, 'VoiceFilter0.plainParams.kParamDrive': d}
                    p = e.preset('fit_%s_%g_%g_%g' % (typ, f, r, d), edits)
                    ref = e.render_serum(p, NOTES, LENGTH)
                    e.render_ours(p, NOTES, LENGTH)   # imports the preset (writes the .sbpreset)
                    sb = os.path.join(e.work, 'imported', os.path.splitext(os.path.basename(p))[0] + '.sbpreset')
                    href = profile(ref)
                    tagb = 'c_%s_%g_%g_%g' % (typ, f, r, d)
                    ident = render_ours_many(e, sb, [(tagb + '_i', '')])[tagb + '_i']
                    ioff, ires = residual(href, profile(ident))
                    rmsoff = ex.rms_db(ref, 0.4, 2.0) - ex.rms_db(ident, 0.4, 2.0)   # the level difference of the sustain (Serum - ours): what the loudness metrics see
                    best = (ires, int(round(r)), ioff)
                    scores = [best]
                    if ires > 1.5 and not a.identity_only:   # the shape differs: look for the resonance of ours that gives the Serum shape
                        coarse = [(tagb + '_%d' % c, 'filter1.resonance=%d' % c) for c in range(0, 101, 5)]
                        aud = render_ours_many(e, sb, coarse)
                        for tag, ov in coarse:
                            c = int(ov.split('=')[1])
                            off, res = residual(href, profile(aud[tag]))
                            scores.append((res, c, off))
                        best = min(scores)
                        fine = [(tagb + '_f%d' % c, 'filter1.resonance=%d' % c) for c in range(max(0, best[1] - 4), min(100, best[1] + 4) + 1) if c != best[1]]
                        aud2 = render_ours_many(e, sb, fine)
                        for tag, ov in fine:
                            c = int(ov.split('=')[1])
                            off, res = residual(href, profile(aud2[tag]))
                            scores.append((res, c, off))
                        best = min(scores)
                    cur = (ires, int(round(r)), ioff)
                    rec = {'type': typ, 'freq': f, 'drive': d, 'serum_reso': r, 'best_reso': best[1], 'best_offset_db': round(best[2], 2), 'best_residual_db': round(best[0], 2),
                           'identity_reso': cur[1], 'identity_offset_db': round(cur[2], 2), 'identity_residual_db': round(cur[0], 2), 'identity_rms_offset_db': round(rmsoff, 2)}
                    results.append(rec)
                    print('%-10s f=%.2f drive=%3g  serum reso %3g -> ours %3d (offset %+5.1f dB, residual %4.1f)   identity: offset %+5.1f dB residual %4.1f   rms offset %+5.1f dB' % (
                        typ, f, d, r, best[1], best[2], best[0], cur[2], cur[0], rmsoff), flush=True)
    if a.json:
        json.dump(results, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
