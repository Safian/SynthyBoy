#!/usr/bin/env python3
# SynthyBoy - perceptual loudness of every factory preset, per category gesture (LUFS, scripts/loudness.py), and the master.volume that levels them.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   measure-loudness.py measure --bin PATH/synthyboy_sound_lab [--profile xl|low] [--presets DIR] [--json OUT] [--set-volume NAME=DB ...]
#   measure-loudness.py calibrate --json MEASURE.json [--out scripts/factory-preset-loudness.json]     the reference of each category and the gesture offsets
#   measure-loudness.py solve --json MEASURE.json --out scripts/factory-preset-levels.json           the master.volume that levels the measured presets (one round; measure again, solve again)
#
# The gesture of a preset follows how its category is played (MPC note names: C3 = MIDI 60, so C1 = 36; every note on its own render, velocity 100):
#   Bass   single notes C1 G1 C2 G2 C3 (36 43 48 55 60), held 2 s
#   Lead   single notes C3 G3 C4 G4 C5 (60 67 72 79 84), held 1.5 s
#   Pad    three note chords (major triads on C3, G3, C4), held until the amp attack is over + 0.4 s (2 s at least, 5 s at most)
#   Keys   the same chords, held 2 s
#   Pluck  a played line: the single notes C3 C4 C5 (60 72 84), each struck 4 times 0.5 s apart (held 0.4 s)
#   FX     one held note, C3 (60), held for the natural length of the sound (a probe render, natural_length)
# For every render the figures of scripts/loudness.py are taken; the preset's figure is the energy mean over its renders. THE MEASURE is `fast_max`: the loudest 100 ms (K-weighted,
# BS.1770) of the gesture; `solve` moves master.volume so that fast_max + the calibration of the category (scripts/factory-preset-loudness.json) is the one target.
# The rule, the target and the check against the ear: docs/release/SOUND-FIXES-1.md.
# SUPERSEDED for the factory set (2026-11-19): the levels are set by the levelling done by hand against the stock MPC instruments (docs/release/LEVELLING-STOCK-2026-11-19.md);
# `measure` still measures, `solve` must not be run on the set (it would undo that levelling).

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


lab_mod = _load("sound_lab", "sound-lab.py")
ln = _load("loudness", "loudness.py")
SR = lab_mod.SR
VEL = 100 / 127
LEVELS = HERE / "factory-preset-levels.json"


PRE = 0.3   # silence before the first note: the engine's smoothing of master.volume starts from the default and would add a burst to the first 50 ms of a preset that is far below it


def shifted(items):
    """The events of a gesture, PRE seconds later."""
    out = []
    for it in items:
        w = it.split()
        if w[0] in ("on", "off"):
            w[1] = repr(round(float(w[1]) + PRE, 4))
        out.append(" ".join(w))
    return out


def triad(root):
    return [root, root + 4, root + 7]


def gesture(cat, params, natural=None):
    """The renders of a preset: a list of (tag, seconds, items). Times in seconds."""
    att = max(float(params.get("env1.attack", 5)) + float(params.get("env1.delay", 0)), 0.0) / 1000.0
    out = []
    if cat == "Bass":
        for n in (36, 43, 48, 55, 60):
            out.append((f"n{n}", 4.5, [f"on 0 {n} {VEL}", f"off 2.0 {n}"]))
    elif cat == "Lead":
        for n in (60, 67, 72, 79, 84):
            out.append((f"n{n}", 4.5, [f"on 0 {n} {VEL}", f"off 1.5 {n}"]))
    elif cat in ("Pad", "Keys"):
        hold = min(5.0, max(2.0, 0.4 + att)) if cat == "Pad" else 2.0
        for r in (60, 67, 72):
            ks = triad(r)
            out.append((f"c{r}", hold + 3.0, [f"on 0 {k} {VEL}" for k in ks] + [f"off {hold} {k}" for k in ks]))
    elif cat == "Pluck":
        for r in (60, 72, 84):
            items = []
            for i in range(4):
                items += [f"on {0.5 * i} {r} {VEL}", f"off {0.5 * i + 0.4} {r}"]
            out.append((f"p{r}", 5.0, items))
    else:  # FX
        hold = natural or 4.0
        out.append(("fx60", hold + 3.0, [f"on 0 60 {VEL}", f"off {hold} 60"]))
    return out


def natural_length(m_levels, t):
    """Natural length of an FX sound from a probe render (a note held 10 s): the time of the last momentary block within 20 LU of the loudest one, 1 .. 8 s (a sound that goes on is held 8 s)."""
    top = m_levels.max()
    idx = np.where(m_levels > top - 20.0)[0]
    last = t[idx[-1]] if len(idx) else 1.0
    return float(min(8.0, max(1.0, last)))


def measure(binary, profile, presets_dir, work, volume_set=None):
    lab = lab_mod.Lab(binary, work)
    files = sorted(Path(presets_dir).glob("*/*.sbpreset"))
    docs = {f: json.load(open(f)) for f in files}
    volume_set = volume_set or {}
    prof = lab_mod.PROFILES.get(profile, profile)

    def vol(doc):
        v = volume_set.get(doc["meta"]["name"])
        return [] if v is None else [f"set master.volume {v}"]

    # FX probes first (the natural length)
    nat = {}
    probe = {}
    for f, d in docs.items():
        if d["meta"]["category"] == "FX":
            probe[f] = lab.job(f"{f.stem}_probe", str(f), profile, 12.0 + PRE, *vol(d), *shifted([f"on 0 60 {VEL}", "off 10.0 60"]))
    lab.run()
    for f, wav in probe.items():
        l, r = lab.load(wav)
        t, m = ln.momentary(l, r, SR)
        nat[f] = natural_length(m, t - PRE)
    jobs = {}
    for f, d in docs.items():
        for tag, secs, items in gesture(d["meta"]["category"], d["state"]["params"], nat.get(f)):
            jobs[(f, tag)] = lab.job(f"{f.stem}_{profile}_{tag}", str(f), profile, secs + PRE, *vol(d), *shifted(items))
        # the peak figure: the eight note chord at velocity 127 (what sound-lab.py reports), held 1.5 s
        chord8 = [48, 55, 60, 64, 67, 72, 76, 79]   # C2 .. G4: the worst case of the peak table, velocity 127
        jobs[(f, "chord8")] = lab.job(f"{f.stem}_{profile}_c8", str(f), profile, 3.5, *vol(d), *shifted([f"on 0 {k} 1.0" for k in chord8] + [f"off 1.5 {k}" for k in chord8]))
    lab.run()
    res = {}
    for f, d in docs.items():
        name, cat = d["meta"]["name"], d["meta"]["category"]
        per = {"fast_max": [], "momentary_max": [], "short_term_max": [], "integrated": []}
        peak = -200.0
        for (ff, tag), wav in jobs.items():
            if ff != f:
                continue
            l, r = lab.load(wav)
            pk = lab_mod.db(max(np.abs(l).max(), np.abs(r).max()))
            if tag == "chord8":
                res.setdefault(name, {})["chord8_peak_db"] = pk
                continue
            peak = max(peak, pk)
            for k, v in ln.loudness_measures(l, r, SR).items():
                per[k].append(v)
        e = res.setdefault(name, {})
        e.update({"category": cat, "file": str(f.relative_to(presets_dir)), "volume": float(d["state"]["params"]["master.volume"]) if name not in volume_set else volume_set[name],
                  "gesture_peak_db": peak, "natural_s": nat.get(f)})
        for k, v in per.items():
            e[k] = ln.power_mean(v)
            e[k + "_each"] = v
    return res


CALIB = HERE / "factory-preset-loudness.json"
TARGET = -12.5
# The presets the owner called "ok" on the level (docs/release/preset-todo.md, MPC XL, package ov29): they define what the gesture of their category must read to be "right". For the
# categories he has not auditioned yet (Lead, Pad, Keys, Pluck) the median of the category as it was is the reference (no change of the category's level, only a levelling inside it)
# until his notes come; re-run `calibrate` with a longer list then.
OK = {
    "Bass": ["808 Boom", "Acid Saw Reso", "Bend Formant Growl", "Click Attack Bass", "Comb Metal Bass", "Custom Shape Wobble", "Diode Saturated Bass", "Fat Reese", "Formant Vowel Bass",
             "Moog Ladder Bass", "Odd Harmonics Bass", "Space Wobble Bass"],
    "FX": ["Bode Shifter Drone", "Metal Gong Strike", "Noise Riser", "Vinyl Dust Crackle"],
}


def calibrate(measured, measure="fast_max"):
    """The reference figure of each category in the state the owner heard (his ok list, or the category median), and the gesture offset that brings it to TARGET."""
    ref = {}
    for cat in ("Bass", "Lead", "Pad", "Keys", "Pluck", "FX"):
        names = [n for n in OK.get(cat, []) if n in measured]
        vals = [measured[n][measure] for n in names] or [m[measure] for m in measured.values() if m["category"] == cat]
        ref[cat] = float(np.median(vals))
    return {"measure": measure, "target": TARGET, "reference": ref, "gesture_offset_db": {c: TARGET - r for c, r in ref.items()},
            "note": "loudness of a preset = fast_max (loudest 100 ms, K-weighted LUFS, energy mean over the gesture) + gesture_offset_db[category]; master.volume is set so that it is the target"}


def solve(measured, calib, volumes, vmin=-60.0, vmax=6.0):
    """master.volume per preset that puts its calibrated loudness on the target (the level follows master.volume 1:1 in dB); returns {name: (new volume, delta, clamped)}."""
    out = {}
    for name, m in measured.items():
        cal = m[calib["measure"]] + calib["gesture_offset_db"][m["category"]]
        delta = calib["target"] - cal
        v = volumes[name] + delta
        nv = round(max(vmin, min(vmax, v)), 1)
        out[name] = (nv, nv - volumes[name], abs(nv - v) > 0.05)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["measure", "calibrate", "solve"])
    ap.add_argument("--bin")
    ap.add_argument("--profile", default="xl")
    ap.add_argument("--presets", default=str(ROOT / "content" / "presets"))
    ap.add_argument("--work", default=None)
    ap.add_argument("--json", required=True, help="measure: the output; calibrate / solve: the measurement to read")
    ap.add_argument("--volumes", default=None, help="measure: a JSON file name -> master.volume that overrides the presets' own (the levels file format); solve: the volumes the measurement was made at (default: the presets' own)")
    ap.add_argument("--out", default=None, help="calibrate: the calibration file (default scripts/factory-preset-loudness.json); solve: the levels file to write")
    a = ap.parse_args()
    if a.cmd == "measure":
        vs = json.load(open(a.volumes)) if a.volumes else None
        res = measure(a.bin, a.profile, a.presets, a.work, vs)
        json.dump(res, open(a.json, "w"), indent=1, sort_keys=True, default=float)
        print(f"{len(res)} presets measured on profile {a.profile}")
    elif a.cmd == "calibrate":
        c = calibrate(json.load(open(a.json)))
        json.dump(c, open(a.out or CALIB, "w"), indent=1, sort_keys=True)
        open(a.out or CALIB, "a").write("\n")
        print(json.dumps(c["gesture_offset_db"]))
    else:
        measured = json.load(open(a.json))
        calib = json.load(open(CALIB))
        res = solve(measured, calib, {n: m["volume"] for n, m in measured.items()})
        levels = json.load(open(a.out)) if a.out and Path(a.out).is_file() else {}
        for n, (v, d, clamped) in res.items():
            levels[n] = v
            if clamped:
                print(f"clamped: {n} {v}")
        if a.out:
            json.dump(levels, open(a.out, "w"), indent=1, sort_keys=True)
            open(a.out, "a").write("\n")
        print(f"{len(res)} presets solved, largest change {max(abs(d) for _, d, _ in res.values()):.1f} dB")


if __name__ == "__main__":
    sys.exit(main())
