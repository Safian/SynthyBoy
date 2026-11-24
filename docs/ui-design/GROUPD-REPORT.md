# GROUPD - WP-14 Switch to wavetable, WP-15 offline render, WP-16 wavetable frame <-> LFO shape

Branch `work-groupD` (from `develop` 9c717ae). Spec: `13-new-keys.md` 4.40 with assumptions A435 .. A448; morning questions 92 .. 98. **0 keys** (2299, `kMaxParams` 4096), **0 capability bits**, state unchanged, `gen-factory-presets.py --check`: up to date (112 presets).

## 1. What was built

| Part | Where |
|------|-------|
| Offline render entry `IEngine::renderOffline` + request / steps / audio structs | `core/include/synthyboy/offline_render.h`, `core/include/synthyboy/engine.h` |
| Driver (prepare at the high tier, `Quality::realtime = false`, ultra = 4x, values / overrides / steps with reset / tail rule of preset-render), twin fallback for engines without a render of their own (engine-own, iOS) | `core/src/offline_render.cpp:44` (`runOfflineRequest`), `:154` (`renderOfflineOnTwin`), `:170` (`renderOfflineAny`) |
| engine-vital: a private `VitalEngine` with a copy of the parameters, content, store and tuning source; the live engine is only read | `core/engine-vital/adapter/vital_engine.cpp:796` |
| Conversions (JUCE-free, both shells): sample region -> wavetable, the nine import options, Resample, Render OSC Warp (level calibration), last note, WAV, frame <-> LFO shape, built-in LFO shapes (moved from the UI) | `core/include/synthyboy/wave_convert.h`, `core/src/wave_convert.cpp` (`:111`, `:141`, `:206`, `:408`, `:464`, `:586`) |
| Importer fix: first NSDF peak within 90 % + fractional period for Pitch Average | `core/wavetable-edit/src/audio_import.cpp:43` (`detectPitchPeriod`), `:197` (PitchAverage) |
| JUCE shell: `UiHost::renderOffline / lastNote / tempoBpm / shareFile`, processor (values of the tree, last note atomics, `ContentSharer` on mobile, reveal on desktop) | `shell-juce/ui/UiHost.h`, `PluginProcessor.cpp:471 / :492 / :508` |
| Flows: `switchToWavetable`, `renderOscWarp`, `resampleToWavetable`, `renderLastNote / saveLastNoteAs / writeLastNoteTemp`, `copyWavetableFrameToLfo`, `copyLfoShapeToWavetable`, `offlineRunner` (applies the ultra preference) | `shell-juce/ui/ContentIO.cpp:818 .. :1040` |
| UI: Switch to Wavetable submenu (Sample / Granular / Spectral), "..." wave menu (Render OSC Warp, Resample, frame <-> LFO), LFO Presets entries, Alt-drag of an LFO tab, Rendering menu, "Last note as audio" sheet (waveform, Save as WAV, Share, Drag out chip), message sheet | `OscWaveView.cpp:541 / :579 / :624`, `LfoShapeEditor.cpp:242 / :490`, `LfoTabsPanel.cpp:50 / :177`, `Editor.cpp:371 .. :433`, `ContentSheets.cpp` (`LastNoteSheet`, `MessageSheet`) |
| CLAP / mobile: `synthyboy.content` gets `switch_to_wavetable`, `render_osc_warp`, `resample`, `render_last_note`, `wavetable_frame_to_lfo`, `lfo_shape_to_wavetable` (last note and tempo tracked in `process`) | `shell-clap/synthyboy_content_ext.h`, `synthyboy_clap.cpp:976 ..` |

## 2. Measured (stated tolerances)

| Round trip | Result | Test |
|------------|--------|------|
| sample (220 Hz, 1/h partials) -> table (Pitch Average) -> played at A3, harmonics 1..12 within 40 dB of the loudest | worst difference **0.003 dB** (limit 0.5 dB); same through the JUCE shell for Sample, Granular and Spectral, and through the CLAP shell | `switch_to_wavetable_sample_to_table_is_audibly_the_source_region`, `groupd_switch_to_wavetable_...`, `clap_content_extension_switches_to_wavetable_...` |
| the START .. END region is what is converted | the saw half and the square half of one file give different tables (no even harmonics for the square, below -40 dB) | `groupd_switch_to_wavetable_...` |
| Resample of a filtered saw, harmonics 1..10 | **0.001 dB** (limit 1 dB); two renders bit identical, so are the two tables | `resample_captures_the_patch_as_a_wavetable_and_is_deterministic`, `groupd_render_osc_warp_bakes_...` |
| Render OSC Warp (Sync, 40 %) baked table played with WT POS 40 against the warped oscillator at three keys | worst harmonic difference **0.005 dB**, level difference -0.011 dB (limit 0.1 dB); extraction without a warp is exact (largest difference between frames 0, h2 / h1 = 0.5000 ... 1/6) | `render_osc_warp_bakes_256_frames_...`, `render_osc_warp_without_a_warp_...` |
| frame -> LFO -> frame | sine 0.0037, 3rd harmonic 0.0039 (frame units, bound 2 x 0.002 + 0.0001), saw / square / triangle exact (3 / 5 / 3 points); noise is capped at the point budget and reports the true error | `wavetable_frame_to_lfo_shape_and_back_stays_within_the_error_bound` |
| offline render does not disturb the live engine | the live output over 60 blocks is **bit identical** with a render between two blocks and with a render on another thread running meanwhile; also on the own engine | `offline_render_does_not_disturb_the_live_engine`, `offline_render_on_the_own_engine_...`, `groupd_last_note_as_audio_...` (through the processor) |
| offline = high tier, not the live tier | a low tier engine's offline render equals a high tier engine (rms 0.2070 = 0.2070), the low tier live render is 2.4 % lower | `offline_render_has_no_tier_caps_...` |
| steps are independent renders | the third step of a sequence equals a render of that step alone (difference 0) | same |
| ultra | 4x differs from 2x and is deterministic; the preference reaches Resample / last note through `offlineRunner` | `offline_render_is_deterministic_and_ultra_...`, `groupd_render_osc_warp_...` |

## 3. Verification

- Incremental builds without compiler warnings after each step (the linker's "ignoring duplicate libraries" only, as before).
- ASan + UBSan (`build-asan`, RelWithDebInfo, `-fsanitize=address,undefined -fno-sanitize-recover=all`): `synthyboy_test_wave_convert`, `synthyboy_test_vital_groupd`, `synthyboy_test_clap_content`, `synthyboy_test_wtedit`, `synthyboy_fuzz_smoke`, `synthyboy_test_user_content` all clean (0 failed cases).
- Full `ctest -j8` (once, at the end): `100% tests passed, 0 tests failed out of 85` (`Total Test time (real) = 280.19 sec`; 82 of GROUPC plus `synthyboy_test_wave_convert`, `synthyboy_test_vital_groupd`, `synthyboy_test_ui_groupd`; `synthyboy_test_threads` passed in the full run).
- `gen-factory-presets.py --check`: factory presets up to date (112 presets).

## 4. Findings

- The importer's `detectPitchPeriod` picked the highest NSDF peak: for a 220 Hz tone it returned the lag of 11 periods (2450 samples) and Pitch Average produced 9 frames instead of 110; it also truncated the period to an integer (0.2 % too short, a seam in every frame). Fixed at the source, so the editor's own audio import improves too (question 94).
- A Sync warp overshoots full scale by 2.6 dB; a 16-bit table file cannot hold it. The bake scales the table and raises the oscillator level (A442); at level 100 % nothing can make it up (the test uses 50 %).
- `juce::Button::triggerClick ()` is asynchronous in this JUCE version; the headless tests call `onClick ()`.

## 5. Not built / open (exact remainder)

- The iOS / Android share sheet (`juce::ContentSharer::shareFilesScoped`, `#if JUCE_IOS || JUCE_ANDROID`) and the DAW file drag-out of the desktop shell are written but could not be run here (the desktop shell reveals the file for Share; the drag is replaced by a handler in the tests): question 97.
- LFO shape -> wavetable on a built-in table makes a new one-frame table instead of converting the built-in into a user table (question 98).
- Resample keeps at most 256 cycles of the bar (question 95). Render OSC Warp needs a wavetable oscillator.
- The JUCE-wrapped CLAP build does not expose `synthyboy.content` (WP-01, question 50), so the new functions are in the native shell only.
- `tools/preset-render` keeps its own loop (same structure and tail rule as `runOfflineRequest`); it was not rewritten onto it.

## 6. Docs touched

`13-new-keys.md` (4.40, A435 .. A448), `14-morning-questions.md` (92 .. 98), `UI-GESTURE-AUDIT.md` (01-32, 01-33, 02-43, 02-44, 05-34, 05-35), `16-COVERAGE-FINAL.md` (S01-09, S04-17, S05-07, S06-07, S11-21, S13-16, S13-17, G03, G51 .. G53), `17-BACKLOG.md` (WP-14, WP-15, WP-16 DONE).
