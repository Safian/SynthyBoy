#!/usr/bin/env python3
# SynthyBoy - peaks of every factory preset before and after the MPC output stage, and the loudness of the set at different make-up gains.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   measure-peaks.py --bin PATH/synthyboy_sound_lab [--profile xl] [--presets DIR] [--gains 18 12 9 6] --json OUT
#
# Per preset, from the raw engine output (the engine's own safety stage `outputLimit`, identity up to +-1.8 and tanh towards +-2 above, is already in it):
#   gesture_peak_db   the loudest sample of the loudness gesture of the category (scripts/measure-loudness.py, velocity 100)
#   chord4_v100 / chord4_v127   the peak of a four note chord (root, fifth, octave, major tenth; the root is C1 for Bass, C3 for the rest) held 1.5 s
#   chord8_v127       the peak of the eight note chord at velocity 127 (the worst case, the figure of scripts/sound-lab.py)
#   limiter_db        how far the engine's safety stage pulls the worst peak down (0 when it does not act): the peak p after the stage gives the unlimited peak x = 1.8 + 0.2 atanh((p - 1.8) / 0.2)
# For every make-up gain: how the gesture sounds after gain and a hard clip at 0 dBFS (what a host without a limiter does): the loudness (fast_max, scripts/loudness.py) and whether it clipped.

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ml = _load("measure_loudness", "measure-loudness.py")
ln = ml.ln
SR = ml.SR


def unlimited(p):
    """The peak before the engine's safety stage (identity up to 1.8, 1.8 + 0.2 tanh((x - 1.8) / 0.2) above) for a peak p after it."""
    if p <= 1.8:
        return p
    return 1.8 + 0.2 * math.atanh(min((p - 1.8) / 0.2, 0.999999))


def chord(cat, vel):
    r = 36 if cat == "Bass" else 60
    ks = [r, r + 7, r + 12, r + 16]
    return ml.shifted([f"on 0 {k} {vel}" for k in ks] + [f"off 1.5 {k}" for k in ks])


def run(binary, profile, presets_dir, gains, work=None):
    lab = ml.lab_mod.Lab(binary, work)
    files = sorted(Path(presets_dir).glob("*/*.sbpreset"))
    jobs = {}
    for f in files:
        d = json.load(open(f))
        cat = d["meta"]["category"]
        nat = 4.0 if cat == "FX" else None
        for tag, secs, items in ml.gesture(cat, d["state"]["params"], nat):
            jobs[(f, "g_" + tag)] = lab.job(f"{f.stem}_{profile}_g{tag}", str(f), profile, secs + ml.PRE, *ml.shifted(items))
        jobs[(f, "c4_100")] = lab.job(f"{f.stem}_{profile}_c4a", str(f), profile, 3.0 + ml.PRE, *chord(cat, 100 / 127))
        jobs[(f, "c4_127")] = lab.job(f"{f.stem}_{profile}_c4b", str(f), profile, 3.0 + ml.PRE, *chord(cat, 1.0))
        c8 = [48, 55, 60, 64, 67, 72, 76, 79]
        jobs[(f, "c8_127")] = lab.job(f"{f.stem}_{profile}_c8", str(f), profile, 3.5 + ml.PRE, *ml.shifted([f"on 0 {k} 1.0" for k in c8] + [f"off 1.5 {k}" for k in c8]))
    lab.run()
    res = {}
    for f in files:
        d = json.load(open(f))
        e = res.setdefault(d["meta"]["name"], {"category": d["meta"]["category"]})
        gest = [(t, lab.load(w)) for (ff, t), w in jobs.items() if ff == f and t.startswith("g_")]
        e["gesture_peak_db"] = max(ml.lab_mod.db(max(np.abs(l).max(), np.abs(r).max())) for _, (l, r) in gest)
        for tag in ("c4_100", "c4_127", "c8_127"):
            l, r = lab.load(jobs[(f, tag)])
            pk = max(np.abs(l).max(), np.abs(r).max())
            e[tag + "_peak_db"] = ml.lab_mod.db(pk)
            e[tag + "_peak"] = float(pk)
        e["limiter_db"] = max(0.0, 20 * math.log10(unlimited(max(e[t + "_peak"] for t in ("c4_100", "c4_127", "c8_127"))) / max(e[t + "_peak"] for t in ("c4_100", "c4_127", "c8_127"))))
        for g in gains:
            k = 10 ** (g / 20)
            vals, clipped = [], False
            for _, (l, r) in gest:
                cl, cr = np.clip(l * k, -1, 1), np.clip(r * k, -1, 1)
                clipped |= bool(np.abs(np.concatenate([l, r])).max() * k > 1.0)
                vals.append(ln.window_max(cl, cr, SR, ln.FAST_S))
            e[f"gain{g}_gesture_clipped"] = clipped
            e[f"gain{g}_fast_max"] = ln.power_mean(vals)
        for t in ("c4_100", "c4_127", "c8_127"):
            for g in gains:
                e[f"gain{g}_{t}_over"] = bool(e[t + "_peak"] * 10 ** (g / 20) > 1.0)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--profile", default="xl")
    ap.add_argument("--presets", default=str(ROOT / "content" / "presets"))
    ap.add_argument("--gains", type=int, nargs="+", default=[18, 12, 9, 6])
    ap.add_argument("--work", default=None)
    ap.add_argument("--json", required=True)
    a = ap.parse_args()
    res = run(a.bin, a.profile, a.presets, a.gains, a.work)
    json.dump(res, open(a.json, "w"), indent=1, sort_keys=True, default=float)
    print(f"{len(res)} presets")


if __name__ == "__main__":
    sys.exit(main())
