#!/usr/bin/env python3
# SynthyBoy - the gain of an effect on a set of synthetic programs, as plain RMS and as the loudness measure of scripts/measure-loudness.py (docs/release/TIER-LEVEL-COMPENSATION.md).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   tier-makeup-programs.py --bin synthyboy_fx_makeup_calibrate MODULE "key=value key=value" ["key=value" ...] [--json OUT]
#
# For every settings string it runs the module (bench/fx_makeup_calibrate.cpp) on the programs noise, sawlow, sawmid, sawhigh, squaremid, sinemid, pluck, pad, wide and mono, and prints the
# gain of the output against the input in dB: `r` = plain RMS of the whole render, `l` = the loudest 100 ms, K-weighted (scripts/loudness.py fast_max). A model has to meet both.

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("loudness", HERE / "loudness.py")
ln = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ln)
SR = 44100
PROGRAMS = ["noise", "sawlow", "sawmid", "sawhigh", "squaremid", "sinemid", "pluck", "pad"]


def load(path):
    d = np.fromfile(path, dtype="<f4").astype(np.float64).reshape(-1, 2)
    return d[:, 0], d[:, 1]


def run(binary, module, settings, programs=PROGRAMS, widths=("stereo", "mono")):
    tmp = Path(tempfile.mkdtemp(prefix="tmp-"))
    jobs, tags = [], []
    for st in settings:
        for pr in programs:
            for w in widths:
                tag = f"{len(tags)}"
                jobs.append(f"{module} {w} program={pr} dump={tmp / tag} {st}")
                tags.append((st, pr, w, tag))
    r = subprocess.run([binary], input="\n".join(jobs) + "\n", capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    out = {}
    skip = int(4 * SR)
    models = [float(x.split()[3]) for x in r.stdout.strip().splitlines()]
    for (st, pr, w, tag), model in zip(tags, models):
        li, ri = load(tmp / (tag + ".in"))
        lo, ro = load(tmp / (tag + ".out"))
        pin = ((li[skip:] ** 2 + ri[skip:] ** 2) / 2).mean() + 1e-30
        pout = ((lo[skip:] ** 2 + ro[skip:] ** 2) / 2).mean() + 1e-30
        fi = ln.window_max(li, ri, SR, 0.1)
        fo = ln.window_max(lo, ro, SR, 0.1)
        out.setdefault(st, {})[f"{pr}/{w}"] = {"r": 10 * np.log10(pout / pin), "l": float(fo - fi), "m": model}
    for f in tmp.iterdir():
        f.unlink()
    tmp.rmdir()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--json", default=None)
    ap.add_argument("module")
    ap.add_argument("settings", nargs="+")
    a = ap.parse_args()
    res = run(a.bin, a.module, a.settings)
    for st, d in res.items():
        print(st)
        print("  " + " ".join(k.replace("/stereo", "/w").replace("/mono", "/m")[:11].rjust(11) for k in d))
        print("  r" + " ".join(f"{v['r']:11.2f}" for v in d.values()))
        print("  l" + " ".join(f"{v['l']:11.2f}" for v in d.values()))
        print("  m" + " ".join(f"{v['m']:11.2f}" for v in d.values()))
        el = [v["m"] - v["l"] for v in d.values()]
        er = [v["m"] - v["r"] for v in d.values()]
        print(f"  model - l: max {max(abs(x) for x in el):.2f} rms {np.sqrt(np.mean(np.square(el))):.2f}   model - r: max {max(abs(x) for x in er):.2f} rms {np.sqrt(np.mean(np.square(er))):.2f}")
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    sys.exit(main())
