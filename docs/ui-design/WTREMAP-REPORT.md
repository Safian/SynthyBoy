# WTREMAP report - wavetable remap with RISE / FALL (G69), per-module FX LEVEL (G72), Clean Mode (G54) (2026-11-08)

Branch `worktree-agent-af43bfe1de9b4541c`, based on develop `629805b` (the worktree started on main `b5b6826` and was fast-forwarded to develop first). Spec: `13-new-keys.md` 4.14 and 4.15 (block B16), assumptions A120-A127.

## Result

23 new keys at table positions 1484..1506 (table 1507 keys), state version unchanged (3), 8 modulatable keys (destination choices 404..411, `kNumModDestinations` 412).

| Part | Keys | Engine | UI | Kick |
|------|------|--------|----|------|
| Wavetable remap (G69) | `oscN.remap.on / slot / rise / fall` (12) + `content.remaps[]` | `FrameRemap` in the `Wavetable`, applied in `SynthOscillator::remapFrame ()` | panel `Oscillator N Remap` with the graph `RemapEditor` | pages `osc1_remap`, `osc2_remap`, `osc3_remap` |
| FX LEVEL (G72) | `chorus / compressor / delay / distortion / eq / flanger / phaser / reverb .level` (8) | `ReorderableEffectChain::runRack ()` | knob behind More of each FX panel | page `fx_levels` |
| Clean Mode (G54) | `filter1 / filter2 / fxfilter .clean_mode` (3) | `CleanDriveGate` + `FilterModule::process ()` | switch behind More of the filter panels | on `filter1_extra`, `filter2_extra`, `fx_filter_extra` |

Capability bits `kCapWtRemap` (26), `kCapFxLevel` (27), `kCapCleanMode` (28), all three set in `VitalEngine::capabilities ()` because `core/engine-vital/tests/test_wtremap.cpp` renders each off and on and measures a difference (numbers below). engine-own untouched (reports none of the three).

## What was built (file: purpose)

* `core/src/param_table.cpp:SB_REMAP / SB_FXLEVEL` and the B16 rows at the table end; `core/include/synthyboy/modulation.h` (`kNumModDestinations` 412); `core/include/synthyboy/engine_caps.h`, `core/src/engine_caps.cpp` (3 bits, classification before the module prefixes).
* `core/include/synthyboy/content_state.h`, `core/src/content_state.cpp`, `core/src/state.cpp`: `ContentRemap {osc, slot, name, points}`, `content.remaps[]` (<= 12 entries, <= 64 points, validation, writer, parser).
* `core/engine-vital/upstream/...` (listed in `docs/UPSTREAM.md`): `lookups/wavetable.h` (`FrameRemap`), `producers/synth_oscillator.h/.cpp` (`remapFrame`), `modules/reorderable_effect_chain.h/.cpp` (level), `modules/filter_module.h/.cpp`, `modules/serum_filter_bank.h/.cpp` (`CleanDriveGate`), `common/synth_parameters.cpp` (the new controls).
* `core/engine-vital/adapter/vital_engine.cpp` (`applyRemaps`, snapshot `remap` tables baked with `core/shapes`, caps), `vital_bindings.cpp` (level and clean mode bindings, remap keys special).
* `core/src/kick_pages.cpp`, `kick_pages.h` (107 pages), `docs/kick-pages.json`, `docs/params.json`, `docs/PARAMETERS.md` regenerated; 112 factory presets regenerated with `scripts/gen-factory-presets.py` (they list every key explicitly, so the new keys appear with their neutral defaults; `--check` passes).
* UI: `shell-juce/ui/panels/RemapEditor.h/.cpp` (graph), `OscRemapPanel.cpp` (panel), `Fx*Panel.cpp` (eight `.level` entries), `FxFilterPanel.cpp`, `FilterPanels.cpp` (`clean_mode`), `CMakeLists.txt` (test target), `ui-screenshots/wtremap-osc-remap-*.png`.
* Docs: `13-new-keys.md` (4.14, 4.15, 5, 7, 9, 12), `02-oscillators.md`, `10-coverage-matrix.md`, `UI-GESTURE-AUDIT.md` row 02-14 (file:line), `UPSTREAM.md`, `14-morning-questions.md` rows 18, 19.

## Proof of audibility (test_wtremap.cpp, vital engine, 48 kHz)

* Remap: a curve that maps everything to the last frame, frame knob at 0 %: relative RMS error to "frame 100 %, no remap" 0.0000, to "frame 0, no remap" 0.631 (the table "Sine to Saw" changes along the frame by 0.63). Remap off with the content present is bit-identical to no content (< 1e-6). An inverting curve at 25 % equals 75 % (< 0.02). Slot 1..4 pick their own curve, an empty slot is the identity.
* RISE / FALL: after a jump of the frame knob RISE 1500 ms differs from RISE 0 by > 0.2 (20-200 ms after the jump), FALL does not change an upward move (< 1e-3) and the reverse; RISE 100 ms settles on the target; a new note jumps to its frame.
* FX LEVEL: for all eight modules -12 dB gives -12.00 dB and +6 dB gives +6.00 dB of output RMS; a module that is off ignores the key; chorus level modulated by macro 1 drops 36 dB.
* Clean Mode: at 12 dB drive (Analog, resonance 40) on vs off differ by a relative RMS error of 2.07 for the voice filter and the FX filter; on equals the drive 0 render times 10^(12/20) (+-0.3 dB, peak error < 1 %); Comb has no drive: the switch changes nothing.

## Gates (run once at the end)

`cmake -S . -B build -G Ninja -DSYNTHYBOY_BUILD_PLUGIN=ON` (JUCE and clap-juce-extensions from the main checkout's `_deps`) then `cmake --build build -j16` (no compiler warning in our sources; only the linker's "duplicate libraries" note), `ctest --test-dir build -j6`: **46 / 46 passed** (including `synthyboy_test_vital_wtremap` 8 cases / 827 checks, `synthyboy_test_ui_wtremap` 5 cases / 120 checks, `synthyboy_test_new_keys`, `synthyboy_test_engine_caps`, `synthyboy_test_shell_clap`, `synthyboy_factory_presets_reproducible`, `synthyboy_test_rt`, `synthyboy_test_ui_panels` (every key reachable, 44 pt targets)). `build/clap_smoke ...SynthyBoy.clap`: all checks passed. `scripts/check-params-doc.sh`: up to date. No sanitizer run (no audio buffer layout changed: the remap reads the frame of the existing block, the level ramps the effect output in place).

## Decisions (assumptions of 13-new-keys.md 12)

A120 position domain and law; A121 slot / content change at the next block; A122 RISE / FALL one pole glide per voice, note on jumps; A123 unison frame spread after the remap; A124 FX LEVEL range -24..12 dB, modulatable, after the dry / wet; A125 Clean Mode definition; A126 cap bits 26..28; A127 positions / destination numbers of this branch.

## Open / for the lead

1. **Merge:** every parallel block appends to the table end and bumps `kCount`, `kNumModDestinations`, `kKickPageCount`, the cap bits (26..28 here), `docs/params.json`, `PARAMETERS.md`, `kick-pages.json` and the 112 presets; on conflict regenerate the generated files (`check-params-doc.sh --update`, `paramdump --kick-pages`, `gen-factory-presets.py`) and re-number B16 behind the other blocks (positions in `test_new_keys.cpp`, destinations 404..411 follow the table order).
2. G69 in the mod matrix (a drawn curve and RISE / FALL per `modNN` slot) is not built: the task defined G69 as the wavetable position remap; row 18 of `14-morning-questions.md`.
3. Clean Mode follows 04 section 1.1 ("-24 dB pre, +24 dB post"), not a quality / anti-alias toggle (our drive is 0..20 dB): row 19. The Serum manual page is not in the repository.
4. The remap curve is not shown on the 3D wavetable view (the graph only); the unison frame spread is not remapped (A123).
5. `kNumFxSlots`-style own-engine: engine-own implements none of B16 (unchanged policy, morning question 11).
