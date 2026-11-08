# WARPENV report - the Serum 2 warp modes (G34-G37) and the ENV 1 buttons (G39) (2026-11-09)

Branch `work-warpenv`, cut from develop `4e803e6` (table 1596 keys, 520 destinations, cap bits to 43, 118 Kick pages). Spec: `13-new-keys.md` 4.22 (block **B21**, positions 1596..1602), assumptions A270 .. A284. Every behaviour decision was taken from the Serum 2 manual (p.49-56 warp, p.146 and p.149 ENV 1 button). The backlog (`17-BACKLOG.md`) planned this as B19 / A220; the numbers B21 / A270 are the ones the lead gave for this branch, and the planned B21 of WP-04 in the backlog is therefore taken: the lead renumbers at merge.

## Result

| Item | Engine | Keys / choices | Kick | UI | Test |
|------|--------|----------------|------|----|------|
| G34 Sync Var, Bend +, -, +/-, Asym +, -, +/-, Flip, Mirror, Remap 1-4, Odd/Even (entries 11-24) | `synth_oscillator.cpp` `xWarpPhase` / `xWarpValue` | 14 entries appended to the warp lists, cap 44 | `oscN_warp`, `oscN_warp2` (the type key reaches every entry) | grouped picker `warpTypeGroups`, Swap Warps, VAR label | `warpenv_every_mode_is_audible_in_both_slots`, `_neutral_positions_and_the_alt_modes`, `_remap_modes_use_the_curve_of_the_oscillator` |
| G35 Filter LPF / HPF (25, 26), 14 distortions (27-40) | same | 16 entries, caps 45, 46 | same | same | same |
| G36 FM Thru-Zero / Exp / Linear from X / Y (41-46) | same, integrated modulator in `WarpState` | 6 entries, cap 47 | same | same | `warpenv_fm_thru_zero_exp_and_linear_are_different_laws`, `_modulator_modes_follow_the_other_oscillator` |
| G37 PD from X / Y / Self (47-49), AM from X / Y (50, 51) | same | 5 entries, cap 48 | same | same | same |
| G33 Swap Warps | UI action | no key | - | last entry of the list of both slots | `uig_swap_warps_exchanges_the_two_warp_slots` |
| G39 ENV 1 button of the Main / Direct channels (sub, osc 1-3, noise, filter 1-2) | free outputs behind the amplitude envelope (`producers_module`, `filters_module`, `synth_voice_handler`), `VoiceKeepAlive` voice killer | 7 keys `*.env1` (default On), cap 49 | page `mix_env1` (119 pages) | toggles behind More of the Mixer panel, greyed unless the channel goes to Main / Direct | `warpenv_env1_button_of_every_channel`, `_env1_off_follows_the_longest_envelope_release`, `uig_env1_buttons_sit_on_the_mixer_and_grey_out_off_main_and_direct` |

Table 1603 keys (+7), `kNumModDestinations` unchanged (520: no modulatable key), `kMaxParams` 2048, Kick pages 119, capability bits 44 .. 49 (`kCapAllKnown` = 2^50 - 1), state version 3 unchanged. `legacyChoiceMax` (core/vital-import/vital_import.cpp) caps an imported Vital `oscN.warp.type` / `warp2.type` at 10; test `vi_warp_choices_stay_in_the_vital_range`. The 112 factory presets were regenerated (they list every key; `gen-factory-presets.py --check` passes), `docs/params.json`, `PARAMETERS.md` and `kick-pages.json` regenerated.

## What was built (file: purpose)

* `core/src/param_table.cpp`: `SB_WARP_APPENDED`, the 41 appended names in the three warp lists; the seven `env1` keys; `core/include/synthyboy/params.h` (`kNumLegacyWarpTypes` 11, `kNumWarpTypes` 52); `core/include/synthyboy/engine_caps.h` / `core/src/engine_caps.cpp` (bits 44-49, `warpTypeCaps`, `kCapWarpAll`); `core/src/kick_pages.cpp` + `kick_pages.h` (`mix_env1`).
* `core/engine-vital/upstream/src/synthesis/producers/synth_oscillator.h/.cpp` (listed in `docs/UPSTREAM.md`): 41 new `DistortionType` values, the per lane stage code in the anonymous namespace (`warpCycle`, `xWarpPhase`, `xWarpValue`, `xShape`), `WarpStage` / `WarpState` in the `VoiceBlock` and `warp_state_[2][kNumPolyPhase]` handled like `phases_` (compaction for a single voice, `storeWarpState`, reset at a note on). The Vital modes keep their templated path.
* `sample_oscillator.cpp`: AM, the distortions and the FM laws on a sample voice.
* `producers_module.*`, `filters_module.*`, `filter_module.*`, `synth_voice_handler.*`, `common/synth_parameters.cpp`: the ENV 1 controls, `kRawFree` / `kDirectFree` / `kMainFree` outputs, the final `Add` of the voice outputs and the `VoiceKeepAlive` voice killer.
* `core/engine-vital/adapter/vital_bindings.cpp` (warp map generated for 52 entries, the seven bindings), `vital_engine.cpp` (caps).
* UI: `shell-juce/ui/model/GestureModel.cpp` (`warpTypeGroups`: eight groups, no gaps; `warpedValue`: the pictures of modes 11-51), `ParamControls.h/.cpp` (`ParamChoice::setExtraAction`), `panels/OscPanel.cpp` (groups and Swap Warps for both slots, `Var` label), `panels/MixerPanel.cpp` (ENV 1 toggles and their dimming).
* Tests: `core/engine-vital/tests/test_warpenv.cpp` (12 cases), `shell-juce/ui/tests/test_ui_gaps.cpp` (3 cases + the changed warp list case), `test_ui_model.cpp`, `core/tests/test_new_keys.cpp` / `golden_v2.h` / `test_engine_caps.cpp` / `test_fxextra_keys.cpp` / `test_shell_clap.cpp` (counts, positions, the warp list size and the golden "version 2 sound" with the warp list of version 2), `core/vital-import/tests/test_vital_import.cpp`.
* Docs: `13-new-keys.md` 4.22 and sections 3, 4.9, 7, 9, 12; `14-morning-questions.md` rows 35-38; `16-COVERAGE-FINAL.md`, `17-BACKLOG.md`, `10-coverage-matrix.md`, `UI-GESTURE-AUDIT.md` (rows 02-12, 02-29), `UPSTREAM.md`.

## Proof of audibility (test_warpenv.cpp, vital engine, 48 kHz)

* Every one of the 41 modes in slot 1 and in slot 2 differs from warp None by a relative RMS error above 0.05 (the table "Sine to Saw" at 60 %, key 48), is finite and has a peak below 8.
* Neutral positions equal the original sound (< 0.02): Bend +/-, Asym +/- and Odd/Even at 50 %, Flip, Remap 1-4, Bend +/-, Asym +/- at 0 %. Odd/Even at 0 % keeps only the odd harmonics (second harmonic below 2 % of the first), at 100 % only the even ones. LPF at 10 % lowers the spectral centroid below 60 %, HPF at 80 % raises it above 130 %. Sync Var with VAR 0 equals Sync (< 0.02), VAR 100 % differs by more than 0.1.
* The drawn curve decides Remap 1, 2 and 4 (curve versus the empty slot: > 0.05).
* FM / PD / AM from osc 2 stop when osc 2 is switched off, osc 3 does not matter (< 1e-6), and the other way round; Thru-Zero, Exp and Linear differ pairwise by more than 0.1 (Thru-Zero against Linear 0.02: they differ only where the frequency would go negative).
* Slot 1 and slot 2 render every mode (12 .. 51) sample identically.
* Sample voice: AM, the 14 distortions and the FM laws change a looped sine sample (> 0.03), the filters, Odd/Even, Flip, Bend, Remap and PD are bit-identical to warp None.
* ENV 1: with a 400 ms attack the enveloped channel is at least 3 times quieter than the free channel between 60 and 140 ms for each of the seven channels; with the button off the voice ends with ENV 1 (silent 0.6 s after the note off), with an enabled ENV 2 of 1.5 s release it keeps sounding, with the button on the voice ends with ENV 1 whatever ENV 2 does. All buttons on is bit-identical to the default.
* Stress: all modes in both slots at amount 0 / 100 %, spread +-100 %, unison 16, keys 12 and 120, modulators at +-48 semitones: finite.

## Defects found

1. **Diode curves gave NaN (fixed).** `test_rt_alloc` (random walk of all parameters) and `test_threads` produced non finite output with the new `Dist Diode 1` at a high amount: `futils::exp` is an approximation that leaves the float range for arguments beyond about -87, and a loud unison sum times a 30 dB drive reaches it. `xShape` now limits `value x drive` to +-40. Both tests pass again. The same function is the reason the Vital distortion effect has a drive limit; nothing there changed.
2. **The second warp slot ramped up from zero over the first block of each note (fixed, A284).** `last_distortion2_values_` was not snapped at a note on (slot 1 is, in `reset ()`), so slot 2 started every note without its warp for up to 5 ms. It is found because `warpenv_slot_one_and_slot_two_render_the_same_mode_identically` compares the two slots sample by sample. Only patches with warp 2 on change.
3. **Slot 2 does not take part in the band limit of the wavetable read that slot 1 Sync / Formant gets** (pre-existing, documented, not changed): `Sync Var` in slot 2 therefore renders differently from slot 1 (the test leaves that one mode out, 4.22 / A275).
4. The golden helper `maxInV2` of `core/tests/golden_v2.h` did not know that the warp list grew; with the longer list the "busy" version 2 patch drew warp types the version 2 engine did not have and the golden audio hash broke. It now subtracts the 41 appended entries (no change of the golden values).

## Gates (run at the end)

`cmake --build build -j10` (no compiler warning in our sources; the linker's "ignoring duplicate libraries" note only); `ctest -j8` of the whole tree: see the last paragraph of the task answer (the final run after the last commit); `scripts/check-params-doc.sh` up to date; `gen-factory-presets.py --check` up to date; `paramdump --kick-pages` equals `docs/kick-pages.json`. `state_random_walk` (the fixed seed 2030 of `test_vital_audio.cpp`) needed no change. No sanitizer run: the audio buffers keep their layout (new outputs of the same size, the state is plain members).

## Open / for the lead

1. Merge: B21 appends 7 keys (1596..1602), bumps `kCount`, the Kick page count (119), the cap bits (44-49), `kCapAllKnown`, `params.json`, `PARAMETERS.md`, `kick-pages.json` and the 112 presets; regenerate those files after resolving. The planned B21 / A-numbers of `17-BACKLOG.md` WP-04 collide with this branch's names (B21, A270 .. A284).
2. Morning questions 35 (Sync Var as an entry and the phase knob as VAR fader instead of 6 `warp.var` keys), 36 (the laws and shapers are our own: listen against Serum 2), 37 (filter ENV 1 buttons, 7 keys instead of 5), 38 (the two old `FM from Osc` entries are phase modulation, the new FM entries are real FM).
3. Not built here (stay with WP-06 / G29): the modulator sources Noise, Sub, Filter 1, Filter 2 for FM / PD / AM / RM.
4. The warp picture of the oscillator view (`warpedValue`, `model/GestureModel.cpp`) draws all 51 modes now (a drawing of the idea of each, not the DSP; Remap 1, 2 and 4 follow the curve drawn for the oscillator, `OscWaveView.cpp` `paintWave2D`); the picker lists have names only, no thumbnail per entry. The view draws slot 1 only (as before).
