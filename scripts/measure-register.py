#!/usr/bin/env python3
# SynthyBoy - the register of every factory preset: the lowest strong partial of single notes against the played note.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   measure-register.py --bin PATH/synthyboy_sound_lab [--profile xl] [--notes 36 48 60] [--json OUT] [--presets DIR] [PRESET.sbpreset ...]
#
# For each preset and note (velocity 100, held 1.6 s, the spectrum of 0.05 .. 1.5 s after the note on, 2^17 point FFT, Hann): the peaks of the magnitude spectrum; the loudest peak; the lowest peak within 20 dB of it ("lowest strong
# partial": the fundamental of a harmonic sound; the lowest mode of a bell); the number of octaves of that partial above the played note (rounded, "oct"; the exact figure in "oct_exact"),
# and the deviation of the partial from the nearest octave of the played note in cents ("cents": a detuned unison, a chorus, an inharmonic sound show it large; it counts only for a clear peak).
# Also the fundamental against the played note ("fund_db": the level of the played note's own frequency, within +-30 cents, against the loudest peak): a bass whose fundamental is weak shows it.
# The expectation of a category: Bass: the lowest strong partial at or below the played note (oct <= 0); everything else: at the played note (oct == 0).

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path

import numpy as np
from scipy.signal import find_peaks

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
spec = importlib.util.spec_from_file_location("sound_lab", HERE / "sound-lab.py")
lab_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab_mod)
SR = lab_mod.SR
NFFT = 1 << 17


def analyse(l, r, note, t0=0.05, t1=1.5):
    x = 0.5 * (l + r)[int(t0 * SR):int(t1 * SR)]
    if len(x) < 4096 or np.abs(x).max() < 1e-7:
        return None
    seg = np.zeros(NFFT)
    seg[:len(x)] = x * np.hanning(len(x))
    mag = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(NFFT, 1 / SR)
    db = 20 * np.log10(np.maximum(mag, 1e-12))
    f0 = 440.0 * 2 ** ((note - 69) / 12)
    sel = (f > 15) & (f < 12000)
    top = db[sel].max()
    pk, _ = find_peaks(np.where(sel, db, -300), height=top - 20, distance=int(3 * NFFT / SR / 2) or 1, prominence=6)
    if len(pk) == 0:
        return None
    loud = pk[np.argmax(db[pk])]
    low = pk[0]
    # a noise-like sound has no pitch: the loudest peak is not 15 dB over the median of the band around it
    med = np.median(db[(f > 0.5 * f[loud]) & (f < 2 * f[loud])])
    pitched = (db[loud] - med) > 15
    oct_exact = math.log2(f[low] / f0)
    k = round(oct_exact)
    cents = 1200 * (oct_exact - k)
    own = (f > f0 * 2 ** (-30 / 1200)) & (f < f0 * 2 ** (30 / 1200))
    fund_db = float(db[own].max() - top) if own.any() else -99.0
    return {"low_hz": float(f[low]), "loud_hz": float(f[loud]), "oct": int(k), "oct_exact": float(oct_exact), "cents": float(cents), "loud_oct": float(math.log2(f[loud] / f0)), "fund_db": fund_db, "pitched": bool(pitched), "peaks": int(len(pk))}


def measure(binary, profile, files, notes, work=None, overrides=None):
    lab = lab_mod.Lab(binary, work)
    jobs = {}
    for f in files:
        extra = [f"set {k} {v}" for k, v in (overrides or {}).get(Path(f).stem, {}).items()]
        for n in notes:
            jobs[(str(f), n)] = lab.job(f"{Path(f).stem}_{profile}_r{n}", str(f), profile, 2.0, *extra, f"on 0 {n} {100 / 127}", f"off 1.6 {n}")
    lab.run()
    out = {}
    for (f, n), wav in jobs.items():
        l, r = lab.load(wav)
        d = json.load(open(f))
        e = out.setdefault(d["meta"]["name"], {"category": d["meta"]["category"], "notes": {}})
        e["notes"][n] = analyse(l, r, n)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("presets", nargs="*")
    ap.add_argument("--bin", required=True)
    ap.add_argument("--profile", default="xl")
    ap.add_argument("--notes", type=int, nargs="+", default=[36, 48, 60])
    ap.add_argument("--presets-dir", default=str(ROOT / "content" / "presets"))
    ap.add_argument("--work", default=None)
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    files = a.presets or sorted(Path(a.presets_dir).glob("*/*.sbpreset"))
    res = measure(a.bin, a.profile, files, a.notes, a.work)
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1, sort_keys=True)
    for name, e in sorted(res.items()):
        cells = []
        for n, m in e["notes"].items():
            cells.append(f"{n}: none" if m is None else f"{n}: low {m['low_hz']:.0f} Hz oct {m['oct']:+d} ({m['cents']:+.0f} c) loud {m['loud_oct']:+.1f} fund {m['fund_db']:.0f} dB{'' if m['pitched'] else ' noisy'}")
        print(f"{name} [{e['category']}]  " + "  |  ".join(cells))


if __name__ == "__main__":
    sys.exit(main())
