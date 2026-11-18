#!/usr/bin/env python3
# SynthyBoy - the calibration tables of the tier level makeup (docs/release/TIER-LEVEL-COMPENSATION.md): the power gain of each effect on the calibration program, over its parameters.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   tier-makeup-calibrate.py --bin PATH/synthyboy_fx_makeup_calibrate --out core/engine-own/fx/fx_makeup_tables.h [--json tables.json]
#
# Runs the bench synthyboy_fx_makeup_calibrate (bench/fx_makeup_calibrate.cpp: noise whose power per Hz falls with f^-1.5 from 30 Hz to 12 kHz, stereo with 70 % shared power, 10 s,
# the last 6 s measured) on the grids below and writes the tables as a C++ header. The grids hold ONE effect parameter set at a time; the secondary parameters are corrected in
# core/engine-own/fx/fx_makeup.cpp by the curves at the end of this file (measured once, in docs/release/TIER-LEVEL-COMPENSATION.md).
#   reverb  W (power of the wet path alone, mix 100) by type (room for type 4), decay (the feedback key scales it for types 4 and 5) and size
#   flanger, hyper, bode: the gains per band of the program meter (ten per cell: mono five, wide five) by regression on 27 programs, flanger by feedback and centre, hyper by unison, mix and
#           dim mix, bode by feed for a shifted wet path (shift x range = 100 Hz) and an unshifted one

import argparse
import importlib.util
import json
import subprocess
import sys
from multiprocessing import Pool
from pathlib import Path

_spec = importlib.util.spec_from_file_location("regress", Path(__file__).resolve().parent / "tier-makeup-regress.py")
regress = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(regress)

REVERB_T = [100, 200, 400, 800, 1600, 3200, 6400, 12800]
REVERB_SIZE = [0, 25, 50, 75, 100]
FLANGER_FB = [-95, -75, -50, -25, 0, 25, 50, 75, 95]
FLANGER_CENTRE = [20, 60, 200, 600, 2000, 6000, 16000]
HYPER_UNISON = list(range(8))
HYPER_MIX = [0, 25, 50, 75, 100]
HYPER_DIM = [0, 50, 100]
BODE_FEED = [0, 20, 40, 60, 80, 100]


def run(binary, jobs):
    r = subprocess.run([binary], input="\n".join(jobs) + "\n", capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    out = []
    for line in r.stdout.splitlines():
        w = line.split()
        out.append(float(w[1]))
    assert len(out) == len(jobs), (len(out), len(jobs), r.stdout[-300:])
    return out


def fit_one(binary, module, settings):
    return regress.fit_cell(binary, module, settings)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    tab = {}

    # reverb: tables of the neutral reverb (the defaults of the filters: low cut 20, high cut 8000, shelves default, width 100), mix 100
    rev = {}
    for t in range(6):
        for room in (range(5) if t == 4 else [2]):
            jobs = [f"reverb stereo reverb.mix=100 reverb.type={t} reverb.room={room} reverb.decay={T} reverb.size={s} reverb.feedback=50" for T in REVERB_T for s in REVERB_SIZE]
            vals = run(a.bin, jobs)
            rev[f"{t}_{room}"] = [vals[i * len(REVERB_SIZE):(i + 1) * len(REVERB_SIZE)] for i in range(len(REVERB_T))]
    tab["reverb"] = rev

    # flanger, hyper and bode: the gains per band of the program meter, by non negative regression on 27 programs (scripts/tier-makeup-regress.py fit_cell), at mix 100 for the equal power
    # effects (flanger: the wet path alone; Bode: the wet path alone, shifted or not) and for the whole module of Hyper (its mix is linear and Dimension adds, so the cells run over mix and dim mix)
    cells = []
    for fb in FLANGER_FB:
        for c in FLANGER_CENTRE:
            cells.append(("flanger", f"flanger.mix=100 flanger.feedback={fb} flanger.center={c}"))
    for u in HYPER_UNISON:
        for m in HYPER_MIX:
            for d in HYPER_DIM:
                cells.append(("hyperdim", f"hyperdim.unison={u} hyperdim.mix={m} hyperdim.dim_mix={d}"))
    for f in BODE_FEED:
        cells.append(("bode", f"bode.mix=100 bode.shift=25 bode.range=400 bode.feed={f}"))
    for f in BODE_FEED:
        cells.append(("bode", f"bode.mix=100 bode.shift=0 bode.feed={f}"))
    with Pool(2) as pool:
        fits = pool.starmap(fit_one, [(a.bin, mod, st) for mod, st in cells])
    worst = max(f[2] for f in fits)
    sys.stderr.write(f"band fits: {len(fits)} cells, worst max error {worst:.2f} dB, mean rms {sum(f[1] for f in fits) / len(fits):.2f} dB\n")
    k = 0
    fl = []
    for fb in FLANGER_FB:
        row = []
        for c in FLANGER_CENTRE:
            row.append(fits[k][0].tolist()); k += 1
        fl.append(row)
    hy = []
    for u in HYPER_UNISON:
        row = []
        for m in HYPER_MIX:
            r2 = []
            for d in HYPER_DIM:
                r2.append(fits[k][0].tolist()); k += 1
            row.append(r2)
        hy.append(row)
    bo = [[], []]
    for t in (0, 1):
        for f in BODE_FEED:
            bo[t].append(fits[k][0].tolist()); k += 1
    tab["flanger"], tab["hyper"], tab["bode"] = fl, hy, bo

    if a.json:
        json.dump(tab, open(a.json, "w"), indent=1)

    def arr(x, fmt="{:.2f}f"):
        if isinstance(x, list) and x and isinstance(x[0], list):
            return "{ " + ", ".join(arr(y, fmt) for y in x) + " }"
        return "{ " + ", ".join(fmt.format(max(float(y), -60.0)) for y in x) + " }"

    h = []
    h.append("// SynthyBoy - own engine: the calibration tables of the loudness models (generated by scripts/tier-makeup-calibrate.py; do not edit).")
    h.append("// Copyright (C) 2026 Safian Szabolcs")
    h.append("// SPDX-License-Identifier: AGPL-3.0-or-later")
    h.append("#pragma once")
    h.append("")
    h.append("namespace synthyboy { namespace own { namespace makeup { namespace tables {")
    h.append(f"constexpr int kReverbT = {len(REVERB_T)}, kReverbSize = {len(REVERB_SIZE)};")
    h.append("constexpr float kReverbDecayMs[kReverbT] = " + arr(REVERB_T, "{:.0f}.0f") + ";")
    h.append("constexpr float kReverbSizePct[kReverbSize] = " + arr(REVERB_SIZE, "{:.0f}.0f") + ";")
    h.append("// the wet power in dB of the neutral reverb, [type 0 1 2 3 5, then the five rooms of type 4][decay][size]")
    h.append("constexpr float kReverbDb[10][kReverbT][kReverbSize] = {")
    order = ["0_2", "1_2", "2_2", "3_2", "5_2", "4_0", "4_1", "4_2", "4_3", "4_4"]
    h.append(",\n".join("    " + arr(rev[o]) for o in order))
    h.append("};")
    h.append(f"constexpr int kFlangerFb = {len(FLANGER_FB)}, kFlangerCentre = {len(FLANGER_CENTRE)};")
    h.append("constexpr float kFlangerFbPct[kFlangerFb] = " + arr(FLANGER_FB, "{:.0f}.0f") + ";")
    h.append("constexpr float kFlangerCentreHz[kFlangerCentre] = " + arr(FLANGER_CENTRE, "{:.0f}.0f") + ";")
    h.append("// the wet power of the flanger at mix 100 in dB per meter band, mono five then wide five [feedback][centre][10]")
    h.append("constexpr float kFlangerBands[kFlangerFb][kFlangerCentre][10] = " + arr(tab["flanger"]) + ";")
    h.append(f"constexpr int kHyperUnison = {len(HYPER_UNISON)}, kHyperMix = {len(HYPER_MIX)}, kHyperDim = {len(HYPER_DIM)};")
    h.append("constexpr float kHyperMixPct[kHyperMix] = " + arr(HYPER_MIX, "{:.0f}.0f") + ";")
    h.append("constexpr float kHyperDimPct[kHyperDim] = " + arr(HYPER_DIM, "{:.0f}.0f") + ";")
    h.append("// the power of the whole module in dB per meter band [unison choice][hyper mix][dim mix][10]")
    h.append("constexpr float kHyperBands[kHyperUnison][kHyperMix][kHyperDim][10] = " + arr(tab["hyper"]) + ";")
    h.append(f"constexpr int kBodeFeed = {len(BODE_FEED)};")
    h.append("constexpr float kBodeFeedPct[kBodeFeed] = " + arr(BODE_FEED, "{:.0f}.0f") + ";")
    h.append("// the wet power of Bode at mix 100 in dB per meter band [shifted, unshifted][feed][10]")
    h.append("constexpr float kBodeBands[2][kBodeFeed][10] = " + arr(tab["bode"]) + ";")
    h.append("}}}} // namespace synthyboy::own::makeup::tables")
    open(a.out, "w").write("\n".join(h) + "\n")


if __name__ == "__main__":
    sys.exit(main())
