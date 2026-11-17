# ShapeEditorPage, ShapeCanvas, ToolSelector

## What it is
The editor of a drawn LFO shape. `ToolSelector`: the four tools POINT, FLAT, RAMP+, RAMP-. `ShapeCanvas`: the drawing surface (grid, points, segment middles) where touches edit the shape model. `ShapeEditorPage`: the whole page: BACK, tools, UNDO, DELETE, canvas, the Q groups EDIT (POINT, X, Y, CURVE of the selected point) and GRID (GRID X, GRID Y, SNAP), loop point and smooth switches.

## Header and types
`shell-mpc/ui/shape_editor.h`: `ToolSelector` (`Tool { Point, Flat, RampUp, RampDown, kTools }`, `State { index }`), `ShapeCanvas` (`Op { Edited = 1, Selected = 2 }`, `State`), `ShapeEditorPage : Composite`. Model: `lfo::ShapeModel`, `lfo::Undo` ([helpers.md](helpers.md)).

## State
`ShapeCanvas::State`: `shape` (`lfo::ShapeModel*`, the owner's model, edited in place), `undo` (`lfo::Undo*`), `tool`, `gridX` 8, `gridY` 8, `snap` true, `selected` -1, `selectedSeg` -1. `kHitR` 28, `kMidR` 22, `kLongPressMs` 600. Helpers `plot()`, `pointAt`, `segAt`, `midHandle`, `xOf`, `yOf`, `xFrom`, `yFrom`.
`ShapeEditorPage` members: `back`, `tools`, `undoButton`, `del`, `snap`, `loop`, `smooth` (`ToggleIcon`), `canvas`, `fpoint`, `fx`, `fy`, `fcurve`, `fgx`, `fgy` (`NumberField`), `groups[2]`, `model`, `undo`; `activeGroup()`, `setActiveGroup (i)`, `turnQ`, `sync()` (refresh the fields from the model and canvas state after every change), `canvasRect()`. `kBarH` 72, `kColW` 300.

## Size
`ShapeCanvas::minSize()` 400 x 240; the page fills a tab.

## Touch and knob behaviour
Tool POINT: a tap on empty ground adds a point (snapped with SNAP) and the finger keeps it (drag moves it); touch on a point selects and drags it (end points only vertically); touch on the small handle in the middle of a segment drags its curve (power -20 to 20); a long press (600 ms) on a point deletes it. Tools FLAT, RAMP+, RAMP-: touch on a grid cell draws a flat piece or a ramp over its column; a drag across columns draws a staircase or a saw tooth.
Change: `Index`, `id` of the canvas, `index` = `Op`: `Edited` (the model changed: write it to your content state) or `Selected` (only the selection moved). The canvas pushes the undo state before a change.

## Q-Link
EDIT [POINT, X, Y, CURVE] and GRID [GRID X, GRID Y, SNAP]; `turnQ` goes to the active group.

## Options and variants
Snap on or off, grid sizes, loop point, smoothing.

## Minimal example
```cpp
ShapeEditorPage page;
page.place ({ 0, 110, 1280, 628 });
page.canvas.setId (500);
page.model.initLinear();                        // the page's own model (the canvas points at it); or load your stored points
Change c = page.touchDown (x, y, nowMs);
c = page.touchMove (x, y + 10);
if (c.id == 500 && c.index == ShapeCanvas::Edited)
{
    store (page.model);                         // owner: write it to the content state
    page.sync();                                // refresh the fields
}
```

## Used on
`shell-mpc/overlay/lfo_ui.h` (the editor page above the LFO page, opened by EDIT on the display corner). Picture: `docs/user-guide/img/lfo-editor.png`.

## Limits and pitfalls
- The component edits `model` in place and the undo stack is the owner's; keep both between updates.
- At most 128 points (`lfo::kMaxPoints`), 32 undo states.
- The canvas reports `Edited` after every change of a drag: throttle storing if it is expensive.
