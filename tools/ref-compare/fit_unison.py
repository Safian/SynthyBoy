#!/usr/bin/env python3
"""SynthyBoy - ref-compare: measures the unison law of the real Serum 2 (and ours, for the comparison): the level of every voice by voice count and blend, and the stereo spread.

  fit_unison.py --root REPO --build BUILD --what levels|pan|detune [--json OUT]

levels: the default saw of the plug-in, Unison N, detune 1.0 (the outer voices at +-200 cents, linear), stereo spread 0, blend 0 .. 100 %: the amplitude of the line of every voice (Blackman-Harris
        window, the power around the line) relative to the single voice; printed per count and blend: the centre voice(s) and the outer voices. The result of the 656 renders:
        centre = c0(N) * sc(blend), outer = c0(N) * so(blend), c0 from 0.4384 (3 voices) to 0.3016 (16), 2 voices 0.320 each, shapes in core/engine-vital/.../synth_oscillator.cpp (kSerumCentre, kSerumOuter).
pan:    3 voices, blend 100, the stereo spread 0 .. 100 %: the gain of the low voice in the left and the right channel (kSerumPanOwn / kSerumPanOther).
detune: 3 voices, blend 100, the detune 0.02 .. 1 (file value): the outer voices in cents (200 * d ^ 1.25).
Nothing is committed with audio; the renders go to tests/serum-reference/experiments (gitignored).
"""
import argparse
import json
import math
import os
import sys

import numpy as np
from scipy.signal.windows import blackmanharris

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import experiment as ex  # noqa: E402

NOTES = '60:100:0:2.5'
F0 = 261.63


def spectrum(x, ch):
    seg = x[int(0.3 * 48000):int(2.3 * 48000), ch].astype(np.float64)
    w = blackmanharris(len(seg))
    return np.abs(np.fft.rfft(seg * w)) * 2 / w.sum(), np.fft.rfftfreq(len(seg), 1 / 48000)


def voice_amps(x, n, span=200.0, ch=0):
    m, f = spectrum(x, ch)
    out = []
    for i in range(n):
        t = (2 * i - (n - 1)) / (n - 1) if n > 1 else 0.0
        f0 = F0 * 2 ** ((span * t - 0.8) / 1200)
        tol = 2.0 if n == 1 else max(0.5, min(0.42 * F0 * (2 ** (span * 2 / (n - 1) / 1200) - 1), 2.0))
        sel = (f > f0 - tol) & (f < f0 + tol)
        out.append(float(np.sqrt(np.sum(m[sel] ** 2) / 2.0)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--what', default='levels')
    ap.add_argument('--json', default='')
    a = ap.parse_args()
    e = ex.Experiment(a.root, a.build)
    base = {'Global0.plainParams.kParamMasterVolume': 0.25}
    res = []
    if a.what == 'levels':
        ref = None
        for n in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16):
            for bl in ((0,) if n == 1 else (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100)):
                ed = dict(base, **{'Oscillator0.plainParams.kParamUnison': float(n), 'Oscillator0.plainParams.kParamDetune': 1.0, 'Oscillator0.plainParams.kParamDetuneWid': float(bl),
                                   'Oscillator0.plainParams.kParamUnisonStereo': 0.0})
                audio = e.render_serum(e.preset('fu_%d_%d' % (n, bl), ed), NOTES, 3.0)
                amps = voice_amps(audio, n)
                ref = ref or amps[0]
                k = n // 2
                centre = [amps[k - 1], amps[k]] if n % 2 == 0 and n > 1 else [amps[k]]
                outer = [amps[i] for i in range(n) if not (n > 1 and (i == k or (n % 2 == 0 and i == k - 1)))]
                res.append({'n': n, 'blend': bl, 'centre': [c / ref for c in centre], 'outer': [o / ref for o in outer]})
                print('N=%2d blend=%3d  centre %s  outer %s' % (n, bl, ' '.join('%.4f' % (c / ref) for c in centre), ' '.join('%.4f' % (o / ref) for o in outer[:2])), flush=True)
    elif a.what == 'pan':
        for sp in (0, 10, 25, 50, 75, 90, 100):
            ed = dict(base, **{'Oscillator0.plainParams.kParamUnison': 3.0, 'Oscillator0.plainParams.kParamDetune': 1.0, 'Oscillator0.plainParams.kParamDetuneWid': 100.0, 'Oscillator0.plainParams.kParamUnisonStereo': float(sp)})
            audio = e.render_serum(e.preset('fp_%d' % sp, ed), NOTES, 3.0)
            left, right = voice_amps(audio, 3, ch=0)[0], voice_amps(audio, 3, ch=1)[0]
            res.append({'spread': sp, 'left': left, 'right': right})
            print('spread %3d  low voice left %.5f right %.5f' % (sp, left, right), flush=True)
    elif a.what == 'detune':
        for d in (0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1.0):
            ed = dict(base, **{'Oscillator0.plainParams.kParamUnison': 3.0, 'Oscillator0.plainParams.kParamDetune': d, 'Oscillator0.plainParams.kParamDetuneWid': 100.0, 'Oscillator0.plainParams.kParamUnisonStereo': 0.0})
            audio = e.render_serum(e.preset('fd_%g' % d, ed), NOTES, 3.0)
            m, f = spectrum(audio, 0)
            pk = sorted(sorted([i for i in range(2, len(m) - 2) if 150 < f[i] < 420 and m[i] > m[i - 1] and m[i] >= m[i + 1]], key=lambda i: -m[i])[:3])
            cents = [round(1200 * math.log2(f[i] / F0), 1) for i in pk]
            res.append({'detune': d, 'cents': cents})
            print('detune %.2f  outer voices %s cents  (200 * d ^ 1.25 = %.1f)' % (d, cents, 200 * d ** 1.25), flush=True)
    if a.json:
        json.dump(res, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
