#!/usr/bin/env python3
# SynthyBoy - levels the factory presets to the stock MPC instruments: the sample peaks of what the host receives (the engine output through the shells' output stage,
# core/include/synthyboy/output_stage.h) against the medians of the stock instruments per category (scripts/stock-level-targets.json, measured on the MPC host meter).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   level-to-stock.py measure --probe BIN --content-dir DIR --gain DB --out MEASURE.jsonl        renders every preset (Key 61 and XL profiles) with synthyboy_level_probe
#   level-to-stock.py gains   --measure MEASURE.jsonl [--targets FILE] [--candidates -6 0 3]      the master.volume change every preset needs at candidate make-up gains
#   level-to-stock.py solve   --probe BIN --content-dir DIR --gain DB [--targets FILE] [--rounds 3] [--work DIR]
#                             measure, set master.volume, regenerate the presets, measure again, until no preset moves by more than 0.05 dB (writes scripts/factory-preset-levels.json)
#   level-to-stock.py report  --measure MEASURE.jsonl [--before BEFORE.jsonl] [--targets FILE] [--md OUT.md]   tables and checks of docs/release/LEVELLING-STOCK-2026-11-19.md
#
# The rule (docs/release/LEVELLING-STOCK-2026-11-19.md): per preset ONE master.volume change d (dB) = the least-squares fit, equal weights, of the four dB errors
#   (note peak v127 - target note peak), (chord peak v127 - target chord peak), on the Key 61 profile ("low") and on the XL profile ("high,voices=16,control=economy"),
# where a peak is the peak the output stage receives (raw + make-up gain; the limiter idles at the targets, so it is also what the host receives) -> d = the mean of
# (target - peak) over the four. master.volume is the last gain of the engine before its safety knee (+-1.8 of full scale), so a peak follows it 1:1 in dB while the raw peak
# stays under the knee; a second round measures what the first set. Then two limits: the chord peak within 2.5 dB of its target on both profiles (CHORD_WINDOW) and no raw
# engine peak above 1.5 of full scale (SAFE_RAW_DB). Clamped to the range of master.volume (docs/params.json: -60 .. +6 dB), rounded to 0.1 dB.
# Gestures (synthyboy_level_probe): note key 48 (C3 with C4 = 60), chord 36 48 55 60, six note chord 36 43 48 55 60 64; velocity 127 and 100; held 3 s; 48 kHz.

import argparse
import json
import os
import statistics as st
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
LEVELS_PATH = os.path.join(SCRIPT_DIR, "factory-preset-levels.json")
TARGETS_PATH = os.path.join(SCRIPT_DIR, "stock-level-targets.json")
GEN = os.path.join(SCRIPT_DIR, "gen-factory-presets.py")
PRESETS = os.path.join(ROOT_DIR, "content", "presets")
PROFILES = {"low": "low", "xl": "high,voices=16,control=economy"}
CATEGORIES = ["Bass", "Lead", "Pad", "Keys", "Pluck", "FX"]
VOLUME_MIN, VOLUME_MAX = -60.0, 6.0
KNEE_DB = 20 * __import__("math").log10(1.8)      # +5.11 dBFS: the engine's safety knee starts there
SAFE_RAW_DB = 20 * __import__("math").log10(1.5)  # +3.52 dBFS: the margin the levelling keeps under it


def load_targets(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def measure(probe, content_dir, gain, out):
    def one(key):
        r = subprocess.run([probe, "--presets", PRESETS, "--content-dir", content_dir, "--profile", PROFILES[key], "--gain", str(gain)],
                           check=True, capture_output=True, text=True)
        return [dict(json.loads(line), prof=key) for line in r.stdout.splitlines() if line.strip()]
    with ThreadPoolExecutor(max_workers=2) as ex:
        rows = sum(ex.map(one, sorted(PROFILES)), [])
    with open(out, "w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r, sort_keys=True) + "\n")
    return rows


def read_measure(path):
    with open(path, "r", encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def by_preset(rows):
    out = {}
    for r in rows:
        out.setdefault(r["name"], {"category": r["category"]})[r["prof"]] = r
    return out


def errors(p, targets, gain=None):
    """The four dB errors (peak - target) of a preset; `gain` re-bases the peaks to another make-up gain (raw + gain)."""
    cat = p["category"]
    e = []
    for prof in PROFILES:
        r = p[prof]
        g = r["gain"] if gain is None else gain
        e.append(r["note_v127"]["raw"] + g - targets["peak_note_v127"][cat])
        e.append(r["chord_v127"]["raw"] + g - targets["peak_chord_v127"][cat])
    return e


CHORD_WINDOW = 2.5   # dB: the chord peak (velocity 127) is pulled into this window around its target after the fit (the test allows 3)


def needed_change(p, targets, gain=None):
    return -st.mean(errors(p, targets, gain))


def constrained_change(p, targets):
    """The fit, then two limits: the velocity 127 chord peak within CHORD_WINDOW of its target on both profiles (the stock chord / note ratio differs from ours for many
    presets: the chord wins), and no raw engine peak (note, chord, six note chord, velocity 127) above SAFE_RAW_DB (1.5 of full scale, under the knee at 1.8)."""
    d = needed_change(p, targets)
    cat = p["category"]
    ce = [p[x]["chord_v127"]["pre"] - targets["peak_chord_v127"][cat] for x in PROFILES]
    hi = min(CHORD_WINDOW - e for e in ce)
    lo = max(-CHORD_WINDOW - e for e in ce)
    # the two profiles more than twice the window apart (an effect the Key 61 skips carries the peak): the middle, so both miss by the same
    d = (lo + hi) / 2 if lo > hi else min(max(d, lo), hi)
    raw = max(p[x][g]["raw"] for x in PROFILES for g in ("note_v127", "chord_v127", "chord6_v127"))
    return min(d, SAFE_RAW_DB - 0.05 - raw)


def current_levels():
    with open(LEVELS_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def cmd_gains(a):
    targets = load_targets(a.targets)
    ps = by_preset(read_measure(a.measure))
    levels = current_levels()
    print("make-up gain: median / p10 / p90 / min / max of the needed master.volume change (dB); presets that would need master.volume above +%g or below %g" % (VOLUME_MAX, VOLUME_MIN))
    best = None
    for g in a.candidates:
        d = sorted(needed_change(p, targets, g) for p in ps.values())
        over = [n for n, p in ps.items() if levels[n] + needed_change(p, targets, g) > VOLUME_MAX]
        under = [n for n, p in ps.items() if levels[n] + needed_change(p, targets, g) < VOLUME_MIN]
        q = st.quantiles(d, n=10)
        print("  %+5.1f dB: median %+.2f  p10 %+.2f  p90 %+.2f  min %+.2f  max %+.2f   over +6: %d %s  under -60: %d" % (g, st.median(d), q[0], q[-1], d[0], d[-1], len(over), sorted(over)[:6], len(under)))
    # the rule: the gain at which the median preset needs no change, in steps of 0.5 dB, raised until no preset needs more than the top of master.volume
    g0 = a.candidates[0]
    med = st.median(needed_change(p, targets, g0) for p in ps.values())
    g = round((g0 + med) * 2) / 2
    while any(levels[n] + needed_change(p, targets, g) > VOLUME_MAX for n, p in ps.items()):
        g += 0.5
    print("rule (median change about 0, nobody above +6 dB of master.volume): kMakeupGainDb = %+.1f dB" % g)


def cmd_solve(a):
    targets = load_targets(a.targets)
    os.makedirs(a.work, exist_ok=True)
    prev, applied = None, {}
    for rnd in range(a.rounds + 1):
        path = os.path.join(a.work, "round%d.jsonl" % rnd)
        ps = by_preset(measure(a.probe, a.content_dir, a.gain, path))
        if rnd == a.rounds:
            break
        levels = current_levels()
        moved = 0.0
        new = {}
        for n, v in levels.items():
            p = ps.get(n)
            if p is None:
                new[n] = v
                continue
            d = constrained_change(p, targets)
            # a preset whose peaks do not follow master.volume 1:1 (an effect after the volume that depends on the level): the slope seen in the last round scales the step
            if prev is not None and n in prev and abs(applied.get(n, 0.0)) >= 0.3:
                seen = st.mean(p[x]["chord_v127"]["pre"] - prev[n][x]["chord_v127"]["pre"] for x in PROFILES)
                slope = min(1.5, max(0.3, seen / applied[n]))
                d = d / slope
            nv = round(min(VOLUME_MAX, max(VOLUME_MIN, v + d)), 1)
            applied[n] = nv - v
            moved = max(moved, abs(nv - v))
            new[n] = nv
        prev = ps
        with open(LEVELS_PATH, "w", encoding="utf-8") as f:
            json.dump(dict(sorted(new.items())), f, indent=1, ensure_ascii=False)
            f.write("\n")
        subprocess.run([sys.executable, "-I", GEN, "--out", PRESETS], check=True, capture_output=True)
        print("round %d: largest move %.2f dB" % (rnd + 1, moved))
        if moved <= 0.05:
            ps = by_preset(measure(a.probe, a.content_dir, a.gain, os.path.join(a.work, "final.jsonl")))
            break
    print("measurements in", a.work)


def med(xs):
    xs = [x for x in xs if x is not None and x > -150]
    return st.median(xs) if xs else float("nan")


def cmd_report(a):
    targets = load_targets(a.targets)
    rows = read_measure(a.measure)
    ps = by_preset(rows)
    before = by_preset(read_measure(a.before)) if a.before else None
    L = []
    w = L.append
    w("Targets (`%s`): %s" % (os.path.relpath(a.targets, ROOT_DIR), targets.get("notes", "")))
    w("")
    w("Make-up gain of the measurement: %+.1f dB. Peaks in dBFS after the output stage (what the host receives), sustained = RMS 0.3 .. 2.0 s after the strike, medians per category." % rows[0]["gain"])
    w("")
    for prof in PROFILES:
        w("**%s profile**" % ("Key 61 (low)" if prof == "low" else "XL (high, 16 voices, economy control)"))
        w("")
        w("| category | n | note peak v127 (target) | chord peak v127 (target) | note sustained | chord sustained | note v127-v100 (stock) | chord6 peak | limiter engaged, chord v127 (median share of blocks) |" + (" before: note / chord peak, note / chord sustained |" if before else ""))
        w("|---|---|---|---|---|---|---|---|---|" + ("---|" if before else ""))
        for c in CATEGORIES + ["ALL"]:
            sel = [p[prof] for p in ps.values() if c == "ALL" or p["category"] == c]
            tn = targets["peak_note_v127"].get(c) if c != "ALL" else None
            tc = targets["peak_chord_v127"].get(c) if c != "ALL" else None
            stockvel = (targets["peak_note_v127"][c] - targets["peak_note_v100"][c]) if c != "ALL" and "peak_note_v100" in targets else None
            cells = [c, str(len(sel)),
                     "%.1f (%s)" % (med(r["note_v127"]["post"] for r in sel), "%.1f" % tn if tn is not None else "-"),
                     "%.1f (%s)" % (med(r["chord_v127"]["post"] for r in sel), "%.1f" % tc if tc is not None else "-"),
                     "%.1f" % med(r["note_v127"]["sus"] for r in sel),
                     "%.1f" % med(r["chord_v127"]["sus"] for r in sel),
                     "%.1f (%s)" % (med(r["note_v127"]["pre"] - r["note_v100"]["pre"] for r in sel), "%.1f" % stockvel if stockvel is not None else "-"),
                     "%.1f" % med(r["chord6_v127"]["post"] for r in sel),
                     "%.1f %%" % (100 * med(r["chord_v127"]["engaged"] for r in sel))]
            if before:
                bs = [p[prof] for p in before.values() if c == "ALL" or p["category"] == c]
                cells.append("%.1f / %.1f, %.1f / %.1f" % (med(r["note_v127"]["post"] for r in bs), med(r["chord_v127"]["post"] for r in bs),
                                                            med(r["note_v127"]["sus"] for r in bs), med(r["chord_v127"]["sus"] for r in bs)))
            w("| " + " | ".join(cells) + " |")
        w("")
    # residuals
    res = []
    for n, p in sorted(ps.items()):
        e = errors(p, targets)
        res.append((max(abs(x) for x in e), n, p["category"], e))
    big = [r for r in res if r[0] > 3.0]
    w("Residuals (dB, after the fit; per preset the largest of note / chord on Key 61 / XL): median %.2f, p90 %.2f, max %.2f; above 3 dB: %d presets." % (
        st.median(r[0] for r in res), st.quantiles([r[0] for r in res], n=10)[-1], max(r[0] for r in res), len(big)))
    w("")
    if big:
        w("| preset | category | note error Key 61 / XL | chord error Key 61 / XL | chord - note (ours) | chord - note (target) |")
        w("|---|---|---|---|---|---|")
        for m, n, c, e in sorted(big, reverse=True):
            p = ps[n]["low"]
            w("| %s | %s | %+.1f / %+.1f | %+.1f / %+.1f | %+.1f | %+.1f |" % (n, c, e[0], e[2], e[1], e[3], p["chord_v127"]["raw"] - p["note_v127"]["raw"],
                                                                              targets["peak_chord_v127"][c] - targets["peak_note_v127"][c]))
        w("")
    # limiter engagement, knee, profiles
    eng = [p["low"]["chord_v127"]["engaged"] for p in ps.values()] + [p["xl"]["chord_v127"]["engaged"] for p in ps.values()]
    share = sum(x <= 0.05 for x in eng) / len(eng)
    w("Limiter, velocity 127 chord: engaged in at most 5 %% of the blocks for %.1f %% of the preset renders (Key 61 and XL); median share %.2f %%, p90 %.2f %%, max %.1f %%; deepest reduction max %.2f dB." % (
        100 * share, 100 * st.median(eng), 100 * st.quantiles(eng, n=10)[-1], 100 * max(eng),
        max(max(p[x]["chord_v127"]["gr_max"] for x in PROFILES) for p in ps.values())))
    e6 = [p[x]["chord6_v127"]["engaged"] for p in ps.values() for x in PROFILES]
    w("Six note chord (36 43 48 55 60 64, velocity 127): limiter engaged at all in %d of %d renders, median share %.2f %%, deepest reduction max %.2f dB." % (
        sum(x > 0 for x in e6), len(e6), 100 * st.median(e6), max(p[x]["chord6_v127"]["gr_max"] for p in ps.values() for x in PROFILES)))
    raw = [(max(p[x][g]["raw"] for x in PROFILES for g in ("note_v127", "chord_v127", "chord6_v127")), n) for n, p in ps.items()]
    w("Raw engine peaks (note, chord, six note chord, velocity 127): max %+.2f dBFS (%s); above +3.52 dBFS (1.5): %d; above the knee +5.11 dBFS (1.8): %d." % (
        max(raw)[0], max(raw)[1], sum(r[0] > SAFE_RAW_DB for r in raw), sum(r[0] > KNEE_DB for r in raw)))
    diff = sorted(((abs(p["low"]["chord_v127"]["post"] - p["xl"]["chord_v127"]["post"]), n, p["low"]["chord_v127"]["post"], p["xl"]["chord_v127"]["post"]) for n, p in ps.items()), reverse=True)
    far = [d for d in diff if d[0] > 1.0]
    w("Key 61 against XL, chord peak velocity 127: median difference %.2f dB; more than 1 dB apart: %d presets%s" % (
        st.median(d[0] for d in diff), len(far), (": " + ", ".join("%s (%.1f / %.1f)" % (d[1], d[2], d[3]) for d in far)) if far else "."))
    text = "\n".join(L) + "\n"
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(text)
    print(text)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure")
    m.add_argument("--probe", required=True); m.add_argument("--content-dir", required=True); m.add_argument("--gain", type=float, required=True); m.add_argument("--out", required=True)
    g = sub.add_parser("gains")
    g.add_argument("--measure", required=True); g.add_argument("--targets", default=TARGETS_PATH); g.add_argument("--candidates", type=float, nargs="+", default=[0.0, -6.0, 3.0])
    s = sub.add_parser("solve")
    s.add_argument("--probe", required=True); s.add_argument("--content-dir", required=True); s.add_argument("--gain", type=float, required=True)
    s.add_argument("--targets", default=TARGETS_PATH); s.add_argument("--rounds", type=int, default=3); s.add_argument("--work", default="level-to-stock-work")
    r = sub.add_parser("report")
    r.add_argument("--measure", required=True); r.add_argument("--before"); r.add_argument("--targets", default=TARGETS_PATH); r.add_argument("--md")
    a = ap.parse_args()
    if a.cmd == "measure":
        measure(a.probe, a.content_dir, a.gain, a.out)
    elif a.cmd == "gains":
        cmd_gains(a)
    elif a.cmd == "solve":
        cmd_solve(a)
    else:
        cmd_report(a)


if __name__ == "__main__":
    main()
