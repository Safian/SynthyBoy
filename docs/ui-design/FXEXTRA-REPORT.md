# FXEXTRA report: block B17, the compressor / delay / distortion / phaser / reverb extras (G73-G77)

Branch `worktree-agent-add5cc2768f21382a` (worktree of `develop`). Spec: `13-new-keys.md` 4.17 (assumptions A140-A158). Key block name **B17**.

## What was built

Every missing parameter of the five modules is a key, an engine feature in the Vital adapter, a Kick page entry, a UI control and an off/on render test.

| Item | Keys (positions 1484 .. 1497) | Engine | UI |
|------|------------------------------|--------|----|
| G73 compressor limit mode, crossovers | `compressor.limit`, `limit_latency_comp`, `crossover_low` (mod), `crossover_high` (mod) | `LookaheadLimiter` and `Compressor::processLimit` in `effects/compressor.cpp` (1.5 ms look-ahead, stereo linked, box-smoothed minimum of the needed gain, exact final clamp, estimated inter-sample peak); `LinkwitzRileyFilter::setCutoff`; latency of the chain via `CompressorModule::latency ()` / `serumModuleLatency` | "LIMIT" badge and draggable crossover tags on the band view (`FxCompressorPanel.cpp:151`), the four keys under More |
| G74 delay HQ, offset, Tap->Delay | `delay.hq`, `delay.offset` (mod) | `MemoryTemplate::getHighQuality` (8-point Lagrange read, `lookups/memory.h`), `Delay::readMemory`, right-channel period scalar (`effects/delay.cpp`) | TAP button (tap tempo, `TapTempo`), x1 / x4/3 / x3/2 snap buttons, echo view shows the offset (`FxDelayPanel.cpp:180-209`) |
| G75 distortion types, key track, X-Shaper | `distortion.filter_keytrack` (mod), `distortion.type` 6 .. 13 appended (Tube, Diode 1, Diode 2, Zero square, Asym, Rectify, X-Shaper, X-Shaper (Asym)), content `curves.shaper_a / shaper_b` | `effects/distortion.cpp` (new shapers, DC blocker for asymmetric ones, `processXShaper` with 513-point tables), `DistortionModule` key track input, tables baked from the content in `vital_engine.cpp` | the X-Shaper curve editor `panels/FxShaperEditor.cpp` (shown for the two X-Shaper types), key track under More |
| G76 phaser DEPTH 2 | `phaser.depth2` (mod) | right-channel depth offset in `effects/phaser.cpp` | knob under More, second notch curve in the view |
| G77 reverb algorithms and room modes | `reverb.type`, `room`, `width` (mod), `er_size`, `diff_a` (mod), `diff_b` (mod) | `effects/reverb_algorithms.{h,cpp}` (Plate after Dattorro, Hall, Vintage, Nitrous, Basin with room modes), integrated in `Reverb::processAlgorithm`; `width` is mid / side on the wet signal of every type | type / room chips and four knobs under More, dimmed where the algorithm does not use them |

The new keys are at the end of `kRaw[]` (positions 1484 .. 1497, ids = `hashKey`), the table has 1498 keys, 411 modulatable keys (destination choices 404 .. 411 for the 8 new ones, `kNumModDestinations` = 412). All defaults are neutral: the golden render test `version_2_sound_is_unchanged_in_both_engines` (bit exact on this platform), the 112 factory presets (regenerated: they hold every key explicitly, so only the 14 new keys at their defaults were added, `scripts/gen-factory-presets.py --check` passes) and the other render tests are unchanged. Five new capability bits (44 .. 48, `kCapFxExtrasAll`), all set by the Vital adapter because `core/engine-vital/tests/test_fxextra.cpp` renders every key off and on; the own engine reports none. Kick pages 104 .. 108 (`fx_comp_limit`, `fx_delay_3`, `fx_distortion_2`, `fx_phaser_3`, `fx_reverb_3`), `docs/kick-pages.json`, `docs/params.json`, `docs/PARAMETERS.md` regenerated.

## Files

* Spec and docs: `docs/ui-design/13-new-keys.md` (4.17, section 5 addition, section 7 totals, A140-A158), `14-morning-questions.md` (rows 18-20), `UI-GESTURE-AUDIT.md` (rows 08-10 .. 08-14), this file.
* Core: `core/src/param_table.cpp` (B17 block, appended `kDistortionTypes`, `kReverbTypes`, `kReverbRooms`), `core/include/synthyboy/{params,modulation,engine_caps,content_state,kick_pages}.h`, `core/src/{engine_caps,content_state,state,kick_pages}.cpp`.
* Engine (Vital adapter): `core/engine-vital/upstream/src/synthesis/effects/{compressor,delay,distortion,phaser,reverb}.{h,cpp}`, `reverb_algorithms.{h,cpp}` (new), `filters/linkwitz_riley_filter.h`, `lookups/memory.h`, `modules/{compressor,delay,distortion,phaser,reverb}_module.*`, `reorderable_effect_chain.cpp`, `serum_fx_modules.cpp`, `common/synth_parameters.cpp`, `interface/look_and_feel/synth_strings.h`, adapter `vital_bindings.cpp`, `vital_engine.cpp`. `engine-own` is untouched.
* UI: `shell-juce/ui/model/FxModel.{h,cpp}` (pure: tap tempo, offset snap, crossover scrub, curve edit operations and presets), `panels/FxShaperEditor.{h,cpp}` (new), `FxCompressorPanel.cpp`, `FxDelayPanel.cpp`, `FxDistortionPanel.cpp`, `FxPhaserPanel.cpp`, `FxReverbPanel.cpp`.
* Tests: `core/engine-vital/tests/test_fxextra.cpp` (new suite `synthyboy_test_vital_fxextra`), `core/tests/test_fxextra_keys.cpp` (new suite `synthyboy_test_fxextra_keys`), `shell-juce/ui/tests/test_fx.cpp` (8 new cases), count / golden updates in `test_new_keys.cpp`, `test_engine_caps.cpp`, `test_state_v3.cpp`, `test_shell_clap.cpp`, `golden_v2.h`.

## Tests

Full `ctest -j6`: 46 of 46 suites pass (Release, Apple clang arm64, no compiler warnings in our code); `gen-factory-presets.py --check` and `check-params-doc.sh` pass. No sanitizer run: no audio buffer handling outside the module-owned rings was touched (the limiter rings and the reverb buffers are allocated at `setSampleRate`, the allocation test `synthyboy_test_rt` passes).

* `synthyboy_test_vital_fxextra` (11 cases): capabilities; the limit never exceeds its ceiling (direct limiter test with noise, spikes and a loud sine, bound 1.00001x), the ceiling follows the upper threshold (12 dB = x3.98), the chain reports 72 samples of latency (0 with the compensation off), a signal below the ceiling passes unchanged, linked stereo; crossovers move the bands and the explicit defaults equal the old fixed ones; HQ and offset change exactly the right channel (left unchanged to 1e-3); all eight new distortion types differ from the soft clip, are finite, DC free; X-Shaper curves reshape the output (curve A at 0.25 gives 0.25x, Asym uses B for the negative half, removing the content restores the identity); depth 2 changes the right channel only; the five reverbs: tail present, level within 0.15 .. 8x of the Vital reverb, decays, stereo, all different from each other and from Vital, decay time scales the tail, the five rooms differ, the room is inert for other types, ER size / DIFF A / DIFF B / width act; width acts on the Vital reverb; 24 rounds of random settings with type changes stay finite.
* `synthyboy_test_fxextra_keys`: positions, ranges, defaults, destination numbers, choice names, caps, Kick pages reach the keys, X-Shaper curves round trip through the state (byte stable) and are validated.
* `synthyboy_test_fx` (UI): every key has a control on its panel (knob with mod ring if modulatable, 44 point targets, dimmed without the caps), the pure models, TAP and the snaps, crossover tag drag, the X-Shaper editor gestures and buttons against the host content, reverb dimming, depth 2, phone (390) and tablet (1024) screenshots of the five panels in `/tmp/ui-shots/fx-extras/`.
* Updated goldens: `distortion.type` counts 6 entries in the 880-key fingerprint (`golden_v2.h`), key and page counts.

## Assumptions and open questions (also in 13-new-keys.md 12 and 14-morning-questions.md)

* The Serum manual is not in the repository: every definition is ours (A140). In particular: Tap->Delay as a UI tap tempo (A147), DEPTH 2 as a right-channel depth offset (A151), DIFF A / B as input / loop diffusion (A155), the five reverb designs (A152, they are not copies of Serum's reverbs: level and character need a listening pass), the eight distortion curves (A148).
* The compressor crossovers of the engine are 120 Hz / 2500 Hz, not the 150 Hz / 3 kHz of design 08 (A143); the band labels now show the real values.
* `limit_latency_comp` On (default) reports 72 samples (48 kHz) as plugin latency while the limit is on; hosts that cannot change latency live will pick it up at the next transport start.
* Switching the reverb type, the room, or the limit mode clears the state of the module (stepped change, a short click is possible).
* Engine facts found while testing (not changed): two renders of the Vital engine after `reset ()` differ by about 1e-4 relative (free-running state); the Vital reverb and phaser LFO phases are not reset by `reset ()`.

## Merge notes for the lead

Parallel blocks append to the same places: `kRaw[]` end (positions here start at 1484: re-number the golden positions in `test_fxextra_keys.cpp` and the destination numbers 404-411 if another block lands first), `kNumModDestinations`, the counts in `test_new_keys.cpp` / `test_shell_clap.cpp` (1498 keys, 108 pages), `engine_caps.h` (bits 44-48 were chosen apart from the work package bits), `StateContent` (two members), `docs/params.json`, `PARAMETERS.md`, `docs/kick-pages.json` and the factory presets (all generated: regenerate after the merge with `scripts/check-params-doc.sh --update`, `synthyboy_paramdump --kick-pages` and `scripts/gen-factory-presets.py`).
