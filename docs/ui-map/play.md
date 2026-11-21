# Play page

## Purpose

The Play page is the main synthesis view of the MPC overlay. It shows three panels side by side: the oscillator cycle (left third), the envelope curve with the live dot (middle third), and the LFO shape with the live cursor of the currently selected LFO (right third). Each panel has a selector for its instance, a display area, and a block of controls below. The page lets the user choose which oscillator (Sub, A, B, C, Noise), envelope (1..6), and LFO (1..10, RND 1..4) to edit, and exposes all their parameters through compact controls on the panel or full-screen detail pages.

## Layout

The page occupies region `{x: 23, y: 14, w: 1234, h: 190}` on the overlay canvas (shell-mpc/overlay/overlay_layout.json:454). It is the "play1" view of kind "play" (shell-mpc/overlay/overlay_layout.json:454). The three panels share the same row structure defined in shell-mpc/ui/play_rows.h: the selector row (kSelectorH = 56 px), the display area (displayRect), and the controls block (kControlsH = 232 px) starting at controlsLine. The oscillator third has the instance selector, power, route, and full-screen buttons in its header band. The envelope and LFO thirds have their instance selectors in the same row.

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| Oscillator instance selector | - | Tap / scroll | Opens list of 5 oscillators (Sub, A, B, C, Noise); selection changes the panel | shell-mpc/overlay/osc_ui.h:478 |
| Oscillator power | `on` | Tap | Toggles oscillator on/off | shell-mpc/overlay/osc_ui.h:492 |
| Oscillator route | `destination` | Tap | Steps through F1, F2, F12, Mix, Direct, Balance, Off | shell-mpc/overlay/osc_ui.h:494 |
| Oscillator type (waveform / type) | `sub.waveform` (Sub), `oscN.type` (A/B/C) | Tap / long-press opens list | Steps through waveform types; long-press opens type list | shell-mpc/overlay/osc_ui.h:500 |
| Oscillator source | `wavetable` (wavetable), `sample` (noise) | Tap / long-press opens list | Selects wavetable or sample source; long-press opens content/browser list | shell-mpc/overlay/osc_ui.h:508 |
| Oscillator octave (Sub) | `sub.octave` | Drag / tap | Changes octave -4..4 | shell-mpc/overlay/osc_ui.h:515 |
| Oscillator filter (Noise) | `noise.filter` | Drag | Changes noise filter cutoff | shell-mpc/overlay/osc_ui.h:517 |
| Oscillator noise type | `noise.sample` | Tap | Toggles noise between sample and generator | shell-mpc/overlay/osc_ui.h:502 |
| Oscillator pan | `pan` | Drag | Changes pan -100..100 | shell-mpc/overlay/osc_ui.h:518 |
| Oscillator level | `level` | Drag | Changes level -96..6 dB | shell-mpc/overlay/osc_ui.h:519 |
| Oscillator full screen button | - | Tap | Opens OscFullScreen (full screen of the oscillator) | shell-mpc/overlay/osc_ui.h:481 |
| Envelope instance selector | - | Tap / scroll | Opens list of 6 envelopes (ENV 1..6); selection changes the panel | shell-mpc/overlay/env_ui.h:356 |
| Envelope graph | - | Drag handles, tap LOCK, drag ruler | Edits envelope shape (delay, attack, hold, decay, sustain, release, curves); LOCK toggles zoom lock | shell-mpc/overlay/env_ui.h:363 |
| Envelope full screen button | - | Tap | Opens EnvFullScreen (full screen of the envelope) | shell-mpc/overlay/env_ui.h:364 |
| Envelope targets button | - | Tap | Opens ModTargetsPage (modulation matrix rows with this envelope as source) | shell-mpc/overlay/env_ui.h:365 |
| Envelope rate | `envN.rate` | Drag | Changes envelope rate | shell-mpc/overlay/env_ui.h:366 |
| Envelope division | `envN.division` | Tap | Steps through sync divisions | shell-mpc/overlay/env_ui.h:367 |
| Envelope rise | `envN.fade_in` | Drag | Changes envelope fade-in time | shell-mpc/overlay/env_ui.h:368 |
| Envelope smooth time | `envN.smooth_time` | Drag | Changes envelope smoothing time | shell-mpc/overlay/env_ui.h:369 |
| Envelope sync | `envN.sync` | Tap | Steps through sync modes | shell-mpc/overlay/env_ui.h:370 |
| Envelope stereo toggle | `envN.stereo` | Tap | Toggles stereo mode | shell-mpc/overlay/env_ui.h:371 |
| Envelope mode | `envN.sync_type` | Tap | Steps through trigger modes | shell-mpc/overlay/env_ui.h:372 |
| Envelope mode list | `envN.sync_type` | Tap | Opens list of sync types | shell-mpc/overlay/env_ui.h:373 |
| Envelope phase | `envN.phase` | Drag | Changes trigger phase 0..360 | shell-mpc/overlay/env_ui.h:374 |
| Envelope delay | `envN.delay` | Drag | Changes envelope delay time | shell-mpc/overlay/env_ui.h:375 |
| Envelope direction | `envN.direction` | Tap | Steps through directions | shell-mpc/overlay/env_ui.h:376 |
| Envelope swing | `envN.swing` | Drag | Changes swing amount | shell-mpc/overlay/env_ui.h:377 |
| Envelope stereo | `envN.stereo` | Drag | Changes stereo spread | shell-mpc/overlay/env_ui.h:378 |
| Envelope mono toggle | `envN.mono` | Tap | Toggles mono mode | shell-mpc/overlay/env_ui.h:379 |
| Envelope key transpose | `envN.keytrack_transpose` | Drag | Changes key tracking transpose | shell-mpc/overlay/env_ui.h:380 |
| Envelope key tune | `envN.keytrack_tune` | Drag | Changes key tracking tune | shell-mpc/overlay/env_ui.h:381 |
| Envelope picker | `envN.shape` | Tap | Selects envelope shape from list | shell-mpc/overlay/env_ui.h:382 |
| Envelope smooth switch | `envN.smooth` | Tap | Toggles envelope smoothing | shell-mpc/overlay/env_ui.h:383 |
| Envelope host switch | `envN.host` | Tap | Toggles host sync | shell-mpc/overlay/env_ui.h:384 |
| Envelope edit button | - | Tap | Opens ShapeEditorPage (drawn shape editor) | shell-mpc/overlay/env_ui.h:385 |
| LFO instance selector | - | Tap / scroll | Opens list of 14 LFOs (LFO 1..10, RND 1..4); selection changes the panel | shell-mpc/overlay/lfo_ui.h:483 |
| LFO shape | `lfoN.shape` | Tap / long-press opens list | Steps through built-in shapes; long-press opens shape list | shell-mpc/overlay/lfo_ui.h:485 |
| LFO style (RND) | `rlfoN.style` | Tap | Steps through random LFO styles | shell-mpc/overlay/lfo_ui.h:486 |
| LFO rate | `lfoN.rate` | Drag | Changes LFO rate | shell-mpc/overlay/lfo_ui.h:487 |
| LFO division | `lfoN.division` | Tap | Steps through sync divisions | shell-mpc/overlay/lfo_ui.h:488 |
| LFO sync mode | - | Tap | Steps through sync modes (Free, Tempo, Dotted, Triplet, Key) | shell-mpc/overlay/lfo_ui.h:489 |
| LFO rise | `lfoN.fade_in` | Drag | Changes LFO fade-in time | shell-mpc/overlay/lfo_ui.h:490 |
| LFO smooth time | `lfoN.smooth_time` | Drag | Changes LFO smoothing time | shell-mpc/overlay/lfo_ui.h:491 |
| LFO sync | `lfoN.sync` | Tap | Steps through sync modes | shell-mpc/overlay/lfo_ui.h:492 |
| LFO stereo toggle | `rlfoN.stereo` | Tap | Toggles stereo mode for random LFO | shell-mpc/overlay/lfo_ui.h:493 |
| LFO full screen button | - | Tap | Opens LfoFullScreen (full screen of the LFO) | shell-mpc/overlay/lfo_ui.h:484 |
| LFO LFO cells | - | Tap | Steps through LFO 1..10 | shell-mpc/overlay/lfo_ui.h:496 |
| LFO RND cells | - | Tap | Steps through RND 1..4 | shell-mpc/overlay/lfo_ui.h:497 |
| LFO sync mode cell | - | Tap | Steps through sync modes | shell-mpc/overlay/lfo_ui.h:498 |
| LFO sync trip cell | - | Tap | Toggles triplet | shell-mpc/overlay/lfo_ui.h:499 |
| LFO sync dot cell | - | Tap | Toggles dotted | shell-mpc/overlay/lfo_ui.h:500 |
| LFO mono top | - | Tap | Toggles mono | shell-mpc/overlay/lfo_ui.h:501 |
| LFO targets button | - | Tap | Opens ModTargetsPage (modulation matrix rows with this LFO as source) | shell-mpc/overlay/lfo_ui.h:502 |
| LFO picker | `lfoN.shape` / `rlfoN.style` | Tap | Selects shape/style from list | shell-mpc/overlay/lfo_ui.h:503 |
| LFO smooth switch | `lfoN.smooth` | Tap | Toggles LFO smoothing | shell-mpc/overlay/lfo_ui.h:504 |
| LFO host switch | `lfoN.host` | Tap | Toggles host sync | shell-mpc/overlay/lfo_ui.h:505 |
| LFO edit button | - | Tap | Opens ShapeEditorPage (drawn shape editor) | shell-mpc/overlay/lfo_ui.h:506 |
| LFO style | `rlfoN.style` | Tap | Steps through random LFO styles | shell-mpc/overlay/lfo_ui.h:507 |
| LFO rate | `lfoN.rate` | Drag | Changes LFO rate | shell-mpc/overlay/lfo_ui.h:508 |
| LFO division | `lfoN.division` | Tap | Steps through sync divisions | shell-mpc/overlay/lfo_ui.h:509 |
| LFO rise | `lfoN.fade_in` | Drag | Changes LFO fade-in time | shell-mpc/overlay/lfo_ui.h:510 |
| LFO mode | `lfoN.sync_type` | Tap | Steps through trigger modes | shell-mpc/overlay/lfo_ui.h:512 |
| LFO mode list | `lfoN.sync_type` | Tap | Opens list of sync types | shell-mpc/overlay/lfo_ui.h:513 |
| LFO phase | `lfoN.phase` | Drag | Changes trigger phase 0..360 | shell-mpc/overlay/lfo_ui.h:514 |
| LFO delay | `lfoN.delay` | Drag | Changes LFO delay time | shell-mpc/overlay/lfo_ui.h:515 |
| LFO direction | `lfoN.direction` | Tap | Steps through directions | shell-mpc/overlay/lfo_ui.h:516 |
| LFO swing | `lfoN.swing` | Drag | Changes swing amount | shell-mpc/overlay/lfo_ui.h:517 |
| LFO stereo | `lfoN.stereo` | Drag | Changes stereo spread | shell-mpc/overlay/lfo_ui.h:518 |
| LFO mono toggle | `lfoN.mono` | Tap | Toggles mono mode | shell-mpc/overlay/lfo_ui.h:519 |
| LFO stereo toggle | `rlfoN.stereo` | Tap | Toggles stereo mode for random LFO | shell-mpc/overlay/lfo_ui.h:520 |
| LFO key transpose | `lfoN.keytrack_transpose` | Drag | Changes key tracking transpose | shell-mpc/overlay/lfo_ui.h:521 |
| LFO key tune | `lfoN.keytrack_tune` | Drag | Changes key tracking tune | shell-mpc/overlay/lfo_ui.h:522 |

## Opens

| Control / gesture | Opens | Description |
|---|---|---|
| Oscillator instance selector | ListPopup (instances) | List of 5 oscillators: Sub, A, B, C, Noise |
| Oscillator type (long-press) | ListPopup (param) | List of waveform types for the selected oscillator |
| Oscillator source (long-press) | ListPopup (browser) | Content browser (WAV files, SFZ instruments) or wavetable browser |
| Oscillator full screen button | OscFullScreen | Full screen of the oscillator with tuning column, bar controls, panel controls |
| Oscillator tune more (full screen) | oscspec::kMore | Detail page with transpose, transpose_snap, pitch_coarse, pitch_track, and more |
| Oscillator route (full screen) | kPageRoute | Route popup with target, balance, bus1_send, bus2_send |
| Oscillator detail buttons (bar) | kPageUnison / kPageGrain / kPageEnvelope | Unison detail, Grain window, Envelope detail pages |
| Oscillator spec pages (bar) | oscspec::kWtMap / oscspec::kMore / oscspec::kSampleRegion / oscspec::kSampleSlice / oscspec::kGrain / oscspec::kSpecFilter / oscspec::kSpecScan | Oscillator-specific detail pages |
| Envelope instance selector | ListPopup (instances) | List of 6 envelopes: ENV 1..6 |
| Envelope targets button | ModTargetsPage | Rows of the modulation matrix with this envelope as source (depth, polarity, bypass, remove) |
| Envelope targets add | ListPopup (dest) | Destination list: groups of mod targets, then parameters of a group |
| Envelope full screen button | EnvFullScreen | Full screen of the envelope with graph, targets, and all controls |
| LFO instance selector | ListPopup (instances) | List of 14 LFOs: LFO 1..10, RND 1..4 |
| LFO shape (long-press) | ListPopup (param) | List of LFO shapes (built-in shapes) |
| LFO style (long-press) | ListPopup (param) | List of random LFO styles |
| LFO sync (long-press) | ListPopup (param) | List of sync modes |
| LFO mode (long-press) | ListPopup (param) | List of sync types |
| LFO direction (long-press) | ListPopup (param) | List of directions |
| LFO division (long-press) | ListPopup (param) | List of sync divisions |
| LFO edit button | ShapeEditorPage | Shape editor with canvas, tools, undo, delete, snap, loop, smooth, X/Y/Curve/Grid fields, shapes list, more actions, point modulation |
| LFO targets button | ModTargetsPage | Rows of the modulation matrix with this LFO as source |
| LFO targets add | ListPopup (dest) | Destination list: groups of mod targets, then parameters of a group |
| LFO full screen button | LfoFullScreen | Full screen of the LFO with all controls |
| Shape editor shapes button | ListPopup (editor) | Shapes list: factory shapes, my shapes, save as, replace, rename, delete |
| Shape editor more button | ListPopup (editor) | Actions list: EDIT (select all, flip, shift, reset curves, clear loop), GRID (grid sizes), COPY (copy to LFO, shape to osc, osc to shape) |
| Shape editor mod button | PointModPage | Point modulation: target (X/Y/Curve), amount, remove, board |
| Shape editor mode stepper | ListPopup (param) | List of sync types |
| Oscillator modulations page | ModulationsPage | Rows of modulations for the selected control (depth, polarity, bypass, remove) |
| Envelope modulations page | ModulationsPage | Rows of modulations for the selected control |
| LFO modulations page | ModulationsPage | Rows of modulations for the selected control |

## Q-Link groups

The Play page publishes three Q groups (one per panel) when all three are in their compact view, coordinated by PlayView::publishQ (shell-mpc/overlay/play_view.h:350):

- **Oscillator group** (ADS): Power, route, type selector, source, octave (Sub), filter (Noise), pan, level. Active on Q 1..4 when the oscillator third is highlighted. Next on Q 5..8.
- **Envelope group** (ADSR): Delay, attack, hold, decay, sustain, release knobs and fields. Active on Q 5..8 when the envelope third is highlighted. Next on Q 9..12.
- **LFO group** (Rate / Random): Rate, division, sync mode, rise, smooth time. Active on Q 9..12 when the LFO third is highlighted. Next wraps to Q 1..4.

When a full screen is open, its own page publishes its Q group (e.g., OscFullScreen ADSR/SHAPE/TIME, LfoFullScreen Rate/Timing/Motion/Shape, ShapeEditorPage Edit/Grid/Trigger virtual targets). The targets and modulations pages publish one group per row.

## Live parts

- **Oscillator cycle**: The waveform display in the oscillator third is drawn live from the engine's oscillator cycle computation (shell-mpc/overlay/play_view.h:160). The hash includes the cycle samples (shell-mpc/overlay/play_view.h:328).
- **Envelope graph**: The envelope curve with live dots showing the current stage of each voice (shell-mpc/overlay/env_ui.h:548-564). The dots come from `live->envVoiceStage` and `live->envVoiceFraction` (shell-mpc/overlay/env_ui.h:549-563).
- **LFO curve**: The LFO shape display with live cursors showing the current phase of each voice (shell-mpc/overlay/lfo_ui.h:1033-1053). The cursors come from `live->lfoVoicePhase` (shell-mpc/overlay/lfo_ui.h:1041). For random LFOs, the display shows a trace of the output over the last moments (shell-mpc/overlay/lfo_ui.h:1057-1073).

## Duplicates

| Parameter key | Also on |
|---|---|
| `on` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:493) |
| `destination` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:495), oscillator route page (shell-mpc/overlay/osc_ui.h:815) |
| `pan` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:518), panel slot (shell-mpc/overlay/osc_ui.h:662) |
| `level` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:519), panel slot (shell-mpc/overlay/osc_ui.h:663) |
| `sub.waveform` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:500) |
| `oscN.type` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:500) |
| `sub.octave` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:515) |
| `noise.filter` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:517) |
| `noise.sample` | Oscillator full screen (shell-mpc/overlay/osc_ui.h:502) |
| `lfoN.rate` | LFO full screen (shell-mpc/overlay/lfo_ui.h:508) |
| `lfoN.division` | LFO full screen (shell-mpc/overlay/lfo_ui.h:509) |
| `lfoN.sync` | LFO full screen (shell-mpc/overlay/lfo_ui.h:498-500 via SyncSwitch) |
| `lfoN.fade_in` | LFO full screen (shell-mpc/overlay/lfo_ui.h:510) |
| `lfoN.smooth_time` | LFO full screen (shell-mpc/overlay/lfo_ui.h:511) |
| `lfoN.sync_type` | LFO full screen (shell-mpc/overlay/lfo_ui.h:512), shape editor (shell-mpc/overlay/lfo_ui.h:1458) |
| `lfoN.phase` | LFO full screen (shell-mpc/overlay/lfo_ui.h:514), shape editor (shell-mpc/overlay/lfo_ui.h:1464) |
| `lfoN.delay` | LFO full screen (shell-mpc/overlay/lfo_ui.h:515) |
| `lfoN.direction` | LFO full screen (shell-mpc/overlay/lfo_ui.h:516) |
| `lfoN.swing` | LFO full screen (shell-mpc/overlay/lfo_ui.h:517) |
| `lfoN.stereo` | LFO full screen (shell-mpc/overlay/lfo_ui.h:518) |
| `lfoN.mono` | LFO full screen (shell-mpc/overlay/lfo_ui.h:519) |
| `lfoN.shape` | LFO full screen (shell-mpc/overlay/lfo_ui.h:503), shape editor (via content) |
| `rlfoN.style` | LFO full screen (shell-mpc/overlay/lfo_ui.h:507) |
| `lfoN.smooth` | LFO full screen (shell-mpc/overlay/lfo_ui.h:504) |
| `envN.rate` | Envelope full screen |
| `envN.division` | Envelope full screen |
| `envN.fade_in` | Envelope full screen |
| `envN.smooth_time` | Envelope full screen |
| `envN.sync` | Envelope full screen |
| `envN.stereo` | Envelope full screen |
| `envN.sync_type` | Envelope full screen |
| `envN.phase` | Envelope full screen |
| `envN.delay` | Envelope full screen |
| `envN.direction` | Envelope full screen |
| `envN.swing` | Envelope full screen |
| `envN.stereo` | Envelope full screen |
| `envN.mono` | Envelope full screen |
| `envN.keytrack_transpose` | Envelope full screen |
| `envN.keytrack_tune` | Envelope full screen |
| `envN.shape` | Envelope full screen |
| `envN.smooth` | Envelope full screen |
| `envN.host` | Envelope full screen |

## Questionable

| Control | Reason | Source |
|---|---|---|
| LFO `stereo` appears as both a toggle (`rlfoN.stereo`, kStereoToggle) and a knob (`lfoN.stereo`, kStereo) | The toggle is for random LFOs, the knob for regular LFOs, but the naming is confusing and they share the same visual area in the compact view | shell-mpc/overlay/lfo_ui.h:493, shell-mpc/overlay/lfo_ui.h:520 |
| LFO `sync_type` appears as both a stepper (kMode) and a list (kModeList) | The stepper is for regular LFOs (6 choices), the list for random LFOs (2 choices), but they are different UI components for the same parameter concept | shell-mpc/overlay/lfo_ui.h:512-513 |
| Envelope `stereo` appears twice in the controls table | Once as a toggle and once as a knob - need to verify if these are actually different parameters or a documentation error | shell-mpc/overlay/env_ui.h:371, shell-mpc/overlay/env_ui.h:378 |
| LFO `smooth_time` is disabled for random LFOs but the control is still visible | The control is dimmed but still takes touch space; could be hidden instead | shell-mpc/overlay/lfo_ui.h:947 |
| LFO `edit` button is disabled for random LFOs but the control is still visible | Same as above - dimmed but takes space | shell-mpc/overlay/lfo_ui.h:956 |
| Envelope graph has two view states (compact and full) that share the same selection/zoom/lock state via GraphView array | The graph view state is stored per stack depth but the compact and full graph share the same handle IDs, which could cause confusion | shell-mpc/overlay/env_ui.h:496 |
| Shape editor virtual Q targets (POINT, X, Y, CURVE, GRID X, GRID Y, SNAP, BEND ALL) are not real parameters | They are virtual targets (QRouter::kVirtual) that control the shape editor state, not engine parameters. This makes the Q-Link mapping non-obvious | shell-mpc/overlay/lfo_ui.h:62, shell-mpc/overlay/lfo_ui.h:1200-1224 |
| Point modulation virtual targets (MODULATE, AMOUNT) are also virtual | Same issue as shape editor - these control the point modulation page state, not engine parameters | shell-mpc/overlay/lfo_ui.h:62, shell-mpc/overlay/lfo_ui.h:1956-1993 |
