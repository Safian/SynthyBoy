# 16 - Final coverage audit: SynthyBoy against the Serum 2 and Vital manuals

Branch `work-coverage` (from `develop` bc376e4, 1594 keys). Docs-only audit of 2026-11-08. Every row was checked against the code (file:line, key in `docs/params.json`, UI panel, test name); the earlier reports (`10-coverage-matrix.md`, `UI-GESTURE-AUDIT.md`, `*-REPORT.md`) were used as a to-do list for where to look, not as evidence. The definitive work list built from this file is [17-BACKLOG.md](17-BACKLOG.md).

## 1. Status legend

| Status | Meaning |
|--------|---------|
| DONE | engine + key + Kick page + UI + a test that proves the audible or functional effect; Kick and UI are implied for every key (see X01-07, X01-08), so a row lists the key, the engine / test and the panel |
| PARTIAL | part of the function is DONE; the gap column says exactly what is missing |
| MISSING | not in the code (or only a facade that does nothing) |
| IN-PROGRESS | owned by one of the two running workers (`agy/modamt` = block B20: mod-amount modulation G1-G6 of the import audit; the warp-env items G34-G39 are DONE in block B21 since 2026-11-09); not counted as MISSING |
| DIFFERENT | differs from Serum on purpose (a row of 14-morning-questions.md or a decision of 12-decisions.md is named) |
| EXCLUDED | Kick or not a function (00-BRIEF: browser, internal grid / sequencer, Arp, Clips) |

Abbreviations in the evidence column. Files: `PT` = core/src/param_table.cpp, `VB` = core/engine-vital/adapter/vital_bindings.cpp, `VE` = core/engine-vital/adapter/vital_engine.cpp; panels are in shell-juce/ui/panels/. Tests: `t_X` = core/engine-vital/tests/test_X.cpp, `ct_X` = core/tests/test_X.cpp, `ui_X` = shell-juce/ui/tests/test_X.cpp, followed by `:line` of the TEST and its name. Other: `14 row n` = docs/ui-design/14-morning-questions.md, `12-decisions Qn`, `G` = old gap id of 10-coverage-matrix.md section 3, `A` = assumption of 13-new-keys.md.

## 2. Findings that contradict the earlier reports

1. **The wavetable editor is a facade.** Its edits never reach the engine or a preset. Closing the sheet only updates the wavetable panel's own display copy (WavetablePanel.cpp:709-721, `onClosed` -> `waveView->setWavetable`); there is no `UiHost` call, no `content.wavetables` entry, no `oscN.wavetable` = Preset table. `Save Table` and `Embed in Preset` only clear the modified flag (WavetableEditorSheet.cpp:2068-2069); the export builds the WAV bytes and drops them (:2228-2237); Import feeds a generated 2 s saw sweep or a 64x64 gradient, there is no file chooser (:1298-1330). The core library (core/wavetable-edit, core/formula) is complete and tested; the UI-GESTURE-AUDIT rows for the editor are 'closed' because the gestures work, not because the result is usable. Rows S13-01 .. S13-17.
2. **The shells have no content store.** `setContentStore` is called only by tools/preset-render (preset_render.cpp:96) and tools/vital-import; shell-juce and shell-clap never set one, and the only `FileChooser` in the UI is the tuning panel (GlbTuningPanel.cpp:255). Consequence: a user wavetable (`Preset table`), a sample (`oscN.type` = Sample), a Convolve impulse response and every imported Vital preset with content cannot be loaded, saved or played from the plugin, although the engine and its tests handle them (t_wp4 :179, :333, t_wp7 :231). The sample view draws a procedural waveform (OscWaveView.cpp:903-914), not a sample. Rows S04-02, S07-06, S10 Convolve, S13, I01-16, X01-01 .. X01-03. This is the largest open item.
3. **Convolve 'Load IR...' is a toggle.** FxConvolvePanel.cpp:26-31 flips `convolve.ir` between None and Preset IR; `content.impulses` is never filled, so from the UI the effect stays a bypass.
4. **The preference toggles do nothing.** `Dbl-Tap`, `Freq: Hz`, `Silence Stop` (GlbSettingsPanel.cpp:53-75) flip private booleans that nothing reads, and there is no preference storage. 10-coverage-matrix.md calls the preferences HAVE ('UI settings file'). The About line is a hard-coded string (GlbSettingsPanel.cpp:94). Rows S15-02, S15-06, S15-07, S15-20.
5. **`Lock Matrix` does nothing.** `matrixLocked` is set in ModMatrixPanel.cpp:760 and read nowhere else (:737, :840). Row S11 `Lock Matrix`.
6. **Only 37 of the 80 Serum filter types have their own slot.** core/filters-serum implements 80 types, but the engine exposes 22 models x 12 styles = 8 Vital models + 37 Serum types (serum_filter_bank.cpp:27-42; WP48-REPORT: 'the other filter types map to the eight existing models'). 43 types of the manual have no slot (12 Normal 12/24 dB, 4 morphing multi, 9 flanges, 12 misc, 6 new; the 8 Vital models only approximate some of them). Decision Q13 said all ~35 missing types as real DSP; the DSP exists, the mapping and the picker do not. Rows S08. The PZ SVF curve has an engine path (t_wp6 :262) but no editor.
7. **The Vital importer is reachable only through a CLI.** tools/vital-import writes `.sbpreset` + content; the plugin neither links it nor can it open a file (I01-16). It also still treats `detune_range`, `midi_track`, `voice_priority`, `spectral_unison` as auxiliary controls (vital_import.cpp:188) although `oscN.unison.range`, `oscN.pitch_track`, `master.voice.steal_priority` exist since B14 / B15 (I01-10).
8. **Task overlap to watch: G6 of the import audit.** The mod-amount worker was told to add a key for `osc_N_transpose_quantize`; the key `oscN.transpose_snap` already exists (B14, PT:~371, bound to `transpose_quantize` at VB:161). The worker must reuse it and add only the importer mapping.
9. **Key budget.** `kMaxParams` = 2048, 1594 keys used. The remaining work (17-BACKLOG.md) needs roughly 175-210 keys (about 300-340 if RISE / FALL became 128 keys as MANUALCHECK proposed), plus whatever the two running workers add (assumed at most 40); it fits under the cap, but a 128-key RISE / FALL block would leave under 80 spare keys (recommendation in the backlog: store RISE / FALL in the per-slot curve content, 0 keys).
10. **UI-GESTURE-AUDIT header count.** The header says 'GAP left 6'; the table has 3 rows that are not implemented / closed (02-24, 02-25, 02-26, all granular / spectral). The header was not updated when OSCEXTRA closed 02-27 / 02-28.
11. **Smaller drift.** Parameter undo / redo has no test (Editor.cpp:155); the Auto slice markers are not drawn because `IEngine` does not hand out the onsets; 'Round Robin' of Vital's note priority is not a separate choice; the 4x oversample switch ends sounding notes (14 row 29).

Items that no earlier list named (found in this audit, all in the tables above): sample Trim, Snap Off / Zero / Beats / Loop and Show Marker Animation (S04); a modulation destination for the single slice (S04); Swap Warps (S02); LFO phase Snap to Grid (S11); matrix sources Fixed, Release Velocity, Active Voices, Note-On Alt 1 / 2, Note-On Rand Discrete, Expression X (S11); Aux Source submenu on the control sheet (S11); live modulation display on knobs (S11); the preference list of p.313-315 (S15); MPE mappings XYZ / YZ -> macros, Y -> mod wheel, bend also to X, Expr Y bipolar (S15, S16); lock for quality and tuning (S15); MIDI map load / save files (S16); rack cut / copy / paste / clear and rack presets (S10); formula presets and examples (S14); Round Robin note priority (V01); the facade findings 1-7 of section 2.

## 3. Summary

| Area | Rows | DONE | PARTIAL | MISSING | IN-PROGRESS | DIFFERENT | EXCLUDED |
|------|-----:|-----:|--------:|--------:|------------:|----------:|---------:|
| S01 Serum 2: welcome, basics, pitch, routing, module menu (p.13-36) | 24 | 16 | 2 | 2 | 0 | 0 | 4 |
| S02 Serum 2: wavetable oscillator, unison, warp (p.37-57) | 38 | 33 | 4 | 1 | 0 | 0 | 0 |
| S03 Serum 2: multisample oscillator (p.58-67) | 5 | 5 | 0 | 0 | 0 | 0 | 0 |
| S04 Serum 2: sample oscillator (p.68-84) | 18 | 7 | 5 | 5 | 0 | 0 | 1 |
| S05 Serum 2: granular oscillator (p.85-103) | 7 | 6 | 0 | 1 | 0 | 0 | 0 |
| S06 Serum 2: spectral oscillator (p.104-122) | 7 | 6 | 0 | 1 | 0 | 0 | 0 |
| S07 Serum 2: sub and noise (p.123-132) | 8 | 5 | 0 | 3 | 0 | 0 | 0 |
| S08 Serum 2: filters (p.133-143) | 17 | 9 | 3 | 4 | 0 | 1 | 0 |
| S09 Serum 2: mixer and routing (p.144-151) | 7 | 7 | 0 | 0 | 0 | 0 | 0 |
| S10 Serum 2: FX (p.152-182) | 27 | 20 | 1 | 5 | 0 | 1 | 0 |
| S11 Serum 2: modulation (p.183-214) | 47 | 25 | 5 | 10 | 5 | 2 | 0 |
| S12 Serum 2: voicing, keyboard, clips, arp (p.215-273) | 12 | 9 | 0 | 0 | 0 | 0 | 3 |
| S13 Serum 2: wavetable editor and import (p.274-303) | 17 | 1 | 11 | 5 | 0 | 0 | 0 |
| S14 Serum 2: formula parser (p.304-311) | 4 | 1 | 1 | 2 | 0 | 0 | 0 |
| S15 Serum 2: global settings, voice control, quality, tuning (p.312-322) | 21 | 6 | 5 | 10 | 0 | 0 | 0 |
| S16 Serum 2: appendices (p.323-354) | 10 | 3 | 2 | 2 | 0 | 0 | 3 |
| V01 Vital manual (unofficial, 43 pages as cited) | 32 | 21 | 7 | 0 | 2 | 1 | 1 |
| I01 Vital import fidelity and import entry points | 16 | 1 | 1 | 6 | 6 | 2 | 0 |
| X01 Cross-cutting: shell, content pipeline, platforms (found in this audit) | 10 | 3 | 1 | 4 | 0 | 2 | 0 |
| **Total** | **327** | **167** | **48** | **78** | **13** | **9** | **12** |

Serum 2 chapters (S01-S16): 269 rows; Vital manual (V01): 32 rows; import fidelity (I01): 16; cross-cutting (X01): 10. A row is a user-facing function or a group of functions that share one status and one evidence; the Serum filter list, the warp mode table and the modulation sources are split where the status differs.

Key budget: `kMaxParams` = 2048 (core/include/synthyboy/params.h:77), 1594 keys used, 454 free. See 17-BACKLOG.md section 1.

## 4. Per-function table

### S01 Serum 2: welcome, basics, pitch, routing, module menu (p.13-36)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S01-01 | S p.13-15, 354 | Registering, installing, uninstalling, credits | EXCLUDED | not a function (store / host) |  |
| S01-02 | S p.16-17 | Exploring Serum: pages OSC / MIX / FX / MOD / GLOBAL, sound design overview | DONE | Editor.cpp page tabs; panels registered per Page (osc, mix, flt, env, lfo, mod, fx, glb); ui_render `uir_editor_layout_touch_targets_and_modes`, ui_panels `uip_headless_render_every_page` | CLIP / ARP tabs excluded (Kick) |
| S01-03 | S p.18 | Adding Serum to a track | EXCLUDED | host business |  |
| S01-04 | S p.18-19 | Loading a preset: prev / next | DONE | UiHost::stepPreset (UiHost.h:37); ui_render `uir_preset_strip_and_screenshots` | browser = Kick |
| S01-05 | S p.327-339 (B) | Presets browser: folders, search, tags, ratings, packs, export | EXCLUDED | Kick (00-BRIEF) |  |
| S01-06 | S p.20 | Creating a new sound: Init Preset | DONE | UiHost::initPatch -> PluginProcessor; GlbSettingsPanel.cpp:167; ui_shellfeat `shf_save_as_default_new_instances_and_init_preset` |  |
| S01-07 | S p.21 | Saving changes, ARTIST / DESC metadata | EXCLUDED | Kick preset browser / save owns the metadata UI (core format has author / description / tags: core/src/preset.cpp, ct_preset `preset_save_load_round_trip` :38) | confirm with the owner that the synth UI needs no save dialog |
| S01-08 | S p.22 | Embedding content when saving a preset | DONE (WP-01) | state v3 `content` section references files by pack / file / sha256 (core/src/content_state.cpp, ct_state_v3 :250); Q8 decision: tables are not embedded ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S01-09 | S p.22 | Dragging audio to the DAW / share a rendered note | MISSING | none (grep Resample / renderOffline: none) | needs an offline render API; G03 |
| S01-10 | S p.24 | Help tooltips | DONE | Editor.h:74 TooltipWindow; setTooltip on controls |  |
| S01-11 | S p.25 | Serum keyboard (touch keys, velocity, octave shift) | DONE | Strip.cpp; ui_render `uir_keyboard_notes_reach_the_engine` |  |
| S01-12 | S p.25-26 | Knobs and sliders: drag, fine, wheel, numeric entry, reset | DONE | Sheet.cpp (value entry), ParamControls.cpp; ui_render `uir_sheet_value_entry`, `uir_toggle_choice_and_slider`, ui_model `ui_gesture_maths` | the double-click preference is a dead toggle: S15-02 |
| S01-13 | S p.26 | MIDI Learn / Remove MIDI CC / Lock Parameter | DONE | core/src/shell_features.cpp; Sheet.cpp:124-150; ct_shell_features `shell_midi_map_learn` :77, `shell_locks_values` :129; ui_shellfeat `shf_midi_learn_through_the_param_sheet` | omni, 7 bit, one CC per parameter (14 row 23) |
| S01-14 | S p.27 | Undo and Redo | DONE | Editor.cpp:155 performUndo; wavetable sheet undo: ui_gaps `uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger` | no dedicated test for the editor-level parameter undo |
| S01-15 | S p.27 | Main output volume and meter | DONE | `master.volume` (PT:389); Strip.cpp:25 outputPeakL / R |  |
| S01-16 | S p.28 | Enable an oscillator / filter | DONE | `osc1..3.on`, `filter1/2.on`, `noise.on`, `sub.on`, `fxfilter.on` |  |
| S01-17 | S p.29 | Choosing options with menus, arrows, wheel | DONE | ParamChoice (ParamControls.cpp:605); ui_render `uir_toggle_choice_and_slider` |  |
| S01-18 | S p.29 | Pitch OCT / SEM / FIN / CRS | DONE | `oscN.transpose`, `oscN.tune`, `oscN.pitch_coarse` (PT:371); TransposeControl.cpp:17; t_pitchuni `pitch_coarse_and_modes_render_distinct_audio` :88; ui_gaps `uig_transpose_oct_sem_control` |  |
| S01-19 | S p.30 | Pitch modes Semitones / Harmonics / Ratio / Step | PARTIAL | `oscN.pitch_mode` (PT:371, PT:100); t_pitchuni :88, `step_mode_follows_the_active_tuning` :279 | Ratio has no SRC choice (S01-20); Step follows the active scale / .scl only, real MTS-ESP source not linked (S15-18) |
| S01-20 | S p.30 | Ratio mode SRC (tune to another oscillator or Sub) | DONE (GROUPB WP-05) | `oscN.ratio_src` (B27), pitch taps `producers_module.cpp:154`; `test_oscextra2.cpp` `ratio_source_tunes_an_oscillator_to_another_oscillator_or_the_sub`; `OscPanel.cpp:112` (loop-safe list) | Serum enumeration read from two files (14 row 77) |
| S01-21 | S p.30 | Global > Main Tuning as modulation destination | DONE | `master.tune` is a modulation destination (modDestinations); t_vital_params `modulatable_parameters_are_modulation_destinations_of_the_engine` :276 |  |
| S01-22 | S p.31-32 | Routing: filter knob blend, Main, Direct, None, Bus 1 / 2 sends | DONE | `oscN.destination` (PT:kOscDestinations), `oscN.filter_blend`, `oscN.bus1/2_send`, `filterN.destination`, `filterN.busM_send`; t_wp5 :100, :129, :152, :209; OscWaveView.cpp:758 |  |
| S01-23 | S p.33-35 | Module menu: Lock, Init, Copy, Paste, Copy w/ mods, Swap, drag-copy | DONE | ModuleSheet.cpp:102-185; ui_gaps `uig_module_ops_swap_copy_paste_init`, `uig_module_header_drag_onto_another_header_swaps_alt_copies`; ui_shellfeat `shf_module_lock_*` | lock keeps the module's own keys, not the modNN slots pointing at it (14 row 24) |
| S01-24 | S p.36 | Resizing the UI | DONE | Theme scale + phone / tablet / wide layouts; ui_render `uir_editor_layout_touch_targets_and_modes` |  |

### S02 Serum 2: wavetable oscillator, unison, warp (p.37-57)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S02-01 | S p.38 | Wavetable anatomy: up to 256 frames of 2048 samples | DONE | engine reads 1..256 frames (t_wp4 `user_wavetable_of_257_frames_keeps_every_frame` :496); 13 built-in tables |  |
| S02-02 | S p.39, V p.7-8 | Waveform display 2D / 3D / spectral | DONE | WavetablePanel.cpp:144, OscWaveView.cpp:292; ui_wavetable `ui_wavetable_panel_controls_and_visual`, ui_gaps `uig_wavetable_3d_view_tilt_pinch` | a Preset-table oscillator cannot be displayed (no store): X01-01 |
| S02-03 | S p.40 | Choose a wavetable: menu, arrows, wheel, swipe | DONE (WP-01) | `oscN.wavetable` = 13 built-ins + `Preset table` (PT); OscWaveView.cpp:328; ui_gaps `uig_osc_wavetable_name_swipe_steps_the_table` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S02-04 | S p.41 | PHASE | DONE | `oscN.phase`; original v2 key (sweep test t_vital_audio :138) |  |
| S02-05 | S p.42 | RAND (random phase) | DONE | `oscN.random_phase`; original v2 key |  |
| S02-06 | S p.43 | Phase Legato | DONE | `oscN.phase_legato`, `voice.phase_legato`; t_pitchuni `phase_legato_changes_the_start_phase_of_overlapping_notes` :337 |  |
| S02-07 | S p.44 | Phase memory All Voices / Contiguous / Per Voice | DONE | `oscN.phase_memory`; t_pitchuni :364 |  |
| S02-08 | S p.45 | WT POS | DONE | `oscN.frame`, `oscN.frame_spread` |  |
| S02-09 | S p.45 | WT POS Smooth Interpretation switch | DONE (GROUPB WP-05) | `oscN.smooth_interp` (B27), `synth_oscillator.cpp:1338`; `smooth_interpolation_off_steps_between_frames_on_blends_them` | default Off (14 row 78) |
| S02-10 | S p.45 | Unison voices 1-16 with CPU colour | DONE | `oscN.unison.voices`; OscWaveView.cpp:122; t_vital_audio `unison_seven_voices_against_one` :750 |  |
| S02-11 | S p.46-47 | Unison MODE Linear / Super / Exp / Inv / Random | DONE | `oscN.unison.mode` (PT:377, VB:155); t_pitchuni `unison_modes_and_blend_shape_render_distinct_audio` :180 |  |
| S02-12 | S p.47 | Unison STACK (Center-12 / -24, octaves, 12+7 with 1-3x repeat) | DONE | `oscN.unison.stack` (11 styles), `oscN.unison.stack_repeat` (PT:380); t_pitchuni :180 |  |
| S02-13 | S p.47-48 | Unison WIDTH, RANGE, BLEND, blend shape | DONE | `unison.spread`, `unison.range`, `unison.blend`, `unison.blend_shape`; t_pitchuni :312 | unison blend default 80 % vs manual 75 % (14 row 31) |
| S02-14 | S p.48, V p.41 | Unison WT POS / WARP 1 / WARP 2 spread | DONE | `frame_spread`, `warp.spread`, `warp2.spread`, `morph.spread` |  |
| S02-15 | S p.49 | WARP menu (grouped), amount, < > | DONE | `oscN.warp.type`, `warp.amount`; ParamControls.cpp:626 setGroups; ui_gaps `uig_warp_type_list_is_grouped` |  |
| S02-16 | S p.49, 56 | Second warp slot | DONE | `oscN.warp2.*` (VB:131); t_wp4 `warp_slot_2_changes_the_oscillator_and_chains_after_slot_1` :377 |  |
| S02-17 | S p.56 | Swap Warps (exchange the two slots) | DONE | Swap Warps entry at the end of the grouped warp lists (`ParamChoice::setExtraAction`, `OscPanel.cpp` `swapWarps`); test `uig_swap_warps_exchanges_the_two_warp_slots` |  |
| S02-18 | S p.50 | Warp Sync | DONE | warp type Sync (PT:43) |  |
| S02-19 | S p.50 | Sync Var fader (hard to soft sync) | DONE | warp entry `Sync Var` (11), VAR fader = the phase knob of the slot (labelled Var); test_warpenv `warpenv_neutral_positions_and_the_alt_modes` | a separate entry instead of a fader on Sync: morning question 35 |
| S02-20 | S p.50 | Bend +, Bend -, Bend +/- | DONE | warp entries `Bend +` (12), `Bend -` (13), `Bend +/-` (14); cap bit 44; test_warpenv | own laws: morning question 36 |
| S02-21 | S p.50 | PWM | DONE | warp type Pulse (Vital pulse width) is the PWM of Serum in effect (PT:43) | behaviour not A/B checked against Serum |
| S02-22 | S p.50-51 | Asym +, Asym -, Asym +/-, Flip, Mirror | DONE | warp entries `Asym +/-/+-` (15-17), `Flip` (18), `Mirror` (19); cap bit 44; test_warpenv | own laws: morning question 36 |
| S02-23 | S p.51 | Remap 1-4 warp modes (graph, mirrored, sine, 4x) | DONE | warp entries `Remap 1-4` (20-23) read the curve of `oscN.remap.slot` (B16); test_warpenv `warpenv_remap_modes_use_the_curve_of_the_oscillator` |  |
| S02-24 | S p.51 | Quantize | DONE | warp type Quantize (PT:43) |  |
| S02-25 | S p.51 | Odd/Even | DONE | warp entry `Odd/Even` (24); test_warpenv (0 % odd only, 100 % even only) |  |
| S02-26 | S p.52 | Warp Filter group LPF / HPF | DONE | warp entries `Filter LPF` (25), `Filter HPF` (26); cap bit 45; test_warpenv (centroid) |  |
| S02-27 | S p.52-54 | Warp Distortion group (14 modes: Tube .. Soft Sat.) | DONE | warp entries 27-40 `Dist Tube` .. `Dist Soft Sat.`; cap bit 46; test_warpenv | four shapers are our own curves: morning question 36 |
| S02-28 | S p.54 | FM from Osc B / C | DONE | warp types FM from Osc N (PT:43-47); t_vital_params `oscillators_that_modulate_each_other_stay_finite` :539 |  |
| S02-29 | S p.54, 56 | FM / PD / AM / RM from Noise, Sub, Filter 1, Filter 2 (audio-rate taps) | DONE (GROUPB WP-06) | warp entries 52 .. 75 (`kSrcFmThruZeroNoise ..`), `synth_oscillator.cpp:351`; `warp_modes_from_the_noise_sub_and_filters_change_the_sound` (24 modes), `..._use_the_filter_of_the_same_voice`, `..._act_on_a_sample_oscillator` | the filters are read one block late (14 row 80) |
| S02-30 | S p.54-55 | FM Thru-Zero, Exp, Linear | DONE | warp entries 41-46 `FM Thru-Zero / Exp / Linear from X / Y`; cap bit 47; test_warpenv `warpenv_fm_thru_zero_exp_and_linear_are_different_laws` |  |
| S02-31 | S p.55 | PD (from B, C, Self) | DONE | warp entries 47-49 `PD from X / Y`, `PD Self`; cap bit 48; test_warpenv |  |
| S02-32 | S p.56 | AM (from B, C) | DONE | warp entries 50, 51 `AM from X / Y`; cap bit 48; test_warpenv |  |
| S02-33 | S p.56 | RM from Osc B / C | DONE | warp types RM from Osc N |  |
| S02-34 | V p.9 | Left mod knob: spectral morph algorithms (Vocode, Formant Scale, Harmonic / Inharmonic Stretch, Smear, Random Amplitudes, LP, HP, Phase Disperse, Shepard, Time Skew) | DONE | `oscN.morph.*` (kMorphTypes PT:39); t_vital_params `morph_never_gives_nan` :510 |  |
| S02-35 | V p.10 | Right mod knob: Formant, Squeeze, Sync, Bend, PWM, Quantize | DONE | warp types (PT:43) |  |
| S02-36 | S p.57 | Wavetable pan, level | DONE | `oscN.pan`, `oscN.level` |  |
| S02-37 | V p.40 | Hi-Resolution Wavetable option | PARTIAL | tier-controlled in the engine | no key (tiny) |
| S02-38 | S p.29, 69 | Oscillator type menu: Wavetable, Multisample, Sample, Granular, Spectral | PARTIAL | `oscN.type` = Wavetable, Sample (PT:88) | Multisample, Granular, Spectral entries missing (S03, S05, S06) |

### S03 Serum 2: multisample oscillator (p.58-67)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S03-01 | S p.58-60 | Multisample: select instrument / SFZ, zone display | **DONE (GROUPC)** | GROUPC WP-13: `OscWaveView::paintMultisample`, `loadInstrumentFile`; SFZ instrument in `content.instruments[]` | - |
| S03-02 | S p.61-62 | Multisample envelope (OVERRIDE, DELAY, A, H, D, S, R) | **DONE (GROUPC)** | GROUPC WP-13: `oscN.multi.override / delay / attack / hold / decay / sustain / release`, `LayerVoice` envelope | - |
| S03-03 | S p.62 | Multisample VEL TRACK, RAND | **DONE (GROUPC)** | GROUPC WP-13: `oscN.multi.vel_track / vel_override`, `oscN.random_phase` | - |
| S03-04 | S p.63-66 | Multisample TIMBRE, unison, warp, pan, level | **DONE (GROUPC)** | GROUPC WP-13: `oscN.multi.timbre`, the oscillator's own unison / warp / pan / level | - |
| S03-05 | S p.67 | Switch the last note played to a sample | **DONE (GROUPC)** | GROUPC WP-13: `switchToSingleSample` (`ContentIO.cpp`) | - |

### S04 Serum 2: sample oscillator (p.68-84)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S04-01 | S p.69 | Select the sampler (oscillator type Sample) | DONE | `oscN.type` (PT:315); t_wp4 `sample_oscillator_type_plays_the_content_sample_at_the_key_pitch` :179 |  |
| S04-02 | S p.68, 70 | Choose / load a sample: list, Load Sample, show location, reload; root note from the file name or WAV chunk | DONE (WP-01) | engine reads `content.samples` through an IContentStore (t_wp4 :179) but the shell sets none and has no file chooser (X01-01) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S04-03 | S p.71 | Sample START / END markers | DONE (WP-01) | `oscN.sample.start`, `.end` (PT:582..); OscWaveView.cpp:504; ui_gaps `uig_sample_view_markers_loupe_loop_pair` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S04-04 | S p.72 | Sample ops: Show Marker Animation, Zoom to Start / End | DONE (WP-01 range band, GROUPA2 WP-17 live marker: `OscWaveView.cpp:409`, test `content_sample_marker_animation_shows_the_live_unison_start`) | zoom / pan: OscWaveView.cpp:336; ui_gaps `uig_sample_view_zoom_pan_buttons_pinch_wheel` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S04-05 | S p.72-73 | Snap Off / Zero / Beats / Loop | DONE (WP-01) | none ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S04-06 | S p.73 | Fade Edges, Normalize, Reverse | DONE | `oscN.sample.fade_edges`, `.normalize`, `.reverse`; t_wp4 `sample_scan_normalize_fades_slices_and_unison_start` :250, `sample_loop_modes_start_end_and_reverse` :205 |  |
| S04-07 | S p.73 | Trim (cut the sample to start / end) | DONE (WP-01) | none (start / end exist) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S04-08 | S p.74-75 | Slicing Off / Auto (threshold) / Manual | DONE (WP-01) | `oscN.sample.slice_mode`, `.slice_threshold`, `content.samples[].slices`; t_wp4 `sample_slices_auto_and_manual_pick_the_slice_by_key` :295; ui_gaps `uig_sample_view_slices_manual`, `uig_sample_view_threshold_line_auto_slices` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S04-09 | S p.76 | Slice options: Play Slice to End, Play Single Slice, Root Note | DONE | `oscN.sample.slice_play`, `.slice_root` |  |
| S04-10 | S p.76 | Mod source on the single-slice destination | DONE (GROUPB WP-05) | `oscN.sample.slice` (B27, destinations 909 .. 911), `sample_oscillator.cpp:128`; `single_slice_destination_moves_the_played_slice_by_value_and_by_modulation` |  |
| S04-11 | S p.76 | Send slices to clip / auto-sync to clip | EXCLUDED | Kick (clips) |  |
| S04-12 | S p.77-79 | Loop menu: One-shot, Forward, Reverse, Forward/Reverse, Tailed; loop start / end / crossfade | DONE | `oscN.sample.loop_mode`, `.loop_start`, `.loop_end`, `.loop_xfade`, `.loop_relative`, `.loop_exit`; t_wp4 :205 |  |
| S04-13 | S p.80 | Scan (speed, range, reverse, lock to tempo, to BPM) | DONE | `oscN.sample.scan` .. `.length_beats`; t_wp4 :250 | varispeed; pitch-preserving scan is the granular oscillator (S05) |
| S04-14 | S p.81-82 | Sample unison START / SPAN | DONE | `oscN.sample.uni_start`, `.uni_span`; t_wp4 :250 |  |
| S04-15 | S p.83 | Warp on a sample | PARTIAL | only Quantize / FM / RM act on a sample voice (14 row 9) | other warps inert by decision; revisit after warp-env |
| S04-16 | S p.84 | Sample pan, level | DONE | `oscN.pan`, `oscN.level` |  |
| S04-17 | S p.84, 103, 122 | Switch a sample to a wavetable | MISSING | none | G51 |
| S04-18 | V p.10-12 | Vital sampler: level, pan, pitch, loop, keytrack, bounce, routing, own sample | PARTIAL | `noise.*` covers the built-in noise samples only (PT:407.., VB:170) | custom sample, bounce (ping-pong) missing: I01-07 |

### S05 Serum 2: granular oscillator (p.85-103)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S05-01 | S p.85-90 | Granular: select, start / end, operations, loop menu, crossfade | **DONE (GROUPC)** | GROUPC WP-11: `oscN.type` Granular, shared region / loop keys, `loop_grains`, `manual` | - |
| S05-02 | S p.91-92 | Granular window amount / skew / shape | **DONE (GROUPC)** | GROUPC WP-11: `oscN.gran.window_*`, the thumbnail | - |
| S05-03 | S p.93-95 | Granular unison START / SPAN / SPAWN pattern | **DONE (GROUPC)** | GROUPC WP-11: shared `uni_start / uni_span`, `oscN.gran.spawn_pattern` | - |
| S05-04 | S p.96-98 | Granular X\|Y control | **DONE (GROUPC)** | GROUPC WP-11: `oscN.gran.y_axis / xy_y`, the X\\|Y dot | - |
| S05-05 | S p.99-101 | Granular warp mode, scan, density, length | **DONE (GROUPC)** | GROUPC WP-11: shared warp keys, `scan*`, `density*`, `length*` | - |
| S05-06 | S p.102 | Granular grain randomization, pan, level | **DONE (GROUPC)** | GROUPC WP-11: `oscN.gran.rand_*`, `reverse_grains` | - |
| S05-07 | S p.103 | Granular: switch to wavetable | MISSING | none | G51 |

### S06 Serum 2: spectral oscillator (p.104-122)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S06-01 | S p.104-106 | Spectral: select, start / end | **DONE (GROUPC)** | GROUPC WP-12: `oscN.type` Spectral, spectrogram, shared start / end | - |
| S06-02 | S p.107 | Spectral high / low frequency markers | **DONE (GROUPC)** | GROUPC WP-12: `oscN.spec.freq_lo / freq_hi / lohi_smooth / lohi_post_warp`, the frequency strip | - |
| S06-03 | S p.108-110 | Spectral operations, loop menu, crossfade | **DONE (GROUPC)** | GROUPC WP-12: shared loop keys, `manual`, `position` | - |
| S06-04 | S p.111-113 | Spectral unison | **DONE (GROUPC)** | GROUPC WP-12: shared unison keys | - |
| S06-05 | S p.114-115 | Spectral X\|Y control | **DONE (GROUPC)** | GROUPC WP-12: `oscN.spec.y_axis / xy_y` | - |
| S06-06 | S p.116-121 | Spectral scan, cut, filter curve, mix, warp mode, pan, level | **DONE (GROUPC)** | GROUPC WP-12: `scan*`, `cut`, `mix`, the filter graph, `phase_lock`, `transients`, 23 spectral warps | - |
| S06-07 | S p.122 | Spectral: switch to wavetable | MISSING | none | G51 |

### S07 Serum 2: sub and noise (p.123-132)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S07-01 | S p.123-127 | Sub oscillator: on, waveform (6), OCT, CRS, pan, level, routing | DONE | `sub.*` (PT:731-741); sub_source.cpp; t_oscextra `sub_osc_on_off_and_level` :244, `sub_osc_waveforms_have_their_designed_spectra` :265, `sub_osc_octave_and_tune_set_the_pitch` :316, `sub_osc_pan_and_destination` :338; SubPanel.cpp:142-194; ui_gaps `uig_sub_waveform_picker_and_noise_sample_stepper` |  |
| S07-02 | S p.127 | Sub PHASE and contiguous option | DONE (GROUPA1 WP-04) | `sub.phase`, `sub.phase_contiguous` (B23, PT:2051-2052); sub_source.cpp; t_noisesample `sub_phase_sets_the_start_phase`, `sub_phase_contiguous_continues_with_the_phase_of_the_previous_note`; SubPanel.cpp:37; Serum import `kParamInitialPhase` / `kParamContiguousPhase` (A349) | closed by GROUPA1: docs/ui-design/GROUPA1-REPORT.md |
| S07-03 | S p.145 | Sub into filter blend, bus sends | DONE | `sub.filter_blend`, `sub.bus1/2_send`; t_oscextra :384, :408 |  |
| S07-04 | S p.128, 132 | Noise: on, level, pan, routing | DONE | `noise.*`; t_oscextra `noise_extras_keep_the_existing_noise_routing` :612 |  |
| S07-05 | S p.129 | Noise presets: White / Pink / Brown / Geiger (+5 of ours) | DONE | `noise.sample` (PT:743, VB:170); t_oscextra `noise_sample_selection_changes_the_colour` :486; NoisePanel.cpp:22-79 | the other five names and the pulse width are ours (14 row 27) |
| S07-06 | S p.130 | Noise: load an own sample | DONE (GROUPA1 WP-04) | `noise.type` Noise / Sample (B23), `content.samples[]` osc 4, `Sample::makeUserData` / `useUserData`; NoisePanel.cpp:111 (Load...), WP-01 flow `loadSampleFile`; t_noisesample `noise_type_sample_plays_the_content_sample`; ui_content `content_noise_sample_load_through_the_noise_panel_...` | closed by GROUPA1: docs/ui-design/GROUPA1-REPORT.md |
| S07-07 | S p.131-132 | Noise one-shot / loop, START, RAND, PITCH, FINE, keytrack | DONE | `noise.loop`, `.phase`, `.random_phase`, `.pitch`, `.fine`, `.keytrack`; t_oscextra :519, :551 |  |
| S07-08 | S p.129 | Noise STEREO and FILTER knobs (colour samples) | DONE (GROUPA1 WP-04) | `noise.stereo`, `noise.filter` (B23); sample_source.cpp `colourStage`; t_noisesample `noise_stereo_decorrelates_the_colour_samples`, `noise_filter_low_and_high_passes_the_colour_samples` | closed by GROUPA1: docs/ui-design/GROUPA1-REPORT.md |

### S08 Serum 2: filters (p.133-143)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S08-01 | S p.134-135 | Filter module: on, routing, type menu | PARTIAL | `filterN.model` 22 models x `filterN.style` 12 (PT:49-52) = 45 distinct Serum / Vital types reachable; core/filters-serum implements 80 (CATALOGUE.md) | 43 of the 80 manual types have no model / style slot (rows below) |
| S08-02 | S p.135-136 | Normal family: MG Low 6/12/18/24, Low / High 6/12/18/24, Band / Peak / Notch 12/24 (18 types) | PARTIAL | Slope model: MG Low 6/18, Low 6/18, High 6/18 (serum_filter_bank.cpp:27-42); t_wp6 `all_37_types_are_stable_and_differ_from_each_other_and_from_the_old_models` :189 | 12 types unreachable: MG Low 12/24, Low 12/24, High 12/24, Band 12/24, Peak 12/24, Notch 12/24 (Vital Analog / Ladder only approximate them) |
| S08-03 | S p.136 | Multi: LH LB LP LN HB HP HN BP BN PP PN NN | DONE | Multi SVF model (12 styles); t_wp6 :189 |  |
| S08-04 | S p.136 | Multi morphing: LBH LPH LNH BPN | MISSING | none (DSP exists in core/filters-serum) | 4 types |
| S08-05 | S p.136 | Flanges: Cmb / Flg / Phs with L / H / HL feedback filter (9 types) | MISSING | none (DSP exists) | Vital Comb / Phaser models only approximate |
| S08-06 | S p.137-138 | Misc: EQ 6 dB x3, Ring Mod x2, SampHold x2, Combs / Allpasses / Reverb, French LP, Add Bass, Scream x2 | DONE | models EQ 6 dB .. Scream (serum_filter_bank.cpp:30-37); t_wp6 :189 |  |
| S08-07 | S p.137-138 | Misc: Low / Band / High EQ 12, German LP, Formant I / II / III, Bandreject, Dist.Comb 1/2 LP/BP | MISSING | none (DSP exists) | 12 types (Vital Formant approximates Formant I) |
| S08-08 | S p.138-139 | New: New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF | DONE | models 17-21; t_wp6 :189 |  |
| S08-09 | S p.139 | New: MG Ladder, Acid Ladder, MG Dirty, Comb 2, Exp MM, Exp BPF | MISSING | none (DSP exists; Vital Ladder / Diode / Dirty approximate three) | 6 types |
| S08-10 | S p.135-139 | Var parameter per type (FAT, FREQ, MORPH, ...) | DONE | `filterN.var`, `fxfilter.var`; t_wp6 `filter_var_acts_on_the_new_models_and_not_on_the_eight_old_ones` :166 |  |
| S08-11 | S p.139 | PZ SVF drawable response | PARTIAL | engine fits `content.filter_curves` (t_wp6 `pz_svf_follows_the_drawn_curve_from_the_content` :262) | no curve editor in the UI (grep filterCurves in shell-juce: none): the drawn curve cannot be made |
| S08-12 | S p.140-141 | Filter display: response, +FFT, phase | DONE | FilterViews.h:113 FilterResponseView Mode resp / plusFft / phase |  |
| S08-13 | S p.141-142 | Cutoff, resonance, drive | DONE | `filterN.cutoff`, `.resonance`, `.drive` |  |
| S08-14 | S p.142 | Drive Clean Mode (-24 dB in, +24 dB out) | DONE | `filterN.clean_mode` (PT:704); t_wtremap `clean_mode_feeds_the_filter_24_db_down_and_boosts_24_db_after_it` :332 |  |
| S08-15 | S p.142 | Keytrack follows the oscillator pitch incl. portamento | DIFFERENT | filter keytrack follows the MIDI note like Vital (12-decisions Q14); import stays 1:1 | G56 |
| S08-16 | S p.143 | Filter pan, mix, level | DONE | `filterN.pan`, `.mix`, `.level`; t_wp6 :109, :146 |  |
| S08-17 | V p.13-15 | Vital filter models Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser; blend; keytrack; formant X / Y; comb offset | DONE | `filterN.model` 0-7 (PT:49), `.blend`, `.keytrack`, `.formant.*`, `.comb_offset`; t_vital_audio `filters_other_models_respond_to_the_cutoff_and_stay_bounded` :402 |  |

### S09 Serum 2: mixer and routing (p.144-151)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S09-01 | S p.144-145 | Mixer channels (A, B, C, SUB, NOISE, F1, F2, buses), toggles | DONE | MixerPanel.cpp:74; OscWaveView.cpp:1256; ui_gaps `uig_mixer_strip_header_sheet_swap_copy_and_pan` |  |
| S09-02 | S p.146 | Mixer ENV 1 button per channel (Main / Direct) | DONE | `sub.env1`, `oscN.env1`, `noise.env1`, `filterN.env1` (B21), toggles behind More of MixerPanel (`MixerPanel.cpp`), greyed unless the channel goes to Main / Direct; tests `warpenv_env1_button_of_every_channel`, `warpenv_env1_off_follows_the_longest_envelope_release`, `uig_env1_buttons_sit_on_the_mixer_and_grey_out_off_main_and_direct` |  |
| S09-03 | S p.146, 149 | Sends to Bus 1 / Bus 2, routing of filters to buses | DONE | `*.bus1/2_send`, `filterN.destination`; t_wp5 :152, :209 |  |
| S09-04 | S p.147 | Mixer pan and levels | DONE | `oscN.pan/level`, `noise.*`, `sub.*`; OscWaveView.cpp:1480; ui_lfo `uil_mix_console_strip_drag_is_the_level_and_the_name_tap_is_the_power` |  |
| S09-05 | S p.148-149 | Mixing filters: routing, pan, mix, levels | DONE | `filter.routing`, `filterN.pan/mix/level` |  |
| S09-06 | S p.150 | Mixing the buses, bus levels, per-bus FX bypass | DONE | `bus1/2.level`, `.destination`, `.fx_bypass` (PT:599-600); t_wp5 :152 |  |
| S09-07 | S p.151 | Main and Direct levels | DONE | `master.volume`, `direct.level` (PT:605), `main.fx_bypass` | DIRECT still passes master.volume (14 row 9) |

### S10 Serum 2: FX (p.152-182)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S10-01 | S p.152-153 | FX racks MAIN / BUS 1 / BUS 2 | DONE | `<fx>.rack` for 16 modules (kRacks PT:96); t_wp5 `racks_assign_the_effect_to_main_bus_1_or_bus_2` :231; FxChainPanel.cpp:34 |  |
| S10-02 | S p.154, 158 | Load / save rack presets | DONE (WP-01) | none ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S10-03 | S p.155-157 | Add / copy / remove modules (several instances of one effect) | PARTIAL | 16 fixed modules, one instance per type, remove = bypass (A19, 14 row 3) | several instances of the same effect impossible; owner decision |
| S10-04 | S p.156 | Reorder modules | DONE | `fx.order.1..16`; t_wp5 :271, :309; FxChainPanel hold-to-reorder |  |
| S10-05 | S p.157 | Bypass a module | DONE | `<fx>.on` |  |
| S10-06 | S p.158 | Rack operations: cut / copy / paste / clear a bus | DONE (GROUPA1 WP-10) | `fxCopyRack` / `fxClearRack` / `fxPasteRack` (core/src/fx_rack.cpp), FxRackView.h:214 (`showBusMenu`); ct_fxrack `fxrack_copy_paste_...`, `fxrack_cut_and_clear_...`, `fxrack_paste_stops_...`; ui_fxinst `fxui_wp10_bus_operations_...` | closed by GROUPA1: docs/ui-design/GROUPA1-REPORT.md |
| S10-07 | S p.159 | Modulate FX parameters | DONE | FX keys are modulation destinations; t_fxextra `b17_keys_survive_random_settings_and_type_changes` :604 |  |
| S10-08 | S p.160 | Per-module LEVEL trim | DONE | `<fx>.level` (kCapFxLevel); t_wtremap `fx_level_trims_every_module_output` :282; ui_wtremap `uiw_fx_panels_have_the_level_knob_and_the_filter_panels_the_clean_mode_switch` |  |
| S10-09 | S p.161 | Bode frequency shifter | DONE | `bode.*` (PT:648); core/fx-serum-b; t_wp7 `bode_shifts_the_frequency_up_and_down` :201; FxBodePanel.cpp:189 |  |
| S10-10 | S p.162, V p.27-28 | Chorus | DONE | `chorus.*`; FxChorusPanel.cpp:194 |  |
| S10-11 | S p.162-163, V p.28-30 | Compressor: single / multiband, limit, crossovers | DONE | `compressor.*` (+limit, crossovers); t_fxextra :153, :209, :245; FxCompressorPanel.cpp:315 |  |
| S10-12 | S p.163 | Compressor limit attack 0-10 ms / makeup 36 dB / threshold 0 .. -120 | DIFFERENT | fixed 1.5 ms look-ahead, threshold -80 .. 0 (14 row 31) |  |
| S10-13 | S p.164-166 | Convolve: engine | DONE | `convolve.*` (PT:665); core/fx-serum-b; t_wp7 `convolve_uses_the_impulse_of_the_content_and_rebuilds_when_the_size_changes` :231 |  |
| S10-14 | S p.164-166 | Convolve: load an impulse response | DONE (WP-01) | FxConvolvePanel.cpp:26-31: `Load IR...` only toggles `convolve.ir` None / Preset IR; no file, no `content.impulses` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S10-15 | S p.165-166, V p.30-31 | Delay: modes, HQ, scalar offset, Tap->Delay | DONE | `delay.*` (+style 4, hq, offset); t_fxextra :273, :307; FxDelayPanel.cpp:312 |  |
| S10-16 | S p.166 | Delay left offset (one scalar offset per channel) | DONE (GROUPA1 WP-10) | `delay.offset_left` (B25, PT:2061); delay.cpp; t_fxextra `delay_offset_left_multiplies_the_time_of_the_left_channel`; Serum import `kParamOffsetL` / `kParamOffsetR` (A351) | closed by GROUPA1 |
| S10-17 | S p.167-169, V p.32-33 | Distortion: 13 types, filter order, X-Shaper editor | DONE | `distortion.*`; t_fxextra :346, :395; FxShaperEditor.cpp; ui_fx `fx_extras_xshaper_editor_edits_the_content` | default type Soft clip vs Tube (14 row 31) |
| S10-18 | S p.169, V p.34 | Equalizer | DONE | `eq.*` (3 bands, superset of Serum's 2); FxEqPanel.cpp:328 |  |
| S10-19 | S p.170, V p.34 | Filter (FX) | DONE | `fxfilter.*`; FxFilterPanel.cpp:198 |  |
| S10-20 | S p.171-172, V p.35-36 | Flanger | DONE | `flanger.*`; FxFlangerPanel.cpp:185 |  |
| S10-21 | S p.173 | Hyper / Dimension | DONE | `hyperdim.*` (PT:624); core/fx-serum-a hyper_dimension.cpp; t_wp7 :174; FxHyperDimPanel.cpp:215 |  |
| S10-22 | S p.174, V p.36-37 | Phaser incl. DEPTH 2 | DONE | `phaser.*` (+depth2); t_fxextra :463; FxPhaserPanel.cpp:192 | rate range 0.04 .. 4 Hz vs manual 0 .. 20 Hz (14 row 31) |
| S10-23 | S p.175-177, V p.37-38 | Reverb: Plate / Hall / Vintage / Nitrous / Basin, rooms, controls per type | DONE | `reverb.*` (PT:722); t_fxextra `reverb_algorithms` :491; FxReverbPanel.cpp:251 | algorithms are our own, sound needs a listening pass (14 row 21); default type Vital (row 31) |
| S10-24 | S p.177 | Reverb FEEDBACK of Nitrous / Basin | DONE (GROUPA1 WP-10) | `reverb.feedback` (B25, PT:2062); reverb_algorithms.cpp; t_fxextra `reverb_feedback_scales_the_tail_of_nitrous_and_basin_only`; Serum import of Space / Abyss (A352) | closed by GROUPA1; the law is ours (14 row 68) |
| S10-25 | S p.177-181 | Splitter L/H, L/M/H, M/S | DONE | `splitter_*.*` (PT:681); core/fx-serum-a; t_wp7 `splitters_are_transparent_and_host_the_effects_of_their_bands` :408; FxSplitterPanels.cpp:500 |  |
| S10-26 | S p.182 | Utility | DONE | `utility.*` (PT:635); t_wp7 :121; FxUtilityPanel.cpp:192 |  |
| S10-27 | V p.27 | Effects chain of 9 effects, order | DONE | `fx.order.*`; FxChainPanel.cpp:65 |  |

### S11 Serum 2: modulation (p.183-214)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S11-01 | S p.183-186 | Envelopes: delay, attack, hold, decay, sustain, release, curves (6 envelopes vs Serum's 4) | DONE | `envN.*` (9 keys x 6); t_vital_audio `matrix_envelope_on_the_cutoff_opens_the_filter` :564; EnvelopePanel.cpp:299 |  |
| S11-02 | S p.184-186 | Envelope zoom lock, drag points, magnifier | DONE | EnvCurveView.cpp; ui_lfo `uil_envelope_zoom_only_while_unlocked_and_eq_pinch_sets_the_resonance` |  |
| S11-03 | S p.185 | Envelope time units ms / BPM, beat grid | DONE | `envN.units`, `envN.*_beats`; t_wp123 `envelope_beat_units_follow_the_host_tempo` :432 |  |
| S11-04 | S p.185 | Envelope Legato Inverted | DONE | `envN.legato_inverted` (PT:291); t_wp123 :471 |  |
| S11-05 | S p.187, 194 | Assign an envelope / LFO to a control by drag (halo, destination count, tooltip) | DONE | ui_render `uir_assign_by_touch_through_controls`; ui_gaps `uig_envelope_tabs_select_long_press_and_drop_on_a_control`, `uig_depth_halo_badge_is_a_target` |  |
| S11-06 | S p.188 | LFOs 1-10 (+ 4 random LFOs), tiles | DONE | `lfo1..10.*`, `rlfo1..4.*`; t_wp123 `lfo_9_and_10_run_and_modulate` :170; LfoTabsPanel.cpp:174 |  |
| S11-07 | S p.189-190 | LFO graph tools: point, flat, ramp up / down, steps, brush, marquee, snap, proportional move | DONE | LfoShapeEditor.cpp; ui_lfo `uil_shape_editor_edits_the_point_list_through_gestures`; ui_gaps `uig_lfo_steps_tool_draws_one_step_per_grid_column`, `uig_lfo_brush_tool_paints_the_chosen_brush`, `uig_lfo_marquee_select_and_proportional_move`; engine `lfoN.shape` = Custom (kCapCustomLfoShape) t_wp123 :559 |  |
| S11-08 | S p.191, V p.19 | LFO SHAPE menu: load a preset, save a user shape, favourites | DONE (WP-01) | `Presets` button = the 12 built-in shapes as point lists (LfoShapeEditor) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S11-09 | S p.191, V p.21-22 | LFO TYPE Normal, S&H, Chaos Lorenz, Chaos Rossler | DONE | `rlfoN.style` Perlin / S&H / Sine interpolate / Lorenz / Rossler (PT:kRandomStyles); t_wp123 `random_lfo_rossler_attractor_style` :404 |  |
| S11-10 | S p.191 | LFO type Path | DIFFERENT | dropped: the manual does not describe it (12-decisions Q17) |  |
| S11-11 | S p.191, V p.19-20 | LFO MODE FREE / RETRIG / ENVELOPE (+ sustain env, loop point, loop and hold) | DONE | `lfoN.sync_type` (6 modes, PT:kLfoSyncTypes) |  |
| S11-12 | S p.191-192 | LFO MONO, DIRECTION, HOST | DONE | `lfoN.mono`, `.direction`, `.host`; t_wp123 :314, :340, :362; t_lfovoice :384-441 |  |
| S11-13 | S p.192 | LFO GRID (x, y), snap | DONE | LfoShapeEditor grid fields; ui_gaps `uig_lfo_selected_point_bar_loop_start_segment_fields_grid_all_curves` |  |
| S11-14 | S p.192 | LFO BPM / HZ, TRIP / DOT, RATE, 10x range | DONE | `lfoN.rate` 0.01 .. 200 Hz (PT:226), `.sync`, `.division`; the 200 Hz maximum covers the 10x switch |  |
| S11-15 | S p.192 | LFO swing through the RATE menu (one global SWING amount + division) | DIFFERENT | per-LFO amount on a fixed grid (14 row 28, `lfoN.swing`, t_wp_voiceglb `lfo_swing_delays_the_second_slot_of_every_pair` :445) | owner question open |
| S11-16 | S p.192-193 | LFO RISE, DELAY, SMOOTH, PHASE | DONE | `lfoN.fade_in`, `.delay`, `.smooth`, `.smooth_time`, `.phase` |  |
| S11-17 | S p.193 | LFO PHASE Snap to Grid | MISSING | none | small UI item |
| S11-18 | S p.193 | LFO editor on a large canvas | DONE | LfoShapeEditor.cpp (full width on tablet); UI-FOLLOW-REPORT |  |
| S11-19 | S p.189 | Copy all LFO settings by dragging a tab | DONE | ui_lfo `uil_drag_a_chip_onto_another_copies_the_lfo` |  |
| S11-20 | S p.195-196 | Modulator depth, negative depth, halo | DONE | `modNN.amount`, `.bipolar`; ModSourceBar.cpp; ui_gaps `uig_depth_halo_badge_is_a_target` |  |
| S11-21 | S p.196-198 | Copy a wavetable frame to an LFO / an LFO shape to a wavetable | MISSING | none (grep in shell-juce/ui: none) | G52; the wavetable half needs X01-01 |
| S11-22 | S p.198-200 | Modulate LFO points (X / Y) by menu or drag | DONE | `content.lfo_shapes[].points[].mod_x/mod_y`; t_lfovoice :140-340, t_wp123 :559; ui_lfo `uil_point_mod_reaches_the_engine_the_render_differs` |  |
| S11-23 | S p.199-201 | Control context menu: Mod Source submenu, bypass, remove, remove all, reset, MIDI learn, lock | DONE | Sheet.cpp:57-222; ui_render `uir_sheet_value_entry`; ui_shellfeat `shf_midi_learn_*` |  |
| S11-24 | S p.199 | Context menu: Aux Source submenu | DONE (GROUPB WP-08) | tap a modulation row of the control sheet: `Sheet.cpp:41` (`showConnectionMenu`) |  |
| S11-25 | S p.199, 211 | Context menu: Edit Custom Curve (CRV of a connection) | DONE (modamt B20 + GROUPB WP-08) | `Sheet.cpp:41` -> `UiContext::ViewState::openSlotCurve` (`Editor.cpp:46`), `ModCurvePanel::showModSlotCurve`; test `uim_wp08_rise_fall_sliders_write_the_slot_record_and_the_sheet_opens_the_curve` |  |
| S11-26 | S p.202-205 | Velocity and Note graphs, Legato (portamento time), Init Graph | DONE | `curve.velocity.legato`, `curve.note.legato`, `content.curves`; t_wp123 `velocity_and_note_curves_and_their_legato_glide` :498; ModVelNotePanel.cpp:310 |  |
| S11-27 | S p.206-208 | Macros 1-8: assign, swap, macros as destinations | DONE | `macro.1..8`; t_wp123 `macros_5_to_8_are_sources_and_destinations` :140; ui_gaps `uig_macro_chips_swap_by_drag` |  |
| S11-28 | S p.208, 210 | Oscillators (A, B, C, Noise, Sub) and Filter 1 / 2 outputs as modulation sources | DONE (GROUPB WP-06) | modSources 43 .. 49 (`SourceTap`, `synth_voice_handler.cpp:365`); `every_sound_source_moves_a_destination` | the held last sample of the block (14 row 79) |
| S11-29 | S p.210 | Matrix-only sources: Active Voices | DONE (GROUPB WP-06) | modSource 50; `active_voices_alt_rand_fixed_release_velocity_and_expression_x` |  |
| S11-30 | S p.210 | Matrix-only sources: Note-On Alt 1 / 2 | DONE (GROUPB WP-06) | modSources 51, 52 (`voice_handler.cpp` `prepareVoiceValues`); same test |  |
| S11-31 | S p.210 | Matrix-only sources: Note-On Rand 1 / 2 and Rand (Discrete) | DONE (GROUPB WP-06) | `random` (Rand 1), modSources 53 (Rand 2) and 54 (Discrete, a number per slot, `sourceFor`); same test |  |
| S11-32 | S p.210 | Matrix-only sources: Release Velocity | DONE (GROUPB WP-06) | modSource 55 = the output of `lift` (the note-off velocity); same test (a hard release against a soft one) |  |
| S11-33 | S p.210 | Matrix-only sources: Voice Index, Voice Mod 1 / 2 | DONE (GROUPA2 WP-07; Active Voices stays WP-06) | modSources 40 .. 42 (`voice_index`, `voice_mod1`, `voice_mod2`, `kCapVoiceSources`); `voicecontrol_voice_index_and_voice_mods_are_modulation_sources`; Serum ids 56 / 57 / 58 map to them | |
| S11-34 | S p.210 | Expression / MPE X / Y / Z as sources | DONE (GROUPB WP-06) | `slide` (Y), `poly_aftertouch` (Z), `lift`, `oct_note`, and modSource 56 Expression X (the per note bend over 48 semitones) |  |
| S11-35 | S p.211 | Sources Mod Wheel, Aftertouch, Poly Aftertouch, Pitch Bend | DONE | modSources 23-28, 35-38 (docs/params.json); t_vital_audio `matrix_macro_velocity_and_mod_wheel_are_sources` :586 |  |
| S11-36 | S p.211 | Source Fixed (constant 1 with depth slider) | DONE (GROUPB WP-06) | modSource 57; same test |  |
| S11-37 | S p.209 | 64 matrix slots, source / amount / polarity / stereo / destination / bypass | DONE | `modNN.*` (PT:301); t_vital_audio :635, :682; t_vital_params `modulation_slots_are_wired_and_unwired` :461; ModMatrixPanel.cpp:843 |  |
| S11-38 | S p.211 | CRV curve of the source, editable curve | DONE (modamt B20) | `content.mod_curves`, `ModCurvePanel` Custom, `RemapEditor` mod curve mode; RISE / FALL beside it (GROUPB WP-08) |  |
| S11-39 | S p.211, V p.25-26 | RISE / FALL slew limiter on the source (curve editor) | DONE (GROUPB WP-08) | `content.mod_curves[].rise / .fall` (0 keys), `slewStep` (`modulation_connection_processor.cpp:72`); `test_modslew.cpp`; sliders `ModCurvePanel.cpp:320` | the 128 key proposal not built (14 row 81) |
| S11-40 | S p.211-212 | AUX SOURCE, INV, aux CRV, OUTPUT | DONE | `modNN.aux_source/inv/power/output`; t_wp123 `aux_source_multiplies_the_slot` :266 |  |
| S11-41 | S p.212-213 | Matrix expand, move row, bypass, remove | DONE | ui_gaps `uig_mod_swap_slots_model_and_matrix_row_handle`; ModMatrixReorder.h |  |
| S11-42 | S p.214 | Matrix ops: sort by source / destination, vibrato, Velo->Amp, bake macros, init | DONE | ModMatrixPanel.cpp:735-765; ModLogic.h:386; ui_model `ui_mod_snapshot_and_slot_editing` |  |
| S11-43 | S p.214 | Matrix op: Lock Matrix (keep assignments on preset change) | DONE (GROUPA2 WP-02) | lock name `matrix` (`modNN.*` + `content.mod_curves`); `ModMatrixPanel.cpp:785`; same test as S15-13 | |
| S11-44 | V p.24-25, audit G1 | Modulation of another connection's amount (`X -> modulation_N_amount`) | IN-PROGRESS | G1, worker mod-amount (B20): 52 connections in 17 of 35 real presets |  |
| S11-45 | V p.23, audit G4 | Modulation source `stereo` | IN-PROGRESS | G4, worker mod-amount (B20) |  |
| S11-46 | audit G3 | Modulation of portamento time, polyphony, stereo routing | IN-PROGRESS | G3, worker mod-amount (B20) |  |
| S11-47 | V p.16 | Live modulation display: cyan value line on knobs, LFO / envelope playheads, random-LFO history, hover audition | DONE (GROUPA2 WP-17; hover audition stays out) | `LiveValues` (readout.h), `updateReadout` (`vital_engine.cpp:1271`), `LiveBus.cpp:172`, the knob mark (`Overlays.cpp:115`), `LfoPanel.cpp:268`, `EnvCurveView.cpp:55`; tests `synthyboy_test_vital_readout`, `content_live_bus_...` (`test_ui_content.cpp:1740`), `content_a_modulated_knob_...` (`:1816`), `content_lfo_displays_...` (`:1873`), `content_envelope_view_...` (`:1949`) | hover audition (a touch UI has no hover) |

### S12 Serum 2: voicing, keyboard, clips, arp (p.215-273)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S12-01 | S p.216, V p.39 | Voicing: Mono, Legato, Poly (voice count) | DONE | `master.polyphony`, `master.legato`; t_vital_audio `voices_polyphony_parameter_and_stealing` :1063, `voices_legato_does_not_restart_the_envelope` :1098; MasterVoicingPanel.cpp:41 |  |
| S12-02 | S p.216, V p.43 | Voice steal priority (Newest, Oldest, Highest, Lowest, Velocity), same-note Steal / Stack / Ignore | DONE | `master.voice.steal_priority`, `.same_note`; t_wp_voiceglb :143, :198; GlbVoicePanel.cpp:37 | Vital's 'Round Robin' is not a separate choice |
| S12-03 | S p.216 | Poly legato (paraphonic) | DONE | `master.poly_legato`; t_wp_voiceglb :237, :269 |  |
| S12-04 | S p.217-218 | Portamento: Porta, Curve, Always, Scaled | DONE | `master.portamento.*`; t_vital_audio `voices_portamento_reaches_ninety_percent_in_ninety_percent_of_the_time` :1118; GlbPortamentoPanel.cpp:303 |  |
| S12-05 | S p.219-243 | Clips (module, banks, piano roll, recording, MIDI out, macros) | EXCLUDED | Kick (grid / sequencer) |  |
| S12-06 | S p.244-266 | Arpeggiator (module, patterns, graph editor, playback, retrigger, velocity) | EXCLUDED | Kick |  |
| S12-07 | S p.267 | Keyboard Transpose | DONE | `master.transpose` |  |
| S12-08 | S p.268 | Key and Scale | DONE | `master.scale.*` (PT:568); t_wp123 `key_scale_conforms_notes_before_the_allocator` :640; GlbKeyScalePanel.cpp:258 |  |
| S12-09 | S p.269 | Keyboard Swing (grid swing) | EXCLUDED | Kick grid swing; LFO swing see S11 |  |
| S12-10 | S p.269-273 | Oscillator Mapping: key / velocity ranges, FOLD, WARP | DONE | `oscN.map.*` (PT:306); t_wp123 `oscillator_mapping_gates_folds_and_keeps_modulating` :891; GlbOscMapPanel.cpp:430 |  |
| S12-11 | S p.314, 325 | Sustain pedal, pitch bend, mod wheel, channel pressure input | DONE | ct_events `sustain_pedal_keeps_released_notes_sounding` :126; t_vital_audio `voices_pitch_bend_range` :1152 |  |
| S12-12 | V p.39 | Pitch bend range (and separate down range) | DONE | `master.bend_range`, `.bend_down_range`, `.bend_down_link`; t_wp_voiceglb :321 |  |

### S13 Serum 2: wavetable editor and import (p.274-303)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S13-01 | S p.274-275 | Editor thumbnails, select, reorder | DONE (WP-01) | core: wtedit model (core/wavetable-edit, 256 frames); UI: WavetableEditorSheet thumbnails; ui_gaps `uig_wavetable_thumbnail_reorder_swipe_close` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-02 | S p.276-277 | Drawing tools (flat, sine, slopes, curves, half sines, interpolate, nudge, noise, mirror), grid | DONE (WP-01) | core drawing_tools.cpp; WaveCanvas (WavetableEditorSheet.cpp:21); ui_gaps `uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-03 | S p.278-280 | FFT area (draw amplitude / phase) and FFT menu | DONE (WP-01) | core frame_ops.cpp `fft*`; SpectrumCanvas (WavetableEditorSheet.cpp:225); ui_gaps `uig_wavetable_fft_bin_entry_and_nudge_all_phases` ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-04 | S p.281-283 | Frame management: copy / paste, insert / remove, sort | DONE (WP-01) | core frame_ops.cpp; ui_wavetable `ui_wavetable_editor_sheet_operations`; WavetableEditorSheet.cpp:1723 ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-05 | S p.284, 304-311 | Formula parser field in the editor | DONE (WP-01) | core/formula (GRAMMAR.md, 21 + 4 functions); FormulaView (WavetableEditorSheet.cpp:707-735) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-06 | S p.285-288 | Single / All process menus (normalize, flip, fade, filter, blur, PWM, set spectra, ...) | DONE (WP-01) | core frame_ops.cpp; ProcessView (WavetableEditorSheet.cpp:510) with search ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-07 | S p.289 | Morph menu | DONE (WP-01) | core `interpolateBetweenKeyframes`; WavetableEditorSheet.cpp:1740 ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-08 | V p.8 | Vital component / keyframe model: Wave Source, Line Source, Phase Shift, Wave Window, Frequency Filter, Slew Limiter, Wave Folder, Wave Warp | PARTIAL | core wtedit::vital (vital_model.cpp); UI lists and adds components (WavetableEditorSheet.cpp:940-1000) | no per-component parameter editing, no Line Source / Audio File Source UI, no keyframe editor; the render has no path to the oscillator |
| S13-09 | S p.285 | Import a wavetable file (WAV) | DONE (WP-01) | UI imports a synthetic 2 s saw sweep: WavetableEditorSheet.cpp:1298-1330 ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-10 | S p.291-298 | Import audio as wavetable: multi-cycle, advanced, FFT / vocoded, single-cycle, import menu | DONE (WP-01) | core audio_import.cpp (modes PitchAverage .. Fft2048); ImportJob worker thread (:1006) with progress ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-11 | S p.300-301 | Import an image as a wavetable | DONE (WP-01) | core image_import.cpp ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-12 | S p.285-288 | Export (WAV 16 / 32 bit, single cycle, all / single) | DONE (WP-01) | core wav_export.cpp ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-13 | S p.290, 302-303 | Save wavetable, Embed in preset, Missing Files dialog | DONE (WP-01) | menu items `Save Table` / `Embed in Preset` only clear the modified flag (WavetableEditorSheet.cpp:2068-2069) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S13-14 | S p.301 | Animate WT POS with an LFO after import | DONE | matrix: `lfoN` -> `oscN.frame` |  |
| S13-15 | S p.315 | Mod wheel scans WT POS while the editor is open | MISSING | none | preference of S15-05 |
| S13-16 | S p.324 | Render OSC Warp (current frame -> 256 frames across WARP) | MISSING | none | G53; needs offline warp render + X01-01 |
| S13-17 | S p.324 | Resample (play one bar, capture as wavetable) | MISSING | none | G53 / G03; needs an offline render API |

### S14 Serum 2: formula parser (p.304-311)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S14-01 | S p.304-306 | Formula grammar: functions, binary operators, constants and variables, q FFT mode | DONE | core/formula (GRAMMAR.md, REPORT.md, formula.cpp bytecode VM); core/formula/tests | manual ambiguities listed `[S unclear]` in GRAMMAR.md (precedence of && / \|\|, % and ?:) |
| S14-02 | S p.284 | Formula applied to the editor's table | DONE (WP-01) | applyFormula (WavetableEditorSheet.cpp:719) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S14-03 | S p.307-308 | Formula presets menu, saving own formulas, manual file management | DONE (WP-01) | FormulaView has a text field, Apply, Clear (WavetableEditorSheet.cpp:707-735) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| S14-04 | S p.309-311 | Formula examples | DONE (WP-01) | none in the UI ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 

### S15 Serum 2: global settings, voice control, quality, tuning (p.312-322)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S15-01 | S p.313 | Preferences: help tooltips, parameter value bubbles | DONE | Editor.h:74; ParamControls.cpp:152 ValueBubble |  |
| S15-02 | S p.313 | Preference: double-click resets or opens a value box | DONE (GROUPA2 WP-02) | `Preferences::doubleClickResets` (preferences.h:16); `ParamControls.cpp:414` (reset or `showValueBox`); test `content_preferences_change_how_the_controls_behave` (`test_ui_content.cpp:1112`) | the dead toggle of GlbSettingsPanel is replaced by the 14 preference toggles |
| S15-03 | S p.314 | Preferences: mouse-wheel parameter control, keyboard shortcuts on/off, default waveform view 2D / 3D | DONE (GROUPA2 WP-02) | `mouseWheel`, `keyboardShortcuts`, `waveform3d`; `ParamControls.cpp:457`, `Editor.cpp:191`, `OscWaveView.cpp`, `WavetablePanel.cpp`; tests `content_preferences_*` (`test_ui_content.cpp:1071`, `:1112`, `:1237`) | |
| S15-04 | S p.314 | Preferences: MPE enabled by default, pitch bend also maps to Expr X, Expr Y bipolar | DONE (GROUPA2 WP-02) | `mpeByDefault`, `mpeBendToX`, `mpeYBipolar` set the keys of a new instance (JUCE `PluginProcessor.cpp` constructor, native `synthyboy_clap.cpp`); tests `content_a_new_instance_takes_the_mpe_preferences_unless_the_user_saved_a_default` (`test_ui_content.cpp:1672`), `native_clap_preferences_mpe_defaults_default_midi_map_and_preset_changes` | a saved default patch wins (A383) |
| S15-05 | S p.314-315 | Preferences: limit mod depth on drop, mod wheel -> WT POS, load MIDI map from presets, ultra quality render, update check | DONE (GROUPA2 WP-02; the update check is n/a) | `limitModDepth` (`ModModel.cpp:203`), `modWheelToWtPos` (`WavetableEditorSheet.cpp`), `loadMidiMapFromPresets` (`PluginProcessor.cpp:104`), `ultraRender` (`IEngine::setUltraRender`); tests `content_preferences_limit_the_mod_depth_...` (`test_ui_content.cpp:1237`), `content_midi_map_files_default_map_and_the_map_of_a_preset` (`:1352`), `content_the_juce_shell_bounces_at_ultra_quality_when_the_preference_is_on` (`:1644`), `synthyboy_test_vital_silence` `ultra_render_is_the_4x_oversampling_of_the_quality_option` | WP-15 consumes the same flag for the offline render |
| S15-06 | S p.315 | Preference: silence notes and FX tails when the host stops | DONE (GROUPA2 WP-02) | `IEngine::silence ()` (`vital_engine.cpp:541`), `TransportStopWatch`, both shells (`PluginProcessor.cpp:412`, `synthyboy_clap.cpp:351`); tests `silence_ends_the_notes_and_flushes_the_tails_at_once`, `content_the_juce_shell_silences_notes_and_tails_when_the_host_transport_stops` (`test_ui_content.cpp:1597`), `native_clap_silences_notes_and_tails_when_the_host_stops` | |
| S15-07 | S p.313-315, V p.42 | Frequency units option Hz / semitones | DONE (GROUPA2 WP-02) | `freqSemitones`; `displayText (info, plain, semitones)` / `valueText` (`ControlModel.cpp`, `UiContext.h`), value box `Sheet.cpp`; test `content_preference_frequency_units_show_and_take_semitones` (`test_ui_content.cpp:1205`) | semitones from middle C, cutoff-type readouts only (A382) |
| S15-08 | S p.316-317 | Voice control: select oscillators, load / init a configuration, 1-8 step sequence of per-voice values | DONE (GROUPA2 WP-07) | `voicectl.length`, `.sN.{pan,detune,cutoff,env,mod1,mod2}`, `.osc.{a,b,c,noise,sub}` (B26, PT:2063-2124); `voice_handler.cpp:218`; `GlbVoiceControlPanel.h:97`; tests `synthyboy_test_vital_voicecontrol` (10 cases), `content_voice_control_panel_steps_length_init_presets_and_style_file` (`test_ui_content.cpp:940`) | |
| S15-09 | S p.318 | Voice control randomization: PAN, DETUNE, CUTOFF, ENVS | DONE (GROUPA2 WP-07) | `voicectl.rand.*` (+ the x10 switch); `voicecontrol_randomisation_is_stable_per_note_and_differs_between_notes` | |
| S15-10 | S p.318 | Voice control scaling of all envelope and LFO times | DONE (GROUPA2 WP-07) | `voicectl.scale.env`, `.scale.lfo`, `.scale.lfo_mode`; `voicecontrol_envelope_time_per_voice_and_scaling_of_all_envelopes`, `voicecontrol_lfo_scaling_by_percent_and_by_rate` | |
| S15-11 | S p.319 | Voice control save / presets | DONE (GROUPA2 WP-07) | user library category `voice_control` (`serializeVoiceControlPreset`, `library_items.cpp`), the Presets button of the panel (`GlbVoiceControlPanel.h:279`), `.SerumStyle` import (18 files, 0 dropped); tests `library_item_voice_control_holds_the_62_keys_and_applies_a_complete_configuration`, `serum_style_files_are_complete_voice_control_configurations` | |
| S15-12 | S p.319 | Quality Draft / High / Ultra (1x / 2x / 4x) | DONE | tiers (IEngine::setQuality) + `quality.oversample_4x`; t_wp_voiceglb `oversample_4x_changes_the_aliasing` :534; GlbQualityPanel.cpp:254 | switching 4x ends sounding notes (14 row 29) |
| S15-13 | S p.319 | Lock the quality across preset loads | DONE (GROUPA2 WP-02) | lock name `quality`; the Quality panel button (`GlbQualityPanel.cpp:30`); test `content_locks_of_the_global_settings_matrix_quality_tuning_survive_a_preset_change` (`test_ui_content.cpp:1458`) | |
| S15-14 | S p.319 | S1 Compatibility Mode (Serum 1 presets) | MISSING | none | no Serum preset importer exists; owner decision (probably drop) |
| S15-15 | S p.320 | Disable parameter smoothing | DONE | `quality.no_smoothing`; t_wp_voiceglb `no_smoothing_makes_a_parameter_jump_instant` :582 |  |
| S15-16 | S p.320 | Concert pitch A4 | DONE | `master.tuning.a4` (PT:571); t_wp123 `tuning_concert_pitch_scales_user_file_and_mts_esp` :673; GlbTuningPanel.cpp:341 |  |
| S15-17 | S p.320 | Tuning files .tun (+ .scl / .kbm), clear tuning | DONE | core/tuning; `master.tuning.scale`; GlbTuningPanel.cpp:255 file chooser; core/tuning/test_tuning.cpp |  |
| S15-18 | S p.321 | MTS-ESP tuning (enable, note-on only), .tun takes precedence | PARTIAL | `master.tuning.mts_esp`, `.mts_note_on_only`; core/include/synthyboy/tuning.h seam; fake source in t_wp123 :673 | libMTSClient not linked, licence not verified (14 row 5) |
| S15-19 | S p.321 | Lock the tuning configuration | DONE (GROUPA2 WP-02) | lock name `master.tuning` (keys + tuning table); the "Tuning Lock" button (`GlbTuningPanel.cpp:130`); same test | |
| S15-20 | S p.322 | Build version and date | DONE (GROUPA2 WP-02) | `UiHost::aboutText ()` (CMake version `SYNTHYBOY_VERSION_STRING`, `__DATE__`, engine, parameters, packs); test `content_about_line_comes_from_the_build` (`test_ui_content.cpp:1338`) | the hard-coded string is gone |
| S15-21 | S p.325-326 | MPE: enable, XYZ -> Macro 1 / 2 / 3, YZ -> Macro 1 / 2, Y -> Mod Wheel, bend range 1-96 | DONE (GROUPA1 WP-09) | `master.mpe.on`, `.bend_range`, `.xyz_macro`, `.y_modwheel`, `.bend_to_x`, `.y_bipolar` (B24, PT:2057-2060); vital_engine.cpp `mpeAxis`; t_mpe (5 cases); GlbMpePanel.cpp:197 | closed by GROUPA1: docs/ui-design/GROUPA1-REPORT.md; the encoding of Serum's `mpeConfig` is unknown (14 row 67) |

### S16 Serum 2: appendices (p.323-354)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| S16-01 | S p.323-324 | Main menu: Init Preset, Init LFOs, Init Modulations, Revert to Saved, Save as Default | DONE | GlbSettingsPanel.cpp:27-48, 160-220; ui_gaps `uig_settings_buttons_act_and_confirm_with_a_toast`; ct_shell_features `shell_user_default_round_trip` :282 |  |
| S16-02 | S p.324 | Main menu: Load Preset, Import Preset Pack, folders, rescan | EXCLUDED | Kick |  |
| S16-03 | S p.324 | Main menu: Render OSC Warp, Resample | MISSING | none | see S13 (G53) |
| S16-04 | S p.325 | Main menu: Load / Save MIDI Map files, default map | DONE (GROUPA2 WP-02) | `serializeMidiMap` / `parseMidiMap` / default map file (`preferences.cpp`), the four buttons of the settings (`GlbSettingsPanel.h`), `.SerumMIDIMap` and preset `midiMap` import (macros); tests `midi_map_files_default_map_and_serum_ids`, `content_midi_map_files_default_map_and_the_map_of_a_preset` (`test_ui_content.cpp:1352`), `serum_midi_cc_maps_of_presets_and_map_files_become_bindings` | Serum ids of other modules than the macros are not known (question 74) |
| S16-05 | S p.325 | Main menu: Load Tuning (.tun) | DONE | GlbTuningPanel.cpp:255 |  |
| S16-06 | S p.340-343 (C) | Preferences file: default artist; preset change by MIDI / notes | EXCLUDED | Kick preset browser |  |
| S16-07 | S p.344-345 (D) | Serum file structure | EXCLUDED | not a function |  |
| S16-08 | S p.346-347 (E) | Creating wavetables (help text) | MISSING | none | help text in the import sheet (small) |
| S16-09 | S p.348 (F) | Optimizing: CPU tiers, unison | DONE | tiers; t_vital_params `quality_tiers_cap_without_touching_the_parameters` :308 |  |
| S16-10 | S p.349-353 (G) | Keyboard shortcuts | PARTIAL (GROUPA2 WP-02: the set that makes sense is built) | Cmd/Ctrl-click reset, Shift-drag fine, Shift-Alt-click polarity (`ParamControls.cpp:293`, `:306`), Alt-click filter display, undo / redo; gated by the preference; the rest (preset browser, clips, piano roll, Option-F, Renders folder) is Kick or not applicable, see 13-new-keys 4.31 | |

### V01 Vital manual (unofficial, 43 pages as cited)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| V01-01 | V p.3 | Sizing, general navigation | DONE | Theme scale; page tabs |  |
| V01-02 | V p.3-4 | Presets: browse, save, export, import / export bank | EXCLUDED | Kick |  |
| V01-03 | V p.4 | Open an external `.vital` preset, import a `.vitalbank` | PARTIAL | core/vital-import + tools/vital-import CLI (`synthyboy_vital_import`) | not reachable from the plugin: no file chooser, no content store (I01-16) |
| V01-04 | V p.4 | Init preset, parameter adjustments (drag, ctrl fine, alt value entry) | DONE | see S01 |  |
| V01-05 | V p.5 | Voice: 3 oscillators + sampler + 2 filters | PARTIAL | 3 osc + 2 filters; sampler = noise sample slot | Vital sampler with own audio file missing (S04, I01-07) |
| V01-06 | V p.6 | Oscillator activator, level, pan | DONE | `oscN.on/level/pan` |  |
| V01-07 | V p.6-7 | Pitch semitone / cents, transpose snap keyboard, global snap | DONE | `oscN.transpose_snap` (VB:161), bit 12 global snap; TransposeControl.cpp:218; ui_gaps `uig_pitch_mode_and_transpose_snap_keyboard`; t_pitchuni `transpose_snap_quantizes_transpose` :160 |  |
| V01-08 | V p.7 | Routing options F1 / F2 / both / FX / direct | DONE | `oscN.destination` |  |
| V01-09 | V p.8 | Wavetable editor | PARTIAL | see S13 |  |
| V01-10 | V p.8-9 | Unison and phase settings | DONE | see S02 |  |
| V01-11 | V p.10 | FM / RM from another oscillator or the sampler | PARTIAL | FM / RM from Osc N | from the sampler / noise missing (S02, G29) |
| V01-12 | V p.13-15 | Filters: types, blend, keytrack, routing, formant, comb, phaser | DONE | see S08 |  |
| V01-13 | V p.16 | Modulation overview, cyan live display | DONE (GROUPA2 WP-17) | depth halo + live mark | see S11-47 |
| V01-14 | V p.17-18 | Envelopes (env 1 mapped to volume by default) | DONE | see S11; default patch maps env1 to amp |  |
| V01-15 | V p.18-20 | LFOs: shape chooser, brush, grid, phase, mode, frequency, smooth, delay, stereo | DONE | `lfoN.stereo`, see S11 |  |
| V01-16 | V p.21-22 | Random LFOs: Perlin, S&H, Sine interpolate, Lorenz | DONE | `rlfoN.*` |  |
| V01-17 | V p.22-23 | Sources Note, Velocity, Random, Lift, Slide, Pressure, Oct Note | DONE | modSources 23-38 |  |
| V01-18 | V p.23 | Source Stereo | IN-PROGRESS | G4, worker mod-amount |  |
| V01-19 | V p.23 | Macros 1-4 (we have 8), pitch wheel, mod wheel | DONE | `macro.1..8`; Strip.cpp wheels |  |
| V01-20 | V p.24-25 | Modulation list: bypass, bipolar, stereo, amount, power | DONE | `modNN.*` |  |
| V01-21 | V p.25-26 | Modulation remap curve (editable, RISE / FALL not in Vital) | IN-PROGRESS | G2, worker mod-amount (B20) |  |
| V01-22 | V p.27-38 | Effects chain: Chorus, Compressor, Delay, Distortion, EQ, Filter, Flanger, Phaser, Reverb | DONE | see S10 |  |
| V01-23 | V p.39 | Global: voices, bend, legato, portamento, velocity track | DONE | `master.*` |  |
| V01-24 | V p.40 | Oscillator advanced: note tracking | DONE | `oscN.pitch_track`, `.fixed_pitch`; t_pitchuni :131 |  |
| V01-25 | V p.40 | Oscillator advanced: Hi-Resolution Wavetable | PARTIAL | see S02 | tiny |
| V01-26 | V p.40-41 | Unison stack options, detune range, blend, stereo unison, spread knobs | DONE | see S02 |  |
| V01-27 | V p.40 | Spectral unison switch | DIFFERENT | fixed on in the engine (VITAL-IMPORT-AUDIT section 4: all 35 files use the default) |  |
| V01-28 | V p.42 | Oscilloscope, spectrum analyzer | DONE | GlbScopePanel.cpp:207; ui_gaps `uig_voice_badge_cpu_pill_and_scope` |  |
| V01-29 | V p.42 | Oversampling | DONE | tiers + `quality.oversample_4x` |  |
| V01-30 | V p.42 | Frequency units option | PARTIAL | see S15 (dead toggle) |  |
| V01-31 | V p.43 | Voice settings: note priority | DONE | `master.voice.steal_priority` | no distinct Round Robin |
| V01-32 | V p.43 | Tuning (Pythagorean, Just, default, user files), cents, transpose | DONE | `master.tuning.scale` (12-TET / Pythagorean / Just / User), `master.tune`, `master.transpose` |  |

### I01 Vital import fidelity and import entry points

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| I01-01 | audit G1 | Mod amount as destination | IN-PROGRESS | worker mod-amount (B20) |  |
| I01-02 | audit G2 | Per-connection curve (`line_mapping`) in the importer | IN-PROGRESS | worker mod-amount (B20) |  |
| I01-03 | audit G3 | Modulate portamento time / polyphony / stereo routing | IN-PROGRESS | worker mod-amount (B20) |  |
| I01-04 | audit G4 | Source `stereo` | IN-PROGRESS | worker mod-amount (B20) |  |
| I01-05 | audit G5 | `stereo_routing` (5 presets), `voice_override` (1 preset) | IN-PROGRESS | worker mod-amount (B20): keys + bindings |  |
| I01-06 | audit G6 | `osc_N_transpose_quantize` | IN-PROGRESS | worker mod-amount task text asks for a key, but `oscN.transpose_snap` exists since B14 and is bound to `transpose_quantize` (VB:161): the worker must reuse it and only add the importer mapping | duplicate-key risk, tell the lead |
| I01-07 | audit G7 | Sample as a 4th source (5 of 35 presets lose their sample); Vital warp 'from sample' | PARTIAL (GROUPA1 WP-04) | a Vital sample is the sample of the noise source (`noise.type` = Sample, osc 4): 7 samples of the 35 presets kept, 0 dropped; vi_fixture_sample_imported_to_the_noise_source | the warp 'from sample' (`sample_input`) has no home until WP-06 |
| I01-08 | audit G8 | Stored `mod_wheel` position (23 files non-zero) | DECIDED (GROUPB WP-18) | not applied (A410) | the A/B needs Vital (14 row 82) |
| I01-09 | audit G9 | Single-frame `Init` and phase-shifted `Sine` tables stored as content instead of a built-in | DONE for `Init` (GROUPB WP-18) | `oscN.wavetable` = 14 "Saw Down" (cap bit 63), `matchBuiltinSingleCycle`; test `vi_single_cycle_shortcut_needs_the_same_waveform`; 34 stored tables fewer in the audit set | the shifted `Sine` stays stored (a shift is not a built-in) |
| I01-10 | audit 4 | Importer: `detune_range` -> `oscN.unison.range`, `midi_track` -> `oscN.pitch_track`, `voice_priority` -> `master.voice.steal_priority`; `spectral_unison` | DONE (GROUPB WP-18; `spectral_unison` stays the engine's fixed value) | `vital_import.cpp` (the direct rows), test `vi_detune_range_midi_track_and_voice_priority_are_imported`; audit 35 presets, 0 mismatches | A409 |
| I01-11 | 11 Q31 | Clean-room wavetable renderer for the 10 Vital component types | DONE | core/wavetable-render; audit: 105 of 105 tables, 0 silent | component formulas are our definitions (14 row 1, SPEC-GAP); needs A/B against real Vital |
| I01-12 | 11 Q5 | Stand-alone `.vitaltable`, `.vitallfo`, `.vitalbank` batch | DONE (WP-01) | tools/vital-import writes `.lfoshape.json` (14 row 25) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| I01-13 | 11 Q38 | Export as `.vital` | DIFFERENT | not a product feature (12-decisions: proposed Q38) |  |
| I01-14 | audit 7 | Imported presets that fail the factory render gate (8 of 35) | DIFFERENT | presets themselves are hot (VITAL-IMPORT-AUDIT section 7); gate is for factory presets, report only |  |
| I01-15 | 14 row 1 | Importer A/B against real Vital audio | OPEN (owner) | no Vital render exists in the repo | only listening can settle the SPEC-GAP list (`grep SPEC-GAP core/`) |
| I01-16 | I01-01 | Import entry points inside the plugin (file chooser, content store, batch) | DONE (WP-01) | importer is a CLI only (tools/vital-import); shell-juce / shell-clap do not link core/vital-import ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 

### X01 Cross-cutting: shell, content pipeline, platforms (found in this audit)

| ID | Ref | Function | Status | Evidence | Gap / note |
|----|-----|----------|--------|----------|------------|
| X01-01 | audit | Content store in the shells (user wavetables, samples, impulse responses, imported presets) | DONE (WP-01) | grep setContentStore in shell-juce, shell-clap: none; only tools/preset-render and tools/vital-import attach one (preset_render.cpp:96) ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| X01-02 | audit | Wavetable editor to engine link (open from the oscillator's table, apply, save, embed) | DONE (WP-01) | WavetablePanel.cpp:709-721 copies the panel's view table in and out; no UiHost call ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| X01-03 | audit | Sample / wavetable / IR file chooser flows (only the tuning panel has one: GlbTuningPanel.cpp:255) | DONE (WP-01) | none ; closed by WP-01: docs/ui-design/WP01-REPORT.md | 
| X01-04 | 14 row 11 | iOS ships which engine: engine-own has none of the Serum-class features | MISSING | engine-own sets 1 of the 43 cap bits (event contract) | owner decision (WP-17) |
| X01-05 | 14 row 29 | 4x oversample switch ends sounding notes (needs prepare) | DIFFERENT | accepted (14 row 29) |  |
| X01-06 | 14 row 13 | Velocity 0.25 -> 1.0 = 24.1 dB (squared law) | DIFFERENT | left as is until listening (14 row 13) |  |
| X01-07 | 09 | Kick remote-control pages | DONE | 118 pages in core/src/kick_pages.cpp, docs/kick-pages.json; ct_shell_clap `kick_remote_control_pages_and_reachability` :208 asserts all 1594 keys reachable; `clap.remote-controls` extension: native_clap_extensions_and_parameter_parity :122 |  |
| X01-08 | 09 | Every key has a control in the JUCE UI | DONE | ui_panels `uip_every_parameter_key_has_a_control` |  |
| X01-09 | audit | Quality: ASan / UBSan, TSan, fuzz smoke, pluginval, clap-validator on the final tree | PARTIAL | 14 rows 6, 7; ct_rt_alloc `process_does_not_allocate_*`; fuzz_smoke.cpp | clap-validator not re-run on the final tree (14 row 6) |
| X01-10 | audit | Params doc / presets in sync with the key table | DONE | scripts/check-params-doc.sh, scripts/gen-factory-presets.py --check (SWEEP-REPORT, VOICEGLB-REPORT) |  |

## 5. Appendix A - old gap list G01-G84 (10-coverage-matrix.md section 3)

| G | Gap | Status now | Evidence / rows |
|---|-----|-----------|-----------------|
| G01 | Macros 5-8 | DONE | `macro.5..8`, t_wp123 :140 |
| G02 | Save as Default Preset | DONE | S16, ct_shell_features :282 |
| G03 | Drag-out / share audio, resample, offline render | MISSING | S01-09, S13-17 |
| G04 | Embed a sample in the preset | MISSING | S01-08, X01-01 |
| G05 | Key + Scale, transpose-snap keyboard | DONE | S12, V01 snap; t_wp123 :640 |
| G06 | Oscillator mapping | DONE | S12; t_wp123 :891 |
| G07 | MIDI Learn / CC map | DONE | S01; omni 7 bit (14 row 23) |
| G08 | Lock parameter / module | DONE | S01; Lock Matrix and quality / tuning lock are separate rows (S11, S15) |
| G09 | Sustain pedal | DONE | ct_events :126 |
| G10 | MPE / per-note expression | PARTIAL | S15-21: macro / mod wheel mappings missing |
| G11 | Tuning A4, .scl / .kbm / .tun, MTS-ESP | PARTIAL | S15-16 .. S15-19: MTS-ESP client not linked |
| G12 | Voice steal priority, override, same-note | DONE | S12; t_wp_voiceglb :143, :198 |
| G13 | Poly legato | DONE | t_wp_voiceglb :237 |
| G14 | Separate bend up / down | DONE | t_wp_voiceglb :321 |
| G15 | Global stereo spread | DONE | t_wp_voiceglb :369 |
| G16 | Swing for synced LFOs | DIFFERENT | 14 row 28: per-LFO amount, no global amount / division |
| G17 | Per-voice control | DONE (GROUPA2 WP-07) | S15-08 .. S15-11 |
| G18 | 4x oversampling, no smoothing | DONE | t_wp_voiceglb :534, :582 |
| G19 | Multisample oscillator | MISSING | S03 |
| G20 | Sample oscillator | PARTIAL | engine DONE (t_wp4); no way to load a sample: S04-02, X01-01 |
| G21 | Granular oscillator | MISSING | S05 |
| G22 | Spectral oscillator | MISSING | S06 |
| G23 | Sub oscillator | DONE | S07; sub.phase separate (MANUALCHECK) |
| G24 | User wavetable selection | PARTIAL | engine DONE (t_wp4 :333); no store / list: S02-03, X01-01 |
| G25 | CRS continuous coarse pitch | DONE | `oscN.pitch_coarse` |
| G26 | Pitch modes Harmonics / Ratio / Step | DONE (GROUPB WP-05: Ratio SRC) | S01-19, S01-20 |
| G27 | Pitch tracking toggle, bend tracking | DONE | `oscN.pitch_track`, `.pitch_bend_track`; t_pitchuni :131, :254 |
| G28 | Routing blend, None, Bus 1 / 2 | DONE | S01 routing row; t_wp5 |
| G29 | Oscillator / filter outputs as sources | DONE (GROUPB WP-06) | S11, S02 (FM from Noise / Sub / Filters) |
| G30 | 256 frames, smooth interpolation switch | DONE (GROUPB WP-05) | S02 |
| G31 | Phase legato, phase memory | DONE | S02 |
| G32 | Unison laws, range, stack repeat, warp 2 spread, hi-res | DONE | S02 (hi-res PARTIAL) |
| G33 | Two free warp slots, Swap Warps | DONE | slot 2 (B09), Swap Warps (B21) |
| G34 | Warp modes Var, Bend +/-, Asym, Flip, Mirror, Remap 1-4, Odd/Even | DONE | warp-env (B21) |
| G35 | Warp Filter group, Distortion group | DONE | warp-env (B21) |
| G36 | Warp FM Thru-Zero / Exp / Linear, FM from sampler | PARTIAL | Thru-Zero / Exp / Linear DONE (B21); sources Noise / Sub / Filters stay MISSING (G29) |
| G37 | Warp PD and AM | DONE | warp-env (B21); sources Noise / Sub / Filters stay MISSING (G29) |
| G38 | Noise colours, STEREO / FILTER, own sample, loop, start, pitch | PARTIAL | colours / loop / pitch DONE; STEREO, FILTER, own sample MISSING |
| G39 | Mixer: ENV 1 toggle, buses, DIRECT, filter outputs | DONE | ENV 1 toggles (B21) |
| G40 | Wavetable name / modified, save / load / embed | MISSING | S13-13, X01-02 |
| G41 | Drawing tools, grid | PARTIAL | core + UI DONE, not connected |
| G42 | Keyframe / component model, thumbnails | PARTIAL | S13-01, S13-08 |
| G43 | FFT area and menu | PARTIAL | S13-03 |
| G44 | Frame operations | PARTIAL | S13-04 |
| G45 | Process menus Single / All | PARTIAL | S13-06 |
| G46 | Morph menu | PARTIAL | S13-07 |
| G47 | Vital modifiers | PARTIAL | S13-08: add / list only |
| G48 | Formula parser | PARTIAL | core DONE; presets / saving MISSING (S14) |
| G49 | Export | PARTIAL | S13-12: bytes dropped |
| G50 | Import (audio, image, Vital styles) | PARTIAL | S13-09 .. S13-11: core DONE, UI synthetic |
| G51 | Switch oscillator type on import / sample to wavetable | MISSING | S04, S05, S06 last rows |
| G52 | Wavetable frame <-> LFO shape copy | MISSING | S11 |
| G53 | Render OSC Warp | MISSING | S13-16 |
| G54 | Drive Clean Mode | DONE | t_wtremap :332 |
| G55 | Filter pan and level | DONE | t_wp6 :109, :146 |
| G56 | Keytrack follows oscillator pitch | DIFFERENT | 12-decisions Q14 |
| G57 | Drawable PZ SVF | PARTIAL | engine DONE, editor MISSING (S08) |
| G58 | About 35 more Serum filter types | PARTIAL | 37 DONE, 43 of 80 unreachable (S08) |
| G59 | Envelope ms / BPM units | DONE | t_wp123 :432 |
| G60 | Envelope Legato Inverted | DONE | t_wp123 :471 |
| G61 | LFO 9-10 | DONE | t_wp123 :170 |
| G62 | Free-form LFO shapes | DONE | editor + engine; user shape presets PARTIAL (S11) |
| G63 | LFO Path, Rossler | DONE | Rossler DONE; Path DIFFERENT (Q17) |
| G64 | LFO mono / direction / host / 10x / phase snap | PARTIAL | all DONE except phase snap to grid |
| G65 | LFO point modulation | DONE | t_lfovoice, t_wp123 :559 |
| G66 | VELO / NOTE custom curves | DONE | t_wp123 :498 |
| G67 | Sources Lift, Slide, Oct Note, Poly AT, Stereo, Rand 2 / discrete, Active Voices, Voice Index / Mod | DONE (GROUPA2 WP-07, GROUPB WP-06; Stereo from modamt) | S11 |
| G68 | Matrix AUX SOURCE, INV, CRV, OUTPUT | DONE | t_wp123 :266 |
| G69 | Per-slot curve, RISE / FALL | DONE (modamt curve, GROUPB WP-08 RISE / FALL) | `content.mod_curves` |
| G70 | Parallel FX racks | DONE | t_wp5 :231 |
| G71 | Dynamic module instancing | PARTIAL | A19, 14 row 3: one instance per type |
| G72 | Per-module LEVEL | DONE | t_wtremap :282 |
| G73 | Compressor limit, crossovers | DONE | t_fxextra :153, :245; ranges DIFFERENT (row 31) |
| G74 | Delay Tap->Delay, HQ, offset | DONE | t_fxextra :273, :307; left offset MISSING (MANUALCHECK) |
| G75 | Distortion X-Shaper, Tube / Diode, key track | DONE | t_fxextra :346, :395 |
| G76 | Phaser DEPTH 2 | DONE | t_fxextra :463 |
| G77 | Reverb algorithms | DONE | t_fxextra :491; FEEDBACK MISSING (MANUALCHECK) |
| G78 | Bode | DONE | t_wp7 :201 |
| G79 | Convolve | PARTIAL | engine DONE; IR cannot be loaded (S10) |
| G80 | Hyper / Dimension | DONE | t_wp7 :174 |
| G81 | Utility | DONE | t_wp7 :121 |
| G82 | Splitters | DONE | t_wp7 :408 |
| G83 | `clap.remote-controls` | DONE | ct_shell_clap :122, :208 |
| G84 | `.vital` / `.vitaltable` / `.vitallfo` import | PARTIAL | CLI DONE, plugin entry MISSING (I01-16) |

Count of the 84: DIFFERENT 2, DONE 40, IN-PROGRESS 4, MISSING 11, PARTIAL 27.

## 6. Appendix B - open questions Q1-Q40 (10-coverage-matrix.md section 5.2, 12-decisions.md)

| Q | Decision | Implemented? | Evidence / rows |
|---|----------|--------------|-----------------|
| Q1 | Clips, Arp, send-to-clip, grid swing excluded | yes | EXCLUDED rows |
| Q2 | 8 macros; Key / Scale, Osc Mapping in the core | yes | G01, G05, G06 |
| Q3 | Double-tap = reset, exact value by long press | reset DONE (ParamControls.cpp:333-335); the 'type' option is a dead toggle | S15-02 |
| Q4 | A/B compare | yes | ui_model `ui_ab_compare_snapshots`, ui_render `uir_ab_compare_through_editor` |
| Q5 | MPC resolution / hardware controls (proposed) | open | no MPC shell in the repository |
| Q6 | Sample osc now; granular / spectral / multisample decide later | sample engine yes; the three others still MISSING | S03, S05, S06, backlog WP-11 .. WP-13 |
| Q7 | Full wavetable editor on mobile | UI yes, not connected | S13, X01-02 |
| Q8 | User wavetables by content id + store; presets packaged with their tables | format yes, shell store no | X01-01 |
| Q9 | Keep Vital warp + morph, add a free second slot (proposed) | yes (warp2, Swap Warps, the Serum 2 warp modes appended: B21) | S02 |
| Q10 | All five export formats (proposed) | core yes, UI drops the bytes | S13-12 |
| Q11 | Full formula grammar (proposed) | core yes | S14 |
| Q12 | Image import, Render OSC Warp, Resample (proposed) | image core yes, UI synthetic; Render OSC Warp and Resample MISSING | S13 |
| Q13 | All ~35 missing Serum filters as real DSP | DSP yes (80 types), 43 unreachable | S08, backlog WP-03 |
| Q14 | Keytrack follows the MIDI note | yes | S08 DIFFERENT |
| Q15 | Per-filter pan / level (proposed) | yes | t_wp6 |
| Q16 | Serum routing, buses, DIRECT, racks | yes | S09, S10 |
| Q17 | All LFO / envelope gaps; drop Path | yes | S11 |
| Q18 | `modNN.aux_*` keys | yes | t_wp123 :266 |
| Q19 | Expose Lift, Slide, Oct Note, Poly AT (proposed) | yes | S11 |
| Q20 | Free-form per-slot curves (proposed) | wavetable position remap yes; matrix curve IN-PROGRESS | 14 row 18 |
| Q21 | Point modulation yes, audio-rate sources GAP (proposed) | point modulation yes; audio-rate sources MISSING | S11 |
| Q22 | Sustain pedal event (proposed) | yes | ct_events :126 |
| Q23 | Full tuning incl. MTS-ESP | all but the MTS-ESP client | S15-18 |
| Q24 | MPE through CLAP note expression | partly | S15-21 |
| Q25 | Per-voice control, key / velocity ranges (proposed) | ranges yes, per-voice control DONE (GROUPA2 WP-07) | S12, S15-08 |
| Q26 | All Serum-only FX modules | yes | S10 |
| Q27 | Force-enable reverb (proposed) | yes | t_wp7 :503 |
| Q28 | Kick pages generated by paramdump (proposed) | yes | X01-07 |
| Q29 | Category macro wiring in factory presets (proposed) | yes | SWEEP-REPORT |
| Q30 | MPC page cycler skips disabled modules (proposed) | open | no MPC shell in the repository |
| Q31 | Own clean-room wavetable renderer | yes | I01-11 |
| Q32 | Core changes C1-C5 | yes | state v3 |
| Q33 | Control-name table under AGPL, noted in NOTICE | yes | NOTICE:31, THIRD_PARTY.md:53 |
| Q34 | Frame position semantics settled by A/B (proposed) | open | needs real Vital audio (I01-15) |
| Q35 | `.vitalbank` batch (proposed) | CLI yes | I01-12 |
| Q36 | Mod amount scaling 1:1 (proposed) | yes, 17382 rows recompute | VITAL-IMPORT-AUDIT section 5 |
| Q37 | Style mapping Percussion / Sequence (proposed) | taken | 11 |
| Q38 | No `.vital` export (proposed) | kept | I01-13 DIFFERENT |
| Q39 | Kick on mobile: CLAP extension vs linking the core (proposed) | open (Phase 5) | backlog WP-01 part g |
| Q40 | `tests/vital-presets/` out of git (proposed) | yes | `.gitignore:17` |

## 7. Appendix C - MANUALCHECK proposals (MANUALCHECK-REPORT.md section 4)

| Proposal | Keys | Status | Backlog |
|----------|-----:|--------|---------|
| `sub.phase`, `sub.phase_contiguous` | 2 | DONE (GROUPA1, B23) | WP-04 |
| `noise.stereo`, `noise.filter` | 2 | DONE (GROUPA1, B23) | WP-04 |
| `delay.offset_left` | 1 | DONE (GROUPA1, B25) | WP-10 |
| `reverb.feedback` | 1 | DONE (GROUPA1, B25) | WP-10 |
| `oscN.ratio_src` | 3 | DONE (GROUPB, B27) | WP-05 |
| ENV 1 on/off: `sub.env1`, `oscN.env1`, `noise.env1` | 5 | IN-PROGRESS (warp-env, the worker decides the key names) | - |
| `modNN.rise`, `modNN.fall` + per-slot curve | 128 | curve DONE (modamt); RISE / FALL DONE as curve content, 0 keys (GROUPB WP-08, 14 row 81) | WP-08 |
| Warp modes Remap 1-4 | 0 (4 choice entries) | IN-PROGRESS (warp-env) | - |
| Section 3 'differences that stay' | - | DIFFERENT (14 row 31) | WP-19 owner list |

## 8. Appendix D - import fidelity gaps (VITAL-IMPORT-AUDIT-REPORT.md section 8)

| Gap | Status | Note |
|-----|--------|------|
| G1 mod amount as destination | IN-PROGRESS | modamt |
| G2 curve of a modulation | IN-PROGRESS | modamt |
| G3 modulate portamento time / polyphony / stereo routing | IN-PROGRESS | modamt |
| G4 source `stereo` | IN-PROGRESS | modamt |
| G5 `stereo_routing`, `voice_override` | IN-PROGRESS | modamt (keys + bindings) |
| G6 `osc_N_transpose_quantize` | IN-PROGRESS | the key `oscN.transpose_snap` exists already: reuse, importer mapping only |
| G7 sample as a 4th source | DONE (GROUPA1 WP-04; the warp 'from sample' is FM Thru-Zero / RM from Noise since GROUPB WP-06) | `noise.type` + content osc 4 |
| G8 stored `mod_wheel` | DECIDED: not applied (GROUPB WP-18, 14 row 82) |
| G9 built-in `Saw down` table | DONE (GROUPB WP-18) |
| importer vs B14 / B15 keys (detune range, midi track, priority) | DONE (GROUPB WP-18) |

## 9. Appendix E - UI rows still GAP / NOT DONE

`UI-GESTURE-AUDIT.md` has 138 table rows; its header says 'GAP left 6', the table has 3 rows that are neither implemented nor closed (checked with a script over the status column):

| Row | Gesture | Status | Reason |
|-----|---------|--------|--------|
| 02-24 | X\|Y dot (granular / spectral) | **DONE (GROUPC)** | closed (GROUPC WP-11 / WP-12): see UI-GESTURE-AUDIT 02-24 |
| 02-25 | Window button (granular) | **DONE (GROUPC)** | closed (GROUPC WP-11): see UI-GESTURE-AUDIT 02-25 |
| 02-26 | Spectral Lo / Hi markers, spectral filter editor | **DONE (GROUPC)** | closed (GROUPC WP-12): see UI-GESTURE-AUDIT 02-26 |

The audit's 'closed' rows describe gestures that work on the data the UI has. Rows whose data is not connected (wavetable editor save / import / export, sample view, Convolve IR, preference toggles, Lock Matrix) are listed in section 2 and in S04, S10, S11, S13, S15.

