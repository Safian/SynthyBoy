# Vital importer on real presets (35 files)

> **Superseded in parts by `VITAL-IMPORT-AUDIT-REPORT.md`.** The render results below are wrong: the stored wavetable files were not readable by the
> engine, so every imported table played as a saw (audit bug 1), and 15 of the 105 tables (all built from an audio file) were silent (bug 2). The
> statement that no file uses an audio-file source is wrong (15 tables do). The import counts and the false `dropped` slots fix stand.

Input: the owner's 10 loose `.vital` files plus the 25 presets of `Black_Lotus_Audio_-Division_DnB.vitalbank` (unzipped outside the repo;
the files themselves are not committed). Tool: `synthyboy_vital_import --in DIR --out OUT --report` (new, `tools/vital-import`), then
`synthyboy_preset_render --presets OUT --tier all` (engine vital, 48 kHz). Build: Release, `SYNTHYBOY_BUILD_PLUGIN=OFF`.

A `.vitalbank` is a zip and the tree has no zip reader, so the CLI refuses a bank file and asks for the unzipped directory.

## Result in one paragraph

All 35 presets import with status `ok` (no crash, no hang, no hard failure; the slowest import is well under the 5 s test bound).
Totals: 16998 settings imported, 121 approximated, 66 dropped, 1295 ignored, 52 unknown keys, 105 wavetables stored, 1 sample.
Fidelity: 14 `close`, 21 `degraded`, 0 `exact`. Rendering: every preset on every tier is finite and not silent; 8 presets break the
sanity gate on at least one tier (6 peak above +3 dBFS, 2 tails that do not end within 12 s). Details below.

## Importer bug found and fixed

Vital writes all 64 modulation slots; the unused ones are `{"source":"","destination":""}`. The importer reported each of them as
`dropped / modSourceUnsupported / source_not_exposed`, i.e. 38 to 64 false "dropped" items per preset, and that alone made every one of the
35 presets `degraded` (before the fix: 35 of 35 degraded, 28 to 64 dropped; after: 21 degraded, 14 close). Fix in
`core/vital-import/vital_import.cpp` (an empty source and empty destination slot is skipped without an item); regression test
`vi_unused_mod_slots_are_not_reported` in `core/vital-import/tests/test_vital_import.cpp` (synthetic fixture, 1 used slot + 63 empty).

## Per preset

Columns: counts from the report (imported / approximated / dropped / ignored / unknown keys), stored wavetables and samples, then the
medium tier render (peak and four-note chord peak in dBFS, RMS of the loudest 300 ms at velocity 0.8, tail length in s) and the sanity gate
over the three tiers (low / medium / high).

| Preset | Fidelity | imp | appr | drop | ign | unk | WT | smp | peak dBFS | chord pk | RMS dBFS | tail s | render gate |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ARP_-_Anticipation | degraded | 501 | 2 | 1 | 37 | 0 | 3 | 0 | -3.5 | -4.0 | -14.3 | 7.50 | pass |
| ARP_-_Big_Water | degraded | 511 | 4 | 3 | 37 | 0 | 3 | 0 | -3.3 | -2.7 | -16.0 | 9.88 | pass |
| ARP_-_FM_Forever | degraded | 545 | 3 | 3 | 37 | 0 | 3 | 0 | -0.9 | -7.7 | -11.4 | 3.87 | pass |
| ARP_-_Traveling | degraded | 501 | 2 | 3 | 37 | 0 | 3 | 0 | -5.6 | -5.2 | -17.5 | 6.92 | pass |
| BS_-_Buzzy_8s | degraded | 521 | 3 | 3 | 37 | 0 | 3 | 0 | 1.7 | 0.8 | -6.5 | 1.17 | pass |
| BS_-_Chonker_8s | degraded | 590 | 6 | 4 | 37 | 0 | 3 | 0 | 6.4 | 6.4 | -4.3 | 1.21 | FAIL peak above +3 dBFS |
| BS_-_Feather_Reese | close | 461 | 2 | 0 | 37 | 0 | 3 | 0 | -0.7 | 0.8 | -8.0 | 0.28 | pass |
| BS_-_Harmonic_Insult | degraded | 555 | 3 | 1 | 37 | 0 | 3 | 0 | 1.4 | -3.6 | -2.9 | 0.93 | pass |
| BS_-_Primal_Hunger | degraded | 522 | 3 | 6 | 37 | 0 | 3 | 0 | 3.9 | 4.3 | -2.4 | 1.56 | FAIL peak above +3 dBFS |
| BS_-_Reesed_to_Death | degraded | 490 | 3 | 2 | 37 | 0 | 3 | 0 | -3.2 | -3.1 | -7.3 | 0.83 | pass |
| BS_-_Squoncher | degraded | 558 | 2 | 1 | 36 | 0 | 3 | 0 | 1.8 | 5.1 | -5.6 | 1.18 | FAIL peak above +3 dBFS |
| KEYS_-_Frowny_Saws | close | 480 | 4 | 0 | 36 | 0 | 3 | 0 | -0.2 | 2.2 | -15.9 | 1.79 | pass |
| KEYS_-_Gentle_Driven_EP | degraded | 565 | 3 | 2 | 37 | 0 | 3 | 0 | -4.4 | 2.1 | -9.8 | 10.00 | pass |
| KEYS_-_I_want_a_CS80 | degraded | 461 | 2 | 2 | 37 | 0 | 3 | 0 | -4.9 | 4.6 | -17.9 | 6.15 | FAIL peak above +3 dBFS (chord) |
| KEYS_-_The_Wall | degraded | 470 | 6 | 1 | 37 | 0 | 3 | 0 | -0.9 | 4.1 | -11.5 | 3.64 | FAIL peak above +3 dBFS (chord) |
| LD_-_I_m_Still_Here | degraded | 469 | 4 | 3 | 37 | 0 | 3 | 0 | -5.4 | -5.0 | -10.5 | 3.17 | pass |
| LD_-_Nostalgia | degraded | 483 | 9 | 4 | 30 | 0 | 3 | 1 | 4.8 | 6.4 | -6.1 | 6.02 | FAIL peak above +3 dBFS |
| LD_-_Saccharine_Chanteuse | close | 485 | 3 | 0 | 37 | 0 | 3 | 0 | -0.8 | 1.4 | -9.1 | 2.12 | pass |
| LD_-_Silver_Dollar_ | degraded | 516 | 7 | 4 | 37 | 0 | 3 | 0 | -0.3 | -0.1 | -14.0 | 4.31 | pass |
| LD_-_Sync_Hole_ | close | 477 | 1 | 0 | 37 | 0 | 3 | 0 | -2.3 | -1.6 | -8.3 | 1.34 | pass |
| PAD_-_Cloudgazer | degraded | 516 | 3 | 1 | 37 | 0 | 3 | 0 | 2.0 | 1.8 | -7.9 | 10.70 | pass |
| PAD_-_Glitterfox | degraded | 536 | 2 | 5 | 36 | 0 | 3 | 0 | -11.7 | -8.2 | -21.7 | 7.69 | pass |
| PAD_-_Hanging_Gardens_of_Ra | degraded | 493 | 0 | 14 | 37 | 0 | 3 | 0 | -4.4 | -1.1 | -26.0 | 4.51 | pass |
| PAD_-_The_Edge_of_Space | degraded | 510 | 7 | 1 | 37 | 0 | 3 | 0 | -0.5 | -1.0 | -4.3 | 13.00 | FAIL tail does not end within 12 s |
| PAD_-_Time_Spiral | degraded | 546 | 2 | 2 | 37 | 0 | 3 | 0 | -8.7 | -9.0 | -17.5 | 13.00 | FAIL tail does not end within 12 s |
| 01_Intro_Atmosphere_Reese | close | 430 | 3 | 0 | 38 | 5 | 3 | 0 | -9.2 | -10.1 | -18.0 | 4.84 | pass |
| 01b_Intro_Choppy_Saw_Stab | close | 424 | 4 | 0 | 38 | 5 | 3 | 0 | -31.7 | -31.2 | -51.4 | 1.25 | pass |
| 02_Buildup_Tension_Pulse | close | 419 | 4 | 0 | 38 | 5 | 3 | 0 | -6.9 | -5.7 | -21.1 | 1.76 | pass |
| 03_Drop_Main_Laser_Screech | close | 429 | 4 | 0 | 38 | 7 | 3 | 0 | -11.5 | -15.7 | -22.2 | 0.19 | pass |
| 04_Drop_Rollers_Foghorn | close | 420 | 3 | 0 | 38 | 5 | 3 | 0 | -6.9 | -6.9 | -19.5 | 4.17 | pass |
| 05_Drop_Punchy_Donk_Pluck | close | 419 | 4 | 0 | 38 | 5 | 3 | 0 | -9.0 | -9.6 | -19.2 | 1.25 | pass |
| 06_Drop_Dedicated_Mono_Sub | close | 410 | 3 | 0 | 38 | 5 | 3 | 0 | -3.7 | -2.5 | -14.3 | 0.10 | pass |
| ADPR_The_Utopia | close | 451 | 3 | 0 | 38 | 5 | 3 | 0 | 0.6 | 0.2 | -9.8 | 0.02 | pass |
| BA_-_Reese_Bad_WAH_WAH | close | 415 | 3 | 0 | 38 | 5 | 3 | 0 | -10.2 | -10.2 | -12.3 | 0.17 | pass |
| wauh_Pulse | close | 419 | 4 | 0 | 38 | 5 | 3 | 0 | -8.7 | -10.4 | -20.8 | 1.75 | pass |

(The 25 bank presets come first, then the 10 loose files. "Chord" marks the gate that tripped on the four-note chord only. Gate detail
over tiers: the same presets fail on low, medium and high; on low `LD_-_Nostalgia` fails by the peak at 1.8 / 4.9 dBFS chord, and
`PAD_-_The_Edge_of_Space` / `PAD_-_Time_Spiral` pass the tail gate only on low.)

Render findings, no importer cause established:
- No preset is non-finite or silent. `01b_Intro_Choppy_Saw_Stab` is the quietest (RMS -51.4 dBFS): cutoff 52 (about 165 Hz), key track 0,
  `env_1` sustain 0 / decay 0.28 s, volume 5223, only 3 modulations (LFO 1 and two macros on cutoff / decay). Nothing in the report points to
  a lost setting; whether real Vital is as quiet at the same note cannot be told without Vital.
- The six peak failures are loud presets (RMS -2.4 to -6.1 dBFS). Vital has no output limiter in the file either, so +3 to +6 dBFS peaks
  are plausible as played; the gate is ours, not part of the import.
- The two tails (13 s = the render cap) are pads with long release / reverb; no feedback value outside range was reported.

## Aggregate: dropped / approximated / ignored reasons (all 35 presets)

| Kind | Reason | Detail id | Items | Presets | Most frequent Vital names |
|---|---|---|---|---|---|
| approximated | clamped | outside_our_range | 91 | 33 | reverb_low_shelf_cutoff x29, reverb_pre_low_cutoff x24, chorus_cutoff x14, flanger_dry_wet x7 |
| approximated | noEngineEquivalent | line_map_curve_dropped | 26 | 11 | modulations[N].line_mapping (modulation curve of a mod slot) |
| approximated | noEngineEquivalent | aux_sync_follows_delay_sync | 2 | 2 | delay_aux_sync |
| approximated | customSample | sample_mapped_to_osc | 1 | 1 | sample |
| approximated | enumRemapped | warp_from_sample_unsupported | 1 | 1 | osc_1_distortion_type |
| dropped | modDestinationUnsupported | destination_not_modulatable | 58 | 19 | see below |
| dropped | limitExceeded | all_oscillators_in_use | 5 | 5 | sample (sample on while all 3 oscillators are on) |
| dropped | noEngineEquivalent | no_engine_equivalent | 2 | 1 | osc_1_transpose_quantize, osc_2_transpose_quantize |
| dropped | modSourceUnsupported | source_not_exposed | 1 | 1 | modulation source `stereo` |
| ignored | noEngineEquivalent | not_part_of_our_model | 598 | 35 | voice_priority, voice_override, oversampling, stereo_mode |
| ignored | noEngineEquivalent | engine_fixed_value | 418 | 35 | osc_N_detune_range, osc_N_midi_track, osc_N_spectral_unison |
| ignored | noEngineEquivalent | quality_tier_or_ui | 210 | 35 | osc_N_smooth_interpolation, osc_N_view_2d |
| ignored | noEngineEquivalent | unity_gain | 35 | 35 | voice_amplitude |
| ignored | noEngineEquivalent | same_as_delay_sync | 33 | 33 | delay_aux_sync |
| ignored | limitExceeded | all_oscillators_in_use | 1 | 1 | sample (sample off) |

`destination_not_modulatable` (58 items), by destination: `modulation_N_amount` (modulation of another slot's amount) 51 items over
about 25 distinct slots, `portamento_time` 3, `polyphony` 2, `stereo_routing` 1 (sources: macro_control_1..4 33, mod_wheel 13, env_2/3 8,
velocity 3, note 1). The two real gaps are therefore "a modulation targeting another modulation amount" and a few voice-level targets.

Clamped items are mostly cutoffs at the ends of Vital's range (MIDI note 0 and 136 against our 20 Hz .. 20 kHz).

## Unknown keys

Only the 10 loose files (Vital 1.5.5) carry unknown keys; the bank files (checked one: `synth_version` 1.0.0) have none:
`custom_warps` (10 of 10), `random_values` (10), `osc_1_spectral_morph_phase`, `osc_2_spectral_morph_phase`, `osc_3_spectral_morph_phase`
(10 each), and in `03_Drop_Main_Laser_Screech` additionally `eq_high_shelf_cutoff` and `eq_high_shelf_gain`. Total 52. All of them are
real Vital 1.5 members of `settings` missing from the 730-row table: `custom_warps` (a list of user warp line shapes, e.g. name
`Triangle`, `num_points`, `points`) and `random_values` (a list of `{seed}` per random LFO) are structured values, the other five are
plain numbers (`osc_N_spectral_morph_phase` 0.5, `eq_high_shelf_cutoff` 3000, `eq_high_shelf_gain` 3.5). `custom_warps` matters when a
`osc_N_distortion_type` / spectral warp selects a user warp; `eq_high_shelf_*` points to a shelf EQ band that the table does not map,
to check against our EQ.

## Wavetables, LFO shapes, samples

- 105 wavetables rendered and stored (35 x 3), 22 distinct names; most frequent: `Init` 38, `Basic Shapes` 31, `Classic Blend` 11.
  Content store sizes: loose 2 tables, bank 20 tables (deduplicated by SHA-256), 1 sample.
- No wavetable matched the built-in single-cycle shortcut (`matchBuiltinSingleCycle`), including 36 single-frame `Init` and 3 single-frame
  `Sine` tables. `Init` is a saw built from sine-phase harmonics (starts at 0, falls to the minimum at mid cycle), `Sine` starts at 29578
  (a phase shifted sine). The shortcut compares phase, so real tables with a different phase or polarity never match and are stored.
  This is correct but wasteful (an `Init` table is stored per phase variant instead of mapping to the built-in Saw).
- 46 custom LFO shapes selected as "Custom" (names include Triangle, Sin, Sine, Ramp Down, Snappy Stab, Laser Decay, Growing Oscillations
  (50 points), Random Pulses (66 points)).
- 1 sample: `LD_-_Nostalgia`, "Grinder", 96 kHz stereo, 285094 frames, mapped to osc 3; 5 more presets had `sample_on` with all three
  oscillators in use and lost it (dropped `all_oscillators_in_use`).

## SPEC-GAP guesses against the real data

`grep SPEC-GAP core/` lists about 25 guesses. What the 35 files can tell:

| SPEC-GAP | Verdict |
|---|---|
| `vital_content.cpp:193` line object stores `1 - value` | Confirmed. Vital's own `Triangle` LFO is in the file as points (0,1), (0.5,0), (1,1); the importer reads it as 0 -> 1 -> 0, which is the triangle that starts at the bottom. |
| `builtin_ref.cpp:253` comparison up to harmonic 40, phase kept | Contradicted in practice: real single-frame tables have arbitrary phase / polarity (`Init`, `Sine`), so the built-in shortcut never fires (0 of 39 single-frame tables). Matching up to phase and polarity would be needed to use it. |
| `builtin_ref.cpp:125`, `:265` level not part of the comparison | Not testable (shortcut never fires on these files). |
| `builtin_ref.cpp:183` LFO shapes 0..4 orientation | Not decidable from the data: all 46 LFO shapes are stored as custom lines, none matched a built-in. |
| `wavetable_render.cpp` frame / component formulas (pull_power, window, fade styles, vocode, fold boost, warp curves, slew, post-process) | No golden here. The files only carry source descriptions, no rendered frames, so correctness of the rendered tables cannot be checked without Vital output; what the data does show is that all 105 tables render without error, within the work budget, and finite. |
| `wavetable_render.cpp:294` / `:298` window size / start position for audio sources | No file in this set uses an audio-file source component with these fields to check (the only sample is the oscillator sample, not a wavetable source). |
| frame position of tables with fewer than 257 frames (design open question 4) | Real tables have 1, 221, 225 and 257 frames; the question stays open, it needs the A/B render against Vital. |

## Reproduce

```
cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF
cmake --build build --target synthyboy_vital_import synthyboy_preset_render
unzip BANK.vitalbank -d /tmp/bank
build/tools/vital-import/synthyboy_vital_import --in /tmp/bank --out /tmp/imp/bank --report
build/tools/preset-render/synthyboy_preset_render --presets /tmp/imp --tier all
```
