#!/usr/bin/env python3
# SynthyBoy - writes docs/release/SOUND-SET-BATCH-2.md from docs/release/sound-set-batch2-measurements.json (numbers, scripts/measure-batch2.py), scripts/sound-set-batch2-texts.py (words)
# and the batch 2 preset data (scripts/batch2/*.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   gen-sound-set-batch2-doc.py [--check]

import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = ROOT / "scripts"
sys.path.insert(0, str(HERE))
MEAS = ROOT / "docs" / "release" / "sound-set-batch2-measurements.json"
OUT = ROOT / "docs" / "release" / "SOUND-SET-BATCH-2.md"
CONTENT = ROOT / "content"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, str(path))
    m = importlib.util.module_from_spec(spec)
    sys.modules[name] = m
    spec.loader.exec_module(m)
    return m


def fmt(v, n=1):
    return f"{v:.{n}f}"


def census(params, doc):
    """What a preset spends, from its parameters: the types of the oscillators, polyphony, unison voices, filters, effects (names as the keys say)."""
    ch = {p["key"]: p.get("choices") for p in doc["params"]}
    dflt = {p["key"]: float(p["default"]) for p in doc["params"]}
    g = lambda k: float(params.get(k, dflt[k]))
    oscs = []
    for n in (1, 2, 3):
        if g(f"osc{n}.on") >= 0.5:
            t = ch[f"osc{n}.type"][int(g(f"osc{n}.type"))]
            u = int(g(f"osc{n}.unison.voices"))
            if t == "Wavetable":
                w = int(g(f"osc{n}.warp.type"))
                t = ch[f"osc{n}.wavetable"][int(g(f"osc{n}.wavetable"))]
                if w:
                    t += " + warp " + ch[f"osc{n}.warp.type"][w]
            else:
                w = int(g(f"osc{n}.warp.type"))
                if w:
                    t += " + warp " + ch[f"osc{n}.warp.type"][w]
            oscs.append(f"{n}: {t} x{u}")
    if g("sub.on") >= 0.5:
        oscs.append("sub")
    if g("noise.on") >= 0.5:
        oscs.append("noise")
    fx = [name for name, key in (("chorus", "chorus.on"), ("phaser", "phaser.on"), ("flanger", "flanger.on"), ("delay", "delay.on"), ("reverb", "reverb.on"), ("distortion", "distortion.on"),
                                 ("eq", "eq.on"), ("compressor", "compressor.on"), ("utility", "utility.on"), ("convolve", "convolve.on"), ("hyperdim", "hyperdim.on")) if g(key) >= 0.5]
    flt = [ch["filter1.model"][int(g("filter1.model"))]] if g("filter1.on") >= 0.5 else []
    return dict(poly=int(g("master.polyphony")), oscs=oscs, filters=flt, fx=fx)


def trigger(t):
    """Which of the thresholds of the audibility rule a measurement passes when its distance is under 3 dB (side, beat, HF, LF, fine structure): written after the numbers."""
    dist, lev, shift, side, beat, hf, lf, fine = t
    if dist >= 3 or abs(lev) >= 3 or abs(shift) >= 0.3:
        return ""
    hits = [f"{n} {v:+.1f} dB" for n, v, th in (("side", side, 3), ("beat", beat, 1), ("HF", hf, 4), ("LF", lf, 4), ("fine structure", fine, 6)) if abs(v) >= th]
    return ("; by " + ", ".join(hits)) if hits else ""


def macro_dests(entry, doc):
    out = {}
    src_key = {int(s["choice"]): s["key"] for s in doc["modSources"]}
    dst_key = {int(d["choice"]): d["key"] for d in doc["modDestinations"]}
    for n in range(1, 65):
        p = f"mod{n:02d}."
        s, d = int(entry["params"].get(p + "source", 0)), int(entry["params"].get(p + "destination", 0))
        if s and d and src_key[s].startswith("macro"):
            out.setdefault(int(src_key[s][5:]), []).append((dst_key[d], float(entry["params"].get(p + "amount", 0))))
    return out


def main():
    check = "--check" in sys.argv
    meas = json.loads(MEAS.read_text())
    texts = load(HERE / "sound-set-batch2-texts.py", "b2texts").TEXTS
    batch = load(HERE / "factory-presets-batch2.py", "b2batch")
    lab = load(HERE / "sound-lab.py", "b2lab")
    pdoc = json.loads((ROOT / "docs" / "params.json").read_text())
    entries = {}
    built = {e[0]: e for e in batch.build()}
    for name, cat, tags, desc, params, routes, mnames, content in batch.ENTRIES:
        entries[name] = dict(category=cat, tags=tags, description=desc, params=built[name][4], macro_names=mnames, content=content)
    order = [n for n in meas]            # the order of the measurement file (the presets of the batch files in file order)
    dists = lab.distances(ROOT / "content" / "presets", order)
    levels = json.loads((HERE / "factory-preset-levels.json").read_text())

    by_cat = {}
    for n in order:
        by_cat.setdefault(entries[n]["category"], []).append(n)
    L = []
    w = L.append

    w("# Sound set, batch 2: 24 new presets on the factory content pack")
    w("")
    w("Status: **built, partly measured, NOT HEARD.** Nothing here was listened to (the author cannot hear): every claim below is a measurement of the offline render of the engine the MPC runs "
      "(own engine, no Vital), or an intention the measurements support or do not contradict. The owner's ears decide; the lines \"Not settled for the ear\" name what to listen for first. "
      "**Not measured at all in this batch: the CPU of any preset (section 4), the 8-note velocity-127 peak, the five-register, velocity and long-hold report of `scripts/sound-lab.py`, "
      "and anything that needs a controller (MPE, the pad pressure on the device).** The reason and the one command that closes the gap are in section 1. Written by `scripts/gen-sound-set-batch2-doc.py` "
      "from `docs/release/sound-set-batch2-measurements.json` (`scripts/measure-batch2.py report`).")
    w("")
    w("The 24 presets are added to the set (160 now); the 136 before them are unchanged, byte for byte. Each was built from Init (the default values), not from the generator's template: on average "
      f"{sum(d['differs_from_default'] for d in dists.values()) / len(dists):.0f} parameters differ from the defaults. Files: `content/presets/<Category>/*.sbpreset`; source: `scripts/factory-presets-batch2.py` and "
      "`scripts/batch2/*.py` (data written with `scripts/sb_preset_dsl.py`), names of the macros in `scripts/factory-preset-macro-names.json`, levels in `scripts/factory-preset-levels.json`. "
      "They use the factory content pack of section 2 (`content/factory-wavetables`, `content/factory-samples`, made by `scripts/gen-content-pack.py`): "
      "a preset names its files by pack id, path and SHA-256 in its `content` section.")
    w("")
    w("| Role | Presets |")
    w("|---|---|")
    roles = [("Sample oscillator (6)", ["Dusty Rhodes Keys", "Glass Ping Pluck", "Breakbeat Slice Keys", "Bowed MPE Strings", "Reverse Gong Swell", "Breath Flute Lead"]),
             ("Granular oscillator (6)", ["Chord Cloud Pad", "Vowel Scrub Choir", "Frozen Pluck Drone", "Gong Grain Swarm", "Stutter Grain Kit", "Rising Grain Riser"]),
             ("Spectral oscillator (4)", ["Spectral Vowel Freeze", "Spectral Bowed Glass", "Spectral Gong Pad", "Spectral Chord Morph"]),
             ("Multisample, key-mapped (3)", ["Nylon Zone Pluck", "Marimba Layer Keys", "Glass Zone Bells"]),
             ("Wavetables of the pack (5)", ["Reese Dual Phase Bass", "Vowel Table Lead", "Drawbar Organ Keys", "Throat Singer Drone", "Ladder FM Bass"])]
    for role, names in roles:
        w(f"| {role} | " + ", ".join(f"{n} ({entries[n]['category']})" for n in names) + " |")
    w("")
    w("By feature: **Convolve** in Dusty Rhodes Keys (cabinet), Glass Ping Pluck, Vowel Scrub Choir, Frozen Pluck Drone (on bus 1), Reverse Gong Swell, Glass Zone Bells (plate), Breakbeat Slice Keys, Marimba Layer Keys (small room), "
      "Reese Dual Phase Bass (cabinet); **buses** in Breakbeat Slice Keys (parallel crush, bus 1), Frozen Pluck Drone (wet-only plate, bus 1), Gong Grain Swarm and Rising Grain Riser (delay and reverb, bus 2), Drawbar Organ Keys (overdrive, bus 1); "
      "**voice control lanes** in Chord Cloud Pad (6 steps) and Nylon Zone Pluck (8 steps); **scales** in Glass Zone Bells (Hirajoshi on D) and Throat Singer Drone (Phrygian dominant on D); **MPE** in Bowed MPE Strings; "
      "**pad pressure** on the held ones (Bowed MPE Strings, Breath Flute Lead, Chord Cloud Pad, Vowel Scrub Choir, Frozen Pluck Drone, Gong Grain Swarm, Spectral Vowel Freeze, Spectral Bowed Glass, Spectral Gong Pad, Spectral Chord Morph, Reese Dual Phase Bass, Vowel Table Lead, Throat Singer Drone, Ladder FM Bass). "
      "The name of the Reese starts with the sub-style word (\"Reese ...\"), so that a browser sorted by name groups it; the folders are the six that exist.")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 1
    w("## 1. How it was measured, and what could not be")
    w("")
    w("**The rule of this batch: no build, no test suite.** So the tools were the ones already built for batch 1 (`work-sound-set/build-sound/`), and they have a hole: **`synthyboy_sound_lab` and `synthyboy_tier_bench` do not set a content store** "
      "(they call `engine->setContent` only), so in them a Sample / Granular / Spectral / Multisample oscillator is silent and Convolve absent. Only `synthyboy_preset_render` has `--content-dir`. "
      "Their sources now read `$SYNTHYBOY_CONTENT_DIR/<pack id>/<file>` (`tools/sound-lab/sound_lab.cpp`, `bench/tier_bench.cpp`; `scripts/sound-lab.py` links the factory packs there by itself): **after a rebuild, "
      "`python3 scripts/sound-lab.py report content/presets/*/<the 24 files> --json out.json` and `synthyboy_tier_bench --preset-dir` (with `SYNTHYBOY_CONTENT_DIR` set) measure the rest.** I did not rebuild.")
    w("")
    w("What was measured instead (`scripts/measure-batch2.py`, which runs `synthyboy_preset_render --engine own --content-dir ... --sr 44100` and analyses the WAVs it writes with the functions of `scripts/sound-lab.py`):")
    w("")
    w("- **Level**: the RMS of the loudest 300 ms of a four note chord (MIDI 60 64 67 72, or 36 40 43 48 for a Bass) at velocity 1.0 held 1 s, the measure of `scripts/tune-preset-levels.py`, reference -20 dBFS. "
      "`master.volume` is tuned to -20.0 on tier `high` (the XL's profile is `high,voices=16,control=economy`; the tier of the renderer has the quality of `high` without the voice and control limits). "
      "As in batch 1 the level is taken at the sustain: for a preset whose envelope 1 attack is over 150 ms the level render is a copy with the attack cut to 5 ms (`LEVEL_OVERRIDES` in the script; for Rising Grain Riser also the filter envelope and the last 28 % of the texture, the top of the build). "
      "The table shows both: the level of the real preset at 1 s and at the sustain.")
    pks = [(meas[n].get("sustain") or {}).get("high", meas[n]["high"])["chord_peak"] for n in order]
    PEAK_RANGE = f"{max(pks):.1f} .. {min(pks):.1f} dBFS raw = {max(pks) + 18:.1f} .. {min(pks) + 18:.1f} dBFS"
    w("- **Peak**: the peak of the same four-note chord (velocity 1.0), raw and after the +18 dB of the MPC output stage. This is *not* the eight-note velocity-127 chord of batch 1: that one needs `sound-lab.py`; "
      "eight notes are about 3 dB higher than four. The conflict between the level and the stage (section 2 of batch 1) is the same: every preset sits at -20 dBFS RMS, so a chord peak of " + PEAK_RANGE + " at the stage.")
    w("- **Stereo, clean**: of the single note (MIDI 60, velocity 0.8, 1.5 s held and its tail; MIDI 36 for a Bass): correlation, mono sum against stereo level and the worst 1/3 octave band, DC, click ratio, spectral centroid and its movement. The tail is the time after the note off until under -90 dBFS.")
    w("- **Response**: for every macro, the single note with the macro at 0 and at 100 (a copy of the preset with the macro's value changed), for the wheel, the pad pressure and the velocity routes the same with the routes re-pointed at an unused macro (the route law is the same: 0 .. 1 of the source times the amount), "
      "as the distance of the two renders (the measures of batch 1: mean difference of the 1/3 octave bands, level, centroid, side level, beat, HF, LF, fine structure; audible when one of: distance >= 3 dB, level >= 3 dB, centroid >= 0.3 oct, side >= 3 dB, beat >= 1 dB, HF or LF >= 4 dB, fine >= 6 dB), "
      "the larger of the held note (0.1 .. 1.4 s) and the whole render with its tail (to 3 s). A macro of the Zone Shift kind (it moves the zone lookup of a key) is measured on the Bass note (MIDI 36), because on MIDI 60 the lookup stays in the same zone. "
      "**Macros that act on a rate (an LFO's rate, a vibrato), on the speed of a scan or on the texture of grains do not show in a spectral distance** and are listed as such with the amount of their route; they were not heard.")
    w("- **Key 61** is the `low` tier of the same renderer: the level and the chord peak are given for it; the sounds that lose Convolve there (all the Convolve presets) are noted.")
    w("- **Without the content pack**: the same presets rendered with an empty content store. The engine does not fall back to anything: a wavetable that is not there becomes the saw, a Sample / Granular / Spectral / Multisample oscillator is silent and Convolve is bypassed (section 7, item 1). "
      "So every preset that has a Sample-type main oscillator also has a layer of an ordinary wavetable oscillator, and the table shows what remains.")
    w("- **Checked on the way with `synthyboy_sound_lab` (no content needed)**: the voice control lanes (a lane value of 80 on pan: 16 dB to one side, in the pattern of the steps; cutoff +-60: the centroid of a saw moves 767 / 1579 / 2994 Hz over three steps; detune +-80: 249.6 / 261.8 / 273.9 Hz), "
      "the buses (a delay on Bus 1 with the oscillator's destination None and its send at 100: only the echo is there, at the level the dry signal had: -43 dBFS both; with no send: nothing of it) and the scale conform "
      "(Hirajoshi on D maps C, C#, D# ... to A#, D, D, D ...; Phrygian dominant on D maps C# to C, E to Eb, G# to G).")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 2
    w("## 2. The factory content pack")
    w("")
    w("`scripts/gen-content-pack.py` synthesises everything (numpy and the standard library; a fixed seed per file, deterministic; `--check` and the test `synthyboy_factory_content_reproducible` fail if the files differ from it). "
      "No third-party material, CC0-1.0, attribution Safian Szabolcs. Two packs in the format of `tools/contentpack/contentpack.py` (a `contentpack.json` with the SHA-256 and size of every file; tests `synthyboy_factory-wavetables_verifies`, `synthyboy_factory-samples_verifies`):")
    w("")
    w("- `content/factory-wavetables/` = pack `com.safian.synthyboy.factory-wavetables`, kind wavetables.")
    w("- `content/factory-samples/` = pack `com.safian.synthyboy.factory-samples`, kind samples: `samples/` (12 files), `instruments/<name>/<name>.sfz` with `samples/` below it (3 instruments), `impulses/` (3 files).")
    w("")
    sizes = {}
    for pack in ("factory-wavetables", "factory-samples"):
        for f in sorted((CONTENT / pack).rglob("*")):
            if f.is_file() and f.name != "contentpack.json":
                sizes[f.relative_to(CONTENT).as_posix()] = f.stat().st_size
    total = sum(sizes.values()) + sum((CONTENT / p / "contentpack.json").stat().st_size for p in ("factory-wavetables", "factory-samples"))
    w(f"**Size: {total / 1e6:.1f} MB ({total} bytes) for {len(sizes)} files** (wavetables {sum(v for k, v in sizes.items() if k.startswith('factory-wavetables')) / 1e6:.1f} MB, samples, instruments and impulses "
      f"{sum(v for k, v in sizes.items() if k.startswith('factory-samples')) / 1e6:.1f} MB), under the 12 MB target. They are committed (the convention of `content/presets`) and `scripts/build-mpc.sh` copies them to `Content/packs/<pack id>/` next to `Content/presets`, "
      "where the plugin's content store (`UserContentStore`, `addPackRoot (Content/packs)`) looks for the files a preset names. `plugin.json` does not declare them yet (a release decision: `contentpack.py build-all` would then build two more zips); "
      "the desktop shells have no pack root, so the batch 2 presets that need files are for the MPC build today.")
    w("")
    w("**Wavetables (16, mono 16 bit WAV, 2048 samples per frame, the format `core/engine-own/osc/content_set.cpp` reads: a file of N x 2048 samples is a table of N frames):**")
    w("")
    w("| File | Frames | Bytes | What it is |")
    w("|---|---|---|---|")
    wt_text = {"formant_sweep_aeiou": "the vowels A E I O U through the formants of a voice at 220 Hz", "choir_singers_shift": "A, O, U through a male, female and child's vocal tract and back",
               "overtone_throat_sweep": "one narrow resonance walking up and down the 4th .. 20th partial", "pwm_triple_stack": "three pulse waves with widths walking apart",
               "pwm_saw_blend": "a pulse sweeping 50 -> 4 % while a saw fades in", "pwm_dual_phase": "two opposite saws whose distance breathes 8 -> 50 -> 8 %",
               "additive_bell_strike": "harmonic bell partials, the high ones fading as the frames go on", "additive_glass_harmonica": "a sparse set of partials that opens one by one",
               "fm_index_sweep": "2:1 FM, index 0 -> 9", "fm_ratio_ladder": "FM ratios 1 .. 8, eight frames each, the index rising in each", "fm_feedback_saw": "one operator with feedback 0 -> 1.9 (sine to saw)",
               "organ_drawbars_16": "sixteen drawbar registrations (the cycle is the 16 foot fundamental: transpose +12)", "metal_comb_sweep": "a comb sweeping through a saw's spectrum",
               "ring_mod_ladder": "ring modulation with the integer ratios 2 .. 17", "sine_fold_sweep": "a sine through a wave folder, gain 0.4 -> 14", "harmonic_sieve": "a saw with only every n-th harmonic, n = 1 .. 16"}
    for k, v in sizes.items():
        if k.startswith("factory-wavetables"):
            stem = Path(k).stem
            w(f"| `{stem}.wav` | {(v - 44) // 4096} | {v} | {wt_text.get(stem, '')} |")
    w("")
    w("**Samples (12, 44.1 kHz, 16 bit, peak -1 dBFS; pitched ones tuned to MIDI 60 except where the name says; a `smpl` chunk with the root and the loop where there is one; for the loop and the root the engine reads the preset's own parameters, section 7 item 3):**")
    w("")
    w("| File | Seconds | Channels | Bytes | What it is |")
    w("|---|---|---|---|---|")
    sm_text = {"pluck_string": "additive plucked string, tuned to MIDI 60", "mallet_marimba": "a marimba bar (1 : 3.99 : 10.1), MIDI 60", "glass_ping": "a struck glass, MIDI 72, six inharmonic partials and a beating twin",
               "breath_noise_burst": "a breath: three bands of noise, 1.2 s, unpitched", "bowed_tone": "bowed tone MIDI 60, 5 Hz vibrato; loop 14.3 .. 77.7 % closes on whole vibrato cycles (mismatch 54 dB under the signal)",
               "vowel_pad": "three voices singing A / O, MIDI 60, loop closes like the bowed tone", "vinyl_noise_bed": "hiss, rumble, 50 Hz hum, crackle and pops, stereo, 4 s loop (wraps round)",
               "drum_loop_120bpm": "two bars of 4/4 at 120 BPM: kick, snare, ghost snares, hats, open hats; 32 sixteenths of 0.125 s, the loop wraps", "rising_texture": "noise whose band climbs 250 Hz -> 7 kHz with a harmonic stack gliding two octaves, 6 s",
               "chord_pad_sections": "C maj7, A min7, F maj7, G 6, 2 s each, crossfaded, 8 s", "gong_metal": "13 inharmonic modes, each a close pair, sizzle, tuned to MIDI 36", "voice_sweep_aeiou": "a voice at 130.8 Hz singing A E I O U in 5 s, vibrato, breath, MIDI 48"}
    import struct
    for k, v in sizes.items():
        if k.startswith("factory-samples/samples/"):
            raw = (CONTENT / k).read_bytes()
            ch, rate = struct.unpack("<HI", raw[22:28])
            nb = struct.unpack("<I", raw[raw.find(b"data") + 4:raw.find(b"data") + 8])[0]
            stem = Path(k).stem
            w(f"| `{stem}.wav` | {nb / (2 * ch * rate):.2f} | {ch} | {v} | {sm_text.get(stem, '')} |")
    w("")
    w("**Instruments for the Multisample type (SFZ, the samples below the folder of the file):**")
    w("")
    w("| Instrument | Zones | Files | Bytes | What it is |")
    w("|---|---|---|---|---|")
    for inst, text in (("pluck_nylon", "nylon plucks in six key zones (roots 36 45 54 63 72 81)"), ("mallet_marimba", "marimba bars in five zones and two velocity layers (1 .. 84 soft, 85 .. 127 hard)"), ("glass_bells", "struck glass in four zones (roots 48 60 72 84)")):
        files = [k for k in sizes if k.startswith(f"factory-samples/instruments/{inst}/") and k.endswith(".wav")]
        zones = (CONTENT / "factory-samples" / "instruments" / inst / f"{inst}.sfz").read_text().count("<region>")
        w(f"| `{inst}` | {zones} | {len(files)} | {sum(sizes[f] for f in files)} | {text} |")
    w("")
    w("**Impulse responses (3, stereo, 24 bit, 44.1 kHz, scaled to an L2 norm of 1, so that the wet level is about the dry level at `convolve.ir_gain` 0 with the Vital law):**")
    w("")
    w("| File | Seconds | Bytes | What it is |")
    w("|---|---|---|---|")
    ir_text = {"small_room": "a small room: eight early reflections in the first 46 ms, a 0.42 s tail that darkens", "plate": "a plate: dense and bright from the first millisecond, RT60 1.25 s, 1.45 s long, a high sheen that dies in 0.35 s", "cabinet_colour": "a guitar cabinet colour: open-back roll-off under 70 Hz, a bump at 95 Hz, presence at 2.5 / 3.3 kHz (two 'microphones'), a low pass at 5.2 kHz, 90 ms"}
    for k, v in sizes.items():
        if k.startswith("factory-samples/impulses/"):
            raw = (CONTENT / k).read_bytes()
            ch, rate = struct.unpack("<HI", raw[22:28])
            nb = struct.unpack("<I", raw[raw.find(b"data") + 4:raw.find(b"data") + 8])[0]
            stem = Path(k).stem
            w(f"| `{stem}.wav` | {nb / (3 * ch * rate):.2f} | {v} | {ir_text.get(stem, '')} |")
    w("")
    w("**How the engine resolves a missing file** (`core/engine-own/osc/content_set.cpp`, `voice.cpp`, `fx_modules.cpp`; measured in the \"without the pack\" column of the table below): a file that cannot be read or whose SHA-256 differs is skipped. "
      "A `Preset table` without its table plays the **saw**; a Sample, Granular, Spectral or Multisample oscillator without its file is **silent** (docs/engine-spec/07 section 1: \"no fallback tone\"); Convolve without its impulse **does not convolve** (the dry signal). "
      "There is no generator to fall back to at run time (`docs/ARCHITECTURE.md` section 6 promises \"tolerates absence\": a sound is produced either way; for the sample types that holds only because these presets carry a wavetable layer).")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 3
    w("## 3. Summary table")
    w("")
    w("Level = chord RMS in dBFS at the sustain (tier high = the XL, and tier low = the Key 61; section 1). Peak = four-note chord at velocity 1.0, raw dBFS and after +18 dB. Mono = level of the mono sum against the stereo level of the single note, worst 1/3 octave band. "
      "Without the pack = the chord RMS with no content store (below -100 means silent). DC as a fraction of full scale.")
    w("")
    w("| Preset | Level XL / 61 | Peak raw -> at +18 | Mono / worst band | DC | Click | Tail s | Without the pack |")
    w("|---|---|---|---|---|---|---|---|")
    for n in order:
        o = meas[n]
        s = o.get("sustain", {}) or {}
        sh, sl = (s.get("high") or o["high"]), (s.get("low") or o["low"])
        wv = o.get("wav", {})
        nc = o.get("nocontent")
        w(f"| {n} | {fmt(sh['chord_rms'])} / {fmt(sl['chord_rms'])} | {fmt(sh['chord_peak'])} -> {fmt(sh['chord_peak'] + 18)} | {fmt(wv.get('mono_db', 0))} / {fmt(wv.get('mono_worst_band_db', 0))} @ {wv.get('mono_worst_band_hz', 0):.0f} Hz | "
          f"{wv.get('dc', 0):.4f} | {fmt(wv.get('click', 0))} | {fmt(o['high']['tail'], 2)} | {'silent' if (nc and nc['chord_rms'] < -100) else fmt(nc['chord_rms']) if nc else '?'} |")
    w("")
    w("The level of the real preset at 1 s (before the attack is over) is in the per-preset lines. The Key 61 figure differs from the XL by more than 1.5 dB where Convolve is bypassed or a reverb is lost: "
      + ", ".join(f"{n} ({fmt((meas[n].get('sustain') or {}).get('low', meas[n]['low'])['chord_rms'] - (meas[n].get('sustain') or {}).get('high', meas[n]['high'])['chord_rms'], 1)} dB)" for n in order
                  if abs((meas[n].get('sustain') or {}).get('low', meas[n]['low'])['chord_rms'] - (meas[n].get('sustain') or {}).get('high', meas[n]['high'])['chord_rms']) > 1.5) + ". They are levelled on the XL.")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 4
    w("## 4. CPU: not measured; what each preset spends")
    w("")
    w("Rule (owner): nothing new may be dearer than the dearest existing preset (batch 1, section 4: Key 61 median 4.78 / p99 6.02 % of the block, XL median 11.53 / p99 14.06 %, in that run on the Mac). "
      "**That could not be checked**: `synthyboy_tier_bench` has no content store, so a sample-type oscillator costs nothing in it, and a rebuild was not allowed. "
      "The command after a rebuild: `SYNTHYBOY_CONTENT_DIR=<store> synthyboy_tier_bench --preset-dir content/presets --repeat 3` (the store: `scripts/sound-lab.py` makes one in its work folder, links named by the pack ids). "
      "**The Spectral oscillator is the dearest voice type in the engine (docs/engine-spec/07 section 7) and the four Spectral presets are the ones to check first, then the Granular ones with the most grains** "
      "(Chord Cloud Pad: 26 grains a second x 180 ms x 2 unison x polyphony 6; Rising Grain Riser: 60 x 150 ms). What was done to keep them cheap: polyphony 1 .. 6 (the heavy ones 1 .. 4: `master.polyphony`), unison 2, "
      "no warp except one spectral warp on each Spectral preset (Add Harmonics, Spectral Comb, Add Subharmonics, Spectral Shift) and no morph, only the Analog and Digital filter models (the cheap ones of batch 1: -0.1 and 0), the effects of batch 1's catalogue (0.1 .. 0.8 % each), "
      "no wavetable warp at all (the 4x warps cost 7 .. 9 % of the XL block per oscillator and per unison voice). The cost of Convolve is not in batch 1's catalogue either; it is bypassed on the Key 61.")
    w("")
    w("| Preset | Polyphony | Oscillators (type x unison voices) | Filter | Effects |")
    w("|---|---|---|---|---|")
    for n in order:
        c = census(entries[n]["params"], pdoc)
        w(f"| {n} | {c['poly']} | {'; '.join(c['oscs'])} | {', '.join(c['filters'])} | {', '.join(c['fx'])} |")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 5
    w("## 5. Distance to the nearest preset")
    w("")
    w("The survey's similarity measure (`scripts/survey-presets.py`): the number of parameters whose values differ. The first 112 were within 4 .. 71 of each other, batch 1 71 .. 123 from the nearest of the set; "
      f"the 24 here are {min(d['distance'] for d in dists.values())} .. {max(d['distance'] for d in dists.values())} from the nearest of the 160.")
    w("")
    w("| Preset | Parameters differing from the defaults | Nearest preset of the 160 | Distance | Nearest of the new ones | Distance |")
    w("|---|---|---|---|---|---|")
    for n in order:
        d = dists[n]
        w(f"| {n} | {d['differs_from_default']} | {d['nearest']} | {d['distance']} | {d['nearest_new']} | {d['distance_new']} |")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 6
    w("## 6. The presets")
    w("")
    kinds = {"lfo": "rate", }
    for n in order:
        e, o, t = entries[n], meas[n], texts[n]
        w(f"### {n} ({e['category']})")
        w("")
        w(f"*Intention.* {e['description']}.")
        w("")
        w(f"*What to play.* {t['play']}")
        w("")
        md = macro_dests(e, pdoc)
        parts = []
        for m in range(1, 9):
            name = e["macro_names"][m - 1]
            if not name:
                continue
            tup = o["macros"].get(str(m))
            dests = [d for d, _ in md.get(m, [])]
            rate_only = dests and all(d.endswith(".rate") for d in dests)
            if tup is None:
                parts.append(f"{name} (not measured)")
            elif rate_only:
                parts.append(f"{name} (acts on a rate: not seen by the spectral measure; route {', '.join(f'{d} {a:+g} %' for d, a in md[m])})")
            elif o["macros_audible"].get(str(m)):
                parts.append(f"{name} (audible: {fmt(tup[0])} dB, {tup[1]:+.1f} dB level, {tup[2]:+.2f} oct{trigger(tup)})")
            else:
                parts.append(f"{name} (weak in the measure: {fmt(tup[0])} dB, {tup[1]:+.1f} dB level, {tup[2]:+.2f} oct; route {', '.join(f'{d} {a:+g} %' for d, a in md[m])}: a texture or scan control, check by ear)")
        w("*Macros.* " + "; ".join(parts) + ".")
        w("")
        s = o.get("sustain", {}) or {}
        sh, sl = (s.get("high") or o["high"]), (s.get("low") or o["low"])
        wv = o.get("wav", {})
        ctr = o.get("controls", {})
        names = {"mod_wheel": "wheel", "poly_aftertouch": "pressure", "velocity": "velocity route"}
        cs = "; ".join(f"{names[k]}: {fmt(v[0])} dB ({v[1]:+.1f} dB, {v[2]:+.2f} oct)" for k, v in ctr.items())
        nc = o.get("nocontent")
        w(f"*Measured.* Level {fmt(sh['chord_rms'])} dBFS at the sustain (XL), {fmt(sl['chord_rms'])} (Key 61); the real preset at 1 s: {fmt(o['high']['chord_rms'])}. Four-note chord peak {fmt(sh['chord_peak'])} dBFS raw = {fmt(sh['chord_peak'] + 18)} at +18 dB. "
          f"Single note: mono sum {fmt(wv.get('mono_db', 0))} dB vs stereo, worst band {fmt(wv.get('mono_worst_band_db', 0))} dB at {wv.get('mono_worst_band_hz', 0):.0f} Hz; DC {wv.get('dc', 0):.4f}, click ratio {fmt(wv.get('click', 0))}, tail {fmt(o['high']['tail'], 2)} s; "
          f"centroid {wv.get('centroid_hz', 0):.0f} Hz, moving {fmt(wv.get('move_centroid_oct', 0), 2)} oct and {fmt(wv.get('move_level_db', 0))} dB over the note. Controls (0 -> 100): {cs}. "
          f"Without the content pack: " + ("silent" if (nc and nc['chord_rms'] < -100) else f"chord {fmt(nc['chord_rms'])} dBFS" if nc else "not measured") + ". Nearest preset: " + f"{dists[n]['nearest']} ({dists[n]['distance']}).")
        w("")
        c = census(e["params"], pdoc)
        w(f"*Spends.* polyphony {c['poly']}; " + "; ".join(c["oscs"]) + f"; filter {', '.join(c['filters'])}; effects {', '.join(c['fx'])}. Not measured: CPU, the 8-note velocity-127 peak, the register and velocity report.")
        w("")
        w(f"*Not settled for the ear.* {t['unsure']}")
        w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 7
    w("## 7. Engine limits and defects found (Sample, Granular, Spectral, Multisample, Convolve)")
    w("")
    w("Everything below was seen in a render or in the code, with the numbers; nothing was worked around silently (the workaround, where there is one, is named). Items 1 and 2 of the list of batch 1 (Ladder at the top, Direct out and `master.volume`) "
      "and the other engine items of that list are being fixed elsewhere and are not repeated; batch 1's item 13 (\"could not be shown\") is what this batch closes.")
    w("")
    w("1. **A missing content file is not replaced by anything.** `docs/ARCHITECTURE.md` section 6 says the plugin tolerates absence and that a sound is produced either way. Measured with an empty store: "
      "before the presets got their wavetable layers, Frozen Pluck Drone, Gong Grain Swarm, Stutter Grain Kit, Rising Grain Riser, Spectral Chord Morph and Breakbeat Slice Keys were **silent** (chord RMS -180 dBFS: a Sample, Granular, Spectral or Multisample oscillator without its file produces nothing, "
      "docs/engine-spec/07 section 1), Convolve without its impulse does not convolve, a `Preset table` plays the saw. Nothing tells the player: there is no `missing_content` indicator on the MPC. "
      "Consequence for the set: every batch 2 preset has a plain wavetable layer (level 10 .. 30 % of its oscillator) that is what you hear if the packs are not installed (the table's column \"Without the pack\" shows -17.9 .. -32.8 dBFS), and "
      "the `synthyboy_preset_sanity` gate (Vital engine, no store by default) would fail \"silent\" for them; its CMake call now passes `--content-dir` to a store of links to the packs.")
    w("2. **`synthyboy_sound_lab` and `synthyboy_tier_bench` could not load content** (no `setContentStore`), so batch 1 could not show these paths and this batch could not use them for the CPU and the full report; fixed in the sources, needs a rebuild (section 1).")
    w("3. **The root note and the loop points of a WAV (`smpl` chunk) are ignored by the Sample, Granular and Spectral oscillators**: `lib_osc.cpp` pitches by `oscN.sample.root` (default 60) and loops by the oscillator's own `sample.loop_start / loop_end` in percent of the file; "
      "`WavInfo::rootNote`, `loopStart` and `loopEnd` are read by the file loader (the UI copies the root to the parameter when it loads a file) but not used by a preset that names the file. "
      "So a preset must carry `sample.root` and the loop percentages itself (the bowed tone's 14.3 .. 77.7 % are computed from its frames); the chunk in the pack's files is information for other tools. Also `ref.name` is passed to the WAV reader and a note suffix in it (\"_C3\", \" F#2\") overrides the chunk's root: the pack's names have none.")
    w("4. **`oscN.multi.timbre` barely changes what you hear with an instrument of stretched zones.** It moves the *lookup key* of the zone (`timbreKey`: `key + t * (127 - 2 key)`), not the pitch: the note stays in tune and a zone from another register is stretched to it. "
      "Measured on Nylon Zone Pluck at MIDI 36 (lookup key 91 at +100 %, zone root 81, a shift of 45 semitones): the fundamental stays 65 Hz, the centroid 556 -> 520 Hz, the spectral distance 1.3 dB; on MIDI 60 and +25 / 50 / 100 % it is exactly 0.0 dB because 60 .. 67 are one zone. "
      "It is a working control for an instrument with different recordings per zone (the marimba's Zone Shift: 2.0 dB at the Bass note), not for the pluck. The macro of Nylon Zone Pluck was changed to a unison double for that reason.")
    w("5. **Granular and spectral texture controls are invisible to a spectral distance.** Density 6 / 26 / 100 grains a second changes the 1/3 octave bands by 1.5 / 0 / 0.9 dB (Chord Cloud Pad), grain length 30 vs 180 ms 2.6 dB, random pitch 12 semitones 5.5 dB, random offset 60 % 1.8 dB; "
      "the controls that move the spectrum (pitch scatter, filter, frequency shift) are the audible ones. The macros of density and length are kept (they are the point of the type) and are marked as texture controls above.")
    w("6. **The modulation range of a dB parameter is the whole range**: `bus1.level` and `bus2.level` run -60 .. +6 dB, so a route of 50 % is **+33 dB** (the amount is a share of the range: `docs/engine-spec/04`). The first build of Frozen Pluck Drone, Gong Grain Swarm, Rising Grain Riser and Breakbeat Slice Keys had such routes (macros of 40 .. 50, wheel 20 .. 30) and would have jumped by 13 .. 33 dB; "
      "they are 12 .. 14 % (macros) and 8 % (wheel) now. Not a defect, a trap: the same holds for `convolve.ir_gain` (64 dB range) and `master.volume`.")
    w("7. **Convolve follows the Vital law's level: the impulse is used as it is** (`convolve.law` 0; with the Serum law the engine normalises the impulse to a peak of 1 and applies `ir_gain`, default -18 dB: `core/fx-serum-b/convolve.cpp`). A file's own level decides the wet level, so the impulses of the pack are scaled to an L2 norm of 1 (wet about equal to dry for noise) and the presets leave `ir_gain` at 0. "
      "A user's impulse straight from a reverb plugin, peak-normalised, would be 20 dB or more louder. The Key 61 (`low`) bypasses Convolve: Dusty Rhodes Keys and Reese Dual Phase Bass are 5 .. 6 dB quieter there (the cabinet's gain is part of their level) and Marimba Layer Keys 3 dB.")
    w("8. **Spectral oscillator**: the spectral warps all act (distance to no warp at amount 70 on the gong: Spectral Pitch Shift 14.2 dB, Spectral Shift 13.0, Add Subharmonics 11.4, Peak Harmonic Down 9.9, Spectral Spread 8.4, Spectral Gate 7.0, Shepard Narrow 6.0 ... down to Spectral Mirror and Phase Twist 0.9 dB and Spectral Smear 1.0, which change this file very little), "
      "the spectral filter keys `spec.cut`, `spec.mix` do nothing without `spec.flt_use` (no defect: they belong to the filter's own table), `spec.transients` did not change a smooth sound. The oscillator's level is rate-aware in the own engine (chord level on the Key 61 within 1 dB of the XL for all four).")
    w("9. **`check-presets.py` rule `mod_slot_duplicate`** (batch 1, item 10) hit again (Breath Flute Lead: a vibrato and the wheel's deepening of it on one LFO); worked around with a second LFO of the same rate and macros that move both rates.")
    w("10. **Things that work as documented and were measured, for the record**: the voice control lanes (pan 16 dB at a lane value of 80; cutoff about an octave at 60; detune 0.8 semitone at 80; the pattern follows the note count), the buses (parallel copy, wet-only rack, destination None), "
      "the scale conform (Hirajoshi, Phrygian dominant), the sample reverse (the gong's level in 0.2 s steps at MIDI 60: -39 -37 -32 -33 -27 dB then falling, forward: -30 -32 -32 -37 ...), the slice mode with manual slices (key = slice from `slice_root`: slice 0 is the kick, 98 % of its energy under 200 Hz; slice 4 the snare, 77 / 14 / 9 % low / mid / high), the SFZ velocity layers (the same note with the two layers' velocity ranges swapped in a temporary copy of the pack: the soft file's first 30 ms has its centroid at 556 Hz, the hard one's at 1016 Hz, 0.07 % against 0.80 % of the energy above 2 kHz), the 24 bit stereo impulse and the SFZ parser (every zone of all three instruments plays).")
    w("11. **MPE was not measurable**: `sound-lab` has no per-channel expression; `master.mpe.on` was checked by reading `own_engine.cpp` (it changes the bend range of channels above 0 to `master.mpe.bend_range` and maps the channel pressure to Poly Aftertouch; the manager channel behaves as before) and by the set-up of Bowed MPE Strings, not by a render. "
      "`master.mpe.y_modwheel` makes the slide act as the mod wheel in the matrix (the preset routes both).")
    w("")

    # ------------------------------------------------------------------------------------------------------------------------------ 8
    w("## 8. Not shown, and why")
    w("")
    w("- CPU of every preset (section 4) and the eight-note velocity-127 peak, the register / velocity / long-hold report of `sound-lab.py`: need the rebuilt tools.")
    w("- The `Vital` engine (what `synthyboy_preset_sanity` runs) was not rendered: the numbers are those of the own engine the MPC runs.")
    w("- Wavetable warps and morphs: not used (4x warp costs 7 .. 9 % of the XL block per oscillator; batch 1 spent them). The filter models Sample Hold, New Wsp, EQ 6 dB, Misc stay unused; the Spectral filter (`spec.flt_use`) and the drawn spectral mask, the multisample envelope override (`multi.override`), key switches and round robin in an SFZ, release triggers: not used.")
    w("- Per-note effects are not possible (batch 1, item 8); the Key 61 loses Convolve, reverb, flanger, Hyper/Dimension and Bode.")
    w("")
    w("## 9. Batch 3")
    w("")
    w("1. A listening round on these 24 and on batch 1 (the \"Not settled\" lines), the first things: the loop of Bowed MPE Strings, the grain clouds' smoothness at their densities, the zone seams of the instruments, the vinyl bed under chords, the level of the four Spectral presets against the rest.")
    w("2. The rebuild and `sound-lab.py report` / `tier_bench` for the 24 (section 1), and a decision about the content packs in `plugin.json` and about the desktop shells' pack root.")
    w("3. A second content pack round from what the ear says: more instruments (an electric piano and a string section with velocity layers and release triggers), more impulse responses (a hall, a spring, a tape), a drum kit for the slice mode, real loops.")
    w("4. A pass of the first 136 against the same measures: the velocity (batch 1's list) and the pad pressure on the held ones.")
    w("")
    text = "\n".join(L) + "\n"
    if check:
        if not OUT.is_file() or OUT.read_text() != text:
            print(f"{OUT} differs from the generator")
            return 1
        print("up to date")
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
