#!/usr/bin/env python3
# SynthyBoy - sound set batch 2: levels and measurements of the batch-2 presets through the offline tools that can read the factory content packs.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   measure-batch2.py levels   [--bin DIR] [--work DIR]    tune master.volume of every batch-2 preset to a chord RMS of -20 dBFS (tier high), write scripts/factory-preset-levels.json
#   measure-batch2.py report   [--bin DIR] [--work DIR]    render the presets and the macro / wheel / pressure variants, write docs/release/sound-set-batch2-measurements.json
#
# Why this tool and not scripts/sound-lab.py alone: the first build of synthyboy_sound_lab and synthyboy_tier_bench did not read content files (no content store, so a Sample / Granular /
# Spectral / Multisample oscillator was silent and Convolve absent); their sources read $SYNTHYBOY_CONTENT_DIR now (a rebuild is needed), and `sound-lab.py report` then measures the full
# set (velocity, 8-note peak, 5 registers, macros, pressure, wheel, long hold) for these presets too. The renderer used here, synthyboy_preset_render (--content-dir), exists in both
# builds: one note (C3, velocity 0.8, held 1.5 s with its tail) and a four note chord at velocity 1.0 (the level measure of the factory presets), at a quality tier. The macro / wheel /
# pressure responses are measured on the single note with the control's routes re-pointed at an unused macro (the route law is the same: 0 .. 1 of the source times the amount) set to 0 and to 100.

import argparse
import hashlib
import importlib.util
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SR = 44100


def _load(name, fname):
    spec = importlib.util.spec_from_file_location(name, HERE / fname)
    m = importlib.util.module_from_spec(spec)
    sys.modules[name] = m
    spec.loader.exec_module(m)
    return m


GEN = _load("sb_gen_factory", "gen-factory-presets.py")
LAB = _load("sb_sound_lab", "sound-lab.py")
BATCH2 = _load("sb_batch2", "factory-presets-batch2.py")
DEFAULT_BIN = ROOT.parent / "work-sound-set" / "build-sound"
SRC = GEN.mod_source


def batch2_entries():
    names = set(BATCH2.macro_names())
    return [e for e in GEN.build_all() if e[0] in names]


def write_entries(entries, directory):
    directory = Path(directory)
    files = {}
    for name, category, meta, doc in entries:
        p = directory / category / GEN.file_name(name)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(doc, encoding="utf-8")
        files[name] = p
    return files


def store_dir(work):
    return LAB.content_store_dir(work)


def run_render(binary, presets_dir, work, tier="high", filt="", wav=None, store=None):
    cmd = [str(binary / "tools" / "preset-render" / "synthyboy_preset_render"), "--presets", str(presets_dir), "--engine", "own", "--tier", tier, "--sr", str(SR),
           "--content-dir", str(store or store_dir(work)), "--filter", filt]
    if wav:
        cmd += ["--wav", str(wav)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    rows = {}
    in_tier = False
    for line in r.stdout.splitlines():
        if line.startswith("== tier"):
            in_tier = True
            continue
        m = re.match(r"^(\S+\.sbpreset)\s+(\S+)\s+(-?[\d.]+|-inf)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+([\d.]+)", line) if in_tier else None
        if m:
            f = m.group(1)
            rows[f] = {"peak": float(m.group(3)), "chord_peak": float(m.group(4)), "rms": float(m.group(5)), "chord_rms": float(m.group(6)), "dc": float(m.group(7)), "tail": float(m.group(8)),
                       "problem": "PROBLEM" in line}
    fails = [l for l in r.stdout.splitlines() if l.startswith("FAIL")]
    return rows, fails, r.stdout


# The level is measured at the sustain, as batch 1 did (scripts/sound-lab.py holds a swelling chord until its attack is over; the chord of synthyboy_preset_render is held for 1 s, which would level
# a slow pad where it has hardly started): the level render is a copy of the preset with the attack of envelope 1 cut to 5 ms and, for the build-ups, the extra changes below.
LEVEL_OVERRIDES = {
    "Rising Grain Riser": {"env2.attack": 5, "osc1.sample.start": 72, "osc1.sample.loop_mode": "Forward loop"},   # the top of the build (the last 1.7 s of the texture), the filter open
    "Reverse Gong Swell": {},
}


def level_doc(name, doc_text):
    doc = json.loads(doc_text)
    p = doc["state"]["params"]
    if float(p.get("env1.attack", 5)) > 150 or float(p.get("env1.delay", 0)) > 0:
        p["env1.attack"], p["env1.delay"] = 5.0, 0.0
    p.update(LEVEL_OVERRIDES.get(name, {}))
    return json.dumps(doc, separators=(",", ":"))


def tune_levels(binary, work, target=-20.0, rounds=4):
    entries = batch2_entries()
    levels_path = HERE / "factory-preset-levels.json"
    levels = json.loads(levels_path.read_text())
    for rnd in range(rounds):
        d = Path(work) / "lvl"
        if d.exists():
            for f in d.rglob("*.sbpreset"):
                f.unlink()
        files = write_entries([(n, c, m, level_doc(n, doc)) for n, c, m, doc in entries], d)
        rows, fails, _ = run_render(binary, d, work)
        worst = 0.0
        for name, category, meta, doc in entries:
            row = rows.get(GEN.file_name(name))
            if row is None:
                print("no row for", name)
                continue
            cur = float(levels.get(name, json.loads(doc)["state"]["params"]["master.volume"]))
            err = target - row["chord_rms"]
            worst = max(worst, abs(err))
            if abs(err) > 0.05 and row["chord_rms"] > -150:
                levels[name] = round(max(-60.0, min(6.0, cur + err)), 1)
        print(f"round {rnd}: worst error {worst:.2f} dB")
        levels_path.write_text(json.dumps(dict(sorted(levels.items())), indent=1, ensure_ascii=False) + "\n")
        if worst < 0.1:
            break
        entries = batch2_entries()


# ---------------------------------------------------------------------------------------------------------------------------------------- variants
def macro_destinations(doc_text, m):
    p = json.loads(doc_text)["state"]["params"]
    want = SRC[f"macro{m}"]
    names = {v: k for k, v in GEN.mod_destination.items()}
    return {names.get(int(p[f"mod{n:02d}.destination"]), "") for n in range(1, 65) if int(p.get(f"mod{n:02d}.source", -1)) == want and float(p.get(f"mod{n:02d}.amount", 0)) != 0}


def variant_doc(doc_text, macro_values=None, remap=None, category=None):
    """A copy of a preset document with macros set and/or every route of a source re-pointed at `macro8` (the wheel / pressure / velocity stand-in)."""
    doc = json.loads(doc_text)
    if category:
        doc["meta"]["category"] = category        # the single note of the renderer is C1 for a Bass and C3 otherwise: a macro that works on the zone lookup of a key (Zone Shift) is measured at C1
    p = doc["state"]["params"]
    for k, v in (macro_values or {}).items():
        p[k] = v
    if remap:
        src_to = SRC[remap[1]]
        for n in range(1, 65):
            for key in (f"mod{n:02d}.source", f"mod{n:02d}.aux_source"):
                if key in p and int(p[key]) == SRC[remap[0]]:
                    p[key] = src_to
    return json.dumps(doc, separators=(",", ":"))


def macro_in_use(doc, m):
    p = json.loads(doc)["state"]["params"]
    want = SRC[f"macro{m}"]
    return any(int(p.get(f"mod{n:02d}.{s}", -1)) == want for n in range(1, 65) for s in ("source", "aux_source"))


def uses_source(doc, src):
    p = json.loads(doc)["state"]["params"]
    want = SRC[src]
    return any(int(p.get(f"mod{n:02d}.{s}", -1)) == want and float(p.get(f"mod{n:02d}.amount", 0)) != 0 for n in range(1, 65) for s in ("source", "aux_source"))


def load_wav(path):
    sr, d = wavfile.read(str(path))
    d = d.astype(np.float64)
    if d.dtype.kind == "i" or True:
        pass
    # 24 bit PCM comes as int32 scaled to full int32 by scipy
    if np.abs(d).max() > 2:
        d = d / 2147483648.0
    return d[:, 0], d[:, 1]


def analyse_wav(path):
    l, r = load_wav(path)
    n = len(l)
    o = {"seconds": n / SR}
    if np.abs(l).max() < 1e-7 and np.abs(r).max() < 1e-7:
        o["silent"] = True
        return o
    o["silent"] = False
    i1 = min(n, int(1.5 * SR))
    cor, mono, worst, wf = LAB.stereo_metrics(l, r, int(0.1 * SR), i1)
    o["stereo_cor"], o["mono_db"], o["mono_worst_band_db"], o["mono_worst_band_hz"] = cor, mono, worst, wf
    o["dc"] = float(max(abs(l[int(0.1 * SR):i1].mean()), abs(r[int(0.1 * SR):i1].mean())))
    o["click"] = float(max(LAB.click_start(l, 0), LAB.click_start(r, 0)))
    t, cen, lev = LAB.centroid_track(l, r, int(0.05 * SR), i1)
    o["centroid_hz"] = float(np.median(cen))
    o["move_centroid_oct"] = float(np.log2(np.percentile(cen, 95) / max(np.percentile(cen, 5), 1)))
    o["move_level_db"] = float(np.percentile(lev, 95) - np.percentile(lev, 5))
    return o


def best_distance(a, b):
    """The larger spectral distance of the held note (0.1 .. 1.4 s) and of the whole render with its tail (0.1 .. 3 s): a macro of the space shows in the tail, a macro of the attack in the note."""
    return max((LAB.spectral_distance(a, b, int(0.1 * SR), int(t1 * SR)) for t1 in (1.4, 3.0)), key=lambda t: t[0])


def audible(t):
    dist, lev, shift, side, beat, hf, lf, fine = t
    return dist >= 3 or abs(lev) >= 3 or abs(shift) >= 0.3 or abs(side) >= 3 or abs(beat) >= 1 or abs(hf) >= 4 or abs(lf) >= 4 or fine >= 6


def report(binary, work):
    entries = batch2_entries()
    work = Path(work)
    base = work / "rep"
    if base.exists():
        for f in base.rglob("*"):
            if f.is_file():
                f.unlink()
    files = write_entries(entries, base)
    # variants live next to the presets as extra files (the renderer renders every .sbpreset of the folder)
    var_dir = work / "var"
    if var_dir.exists():
        for f in var_dir.rglob("*"):
            if f.is_file():
                f.unlink()
    var_dir.mkdir(parents=True, exist_ok=True)
    plan = {}
    for name, category, meta, doc in entries:
        stem = Path(GEN.file_name(name)).stem
        free = [m for m in range(1, 9) if not macro_in_use(doc, m)]
        standin = f"macro{free[-1]}" if free else None
        plan[name] = {"stem": stem, "category": category, "standin": standin, "standin_n": free[-1] if free else None}
        for m in range(1, 9):
            if macro_in_use(doc, m):
                cat = "Bass" if any(d.endswith("multi.timbre") for d in macro_destinations(doc, m)) else None
                for v in (0, 100):
                    (var_dir / f"{stem}__m{m}_{v}.sbpreset").write_text(variant_doc(doc, {f"macro.{m}": v}, category=cat))
        if standin:
            for ctrl in ("mod_wheel", "poly_aftertouch", "velocity"):
                if uses_source(doc, ctrl):
                    for v in (0, 100):
                        (var_dir / f"{stem}__{ctrl}_{v}.sbpreset").write_text(variant_doc(doc, {f"macro.{plan[name]['standin_n']}": v}, remap=(ctrl, standin)))
    # the level render: the same presets with the attack cut (LEVEL_OVERRIDES): what the master volume was tuned on; peak and level at the sustain
    lv_dir = work / "lvrep"
    if lv_dir.exists():
        for f in lv_dir.rglob("*.sbpreset"):
            f.unlink()
    write_entries([(n, c, m, level_doc(n, doc)) for n, c, m, doc in entries], lv_dir)
    lv = {t: run_render(binary, lv_dir, work, tier=t)[0] for t in ("high", "low")}
    res = {}
    for tier in ("high", "low"):
        rows, fails, out = run_render(binary, base, work, tier=tier, wav=work / "wav")
        res[tier] = rows
        for f in fails:
            print(tier, f)
    # the same presets with no content store at all (a pack that is not installed): what is left to hear
    empty = work / "empty-store"
    empty.mkdir(exist_ok=True)
    nrows, _, _ = run_render(binary, base, work, tier="high", wav=work / "wavnc", store=empty)
    # variants: high tier only; the wavs are read
    vrows, vfails, _ = run_render(binary, var_dir, work, tier="high", wav=work / "wavvar")
    out = {}
    for name, category, meta, doc in entries:
        stem = plan[name]["stem"]
        o = {"name": name, "category": category}
        for tier in ("high", "low"):
            r = res[tier].get(stem + ".sbpreset")
            o[tier] = r
        o["nocontent"] = nrows.get(stem + ".sbpreset")
        o["sustain"] = {t: lv[t].get(stem + ".sbpreset") for t in ("high", "low")}
        wavp = work / "wav" / "high" / f"{stem}.wav"
        if wavp.is_file():
            o["wav"] = analyse_wav(wavp)
        lowp = work / "wav" / "low" / f"{stem}.wav"
        if lowp.is_file():
            o["wav_low"] = analyse_wav(lowp)
        macros, ctrls = {}, {}
        base_wav = work / "wav" / "high" / f"{stem}.wav"
        for m in range(1, 9):
            a, b = work / "wavvar" / "high" / f"{stem}__m{m}_0.wav", work / "wavvar" / "high" / f"{stem}__m{m}_100.wav"
            if a.is_file() and b.is_file():
                macros[m] = best_distance(load_wav(a), load_wav(b))
        for ctrl in ("mod_wheel", "poly_aftertouch", "velocity"):
            a, b = work / "wavvar" / "high" / f"{stem}__{ctrl}_0.wav", work / "wavvar" / "high" / f"{stem}__{ctrl}_100.wav"
            if a.is_file() and b.is_file():
                ctrls[ctrl] = best_distance(load_wav(a), load_wav(b))
        o["macros"] = {str(k): list(v) for k, v in macros.items()}
        o["macros_audible"] = {str(k): bool(audible(v)) for k, v in macros.items()}
        o["controls"] = {k: list(v) for k, v in ctrls.items()}
        out[name] = o
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["levels", "report"])
    ap.add_argument("--bin", default=str(DEFAULT_BIN))
    ap.add_argument("--work", default=None)
    a = ap.parse_args()
    work = Path(a.work or tempfile.mkdtemp(prefix="sbb2-"))
    work.mkdir(parents=True, exist_ok=True)
    binary = Path(a.bin)
    if a.cmd == "levels":
        tune_levels(binary, work)
    else:
        res = report(binary, work)
        (ROOT / "docs" / "release" / "sound-set-batch2-measurements.json").write_text(json.dumps(res, indent=1, default=float) + "\n")
        print(f"wrote {len(res)} presets")


if __name__ == "__main__":
    main()
