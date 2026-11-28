# ScalarCurveEditor (the NOTE / VELO curve editor)

## What it is
The editor of the two transfer curves the modulation sources `Note` (24) and `Velocity` (23) are read through (step 5 of the Env / Filter / LFO merges). One component for both sources (`setSource`). A Serum-like small graph with the curve, point handles and the fields of the selected point, LEGATO with a read-only GLIDE chip, and the list of the mod slots that use the source. It is a component and its page logic only: the Filter tab (step 4) embeds it as its NOTE and VELO states.

## Files
- `shell-mpc/ui/scalar_curve_editor.h`: `scurve::ScalarCurve` (model), `scurve::History` (undo), `ScalarCurveCanvas` (graph and touch), `ScalarCurveEditor` (the screen, a `Composite`), `ScalarCurveEvent`.
- `shell-mpc/overlay/scalar_curve_ui.h`: `ScalarCurveUi` (page logic: read, throttled write, Q targets, USED BY scan, the live marker from `IInstanceView::lastPlayed`). Not a registered page: the Filter tab holds one per source as its NOTE and VELO states (step 4, `filter_ui.h`).
- `ScalarUsedByList` (in the same header): the USED BY rows, a display part of the editor (no touch); the labels of the NOTE axis come from `ui::keyName` (`note_mask.h`, the MPC convention key 60 = C3, key 0 = C-2): C-2, C1, C3, C5, G8; `drawLive` / `setMarker` draw the live x marker (a vertical line to the curve and a dot) on the animated layer only (the cached `draw` does not know it).
- `IInstanceView::setScalarCurve (which, points)` (`instance_view.h`), request / poll in `synthyboy_mpc.cpp` (`requestScalarCurve`, `scalarCurvePoll`), the copy of the LFO shape hand-over.
- Needs the core (`synthyboy/shapes/shape.h`): link `synthyboy_core`; not part of `ui.h`.

## The data
The points are CONTENT (`StateContent::velocityCurve` / `noteCurve`, `kMaxCurvePoints` = 64), not parameters: x, y 0 to 1, `power` -20 to 20 = the bend of the segment after the point. An EMPTY vector is the identity. Only the two keys `curve.velocity.legato` / `curve.note.legato` (0 / 1) are parameters. Decisions:
- The identity is always the two points (0, 0), (1, 1) with power 0 in the editor and the EMPTY vector in the content (`ScalarCurveUi::contentOf`); a curve edited back to exactly that collapses to empty.
- The first x is 0 and the last x is 1 (fixed, the engine's `ShapeData::sanitize` forces it); the ends move in y only; a middle point keeps `kMinGap` = 0.01 of the width from its neighbours (the loaded content may have equal x, a vertical step: it stays editable by y).
- The value at x is the engine's own: the points go through `shapes::ShapeData` (`sanitize`, `bake` 2048, `evaluateTable`), the code of `own::ContentHub::publish` and `Voice::curvedSource`; the identity reads x; the result is clamped to 0 .. 1. Nothing is copied. `ScalarCurve::at` equals the engine bit for bit (test).
- x is the key / velocity number over 127 (the engine's scale). The X field shows 0 to 127, Y 0 to 100 %, CURVE -20 to 20 in steps of 0.5.
- GRID X, GRID Y, SNAP are view settings of the editor (default 4, 4, off), not stored anywhere (a curve has no grid field in the content).

## State and owner calls
`setSource (Source::Velocity | Note)`, `setCurve (pts, n)` (not while a finger is down; the owner skips it while `canvas.capturing()` and for 600 ms after a write), `setLegato`, `setGlide ("120 ms")` (read-only chip, only drawn with LEGATO on, the time is `master.portamento.time`), `setUsedBy (UsedBy)` (rows: destination, percent, polarity, AUX tag, bypass; `total` counts all, the list shows 4 and "+ n more"; empty: "NOT ROUTED: tap TARGETS to add").

## Touch
| Gesture | Result |
|---|---|
| drag a handle (44 px target) | moves the point (the finger keeps its offset), x clamped between the neighbours, SNAP to the grid; `Index { kEdit }`, at the end `kEditEnd` |
| tap on the empty graph | adds a point ON the curve at that x (x snapped with SNAP), selects it; `kEditEnd`; refused at 64 points or within the gap of a point |
| vertical drag on the empty graph | bends the segment under the finger so that its middle reaches the finger's height (`lfo::powerForMid`); a flat segment cannot be bent |
| long press (600 ms) on a middle point | deletes it (never an end, never below 2 points) |
| tap on a handle | selects it (`kSelect`) |

`ScalarCurveEditor::apply (Change)` turns a change of any part into an `ScalarCurveEvent` { Curve (final?), Legato (value), Targets }. Buttons: INIT (identity), UNDO (16 steps, one gesture = one step), DELETE POINT; a number field edit is one undo step.

## Q groups
Q1 EDIT [POINT, X, Y, CURVE], Q2 [LEGATO, GRID X, GRID Y, SNAP] (`groups[]`, `qMember (g, slot)`, `ScalarCurveUi::qTargets (g, t)`): POINT, X, Y, CURVE, GRID X, GRID Y, SNAP are virtual targets 0 to 6 (`virtualValue` / `applyVirtual` / `updateVirtuals`), LEGATO is the parameter.

## Geometry
Designed for the Filter tab body 1280 x 556 (`minSize` 1100 x 500): the graph (at least 640 x 300) at the left, at the right INIT / UNDO / DELETE POINT, the glide chip and the USED BY block with TARGETS, at the bottom the two Q blocks. Axis labels: VELO 0, 32, 64, 96, 127; NOTE C-1, C1, C3, C5, G9 (the labels of the desktop panel; key 0 is C-2 in the convention where key 60 is C3, see TODO.md). The line is in the source colour (`theme src[2]` velocity, `src[3]` note), handles 10 px, the identity diagonal faint when the curve is custom, no glow.

## Writes
`ScalarCurveUi`: curve edits go to `IInstanceView::setScalarCurve` at most every 90 ms during a gesture and at its end; LEGATO through `writePlain`; TARGETS sets `targetsRequested` (a request to the page owner, the existing `ModTargetsPage`, no host tab change). Nothing else is written (`master.velocity_track` stays on the Master page).

## Tests and pictures
`shell-mpc/ui/tests/ui_scalar_curve_host.cpp` (model, engine equality, undo, gestures, fields, Q members, layout, drawing), `shell-mpc/overlay/tests/scalar_curve_ui_host.cpp` (USED BY, read / write, throttle, hold, legato, Q targets, state round trip).
