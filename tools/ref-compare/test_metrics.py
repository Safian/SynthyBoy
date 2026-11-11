#!/usr/bin/env python3
"""SynthyBoy - ref-compare: self test of the comparison on synthetic signals (no Serum, no engine). Exit 77 (ctest SKIP) when numpy / scipy are missing.

An identical pair scores 100 and is a MATCH; the broken variants of a signal (50 cents off, level -6 dB, a lowpass three octaves down, one partial muted, a shifted onset) all
score clearly lower; the time alignment finds a known offset; the LUFS of a 997 Hz stereo sine of -20 dBFS peak reads -20.0; two different noise
renders have a larger spectral distance than identical ones.
"""
import os
import sys

try:
    import numpy as np
    from scipy import signal
except ImportError:
    print('numpy / scipy missing: skipped')
    sys.exit(77)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import metrics  # noqa: E402
import score  # noqa: E402

SR = 48000
cfg = score.load_config(os.path.join(HERE, 'scoring.json'))
failures = []


def check(name, cond, detail=''):
    if not cond:
        failures.append(name)
    print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, detail))


def tone(f, partials=(1.0, 0.5, 0.25), gain=0.3, off=2.5, total=4.0):
    t = np.arange(int(total * SR)) / SR
    x = sum(a * np.sin(2 * np.pi * f * (i + 1) * t) for i, a in enumerate(partials))
    env = np.where(t < off, 1.0, np.exp(-(t - off) * 6.0)) * np.minimum(1.0, t / 0.01)
    y = (gain * x * env).astype(np.float32)
    return np.stack([y, y * 0.9], 1)


def scored(ref, ours, **kw):
    rec = {'id': 'x', 'script': 'c3', 'rel': 'x'}
    rec.update(metrics.compare_pair(ref, ours, 0.0, 2.5, 4.0))
    rec['ours_silent'] = metrics.peak_db(ours) < -80.0
    return score.apply(rec, cfg)


ref = tone(261.63)
same = scored(ref, ref.copy())
check('identical scores 100', same['score'] > 99.9 and same['class'] == 'MATCH', same['score'])
variants = {
    'cents50': tone(261.63 * 2 ** (50 / 1200)),
    'level-6dB': ref * 0.5,
    'partial_muted': tone(261.63, partials=(1.0, 0.0, 0.25)),
    'lowpass_300Hz': signal.lfilter(*signal.butter(4, 300 / (SR / 2)), ref, axis=0).astype(np.float32),
    'octave_up': tone(523.26),
}
for name, v in variants.items():
    r = scored(ref, v)
    check('variant %s scores clearly lower' % name, r['score'] < same['score'] - 10.0 and r['class'] != 'MATCH', '%.1f (%s)' % (r['score'], r['class']))
silent = scored(ref, np.zeros_like(ref))
check('silent ours scores 0', silent['score'] == 0.0 and silent['class'] == 'DIFFERENT', silent['score'])

# alignment: ours is 7 ms late
late = np.concatenate([np.zeros((336, 2), np.float32), ref])[: len(ref)]
off, ok = metrics.estimate_offset(ref, late)
check('offset found (7 ms = 336 samples +- 2 ms)', ok and abs(off - 336) <= 96, off)
back = metrics.shift(late, off, len(ref))
r = scored(ref, back)
check('aligned late copy is a MATCH', r['class'] == 'MATCH', r['score'])

# loudness: BS.1770 is calibrated so that a 997 Hz sine of -20 dBFS peak in both channels reads -20.0 LUFS (the K-weighting gain at 997 Hz cancels the -0.691 constant)
t = np.arange(10 * SR) / SR
s = (0.1 * np.sin(2 * np.pi * 997 * t)).astype(np.float32)
l = metrics.lufs(np.stack([s, s], 1))
check('LUFS of a -20 dBFS stereo 997 Hz sine is -20.0', abs(l + 20.0) < 0.1, '%.2f' % l)

# noise: two noise renders differ in the spectrogram, a copy does not
rng = np.random.RandomState(1)
n1 = (0.1 * rng.randn(len(ref), 2)).astype(np.float32)
n2 = (0.1 * rng.randn(len(ref), 2)).astype(np.float32)
check('mel distance of different noise > 0.5 dB, of a copy 0', metrics.mel_distance(metrics.mono(n1), metrics.mono(n2)) > 0.5 and metrics.mel_distance(metrics.mono(n1), metrics.mono(n1)) == 0.0)

# pitch
pc = metrics.pitch_comparison(ref, variants['cents50'], 0.3, 2.4)
check('f0 error of the +50 cents variant is 50 +- 3 cents', pc is not None and abs(abs(pc[0]) - 50.0) < 3.0, pc[0] if pc else None)

print('%d failure(s)' % len(failures))
sys.exit(1 if failures else 0)
