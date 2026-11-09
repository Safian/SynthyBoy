# UI gesture audit: section 3 "Interactions" of docs/ui-design/02 .. 08

**Count after the gap closure (124 rows): closed (gaps) 49 (05-22 added with the per-voice engine, 02-27 and 02-28 added with OSCEXTRA), NOT DONE with a hard technical reason 0, GAP left 6, partial 0, the rest implemented / added before.** `closed (gaps)` rows are covered by `shell-juce/ui/tests/test_ui_gaps.cpp` (pure logic: `test_ui_model.cpp`).

Method: every row of the Interactions table of 02, 03, 04, 05, 06, 07, 08 against `shell-juce/ui/` (state of this branch, line numbers are of the commit that contains this file). Status:

- **implemented** `file:line` = the gesture does what the row says (the generic kit gestures of a ParamKnob / ParamSlider / ParamChoice / ParamToggle are cited once, see K below).
- **partial** = a part is there (what is missing is written down).
- **NOT DONE** = missing, with the reason. `GAP` = the design itself marks the feature as not having an engine key or a core feature (no UI without a key: keys are never invented).
- **added** = missing before this audit, implemented in the same change (tests: `shell-juce/ui/tests/test_ui_lfo.cpp`).

K (kit, every control that is a plain key): vertical drag + fine zones `ParamControls.cpp:204`, double-tap = reset `:247` (reset at `:277`), long-press / right-click = sheet `:287`, `ParamControl::showSheet :77`, mouse wheel `:294`, choice tap / `<` `>` `:520-568`, toggle tap `:623-637`, assign-by-touch drag on a modulatable control `:221-235` (state machine `model/ModModel.cpp`, AssignFlow).

## 02 Oscillators (and mixer)

| # | Control / gesture | Status |
|---|---|---|
| 02-01 | Osc chip: tap selects the oscillator; long-press = module sheet; swipe on the view switches oscillator | **closed (gaps)**: chip row `OscTabsPanel` (`panels/OscTabsPanel.cpp`): OSC1 / OSC2 / OSC3 chips, tap `:204`, swipe along the row `:216`, long-press = module sheet `:242`, chip drag onto a chip = swap (Alt copy, Alt + Shift copy with mods) `:228`. The oscillator panels fold away unless chosen (`OscPanel.cpp:37`, `ViewState::oscTab`). Two fingers swiping on the view: `OscWaveView.cpp:336` -> `OscPanel.cpp:27` (a one-finger drag stays the frame scrub). Alternative: the chips and the `...` module button in every panel header (`Panel.cpp:89`). Tests `uig_osc_chips_select_swipe_power_swap`, `uig_osc_view_two_finger_swipe_switches_oscillator` |
| 02-02 | Power dot `oscN.on`: tap, panel dims | **closed (gaps)**: a power dot (44 pt target) on every chip `OscTabsPanel.cpp:99`; the whole oscillator panel dims to 40 % while `oscN.on` is off `OscPanel.cpp:42`; `[MUTED / OFF]` moved clear of the `...` button; the key is still a toggle behind More and the mixer strip name toggles it (02-17). Test `uig_osc_chips_select_swipe_power_swap` |
| 02-03 | Type menu (non-HAVE types open with a "not available" badge) | implemented: `oscN.type` ParamChoice with cap dimming (`OscPanel.cpp:42`, K) |
| 02-04 | Wavetable `< >`; swipe on the name | **closed (gaps)**: `<` `>` `OscWaveView.cpp:328` (`stepWavetable`); a horizontal swipe of 48 pt or more starting in the name strip steps the table, swipe left = next `:493` (a drag below the strip is still the frame scrub). Test `uig_osc_wavetable_name_swipe_steps_the_table` |
| 02-05 | FRAME: horizontal drag on the view or the slider | implemented: `OscWaveView.cpp:243` (view drag), `frame` knob K; double-tap on the view resets `:297` |
| 02-06 | Main knob: vertical drag, long-press sheet | implemented: K |
| 02-07 | OCT / SEM segments + wheel | **closed (gaps)**: `TransposeControl` (`panels/TransposeControl.cpp:17`): OCT segments -2 .. +2 (tap), SEM wheel (vertical drag, 14 pt per semitone `:161`) with `-` `+`, `transpose = OCT * 12 + SEM` (`splitTranspose` / `joinTranspose`, `model/GestureModel.cpp`); long-press = the standard sheet = numeric entry of the total semitones (the alternative of the design). It takes a row of its own in the panel (`ParamControl::spansRow`). Created at `OscPanel.cpp:92`. Test `uig_transpose_oct_sem_control`, `uim_loop_pair_and_transpose_split` |
| 02-08 | FIN knob | implemented: `oscN.tune` K |
| 02-09 | Pitch MODE (Semitones / Harmonics / Ratio / Step) | **closed (PITCHUNI)**: `oscN.pitch_mode` ParamChoice (`OscPanel.cpp:81`, `ParamControls.cpp:605`): Semitones / Harmonics / Ratio / Step choices. Mode selection updates coarse pitch unit/semantics. Test `uig_pitch_mode_and_transpose_snap_keyboard`. |
| 02-10 | Transpose snap keyboard | **closed (PITCHUNI)**: `TransposeSnapControl` (`panels/TransposeControl.cpp:218`, `TransposeControl.h:51`): 12 semitone note keys C..B with purple lit toggle state, Clear button, Global Snap toggle pill (bit 12: 0x1000). Created at `OscPanel.cpp:95`. Test `uig_pitch_mode_and_transpose_snap_keyboard`. |
| 02-11 | UNISON voices: drag on the number / `-` `+` | **closed (gaps)**: the unison count is a pill coloured by the CPU load (green < 50 %, amber < 80 %, red) `OscWaveView.cpp:122`, amber outline when the tier caps it; new host read-out `UiHost::cpuLoad ()` (processor: time of `engine->process` against the block duration, smoothed, `PluginProcessor.cpp:276`). Drag / `-` `+` as before (K). Tests `uig_voice_badge_cpu_pill_and_scope`, `uim_warp_picture_and_cpu_level` |
| 02-12 | WARP+ type list (groups), `<` `>` | **closed (gaps, B21)**: `ParamChoice::setGroups` / `buildOptionMenu` (`ParamControls.cpp:628`): tap a group, tap a mode; the eight groups Basic / Alt / Filter / Distortion / FM / PD / AM / RM are all filled since block B21 (`warpTypeGroups` `model/GestureModel.cpp:196`, 52 modes for slot 1 and slot 2, `OscPanel.cpp:101`); **Swap Warps** is the last entry of the list (`ParamChoice::setExtraAction` `ParamControls.cpp:694`, `OscPanel.cpp` `swapWarps`); Sync Var relabels the phase knob `Var` (`OscPanel.cpp` `updateControlDimming`). Tests `uig_warp_type_list_is_grouped`, `uig_swap_warps_exchanges_the_two_warp_slots`, `uig_sync_var_names_the_warp_phase_knob_var` |
| 02-13 | Warp mode picture (the 2D view draws the warped frame live) | **closed (gaps)**: `warpedValue` (`model/GestureModel.cpp:99`) draws every one of the 10 warp modes (Sync, Formant, Quantize, Bend, Squeeze, Pulse, FM / RM from Osc 2 / 3), used by `OscWaveView.cpp:13`. The old picture mapped the first three table indices to the wrong modes; it follows the `oscN.warp.type` order now. A drawing of the idea of each mode, not the engine's DSP. Test `uim_warp_picture_and_cpu_level`. **B21**: `warpedValue` also draws the modes 11-51 (Bend, Asym, Flip, Mirror, Remap with the drawn curve of the oscillator `OscWaveView.cpp` `paintWave2D`, Odd/Even, LPF / HPF, the distortions, FM / PD / AM); test `uim_warp_picture_and_cpu_level` extended |
| 02-14 | Remap graph (pencil) | **closed (WTREMAP)**: the panel `Oscillator N Remap` (`OscRemapPanel.cpp:77`, below the oscillator it belongs to, follows the chip row `:29`) holds the graph `RemapEditor` (`RemapEditor.cpp`): drag a point (`mouseDrag :620`, end points vertical only `movePoint :300`), double-tap the line = add a point (`mouseDoubleClick :670`), double-tap a point = delete, drag the diamond = curve power, double-tap the diamond = straight; the Pencil button (`setPencil :244`, stroke `paintStroke :334`) paints freehand in 32 columns, Presets (`applyPreset :371`: Linear, Reverse, Ease in / out, S curve, Steps 4 / 8, Jump), Add / Delete / Undo / Redo / Reset buttons; the slot Remap 1-4 is `oscN.remap.slot` (preset choice), RISE / FALL are knobs under the graph. Curve in `content.remaps[]`. Tests `uiw_remap_panel_follows_the_oscillator_chip_and_has_its_four_keys`, `uiw_remap_graph_edits_reach_the_host_content_through_gestures`, `uiw_remap_slot_key_picks_the_curve_and_the_curve_survives_the_host_state` |
| 02-15 | ROUTE sheet radio `oscN.destination` | implemented as ParamChoice (`OscPanel.cpp:42`) and the filter routing chip of the console (`OscWaveView.cpp:758`); the sheet with a radio group is not a separate widget (the choice list is the same) |
| 02-16 | Mix strip level / pan drag | **closed (gaps)**: level of each of the 8 strips (K, `OscWaveView.cpp:1480` area) and the pan band above the header of O1 O2 O3 NOISE F1 F2 (sideways drag, `oscN.pan` / `noise.pan` / `filterN.pan`; the buses have no pan key). Test `uig_mixer_strip_header_sheet_swap_copy_and_pan` |
| 02-17 | Mix strip header: tap = power; long-press = module sheet | **closed (gaps)**: header (the name at the foot, a 44 pt band): tap = power for O1 O2 O3 NOISE, long-press = module sheet for O1 O2 O3 F1 F2 (`OscWaveView.cpp:1256`). Test `uig_mixer_strip_header_sheet_swap_copy_and_pan` |
| 02-18 | Mix strip header drag onto another strip (swap / copy) | **closed (gaps)**: header drag onto another strip of the same kind = swap, Alt = copy, Alt + Shift = copy with mods (`OscWaveView.cpp:1247`, `module_ops` in `ModuleSheet.cpp:102`; also OSC chips and panel headers `Panel.cpp:99`); the drop target is outlined. Alternative: the module sheet. Tests `uig_mixer_strip_header_sheet_swap_copy_and_pan`, `uig_module_header_drag_onto_another_header_swaps_alt_copies`, `uig_module_ops_swap_copy_paste_init` |
| 02-19 | Sample start / end marker drag with loupe | **closed (gaps)**: marker drag `OscWaveView.cpp:504` with the 44 pt tolerance and the loupe above the finger (`Loupe`, `GestureKit.cpp`: magnified snapshot, above the finger, inside the editor). Alternative: `S` / `E` numeric fields = the knobs of `sample.start` / `sample.end` in More. Test `uig_sample_view_markers_loupe_loop_pair` |
| 02-20 | Loop marker pair: drag, drag between moves both, double-tap between resets | **closed (gaps)**: a drag between the two loop markers moves both and keeps the length (`moveLoopPair`, `OscWaveView.cpp:420` / `:504`), a double tap between them resets the loop to start / end `:619` (double tap elsewhere resets start / end as before). Test `uig_sample_view_markers_loupe_loop_pair`, `uim_loop_pair_and_transpose_split` |
| 02-21 | Sample zoom / pan (pinch, two-finger drag) | **closed (gaps)**: zoom / pan with `Viewport` (`GestureModel.cpp:21`): pinch and two-finger drag `OscWaveView.cpp:336` (`TouchTracker`, `GestureModel.cpp:233`), trackpad pinch and wheel `:353`, and the `+` `-` `Fit` buttons `:93`; window bar and zoom read-out `:876`. Limit: the shell keeps no sample bytes (no `IContentStore` in `PluginProcessor`), so the waveform drawn is the procedural preview, not the file. Tests `uig_sample_view_zoom_pan_buttons_pinch_wheel`, `uim_viewport_zoom_pan_fit`, `uim_touch_tracker_tap_swipe_pinch_pan` |
| 02-22 | Slice handle drag, long-press add / remove | **closed (gaps)**: Manual slices are `content().samples[osc].slices` (reachable through `UiHost::content ()` / `setContent ()`, no new interface needed): handles drawn with a foot triangle `OscWaveView.cpp:876`, drag one `:504` (committed on release, one `setContent`), long-press the waveform adds / long-press a handle removes `:400`, `+ Slice` / `- Slice` buttons (visible only in Manual mode) `:93`; edits keep the list ascending, >= 0.002 apart, <= 128 (`addSlice` / `moveSlice` / `removeSlice`). Needs a loaded sample (ContentSample for the oscillator): without one the tools are disabled with a tooltip. Tests `uig_sample_view_slices_manual`, `uim_slice_list_edits` |
| 02-23 | Slice threshold line drag | **closed (gaps)**: the threshold line in slice mode Auto: dashed line with `TH n %`, drag it `OscWaveView.cpp:322` / `:504` (higher line = fewer slices, `thresholdToLine`), loupe while dragging. Alternative: the `slice_threshold` knob. Limit: the Auto slice MARKERS are not drawn - the onsets are computed inside the engine from the sample bytes (`SampleData::detectOnsets`), which the shell does not have and `IEngine` does not hand out. Test `uig_sample_view_threshold_line_auto_slices` |
| 02-24 | X\|Y dot (granular / spectral) | GAP (Q6: granular / spectral stay GAP) |
| 02-25 | Window button (granular) | GAP |
| 02-26 | Spectral Lo / Hi markers, spectral filter editor | GAP |
| 02-27 | Sub waveform picker | **closed (gaps)**: 6 segmented buttons (SIN, RECT, TRI, SAW, SQR, PULSE) with >= 44 pt touch targets `SubPanel.cpp:142-167`, taps update `sub.waveform` (`:177-194`); direct manipulation (drag level/pan) on `SubWaveView` (`:36-68`). Test `uig_sub_waveform_picker_and_noise_sample_stepper` |
| 02-28 | Noise sample `< >` | **closed (gaps)**: `<` `>` stepper buttons and sample name popup `NoisePanel.cpp:22-26` (`stepSample :47-56`, `showSampleMenu :58-79`) switching among the 9 procedural noise colors; direct manipulation (level/pan) on `OscWaveView.cpp:1028-1065` (OscNoiseView). Test `uig_sub_waveform_picker_and_noise_sample_stepper` |
| 02-29 | MAIN fader | implemented: console Main fader `OscWaveView.cpp:833` (`master.volume`), main knob in the macro row |
| 02-30 | Bus FX bypass button | implemented: `OscWaveView.cpp:757` / `:766` (`FX On` / `FX Byp`) |

## 03 Wavetable view and editor

| # | Control / gesture | Status |
|---|---|---|
| 03-01 | View tap cycles 2D -> 3D -> SP | implemented: `OscWaveView.cpp:292` (osc view), segmented switch `WavetablePanel.cpp:144-155` |
| 03-02 | View horizontal drag scrubs the frame | implemented: `WavetablePanel.cpp:286`, `OscWaveView.cpp:243` |
| 03-03 | 3D view two-finger tilt / pinch | **closed (gaps)**: `WavetableView::handleTouchGesture` (`WavetablePanel.cpp:275`): pinch = zoom, two-finger drag = tilt (vertical) and yaw (sideways), trackpad pinch and wheel, double tap resets the view `:292`; the zoom buttons stay the alternative. Test `uig_wavetable_3d_view_tilt_pinch` |
| 03-04 | EDIT button opens the editor | implemented: `WavetablePanel.cpp:157` |
| 03-05 | Wave canvas one-finger drag with the tool, loupe | **closed (gaps)**: `WaveCanvas` (`WavetableEditorSheet.cpp:21`): one-finger drag with the tool plus the loupe above the finger (`showLoupeAt`); tap tool then tap a cell is the same code path. Test `uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger` |
| 03-06 | Wave canvas two-finger pan / zoom | **closed (gaps)**: pinch / two-finger drag (`WavetableEditorSheet.cpp:2409`), trackpad pinch, Ctrl + wheel and wheel, and the `+` `-` `Fit` buttons in the canvas corner (44 pt); the grid and the waveform follow the `Viewport`. Test `uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger` |
| 03-07 | Tool strip tap | implemented: `WavetableEditorSheet.cpp:1159-1172` |
| 03-08 | Grid field 1..256 | implemented: `WavetableEditorSheet.cpp:1174-1193` (menu of divisions) |
| 03-09 | Nudge tool: long-press then drag | **closed (gaps)**: long-press (hold still 400 ms) in the canvas starts a Nudge stroke whatever the tool is, the first touch point is taken back `WavetableEditorSheet.cpp:34`; with the Nudge tool selected the drag nudges at once as before. Alternative: Process commands. Test `uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger` |
| 03-10 | FFT bars drag, Even / Odd, Snap 0/25/50/75/100 | **closed (gaps)**: `SpectrumCanvas` (EVEN / ODD / SNAP); double tap on a bar opens the numeric entry of that bin (upper half: amplitude in percent, lower half: phase in degrees) `WavetableEditorSheet.cpp:369`, `enterBinValue :2536` (parsers `parseBinAmplitude` / `parseBinPhaseDegrees`). Test `uig_wavetable_fft_bin_entry_and_nudge_all_phases`, `uim_parsers` |
| 03-11 | Phase bars drag, Nudge toggle | **closed (gaps)**: phase bars drag, and the `NUDGE` toggle in the bar: a phase-bar drag then moves every sounding harmonic by the same amount `WavetableEditorSheet.cpp:236` / `applyBinEdit :2496`. Test `uig_wavetable_fft_bin_entry_and_nudge_all_phases` |
| 03-12 | FFT menu applies at the last touched bin | implemented: `fftMenuBtn` `:339`, items `:289-299` |
| 03-13 | Thumbnail tap selects the frame | implemented: `ThumbnailsStrip` `WavetableEditorSheet.cpp:902` |
| 03-14 | Thumbnail long-press drag reorders | **closed (gaps)**: `ThumbnailsStrip` (`WavetableEditorSheet.cpp:1035`): hold on a thumbnail, drag, yellow insertion cursor, release = `moveFrameTo :2555` (one undo step, the moved frame stays selected). Alternative: SORT menu and `Move frame left / right` in the `...` menu. Test `uig_wavetable_thumbnail_reorder_swipe_close` |
| 03-15 | Select range toggle | implemented: `WavetableEditorSheet.cpp:1214-1235`, thumbnails `:902-922` |
| 03-16 | Copy / Paste / + / - | implemented: `:1209-1210` (copy / paste), prev / next frame buttons |
| 03-17 | REMOVE / SORT / MORPH menus | implemented: `:1211-1213`, items `:1811-1814` |
| 03-18 | Process `run` | implemented: `ProcessView` `:346-540`, `Run` buttons `:464` |
| 03-19 | Formula field + Apply + presets menu | implemented: `FormulaView` `:543-720`, presets `:672-683`, Apply `:711` |
| 03-20 | Import (file, method, Import) | **closed (gaps)**: audio import runs on a worker thread (`ImportJob :1006`, `importAudioAsync :2603`, applied by `finishImport :2620`, polled by the sheet's timer); the import sheet shows a progress bar and `Cancel` discards the result. Limit: the importer reports no fraction, so the bar is the indeterminate kind. Image import is quick and stays synchronous. Test `uig_wavetable_import_runs_on_a_worker_with_a_progress_bar` |
| 03-21 | Export menu | implemented: `:1784-1789` (WAV 16 / 32, single cycle, save, embed) |
| 03-22 | Close `x` | **closed (gaps)**: `X`, and a downward swipe (>= 80 pt, mostly vertical) that starts on the header band closes the sheet `WavetableEditorSheet.cpp:2578`; a grabber is drawn at the top. Test `uig_wavetable_thumbnail_reorder_swipe_close` |
| 03-23 | Undo / Redo (own stack, one entry per stroke) | **closed (gaps)**: Undo / Redo buttons, and a two-finger tap on a canvas = undo `WavetableEditorSheet.cpp:2409`; the one-finger stroke that the first finger started is taken back first (`cancelStroke :2452`, no undo / redo entry). Test `uig_wavetable_canvas_zoom_pan_loupe_nudge_two_finger`, `uim_touch_tracker_tap_swipe_pinch_pan` |
| 03-24 | Modifier parameters (Layers) | implemented: `LayersView` `:721-840` |

## 04 Filters

| # | Control / gesture | Status |
|---|---|---|
| 04-01 | Response display drag: x = cutoff (log), y = resonance | implemented: `FilterViews.h:413-500` |
| 04-02 | Mode chip tap / double-tap cycles Response, +FFT, Phase | implemented: `FilterViews.h:141`, `:161`, `:503` |
| 04-03 | Formant: drag the dot (`formant.x`, `.y`) | implemented: `FilterViews.h:437` |
| 04-04 | Blend slider | implemented: `filterN.blend` K (label per model in `FilterPanels.cpp`) |
| 04-05 | Model / Style chips, `<` `>` | implemented: `FilterPanels.cpp:66-70`, menus `:326-354` |
| 04-06 | Type picker entry sets model + style (+ blend) | implemented: `FilterViews.h:732-770`, button `FilterPanels.cpp:75` |
| 04-07 | Power | implemented: `FilterPanels.cpp:39` |
| 04-08 | INPUTS chips (source -> filter) incl. the input rule | implemented: `FilterViews.h:561-600` (rule in `FilterModels.h`) |
| 04-09 | Routing chip | implemented: `FilterViews.h:684-720` |
| 04-10 | Key track knob | implemented: `filterN.keytrack` K |
| 04-11 | Main knobs | implemented: K |
| 04-12 | Var knob for GAP types | implemented: `filterN.var` K (cap dimming with the "not available" tooltip) |
| 04-13 | Filter header long-press = module sheet | **closed (gaps)**: long-press on the header of Filter 1 / Filter 2 = module sheet (`Panel.cpp:89`, `FilterPanels.cpp:17`); the `...` button in the header is the alternative; the FX filter is not a module of `ModuleClipboard` and has none. Test `uig_module_header_long_press_opens_the_sheet_and_dot_button_is_the_alternative` |

## 05 Envelopes, LFOs, random LFOs

| # | Control / gesture | Status |
|---|---|---|
| 05-01 | Envelope node drag (time, sustain) with value bubble | implemented: `EnvCurveView.cpp:470-662` |
| 05-02 | Envelope curve dot vertical drag | implemented: `EnvCurveView.cpp:536` (+ reset by double-tap `:686`) |
| 05-03 | Envelope graph pinch (unlocked) / magnifier | **added**: pinch and wheel while the lock is off (`EnvCurveView.cpp:80-95`); the magnifier slider (time zoom 1 .. 16, enabled while unlocked, follows a pinch) next to Lock `EnvelopePanel.cpp:96`. Test `uig_screenshots_of_the_changed_screens` (render) and the existing envelope zoom tests |
| 05-04 | Envelope tab tap | **closed (gaps)**: tap on E1 .. E6 selects the envelope: the other envelope panels fold away (`EnvelopePanel.cpp:137`, `ViewState::envTab`), like the LFO tabs. Test `uig_envelope_tabs_select_long_press_and_drop_on_a_control` |
| 05-05 | Envelope tab long-press: bypass / remove destinations | **closed (gaps)**: long-press on an envelope tab = the destinations of that envelope with bypass / remove (`SourceTabButton :182` -> `ModSourceChip::showAssignmentsFor`, `ModSourceBar.cpp:131`, the same list as the source chip). Test `uig_envelope_tabs_select_long_press_and_drop_on_a_control` |
| 05-06 | Envelope tab drag onto a control (tablet) | **closed (gaps)**: drag an envelope tab onto a control: the source arms while the finger travels (targets glow), the drop creates the assignment at 50 % (`EnvelopePanel.cpp:147`, the touch flow of `AssignFlow`); dropped elsewhere = nothing. The tap-chip-then-drag-knob route remains. Test `uig_envelope_tabs_select_long_press_and_drop_on_a_control` |
| 05-07 | Shape chooser `< >` | implemented: `LfoPanel.cpp:284` (`stepShape`), list K |
| 05-08 | Mode chooser | implemented: `lfoN.sync_type` ParamChoice (`LfoPanel.cpp` keys) |
| 05-09 | Rate area segments (Hz / Tempo / dotted / triplet / Key follow) | implemented: `lfoN.sync` ParamChoice; the rate / division / key-follow controls follow it: **added** `LfoPanel.cpp:386` (dimming) |
| 05-10 | Rate knob (rate or division) | implemented: two keys, dimmed by mode (above) |
| 05-11 | Phase slider (marker moves) | implemented: drag in the shape display `LfoPanel.cpp:239-253`, `lfoN.phase` K |
| 05-12 | Fade in, Delay, Smooth, Stereo knobs | implemented: K |
| 05-13 | Smooth toggle | implemented: `lfoN.smooth` K |
| 05-14 | LFO tab drag onto another tab = copy all settings | **added**: `LfoTabsPanel.cpp:31-60` (chip drag), `copyLfoSettings` `LfoShapeEditor.cpp:113`; **closed (gaps)**: the module-sheet Copy / Paste / Swap alternative on every LFO and random LFO panel header (`LfoPanel.cpp:356`) |
| 05-15 | Shape display, Point tool: double-tap adds / deletes | **added**: `LfoShapeEditor.cpp:748` |
| 05-16 | Drag point, Snap | **added**: `LfoShapeEditor.cpp:696`, snap; **closed (gaps)**: X / Y number fields (percent) of the selected point `LfoShapeEditor.cpp:611`. Test `uig_lfo_selected_point_bar_loop_start_segment_fields_grid_all_curves` |
| 05-17 | Curve handle drag (per segment); "All curves" | **added**: per-segment drag; **closed (gaps)**: the `All curves` toggle bends every segment with one handle `LfoShapeEditor.cpp:1118`, and the `Segment` menu (straight / ease in / ease out / hard in / hard out) `:688`. Test `uig_lfo_selected_point_bar_loop_start_segment_fields_grid_all_curves` |
| 05-18 | Steps tool (draw steps at grid size) | **closed (gaps)**: Steps tool: a drag paints a flat step per grid column (grid columns are settable, 05-23) `LfoShapeEditor.cpp:985` (`paintCellAt`, Step brush). Test `uig_lfo_steps_tool_draws_one_step_per_grid_column` |
| 05-19 | Brush tool | **closed (gaps)**: Brush tool with the five paint patterns of Vital (`kPaintPatternNames`: Step, Half, Down, Up, Tri; `brushPoints :957`), the picker button next to it chooses the brush; alternative: Flat / Ramp via Steps and Draw. The catalogue was readable in `core/engine-vital/upstream/.../synth_strings.h`. Test `uig_lfo_brush_tool_paints_the_chosen_brush` |
| 05-20 | Marquee select, proportional move | **closed (gaps)**: marquee: hold still on the empty plot (`LfoShapeEditor.cpp:222`), drag a rectangle (`selectInRect :546`), a drag of a selected point moves all; `Prop` gives the rainbow falloff (`applySelectionMove :558`, one undo step). No alternative in the design. Test `uig_lfo_marquee_select_and_proportional_move` |
| 05-21 | Selected point bar: Loop here / Start here | **closed (gaps)**: `Loop here` toggles `loopPoint` of the shape (`LfoShapeEditor.cpp:666`, dashed `LOOP` line), `Start here` writes `lfoN.phase` = x of the point (the phase marker, `:677`). Test `uig_lfo_selected_point_bar_loop_start_segment_fields_grid_all_curves` |
| 05-22 | Modulate X / Y (point modulation) | **closed (gaps)**: with a source chip armed every point of the custom shape shows a dashed purple target ring `LfoShapeEditor.cpp:1403`; a drag on a point sets the depth of the armed source, sideways = X, up / down = Y (end points Y only), the bar follows the finger, the point does not move `:1128`; a tap = Y at 50 % `:1208`; assigned points keep a purple ring + depth bar (lighter = negative, grey = bypassed). Long-press a point = its sheet (source list per axis, Bypass, Remove) `:1347`; API `assignPointMod :1289`, `clearPointMod`, `bypassPointMod`. Stored in `content.lfo_shapes[].points[].mod_x / mod_y` through `UiHost::content / setContent` (no new keys; bypass = depth 0 with the source kept, the old depth is restored), one gesture = one undo step; pure logic `model/ModModel.cpp` (`setPointMod`, `pointModDragAmount`, `pointModDragAxis`). The engine plays it per voice (LFOVOICE-REPORT.md). Tests `uim_point_mod_set_remove_and_limits`, `uim_point_mod_drag_amount_and_axis`, `uil_point_mod_drag_with_an_armed_source_writes_mod_y_and_mod_x_and_undoes`, `uil_point_mod_sheet_actions_bypass_restore_and_remove`, `uil_point_mod_reaches_the_engine_the_render_differs` (render RMS differs with / without, equal when bypassed), screenshots `/tmp/ui-shots/pointmod/` |
| 05-23 | Grid fields (number entry, steppers) | **closed (gaps)**: grid columns x rows (1 .. 32) with number fields and `-` `+` steppers, used by the snap, the grid lines and the Steps / Brush columns `LfoShapeEditor.cpp:510`. Test `uig_lfo_selected_point_bar_loop_start_segment_fields_grid_all_curves` |
| 05-24 | Random LFO style chips | implemented: `rlfoN.style` ParamChoice + `<` `>` in the display (`LfoPanel.cpp:284`) |
| 05-25 | Random LFO toggles stereo / sync | implemented: K |
| 05-26 | VELO / NOTE graph, Init graph | implemented: `ModVelNotePanel.cpp:123-311` (see 06) |
| 05-27 | Undo / Redo of shape edits, one drag = one entry | **added**: `LfoShapeEditor.cpp:310`, `mouseUp :730` (an own stack; the global undo of the kit does not exist) |
| 05-28 | LFO tab chips L1..L10 / R1..R4 (2.2) | **added**: `LfoTabsPanel.cpp` |
| 05-29 | Shape presets | **added**: `LfoShapeEditor.cpp:403` (the 12 built-in shapes as point lists) |
| 05-30 | LFO swing (Serum LFO swing / shuffle) | implemented: `lfoN.swing` modulatable knob behind More (`LfoPanel.cpp:366`, K; assign-by-touch tested for LFO 1 and 10) |

## 06 Modulation

| # | Control / gesture | Status |
|---|---|---|
| 06-01 | Source chip tap = assign mode | implemented: `ModSourceBar.cpp:62-83`, flow `model/ModModel.cpp` |
| 06-02 | Source chip long-press = assignments sheet (bypass / remove) | implemented: `ModSourceBar.cpp:85-110` |
| 06-03 | Knob drag in assign mode creates / updates the slot | implemented: `ParamControls.cpp:221-247` |
| 06-04 | Long-press knob in normal mode = context sheet | implemented: `ParamControls.cpp:287`, `Sheet.cpp` |
| 06-05 | Depth halo badge: tap then drag a specific modulator | **closed (gaps)**: the count badge of the depth halo is a hit target (`ParamControls.cpp:185`): tap = list of the modulators of the control; choosing one arms its source (`selectModulator :199`), the next drag on the control sets that slot's depth through the assign flow. The sheet list stays. Test `uig_depth_halo_badge_is_a_target` |
| 06-06 | Quick slot pill BIP / ST / CRV / x | implemented: `ModMatrixPanel.cpp:70-99` |
| 06-07 | Macro knob drag / double-tap | implemented: K (macro row `Strip.cpp`) |
| 06-08 | Macro chip drag swaps macros | **closed (gaps)**: drag a macro chip of the source drawer onto another macro chip = swap (value and assignments, `ModSourceBar.cpp:64`, `modSwapMacros`); alternative `Ops Menu > Swap Macros` `ModMatrixPanel.cpp`. Test `uig_macro_chips_swap_by_drag` |
| 06-09 | Matrix row handle reorders slots | **closed (gaps)**: the slot number of a row is a 44 pt handle: drag up / down (the row swaps content with the neighbour it passes, `ModMatrixPanel.cpp:587`) or tap for the row menu Move up / Move down / Remove `:207`; `swapModSlots` (`model/ModModel.cpp:247`) exchanges all parameters of the two `modNN` slots. Reordering does not change the sound (slots sum); it changes the list order the user wants. Test `uig_mod_swap_slots_model_and_matrix_row_handle` |
| 06-10 | Matrix curve graph vertical drag (`modNN.power`) / drawn slot curve | implemented: power drag `ModCurvePanel.cpp:180` (double-tap resets `:202`); the Custom button (`:300`, `setCustomMode :388`) swaps the graph for the remap graph widget in mod curve mode (`RemapEditor.cpp:78`, `setModSlot :133`, writes `content.modCurves` in `commitNow :224`): add / move / delete points, curve power diamonds, pencil, presets, undo, reset (gestures of 02-14). Tests `uim_slot_curve_editor_draws_into_the_host_content_per_slot`, `uim_slot_curve_panel_fits_the_phone_and_the_tablet` |
| 06-11 | Matrix row Bypass / Remove | implemented: `ModMatrixPanel.cpp:33-36`, `:97-99` |
| 06-12 | Destination of a slot = the amount of another slot (G1) | implemented: destination menu submenu "Mod Amounts" in eight groups of eight, entries that would close a loop greyed (`ModMatrixPanel.cpp:382-419`, `modAmountWouldLoop` `ModLogic.h:116`); the row shows the loop badge (`modDetectFeedbackLoop :146`); a star on the Crv button marks a drawn slot curve. Test `uim_amount_destinations_are_found_and_loops_are_flagged` |

## 07 Voice, MIDI, global

| # | Control / gesture | Status |
|---|---|---|
| 07-01 | Portamento curve graph: drag curve, drag endpoint | implemented: `GlbPortamentoPanel.cpp:152-186` |
| 07-02 | MONO toggle pill | **added**: `GlbPortamentoPanel.cpp:216-240` (polyphony 1 <-> the last count) |
| 07-03 | LEGATO / ALWAYS / SCALED pills | implemented: ParamToggles of `master.legato`, `.portamento.always`, `.scaled` (`GlbPortamentoPanel.cpp` keys) |
| 07-04 | Voice count badge in the top bar | **closed (gaps)**: new host read-out `UiHost::activeVoiceCount ()` (`IEngine::activeVoices`, `PluginProcessor.cpp:276`); badge `3/16v` in the top bar `Strip.cpp:137`, tap = GLOBAL; it needs 96 pt of name area, so on phone (390) and tablet landscape (shared macro row) the count is on the GLB page tab (`GLB 3`, `PanelHost.cpp:39`, `Editor.cpp:128`) and in GLOBAL > Scope. Test `uig_voice_badge_cpu_pill_and_scope` |
| 07-05 | Quality tier card tap | implemented: `GlbQualityPanel.cpp:151` |
| 07-06 | Oscillator mapping bars: drag handles / bar body; double-tap to type notes | implemented: `GlbOscMapPanel.cpp:175-295` |
| 07-07 | Master scope / FFT display: tap / pinch toggles | **closed (gaps)**: `GlbScopePanel` (`panels/GlbScopePanel.cpp`): master wave / FFT display fed by the lock-free scope ring of the processor (`UiHost::copyScope`, `PluginProcessor.cpp:450`); tap `:138` or pinch `:34` toggles, `[Wave]` `[FFT]` buttons are the alternative; voices / CPU read-out. Test `uig_voice_badge_cpu_pill_and_scope` |
| 07-08 | Init / Utility buttons with confirm toast | **closed (gaps)**: a toast confirms every Init / Utility button (`GestureKit.cpp:121`, `GlbSettingsPanel.cpp:147-181`). The buttons now really act: Init Preset = every parameter to its default and empty content, Init LFOs = all 14 LFO modules and their custom shapes, Revert = `UiHost::revertToSaved ()` (the processor keeps what the host loaded last, `PluginProcessor.cpp:436`); before, Init LFOs and Revert only changed their text. Test `uig_settings_buttons_act_and_confirm_with_a_toast` |
| 07-09 | Voice steal / same-note mode (`master.voice.same_note` / `master.voice.steal_priority`) | implemented: two ParamChoice, primary on the Voice / Stereo panel (`GlbVoicePanel.cpp:20-33`, K); test `glb_voiceglb_panels_and_modulation` |
| 07-10 | Poly legato (`master.poly_legato`) | implemented: ParamToggle, primary (`GlbVoicePanel.cpp:20-33`, K) |
| 07-11 | Pitch bend down range + link (`master.bend_down_range`, `master.bend_down_link`) | implemented: ParamKnob + ParamToggle behind More (`GlbVoicePanel.cpp:20-33`, K) |
| 07-12 | Stereo spread (`master.stereo_spread`) | implemented: modulatable ParamKnob, primary, assign-by-touch tested (`GlbVoicePanel.cpp:20-33`, K) |
| 07-13 | 4x oversampling & no smoothing (`quality.oversample_4x`, `quality.no_smoothing`) | implemented: two ParamToggle, primary on the Quality panel (`GlbQualityPanel.cpp:206-228`, K); 4x is applied by the shell's prepare on change |

## 08 Effects

| # | Control / gesture | Status |
|---|---|---|
| 08-01 | Chain slot chip tap selects the module | implemented: `FxChainPanel.cpp:219-232` |
| 08-02 | Slot power dot: double-tap or `[ON]` | **closed (gaps)**: tap on the dot, double tap anywhere on the slot (`FxChainView.h:359`), or `[ON]` (`:42`) for the selected slot (44 pt); Opt/Alt on the dot or `[ON]` switches every effect (`toggleSlotPower :135`). Test `uig_fx_chain_slot_power_reorder_and_bypass_alternatives` |
| 08-03 | Slot handle: long-press and drag reorders | **closed (gaps)**: the slot is dragged only after a hold (`FxChainView.h:49`), the grabbed slot is ringed and the yellow drop line is drawn; a quick drag does nothing (it scrolls the page); `< Move Left` / `Move Right >` stay. Test `uig_fx_chain_slot_power_reorder_and_bypass_alternatives` |
| 08-04 | `[Byp All]` | **closed (gaps)**: `[Byp All]` `FxChainView.h`; the Opt/Alt-click variant (any bypass button switches every effect) is on the dot and on `[ON]` (`:42`, `:135`). Test `uig_fx_chain_slot_power_reorder_and_bypass_alternatives` |
| 08-05 | Chorus / Delay / Dist / FX Filter curve: 1-finger drag X / Y | implemented: `FxChorusPanel.cpp:75`, `FxDelayPanel.cpp:100`, `FxDistortionPanel.cpp:119`, `FxFilterPanel.cpp:73` |
| 08-06 | Compressor 3-band bars | implemented: `FxCompressorPanel.cpp:101-125` |
| 08-07 | Distortion waveshaper vertical drag = drive | implemented: `FxDistortionPanel.cpp:119-165` |
| 08-08 | EQ nodes: drag X / Y; pinch = resonance | drag implemented: `FxEqPanel.cpp:135-178`; pinch (and wheel) **added**: `FxEqPanel.cpp:190-215` |
| 08-09 | Reverb damping nodes | implemented: `FxReverbPanel.cpp:100-142` |
| 08-10 | Compressor crossover tags (B17, G73): drag a tag between two bands sideways to scrub `compressor.crossover_low` / `_high` (60 points per octave) | **added**: `FxCompressorPanel.cpp:151` (hit, 52 x 44 points), `:170-175` (`scrubCrossover`, `model/FxModel.h`); the band labels follow the keys. Test `fx_extras_compressor_crossover_tags_scrub_the_frequency` (`test_fx.cpp:460`) |
| 08-11 | Delay TAP (Tap->Delay, G74): tap the tempo, Free sync and both delay times follow the interval; x1 / x4/3 / x3/2 snap `delay.offset` | **added**: `FxDelayPanel.cpp:180` (TAP), `:194` (`tap`, `TapTempo` of `model/FxModel.h`), `:209` (`snapOffset`). Test `fx_extras_delay_tap_and_offset_snaps` (`test_fx.cpp:423`), model test `:364` |
| 08-12 | X-Shaper curve editor (B17, G75): drag a point, double-tap the plot = add, double-tap a point = delete, drag the diamond = bend, A / B / Add / Del / Preset / Reset buttons | **added**: `panels/FxShaperEditor.cpp:392` (down), `:415` (drag), `:437` (double-tap), `:284` (bend by bisection on the engine's evaluator), buttons `:199`; shown by `FxDistortionPanel.cpp:246` for the two X-Shaper types. Test `fx_extras_xshaper_editor_edits_the_content` (`test_fx.cpp:492`) |
| 08-13 | Reverb type chips: the controls that the chosen algorithm does not use dim (ER size, DIFF A / B for the new types, room for Basin) | **added**: `FxReverbPanel.cpp:250`. Test `fx_extras_reverb_dims_what_the_algorithm_does_not_use` (`test_fx.cpp:592`) |
| 08-14 | Phaser DEPTH 2: a second (right channel) notch curve is drawn when depth 2 is not 0 | **added**: `FxPhaserPanel.cpp:60`; the knob is a More control (K). Test `fx_extras_phaser_depth2_is_a_more_control_and_draws_a_second_curve` (`test_fx.cpp:615`) |

## Added by this audit (tests in `shell-juce/ui/tests/test_ui_lfo.cpp`)

LFO tab chips + drag-to-copy, sync-mode dimming, the free-form shape editor (05-14 .. 05-18, 05-27 .. 05-29), mixer console strip level drag and name tap (02-16, 02-17), envelope pinch / wheel zoom while unlocked (05-03), EQ node pinch / wheel resonance (08-08), MONO pill (07-02).

## Shell features (docs/ui-design/SHELLFEAT-REPORT.md, tests in `shell-juce/ui/tests/test_ui_shellfeat.cpp`)

| Row | Gesture | Status |
|-----|---------|--------|
| 01-MIDI | Long-press a control -> sheet -> MIDI learn (G07): arm / cancel / remove; amber LEARN frame and `CCn` tag on the knob | **added**: `Sheet.cpp:85,125-150`, `Overlays.cpp:107-125`, `ParamControls.cpp updateRing`, `UiContext.cpp refresh` (map revision); tests `shf_midi_learn_*` |
| 01-LOCK | Module sheet -> Lock module / Unlock module (G08); the header `...` button reads LOCK | **added** (was a disabled GAP): `ModuleSheet.cpp:102-116,179-185`, `GestureKit.cpp ModuleMenuButton::refreshLock`; tests `shf_module_lock_*` |
| 07-DEFAULT | Global Settings: Save as Default / Clear Default, Init Preset loads the default (G02) | **added**: `GlbSettingsPanel.cpp:42-52,157-190`; test `shf_save_as_default_new_instances_and_init_preset` |

## Rows left after the gap closure (docs/ui-design/UI-GAPS-REPORT.md)

- **NOT DONE with a hard reason (0)**: none (02-28 closed with OSCEXTRA procedural noise sample sets). 05-22 is implemented since the engine plays point modulation per voice.
- **GAP in the design / engine, left as the task says (4)**: 02-14, 02-24, 02-25, 02-26 (02-27 closed with OSCEXTRA sub oscillator).
- **partial (0)**: none.

## MANUALCHECK changes (docs/ui-design/MANUALCHECK-REPORT.md)

| Row | Change |
|-----|--------|
| 08 reverb | controls the type does not have are dimmed per the Serum 2 manual p.175-177 (ER SIZE and DIFF B: Vintage; DIFF A: Vintage and Nitrous; MODE (room): Nitrous): `FxReverbPanel.cpp:251` (`updateDimming`), test `fx_extras_reverb_dims_what_the_algorithm_does_not_use` |
| 08 distortion | X-Shaper editor: A and B always selectable (the drive morphs A to B), X-Shaper (Asym) draws the signed graph with centre lines: `FxShaperEditor.cpp:88` (`setAsymmetric`), `:343`; test `fx_extras_xshaper_editor_edits_the_content` |
| 08 delay | the TAP button is a shell tap tempo; Serum's Tap->Delay is the style menu entry `Tap->Delay`: `FxDelayPanel.cpp:168` |
| 02 pitch | `fixed_pitch`, `pitch_bend_track` and `unison.stack_repeat` are dimmed unless they apply (tracking off on a wavetable / sample oscillator, Octaves or Power Chord stack): `OscPanel.cpp:119` (`updateControlDimming`) |
| 02-29 | ENV 1 button of a Main / Direct channel (manual p.146, p.149) | **closed (warp-env, B21)**: seven toggles behind More of the Mixer panel (`MixerPanel.cpp` keys `osc1.env1` .. `filter2.env1`), greyed (40 %) unless the destination of the channel is Main / Direct (`MixerPanel.cpp` `updateEnv1Dimming`); 44 pt targets by the panel test. Test `uig_env1_buttons_sit_on_the_mixer_and_grey_out_off_main_and_direct` |
| 08 racks | the FX chain ribbon is now the rack strips: tap selects, dot / double tap = power, hold + drag = reorder and move between racks with a drop line, hold + release = card menu, `+` = type picker; alternatives `< Move Left`, `Move Right >`, ON, Bypass All, Duplicate, Remove, `+ Add`: `FxRackView.h` (`mouseDown`, `mouseDrag`, `mouseUp`, `showTypePicker`, `showCardMenu`); tests `uig_fx_chain_slot_power_reorder_and_bypass_alternatives`, `fxui_gestures_on_the_cards_select_toggle_and_reorder_with_a_drop_line` |
| 08 instances | the panel of a pool module opens below the strips with the keys of its slot (`FxChainPanel.h` `updateDetail`, `FxKeySet`); the module sheet of a slot has Bypass / Duplicate / Remove and Copy / Paste / Swap / Init / Lock (`ModuleSheet.cpp` `isPoolSlot`) |
| 04-05 | Model / Style chips, hierarchical type menu (Normal, Multi, Flanges, Misc, New, Classic) | **closed (filters80)**: `FilterPanels.cpp:328` (`showModelMenu`), the type picker sheet `FilterViews.h:852` (`FilterTypePickerSheet`) lists the 80 types in the categories of the manual; tests `uig_filter_menu_reaches_every_serum_type_and_every_type_shows_its_name_and_var_label_in_all_three_slots`, `uig_filter_type_picker_sheet_sets_model_and_style_of_each_of_the_80_types_in_each_slot` |
| 04-12 | Var label per type | **closed (filters80)**: `FilterModels.h` `getVarLabelForModel (model, style)`, applied by `FilterPanels.cpp` `refreshControlLabels`, equal to the manual / core metadata for models 22 .. 26 (test above) |
| 04-14 | PZ SVF curve editor: drag a point, double tap adds (max 16), double tap on a point or right click deletes, Reset | **closed (filters80)**: `FilterViews.h:492` (`mouseDown`), `:19` (`PzSvfCurve`), `:235` (`commitPzCurveToContent`, writes `content.filter_curves`); tests `uig_pz_svf_ui_curve_editor_moves_rendered_response` (the drawn curve changes the rendered noise), `uig_pz_svf_curve_has_at_most_16_points_and_a_double_tap_on_a_point_deletes_it`; the alternative to the right click is the double tap on the point |
| 04-15 | Response display of the models 22 .. 26 | **closed (filters80)**: measured from the DSP, `FilterMeasure.h`, `FilterViews.h:615` (`responseAt`); test `uig_filter_view_shows_the_measured_dsp_response_of_the_new_types` |

## Content pipeline (docs/ui-design/WP01-REPORT.md, tests in `shell-juce/ui/tests/test_ui_content.cpp`)

| Row | Gesture / control | Status |
|-----|-------------------|--------|
| 02-30 | Load Sample button (file chooser), `...` menu: Reload, Show location, Trim, Remove, Show marker animation | **closed (WP-01)**: `OscWaveView.cpp:255` (`loadSampleFile`), `:283` (`trimSample`), `:404` (menu in `showSampleMenu`); tests `content_sample_load_plays_the_content_sample_at_the_key_pitch_through_the_shell` |
| 02-31 | Snap Off / Zero / Beats / Loop on the markers (visible button cycles it) | **closed (WP-01)**: `OscWaveView.cpp:353` (`snappedMarker`, used by `mouseDrag`), button `snapBtn`; same test (zero crossing value, beat grid, loop points) |
| 02-32 | Sample waveform is the decoded file (zoom / pan window, min / max columns); Auto slice markers = the engine's onsets | **closed (WP-01)**: `OscWaveView.cpp:1204` (`paintSample`), `:370` (`autoSliceMarkers`); test compares snapshots and the slice list |
| 02-33 | Oscillator view draws the Preset table (2D, 3D stack, spectrum) and has an Edit button | **closed (WP-01)**: `OscWaveView.cpp:970` (`paintUserTable`), `:404` (`openEditor`) |
| 02-34 | Marker animation: modulation band of UNI START | **closed (WP-01)** as the range band (live position waits for WP-17, 14 row 49): `OscWaveView.cpp:388` (`modulationBands`) |
| 03-30 | Wavetable editor menu: Save Table, Embed in Preset (library file + `content.wavetables` + `oscN.wavetable`) | **closed (WP-01)**: `WavetableEditorSheet.cpp:2321` (`saveToLibrary`), menu at `:2100`; test `content_saved_table_survives_a_state_reload_and_differs_from_the_saw_fallback` |
| 03-31 | Import sheet: Choose file..., Audio / Image / Wavetable tabs read the real file | **closed (WP-01)**: `WavetableEditorSheet.cpp:1345` (`doImport`), `:2345` (`importFile`); test `content_wavetable_editor_imports_real_files_saves_and_exports` |
| 03-32 | Export WAV 16 / 32 bit / single cycle write a file chosen in a save dialog | **closed (WP-01)**: `WavetableEditorSheet.cpp:2294` (`exportToFile`); same test |
| 03-33 | Formula Presets menu: examples, My formulas (save, load, favourite, delete, file) | **closed (WP-01)**: `WavetableEditorSheet.cpp:856` (`showPresetsMenu`), `LibraryMenu.cpp`; test `content_user_library_service_lfo_shapes_formulas_and_fx_racks` |
| 05-30 | LFO Presets menu: My shapes (save, load, favourite, delete), load `.lfoshape.json` / `.vitallfo` | **closed (WP-01)**: `LfoShapeEditor.cpp:282`, `applyShapeFromFile`; same test |
| 08-30 | Convolve Load IR... opens a file chooser, shows the IR envelope, Remove | **closed (WP-01)**: `FxConvolvePanel.cpp:187` (`requestImpulseFile`), `ContentIO.cpp:294` (`loadImpulseFile`); test `content_convolve_impulse_response_makes_a_tail` |
| 08-31 | FX rack Presets button (save / load the FX chain) | **closed (WP-01)**: `FxRackView.h:327` (`initRackPresets`); test `content_user_library_service_lfo_shapes_formulas_and_fx_racks` |
| 01-30 | Editor menu: Import Vital file... (+ report sheet), Missing files... (+ dialog after a load) | **closed (WP-01)**: `Editor.cpp:327` (`importVitalFromMenu`), `:307` (`checkMissingContent`), `ContentSheets.cpp`; tests `content_vital_import_inside_the_plugin_with_the_report_sheet`, `content_missing_files_dialog_logic_locate_remove_and_hash_check` |
