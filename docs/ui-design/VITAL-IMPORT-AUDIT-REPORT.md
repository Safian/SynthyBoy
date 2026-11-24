# Vital importer: independent audit on the owner's 35 real presets

Input: the 10 loose `.vital` files of `tests/vital-presets/` and the 25 presets of `Black_Lotus_Audio_-Division_DnB.vitalbank` (a zip, unzipped to a
temp directory outside the repository; no preset file is committed). Tools: `scripts/audit-vital-import.py` (new), `synthyboy_vital_import`,
`synthyboy_vital_roundtrip` (new, `tools/vital-import/vital_roundtrip.cpp`), `synthyboy_preset_render` (fixed: content store). Release build,
engine vital, 48 kHz. Supersedes the render results and two statements of `VITAL-REAL-PRESETS-REPORT.md` (see section 6).

## 1. Result in one paragraph

The audit found one bug that invalidated every earlier render and several accounting and mapping bugs; all are fixed with synthetic regression
tests. After the fixes: 27187 settings keys of the 35 files are accounted exactly once (0 unaccounted, 0 double), 17382 imported / approximated
rows recompute to the value in the `.sbpreset` (0 mismatches; the same check fails on 11 injected mutations), all 1484 keys of every preset read
back unchanged from the Vital engine (35 x 1484, 0 mismatches), and the Vital control inside the engine equals the number in the original file
for 16992 controls (133 more deviate for a documented reason: 90 clamps / remaps the importer reports, 33 `effect_chain_order`, 6 `sample_on`, 4 sample
oscillator controls; 0 unexplained). 8 presets still fail the render gate; with the real wavetables loaded
the cause is the presets themselves (hot master volume, up to +25 dB makeup in an OTT-style compressor, 5 to 7 s reverbs behind it), not the
importer. The real gaps are in section 8; the largest is modulation of other modulation amounts (52 connections in 17 of 35 presets).

## 2. Method (what is independent of the importer)

| Check | How | Source of truth |
|---|---|---|
| Completeness | every key of `settings` (and each of the 64 modulation slots, 3 wavetables, 8 LFO shapes, the sample object) is mapped to exactly one class by `report.json` items, the table rows or a named implicit class (unused modulation slot, consumed by the mapped sample) | the `.vital` file itself |
| Correctness | expected value = Vital's own definition of the control (`synth_parameters.cpp`: range, value scale, display multiplier, unit) converted into OUR unit from `docs/params.json`, clamped, snapped; special rows by their own rule (frame position, destination, warp map, sample, routing, effect order, bend range, modulation source / destination choices) | Vital's parameter definition, not `vital_map.cpp` conversion columns |
| Untouched keys | every key the importer does not address still holds its default (all 1484 - 730 - specials) | `docs/params.json` |
| State round trip | `.sbpreset` loaded, every param set, content store attached, one block rendered, `getParam` compared (1484 keys x 35) | engine |
| Engine controls | `vitalControlValue` of every addressed control compared with the raw number of the original file | engine (forward conversion of the adapter), so importer inverse and adapter must agree |
| Negative test | 11 values of one `.sbpreset` changed by hand (cutoff, release, mod amount, warp type, switch, two effect slots, reverb decay, volume, LFO sync, modulation destination): 12 errors reported | |

`python3 -I scripts/audit-vital-import.py --vital DIR [--vital DIR] --imported OUT --roundtrip rt.json` (exit code 1 on any error). Reproduction at
the end.

## 3. Aggregate numbers (35 files)

| | before the audit (previous report) | after |
|---|---:|---:|
| settings keys + structured members | 27187 | 27187 |
| keys accounted nowhere | 59 (52 unknown members + 7 `sample_*` keys of the mapped sample) | 0 |
| report items: imported / approximated / dropped / ignored | 16998 / 121 / 66 / 1295 | 17014 / 121 / 72 / 1332 |
| wavetables stored / silent | 105 / 15 (and every one read as a saw by the engine) | 105 / 0 |
| presets failing the render gate | 8 (rendered with the saw fallback instead of the tables) | 8 (rendered with the real tables, section 7) |
| fidelity close / degraded | 14 / 21 | 14 / 21 |

The 8270 keys `modulation_N_{amount,power,bipolar,stereo,bypass}` of the 1654 unused modulation slots (Vital writes all 64) are the implicit class
"ignored: unused slot" (design 1.1) and are not report items. The fidelity colour did not change: the new `dropped` items are real losses.

Per file (importer item counts; modulation connections imported / dropped / curve dropped; custom tables; medium tier render: peak / four note chord
peak / RMS of the loudest 300 ms in dBFS and tail in s, `FAIL` = gate):

| Preset | Fidelity | keys | imp | appr | drop | ign | mod i/d/c | WT | render |
|---|---|---:|---:|---:|---:|---:|---|---:|---|
| ARP - Anticipation | degraded | 775 | 501 | 2 | 1 | 37 | 18/1/0 | 3 | -3.3 / -3.3 / -13.8 / 7.53 |
| ARP - Big Water | degraded | 775 | 511 | 4 | 4 | 36 | 20/3/2 | 3 | -4.2 / -5.7 / -17.8 / 9.22 |
| ARP - FM Forever | degraded | 775 | 545 | 3 | 4 | 36 | 27/3/0 | 3 | -5.2 / -12.4 / -15.9 / 3.23 |
| ARP - Traveling | degraded | 775 | 501 | 2 | 3 | 37 | 18/3/0 | 3 | -2.1 / -2.5 / -14.1 / 8.28 |
| BS - Buzzy 8s | degraded | 775 | 521 | 3 | 3 | 37 | 22/3/1 | 3 | 1.1 / 0.7 / -6.5 / 1.19 |
| BS - Chonker 8s | degraded | 775 | 590 | 6 | 4 | 37 | 36/4/3 | 3 | 6.4 / 6.4 / -4.6 / 1.21 FAIL |
| BS - Feather Reese | close | 775 | 461 | 2 | 0 | 37 | 10/0/0 | 3 | 0.2 / 2.8 / -4.2 / 0.22 |
| BS - Harmonic Insult | degraded | 775 | 555 | 3 | 1 | 37 | 29/1/0 | 3 | 1.3 / -3.8 / -2.9 / 0.95 |
| BS - Primal Hunger | degraded | 775 | 522 | 3 | 6 | 37 | 22/5/2 | 3 | 4.5 / 4.8 / -2.3 / 1.65 FAIL |
| BS - Reesed to Death | degraded | 775 | 490 | 3 | 3 | 36 | 16/2/0 | 3 | -3.3 / -3.1 / -7.3 / 0.83 |
| BS - Squoncher | degraded | 775 | 558 | 2 | 1 | 36 | 29/1/1 | 3 | 2.8 / 2.4 / -5.7 / 1.14 (FAIL on tier high: 3.0) |
| KEYS - Frowny Saws | close | 775 | 480 | 4 | 0 | 36 | 14/0/0 | 3 | 0.4 / 2.1 / -15.9 / 1.79 |
| KEYS - Gentle Driven EP | degraded | 775 | 565 | 3 | 2 | 37 | 31/1/0 | 3 | -3.7 / 1.9 / -9.7 / 10.01 |
| KEYS - I want a CS80 | degraded | 775 | 461 | 2 | 2 | 37 | 10/2/0 | 3 | -3.8 / 6.1 / -16.8 / 6.36 FAIL |
| KEYS - The Wall | degraded | 775 | 470 | 6 | 1 | 37 | 12/1/3 | 3 | -0.3 / 3.6 / -10.8 / 3.64 FAIL |
| LD - I'm Still Here | degraded | 775 | 469 | 4 | 4 | 36 | 12/3/0 | 3 | -13.0 / -17.9 / -18.0 / 2.79 |
| LD - Nostalgia | degraded | 775 | 489 | 9 | 4 | 31 | 15/4/3 | 3 | 5.2 / 6.4 / -5.1 / 6.26 FAIL |
| LD - Saccharine Chanteuse | close | 775 | 485 | 3 | 0 | 37 | 15/0/0 | 3 | -0.3 / 1.2 / -9.0 / 2.14 |
| LD - Silver Dollar | degraded | 775 | 516 | 7 | 4 | 37 | 21/4/5 | 3 | 1.1 / 0.5 / -13.2 / 4.40 |
| LD - Sync Hole | close | 775 | 477 | 1 | 0 | 37 | 13/0/0 | 3 | -2.8 / -1.5 / -8.7 / 1.35 |
| PAD - Cloudgazer | degraded | 775 | 516 | 3 | 1 | 37 | 21/0/1 | 3 | 1.9 / 1.8 / -7.9 / 10.70 |
| PAD - Glitterfox | degraded | 775 | 536 | 2 | 5 | 36 | 25/3/0 | 3 | -12.9 / -12.5 / -23.0 / 8.15 |
| PAD - Hanging Gardens of Ra | degraded | 775 | 493 | 0 | 15 | 36 | 16/14/0 | 3 | -4.4 / -0.4 / -23.1 / 5.51 |
| PAD - The Edge of Space | degraded | 775 | 510 | 7 | 1 | 37 | 20/0/4 | 3 | -4.5 / -3.5 / -10.8 / 13.00 FAIL (tail) |
| PAD - Time Spiral | degraded | 775 | 546 | 2 | 2 | 37 | 27/1/0 | 3 | -8.5 / -9.5 / -17.4 / 13.00 FAIL (tail) |
| 01_Intro_Atmosphere_Reese | close | 781 | 431 | 3 | 0 | 42 | 4/0/0 | 3 | -9.7 / -10.7 / -18.3 / 4.53 |
| 01b_Intro_Choppy_Saw_Stab | close | 781 | 425 | 4 | 0 | 42 | 3/0/0 | 3 | -30.5 / -29.6 / -50.3 / 1.41 (env 1 decay 6 ms, macro 2 = 0) |
| 02_Buildup_Tension_Pulse | close | 781 | 420 | 4 | 0 | 42 | 2/0/0 | 3 | -8.1 / -7.5 / -20.3 / 1.75 |
| 03_Drop_Main_Laser_Screech | close | 783 | 430 | 4 | 0 | 44 | 4/0/0 | 3 | -10.6 / -10.3 / -19.2 / 0.20 |
| 04_Drop_Rollers_Foghorn | close | 781 | 421 | 3 | 0 | 42 | 2/0/0 | 3 | -7.5 / -7.6 / -19.0 / 3.96 |
| 05_Drop_Punchy_Donk_Pluck | close | 781 | 420 | 4 | 0 | 42 | 2/0/0 | 3 | -11.9 / -9.8 / -19.8 / 1.02 |
| 06_Drop_Dedicated_Mono_Sub | degraded | 781 | 411 | 3 | 1 | 41 | 0/0/0 | 3 | -9.9 / -9.5 / -15.0 / 0.09 (voice_override dropped) |
| ADPR_The_Utopia | close | 781 | 452 | 3 | 0 | 42 | 8/0/1 | 3 | -0.2 / 0.9 / -9.5 / 0.02 |
| BA - Reese Bad WAH WAH | close | 781 | 416 | 3 | 0 | 42 | 1/0/0 | 3 | -10.2 / -10.2 / -11.7 / 0.17 |
| wauh Pulse | close | 781 | 420 | 4 | 0 | 42 | 2/0/0 | 3 | -10.7 / -10.7 / -20.2 / 1.75 |
| total | 14 close / 21 degraded | 27187 | 17014 | 121 | 72 | 1332 | 527/59/26 | 105 | |

Key classes over all files (keys, not items): imported 17049, approximated 94, dropped 303 (59 connections x 5 slot keys + 8), ignored 9601 (8270 of
them the unused slots) plus the structured members: 2240 modulation slots = 527 imported + 59 dropped + 1654 empty; 105 wavetables and 280 LFO
shapes imported; 35 sample objects = 28 white noise, 1 mapped to an oscillator, 5 dropped (no free oscillator), 1 ignored (off).

## 4. Completeness: what is ignored, what is dropped, and whether that is right

`ignored` is legitimate when the setting is UI, host or tier state, or a value the engine fixes; it is a missing feature when a non-default value
changes what the preset sounds like. The importer now follows that rule (bug 6): a non-default value of a behaviour setting is `dropped`.

| Class (Vital key) | Items | Verdict | Reason / what would be needed |
|---|---:|---|---|
| `modulation_N_*` of the 1654 unused slots | 8270 keys | legitimate | Vital stores 64 slots; slots without a connection carry no information |
| `osc_N_detune_range` (2), `osc_N_midi_track` (1), `osc_N_spectral_unison` (1) | 315 | legitimate while at the default | the engine fixes them; all 35 files use the default. A non-default value is reported `dropped` (exists in the code, no file uses it) |
| `osc_N_transpose_quantize` = 0 | 103 | legitimate | engine fixed value |
| `osc_N_transpose_quantize` != 0 | 2 dropped | **missing feature** | PAD - Glitterfox osc 1 (1065) and osc 2 (1): needs key `oscN.transpose_quantize` (0..8191 note mask) + binding of the engine control `osc_N_transpose_quantize` (Conv index, no tier cost) |
| `osc_N_smooth_interpolation`, `osc_N_view_2d`, `view_spectrogram` | 245 | legitimate | quality tier / UI state |
| `bypass`, `pitch_wheel`, `oversampling`, `beats_per_minute`, `stereo_mode` (0), `voice_priority` (4 = Vital's default) | 210 | legitimate | host / tier / UI state or the default behaviour |
| `mod_wheel` | 35 (23 files non-zero, 74 connections use the wheel as source) | legitimate, to confirm | in this engine the modulation source `mod_wheel` is fed by MIDI CC 1 through the voice handler (`mod_wheel_values_` in `voice_handler.h`), not by a stored control, so the stored number is the on-screen slider. Needs one A/B against Vital to be sure it is not restored |
| `voice_amplitude` = 1 | 35 | legitimate | unity (non-1 is folded into the volume) |
| `delay_aux_sync` (= `delay_sync`) | 33 | legitimate | follows the sync mode; a difference is `approximated` (2 files) |
| `sample_*` of a white-noise / unmapped sample | 238 | legitimate | no sample is played; for a custom sample that cannot be placed the `dropped sample` item stands for them |
| `stereo_routing` = 1, `voice_override` = 0 | 64 | legitimate | defaults |
| `stereo_routing` != 1 | 5 dropped (0.41, 0.72, 0.78, 0.865, 0.245) | **missing feature** | the voice stereo routing narrows the image of the preset; needs `master.stereo_routing` (percent) + binding of the control `stereo_routing` |
| `voice_override` = 1 | 1 dropped | **missing feature** | `06_Drop_Dedicated_Mono_Sub` (steal instead of kill); needs `master.voice_override` (Kill / Steal) + binding of the control |
| `osc_N_spectral_morph_phase` (Vital 1.5, 0.5) | 30 ignored | legitimate while default | no key, no engine control; non-default with the morph audible = `dropped` (none in the files) |
| `custom_warps` (10 loose files) | 10 | legitimate | only selected by a warp type above the 13 built-in ones, none does |
| `random_values` (seeds of the random LFOs) | 10 | legitimate | the sequence of a random LFO is not reproducible by definition; a key `rlfoN.seed` would only make the engine repeat Vital's sequence |
| `eq_high_shelf_cutoff`, `eq_high_shelf_gain` (1 loose file) | 2 | legitimate | not a Vital control (foreign member, 3000 Hz / 3.5 dB); nothing to map |
| `sample` (custom, no free oscillator) | 5 dropped | **missing feature** | the Vital sample is a 4th source next to the 3 oscillators; needs a sample source independent of the oscillator slots (the `noise` source could host a sample, `noise.type`) |
| modulation connections with a destination we cannot modulate | 52 + 3 + 2 + 1 dropped | **missing feature** | section 8 G1 / G3 |
| modulation connection with source `stereo` | 1 dropped | **missing feature** | section 8 G4 |

Approximated rows (121): reverb low shelf cutoff below 20 Hz (29: 26 x note 0 = 8 Hz, three 8 .. 13 Hz), curve of a modulation (26, see G2), reverb pre
low cut (24: 23 x note 0 = 8 Hz, one 300 = outside Vital's own 0 .. 128), chorus cutoff (14: 11 x note 136 = 20.5 kHz, 3 x note 8 = 8 Hz), flanger mix
0.707 (7; Vital's own range is 0 .. 0.5), EQ low cutoff (4: two x note 8, two beyond Vital's own 136: 140 / 160), delay time -3 / -4 (3; Vital's
own range is -2 .. 9), chorus delay 1 ms floor (3), `delay_aux_sync` (2), master volume +6.02 dB (2), reverb pre high cut 128 (2), and one each of flanger center, FX filter cutoff, oscillator frame position 161.96 beyond the 100 % of a
221 frame table, warp from sample (osc 1 of The Edge of Space) and the mapped sample. Every one is a clamp into our narrower range or a value
outside Vital's own range, none is audible. `legacyCapped` (choice index above the pre-append list): 0 cases in the 35 files; unit-tested.

## 5. Correctness

17382 rows recomputed (every imported / approximated table row, the specials: bend range, routing, effect order, MPE, voice amplitude, the
connections of the 64 modulation slots, wavetable / LFO / sample content): 0 mismatches. What the check covers in particular:

* frequency, time and dB scaling: cutoff note -> Hz, LFO / chorus / flanger / phaser / reverb-chorus rate 2^x, delay period, envelope x^4 s -> ms,
  smooth time, reverb decay, portamento (-10 = off = 0 ms), master volume sqrt(x) - 80 dB with the -60 dB floor.
* choice enums: filter model / style (same index, appended models unreachable), warp type (`{0..8, 10, 11}` map; 9 and 12 = from sample -> None,
  1 case), LFO sync / tempo / sync type, delay tempo (4..12 -> 0..8), unison stack, morph type, oscillator destination (5..14 -> Effects only).
* bipolar vs unipolar: ranges of the percent keys come from Vital's range (spread -0.5..0.5 -> +-100 %, flanger mix 0..0.5 -> 0..100 %), modulation
  amount x100, power, bipolar / stereo / bypass flags of all 527 imported connections; source and destination choice numbers from `docs/params.json`.
* frame position: x / (frames - 1) x 100 with the frame count of that oscillator's table (257 for a built-in): checked against the content that was
  stored and, through the engine, the engine control equals the file number for all 105 tables.
* oscillator on / destination, sample routed to the first free oscillator (6 sample settings consumed by that oscillator), noise switched off for a
  custom sample, effect enables and the order of the 9 effects (two decoders, 25 distinct codes in the files, always a permutation of the 16 slots),
  macro values and names, voice / portamento settings, bend range (clamped to 24).
* metadata: name (file stem, the files have no `preset_name`), author, comments -> description, style -> category, tags, `synth_version`.
* LFO shapes: 280 shapes, points `(x, 1 - y)` and powers equal to the file; wavetable frame counts equal the last keyframe position + 1.

Not verifiable without Vital itself: the numbers of the rendered wavetables (clean-room renderer, see `VITAL-REAL-PRESETS-REPORT.md` SPEC-GAP
list) and the effect-order decode (both decoders in the audit follow the same statement of Vital's encoding; identity for code 0 and a permutation for
every code is checked, whether the permutation is Vital's needs one A/B with Vital).

## 6. Bugs found and fixed (all with a synthetic regression test)

| # | Bug | Evidence | Fix |
|---|---|---|---|
| 1 | **The stored wavetable files were raw int16 frames, the engine reads WAV.** `IContentStore` + `decodeWavetable` need RIFF/WAVE (ARCHITECTURE 5.2); every imported "Preset table" fell back to the saw. All earlier renders of imported presets (previous report, the gate results, loudness) were saw renders. The ref hash was the hash of the frames, not of the file | `.sbwt` file starts with sample bytes, not `RIFF`; renders with and without the content store were identical to 0.1 dB | `UserWavetableStore` writes `tables/<name>_<hash>.wav` (16-bit mono, nominal 44.1 kHz), `ImportedWavetable.wavBytes`, `sha256` = hash of the file; `user_wavetable_store`, `vi_import_serialize_load_round_trip` (reads the file back through `DirectoryContentStore` with the hash check) |
| 2 | **All 15 wavetables built from an audio file rendered silent** (9 of 35 presets, e.g. all three tables of Glitterfox). `normalize_gain` / `normalize_mult` are JSON booleans; `normalize_mult` was read as a number, `false` became a gain of 0 | `peak = 0` in the stored tables, `Audio File Source` in the 15 silent ones | `Component.normalizeMult` / `normalizeGain` are booleans (default true / false as Vital's loader), normalise to peak 1 only with `normalize_mult`, boost only with `normalize_gain`; `wt_audio_file_source`, `vi_audio_file_table_with_off_switches_is_not_silent` |
| 3 | The engine cut a user table at 256 frames; Vital tables have 257 (positions 0 .. 256, 22 of the 105 tables), so the frame position of an imported table was one frame short and the last frame never played | `osc_1_wave_frame` of the engine = 255 for `osc1.frame` = 100 % | `kNumOscillatorWaveFrames` in `decodeWavetable` and `buildUserWavetable`; `user_wavetable_of_257_frames_keeps_every_frame` |
| 4 | Unknown `settings` members were counted, never reported (52: `custom_warps`, `random_values`, `osc_N_spectral_morph_phase`, `eq_high_shelf_*`) | audit: 52 keys in no class | one item each, `dropped` only when audible (morph phase of an audible morph, a selected user warp); `vi_every_unknown_setting_gets_an_item` |
| 5 | The `sample_*` settings consumed by a sample mapped to an oscillator were in no class (7 keys) | audit on LD - Nostalgia | counted as imported; `sample_transpose_quantize` = 0 ignored; `vi_mapped_sample_settings_are_accounted` |
| 6 | Behaviour settings with a non-default value were `ignored` (`stereo_routing` x5, `voice_override`) | audit of the ignored list against the file values | `dropped`; `vi_behaviour_settings_are_dropped_when_not_default_and_mpe_is_imported` |
| 7 | `mpe_enabled` had a key (`master.mpe.on`) but was ignored | | imported (same test) |
| 8 | `synthyboy_preset_render` never attached a content store, so the gate rendered imported presets with the saw fallback and silence for samples | | `--content-dir DIR` (default `PRESETS/content`); factory presets unchanged |

Also changed: the wrong statements of `VITAL-REAL-PRESETS-REPORT.md` ("no file uses an audio-file source": 15 tables do; "all 105 tables render":
15 rendered silent). Annex text of doc 11 (1.3.1 step 4, Audio File Source fields) updated.

## 7. The 8 failing presets and the PAD with 14 dropped items

Render gate (peak above +3 dBFS, tail not below -90 dBFS within 12 s) with the real tables, all tiers. The set of failing presets is the same 8 as
before (6 peak: Chonker 6.4, Primal Hunger 4.5, Squoncher 3.0 on tier high, CS80 chord 6.1, The Wall chord 3.6, Nostalgia 5.2 / 6.4; 2 tails: The Edge
of Space and Time Spiral 13 s), but the earlier numbers were saw renders. With bug 1 fixed first, Glitterfox rendered exactly silent, which is how
bug 2 was found. The causes, with the ablations that show them (the same preset with one stage switched off, medium tier):

| Preset | Cause | Evidence |
|---|---|---|
| Chonker, Primal Hunger, Squoncher | upward / OTT style multiband compressor with 7 .. 25 dB makeup gain (`compressor.low.gain` 24.9 dB, mid 18.9 dB) behind distorted unison | compressor off: Chonker 6.4 -> 1.6, Primal Hunger 4.5 -> 0.7, Squoncher 2.8 -> -5.9 dBFS. The highest value in all 35 presets is 6.4 dBFS = 20 log10 2.1, the hard clamp of `sound_engine.cpp` (`Clamp(-2.1, 2.1)`), i.e. the chain runs into Vital's own ceiling |
| CS80, The Wall, Nostalgia | master volume +2.5 / +5.3 / +6.0 dB (file value 7399 = the maximum), 4 notes at velocity 1 add up; compressor is not the reason (CS80 and Nostalgia have it off) | single note peaks -3.8 / -0.3 / 5.2 dBFS, chord +10 dB |
| The Edge of Space, Time Spiral | the same compressor lifts the reverb / delay tail: reverb decay 7.4 s and 5.5 s | compressor off: Edge of Space 13 s -> 10.5 s, peak -4.5 -> -18.6 dBFS; reverb decay 3 s: 5.9 s; all effects off: 1.2 s. Time Spiral: compressor off 10.3 s, flanger off 11.2 s |

All of these use the Vital DSP with the control values of the file (engine read-back equal to the file, section 1), so a real Vital would produce the
same level up to the dropped connections. The gate (+3 dBFS, 12 s) is a factory preset rule; it should not be applied to imported presets (report
only). The dropped modulations can only move the numbers by what they scale; the one preset where they clearly matter for level is
**PAD - Hanging Gardens of Ra** (14 dropped, 9 dB below the median): its macro 1 is 100 % and `macro_control_1 -> modulation_7_amount / _2_ / _5_ ...`
raise the LFO 1 -> filter cutoff / osc 3 level / osc 1 warp connections whose own amounts are 0 (and `velocity -> modulation_17_amount`,
`mod_wheel -> modulation_N_amount`). 13 of its 14 drops are connections whose destination is the amount of another connection, the 14th is
`macro_control_4 -> stereo_routing`; it was not an importer bug but G1 below: the engine had no way to modulate a modulation amount (fixed by B20, see the end of section 8).

## 8. Remaining true gaps (proposed change for each)

G1 - G6 were built by the mod-amount work package (block B20, `MODAMT-REPORT.md`, merged on top of B15); the table shows what each gap became. Re-run of
the audit on the same 35 files after the merge: **35 presets, 0 mismatches, errors 0**, 17852 value rows checked, 17287 engine read-backs equal to the file;
modulation connections imported 586 (the first audit dropped 59 for the reasons of G1, G3, G4), of them **53 with a `modulation_N_amount` destination in 17 presets** (this run counted 53,
the first audit 52) and 1 with the source `stereo`; **31 `line_mapping` curves** in `content.mod_curves[]` (the first audit counted 26 approximated as linear; the audit script now
counts every carried curve); dropped items: 5, all of them the sample object (G7, unchanged). **PAD - Hanging Gardens of Ra**: 0 dropped, 0
approximated (before: 14 dropped); rendered once with macro 1 at 0 and at 100 % (the other macros as imported, tier high, note C3): the two renders differ by
an RMS of 0.0036 on a signal RMS of 0.0186 (19.6 %), i.e. the macro now moves the amounts of the connections it drives (before the merge the 6 connections of macro 1 to
`modulation_N_amount` were dropped and the macro changed nothing of that).

| # | Gap | In the 35 files | Status | What it became |
|---|---|---|---|---|
| G1 | **Modulation of a modulation amount** (`X -> modulation_N_amount`) | 53 connections, 17 presets | **done (B20)** | 64 appended destinations `modNN.amount` (choices 454 .. 517); the engine uses Vital's own `modulation_N_amount` input (per voice for per-voice sources); loops are not wired; UI: submenu "Mod Amounts" in the destination picker (loops greyed), loop badge on the row |
| G2 | **Curve of a modulation** (`line_mapping`) | 26 (first audit) / 31 (counted now), 11+ presets | **done (B20)** | `content.mod_curves[]` (<= 100 points per slot), the connection's own line map in the adapter, "Custom" curve editor in the Slot Curve panel (the remap graph widget in mod curve mode); the importer stores every curve |
| G3 | Modulation of `portamento_time`, `polyphony`, `stereo_routing` | 3 + 2 + 1 | **done (B20)** | destinations 518 (`master.portamento.time`), 519 (`master.polyphony`), 453 (`master.stereo_routing`); each has an off / on render test |
| G4 | Modulation source `stereo` | 1 connection | **done (B20)** | source choice 39 `stereo` |
| G5 | `stereo_routing`, `voice_override` | 5 + 1 presets | **done (B20)** | keys `master.stereo_routing` (1594), `master.voice_override` (1595) |
| G6 | `osc_N_transpose_quantize` | 1 preset (2 oscillators) | **done (B14 key, B20 importer mapping)** | the existing `oscN.transpose_snap` (no new key, no new capability bit) |
| G7 | Sample as a 4th source | 5 presets lose their sample (Primal Hunger, Gentle Driven EP, Cloudgazer, Edge of Space, Time Spiral) | open | `noise.type` (Noise / Sample) with the sample keys of `oscN`, or a 4th sample voice |
| G8 | Stored `mod_wheel` position (23 files non-zero) | | open | only if an A/B shows that Vital applies it: initial CC1 for the shell, no key |
| G9 | Single-frame `Init` / phase shifted `Sine` are stored instead of a built-in | 36 + 3 of 43 single-frame tables | open | Cheap exact option: one appended built-in `Saw down` in the table list |

## 9. Built-in shortcut (`matchBuiltinSingleCycle`)

It works (test: an exact copy of the built-in Sine / Saw / Square / Triangle / Pulse 25 % maps to the built-in, nothing stored) and it never fires on
the real files because the real single-frame tables are **not** the built-in waveforms: Vital's `Init` is `-saw(n + N/2)`, a falling saw whose jump is
at mid cycle (sine coefficients `-2/pi, +1/pi, -2/(3 pi), ...`, ours `-2/(pi k)` rising), and the table named `Sine` is `0.903 cos + 0.430 sin` (a sine
shifted by 64.5 degrees). A built-in table cannot reproduce either: a shift would have to be moved into `oscN.phase`, which is not equivalent under
warp (sync, formant, bend, quantize move the phase before the table lookup), and the inverted polarity has no key at all. Substituting them would
change sync / FM / folding timbres and the sum with other oscillators, so the importer keeps them as content (4 KB each, deduplicated by hash: 8
distinct files for the 43 single-frame uses). `vi_single_cycle_shortcut_needs_the_same_waveform` pins this behaviour. The wasteful part disappears with G9.

## 10. Reproduce

```
cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target synthyboy_vital_import synthyboy_vital_roundtrip synthyboy_preset_render
unzip tests/vital-presets/Black_Lotus_Audio_-Division_DnB.vitalbank -d /tmp/bank
mkdir /tmp/vp && cp tests/vital-presets/*.vital /tmp/vp && cp -R /tmp/bank /tmp/vp/bank
build/tools/vital-import/synthyboy_vital_import --in /tmp/vp --out /tmp/imp --report
build/tools/vital-import/synthyboy_vital_roundtrip --presets /tmp/imp --out /tmp/rt.json
python3 -I scripts/audit-vital-import.py --vital /tmp/vp --imported /tmp/imp --roundtrip /tmp/rt.json
build/tools/preset-render/synthyboy_preset_render --presets /tmp/imp --tier all     # reads /tmp/imp/content
```
