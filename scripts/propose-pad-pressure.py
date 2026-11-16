#!/usr/bin/env python3
# SynthyBoy - which factory presets get a pad pressure route of their own (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   propose-pad-pressure.py [--presets DIR] [--data FILE] [--proposal FILE] [--rederive] [--write] [--check]
#
# Pad pressure is the preset's business: the plugin-wide layer of the shell starts OFF, and a sound that should follow the pressure of the pad carries a modulation route
# Poly Aftertouch -> Filter 1 Cutoff in its own matrix. This script proposes WHICH presets get one and HOW MUCH, from the category and the shape of the amplitude envelope (Env 1):
#   - never: Bass, Pluck (percussive, the pressure of a hit is not played), and every preset whose filter 1 is off or that routes Poly Aftertouch to the cutoff already;
#   - held sounds only: sustain of Env 1 at least 60 % (a pluck-like envelope, a sustain under that, says "struck", not "held");
#   - Pad 40 %, Pad with an attack of 800 ms or more 45 % (a slow swell is played with the hand on the pad), Lead 30 %, Keys 25 %, FX 30 % (a drone: attack of 500 ms or more; other FX never);
#   - DEMO presets (a few that show it off, listed in DEMOS) 70 %.
# The result is DATA, scripts/factory-preset-pad-pressure.json ({"presets": {name: percent}}): an entry that exists is kept when the script runs again (the owner corrects it there), only
# presets without an entry are proposed unless --rederive; a preset the rules do not propose and that has no entry has none. scripts/gen-factory-presets.py writes the route into the preset
# (--write runs it). --proposal writes the Markdown list for the owner. --check: the data names presets that exist, and the presets are what the generator makes of it.

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRESETS_DIR = ROOT / "content" / "presets"
DATA_FILE = ROOT / "scripts" / "factory-preset-pad-pressure.json"
PARAMS_JSON = ROOT / "docs" / "params.json"
FORMAT = "synthyboy-factory-pad-pressure"
AMOUNT = {"Pad": 40, "Lead": 30, "Keys": 25, "FX": 30}
SLOW_PAD_ATTACK_MS, SLOW_PAD_AMOUNT = 800, 45
DRONE_ATTACK_MS = 500
MIN_SUSTAIN = 60
DEMO_AMOUNT = 70
DEMOS = ("Lush Analog Pad", "Vintage Brass Pad", "Hyper Saw Lead", "Rotary Rock Organ")


def routes_pressure(params, doc):
    """True when a slot of the preset sends Poly Aftertouch to the cutoff of Filter 1 (and drives something)."""
    src = {s["key"]: s["choice"] for s in doc["modSources"]}["poly_aftertouch"]
    dst = {d["key"]: d["choice"] for d in doc["modDestinations"]}["filter1.cutoff"]
    for n in range(1, 65):
        p = f"mod{n:02d}."
        if int(params.get(p + "source", 0)) == src and int(params.get(p + "destination", 0)) == dst and params.get(p + "amount", 0) != 0 and not params.get(p + "bypass", 0):
            return True
    return False


def propose_one(category, params, name, already):
    """(percent or 0, reason) for one preset."""
    if category in ("Bass", "Pluck"):
        return 0, "percussive category"
    if already:
        return 0, "routes Poly Aftertouch to the cutoff itself"
    if not params.get("filter1.on", 0):
        return 0, "Filter 1 is off"
    sustain, attack = params.get("env1.sustain", 100), params.get("env1.attack", 0)
    if sustain < MIN_SUSTAIN:
        return 0, f"struck, not held (sustain {sustain:g} %)"
    if category == "FX" and attack < DRONE_ATTACK_MS:
        return 0, "FX that is not a drone"
    if name in DEMOS:
        return DEMO_AMOUNT, "demo"
    amount = AMOUNT.get(category, 0)
    if category == "Pad" and attack >= SLOW_PAD_ATTACK_MS:
        amount = SLOW_PAD_AMOUNT
    return amount, f"held ({category.lower()}, sustain {sustain:g} %, attack {attack:g} ms)" if amount else "category"


def read_presets(presets_dir):
    out = []
    for path in sorted(Path(presets_dir).glob("*/*.sbpreset")):
        d = json.loads(path.read_text(encoding="utf-8"))
        out.append((f"{path.parent.name}/{path.name}", d["meta"]["name"], d["meta"]["category"], d["state"]["params"]))
    return out


def derive(presets_dir=PRESETS_DIR):
    doc = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))
    out = {}
    for rel, name, category, params in read_presets(presets_dir):
        amount, reason = propose_one(category, params, name, routes_pressure(params, doc))
        out[name] = {"path": rel, "category": category, "amount": amount, "reason": reason, "sustain": params.get("env1.sustain", 100), "attack": params.get("env1.attack", 0)}
    return out


def data_text(presets):
    return json.dumps({"format": FORMAT, "version": 1, "presets": presets}, indent=1, ensure_ascii=False) + "\n"


def proposal_text(derived, data):
    lines = ["# Pad pressure per preset", "",
             "Written by `scripts/propose-pad-pressure.py`. A preset in the list has the route Poly Aftertouch -> Filter 1 Cutoff in its own matrix (the plugin-wide layer is off by default now). Correct the percent in",
             "`scripts/factory-preset-pad-pressure.json` (delete a line for no route) and run `python3 scripts/gen-factory-presets.py`. Demo presets show the effect off (70 %).", "",
             "| Preset | Category | Env 1 sustain | Env 1 attack | Route | Reason |", "|---|---|---|---|---|---|"]
    for name, info in derived.items():
        amount = data.get(name, 0)
        route = f"+{amount:g} %" + (" (demo)" if name in DEMOS else "") if amount else "-"
        reason = info["reason"] if amount == info["amount"] or not amount else "set by hand"
        if not amount and info["amount"] == 0 and info["reason"] == "routes Poly Aftertouch to the cutoff itself":
            route = "own route"
        lines.append(f"| {info['path'][:-len('.sbpreset')]} | {info['category']} | {info['sustain']:g} % | {info['attack']:g} ms | {route} | {reason} |")
    n = sum(1 for v in data.values() if v)
    lines += ["", f"{n} of {len(derived)} presets get the route."]
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--presets", default=str(PRESETS_DIR))
    ap.add_argument("--data", default=str(DATA_FILE))
    ap.add_argument("--proposal", default="")
    ap.add_argument("--rederive", action="store_true")
    ap.add_argument("--write", action="store_true", help="run gen-factory-presets.py afterwards")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    derived = derive(args.presets)
    have = json.loads(Path(args.data).read_text(encoding="utf-8")).get("presets", {}) if Path(args.data).is_file() else {}
    if args.check:
        bad = [f"{n}: in the data, not a preset" for n in have if n not in derived]
        for b in bad:
            print(b)
        gen = subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "gen-factory-presets.py"), "--check", "--out", str(args.presets)], capture_output=True, text=True)
        if gen.returncode != 0:
            print(gen.stdout + gen.stderr)
        return 1 if bad or gen.returncode != 0 else 0
    data = {}
    for name, info in derived.items():
        if name in have and not args.rederive:
            data[name] = have[name]
        elif info["amount"]:
            data[name] = info["amount"]
    # a preset that already routes pressure itself keeps its own route: no entry
    Path(args.data).write_text(data_text(data), encoding="utf-8")
    if args.proposal:
        Path(args.proposal).write_text(proposal_text(derived, data), encoding="utf-8")
    if args.write:
        return subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "gen-factory-presets.py"), "--out", str(args.presets)]).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
