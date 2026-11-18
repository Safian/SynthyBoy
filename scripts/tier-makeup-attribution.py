#!/usr/bin/env python3
# SynthyBoy - which effect of the quality tier `low` moves the loudness of a preset (docs/release/TIER-LEVEL-COMPENSATION.md, section 1).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   tier-makeup-attribution.py --bin PATH/synthyboy_sound_lab --out DIR [--names "Reese Dual Phase Bass,..."] [--extra PROFILE ...]
#
# Renders the loudness gesture of scripts/measure-loudness.py for the chosen presets under: the XL profile, `low`, `low` with all effects (`low;fx=all`: the residual of the other
# tier settings), the XL profile with ONE effect bypassed at a time (the lab's fxoff= key) and with all five that `low` bypasses. Prints the loudest 100 ms (fast_max, LUFS) of each
# and the difference to the XL profile in LU; writes DIR/attribution.json.

import argparse
import importlib.util
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
spec = importlib.util.spec_from_file_location("measure_loudness", HERE / "measure-loudness.py")
ml = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ml)

NINE = ["Reese Dual Phase Bass", "Dusty Rhodes Keys", "Bode Barber Riser", "Hyper Dimension Pad", "Bode Shifter Drone", "Cathedral Choir", "Lush Analog Pad", "Frozen Pluck Drone", "Marimba Layer Keys"]
XL = "high;voices=16;control=economy"
EFFECTS = ["reverb", "flanger", "hyperdim", "bode", "convolve"]


def profiles(extra):
    p = {"xl": XL, "low": "low", "low_fxall": "low;fx=all"}
    for e in EFFECTS:
        p["xl_off_" + e] = f"{XL};fxoff={e}"
    p["xl_off_all5"] = f"{XL};fxoff={'+'.join(EFFECTS)}"
    for i, e in enumerate(extra or []):
        p[f"extra{i}"] = e
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--names", default=",".join(NINE), help="the presets to measure; 'all' = every factory preset (the table is then only written to the JSON)")
    ap.add_argument("--extra", action="append")
    a = ap.parse_args()
    every = a.names == "all"
    names = None if every else [n.strip() for n in a.names.split(",")]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="tmk-"))
    pdir = tmp / "presets"
    for f in sorted((ROOT / "content" / "presets").glob("*/*.sbpreset")):
        if every or json.load(open(f))["meta"]["name"] in names:
            (pdir / f.parent.name).mkdir(parents=True, exist_ok=True)
            (pdir / f.parent.name / f.name).symlink_to(f)
    res = {}
    for tag, prof in profiles(a.extra).items():
        res[tag] = ml.measure(a.bin, prof, pdir, tmp / ("w-" + tag))
        print("measured", tag, file=sys.stderr)
    json.dump(res, open(out / "attribution.json", "w"), indent=1, sort_keys=True, default=float)
    tags = list(res)
    if every:
        shutil.rmtree(tmp, ignore_errors=True)
        return
    print("preset".ljust(24) + "xl LUFS".rjust(9) + "".join(t.replace("xl_off_", "-").rjust(10) for t in tags[1:]))
    for n in names:
        base = res["xl"][n]["fast_max"]
        print(n.ljust(24) + f"{base:9.2f}" + "".join(f"{res[t][n]['fast_max'] - base:+10.2f}" for t in tags[1:]))
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
