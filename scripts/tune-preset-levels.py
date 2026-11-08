#!/usr/bin/env python3
# SynthyBoy - level normalisation of the factory presets (writes scripts/factory-preset-levels.json).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   tune-preset-levels.py --render build/tools/preset-render/synthyboy_preset_render [--target -29] [--max-peak -1.5] [--rounds 4]
#
# Renders content/presets with synthyboy_preset_render (tier high), and moves master.volume of every preset so that the RMS of the
# loudest 300 ms of its four-note chord (velocity 1.0, the measure printed as chordRms) is the target; a preset whose chord peak
# would then exceed --max-peak is held back at that peak. master.volume is the last stage of the voice path, so the level follows
# it 1:1 in dB and a few rounds converge. Writes the levels file, regenerates the presets after every round, prints the result.

import argparse
import json
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
LEVELS_PATH = os.path.join(SCRIPT_DIR, "factory-preset-levels.json")
GEN = os.path.join(SCRIPT_DIR, "gen-factory-presets.py")
PRESETS = os.path.join(ROOT_DIR, "content", "presets")


def run_render(binary: str) -> dict:
    out = subprocess.run([binary, "--presets", PRESETS, "--tier", "high"], capture_output=True, text=True, check=False).stdout
    rows = {}
    for line in out.splitlines():
        t = line.split()
        if len(t) >= 8 and t[0].endswith(".sbpreset"):
            rows[t[0]] = {"category": t[1], "peak": float(t[2]), "chord_peak": float(t[3]), "rms": float(t[4]), "chord_rms": float(t[5])}
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True)
    ap.add_argument("--target", type=float, default=-29.0)
    ap.add_argument("--max-peak", type=float, default=-1.5)
    ap.add_argument("--rounds", type=int, default=4)
    args = ap.parse_args()

    levels = json.load(open(LEVELS_PATH)) if os.path.isfile(LEVELS_PATH) else {}

    for rnd in range(args.rounds):
        subprocess.run([sys.executable, "-I", GEN], check=True, capture_output=True)
        rows = run_render(args.render)
        worst = 0.0
        changed = 0
        for fname, r in sorted(rows.items()):
            doc = json.load(open(os.path.join(PRESETS, r["category"], fname)))
            name = doc["meta"]["name"]
            current = float(doc["state"]["params"]["master.volume"])
            delta = args.target - r["chord_rms"]
            delta = min(delta, args.max_peak - r["chord_peak"], args.max_peak - r["peak"])
            new = max(-60.0, min(6.0, round(current + delta, 1)))
            worst = max(worst, abs(args.target - r["chord_rms"]))
            if abs(new - current) >= 0.05:
                levels[name] = new
                changed += 1
        json.dump(levels, open(LEVELS_PATH, "w"), indent=1, sort_keys=True)
        open(LEVELS_PATH, "a").write("\n")
        print(f"round {rnd + 1}: {len(rows)} presets, {changed} master.volume changed, worst deviation before the change {worst:.1f} dB")
        if changed == 0:
            break
    subprocess.run([sys.executable, "-I", GEN], check=True, capture_output=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
