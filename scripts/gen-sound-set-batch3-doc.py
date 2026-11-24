#!/usr/bin/env python3
# SynthyBoy - writes docs/release/SOUND-SET-BATCH-3.md and the batch 3 section of docs/user-guide/factory-sounds.md from docs/release/sound-set-batch3-measurements.json,
# scripts/factory-presets-batch3.py and scripts/sound-set-batch3-texts.py.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   python3 scripts/gen-sound-set-batch3-doc.py            writes docs/release/SOUND-SET-BATCH-3.md
#   python3 scripts/gen-sound-set-batch3-doc.py --guide    rewrites the batch 3 section of docs/user-guide/factory-sounds.md

import importlib.util
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def _load(name, fname):
    spec = importlib.util.spec_from_file_location(name, HERE / fname)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


B3 = _load("sb_batch3", "factory-presets-batch3.py")
TEXTS = _load("sb_batch3_texts", "sound-set-batch3-texts.py").TEXTS
M = json.load(open(ROOT / "docs/release/sound-set-batch3-measurements.json"))
P = M["presets"]
B3.build()
ENTRY = {e[0]: e for e in B3.ENTRIES}
ORDER = list(ENTRY)
GUIDE_MARK = "## Sound Set Batch 3 (24 presets on the unused wavetables)"


def wt(name):
    return ", ".join(f"`{w.split('/')[-1]}`" for w in P[name]["wavetables"])


def rank_line(profile_key, label):
    return None


def doc():
    L = []
    w = L.append
    xl_d, k_d = M["dearest_existing_preset"]["xl"], M["dearest_existing_preset"]["key61"]
    w("# Sound set, batch 3: 24 new presets on the factory wavetables that no preset used")
    w("")
    w("Status: **built and measured in the offline lab, NOT HEARD, NOT MERGED.** Branch `work/sound-batch-3`, made from `feature/mpc-overlay`. "
      "Nothing here was listened to. Every statement about sound is an intention that a measurement supports or does not contradict; a preset marked \"not audible\" in the control table is one where the measure "
      "could not see the change, and the ear decides. No project build and no test suite were run for it, only tools that were already built (section 2).")
    w("")
    w("The plan is `docs/release/SOUND-SET-BATCH-3-PLAN.md` (written by a weaker model; every value was checked against `docs/params.json` and every table path against `content/factory-wavetables/`, "
      "and section 6 lists where this batch differs from it and why). The set goes from 160 to 184 presets: 4 presets for each of the 6 wavetable categories, every one built on at least one table that none of the first 160 used "
      "(7 of the 72 tables are used by batch 2 presets, 65 had no preset; the 24 presets here use 24 of those 65).")
    w("")
    w("Files: `content/presets/<Category>/*.sbpreset` (generated); source `scripts/factory-presets-batch3.py` and `scripts/batch3/*.py` (data written with `scripts/sb_preset_dsl.py`, helpers shared with batch 2); "
      "macro names in `scripts/factory-preset-macro-names.json`; levels in `scripts/factory-preset-levels.json`; browser groups in `scripts/factory-preset-categories.json`; "
      "measurements in `docs/release/sound-set-batch3-measurements.json`. This document is written by `scripts/gen-sound-set-batch3-doc.py` from the measurements and `scripts/sound-set-batch3-texts.py`.")
    w("")
    w("## 1. The 24 presets")
    w("")
    w("| # | Preset | Category | Browser group | Wavetable | What it is for | What to play |")
    w("|---|---|---|---|---|---|---|")
    for i, n in enumerate(ORDER, 1):
        t = TEXTS[n]
        w(f"| {i:02d} | **{n}** | {P[n]['category']} | {P[n]['group']} | {wt(n)} | {t[0]} | {t[1]} |")
    w("")
    w("Pad pressure (`Poly Aftertouch` to Filter 1 cutoff) is on the held sounds (basses, leads, pads, textures); the decaying keys, the plucks and the risers have none, as in batch 2. "
      "The wheel moves the frame (or the effect named in the preset) in every preset. Six macros are named and wired in each, macros 7 and 8 are empty, names are 14 characters at most.")
    w("")
    w("## 2. How it was measured, and what was not")
    w("")
    w("**No project build, no test suite, no gate script.** The tools of the integration build (`build-int/` of the `sb-mpc-overlay` worktree, built from the same commit) were run read only: "
      "`synthyboy_sound_lab`, `synthyboy_tier_bench` and `synthyboy_preset_render` exist there and read `$SYNTHYBOY_CONTENT_DIR`, so the factory wavetables load (nothing was missing). "
      "The Python tools of this worktree were used as they are. `scripts/tune-preset-levels.py` was not run.")
    w("")
    w("- **Level** (`scripts/measure-loudness.py`, the method of `docs/release/SOUND-FIXES-1.md`): the loudest 100 ms of the category gesture (Bass single notes C1 to C3, Lead C3 to C5, Pad and Keys triads, Pluck a played line, FX one note for its natural length), "
      "K weighted (BS.1770), plus the category offset, set to the target of minus 23 LUFS by `master.volume`. The solved volumes are in `scripts/factory-preset-levels.json` (the file that batches 1 and 2 use; `gen-factory-presets.py` writes them into the presets), "
      "measured again after writing them, on the XL profile (`high;voices=16;control=economy`) and on the Key 61 profile (`low`).")
    w("- **Peak**: the eight note chord (C2 to G4) at velocity 127, held 1.5 s, raw engine output. The MPC shell adds 18 dB after it (no limiter), so the second figure is the raw one plus 18. "
      "The conflict between a common level and a clean peak at that stage is the one described in `docs/release/SOUND-SET-BATCH-1.md` section 2; these presets are in the range of batches 1 and 2.")
    w("- **CPU**: `synthyboy_tier_bench --all-presets` on all 184 presets of this worktree, `--config low` (Key 61, 8 notes) and `--config high,voices=16,control=economy` (XL, 16 notes), 4 s measured per run, 3 passes, a chord struck again every second, "
      "median of the 99th percentiles of the passes, as percent of the 2.9 ms block, on the Mac (arm64), one core, other work was running (load average about 3). The Mac is 15 to 25 times faster than the devices, so only the comparison inside the same run counts. "
      "The rule is that nothing new may be dearer than the dearest existing preset of the same run.")
    w("- **Controls** (`scripts/sound-lab.py report`, both profiles): macros 1 to 8 at 0 and 100, the wheel, the pressure ramp, velocity, stereo, click and tail. A control counts as audible when one of the thresholds of `docs/release/SOUND-SET-BATCH-1.md` section 1 is passed.")
    w("")
    w("What was **not** done: no listening. No Hungarian listening guide was written. The pad pressure and the wheel on a real device were not tried. The tools were not rebuilt, so the preset files were checked against the engine as it is in `build-int/` only.")
    w("")
    w("## 3. Numbers")
    w("")
    w("Loudness in LUFS (calibrated fast max, target minus 23.0), XL and Key 61. Peak: 8 note velocity 127 chord, raw dBFS and at the +18 dB stage (XL). CPU: 99th percentile, percent of the block (Key 61 profile / XL profile).")
    w("")
    w("| # | Preset | master.volume dB | Loudness XL | Loudness Key 61 | Peak raw | Peak at +18 | CPU Key 61 | CPU XL |")
    w("|---|---|---|---|---|---|---|---|---|")
    for i, n in enumerate(ORDER, 1):
        r = P[n]
        pk = r["chord8_v127_peak_dbfs_xl"]
        w(f"| {i:02d} | **{n}** | {r['master_volume_db']:.1f} | {r['loudness_lufs_xl']:.1f} | {r['loudness_lufs_key61']:.1f} | {pk:.1f} | {pk + 18:+.1f} | {r['cpu_pct_block_key61']['p99']:.2f} | {r['cpu_pct_block_xl']['p99']:.2f} |")
    w("")
    kmax = max(P[n]["cpu_pct_block_key61"]["p99"] for n in ORDER)
    xmax = max(P[n]["cpu_pct_block_xl"]["p99"] for n in ORDER)
    kn = max(ORDER, key=lambda n: P[n]["cpu_pct_block_key61"]["p99"])
    xn = max(ORDER, key=lambda n: P[n]["cpu_pct_block_xl"]["p99"])
    w(f"**CPU result.** The dearest existing preset of the run is {k_d[0]} on both profiles: Key 61 {k_d[1]['p99']:.2f} %, XL {xl_d[1]['p99']:.2f} % of the block (99th percentile). "
      f"The dearest new preset is {kn} on the Key 61 profile ({kmax:.2f} %) and {xn} on the XL profile ({xmax:.2f} %). None of the 24 is dearer than the dearest existing preset under either profile; "
      f"the dearest new ones are about {k_d[1]['p99'] / kmax:.0f} (Key 61) and {xl_d[1]['p99'] / xmax:.0f} (XL) times cheaper. No wavetable preset here uses more than 3 unison voices on one oscillator, polyphony is 10 at most.")
    w("")
    dev = max(abs(P[n]["loudness_lufs_key61"] + 23.0) for n in ORDER)
    pn = max(ORDER, key=lambda n: P[n]["chord8_v127_peak_dbfs_xl"])
    w(f"Level and peak notes. Every preset is within {dev:.1f} dB of the target on the Key 61 profile (that profile drops the reverb and other effects of the high tier, which changes the level a little). "
      f"The 8 note velocity 127 peaks at the +18 dB stage reach {P[pn]['chord8_v127_peak_dbfs_xl'] + 18:+.1f} dBFS at most ({pn}); that is the known clip risk of the whole set (batches 1 and 2 reach +16.7), a matter for the output stage and not for the single preset. "
      "Cymbal Cloud Riser and Wind Sweep Riser show very low chord peaks because the riser has not started within the 3.5 s of that render; their gesture is the natural length of the note.")
    w("")
    w("## 4. Which controls the measure could hear")
    w("")
    w("| Preset | Macros audible | Wheel | Pressure | Velocity 30 to 127, level change | Click ratio | Tail s |")
    w("|---|---|---|---|---|---|---|")
    for n in ORDER:
        r = P[n]
        ma = ", ".join(str(m) for m in r["macros_audible"])
        miss = [m for m in range(1, 7) if m not in r["macros_audible"]]
        w(f"| **{n}** | {ma}{' (not: ' + ', '.join(str(m) for m in miss) + ')' if miss else ''} | {'yes' if r['wheel_audible'] else 'no'} | {'yes' if r['pressure_audible'] else 'no'} | {r['velocity_level_span_db']:.0f} dB | {r['click_ratio']} | {r['tail_s']} |")
    w("")
    w("A \"no\" is not always a fault: it can be a route that the measure cannot see. Known cases: macros on an LFO rate or on the vibrato rate (the measure looks at the spectrum, not at the rate), macros that change a time (decay, attack: the window is fixed), "
      "Dispersion Phase Pad (the table changes only phases, so the magnitude spectrum is the same by construction), Vinyl Crackle Bed (density is temporal), the wheel of Diphthong Morph Vox (the stepped LFO already covers the same frames), "
      "the Keys and Pluck presets (no pad pressure route by convention, so the pressure measure is 0 there). Macros that were inaudible while I built the set were changed (section 6); the ones that remain are listed above.")
    w("")
    w("## 5. Method of the sound: what was checked by hand")
    w("")
    w("- The wavetable files were read for their real content before the presets were set: frame 0 of `additive_bell_strike` is the strike and the last frame the hum; the cutoff of `pink_lowpass_sweep` falls as the frame goes up; "
      "`noise_band_sweep` peaks at the harmonics 3, 18, 73, 167 and 278 on the frames 0, 16, 32, 48 and 63; the first formant of the diphthong table sits at harmonic 5, which puts its voice near 146 Hz (the pack names 130 Hz for the men's cycle, 175 Hz for the choir, 220 Hz for the women's cycle).")
    w("- A frozen noise in one table cycle repeats once per cycle and is heard as a pitch. The wind, the crackle and the breath of the flute therefore switch pitch tracking off and fix the cycle (`osc1.pitch_track` 0, `osc1.fixed_pitch`): checked that three keys give the same render.")
    w("- A free running LFO (`lfo1.sync_type` Free-run) did not respond to its rate macro in the offline lab, so Step Morph Pluck uses the default restart mode.")
    w("- The vowel tables are one cycle at a fixed voice pitch, so their formants are right only near that pitch (about an octave around it). The four vocal presets and the dispersion pad set `master.transpose` to minus 12 (section 6).")
    w("")
    w("## 6. Differences from the plan, and why")
    w("")
    w("General:")
    w("")
    w("1. **Frame values are percent.** `osc1.frame` runs 0 to 100 over the 64 frames. The plan wrote frame values and modulation amounts as if they were frame numbers in some places.")
    w("2. **Six macros, not four**, as in batches 1 and 2 (macros 7 and 8 empty). Names are at most 14 characters. Two names of the plan were longer than that (Glissando Speed, Pressure Sense).")
    w("3. **A macro cannot route to a source.** The plan's Acoustic Bore Reed macro 3 (Pressure Sense, to `poly_aftertouch`) is not possible. It is replaced.")
    w("4. **Pad pressure and the wheel follow the conventions of batch 2.** Pressure is `Poly Aftertouch` to Filter 1 cutoff on the held sounds; the decaying keys and plucks and the risers have none.")
    w("5. **Wavetable is a choice name** (`Preset table`), the index 13 of the plan is the same thing.")
    w("6. **Level method.** The plan's target of minus 23 LUFS with the category offsets is what was used; the volumes go through the levels file, not as a parameter value written into the generator (that is how batches 1 and 2 do it, and `gen-factory-presets.py` applies it).")
    w("7. **Tables the plan called used.** The plan counted 7 used tables. They are used (through `content`) by batch 2 presets; none of the 24 here is among them.")
    w("8. **The preset names and the 24 tables are the plan's.** One browser group differs (Choir Swell Ensemble). The parameter values were set again from the real content of the tables and from renders; most numbers differ from the plan.")
    w("")
    w("Per preset:")
    w("")
    w("| # | Preset | Differs from the plan, and why |")
    w("|---|---|---|")
    for i, n in enumerate(ORDER, 1):
        w(f"| {i:02d} | **{n}** | {TEXTS[n][2]} |")
    w("")
    w("## 7. Registration: where the count 160 is, and what was changed")
    w("")
    w("Changed in this branch (generated or data): `scripts/gen-factory-presets.py` (`BATCH_FILES`), `content/presets/contentpack.json` (generated: 184 files, description counts itself; the pack version stays 1.2.0 as in batches 1 and 2), "
      "`scripts/factory-preset-categories.json` (24 entries), `scripts/factory-preset-macro-names.json` (24), `scripts/factory-preset-levels.json` (24), `docs/release/SOUND-SET-CATEGORIES.md` (rebuilt from the categories file: every `.xpl` number after the first insertion moves; "
      "the file also had two sections out of step with the categories file, Lead Synth and Pluck, now consistent), `docs/user-guide/factory-sounds.md` (a batch 3 section; that guide still says 136, it was not brought up to 160 by batch 2 either), "
      "and two tests that the new presets broke by their own registration: `scripts/tests/test_assign_preset_categories.py` (160 to 184, two places) and `scripts/tests/test_propose_pad_pressure.py` (batch 3 added to the list of batches that carry their own pressure route).")
    w("")
    w("Python tests run once at the end (`python3 -m pytest`, no build): `test_check_presets`, `test_loudness`, `test_name_preset_macros`, `test_assign_preset_categories`, `test_propose_pad_pressure` pass (78 tests). "
      "Nine tests of the files below fail only because they pin 160 (`test_docs_mpc`, `test_gen_xpl_presets`, `test_embed_presets` three, `test_gen_mpc_xpl` two, `test_survey_presets`, `test_docs_release`); "
      "the same run showed no other failure. `python3 -I scripts/gen-factory-presets.py --check` says up to date (184 presets), `scripts/check-presets.py` 0 errors, `scripts/assign-preset-categories.py --check` 0 errors.")
    w("")
    w("Not changed, to be changed at integration (they say 160 or 161, or 136 / 112 of an earlier state):")
    w("")
    w("| Where | What it says | Kind |")
    w("|---|---|---|")
    for where, what, kind in [
        ("`core/tests/test_macro_names.cpp:297`", "`CHECK_EQ (presets, 160)`", "C++ test pin"),
        ("`core/tests/test_preset.cpp:302, 311`", "all 160 presets against the manifest and the directory scanner", "C++ test text"),
        ("`shell-mpc/tests/preset_index_host.cpp:44`", "`scanned.size() == 160`", "C++ test pin"),
        ("`shell-mpc/accept.sh:56`", "comment: all 160 presets", "comment"),
        ("`tools/contentpack/tests/test_contentpack.py:191`", "files == 160 for the presets pack", "Python test pin"),
        ("`scripts/tests/test_assign_preset_categories.py:37, 175`", "160 presets", "Python test pin"),
        ("`scripts/tests/test_embed_presets.py:50-149`", "160 entries, `p160_json`, 160 arrays", "Python test pin"),
        ("`scripts/tests/test_gen_mpc_xpl.py:308-353`", "exactly 160 `.xpl` files, 161 programs", "Python test pin"),
        ("`scripts/tests/test_gen_xpl_presets.py:124-133`", "160 factory presets planned, numbers 0 to 159", "Python test pin"),
        ("`scripts/tests/test_survey_presets.py:361-363`", "160 presets, 12720 pairs (160 * 159 / 2); becomes 184 and 16836", "Python test pin"),
        ("`scripts/tests/test_docs_mpc.py:129`", "`len(files) == 112 + 48` (the 160 files)", "Python test pin"),
        ("`scripts/tests/test_docs_release.py:125-126`", "count 160 and the text `160 factory presets` in the changelog draft", "Python test pin"),
        ("`README.md:20, 25`", "`synthyboy_preset_sanity` renders the 160 presets; pack of 160 presets", "doc"),
        ("`docs/RELEASE.md:96`", "factory presets pack: 160 presets, 1.5 MB (size grows)", "doc"),
        ("`docs/release/CHANGELOG-draft.md:6, 29, 34, 36`", "160 factory presets, 161 programs", "doc, the test above reads it"),
        ("`docs/release/DEVICE-CHECKLIST.md:99`", "verify 160 presets' loudness and register", "doc"),
        ("`docs/user-guide/SynthyBoy-MPC-User-Guide.md:51, 816, 828, 848, 1029`", "160 presets, 161 programs; 136 presets of the library; 136 searched for routes", "doc"),
        ("`docs/user-guide/hu/SynthyBoy-MPC-felhasznaloi-kezikonyv.md:49, 796, 808`", "160 presets, 161 programs (and 136 at lines 828, 1034, 1067)", "doc, Hungarian"),
        ("`docs/user-guide/hu/mpce-padok.md:179` and `docs/user-guide/mpce-pads.md:177`", "of the 136 factory presets 65 carry the pad pressure route", "doc: batch 3 adds 17 presets with the route, the count changes (and batch 2 was not added either)"),
        ("`docs/user-guide/factory-sounds.md:5`", "136 production presets", "doc, already stale"),
        ("`docs/release/SOUND-SET-CATEGORIES.md:10`", "(`000` .. `183`) was updated; the status line is unchanged", "doc"),
        ("the generated preset index (`scripts/build-preset-index.py`)", "built at package time", "left alone, as the brief says"),
        ("`plugin.json` and the two tests that name `factory-presets-1.2.0.zip`", "pack version 1.2.0", "a version bump (1.3.0) is an integration decision; batches 1 and 2 did not bump"),
    ]:
        w(f"| {where} | {what} | {kind} |")
    w("")
    w("## 8. For the integration round")
    w("")
    w("1. Change the pins of section 7 in one pass (code tests and docs), then run the generator check: `python3 -I scripts/gen-factory-presets.py --check` (184 presets), `python3 scripts/check-presets.py` (0 errors, the 79 warnings are those of the earlier presets), "
      "`python3 scripts/assign-preset-categories.py --check`, and the Python tests named in section 7. The embedded preset tables (`scripts/embed-presets.py` output) and the `.xpl` set (`scripts/gen-mpc-xpl.py`, `scripts/gen-xpl-presets.py`) are generated at package time and were not regenerated here.")
    w("2. Build once, on the merged tree, through the build slot: the core tests that scan the presets (`test_preset`, `test_macro_names`, `synthyboy_preset_sanity` at every tier) and the MPC shell test `preset_index_host`.")
    w("3. The `.xpl` numbers move (section 7). Any project of the owner that stored a program number is affected, the names are the key.")
    w("4. Listening, in this order: Dispersion Phase Pad (is the swirl audible), Vinyl Crackle Bed (dense rattle or crackle), Pink Noise Sub (is the thump enough), Wind Sweep Riser and Cymbal Cloud Riser (the riser time), the four vocal presets (formants an octave below the key), Step Morph Pluck.")
    w("5. Wavetable presets work only with the factory wavetable pack installed; without a file the engine plays a saw (docs/release/SOUND-SET-BATCH-2.md section 7).")
    w("")
    return "\n".join(L) + "\n"


def guide_section():
    L = []
    w = L.append
    w(GUIDE_MARK)
    w("")
    w("Sound Set Batch 3 adds **24 presets** (184 in all) that are built on the factory wavetables that no earlier preset used: four for each of the six wavetable categories (Analog, Digital, Metallic, Noise, Spectral, Vocal). "
      "They need the factory wavetable pack (`content/factory-wavetables`); without a table file the engine plays a saw. They were measured, not heard.")
    w("")
    w("| Preset Name | Browser Group | What It Is | What to Play | Macro to Try First | Wavetable |")
    w("|---|---|---|---|---|---|")
    for n in ORDER:
        t = TEXTS[n]
        m1 = ENTRY[n][6][0]
        w(f"| **{n}** | **{P[n]['group']}** | {t[0]} | {t[1]} | {m1} (Macro 1) | {wt(n)} |")
    w("")
    return "\n".join(L) + "\n"


def main():
    if "--guide" in sys.argv:
        p = ROOT / "docs/user-guide/factory-sounds.md"
        t = p.read_text(encoding="utf-8")
        sec = guide_section()
        head = t.split(GUIDE_MARK)[0].rstrip("\n") + "\n\n---\n\n"
        t = head + sec
        p.write_text(t, encoding="utf-8")
        print("guide written")
        return
    (ROOT / "docs/release/SOUND-SET-BATCH-3.md").write_text(doc(), encoding="utf-8")
    print("doc written")


if __name__ == "__main__":
    main()
