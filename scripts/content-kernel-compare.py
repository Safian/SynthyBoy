#!/usr/bin/env python3
# SynthyBoy - compares two renders of the factory presets (synthyboy_preset_render --wav), one with the block kernels of the content oscillators and one with the reference code
# (--scalar-content): per preset the renders are the same, or the difference is stated as level, as size of the difference signal and per third octave band.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/content-kernel-compare.py KERNEL_DIR REFERENCE_DIR [--tier low] [--all]
#   KERNEL_DIR / REFERENCE_DIR  the --wav folders of the two renders (<dir>/<tier>/<preset>.wav)
#   --all                       list the presets whose renders are identical too (default: only a count)
# Columns: the RMS of the reference render (dBFS), the level difference of the two renders (dB), the RMS of the difference signal relative to the reference RMS (dB: -140 is rounding noise of
# the float samples), the largest difference relative to the peak of the reference (dB), and the largest difference of the level of a third octave band (dB, bands within 60 dB of the loudest).
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch


def load(path):
    rate, data = wavfile.read(path)
    scale = 1.0
    if data.dtype.kind == "i":
        scale = 1.0 / float(2 ** (8 * data.dtype.itemsize - 1))
    elif data.dtype.kind == "u":
        scale = 1.0 / 128.0
    data = data.astype(np.float64) * scale
    if data.ndim == 2:
        data = data.mean(axis=1)
    return rate, data


def third_octave(x, rate):
    f, p = welch(x, fs=rate, nperseg=8192)
    centres = 1000.0 * 2.0 ** (np.arange(-17, 13) / 3.0)
    out = []
    for c in centres:
        lo, hi = c / 2.0 ** (1 / 6), c * 2.0 ** (1 / 6)
        if hi > rate / 2:
            break
        sel = (f >= lo) & (f < hi)
        out.append(10 * np.log10(p[sel].sum() + 1e-30))
    return np.array(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    tier = sys.argv[sys.argv.index("--tier") + 1] if "--tier" in sys.argv else "low"
    if tier in args:
        args.remove(tier)
    if len(args) < 2:
        print(__doc__)
        return 1
    kdir, rdir = os.path.join(args[0], tier), os.path.join(args[1], tier)
    names = sorted(n for n in os.listdir(kdir) if n.endswith(".wav"))
    rows, same = [], 0
    for n in names:
        rate, a = load(os.path.join(kdir, n))
        _, b = load(os.path.join(rdir, n))
        m = min(len(a), len(b))
        a, b = a[:m], b[:m]
        if np.array_equal(a, b):
            same += 1
            if "--all" in sys.argv:
                rows.append((n, 0.0, 0.0, -999.0, -999.0, 0.0))
            continue
        rms_b = np.sqrt(np.mean(b ** 2)) + 1e-30
        rms_a = np.sqrt(np.mean(a ** 2)) + 1e-30
        d = a - b
        rms_d = np.sqrt(np.mean(d ** 2)) + 1e-30
        peak = np.max(np.abs(b)) + 1e-30
        ta, tb = third_octave(a, rate), third_octave(b, rate)
        loud = tb.max()
        mask = tb > loud - 60
        bands = float(np.max(np.abs(ta[mask] - tb[mask]))) if mask.any() else 0.0
        rows.append((n, 20 * np.log10(rms_b), 20 * np.log10(rms_a / rms_b), 20 * np.log10(rms_d / rms_b), 20 * np.log10(np.max(np.abs(d)) / peak), bands))
    print(f"tier {tier}: {len(names)} presets, {same} with identical samples, {len(names) - same} different")
    if rows:
        print(f"{'preset':44s} {'RMS dBFS':>9s} {'level dB':>9s} {'diff/RMS dB':>12s} {'max/peak dB':>12s} {'band dB':>8s}")
        for r in sorted(rows, key=lambda r: -r[3]):
            print(f"{r[0][:44]:44s} {r[1]:9.2f} {r[2]:9.4f} {r[3]:12.1f} {r[4]:12.1f} {r[5]:8.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
