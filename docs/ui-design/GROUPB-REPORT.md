# GROUPB - WP-05 Oscillator extras, WP-06 Modulation sources and audio-rate taps, WP-08 Matrix RISE / FALL and sheet extras, WP-18 Importer follow-ups

Branch `work-groupB` (from `develop` ad512bd). Spec: `13-new-keys.md` 4.33 .. 4.36 with assumptions A397 .. A411; morning questions 77 .. 83. Keys 2125 -> **2134** (block B27, positions 2125..2133, nine keys), modulation destinations 908 -> **911** (`oscN.sample.slice`), modulation sources 42 -> **57**, warp list entries 52 -> **76**, `oscN.wavetable` choices 14 -> **15** ("Saw Down"), capability bits **60 .. 63** (four new, the last one is the 64th), `kMaxParams` 4096 untouched, state version unchanged (additive `rise` / `fall` in `content.mod_curves[]`), Kick pages 143 (the new keys went onto the existing pages). `gen-factory-presets.py --check` passes (the 112 presets carry the nine new keys at their neutral defaults; the renders are unchanged).

## 1. What was built

### WP-05 Oscillator extras (B27)

| Part | Files | One line |
|------|-------|----------|
| Keys, caps, Kick | `param_table.cpp` (`SB_OSC_EXTRA2`), `engine_caps.*` (bit 60 `kCapOscExtras2`), `modulation.h`, `kick_pages.cpp`, regenerated `params.json`, `PARAMETERS.md`, `kick-pages.json`, the 112 presets | `oscN.ratio_src`, `oscN.smooth_interp`, `oscN.sample.slice` |
| Engine: Ratio SRC | `synth_oscillator.cpp/.h` (`pitchTap`, `ratio_source_`), `sub_source.*`, `sample_oscillator.*` (`setPitchTap`), `producers_module.cpp` (`connectRatioSource`, `effectiveRatioSource`, processing order), `oscillator_module.*`, `vital_bindings.cpp` | the pitch of an oscillator or the sub as of the last block is the note of an oscillator in Ratio mode; loops, itself and switched off sources play the note |
| Engine: smooth interpolation | `synth_oscillator.cpp:1338` (`computeSpectralWaveBufferPair`), `synth_parameters.cpp` | the control that no code read is now the switch: Off steps through the frames (as always), On blends the next frame in |
| Engine: single slice | `sample_oscillator.cpp:128`, `oscillator_module.cpp`, `synth_parameters.cpp` | an offset of the key's slice in Play Single Slice mode, modulatable |
| UI | `OscPanel.cpp`, `ParamControls.*` (`setEntryFilter`), `model/RatioSource.h` | the source list greys the oscillator itself and loops; dimming by mode |
| Importers | `vital_import.cpp`, `serum_voice.cpp`, `serum_fx.cpp`, `audit-vital-import.py`, `audit-serum-import.py` | Vital `smooth_interpolation`; Serum `kParamPitchSource`, `kParamXfadeMode`, `kParamSlicingSingleSlice` (A398, A399) |
| Tests | `test_oscextra2.cpp` (6 cases), `test_ui_gaps.cpp`, `test_vital_import.cpp`, `test_serum_import.cpp`, counts in `test_new_keys` / `test_engine_caps` / `test_params` / `test_shell_clap` / `test_fxextra_keys` | |

### WP-06 Modulation sources and audio-rate taps

| Part | Files | One line |
|------|-------|----------|
| Sources | `modulation.cpp/.h` (57), `engine_caps.*` (bit 61), `readout.h` (64), `synth_lfo.h` (64 point inputs), `producers/source_tap.h`, `producers_module.*` (taps), `filters_module.*` (taps and the filter feed), `synth_voice_handler.cpp:365` (`createSourceTaps`), `voice_handler.*` (Alt, Rand 2, Rand (Discrete) x 64, Expression X), `vital_engine.cpp` (`vitalSourceName`, `sourceFor`) | 15 sources; the sound sources are the held last sample of the block, per voice |
| Warp modes | `synth_oscillator.h/.cpp` (`kSrc...`, `isSourceWarp`, `xWarpPhase`), `sample_oscillator.cpp`, `processor_router.*` (`connectAudioFeedback`), `param_table.cpp` (24 names), `params.h` (76), `engine_caps.*` (bit 62) | FM Thru-Zero / Exp / Linear, PD, AM, RM from Noise, Sub, Filter 1, Filter 2; the filters one block late through a per-voice-pair Feedback node |
| UI | `ModMatrixPanel.cpp:326`, `Sheet.cpp:293`, `model/GestureModel.cpp:318` | the sources the menus lacked (Stereo, Voice Control) and the new ones in groups; the warp groups |
| Importers | `serum_map.cpp` (sources, `kFM_NOISE .. kRM_FILT2`), `vital_import.cpp` (FM / RM from sample -> 52 / 72), both audit scripts | 12,991 matrix connections of the 656 presets (120 more) |
| Tests | `test_modsources.cpp` (9 cases), `test_ui_gaps.cpp`, `test_ui_model.cpp`, `test_serum_import.cpp` (+2 cases, 3 updated), `test_vital_import.cpp`, `test_warpenv.cpp` (the B21 range), `golden_v2.h` | each source moves a destination, 24 warp modes against None, a chord equals the sum of its notes, 0 allocations while wiring and unwiring the new sources |

### WP-08 Matrix RISE / FALL and control sheet extras

| Part | Files | One line |
|------|-------|----------|
| Content | `content_state.h/.cpp`, `state.cpp` (`rise`, `fall`, validation) | `ContentModCurve::riseMs / fallMs`, 0 .. 5000 ms, written only when set, a record may have no points |
| Engine | `modulation_connection_processor.*` (`slewStep`), `vital_engine.cpp` (snapshot, `updateCurve`) | a one pole slew in front of the curve, per voice, also on audio rate sources; 90 % in the set time |
| UI | `ModCurvePanel.cpp:320`, `ModMatrixPanel.cpp:155`, `Sheet.cpp:41`, `Editor.cpp:46`, `Panel.h`, `UiContext.h`, `RemapEditor.cpp` | RISE / FALL sliders, the star, tap a row of the sheet: Aux Source submenu and Edit Custom Curve |
| Importer | `serum_fx.cpp`, `audit-serum-import.py` (`audit_mod_slew`) | `kParamSmoothRise / Fall / Link` -> the slot record (A408) |
| Tests | `test_modslew.cpp` (6 cases), `test_state_v3.cpp`, `test_ui_wtremap.cpp`, `test_serum_import.cpp` | step response 101 ms for 100 ms, 400 / 400, fall 304 / 300; defaults bit identical; 0 allocations |

### WP-18 Importer follow-ups

`vital_import.cpp` (`detune_range`, `midi_track`, `voice_priority`, `sample_transpose_quantize` accounting, Saw Down), `builtin_ref.cpp`, `vital_tables.*`, `vital_engine.cpp`, `engine_caps.*` (bit 63), `params.h`, `param_table.cpp`, `golden_v2.h`; `scripts/audit-vital-import.py` (the noise sample model, the new rows); tests `vi_detune_range_midi_track_and_voice_priority_are_imported`, `vi_single_cycle_shortcut_needs_the_same_waveform`, `saw_down_is_the_inverted_half_cycle_shifted_saw_...`. G8 is decided (not applied), the A/B listening is the owner's.

## 2. Verification

* Build: incremental Release build after each package, **zero compiler warnings** (the linker's "ignoring duplicate libraries" notes are older; two `-Wshadow` / `-Wunsequenced` warnings that appeared while writing were fixed). Build directory `build/` inside the worktree with `FETCHCONTENT_SOURCE_DIR_*` pointing at the sources of the main checkout.
* Per package the affected suites (keys, caps, params, state, shell_clap, vital_*, serum_import, ui_*); the first runs found the stale expectations of the golden list (`golden_v2.h`: the warp lists and the wavetable list grow), the key / source / destination counts and the B21 mode range of `test_warpenv.cpp`; all fixed.
* Vital import audit (`audit-vital-import.py`, 25 bank presets unzipped + 10 loose = 35): **18,249 value rows, 0 mismatches, 0 unclassified, errors 0** (the script crashed on the noise sample model of WP-04 before: fixed; wavetables stored 105 -> 71).
* Serum import audit (`audit-serum-import.py --selftest`, 656 files): **0 errors**, 10 mutations found (two new), 26 smooth flags, 2 ratio sources, 39 slew records, 12,991 sources and destinations; dropped leaves 886,278 -> 884,965; unaccounted 0.
* `scripts/gen-factory-presets.py --check` and `scripts/check-params-doc.sh` pass; `ui-screenshots/` restored after the render tests.
* Full `ctest -j8` at the end (Release, plugin on): **100% tests passed out of 75** (118 s). The first full run had one failure, a stale expectation (`state_nan_inf_and_huge_numbers`: a wavetable value above the list clamps to the last choice, now "Saw Down"); fixed, and the FULL suite was run again (`synthyboy_test_threads` passed). New suites: `synthyboy_test_vital_oscextra2` (6 cases), `synthyboy_test_vital_modsources` (9), `synthyboy_test_vital_modslew` (6).

## 3. Assumptions and questions

A397 .. A411 in `13-new-keys.md` 4.33 .. 4.36; questions 77 .. 83 in `14-morning-questions.md`. The ones that need the owner's ears or Serum 2 open: the Serum enumeration of the ratio source and the crossfade flag read from the files (77), Ratio and smooth defaults (78), the readings of the new sources (79), the one block of the filter warps and the Vital FM from sample (80), the RISE / FALL law and whether 128 keys are wanted (81), the stored mod wheel (82), the A/B with real Vital (83).

## 4. Not done / limits

* The A/B listening of the renderer against real Vital and the question whether Vital applies a stored mod wheel need Vital (82, 83).
* The three Vital `Sine` tables that are a phase shifted sine stay stored (a shift is not a built-in table).
* RISE / FALL is content, not keys (81); the modulation of another slot's smooth / delay and the drawn aux curve of Serum remain (backlog S8).
* The Serum Ratio source "Noise" (id 3) has no home (the Noise is no source of the oscillator pitch here), reported.
* The native popup menu of a modulation row of the sheet cannot be driven by a headless test; the two actions it triggers are tested through the model calls.
* Expression X is the per note bend; the CLAP / MPE X axis of WP-09 (pan, CC 10) is a separate quantity that feeds the macros only.
