#!/usr/bin/env python3
# SynthyBoy - level normalisation of the factory presets (writes scripts/factory-preset-levels.json).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   tune-preset-levels.py --render build/tools/preset-render/synthyboy_preset_render [--engine vital|own] [--target -20] [--max-peak -1.5] [--rounds 4] [--sustain-aware]
#
# Renders content/presets with synthyboy_preset_render (tier high), and moves master.volume of every preset so that the RMS of the
# loudest 300 ms of its four-note chord (velocity 1.0, the measure printed as chordRms) is the target; a preset whose chord peak
# would then exceed --max-peak (on tier high or on tier low, which skips some effects) is held back at that peak. master.volume is the last stage of the voice path, so the level follows
# it 1:1 in dB and a few rounds converge. Writes the levels file, regenerates the presets after every round, prints the result.

import argparse
import importlib.util
import json
import os
import subprocess
import sys
from typing import Dict, Optional, Set, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
LEVELS_PATH = os.path.join(SCRIPT_DIR, "factory-preset-levels.json")
GEN = os.path.join(SCRIPT_DIR, "gen-factory-presets.py")
PRESETS = os.path.join(ROOT_DIR, "content", "presets")


def sustain_hold_seconds(params: dict) -> float:
    """Derives chord hold time (seconds) from the preset's amp envelope times (delay + attack + 0.4s, min 1.0s, capped at 5.0s)."""
    att = max(float(params.get("env1.attack", 5)) + float(params.get("env1.delay", 0)), 0.0)
    return min(5.0, max(1.0, 0.4 + att / 1000.0))


def pinned_112_preset_names() -> Set[str]:
    """Return the set of preset names belonging to the original 112 pinned factory presets."""
    if os.path.isfile(GEN):
        spec = importlib.util.spec_from_file_location("gen_factory_presets", GEN)
        mod_ = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod_)
        return set(p[0] for p in mod_.presets) | set(s[0] for s in mod_.showcase)
    return set()


def run_render(binary: str, engine: str, tier: str, sustain_aware: bool = False) -> dict:
    cmd = [binary, "--presets", PRESETS, "--engine", engine, "--tier", tier]
    if sustain_aware:
        cmd.append("--sustain-aware")
    out = subprocess.run(cmd, capture_output=True, text=True, check=False).stdout
    rows = {}
    for line in out.splitlines():
        t = line.split()
        if len(t) >= 8 and t[0].endswith(".sbpreset"):
            rows[t[0]] = {"category": t[1], "peak": float(t[2]), "chord_peak": float(t[3]), "rms": float(t[4]), "chord_rms": float(t[5])}
    return rows


def tune_levels_round(
    rows: dict,
    low: dict,
    pinned_112: Set[str],
    levels: dict,
    target: float = -20.0,
    max_peak: float = -1.5,
    sustain_aware: bool = False,
    presets_dir: str = PRESETS,
    rows_sustain: Optional[dict] = None,
    low_sustain: Optional[dict] = None,
) -> Tuple[int, float]:
    """Perform one round of level adjustment. Returns (changed_count, worst_deviation)."""
    worst = 0.0
    changed = 0
    for fname, r_old in sorted(rows.items()):
        doc = json.load(open(os.path.join(presets_dir, r_old["category"], fname)))
        name = doc["meta"]["name"]
        current = float(doc["state"]["params"]["master.volume"])

        is_new = name not in pinned_112
        use_sustain = sustain_aware and is_new and rows_sustain and fname in rows_sustain

        r = rows_sustain[fname] if use_sustain else r_old
        r_low = (low_sustain.get(fname) if (use_sustain and low_sustain) else low.get(fname))

        delta = target - r["chord_rms"]
        delta = min(delta, max_peak - r["chord_peak"], max_peak - r["peak"])
        if r_low:
            delta = min(delta, max_peak - r_low["chord_peak"], max_peak - r_low["peak"])
        new = max(-60.0, min(6.0, round(current + delta, 1)))
        worst = max(worst, abs(target - r["chord_rms"]))
        if abs(new - current) >= 0.05:
            levels[name] = new
            changed += 1
    return changed, worst


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True)
    ap.add_argument("--engine", choices=("vital", "own"), default="vital")
    ap.add_argument("--target", type=float, default=-20.0)
    ap.add_argument("--max-peak", type=float, default=-1.5)
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--sustain-aware", action="store_true", help="use sustain-aware chord hold measure for new presets")
    args = ap.parse_args()

    levels = json.load(open(LEVELS_PATH)) if os.path.isfile(LEVELS_PATH) else {}
    pinned = pinned_112_preset_names()

    for rnd in range(args.rounds):
        subprocess.run([sys.executable, "-I", GEN], check=True, capture_output=True)
        rows = run_render(args.render, args.engine, "high", sustain_aware=False)
        low = run_render(args.render, args.engine, "low", sustain_aware=False)
        rows_sustain = run_render(args.render, args.engine, "high", sustain_aware=True) if args.sustain_aware else None
        low_sustain = run_render(args.render, args.engine, "low", sustain_aware=True) if args.sustain_aware else None

        changed, worst = tune_levels_round(
            rows=rows,
            low=low,
            pinned_112=pinned,
            levels=levels,
            target=args.target,
            max_peak=args.max_peak,
            sustain_aware=args.sustain_aware,
            presets_dir=PRESETS,
            rows_sustain=rows_sustain,
            low_sustain=low_sustain,
        )
        json.dump(levels, open(LEVELS_PATH, "w"), indent=1, sort_keys=True)
        open(LEVELS_PATH, "a").write("\n")
        print(f"round {rnd + 1}: {len(rows)} presets, {changed} master.volume changed, worst deviation before the change {worst:.1f} dB")
        if changed == 0:
            break
    subprocess.run([sys.executable, "-I", GEN], check=True, capture_output=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
