#!/usr/bin/env python3
# SynthyBoy - the numbers of docs/ui-design/OWN-ENGINE-GAP.md: for every area of the UI how many keys the own engine acts on, how many the UI draws dimmed, how many are editable and inert.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/gen-own-engine-gap.py <synthyboy_paramdump> [--check]
#   Runs "synthyboy_paramdump --own-engine-gap" (one line per key: key, group, live, dimmed, missing capability bits) and rewrites the table between the markers
#   <!-- BEGIN GENERATED TABLE --> and <!-- END GENERATED TABLE --> of docs/ui-design/OWN-ENGINE-GAP.md. --check only compares and fails when the file would change.
# The areas (regular expression over the group of the key) and the sentence "what is missing" are part of this script; a key that no area claims makes the script fail, so a new
# group of the parameter table cannot slip past the work list.
#
# Exit status: 0 up to date / written, 1 differs (--check) or an unclaimed group, 2 usage.

import collections
import os
import re
import subprocess
import sys

DOC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "ui-design", "OWN-ENGINE-GAP.md")
BEGIN = "<!-- BEGIN GENERATED TABLE (scripts/gen-own-engine-gap.py) -->"
END = "<!-- END GENERATED TABLE -->"

# (area, regular expression over the group of the key, what the own engine lacks)
AREAS = [
    ("Top bar: master volume, bend range, transpose, tune", r"^Master$",
     "Acts: volume, polyphony, bend range, velocity tracking, transpose, tune, legato, bend down range / link, stereo spread. Missing: the velocity and note curves, poly legato."),
    ("Master / Portamento (Glide)", r"^Master / Portamento$", "Acts (time, slope, always, scaled by interval)."),
    ("Master / Curves, Voicing", r"^Master / (Curves|Voicing)$", "Acts: same note rule, steal priority. Missing: velocity and note curves, voice override kill / steal, stereo routing."),
    ("Master / Scale, Tuning, MPE", r"^Master / (Scale|Tuning|MPE)$", "Key scale conforming, concert pitch, Scala / tuning files, MPE routing (step 6 of the engine work list; the own engine has the per-note tuning expression and the pedal of the event contract)."),
    ("Quality", r"^Quality$", "4x oversampling and the no-smoothing switch (warped oscillators run 4x oversampled internally)."),
    ("Voice Control", r"^Voice Control", "Per voice pan / detune / cutoff / envelope time, randomisation, scaling, the oscillator mask."),
    ("Oscillators: wavetable choice, frame, warp, level, pan, pitch", r"^(Osc \d|Oscillator \d)$",
     "Acts: oscillators 1 .. 3 (level, pan, pitch, frame, phase, destination, env 1). Missing: the pitch modes, transpose snap, fixed pitch, filter blend and sends, the frame remap, smooth interpolation."),
    ("Oscillators: wavetable table, unison, spectral morph, warp lists", r"^Osc \d / (Wavetable|Unison|Spectral Morph|Warp|Warp 2|Mapping|Remap)$",
     "Acts: the 15 built-in tables and user tables (Preset table, from the content store), the spectral morph (all 11 types), unison (voices, detune, curve, blend, spread, mode, range), warp 1 and 2 (Sync .. Squeeze, FM / RM / PD / AM from the other oscillators, the Noise, the Sub and the filters, bends, asymmetry, Remap 1-4, Odd/Even, Filter LPF / HPF, the distortion warps, the 23 spectral warps on the Spectral type), key and velocity mapping, Ratio SRC, smooth interpolation. Missing: the Serum unison law, stack styles, the frame remap with RISE / FALL, transpose snap."),
    ("Oscillators: sample, granular, spectral, multisample types", r"^Osc \d / (Sample|Granular|Spectral|Multisample)$",
     "Acts: the Sample type (pitch, start, loops, slices, scan, normalize, fade edges, unison), the Granular type (all keys but the X|Y control), the Spectral type (all keys but the X|Y control, filter mask and wavetable filter), the Multisample type (SFZ zones, envelope override, timbre, release triggers, key switches). Missing: the X|Y controls (`y_axis`, `xy_y`), choke groups across voices, the Mask / Vocode spectral warps from the Noise."),
    ("Sub oscillator", r"^Sub$", "Acts: waveform, octave, tune, level, pan, destination, env 1, phase. Missing: direct out, contiguous phase, bus sends, filter blend."),
    ("Noise oscillator", r"^Noise$", "Acts: colours, level, pan, destination, env 1, stereo. Missing: the sample noise type, pitch, fine, phase, keytrack, loop, bounce."),
    ("Mixer (buses, sends, destinations)", r"^Mixer", "The mixer graph: filter blend, buses, sends, direct output."),
    ("Filter 1 (cutoff, on)", r"^Filter 1$", "Acts: on, model, style, cutoff, resonance, drive, blend, mix, keytrack, var, level, env 1, destination (the models map onto the filters-serum types). Missing: pan, clean mode, buses, filter blend."),
    ("Filter 2, FX filter, model settings", r"^(Filter 2|Filter [12] / Model Settings|Filters|FX Filter.*)$", "Acts: filter 2 (same keys), routing, the FX filter. Missing: the formant / comb offset settings of the models, pan, clean mode."),
    ("Envelopes 1 .. 6", r"^Envelope \d", "Acts: all times, sustain and the three curves (squared amplitude, DAHDSR). Missing: the beat units and legato inverted."),
    ("LFOs 1 .. 10, random LFOs", r"^(LFO \d+|Random LFO \d)", "Acts: the 12 shapes, rate, tempo sync, key follow, sync types (retrigger / free-run), delay, fade, smooth, direction, the five random styles. Missing: the custom shape, swing, host lock, stereo offset, the envelope-like sync types (2 .. 5 run as retrigger)."),
    ("Macros 1 .. 8", r"^Macros$", "Acts."),
    ("Modulation matrix (64 slots)", r"^Mod Matrix", "Acts: source, destination, amount, power, bipolar, bypass, aux source (inv, power, output). Missing: stereo lanes, rise / fall / delay of a slot, cycle check."),
    ("FX chain: order, racks, slots (16 instances)", r"^FX / (Order|Racks|Slot \d+)$", "The racks, the 16 instance slots and the modules 12 .. 16 (Bode, Convolve, splitters); the nine standard modules, Hyper/Dimension and Utility run in the fx.order chain on the main bus."),
    ("FX: chorus, flanger, phaser", r"^FX / (Chorus|Flanger|Phaser)$", "Acts (written from public DSP knowledge, levels matched to the oracle)."),
    ("FX: delay, reverb", r"^FX / (Delay|Reverb)$", "Acts: delay (all styles, sync, filter), reverb (one feedback delay network for all types). Missing: delay offset / hq, reverb algorithms are variants of one network, low shelf, er size."),
    ("FX: compressor, distortion, EQ", r"^FX / (Compressor|Distortion|EQ)$", "Acts. Missing: the Serum compressor law, the distortion filter keytrack, the Serum EQ law."),
    ("FX: hyper dimension, utility, Bode, convolve, splitters", r"^FX / (Hyper Dimension|Utility|Bode|Convolve|Splitter .*)$", "Acts: Hyper/Dimension, Utility (fx-serum-a). Missing: Bode, Convolve, the splitters and racks."),
]


def load(paramdump):
    out = subprocess.run([paramdump, "--own-engine-gap"], check=True, stdout=subprocess.PIPE, text=True).stdout
    rows = []
    for line in out.splitlines():
        key, group, live, dimmed, missing = (line.split("\t") + [""])[:5]
        rows.append((key, group, live == "1", dimmed == "1", missing))
    return rows


def table(rows):
    totals = collections.OrderedDict((name, [0, 0, 0, 0]) for name, _, _ in AREAS)
    unclaimed = collections.Counter()
    for key, group, live, dimmed, _ in rows:
        for name, pattern, _ in AREAS:
            if re.search(pattern, group):
                t = totals[name]
                t[0] += 1
                t[1] += live
                t[2] += dimmed and not live
                t[3] += (not live) and (not dimmed)
                break
        else:
            unclaimed[group] += 1
    if unclaimed:
        print("gen-own-engine-gap: groups of the parameter table that no area claims: %s" % ", ".join("%s (%d)" % kv for kv in sorted(unclaimed.items())), file=sys.stderr)
        raise SystemExit(1)
    lines = ["| UI area (panel) | Keys | Own engine acts | Drawn dimmed (capability missing) | Editable, no effect | What is missing |", "|---|---:|---:|---:|---:|---|"]
    sums = [0, 0, 0, 0]
    for name, _, missing in AREAS:
        t = totals[name]
        sums = [a + b for a, b in zip(sums, t)]
        lines.append("| %s | %d | %d | %d | %d | %s |" % (name, t[0], t[1], t[2], t[3], missing))
    lines.append("| **All** | **%d** | **%d** | **%d** | **%d** | |" % tuple(sums))
    return "\n".join(lines)


def main(argv):
    check = "--check" in argv
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    generated = table(load(args[0]))
    with open(DOC, encoding="utf-8") as f:
        text = f.read()
    a, b = text.find(BEGIN), text.find(END)
    if a < 0 or b < a:
        print("gen-own-engine-gap: the markers are missing in %s" % DOC, file=sys.stderr)
        return 1
    updated = text[: a + len(BEGIN)] + "\n\n" + generated + "\n\n" + text[b:]
    if updated == text:
        print("gen-own-engine-gap: up to date")
        return 0
    if check:
        print("gen-own-engine-gap: %s is out of date (run scripts/gen-own-engine-gap.py <synthyboy_paramdump>)" % os.path.normpath(DOC), file=sys.stderr)
        return 1
    with open(DOC, "w", encoding="utf-8") as f:
        f.write(updated)
    print("gen-own-engine-gap: wrote %s" % os.path.normpath(DOC))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
