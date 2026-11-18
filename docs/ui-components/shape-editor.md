# ShapeEditorPage, ShapeCanvas, ToolSelector, PointModPage

## What it is
The editor of a drawn LFO shape; everything the reference editor can do (the table: [../ui-design/lfo-editor-capabilities.md](../ui-design/lfo-editor-capabilities.md)), adapted to a touch screen with 16 knobs. `ToolSelector`: the seven tools POINT, FLAT, RAMP+, RAMP-, SELECT, SOFT, BEND. `ShapeCanvas`: the drawing surface (grid, points, segment middles, the marker, the selection) where touches edit the shape model. `ShapeEditorPage`: the whole page: BACK, tools, SHAPES, MORE, UNDO, DELETE, MOD, canvas, the Q groups EDIT (POINT, X, Y, CURVE of the selected point), GRID (GRID X, GRID Y, SNAP) and TRIGGER (MODE, PHASE, BEND ALL), LOOP and SMOOTH. `PointModPage`: the page above it for the modulation of the selected points.

## Header and types
`shell-mpc/ui/shape_editor.h`: `ToolSelector` (`Tool { Point, Flat, RampUp, RampDown, Select, Soft, Bend, kTools }`, `State { index }`), `ShapeCanvas` (`Op { Edited = 1, Selected = 2, Marker = 3 }`, `MarkerKind { MarkerNone, MarkerDim, MarkerStart, MarkerLoop }`, `State`), `ShapeEditorPage : Composite`, `PointModPage : Composite`. Model: `lfo::ShapeModel`, `lfo::PointSet`, `lfo::Undo` ([helpers.md](helpers.md)).

## State
`ShapeCanvas::State`: `shape` (`lfo::ShapeModel*`, the owner's model, edited in place BY DESIGN), `undo` (`lfo::Undo*`: the canvas pushes the state before the first change of a gesture, so one gesture is one undo state), `tool`, `gridX` 8, `gridY` 8, `snap` true, `selected` -1 (the point the fields show), `selectedSeg` -1, `marks` (`lfo::PointSet`: the selection when it has points of its own; empty: the selection is the point `selected`), `markerX` (0 .. 1, < 0 none) and `markerKind` (both from the owner: the marker is the PHASE of the LFO). `kHitR` 28, `kMidR` 22, `kLongPressMs` 600, `kMarkerR` 28, `kMarkerZone` 56, `kSoftRadiusPercent` 25. Helpers `plot()`, `pointAt`, `segAt`, `midHandle`, `xOf`, `yOf`, `xFrom`, `yFrom`, `selection()` (the selection as a set), `selectOnly (i)`, `selectAll()`, `markerZone()`.
`ShapeEditorPage` members: `back`, `tools`, `shapes`, `more`, `undoButton`, `del`, `modButton` (`MenuButton`), `snap`, `loop`, `smooth` (`LedToggle`), `canvas`, `fpoint`, `fx`, `fy`, `fcurve`, `fgx`, `fgy`, `fphase`, `fbend` (`NumberField`), `mode` (`Stepper`, listable), `groups[3]`, `model`, `undo`; `activeGroup()`, `setActiveGroup (i)`, `turnQ`, `sync()` (refresh the fields from the model and canvas state after every change), `setTrigger (syncType, modeName, modeCount, phaseDegrees)` (the stepper, the field and the marker; `markerKindOf (syncType)`), `setStatus (text)`, `canvasRect()`. `kBarH` 72, `kColW` 300.
`PointModPage` members: `back`, `target` (stepper TIME / LEVEL / CURVE), `amount` (-100 .. 100 percent), `remove`, `board` (`ModSourceBoard`, filled by the owner), `group` (one `QGroup`: [`target`, `amount`] on Q 1 and Q 2, always the active group; frame on the 4 knob device, chips 1 and 2 on the 16 knob device); `load (which, sourceName, amountPercent, points, firstPoint)`. The controls stand one `kQLane` below the top of the page (the lane of the chips). The knobs are the owner's virtual targets 8 (MODULATE) and 9 (AMOUNT): a turn is a write of the host to the Q proxy that `LfoUi::updatePointMod` takes, not a `Change` of the component.

## Size
`ShapeCanvas::minSize()` 400 x 240; the page fills a tab (1280 x 628): the canvas is 956 x 540, the column at the right 300 wide.

## Touch and knob behaviour
Tool POINT: a tap on empty ground adds a point (snapped with SNAP) and the finger keeps it (drag moves it); touch on a point selects and drags it (end points only vertically; a selected group moves together, clamped as a whole so that the order and the form stay); touch on the small handle in the middle of a segment drags its curve (power -20 to 20). QUICK DELETE: a double tap on a point deletes it (never an end point, and never the point the first tap of the pair has just added), a double tap on a curve handle sets the curve straight, a long press (600 ms) on a point deletes it as well; DELETE deletes the selection (the end points stay).
Tools FLAT, RAMP+, RAMP-: touch on a grid cell draws a flat piece (at the height of the finger, on a grid line with SNAP) or a ramp over its column; a drag across columns draws a staircase or a saw tooth in ONE gesture: every column between two events is drawn (the finger height interpolated), so a fast finger leaves no gap; the whole drag is one undo state; a shape of 128 points takes no more.
Tool SELECT: a drag on empty ground draws a rectangle that selects the points inside it; a tap on a point adds it or (when it is selected) removes it; a tap on empty ground clears the selection; dragging a selected point moves the whole selection. With several points selected the fields X and Y are the centre of the selection (the group moves, the order is kept) and the third field is SCALE (the height of the selection around its centre), POINT shows "n SEL".
Tool SOFT: dragging a point moves the selected points (two or more), or every point within 25 percent of the cycle, with it, the nearer the more (weights drawn as a rainbow). Tool BEND: dragging the middle handle of a segment bends all curves by the same amount; BEND ALL (field and Q knob) is the mean curve.
Marker: the vertical line at the PHASE with a 56 px handle at its top (tools POINT, SELECT, SOFT, BEND): in a loop mode (sync types 3, 4, 5) it is LOOP and snaps to the nearest point (the loop point of the model follows), in retrigger and envelope (0, 2) it is START and sticks to a point within 14 px, in free-run (1) it is dimmed with a hint and cannot be dragged. The LOOP toggle sets the loop point at the selected point.
Change: `Index`, `id` of the canvas, `index` = `Op`: `Edited` (the model changed: write it to your content state), `Selected` (only the selection moved) or `Marker` (`value` = the new position 0 .. 1: write the PHASE; the model's loop point may have changed too).

## Q-Link
EDIT [POINT, X, Y, CURVE / SCALE], GRID [GRID X, GRID Y, SNAP] and TRIGGER [MODE, PHASE, BEND ALL]; `turnQ` goes to the active group. 10 virtual targets (0 to 7 the editor, 8 and 9 the point modulation page) and two real parameters (`lfoN.sync_type`, `lfoN.phase`), 11 of the 16 slots (see the design table).

## Options and variants
Snap on or off, grid sizes, loop point, smoothing, the seven tools; the actions that are not values (flip, shift, reset curves, select all, grid presets, copies, the saved shapes) are lists the owner opens from SHAPES and MORE.

## Minimal example
```cpp
ShapeEditorPage page;
page.place ({ 0, 110, 1280, 628 });
page.canvas.setId (500);
page.model.initLinear();                        // the page's own model (the canvas points at it); or load your stored points
page.setTrigger (4, "Loop at point", 6, 90.0);  // the marker is the PHASE
Change c = page.touchDown (x, y, nowMs);
c = page.touchMove (x, y + 10);
if (c.id == 500 && c.index == ShapeCanvas::Edited)
{
    store (page.model);                         // owner: write it to the content state
    page.sync();                                // refresh the fields
}
else if (c.id == 500 && c.index == ShapeCanvas::Marker)
    writePhase (c.value * 360.0);               // owner: the PHASE of the LFO
```

## Used on
`shell-mpc/overlay/lfo_ui.h` (the editor page above the LFO page, opened by EDIT on the display corner; the lists SHAPES and MORE, the name keyboard, `PointModPage`, the saved shapes `lfo_shape_files.h`). Pictures: `docs/user-guide/img/lfo-editor.png`, the states `lfo_editor_*` of the gallery.

## Limits and pitfalls
- The component edits `model` in place and the undo stack is the owner's (the owner pushes before its own changes too: one operation, one state); keep both between updates.
- At most 128 points (`lfo::kMaxPoints`), 32 undo states.
- The canvas reports `Edited` after every change of a drag: throttle storing if it is expensive (`LfoUi` coalesces at 90 ms and at the end of the gesture).
- The loop point is a bookmark: the voice reads the PHASE (the marker), not the stored index (`docs/ui-design/lfo-editor-capabilities.md` section 1).
- The router has 10 virtual targets (0 to 7 the editor, 8 MODULATE and 9 AMOUNT of `PointModPage`): a new knob value needs a real parameter or a bigger `QRouter::kNumVirtual` (and every mock of the instance that sizes its virtual arrays: `demo_common.h`, `mod_ui_host.cpp`, `ui_gallery_extra_host.cpp`).
