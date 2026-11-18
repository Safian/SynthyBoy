# LFO editor: what the reference editor can do, what ours does, and the page design

Source of the reference: the LFO chapter of the reference manual (manual pages 188 to 193, "Drawing an LFO graph", "Modifying LFOs", "Modifying LFO controls", "Using the LFO editor", "Copying a wavetable shape to an LFO", "Copying an LFO shape to a wavetable", "Modulating LFO points"). The owner's decision (2026-11-18): every capability of the reference editor exists in ours, adapted to a touch screen with 16 knobs (a mouse modifier becomes a tool, a mode switch, a double tap or a list entry). Code: `shell-mpc/ui/shape_editor.h` (`ToolSelector`, `ShapeCanvas`, `ShapeEditorPage`), `shell-mpc/ui/lfo_model.h` (`lfo::ShapeModel`, `lfo::Undo`), `shell-mpc/overlay/lfo_ui.h` (the owner), `shell-mpc/overlay/lfo_shape_files.h` (the user shapes on the card).

## 1. What the engine does with the loop point (read from the code, 2026-11-18)

The voice does NOT read `ContentLfoShape::loopPoint` (`core/engine-own/voice/voice.cpp` `Voice::lfoValue`, `core/engine-own/env_lfo/lfo.h` `LfoCore`): the position that the trigger modes use is the PHASE knob (`lfoN.phase`, 0 .. 360 degrees = 0 .. 1 of the cycle). Per `lfoN.sync_type`:

| sync_type | what PHASE is | marker on the canvas |
|---|---|---|
| 0 Retrigger | the start position of the cycle at note on | START (draggable) |
| 1 Free-run | a phase offset added to the shared clock | dimmed, hint "FREE-RUN: PHASE SHIFTS THE CYCLE" |
| 2 Envelope | the start position; one cycle, then the last value is held | START (draggable) |
| 3 Sustain envelope | the sustain point: the run waits there while the key is held, then plays on to the end | LOOP (draggable) |
| 4 Loop at point | after the end the run jumps back to this position and loops from there to the end | LOOP (draggable) |
| 5 Loop and hold | while the key is held the run loops from 0 to this position; on release it plays on to the end | LOOP (draggable) |

So the draggable marker of the editor IS the PHASE value (what is audible). `ShapeModel::loop` (a point index, stored in the shape and in `.lfoshape.json` as `loop_point`) is a BOOKMARK that follows the marker: it names the point the marker sits on (the marker snaps to existing points in the loop modes), it moves with the point while the marker is bound to it, and it keeps the reference editor's "loopback point is a point" meaning in the stored data. In the modes 0 and 2 the marker is the start position and does not touch `loop`. This is stated for the owner as a finding (the stored loop index alone has no effect on the sound).

## 2. Capability table

Legend: done = implemented by this work (the point of the task file in brackets); elsewhere = a control of the LFO page, not of the editor.

| # | Reference capability (manual page) | Ours before | Gap | Ours now |
|---|---|---|---|---|
| 1 | Tool POINT: add, move, delete points, adjust curves (189) | `ShapeCanvas` Mode::Drag / Curve; delete only by 600 ms long press (`tick`) | quick delete | done [1]: double tap on a point deletes it, DELETE deletes the selection; curve handle: double tap resets the curve |
| 2 | Tools FLAT, RAMP UP, RAMP DOWN on the grid (189) | `ShapeCanvas::drawCell`, one cell per event | a fast drag skips columns, a flat piece cannot be re-heighted in its column | done [2]: every column between two events is filled (finger height interpolated), snapped to GRID Y with SNAP, one undo state per gesture |
| 3 | Double-click: add or remove points (190) | tap adds; no double-tap removal | removal | done [1]: tap adds, double tap on an existing point removes it (never the end points; a point just added by the first tap is kept) |
| 4 | Shift-click: draw steps at the grid size (step sequencer), the segment shape is set in the context menu (190) | the three step tools | none: the tool IS the chosen step shape (FLAT, RAMP+, RAMP-); drag = steps in one gesture | done [2] |
| 5 | Alt-drag a point: snap to the grid (190) | SNAP toggle (a Q member) | none (a mode switch instead of a key) | done (SNAP) |
| 6 | Alt-drag a curve point: move all curve points at once (190) | none | all curves | done [6]: tool BEND (drag any curve handle: every curve moves by the same amount) and the Q target BEND ALL (mean curve of all segments) |
| 7 | Click-drag on the background: select several points (190) | none | selection | done [5]: tool SELECT (drag a rectangle; tap on a point adds / removes it); SELECT ALL in MORE |
| 8 | Cmd-drag: relative movement, closer points move more (190) | none | proportional edit | done [5]: tool SOFT (drag a point: the selected points, or all points within 25 percent of the cycle, follow with a linear falloff; the affected points are drawn in rainbow colours by their weight) |
| 9 | Context menu: remove all selected points (190) | none | delete selection | done [1][5]: DELETE |
| 10 | Context menu: assign the start / loopback point; Shift-Cmd-click: set the loopback point, envelope mode (190, 191) | LOOP toggle on the selected point (stored index, no sound) | marker | done [3]: the draggable marker (START / LOOP, snaps to points), LOOP toggle, PHASE field and Q member, MODE field and Q member (section 1) |
| 11 | MODE (Retrig / Free / Envelope) and the envelope loopback (191) | the trigger MODE on the LFO page | reach from the editor | done [3]: MODE stepper in the editor (the six trigger types of the engine, a list popup) |
| 12 | SHAPE menu: load a preset (overwrites the graph), save the graph as a user preset (191) | none | whole feature | done [4]: SHAPES button: factory shapes (the 12 built-in ones as points), my shapes, SAVE AS, OVERWRITE, RENAME, DELETE (asks once) |
| 13 | GRID: horizontal and vertical grid, a number can be typed (192) | GRID X / GRID Y fields (drag, Q) | presets, half / double | done [6]: MORE > GRID: 4, 8, 12, 16, 32, HALVE, DOUBLE; the fields give every value 2 to 32 (the number field is the touch version of the typed number) |
| 14 | Edit a point by exact value (the point menu, 190) | X / Y / CURVE fields in 0.01 steps (drag 2 px per step, Q knob) | none | done: the EDIT group (and `lfo_ui.h` `applyVirtual`); with several points selected the fields move / scale the selection |
| 15 | Copy all settings of an LFO to another (Option-drag a tab) (189) | none | copy | done [6]: MORE > COPY > TO LFO n (the shape, its grid, and `lfoN.shape` = Custom; the other parameters of the LFO are the LFO page's business) |
| 16 | Copy a wavetable shape to an LFO (197) | none | whole feature | done [6]: MORE > COPY > FROM OSC A / B / C (one cycle of the oscillator as at most 128 points, `convert::frameToLfoShape`) |
| 17 | Copy an LFO shape to a wavetable (198) | none | whole feature | done [6]: MORE > COPY > TO OSC A / B / C (the shape as one frame in `SynthyBoy/Wavetables/`, loaded through `requestWavetable`) |
| 18 | SMOOTH (192) | SMOOTH toggle: the S curve of every segment (the shape flag) and the LFO page's smooth time | none | done (SMOOTH toggle in the editor; the time is the LFO page's) |
| 19 | Undo (the reference: Cmd-Z) | UNDO button, 32 states | none | done: every operation above is ONE undo state |
| 20 | Larger canvas (193) | the canvas of the editor page, 956 x 540 | none | done: unchanged or larger |
| 21 | Flip, shift, reset curves (not in the manual; the task file asks for them) | none | actions | done [6]: MORE > EDIT: FLIP HORIZONTAL, FLIP VERTICAL, SHIFT LEFT / RIGHT (one grid column, wraps around), RESET ALL CURVES, SELECT ALL |
| 22 | TYPE (Normal, Path, Chaos Lorenz / Rossler, S and H) (191) | the shape picker of the LFO page and the random LFOs (RND 1 to 4) | elsewhere | elsewhere (LFO page, `lfo-screens.md`) |
| 23 | MONO, DIRECTION, HOST, BPM / HZ, RATE, TRIP / DOT, RISE, DELAY, SMOOTH time, SWING (191 to 193) | all of them on the LFO page | elsewhere | elsewhere (LFO page) |
| 24 | Modulating LFO points (X / Y / curve of a point from a bus; 198 to 200) | none | needs engine support | NOT DONE: `ContentPoint::modX / modY / modCurve` exist in the state, but the own engine does not evaluate them (`core/engine-own` has no reader of them: `ShapeData::evaluate` is pure) and the Q / modulation matrix has no bus concept for them. It needs an engine feature (per-voice evaluation of point modulation and a bus table), not a UI. Reported to the owner as an open question |
| 25 | Start point of the envelope mode (the "start" of the context menu, 190) | none | no separate stored start | done through PHASE: in the modes Retrigger and Envelope the marker is the start position (section 1); a start point DIFFERENT from the phase is not in the engine |

## 3. Page design (1280 x 628, the tab; margin 8)

```
+--------------------------------------------------------------------------------------------------------------+
| BACK | POINT FLAT RAMP+ RAMP- SELECT SOFT BEND | SHAPES | MORE | UNDO | DELETE |   status / 128 POINTS         |  top bar 56 px
+--------------------------------------------------------------------------------------+-----------------------+
|                                                                                      | SELECTED POINT        |
|                                                                                      |  [POINT] [X]          |
|                                  CANVAS  956 x 540 (as before)                       |  [Y]     [CURVE/SCALE]|
|        grid, curve, points, curve handles, the MARKER (line + 56 px handle at the    | GRID                  |
|        top), the selection rectangle, the selection, the rainbow of SOFT             |  [GRID X][GRID Y][SNAP]|
|                                                                                      | TRIGGER               |
|                                                                                      |  [MODE (stepper)]     |
|                                                                                      |  [PHASE] [BEND ALL]   |
|                                                                                      |  (o) LOOP  (o) SMOOTH |
+--------------------------------------------------------------------------------------+-----------------------+
```

- Top bar (y +8, 56 px high): BACK 96, the seven tools (`ToolSelector`, 80 px each: POINT, FLAT, RAMP+, RAMP- keep their indices 0 to 3, SELECT 4, SOFT 5, BEND 6), SHAPES 100 (opens the list of shapes), MORE 84 (opens the list of actions), UNDO 84, DELETE 96; at the right the status text (the result of a save / load / copy for three seconds) and "n / 128 POINTS". The tools are data (the table of `ToolSelector`).
- Canvas: `rect.x + 8, rect.y + 80, w 956, h 540`, as before. The marker: a vertical line at the PHASE position with a round handle (56 px touch zone) at the top; dimmed with a hint text in the modes where it does not apply.
- Right column (x = right - 8 - 300, 300 px wide), three titled Q groups and two LED toggles:
  - EDIT: POINT, X, Y, CURVE (2 x 2 cells of 146 x 56, the lane of the chips above each row). With two or more points selected: POINT shows the count ("3 SEL"), X and Y are the centre of the selection (moving them moves the points together, the order is kept), CURVE becomes SCALE (the height of the selection, 0 to 1: scales it around its centre).
  - GRID: GRID X, GRID Y, SNAP (three cells of 94 x 56).
  - TRIGGER: MODE (full width stepper with its list), PHASE and BEND ALL.
  - LOOP and SMOOTH: `LedToggle`, 146 x 56 each, in one row under the groups.
- Q slots. The router has 8 virtual targets (`QRouter::kNumVirtual`) and 16 slots. Virtual: 0 POINT, 1 X, 2 Y, 3 CURVE / SCALE, 4 GRID X, 5 GRID Y, 6 SNAP, 7 BEND ALL. Real parameters: MODE = `lfoN.sync_type`, PHASE = `lfoN.phase`. Active group on Q 1 to 4 (EDIT by default), the next groups on Q 5 to 10: GRID (5 to 8: GRID X, GRID Y, SNAP) and TRIGGER (9 to 11: MODE, PHASE, BEND ALL); at most 11 of the 16 slots are used. On the 16-knob device all groups are on the knobs at once, with the chips 1 to 11; on the 4-knob device the bank button steps through the groups (EDIT, GRID, TRIGGER) in the same order, the active one framed.
- The actions that are not values (flip, shift, reset curves, select all, grid presets, copy) are list entries of MORE (a two level list: EDIT, GRID, COPY), not Q targets. The tools can be picked by touch only (a tool is not a value).
- The popups use the library: `ListPopup` for SHAPES (two levels: the verbs, then the shapes), MORE and the confirmations (two rows), `TextEntry` for the name of a shape.
- What is not touch: nothing; the editor needs no mouse modifier. A modifier of the reference became: Shift-click = the tool FLAT / RAMP; Alt-drag = SNAP and the tool BEND; background drag = SELECT; Cmd-drag = SOFT; right-click menu = MORE and DELETE.

## 4. Storage of user shapes

The user library of the core (`core/include/synthyboy/user_library.h`) is `<userDir>/library/lfo_shapes/<name>.json` of the JUCE shells and is not instantiated on the MPC (the plugin has no user directory there). The MPC uses the same places as the user wavetables: `SynthyBoy/LFO Shapes/` below each of the roots the instance offers for the player's files (`IInstanceView::wavetableSources`: the SD card, the USB drives, the plugin's own `Content` folder), files `<name>.lfoshape.json` in the format of `synthyboy::serializeLfoShapeFile` (`{"format":"synthyboy-lfo-shape","version":1,"name":..,"smooth":..,"points":[..]}`), with one added key `"loop_point"` (the parser of the core skips keys it does not know, so every other reader keeps working). Listing, reading and writing happen on the overlay thread when a popup opens or a button is tapped (never in `draw`, never on the audio thread), with the safe file helpers (`shell-mpc/safe_file.h`: bounded reads, `writeReplacing`).
