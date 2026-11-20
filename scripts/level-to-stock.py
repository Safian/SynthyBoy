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
# The rule (docs/release/LEVELLING-STOCK-2026-11-19.md): per preset ONE master.volume change d (dB) = the weighted least-squares fit of the dB errors against the stock medians of
# its category of (note peak v127), (note sustained v127), (sustained of the four note chord in the stock voicing 48 55 60 64, v127), on the Key 61 profile ("low") and on the
# XL profile ("high,voices=16,control=economy"); equal weights; Keys and Pluck (decaying sounds) by the note peak only (PEAK_ONLY: why there); a sustained level of a
# sound that has decayed (under -80 dBFS) is left out. A peak is what the output stage receives (raw + make-up gain), a sustained level the RMS 0.3 .. 2 s after the strike after the stage.
# The chord PEAK is a limit, not a target: the limiter may act on a velocity 127 four note chord (both voicings, both profiles), by at most 3 dB and in at most 25 % of the
# blocks; no raw engine peak above 1.4 of full scale (the knee starts at 1.8; the preset gate allows +3 dBFS = 1.41), the gate's own chord included. master.volume follows 1:1 in dB (the last gain before the knee); rounds measure what the last set.
# Clamped to the range of master.volume (docs/params.json: -60 .. +6 dB), rounded to 0.1 dB.
# Gestures (synthyboy_level_probe): note key 48 (C3 with C4 = 60), chords 36 48 55 60 (ours) and 48 55 60 64 (the stock voicing), six note chord 36 43 48 55 60 64; velocity 127 and 100;
# held 3 s after 0.3 s of silence; 48 kHz.

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
TIER_TRIM_PATH = os.path.join(SCRIPT_DIR, "factory-preset-tier-trim.json")
TARGETS_PATH = os.path.join(SCRIPT_DIR, "stock-level-targets.json")
GEN = os.path.join(SCRIPT_DIR, "gen-factory-presets.py")
PRESETS = os.path.join(ROOT_DIR, "content", "presets")
PROFILES = {"low": "low", "xl": "high,voices=16,control=economy"}
CATEGORIES = ["Bass", "Lead", "Pad", "Keys", "Pluck", "FX"]
VOLUME_MIN, VOLUME_MAX = -60.0, 6.0
KNEE_DB = 20 * __import__("math").log10(1.8)      # +5.11 dBFS: the engine's safety knee starts there
SAFE_RAW_DB = 20 * __import__("math").log10(1.4)  # +2.92 dBFS: the margin the levelling keeps under it (and under the +3 dBFS of the preset gate)


def load_tier_trims():
    if not os.path.isfile(TIER_TRIM_PATH):
        return {}
    with open(TIER_TRIM_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def load_targets(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def measure(probe, content_dir, gain, out):
    def one(key):
        r = subprocess.run([probe, "--presets", PRESETS, "--content-dir", content_dir, "--profile", PROFILES[key], "--gain", str(gain), "--limiter", "on" if LIMITER_ON else "off"],
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


# Decaying instruments (Keys, Pluck): the note peak only. Weighting the peak 2:1 against the sustained levels was tried (2026-11-19): our keys and plucks decay
# faster than the stock ones, their 0.3 .. 2 s level lies 10 to 50 dB under the stock bar readings, the sustained errors pull the volume up until the raw-peak limit,
# and 16 of 50 Keys and 18 of 44 Pluck note peaks ended more than 3 dB over their target (peak only: 1 of 50 and 0 of 44).
PEAK_ONLY = ("Keys", "Pluck")
SILENT_DB = -80.0      # a sustained level under this (the sound has decayed in the 0.3 .. 2 s window) is left out
CEILING_DB = -0.5      # the output stage's ceiling
LIMIT_GR_DB = 3.0      # the limiter budget: at most this reduction on a velocity 127 four note chord (either voicing) ...
LIMIT_SHARE = 0.25     # ... and in at most this share of its blocks
CHORDS = ("chord_v127", "chordS_v127")   # ours (36 48 55 60) and the stock voicing (48 55 60 64)
# The default sound (2026-11-19, the owner's rule): no limiter. Then every velocity 127 chord of the probe (four notes in both voicings, six, eight) must peak at most
# HARD_PEAK_DB at the stage output, so the host meter never reaches its over-0 segment. LIMITER_ON (the --limiter option) restores the budget mode of the limiter.
HARD_PEAK_DB = -1.0
HARD_CHORDS = ("chord_v127", "chordS_v127", "chord6_v127", "chord8_v127")
LIMITER_ON = False
POLY_ALPHA = 0.0   # the polyphony gain prototype (simulate only): a chord of n notes scaled by n ** -POLY_ALPHA


# The owner's simple level rule set (his latest decision 2026-11-20; overrides stock targets):
# Measured with engine default (voice-count alpha 0.5, make-up gain 0, limiter off).
# Target single-note peak at velocity 127: T = -4.5 dBFS (SAME for all categories).
# Chords: 4-note (stock voicing 48 55 60 64 and ours 36 48 55 60), 6-note and 8-note at velocity 127, both profiles (Key 61 and XL).
# d = min(T - note_peak, -0.5 - 0.05 - max_chord_peak over all those chords and both profiles).
TOP_TARGET_T = -10.0
TOP_CHORD_LIMIT = -0.55  # -0.5 - 0.05: hottest level where every full chord stays <= -0.5 dBFS without limiter
TOP_CHORDS = ("chord_v127", "chordS_v127", "chord6_v127", "chord8_v127")


def top_change(p):
    note_peak = max(p[prof]["note_v127"]["post"] for prof in PROFILES)
    max_chord_peak = max(p[prof][g]["post"] for prof in PROFILES for g in TOP_CHORDS)
    return min(TOP_TARGET_T - note_peak, TOP_CHORD_LIMIT - max_chord_peak)


def errors(p, targets, gain=None):
    """The weighted dB errors (measured - target) of the objective: note peak v127, note sustained v127, sustained of the stock-voiced chord v127, on both profiles.
    `gain` re-bases the peaks to another make-up gain (the sustained levels move with it 1:1 too). Returns [(weight, error)]."""
    cat = p["category"]
    out = []
    for prof in PROFILES:
        r = p[prof]
        shift = 0.0 if gain is None else gain - r["gain"]
        out.append((1.0, r["note_v127"]["pre"] + shift - targets["peak_note_v127"][cat]))
        if cat in PEAK_ONLY:
            continue
        if r["note_v127"]["sus"] > SILENT_DB:
            out.append((1.0, r["note_v127"]["sus"] + shift - targets["sustain_note_v127"][cat]))
        if r["chordS_v127"]["sus"] > SILENT_DB:
            out.append((1.0, r["chordS_v127"]["sus"] + shift + poly("chordS_v127") - targets["sustain_chord_v127"][cat]))
    return out


def needed_change(p, targets, gain=None):
    e = errors(p, targets, gain)
    return -sum(w * x for w, x in e) / sum(w for w, _ in e)


GATE_LIMIT_DB = 2.85   # the preset gate (synthyboy_preset_render) fails a raw peak above +3 dBFS; the levelling keeps 0.15 dB under it


def gate_peaks(gate, content_dir):
    """The raw peaks of the preset gate's own renders (synthyboy_preset_render, own engine, tiers low / medium / high: its single note and its chord; the highest of the
    tiers), by preset name. Its gestures differ from the probe's (velocity 0.8 and 1.0, 1 s chord with a release, 256 frame blocks, no silence first)."""
    r = subprocess.run([gate, "--presets", PRESETS, "--engine", "own", "--content-dir", content_dir], capture_output=True, text=True)
    by_file = {}
    for line in r.stdout.splitlines():
        w = line.split()
        if len(w) >= 4 and w[0].endswith(".sbpreset"):
            by_file[w[0]] = max(by_file.get(w[0], -200.0), float(w[2]), float(w[3]))
    names = {}
    for cat in CATEGORIES:
        d = os.path.join(PRESETS, cat)
        for f in sorted(os.listdir(d)):
            if f.endswith(".sbpreset"):
                with open(os.path.join(d, f), "r", encoding="utf-8") as fh:
                    names[json.load(fh)["meta"]["name"]] = by_file.get(f)
    return names


NOTES = {"chord_v127": 4, "chordS_v127": 4, "chord6_v127": 6, "chord8_v127": 8, "chordG_v127": 4}


def poly(g):
    """The level change of the polyphony gain prototype for a gesture (0 dB for a note, or with POLY_ALPHA 0)."""
    n = NOTES.get(g, 1)
    return -20.0 * POLY_ALPHA * __import__("math").log10(n) if POLY_ALPHA else 0.0


def constrained_change(p, targets, gate_peak=None):
    """The fit, then the limits. Without the limiter (the default): every velocity 127 chord (HARD_CHORDS, both profiles) at most HARD_PEAK_DB at the output. With it
    (LIMITER_ON): the limiter budget on both four note chords (the peak it sees at most LIMIT_GR_DB over the ceiling; a chord whose limiter acted in more than LIMIT_SHARE of
    the blocks goes down by 0.3 dB per round). Always: no raw engine peak above SAFE_RAW_DB, the preset gate's raw peaks under GATE_LIMIT_DB."""
    d = needed_change(p, targets)
    for prof in PROFILES:
        if LIMITER_ON:
            for g in CHORDS:
                r = p[prof][g]
                d = min(d, CEILING_DB + LIMIT_GR_DB - 0.1 - r["pre"])
                if r["engaged"] > LIMIT_SHARE:
                    d = min(d, -0.3)   # over the share: a small step down per round (a larger one swings back and forth with the fit)
        else:
            for g in HARD_CHORDS:
                d = min(d, HARD_PEAK_DB - 0.05 - (p[prof][g]["pre"] + poly(g)))
    raw = max(p[x][g]["raw"] + poly(g) for x in PROFILES for g in ("note_v127", "chord_v127", "chordS_v127", "chord6_v127", "chord8_v127", "chordG_v127"))
    d = min(d, SAFE_RAW_DB - 0.05 - raw)
    if gate_peak is not None:
        d = min(d, GATE_LIMIT_DB - gate_peak)
    return d


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
        over = [n for n, p in ps.items() if levels[n] + min(needed_change(p, targets, g), constrained_change(p, targets) - (g - p["low"]["gain"])) > VOLUME_MAX]
        under = [n for n, p in ps.items() if levels[n] + needed_change(p, targets, g) < VOLUME_MIN]
        q = st.quantiles(d, n=10)
        print("  %+5.1f dB: median %+.2f  p10 %+.2f  p90 %+.2f  min %+.2f  max %+.2f   over +6: %d %s  under -60: %d" % (g, st.median(d), q[0], q[-1], d[0], d[-1], len(over), sorted(over)[:6], len(under)))
    # the rule: the gain at which the median preset needs no change, in steps of 0.5 dB, raised until no preset needs more than the top of master.volume
    g0 = a.candidates[0]
    med = st.median(needed_change(p, targets, g0) for p in ps.values())
    g = round((g0 + med) * 2) / 2
    while any(levels[n] + min(needed_change(p, targets, g), constrained_change(p, targets) - (g - p["low"]["gain"])) > VOLUME_MAX for n, p in ps.items()):
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
        tier_trims = load_tier_trims()
        gates = gate_peaks(a.gate, a.content_dir) if a.gate else {}
        moved = 0.0
        new_levels = {}
        new_tier_trims = {}
        for n, v in levels.items():
            p = ps.get(n)
            if p is None:
                new_levels[n] = v
                if n in tier_trims:
                    new_tier_trims[n] = tier_trims[n]
                continue
            if getattr(a, "rule", "stock") == "top":
                # Per-profile levelling (owner decision 2026-11-20):
                # T = -10.0 dBFS, chords <= -0.5 dBFS (solve to -0.55).
                # d_p = min(T - note_p, -0.55 - maxchord_p) for each profile p.
                # master.volume = base + d_high (the XL value).
                # tierTrimDb = d_low - d_high, clamped to -6..+9, written when magnitude >= 0.5 dB.
                note_high = p["xl"]["note_v127"]["post"]
                maxchord_high = max(p["xl"][g]["post"] for g in TOP_CHORDS)
                d_high = min(TOP_TARGET_T - note_high, TOP_CHORD_LIMIT - maxchord_high)

                note_low = p["low"]["note_v127"]["post"]
                maxchord_low = max(p["low"][g]["post"] for g in TOP_CHORDS)
                d_low = min(TOP_TARGET_T - note_low, TOP_CHORD_LIMIT - maxchord_low)

                if a.gate and n in gates:
                    d_high = min(d_high, GATE_LIMIT_DB - gates[n])
                    d_low = min(d_low, GATE_LIMIT_DB - gates[n])

                cur_trim = tier_trims.get(n, 0.0)
                raw_trim = cur_trim + (d_low - d_high)
                clamped_trim = round(min(9.0, max(-6.0, raw_trim)), 1)
                if abs(clamped_trim) >= 0.5:
                    new_tier_trims[n] = clamped_trim

                # a preset whose peaks do not follow master.volume 1:1: the slope seen in the last round scales the step
                if prev is not None and n in prev and abs(applied.get(n, 0.0)) >= 0.3:
                    seen = st.mean(p[x]["note_v127"]["pre"] - prev[n][x]["note_v127"]["pre"] for x in PROFILES)
                    slope = min(1.5, max(0.3, seen / applied[n]))
                    d_high = d_high / slope

                raw_vol = v + d_high
                if raw_vol > VOLUME_MAX:
                    missing = raw_vol - VOLUME_MAX
                    note_pk = max(p[prof]["note_v127"]["pre"] for prof in PROFILES)
                    note_reached = note_pk + (VOLUME_MAX - v)
                    print("  [volume range exceeded] '%s': needed master.volume %+.2f dB (> %+.1f dB, missing %.2f dB); note peak reaches %+.2f dBFS instead of %+.1f dBFS" % (
                        n, raw_vol, VOLUME_MAX, missing, note_reached, TOP_TARGET_T))
                nv = round(min(VOLUME_MAX, max(VOLUME_MIN, raw_vol)), 1)
                applied[n] = nv - v
                trim_delta = abs(clamped_trim - cur_trim)
                moved = max(moved, abs(nv - v), trim_delta)
                new_levels[n] = nv
            else:
                d = constrained_change(p, targets, gates.get(n))
                if prev is not None and n in prev and abs(applied.get(n, 0.0)) >= 0.3:
                    seen = st.mean(p[x]["note_v127"]["pre"] - prev[n][x]["note_v127"]["pre"] for x in PROFILES)
                    slope = min(1.5, max(0.3, seen / applied[n]))
                    d = d / slope
                raw_vol = v + d
                nv = round(min(VOLUME_MAX, max(VOLUME_MIN, raw_vol)), 1)
                applied[n] = nv - v
                moved = max(moved, abs(nv - v))
                new_levels[n] = nv
        prev = ps
        with open(LEVELS_PATH, "w", encoding="utf-8") as f:
            json.dump(dict(sorted(new_levels.items())), f, indent=1, ensure_ascii=False)
            f.write("\n")
        with open(TIER_TRIM_PATH, "w", encoding="utf-8") as f:
            json.dump(dict(sorted(new_tier_trims.items())), f, indent=1, ensure_ascii=False)
            f.write("\n")
        subprocess.run([sys.executable, "-I", GEN, "--out", PRESETS], check=True, capture_output=True)
        print("round %d: largest move %.2f dB" % (rnd + 1, moved))
        if moved <= 0.05:
            break
    ps = by_preset(measure(a.probe, a.content_dir, a.gain, os.path.join(a.work, "final.jsonl")))
    print("measurements in", a.work)


def med(xs):
    xs = [x for x in xs if x is not None and x > -150]
    return st.median(xs) if xs else float("nan")


def cmd_simulate(a):
    """The polyphony gain prototype on paper: from one measurement, the fit and its limits with every chord scaled by n ** -alpha (exact for a linear voice bus and effects;
    the prototype is not in the engine). Prints, per category, the note peak / note sustained / chord sustained after the fit, and the presets the limits hold back."""
    global POLY_ALPHA
    targets = load_targets(a.targets)
    ps = by_preset(read_measure(a.measure))
    levels = current_levels()
    for alpha in a.alphas:
        POLY_ALPHA = alpha
        rows = {}
        held = []
        for n, p in ps.items():
            d = constrained_change(p, targets)
            d = min(VOLUME_MAX - levels[n], max(VOLUME_MIN - levels[n], d))
            free = needed_change(p, targets)
            e = errors(p, targets)
            mean = st.mean(x for _, x in e) + d
            if abs(mean) > 3.0:
                held.append("%s %+.1f" % (n, mean))
            c = p["category"]
            r = p["low"]
            rows.setdefault(c, []).append((r["note_v127"]["pre"] + d, r["note_v127"]["sus"] + d if r["note_v127"]["sus"] > SILENT_DB else None,
                                           r["chordS_v127"]["sus"] + d + poly("chordS_v127") if r["chordS_v127"]["sus"] > SILENT_DB else None,
                                           r["chordS_v127"]["pre"] + d + poly("chordS_v127") - (r["note_v127"]["pre"] + d)))
        print("alpha %.2f (a four note chord %+.1f dB, eight %+.1f dB): Key 61 medians, target in brackets" % (alpha, -20 * alpha * __import__("math").log10(4), -20 * alpha * __import__("math").log10(8)))
        for c in CATEGORIES:
            v = rows[c]
            print("  %-5s note peak %6.1f (%5.1f)  note sus %6.1f (%5.1f)  chord sus %6.1f (%5.1f)  chord - note peak %+5.1f (%+.1f)" % (
                c, med(x[0] for x in v), targets["peak_note_v127"][c], med(x[1] for x in v), targets["sustain_note_v127"][c], med(x[2] for x in v), targets["sustain_chord_v127"][c],
                med(x[3] for x in v), targets["peak_chord_v127"][c] - targets["peak_note_v127"][c]))
        print("  held back (mean error beyond 3 dB): %d: %s" % (len(held), ", ".join(sorted(held))))
    POLY_ALPHA = 0.0


def cmd_report(a):
    targets = load_targets(a.targets)
    rows = read_measure(a.measure)
    ps = by_preset(rows)
    before = by_preset(read_measure(a.before)) if a.before else None
    L = []
    w = L.append
    w("Targets (`%s`): %s" % (os.path.relpath(a.targets, ROOT_DIR), targets.get("notes", "")))
    w("")
    w("Make-up gain of the measurement: %+.1f dB. Medians per category; peaks in dBFS after the output stage (what the host receives; the limiter's ceiling is -0.5 dBFS), "
      "sustained = RMS 0.3 .. 2.0 s after the strike (presets silent there left out); chord = the stock voicing 48 55 60 64 unless marked \"ours\" (36 48 55 60); target in brackets." % rows[0]["gain"])
    w("")
    for prof in PROFILES:
        w("**%s profile**" % ("Key 61 (low)" if prof == "low" else "XL (high, 16 voices, economy control)"))
        w("")
        head = "| category | n | note peak | note sustained | chord sustained | chord peak | chord peak, ours | six note chord peak | note v127-v100 | chord v127-v100 |"
        if before:
            head += " before: note peak / note sus / chord peak / chord sus (ours, ov40 levels at +9 dB) |"
        w(head)
        w("|---|---|---|---|---|---|---|---|---|---|" + ("---|" if before else ""))
        for c in CATEGORIES + ["ALL"]:
            sel = [p[prof] for p in ps.values() if c == "ALL" or p["category"] == c]
            def t(key):
                return "%.1f" % targets[key][c] if c != "ALL" else "-"
            def tv(k1, k2):
                return "%.1f" % (targets[k1][c] - targets[k2][c]) if c != "ALL" else "-"
            cells = [c, str(len(sel)),
                     "%.1f (%s)" % (med(r["note_v127"]["post"] for r in sel), t("peak_note_v127")),
                     "%.1f (%s)" % (med(r["note_v127"]["sus"] for r in sel), t("sustain_note_v127")),
                     "%.1f (%s)" % (med(r["chordS_v127"]["sus"] for r in sel), t("sustain_chord_v127")),
                     "%.1f (%s)" % (med(r["chordS_v127"]["post"] for r in sel), t("peak_chord_v127")),
                     "%.1f" % med(r["chord_v127"]["post"] for r in sel),
                     "%.1f (%s)" % (med(r["chord6_v127"]["post"] for r in sel), t("peak_chord6_v127")),
                     "%.1f (%s)" % (med(r["note_v127"]["pre"] - r["note_v100"]["pre"] for r in sel), tv("peak_note_v127", "peak_note_v100")),
                     "%.1f (%s)" % (med(r["chordS_v127"]["pre"] - r["chordS_v100"]["pre"] for r in sel), tv("peak_chord_v127", "peak_chord_v100"))]
            if before:
                bs = [p[prof] for p in before.values() if c == "ALL" or p["category"] == c]
                cells.append("%.1f / %.1f / %.1f / %.1f" % (med(r["note_v127"]["post"] for r in bs), med(r["note_v127"]["sus"] for r in bs),
                                                            med(r["chord_v127"]["post"] for r in bs), med(r["chord_v127"]["sus"] for r in bs)))
            w("| " + " | ".join(cells) + " |")
        w("")
    # voicing
    w("**Voicing** (Key 61, medians of ours 36 48 55 60 minus the stock voicing 48 55 60 64, dB): " + ", ".join(
        "%s peak %+.1f / sustained %+.1f" % (c, med(p["low"]["chord_v127"]["post"] - p["low"]["chordS_v127"]["post"] for p in ps.values() if p["category"] == c),
                                             med(p["low"]["chord_v127"]["sus"] - p["low"]["chordS_v127"]["sus"] for p in ps.values() if p["category"] == c)) for c in CATEGORIES) + ".")
    w("")
    w("**Chord against note** (Key 61, stock voicing, medians, dB): peak of the chord minus peak of the note, raw: " + ", ".join(
        "%s %+.1f (strike, first 100 ms: %+.1f; stock %+.1f)" % (c, med(p["low"]["chordS_v127"]["raw"] - p["low"]["note_v127"]["raw"] for p in ps.values() if p["category"] == c),
                                                                  med(p["low"]["chordS_v127"]["early"] - p["low"]["note_v127"]["early"] for p in ps.values() if p["category"] == c),
                                                                  targets["peak_chord_v127"][c] - targets["peak_note_v127"][c]) for c in CATEGORIES)
      + "; sustained chord minus note: " + ", ".join("%s %+.1f (stock %+.1f)" % (c, med(p["low"]["chordS_v127"]["sus"] - p["low"]["note_v127"]["sus"] for p in ps.values() if p["category"] == c and p["low"]["note_v127"]["sus"] > SILENT_DB),
                                                                                   targets["sustain_chord_v127"][c] - targets["sustain_note_v127"][c]) for c in CATEGORIES) + ".")
    w("")
    # residuals of the objective
    res = []
    for n, p in sorted(ps.items()):
        e = errors(p, targets)
        res.append((max(abs(x) for _, x in e), n, p["category"], p))
    big = [r for r in res if r[0] > 3.0]
    w("Residuals of the objective (dB; per preset the largest of its errors on both profiles): median %.2f, p90 %.2f, max %.2f; above 3 dB: %d presets." % (
        st.median(r[0] for r in res), st.quantiles([r[0] for r in res], n=10)[-1], max(r[0] for r in res), len(big)))
    w("")
    if big:
        w("| preset | category | note peak error Key 61 / XL | note sustained error | chord sustained error | limited by |")
        w("|---|---|---|---|---|---|")
        for m, n, c, p in sorted(big, key=lambda r: -r[0]):
            def err(prof, g, k, tk):
                v = p[prof][g][k]
                return (v - targets[tk][c]) if (k == "pre" or v > SILENT_DB) else None
            f = lambda x: "-" if x is None else "%+.1f" % x
            lim = []
            if any(p[x][g]["pre"] >= CEILING_DB + LIMIT_GR_DB - 0.3 for x in PROFILES for g in CHORDS):
                lim.append("limiter budget")
            if max(p[x][g]["raw"] for x in PROFILES for g in ("note_v127", "chord_v127", "chordS_v127", "chord6_v127", "chordG_v127")) >= SAFE_RAW_DB - 0.3:
                lim.append("raw peak")
            w("| %s | %s | %s / %s | %s / %s | %s / %s | %s |" % (n, c, f(err("low", "note_v127", "pre", "peak_note_v127")), f(err("xl", "note_v127", "pre", "peak_note_v127")),
                                                             f(err("low", "note_v127", "sus", "sustain_note_v127")), f(err("xl", "note_v127", "sus", "sustain_note_v127")),
                                                             f(err("low", "chordS_v127", "sus", "sustain_chord_v127")), f(err("xl", "chordS_v127", "sus", "sustain_chord_v127")),
                                                             ", ".join(lim) or "the spread of its own errors"))
        w("")
    for g, name in (("chord_v127", "ours, 36 48 55 60"), ("chordS_v127", "stock voicing, 48 55 60 64")):
        gr = [p[x][g]["gr_max"] for p in ps.values() for x in PROFILES]
        sh = [p[x][g]["engaged"] for p in ps.values() for x in PROFILES]
        ok = sum(1 for p in ps.values() for x in PROFILES if p[x][g]["gr_max"] <= LIMIT_GR_DB + 0.05 and p[x][g]["engaged"] <= LIMIT_SHARE)
        w("Limiter, four note chord velocity 127 (%s, 3 s): within the budget (at most %.0f dB, at most %.0f %% of the blocks) on %d of %d renders (%.1f %%); deepest reduction median %.2f, max %.2f dB; share of blocks median %.1f %%, p90 %.1f %%, max %.1f %%." % (
            name, LIMIT_GR_DB, 100 * LIMIT_SHARE, ok, len(gr), 100.0 * ok / len(gr), st.median(gr), max(gr), 100 * st.median(sh), 100 * st.quantiles(sh, n=10)[-1], 100 * max(sh)))
    e6 = [p[x]["chord6_v127"] for p in ps.values() for x in PROFILES]
    w("Six note chord (36 43 48 55 60 64, velocity 127): the limiter acts in %d of %d renders; deepest reduction median %.2f, max %.2f dB; share of blocks median %.1f %%, max %.1f %%." % (
        sum(r["engaged"] > 0 for r in e6), len(e6), st.median(r["gr_max"] for r in e6), max(r["gr_max"] for r in e6), 100 * st.median(r["engaged"] for r in e6), 100 * max(r["engaged"] for r in e6)))
    post = max(p[x][g]["post"] for p in ps.values() for x in PROFILES for g in ("note_v127", "chord_v127", "chordS_v127", "chord6_v127", "chordG_v127"))
    w("Highest sample the host receives on any of these gestures: %.2f dBFS (the ceiling)." % post)
    raw = [(max(p[x][g]["raw"] for x in PROFILES for g in ("note_v127", "chord_v127", "chordS_v127", "chord6_v127", "chordG_v127")), n) for n, p in ps.items()]
    w("Raw engine peaks: max %+.2f dBFS (%s); above +2.92 dBFS (1.4): %d; above the knee +5.11 dBFS (1.8): %d." % (
        max(raw)[0], max(raw)[1], sum(r[0] > SAFE_RAW_DB for r in raw), sum(r[0] > KNEE_DB for r in raw)))
    diff = sorted(((abs(p["low"]["chordS_v127"]["sus"] - p["xl"]["chordS_v127"]["sus"]), n, p["low"]["chordS_v127"]["sus"], p["xl"]["chordS_v127"]["sus"]) for n, p in ps.items() if p["low"]["chordS_v127"]["sus"] > SILENT_DB), reverse=True)
    far = [d for d in diff if d[0] > 1.0]
    w("Key 61 against XL, sustained chord velocity 127: median difference %.2f dB; more than 1 dB apart: %d presets%s" % (
        st.median(d[0] for d in diff), len(far), (": " + ", ".join("%s (%.1f / %.1f)" % (d[1], d[2], d[3]) for d in far)) if far else "."))
    text = "\n".join(L) + "\n"
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(text)
    print(text)


def cmd_table(a):
    rows = read_measure(a.measure)
    ps = by_preset(rows)
    levels = current_levels()
    tier_trims = load_tier_trims()
    L = []
    w = L.append
    w("# Factory Preset Output Level Table (2026-11-20)")
    w("")
    w("The owner's latest simple level rule (2026-11-20):")
    w("- Engine default: voice-count alpha 0.5, make-up gain 0 dB, limiter OFF.")
    w("- Target single-note peak at velocity 127: T = -10.0 dBFS across all categories (supersedes -4.5 dBFS).")
    w("- Full chords (4-note stock and ours, 6-note, 8-note at velocity 127) at most -0.5 dBFS without limiter.")
    w("- Per-profile solve: d_p = min(T - note_p, -0.55 - maxchord_p) for each profile.")
    w("- master.volume = base + d_high (XL); tierTrimDb = d_low - d_high written when |tierTrimDb| >= 0.5 dB.")
    w("- At least 5 dB room to master.volume cap (+6 dB): master.volume <= +1.0 dB.")
    w("- Velocity tracking = 100% for all presets (engine law lerp(1, v^2, t)).")
    w("")
    w("| Category | Preset | Note Peak v127 (K61) | Note Peak v127 (XL) | K61 vs XL Diff | 4-Note Chord | 6-Note Chord | 8-Note Chord | v127 - v100 | v127 - v64 | master.volume | Room to +6 | tierTrimDb | Flag |")
    w("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")

    col_note_low = []
    col_note_xl = []
    col_k61_xl_diff = []
    col_chord4 = []
    col_chord6 = []
    col_chord8 = []
    col_v100_diff = []
    col_v64_diff = []
    col_vol = []
    col_room = []
    col_tier_trim = []

    outside_v64 = []
    chords_over_limit = []
    room_under_5 = []
    k61_xl_over_1 = []
    notes_outside_window = []

    cat_notes = {}

    for n in sorted(ps.keys(), key=lambda x: (ps[x]["category"], x)):
        p = ps[n]
        cat = p["category"]
        vol = levels.get(n, 0.0)
        room = 6.0 - vol
        trim = tier_trims.get(n, 0.0)

        n_low = p["low"]["note_v127"]["post"]
        n_xl = p["xl"]["note_v127"]["post"]
        k61_xl_diff = abs(n_low - n_xl)

        c4 = max(p[x][g]["post"] for x in PROFILES for g in ("chord_v127", "chordS_v127"))
        c6 = max(p[x]["chord6_v127"]["post"] for x in PROFILES)
        c8 = max(p[x]["chord8_v127"]["post"] for x in PROFILES)

        v100 = p["low"]["note_v100"]["post"]
        v100_diff = n_low - v100

        v64 = p["low"]["note_v64"]["post"] if "note_v64" in p["low"] else (p["low"]["note_v100"]["post"] + 6.0)
        v64_diff = n_low - v64

        col_note_low.append(n_low)
        col_note_xl.append(n_xl)
        col_k61_xl_diff.append(k61_xl_diff)
        col_chord4.append(c4)
        col_chord6.append(c6)
        col_chord8.append(c8)
        col_v100_diff.append(v100_diff)
        col_v64_diff.append(v64_diff)
        col_vol.append(vol)
        col_room.append(room)
        col_tier_trim.append(trim)

        cat_notes.setdefault(cat, []).append(n_low)
        cat_notes.setdefault(cat + "_xl", []).append(n_xl)

        if v64_diff < 6.0:
            outside_v64.append((n, v64_diff))

        max_chord = max(c4, c6, c8)
        if max_chord > -0.5:
            chords_over_limit.append((n, max_chord))

        if room < 5.0:
            room_under_5.append((n, vol, room))

        if k61_xl_diff > 1.0:
            k61_xl_over_1.append((n, k61_xl_diff))

        for n_val, prof_label in [(n_low, "K61"), (n_xl, "XL")]:
            if not (-12.0 <= n_val <= -9.5):
                notes_outside_window.append((n, prof_label, n_val))

        flag = "-"
        if room < 5.0:
            flag = "ROOM<5dB"
        elif max_chord > -0.5:
            flag = "CHORD>-0.5"
        elif not (-12.0 <= n_low <= -9.5 and -12.0 <= n_xl <= -9.5):
            flag = "NOTE_WINDOW"

        trim_str = f"{trim:+.1f}" if abs(trim) >= 0.5 else "0.0"
        w(f"| {cat} | {n} | {n_low:+.2f} | {n_xl:+.2f} | {k61_xl_diff:.2f} | {c4:+.2f} | {c6:+.2f} | {c8:+.2f} | {v100_diff:.2f} | {v64_diff:.2f} | {vol:+.1f} | {room:.1f} | {trim_str} | {flag} |")

    w("")
    w(f"## Summary Statistics (all {len(ps)} presets)")
    w("")
    w("| Metric | Median | Min | Max | Target / Acceptance Window |")
    w("|---|---|---|---|---|")
    w(f"| Note Peak v127 (Key 61) | {st.median(col_note_low):+.2f} dBFS | {min(col_note_low):+.2f} dBFS | {max(col_note_low):+.2f} dBFS | -10.00 dBFS (window -12.0 .. -9.5 dBFS) |")
    w(f"| Note Peak v127 (XL) | {st.median(col_note_xl):+.2f} dBFS | {min(col_note_xl):+.2f} dBFS | {max(col_note_xl):+.2f} dBFS | -10.00 dBFS (window -12.0 .. -9.5 dBFS) |")
    w(f"| Key 61 vs XL Note Difference | {st.median(col_k61_xl_diff):.2f} dB | {min(col_k61_xl_diff):.2f} dB | {max(col_k61_xl_diff):.2f} dB | <= 1.00 dB |")
    w(f"| 4-Note Chord Peak | {st.median(col_chord4):+.2f} dBFS | {min(col_chord4):+.2f} dBFS | {max(col_chord4):+.2f} dBFS | <= -0.50 dBFS |")
    w(f"| 6-Note Chord Peak | {st.median(col_chord6):+.2f} dBFS | {min(col_chord6):+.2f} dBFS | {max(col_chord6):+.2f} dBFS | <= -0.50 dBFS |")
    w(f"| 8-Note Chord Peak | {st.median(col_chord8):+.2f} dBFS | {min(col_chord8):+.2f} dBFS | {max(col_chord8):+.2f} dBFS | <= -0.50 dBFS |")
    w(f"| v127 - v100 Dynamics Diff | {st.median(col_v100_diff):.2f} dB | {min(col_v100_diff):.2f} dB | {max(col_v100_diff):.2f} dB | median >= 3.50 dB |")
    w(f"| v127 - v64 Dynamics Diff | {st.median(col_v64_diff):.2f} dB | {min(col_v64_diff):.2f} dB | {max(col_v64_diff):.2f} dB | all >= 6.00 dB |")
    w(f"| master.volume | {st.median(col_vol):+.1f} dB | {min(col_vol):+.1f} dB | {max(col_vol):+.1f} dB | <= +1.0 dB (min 5 dB room) |")
    w(f"| Room to +6 dB cap | {st.median(col_room):.1f} dB | {min(col_room):.1f} dB | {max(col_room):.1f} dB | >= 5.0 dB |")
    w("")
    w("### Per-Category Note Medians (Acceptance: within 0.5 dB of -10.0 dBFS)")
    for cat in CATEGORIES:
        med_k61 = st.median(cat_notes[cat])
        med_xl = st.median(cat_notes[cat + "_xl"])
        pass_k61 = abs(med_k61 - (-10.0)) <= 0.5
        pass_xl = abs(med_xl - (-10.0)) <= 0.5
        w(f"- **{cat}**: Key 61 median {med_k61:+.2f} dBFS ({'PASS' if pass_k61 else 'FAIL'}), XL median {med_xl:+.2f} dBFS ({'PASS' if pass_xl else 'FAIL'})")
    w("")
    w("### Offline Acceptance Verification")
    w(f"- Chords over -0.5 dBFS: {len(chords_over_limit)}")
    for n, pk in chords_over_limit:
        w(f"  - FAIL: {n} chord peak {pk:+.2f} dBFS > -0.5 dBFS")
    w(f"- Key 61 vs XL Note Diff > 1.0 dB: {len(k61_xl_over_1)}")
    for n, diff in k61_xl_over_1:
        w(f"  - FAIL: {n} diff {diff:.2f} dB > 1.0 dB")
    w(f"- Velocity 127 vs 64 Diff < 6.0 dB: {len(outside_v64)}")
    for n, diff in outside_v64:
        w(f"  - FAIL: {n} span {diff:.2f} dB < 6.0 dB")
    w(f"- Presets with Room to +6 dB < 5.0 dB (master.volume > +1.0 dB): {len(room_under_5)}")
    for n, vol, rm in room_under_5:
        w(f"  - FAIL: {n} vol {vol:+.1f} dB, room {rm:.1f} dB < 5.0 dB")
    w(f"- Single Note Peaks Outside Window [-12.0, -9.5] dBFS: {len(notes_outside_window)}")
    for n, prof, pk in notes_outside_window:
        w(f"  - FAIL: {n} ({prof}): {pk:+.2f} dBFS outside [-12.0, -9.5] dBFS")
    w("")
    text = "\n".join(L) + "\n"
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(text)
    print(f"Table written with {len(ps)} presets to {a.out if a.out else 'stdout'}.")
    print(f"Median v127-v100: {st.median(col_v100_diff):.2f} dB, v127-v64: {st.median(col_v64_diff):.2f} dB")
    print(f"Chords over -0.5: {len(chords_over_limit)}, K61 vs XL > 1dB: {len(k61_xl_over_1)}, Room < 5dB: {len(room_under_5)}, Notes outside [-12, -9.5]: {len(notes_outside_window)}")


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
    s.add_argument("--gate", help="synthyboy_preset_render: its raw peaks (every tier) are kept under +2.85 dBFS so the preset gate passes")
    s.add_argument("--rule", choices=["stock", "top"], default="stock", help="top: owner's 2026-11-20 level rule (T=-4.5 dBFS, chords <= -0.5 dBFS)")
    sm = sub.add_parser("simulate")
    sm.add_argument("--measure", required=True); sm.add_argument("--targets", default=TARGETS_PATH); sm.add_argument("--alphas", type=float, nargs="+", default=[0.0, 0.25, 0.5])
    r = sub.add_parser("report")
    r.add_argument("--measure", required=True); r.add_argument("--before"); r.add_argument("--targets", default=TARGETS_PATH); r.add_argument("--md")
    t = sub.add_parser("table")
    t.add_argument("--measure", required=True); t.add_argument("--out")
    a = ap.parse_args()
    if a.cmd == "measure":
        measure(a.probe, a.content_dir, a.gain, a.out)
    elif a.cmd == "gains":
        cmd_gains(a)
    elif a.cmd == "solve":
        cmd_solve(a)
    elif a.cmd == "simulate":
        cmd_simulate(a)
    elif a.cmd == "report":
        cmd_report(a)
    else:
        cmd_table(a)


if __name__ == "__main__":
    main()
