# GROUPA1 - item 0 (NaN, render gate), WP-04 Noise / Sub / sample as a source, WP-09 MPE completion, WP-10 FX remainder

Branch `work-groupA1` (from `develop` 81e8dda). Spec: `13-new-keys.md` 4.27 (item 0 and WP-04, block B23), 4.28 (WP-09, block B24), 4.29 (WP-10, block B25); assumptions A347 .. A368; morning questions 65 .. 70. Keys 2051 -> **2063** (positions 2051..2062), modulation destinations 904 -> 909, capability bits 52 .. 57 (six new), `kMaxParams` 4096 untouched, state version unchanged (the `content.samples[]` list may hold an entry with `osc` 4). `gen-factory-presets.py --check` passes (the 112 presets were regenerated with the 12 new keys at their neutral defaults; the renders are unchanged).

## 1. What was built

### Item 0 - NaN in the effect chain and the render gate failures

| Part | Where | One line |
|------|-------|----------|
| Root cause | `core/engine-vital/upstream/src/synthesis/framework/futils.h` (`exp2`) | the loud signal behind the compressor makeup +13.5 dB and the two EQs reached a Diode 1 distortion at +12.7 dB; `exp(-x)` with x > 126 built the float exponent field from an integer out of range (`pow2ToFloat`) and returned NaN. The exponent is clamped to -126 .. 126; the working range is untouched |
| Guard | `reorderable_effect_chain.cpp` (`scrubNonFinite`, `runRack`, `FxInstanceModule::run`) | a module whose block is not finite is replaced by silence and reset: NaN cannot latch into a bus |
| Serum Limit | `compressor.cpp` / `.h` (limit mode: the makeup gain drives into the limiter, `LookaheadLimiter` gets a `pre_gain`), `serum_fx.cpp` (ratio >= 100,000 -> `compressor.limit`), `scripts/audit-serum-import.py` | 128 compressors of 112 presets are in Limit mode (A347) |
| Tests | `test_fxinstances.cpp` `fxi_loud_compressor_eq_diode_chain_stays_finite` (the slot values of the preset typed in; **fails without the fix**: all four loudness combinations NaN), `test_fxextra.cpp` `robust_exp2_...`, `robust_extreme_legal_values_never_produce_nan` (14 distortion types x both ends of the drive behind the loudest compressor / EQ), `compressor_limit_mode_makeup_gain_drives_into_the_limiter`; `test_serum_import.cpp` `serum_compressor_ratio_at_its_maximum_is_the_limit_mode` | |

The render gate (`synthyboy_preset_render`, medium, 656 imported presets, the packs linked as directories): before 61 failures (39 peaks, 19 tails, 4 silent, 1 NaN), after **57** (33 peaks, 20 tails, 4 quiet, 0 NaN). Decisions per cause (details in 13-new-keys 4.27): the NaN was a DSP fault (fixed); the Limit-mode compressors were an importer mapping bug (fixed, 4 presets leave the peak list); the remaining peaks, tails and quiet presets are patch properties - hot level law (question 54), feedback delays (11 of the 19 tails), self-oscillating FX filters (4), long reverbs, a level opened only by modulation - documented, not fixed (question 69).

### WP-04 Noise, Sub, sample as a source (B23)

Six keys: `sub.phase`, `sub.phase_contiguous`, `noise.stereo`, `noise.filter`, `noise.type` (Noise / Sample), and `noise.bounce` (the Vital loop mode `sample_bounce`; the backlog said five keys, the loop modes of the task text needed one more). Capability bits `kCapSubPhase`, `kCapNoiseColour`, `kCapNoiseSample`.

| Part | Files | One line |
|------|-------|----------|
| Engine | `sub_source.cpp` (start phase, contiguous), `sample_source.cpp` / `.h` (`colourStage`: STEREO and FILTER of the colours; `makeUserData` / `useUserData`; a second independent channel of the four colours, no random generator is constructed so the seed counter and every earlier render stay), `sample_module.cpp`, `sub_module.cpp`, `synth_parameters.cpp`, `vital_engine.cpp` (`noise.type`, the user sample in the snapshot: decoded on the message thread, a pointer swap on the audio thread), `vital_bindings.cpp` | the noise source plays a generator or a content sample (`content.samples[]` osc 4) with loop / one shot / ping-pong, pitch, fine, key track, phase |
| Keys, caps, state | `param_table.cpp` (B23), `engine_caps.*`, `modulation.h`, `content_state.*` (`kNoiseSampleOsc`, `kMaxContentSamples` 4), `shell_features.cpp` (the "noise" lock keeps the entry) | |
| Kick | `kick_pages.cpp`: the more sheets of `sub_main` (3 -> 5) and `noise` (7 -> 11); `docs/kick-pages.json` regenerated; the page count stays 135 | |
| UI | `NoisePanel.*` (NOISE menu, Load... / Remove, status), `OscWaveView.cpp` (`OscNoiseView`: the real waveform and the file name), `SubPanel.*` (start phase, start dot, CONT), `ContentIO.cpp` (`loadSampleFile` for osc 4, "Noise sample" in the Missing Files dialog), `PluginProcessor.cpp` / `UiHost.h` (`reloadContent`: a relinked missing file now reaches the engine - found by the test) | S07-06, the facade WP-01 left open, is closed |
| Importers | `vital_import.cpp` (a custom sample is the noise source; loop, bounce, key track, random phase, transpose, tune, their modulation), `serum_voice.cpp` / `serum_content.cpp` / `serum_map.cpp` (embedded noise data or the Noises file, `kParamColor` = filter, sub phase), `audio_decode.cpp` (AIFF / AIFF-C, 32 bit float), audit script (new check `audit_noise`, sub phase) | S7 closed |
| Tests | `test_noisesample.cpp` (8 cases: caps, defaults are the stock mono noise bit for bit, STEREO correlation per colour and per knob position, FILTER low / high pass and off bit exact, the sample at its pitch and one octave up, key track, STEREO / FILTER inert on a sample, fallback, restore, one shot / loop / ping-pong, sub phase 0 / 25 / 50 %, contiguous), `test_vital_import.cpp` `vi_fixture_sample_imported_to_the_noise_source`, `test_serum_import.cpp` `serum_noise_source_takes_the_sample_file_the_embedded_data_or_the_colour`, `test_serum_container.cpp` `aiff_pcm_and_sowt_decode`, `test_ui_content.cpp` (load, play, state round trip into a second instance, locate, step, remove), `test_ui_gaps.cpp` (phone and tablet, modulation by touch) | |

Numbers: the Vital audit set (35 presets) now keeps all 7 samples (before 5 presets lost theirs); the 656 Serum presets store 552 more files (2662 in total), 1028 audited audio samples (476), the dropped leaves fall from 966,999 to 886,903.

### WP-09 MPE completion (B24)

Four keys: `master.mpe.xyz_macro` (Off / XYZ -> Macro 1, 2, 3 / YZ -> Macro 1, 2), `master.mpe.y_modwheel`, `master.mpe.bend_to_x`, `master.mpe.y_bipolar`; capability bit `kCapMpeRouting`. A new note expression `Event::Pan` (MIDI CC 10, CLAP `PAN`) is the X axis. The engine (`vital_engine.cpp` `mpeAxis`) writes the macro controls from the axes, also for the host's note expressions with MPE off (manual p.325), gives the macros back to their knobs when the mapping ends, and sets the mod wheel of the note's channel from Y. The quick-map buttons of `GlbMpePanel` (they only renamed themselves) now set the keys. Tests `test_mpe.cpp` (5 cases: axes per mode, MPE on and off, switching off restores the knob, audible macro and mod wheel routes, bipolar Y table, pitch bend also to X and the per-note bend still bends the pitch), `test_events.cpp` (CC 10), `test_ui_gaps.cpp` `uig_wp09_...`, `test_serum_import.cpp` (`mpeConfig`).

### WP-10 FX remainder (B25)

`delay.offset_left` and `reverb.feedback` (bits `kCapFxDelayOffsetLeft`, `kCapReverbFeedback`), the pool modules take them through the descriptors, the Kick more sheets of `fx_delay_3` and `fx_reverb_3`, the delay view and the reverb dimming. Rack operations cut / copy / paste / clear a bus (S10-06): `core/fx_rack` (`FxRackClip`, 3 functions), `fx_ops`, the "Bus ..." button of `FxRackView` with a menu per bus; rack presets (S10-02) were closed by WP-01 and are untouched. Serum import: the offsets of both channels (151 modules had their right offset dropped) and the FEEDBACK of Space / Abyss. Tests: `test_fxextra.cpp` (offset left: left channel moves, right untouched, echo position; feedback: low / neutral / high tail, 50 % is the old sound, inert for the other types), `test_fxrack.cpp` (3 cases), `test_fxinstances_ui.cpp` `fxui_wp10_bus_operations_...` (phone and tablet), `test_fx.cpp` (2), `test_serum_import.cpp` `serum_delay_offsets_and_reverb_feedback_have_keys`.

## 2. Verification

* Build: incremental Release build, **zero compiler warnings** (two `-Wdangling-gsl` warnings of the first version of `fxCopyRack` / `fxClearRack` were fixed; the linker's "ignoring duplicate libraries" notes are older).
* `ctest -j8` (68 tests, once at the end, 288 s): 66 passed; the two failures were stale expectations of the parameter counts of the delay (13 -> 14) and the reverb (19 -> 20) in `fxi_descriptor_maps_every_type_to_its_own_keys` and `fxi_every_parameter_of_every_type_is_mapped_to_the_right_control`; corrected, both suites re-run alone: 0 failed (`synthyboy_test_fxinstance_keys` 6 cases, `synthyboy_test_vital_fxinstances` 13 cases / 3573 checks). `synthyboy_test_threads` passed under load.
* New suites: `synthyboy_test_vital_noisesample` (8 cases, 712 checks), `synthyboy_test_vital_mpe` (5 cases, 143 checks); extended: `fxextra` (17 cases, 392,647 checks), `fxinstances`, `fxrack` (12), `serum_import` (20 cases / 1183 checks, the real run: 656 presets, 0 failed, 0 unaccounted), `serum_container` (10), `vital_import` (37), `ui_content` (9), `ui_gaps` (48), `ui_fxinst` (13), `ui_fx`, keys / caps / events / params / shell_clap.
* Serum import audit (`python3 -I scripts/audit-serum-import.py --serum ... --imported ... --selftest`): 656 files, 222,925 report entries, 17,444 effect values, 12,870 matrix sources and destinations, 656 noise sources, 1028 audio samples, **0 errors**, all 7 mutations found.
* Vital import: the 35 real presets import with 7 samples and 0 dropped (`synthyboy_vital_import`); the render of `PAD - Time Spiral` plays the noise sample.
* Render gate on the 656 imported presets (`synthyboy_preset_render --tier medium`): 61 failures before, **57** after (33 peaks, 20 tails, 4 quiet, 0 NaN).
* `scripts/gen-factory-presets.py --check`: 112 presets up to date; `scripts/check-params-doc.sh`: `docs/params.json`, `PARAMETERS.md`, `kick-pages.json` regenerated with the repo tools; `ui-screenshots/` restored after the render tests.

## 3. Assumptions and questions

A347 .. A368 in 13-new-keys.md 4.27 - 4.29; questions 65 .. 70 in 14-morning-questions.md. The ones that need the owner's ears: the Limit-mode makeup before the limiter (65), the Serum colour noise FILTER law (66), the MPE axis definitions (67), the reverb FEEDBACK law (68), the loosened gate for imported presets (69).

## 4. Not done / limits

* The Vital warp "from sample" (`sample_input`) has no home until WP-06 (the oscillator FM / PD / AM sources Noise, Sub, Filters).
* The `.clap` shell converts the CLAP `PAN` expression (one line) - no dedicated CLAP test; the engine path is tested with the event.
* The Serum `mpeConfig` encoding is unknown (all 656 files hold 0): nothing is decoded for a non-zero value.
* The MPE pad of `GlbMpePanel` is still a visual of its own (it does not send events); the mappings are driven by real expression events.
* The rack clipboard lives in the rack view (one per editor), it does not survive the editor closing.
