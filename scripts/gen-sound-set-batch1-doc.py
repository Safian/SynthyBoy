#!/usr/bin/env python3
# SynthyBoy - writes docs/release/SOUND-SET-BATCH-1.md from docs/release/sound-set-batch1-measurements.json (numbers), scripts/sound-set-batch1-texts.py (words) and the batch 1 preset data.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   gen-sound-set-batch1-doc.py [--check]

import importlib.util
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
MEAS = ROOT / "docs" / "release" / "sound-set-batch1-measurements.json"
OUT = ROOT / "docs" / "release" / "SOUND-SET-BATCH-1.md"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, str(path))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def macro_targets(entry, doc):
    """{macro: [(destination, amount)]} of the routes of one batch entry."""
    out = {}
    params = entry["params"]
    src_name = {v: k for k, v in ((s["key"], int(s["choice"])) for s in doc["modSources"])}
    src_key = {int(s["choice"]): s["key"] for s in doc["modSources"]}
    dst_key = {int(d["choice"]): d["key"] for d in doc["modDestinations"]}
    for n in range(1, 65):
        p = f"mod{n:02d}."
        s, d = int(params.get(p + "source", 0)), int(params.get(p + "destination", 0))
        if s and d and src_key[s].startswith("macro"):
            out.setdefault(int(src_key[s][5:]), []).append((dst_key[d], float(params.get(p + "amount", 0))))
    return out


def fmtdb(v, n=1):
    return f"{v:.{n}f}"


def main():
    meas = json.loads(MEAS.read_text())
    texts = load(ROOT / "scripts" / "sound-set-batch1-texts.py", "texts").TEXTS
    batch = load(ROOT / "scripts" / "factory-presets-batch1.py", "batch1")
    pdoc = json.loads((ROOT / "docs" / "params.json").read_text())
    entries = {}
    for name, cat, tags, desc, params, content in batch.build():
        entries[name] = dict(category=cat, tags=tags, description=desc, params=params)
    mnames = batch.macro_names()
    order = list(texts)
    dists = meas["distances"]
    avg_diff = sum(d["differs_from_default"] for d in dists.values()) / len(dists)
    near_lo, near_hi = min(d["distance"] for d in dists.values()), max(d["distance"] for d in dists.values())
    L = []
    w = L.append

    w("# Sound set, batch 1: 24 new presets")
    w("")
    w("Status: **built and measured, NOT HEARD.** Nothing here was listened to (the author cannot hear): every claim below is a measurement of the offline render of the engine the MPC runs (own engine, no Vital), "
      "through the two quality profiles of the devices, and every statement about sound is an intention that the measurements support or do not contradict. The owner's ears decide; the lines "
      "\"Not settled for the ear\" name what to listen for first. Written by `scripts/gen-sound-set-batch1-doc.py` from `docs/release/sound-set-batch1-measurements.json`.")
    w("")
    w("The 24 presets are added to the set (136 now); the 112 of the first sets are unchanged, byte for byte. Each was built from Init (the default values), not from the generator's template: "
      f"on average {avg_diff:.0f} parameters differ from the defaults (the first hundred and twelve: 34 .. 64). Files: `content/presets/<Category>/*.sbpreset`; source: `scripts/factory-presets-batch1.py` and `scripts/batch1/*.py` "
      "(data written with `scripts/sb_preset_dsl.py`), names of the macros in `scripts/factory-preset-macro-names.json`, levels in `scripts/factory-preset-levels.json`.")
    w("")
    w("| Role | Presets |")
    w("|---|---|")
    groups = [("Bass (4)", ["Subterranean Duo", "Bend Formant Growl", "Triple Fold Reese", "Pitch Drop 808"]),
              ("Leads (4)", ["Sync Cry Lead", "Vowel Whistle Lead", "Triple Fifth Lead", "Bell FM Ripple Lead"]),
              ("Pads (4)", ["Stretched Glass Choir", "Mid Side Halo", "Parallel Dusk Pad", "Brass Swell Ensemble"]),
              ("Keys / plucks (3)", ["Tine Glass Keys", "Karplus Harp", "Ring Mod Gamelan"]),
              ("Textures / atmospheres (3)", ["Shepard Ascent", "Lorenz Garden", "Breath of Machines"]),
              ("Sequence / rhythm (2)", ["Pentatonic Step Pluck", "Tempo Gate Chords"]),
              ("FX / one-shots (2)", ["Bode Barber Riser", "Detonation Tail"]),
              ("Performance, pad pressure + macros (2)", ["Bowed Pressure Strings", "Wah Pressure Clav"])]
    for g, ns in groups:
        w(f"| {g} | " + ", ".join(f"{n} ({entries[n]['category']})" for n in ns) + " |")
    w("")
    w("## 1. How it was measured")
    w("")
    w("`build-sound/tools/sound-lab/synthyboy_sound_lab` (new, `tools/sound-lab/`) renders one preset offline under a device profile with scripted notes, pressure, wheel and macro settings (44.1 kHz, 128 frame blocks, quality of the profile); "
      "`scripts/sound-lab.py` analyses the files. Profiles: **XL** = `high,voices=16,control=economy` (what the MPC XL runs), **Key 61** = `low` (8 voices, unison cap 3, 2x warp, reverb / flanger / Hyper-Dimension / Bode / Convolve bypassed). "
      "The raw engine output is measured; the MPC shell adds a fixed +18 dB after it (`SYNTHYBOY_MPC_OUTPUT_GAIN_DB`, no limiter).")
    w("")
    w("- **Level**: RMS of the loudest 300 ms of a four note chord at velocity 1.0 (the measure of `scripts/tune-preset-levels.py`, reference -20 dBFS), where a swelling sound is held until its attack is over (attack + 0.4 s, at most 5 s; the first hundred are levelled after one second, which leaves slow pads far louder at the sustain). `master.volume` is tuned to -20.0 on the XL profile; the Key 61 profile is then within the table's figures.")
    w("- **Clipping**: the peak of an eight note chord at velocity 127, raw and after the +18 dB (see section 2).")
    w("- **Stereo**: correlation of L and R, the level of the mono sum against the stereo level (0 dB = fully correlated, -3 dB = uncorrelated), and the worst 1/3 octave band of the mono sum against the stereo band (a hole would be below -10 dB).")
    w("- **Clean**: DC (mean of a held note), the largest first difference in the first 3 ms against the next 100 ms (click ratio; 3 or more looks like a click), the tail (seconds after the note off until under -90 dBFS; the render window ends at 4 s, so 4.00 means 'longer').")
    w("- **Response**: velocity 30 / 80 / 127 level and centroid; for every macro, the wheel and a pressure ramp: the distance between the sound at 0 and at 100 (mean difference of the 1/3 octave bands, level, centroid, side level, beat movement, level above 6 kHz and below 200 Hz, fine-structure distance of the 5 Hz spectrum). A control counts as audible when one of: distance >= 3 dB, level >= 3 dB, centroid >= 0.3 oct, side >= 3 dB, beat >= 1 dB, HF or LF >= 4 dB, fine >= 6 dB. Macros that act on a tempo-synced or free LFO rate, on a vibrato, or on a route that only works with pressure (Wah Range) are not seen by these measures and were checked by hand (listed per preset).")
    w("- **Movement**: over a 10 s held note, the spread (5th to 95th percentile) of the spectral centroid in octaves and of the level in dB, and the mean change of the 8 Hz spectrum in 0.5 s.")
    w(f"- **Distance to the nearest preset**: the survey's similarity measure (`scripts/survey-presets.py`): the number of parameters whose values differ. The survey found the first 112 within 4 .. 71 of each other (mean 37.7, a template cluster at 15 or fewer); the 24 here are {near_lo} .. {near_hi} from the nearest of the 136 (table in section 5).")
    w("- **CPU**: `synthyboy_tier_bench` on the Mac (arm64, build Release, 3 passes, 4 s each), the engine as the MPC shell runs it: Key 61 profile `low` with 8 notes, XL profile with 16 notes, a chord struck again every second. The Mac is 15 to 25 times faster than the devices: only the comparison with the dearest existing presets in the same run counts (section 5).")
    w("")
    w("## 2. The level and the +18 dB output stage: the two rules cannot both hold")
    w("")
    r = meas["reports"]
    pk = [v["xl"]["chord8_peak_db"] for v in r.values()]
    pk_out = [v["xl"]["chord8_peak_out_db"] for v in r.values()]
    w("The brief asks for presets that sit at the set's reference level (chord RMS -20 dBFS) **and** do not clip at the +18 dB stage with an eight note chord at velocity 127. They are incompatible: -20 dBFS RMS plus 18 dB is -2 dBFS RMS, so any signal with a crest factor above 2 dB is over full scale. "
      f"Measured: the 8-note velocity-127 peak of the 24 is {max(pk):.1f} .. {min(pk):.1f} dBFS raw, which is **{min(pk_out):.1f} .. {max(pk_out):.1f} dBFS after the +18 dB**; the first 112 are in the same range (Lush Analog Pad -4.8 raw, Fat Reese -4.4 raw, Screaming Sync Lead -12.9 raw: +13.2, +13.6, +5.1 at the stage). "
      "I levelled to the reference, because the first hundred are levelled to it and the owner compares by ear, and I report the number per preset so the decision is his:")
    w("")
    w("- to be sure of 0 dBFS at the stage with the eight note chord, every preset has to go down by its figure in the table (about 11 to 22 dB), and then sits 11 to 22 dB under the first 112: not comparable;")
    w("- or the stage gets a limiter (`SYNTHYBOY_MPC_OUTPUT_LIMITER=ON` is the existing switch; the track's own limiter does it too).")
    w("")
    w("What I did within the brief: the percussive presets (Karplus Harp, Pentatonic Step Pluck, Detonation Tail) end with the compressor in **Limit** mode as the last effect (`fx.order` with the compressor last: the default order has it third, before the EQ, the chorus and the utility, which then add to the peaks), so their peak is a ceiling, not a crest; they have the smallest overshoot of the batch.")
    w("")
    w("## 3. Summary table")
    w("")
    w("Level = chord RMS in dBFS (XL / Key 61). Peak = 8-note velocity 127: raw dBFS, and after +18 dB. Mono = level of the mono sum against the stereo level (XL), worst 1/3 octave band. DC as a fraction of full scale.")
    w("")
    w("| Preset | Level XL / 61 | Peak raw -> at +18 | Mono / worst band | DC | Click | Tail s |")
    w("|---|---|---|---|---|---|---|")
    for n in order:
        x, lo = r[n]["xl"], r[n]["low"]
        w(f"| {n} | {fmtdb(x['chord_rms'])} / {fmtdb(lo['chord_rms'])} | {fmtdb(x['chord8_peak_db'])} -> {fmtdb(x['chord8_peak_out_db'])} | {fmtdb(x['mono_db'])} / {fmtdb(x['mono_worst_band_db'])} @ {x['mono_worst_band_hz']:.0f} Hz | {x['dc_max']:.4f} | {x['click_start_max']:.1f} | {x['tail_s_max']:.2f}{'+' if x['tail_s_max'] >= 3.99 else ''} |")
    w("")
    w("## 4. CPU against the budget")
    w("")
    cpu = meas["cpu"]
    base = meas["cpu_baseline"]
    w("Rule (owner): nothing new may be dearer than the current dearest preset, on the Key 61 profile (`low`, 8 notes: p99 of the dearest about 60 % of the block on the device) and on the XL profile. "
      "The same run, medians of 3 passes, share of the block: **dearest existing presets in this run**:")
    w("")
    w("| Existing preset | Key 61 median / p99 | XL median / p99 |")
    w("|---|---|---|")
    for n, v in sorted(base.items(), key=lambda kv: -kv[1]["xl"][0])[:8]:
        w(f"| {n} | {v['low'][0]:.2f} / {v['low'][1]:.2f} | {v['xl'][0]:.2f} / {v['xl'][1]:.2f} |")
    w("")
    maxlow = max(v["low"][0] for v in base.values()); maxlowp = max(v["low"][1] for v in base.values())
    maxxl = max(v["xl"][0] for v in base.values()); maxxlp = max(v["xl"][1] for v in base.values())
    w(f"Dearest existing: Key 61 median {maxlow:.2f}, p99 {maxlowp:.2f}; XL median {maxxl:.2f}, p99 {maxxlp:.2f}. **The 24 new presets:**")
    w("")
    w("| Preset | Polyphony | Key 61 median / p99 | XL median / p99 | Verdict |")
    w("|---|---|---|---|---|")
    for n in order:
        v = cpu[n]
        ok = v["low"][0] <= maxlow + 0.01 and v["xl"][0] <= maxxl + 0.01 and v["low"][1] <= maxlowp * 1.12 and v["xl"][1] <= maxxlp * 1.05
        w(f"| {n} | {int(entries[n]['params'].get('master.polyphony', 16))} | {v['low'][0]:.2f} / {v['low'][1]:.2f} | {v['xl'][0]:.2f} / {v['xl'][1]:.2f} | {'within' if ok else 'ABOVE'} |")
    w("")
    w("The tier bench plays 16 notes on the XL and 8 on the Key 61 (a chord struck every second); a preset with a polyphony below that (the heavy ones carry 4 .. 12 in `master.polyphony`) plays that many voices. That is part of the sound's definition: raise it, and the cost goes up in proportion.")
    w("")
    w("What costs what (Mac, one oscillator, base: a saw through the Digital filter = 1.31 % at `low`, 2.56 % on the XL; median share of the block):")
    w("")
    w("| Feature | Key 61 (added) | XL (added) |")
    w("|---|---|---|")
    for lab_, lo_, xl_ in meas["cost_catalog"]:
        w(f"| {lab_} | {lo_} | {xl_} |")
    w("")
    w("What this taught the design (and what had to be cut after the first bench of the batch, which exceeded the budget by 2 to 6 times): a **4x warp costs 7 .. 9 % of the XL block per oscillator and per unison voice** (so warps live on a single-voice oscillator and the width comes from an unwarped one); "
      "**any change of the frame or the morph amount of an oscillator that has a morph rebuilds its morph cycle in every voice** (+5 .. 9 % XL): one morph moves, the others are fixed and their frame is not modulated; "
      "the dear filter models (Slope, Normal, Smear, Diode, EMS, Phaser: 8 .. 10 % XL) are used once at most and never together with a warp; the effects are cheap (0.2 .. 0.8 %), so they carry the width and the depth instead.")
    w("")
    w("## 5. Distance to the nearest preset")
    w("")
    w("| Preset | Parameters differing from the defaults | Nearest preset of the 136 | Distance | Nearest of the new ones | Distance |")
    w("|---|---|---|---|---|---|")
    for n in order:
        d = meas["distances"][n]
        w(f"| {n} | {d['differs_from_default']} | {d['nearest']} | {d['distance']} | {d['nearest_new']} | {d['distance_new']} |")
    w("")
    w("## 6. The presets")
    w("")
    for n in order:
        t = texts[n]
        e = entries[n]
        x, lo = r[n]["xl"], r[n]["low"]
        w(f"### {n} ({e['category']})")
        w("")
        w(f"*Intention.* {t['intention']}")
        w("")
        w(f"*What to play.* {t['play']}")
        w("")
        targets = macro_targets(e, pdoc)
        parts = []
        for m in range(1, 9):
            nm = mnames[n][m - 1]
            if nm:
                aud = x["macros"].get(str(m))
                if aud:
                    flag = "audible" if (aud[0] >= 3 or abs(aud[1]) >= 3 or abs(aud[2]) >= 0.3 or abs(aud[3]) >= 3 or abs(aud[4]) >= 1 or abs(aud[5]) >= 4 or abs(aud[6]) >= 4 or aud[7] >= 6) else "weak (see note)"
                    parts.append(f"{nm} ({flag}: {aud[0]:.1f} dB, {aud[1]:+.1f} dB level, {aud[2]:+.2f} oct)")
                else:
                    parts.append(nm)
        w("*Macros.* " + "; ".join(parts) + ".")
        w("")
        pr, mw = x["pressure"], x["modwheel"]
        v = x["vel_level_db"]
        w(f"*Measured.* Level {fmtdb(x['chord_rms'])} dBFS (XL), {fmtdb(lo['chord_rms'])} (Key 61); 8-note velocity-127 peak {fmtdb(x['chord8_peak_db'])} dBFS raw = {fmtdb(x['chord8_peak_out_db'])} at +18 dB; mono sum {fmtdb(x['mono_db'])} dB vs stereo, worst band {fmtdb(x['mono_worst_band_db'])} dB at {x['mono_worst_band_hz']:.0f} Hz; "
          f"DC {x['dc_max']:.4f}, click ratio {x['click_start_max']:.1f}, tail {x['tail_s_max']:.2f} s{'+' if x['tail_s_max'] >= 3.99 else ''}. Velocity 30 / 80 / 127: {fmtdb(v['30'])} / {fmtdb(v['80'])} / {fmtdb(v['127'])} dB, centroid {x['vel_centroid_oct']:+.2f} oct. "
          f"Pressure ramp: {pr[0]:.1f} dB distance ({pr[1]:+.1f} dB, {pr[2]:+.2f} oct); wheel: {mw[0]:.1f} dB ({mw[1]:+.1f} dB, {mw[2]:+.2f} oct). Movement over 10 s: centroid {x['move_centroid_oct']:.2f} oct, level {x['move_level_db']:.1f} dB, flux {x['move_flux_db']:.1f} dB per 0.5 s. "
          f"CPU Key 61 {cpu[n]['low'][0]:.2f} / {cpu[n]['low'][1]:.2f} %, XL {cpu[n]['xl'][0]:.2f} / {cpu[n]['xl'][1]:.2f} % (median / p99). Nearest preset: {meas['distances'][n]['nearest']} ({meas['distances'][n]['distance']}).")
        w("")
        w(f"*On the Key 61 (`low`).* Level {fmtdb(lo['chord_rms'])} dBFS (difference {lo['chord_rms'] - x['chord_rms']:+.1f} dB), peak {fmtdb(lo['chord8_peak_db'])} dBFS raw, velocity centroid {lo['vel_centroid_oct']:+.2f} oct, mono sum {fmtdb(lo['mono_db'])} dB.")
        w("")
        w(f"*Not settled for the ear.* {t['unsure']}")
        w("")
    texts_mod = load(ROOT / "scripts" / "sound-set-batch1-texts.py", "texts2")
    w(texts_mod.TAIL)
    OUT.write_text("\n".join(L) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
