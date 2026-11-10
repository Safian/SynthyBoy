# REMAINDER - the code remainders of the backlog

Branch `work-remainder` (from `develop` d5cf633). Spec: `13-new-keys.md` 4.41 .. 4.43 with assumptions A449 .. A459; morning questions 99 .. 106; gesture audit rows 06-60, 02-45, 05-36. Keys 2299 -> **2628** (block B31 320 keys, positions 2299..2618; block B32 9 keys, 2619..2627), modulation destinations 987 -> **1246**, capability bits of the second word 4 .. 6 (`kCap2ModSlew`, `kCap2ModDelay`, `kCap2SpectralFilterTable`), Kick pages 155 -> **159**, `kMaxParams` 4096 untouched, state version unchanged (the old `rise` / `fall` fields are migrated on load). `gen-factory-presets.py --check`: up to date (112 presets, the 329 new keys at their neutral defaults, the renders unchanged).

## 1. What was built

| Item | Where | One line |
|------|-------|----------|
| 1 RISE / FALL / DELAY keys | `param_table.cpp` (`SB_SLOT_SLEW`), `modulation_connection_processor.cpp/.h`, `synth_voice_handler.cpp`, `synth_parameters.cpp`, `vital_bindings.cpp`, `vital_engine.cpp`, `state.cpp` (`migrateSlewRecords`), `serum_fx.cpp`, `kick_pages.cpp`, `ModCurvePanel.cpp`, `ModMatrixPanel.cpp`, `audit-serum-import.py` | per slot `modNN.rise / fall / delay / delay_beats / delay_sync`; the four time keys are modulatable (also by other slots); per voice slew coefficients once per block; DELAY is a hold-off after the note on (ms or beats); the content records are migrated into the keys |
| 2 Wavetable as the spectral filter | `param_table.cpp` (`SB_OSC_SPECFLT`), `vital_tables.cpp/.h` (`frameToFilterCurve`, `filterBank`), `sample_oscillator.cpp/.h` (`kSpecFltFrame`), `vital_engine.cpp`, `SpecFilterSource.h`, `OscPanel.cpp`, `kick_pages.cpp` | `oscN.spec.flt_use / flt_table / flt_frame`: the waveform of a frame is the gain curve (v + 1) over the log frequency axis, the position (modulatable) blends two frames, the drawn curve is the fallback; menu + picture + FRAME slider in the panel |
| 3 `synthyboy.content` in the JUCE-wrapped CLAP | `shell-clap/synthyboy_content_service.cpp/.h` (the moved functions over a `ContentHost`), `synthyboy_clap.cpp` (`NativeContentHost`), `shell-juce/ClapBridge.cpp`, `PluginProcessor.cpp/.h`, `third_party/patches/clap-juce-extensions-extension-hook.patch`, `CMakeLists.txt` (patch step on a copy in the build dir), `test_clap_content.cpp`, `THIRD_PARTY.md` | the wrapper gets a hook (`clapPluginExtension`), the processor of the CLAP build answers `synthyboy.content` with the same functions as the native shell; ctest `synthyboy_test_clap_content_juce` runs the two existing tests against `SynthyBoy.clap` |
| 4 S4 PZ SVF | `serum_voice.cpp` (`pzFilter`), `serum_fx.cpp` (FX filter, `runPzFile`), `serum_api.cpp`, `serum_plugin_import.cpp`, `ContentIO.cpp` | poles and zeros of the presets' PZ SVF filters and of the 31 `.XferFilterPZ*` files become `content.filter_curves` (4 sets blended by X \| Y, 129 points reduced to 16) |
| 4 S13 | `content_state.h`, `state.cpp`, `vital_point_mod.h`, `vital_engine.cpp`, `ModModel.*`, `LfoShapeEditor.cpp`, `serum_fx.cpp`, `serum_voice.cpp` | the curve target of the LFO point modulation (`mod_curve`, the power of the segment after the point moves by amount x 10, menu "Modulate Curve"); the LFO swing flag takes the global swing |
| 4 S12 | `serum_voice.cpp`, `multisample.cpp/.h`, `sample_oscillator.cpp/.h`, `synth_voice_handler.cpp` | `trimHead` / `trimTail` fold into START / END; SFZ `trigger=release`, `xfin_*` / `xfout_*`, `fil_*` in the multisample engine |
| Tests | `test_modslew.cpp` (10 cases), `test_spectral_filter_table.cpp` (3, new), `test_multisample_osc.cpp` (+2), `test_multisample.cpp` (+1), `test_wp123.cpp` (+1), `test_state_v3.cpp` (+1), `test_serum_import.cpp` (+2, 1 renamed and extended), `test_ui_wtremap.cpp`, `test_ui_gaps.cpp` (+1), `test_clap_content.cpp` (runs against both modules), counts in `test_new_keys` / `test_params` / `test_engine_caps` / `test_fxextra_keys` / `test_shell_clap` | each cap bit has an off / on render proof |

## 2. Measured

* RISE / FALL by keys: the step responses of GROUPB are unchanged (100 ms -> 101.3 ms, 400 ms -> 400 ms, fall 300 ms -> 304 ms); defaults bit identical; modulation of `mod01.rise` by another slot (macro 1, 10 %): 90 % time 501 ms against 5 ms; 0 allocations on the audio thread; DELAY 200 ms: the connection is silent for 37 blocks of 256 and acts afterwards, one beat at 120 BPM = 94 blocks.
* Spectral filter from a wavetable: Saw / Square differ from no filter and from each other; frame 0 of a two frame table (gain 0) leaves under 20 % of the level, frame 100 % (a hole at 632 Hz) 3 x more than frame 0; the macro moves the frame; the switch off is bit identical to no keys.
* JUCE-wrapped CLAP: the two tests of the native shell pass against `SynthyBoy.clap` (switch to wavetable worst harmonic difference 0.003 dB, as native).
* Serum audit (`audit-serum-import.py --selftest`, 656 files): `audited 656 files ... 0 errors`; the new checks: 39 slew slots, 6 slot delays, 7 PZ curves (within 6 dB of the unreduced response), 4 swing flags, 48 point curve modulations, 2 sample trims; the mutations of `mod12.rise` and `mod08.delay_beats` are found. Import: `unaccounted=0`, dropped leaves 884,965 -> 878,235.
* Vital audit (35 presets): 18,249 value rows, 0 mismatches, `ERRORS: 0`.

## 3. Verification

* Build: incremental after each item; zero compiler warnings (only the linker's "ignoring duplicate libraries"). Two `-Wfloat-equal` / `-Wmissing-prototypes` warnings that appeared while writing were fixed.
* ASan + UBSan (`build-asan`, RelWithDebInfo, `-fsanitize=address,undefined -fno-sanitize-recover=all`): `synthyboy_test_vital_modslew`, `_spectral_filter_table`, `_multisample`, `_wp123`, `_spectral`, `_modsources`, `synthyboy_test_multisample`, `synthyboy_test_state_v3` all clean (0 failed cases).
* Full `ctest -j8` (once, at the end, Release, plugin on): `100% tests passed, 0 tests failed out of 87` (221 s; 85 of GROUPD plus `synthyboy_test_vital_spectral_filter_table` and `synthyboy_test_clap_content_juce`). The first partial runs found the stale expectations (key / destination / Kick page counts, `requiredCaps` of the keys of the second word, the slider test of the matrix panel, the content round trip with the old slew fields) and a race of my own (the juce and the native variant of the clap test shared one temp directory: now per process).
* `gen-factory-presets.py --check`, `check-params-doc.sh`, `check-manifest.py`, `docs/kick-pages.json` against `synthyboy_paramdump --kick-pages`: up to date. `ui-screenshots/` restored after the render tests.

## 4. Findings

* The sample oscillators read the key release from `kNotePressed`, which carried the number of keys held at the note on and never went down: the loop exit on release and everything that waited for the release did nothing. `kNotePressed` now carries the voice events (`voice_event ()`); the loop exit and the release zones work.
* The JUCE-wrapped CLAP build reports every parameter as 0 .. 1; a host sends normalised values there (question 106).
* The importer's former drop of `kParamSmoothLink` without a time is now ignored with a reason.

## 5. Not built / exact remainder

* S5 path LFOs: not built, the manual names the type and nothing else (question 103 holds a proposal); backlog row S5 stays.
* S13: random LFOs beyond the fourth (2 files; the model has `rlfo1..4`); the global swing itself (S11, 29 files).
* S12: `gain`, the BPM scan range / rate / divide, the base tempo and the loop start link (unknown enumerations, no key); SFZ: no `fileg`, the 1 pole filter types use the 2 pole shape, release zones sound only while ENV 1 keeps the voice alive.
* Wavetable filter: the picture is exact only for the Preset table and Sine / Saw / Square / Triangle; no preset of the 656 uses it, so there is no importer mapping (question 101).
* PZ SVF: a static response curve, not a pole / zero filter (the X \| Y morph and the phase are lost, question 102).
* S8: the drawn aux curve and the output curve display. S1 clip player and S2 arpeggiator were not part of this task and stay rows.
* iOS / Android share sheet and DAW drag-out of GROUPD (question 97) are still untested on devices.
