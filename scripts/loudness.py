#!/usr/bin/env python3
# SynthyBoy - perceptual loudness (ITU-R BS.1770-4: K-weighting, 400 ms blocks, gating) of the renders of the sound lab.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# numpy / scipy only. Everything is in LUFS: a 997 Hz sine of -23 dBFS in both channels reads -23.0 LUFS (the constant -0.691 of the standard is included).
#
#   k_weight(l, r, sr)                -> the two channels after the pre-filter (high shelf +4 dB at 1.68 kHz) and the RLB high-pass (38 Hz)
#   momentary(l, r, sr)               -> (times, LUFS) of 400 ms blocks every 100 ms (75 % overlap), no gate
#   short_term(l, r, sr)              -> (times, LUFS) of 3 s windows every 100 ms (the render is padded with silence to at least 3 s: a 0.4 s note then reads 8.7 dB under its energy)
#   integrated(l, r, sr)              -> gated loudness: blocks under -70 LUFS dropped, then blocks more than 10 LU under the mean of the rest dropped (BS.1770-4)
#   window_max(l, r, sr, win_s)       -> the loudest window of any length (the factory presets use 100 ms: FAST_S)
#   loudness_measures(l, r, sr)       -> dict: fast_max (100 ms), momentary_max, short_term_max, integrated (all LUFS; -200 for silence)

import math

import numpy as np
from scipy.signal import lfilter

FLOOR = -200.0
FAST_S = 0.1   # the integration time of the ear for a short sound: the loudest 100 ms (see window_max)


def k_coefficients(sr):
    """The two biquads of BS.1770 for any sample rate (the standard gives them for 48 kHz; these are the analogue prototype with the bilinear transform: the same figures at 48 kHz)."""
    g, q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    k = math.tan(math.pi * fc / sr)
    vh = 10 ** (g / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    shelf_b = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0]
    shelf_a = [1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    q2, fc2 = 0.5003270373253953, 38.13547087613982
    k = math.tan(math.pi * fc2 / sr)
    a0 = 1 + k / q2 + k * k
    hp_b = [1.0, -2.0, 1.0]
    hp_a = [1.0, 2 * (k * k - 1) / a0, (1 - k / q2 + k * k) / a0]
    return (shelf_b, shelf_a), (hp_b, hp_a)


def k_weight(l, r, sr):
    (sb, sa), (hb, ha) = k_coefficients(sr)
    out = []
    for x in (l, r):
        y = lfilter(sb, sa, np.asarray(x, dtype=np.float64))
        out.append(lfilter(hb, ha, y))
    return out[0], out[1]


def _windows(l, r, sr, win_s, hop_s, min_len_s=0.0):
    """Mean square of the K-weighted channels (L + R summed, channel weights 1) in windows of win_s every hop_s; the signal is padded with silence to min_len_s and to a whole window."""
    kl, kr = k_weight(l, r, sr)
    p = kl * kl + kr * kr
    win = int(round(win_s * sr))
    hop = int(round(hop_s * sr))
    n = max(len(p), int(round(min_len_s * sr)), win)
    if n > len(p):
        p = np.concatenate([p, np.zeros(n - len(p))])
    c = np.concatenate([[0.0], np.cumsum(p)])
    starts = np.arange(0, n - win + 1, hop)
    z = (c[starts + win] - c[starts]) / win
    return (starts + win / 2) / sr, z


def _lufs(z):
    return np.where(z > 1e-20, -0.691 + 10 * np.log10(np.maximum(z, 1e-20)), FLOOR)


def momentary(l, r, sr):
    t, z = _windows(l, r, sr, 0.4, 0.1)
    return t, _lufs(z)


def short_term(l, r, sr):
    t, z = _windows(l, r, sr, 3.0, 0.1)
    return t, _lufs(z)


def integrated(l, r, sr):
    _, z = _windows(l, r, sr, 0.4, 0.1)
    lk = _lufs(z)
    z1 = z[lk > -70.0]
    if len(z1) == 0:
        return FLOOR
    rel = -0.691 + 10 * math.log10(z1.mean()) - 10.0
    z2 = z[(lk > -70.0) & (lk > rel)]
    if len(z2) == 0:
        return FLOOR
    return -0.691 + 10 * math.log10(z2.mean())


def loudness_measures(l, r, sr):
    return {"fast_max": window_max(l, r, sr, FAST_S), "momentary_max": float(momentary(l, r, sr)[1].max()), "short_term_max": float(short_term(l, r, sr)[1].max()), "integrated": float(integrated(l, r, sr))}


def power_mean(values):
    """Mean of loudness values in the energy domain (what playing the notes one after the other adds up to)."""
    v = [x for x in values if x > FLOOR]
    if not v:
        return FLOOR
    return 10 * math.log10(sum(10 ** (x / 10) for x in v) / len(values))


def window_max(l, r, sr, win_s, hop_s=0.01):
    """Loudest window of win_s seconds (LUFS), the window slid in hop_s steps; the signal is not padded (a sound shorter than the window is measured over its length plus the silence after it in the render)."""
    _, z = _windows(l, r, sr, win_s, hop_s)
    return float(_lufs(z).max())
