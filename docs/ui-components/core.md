# core.h: the contract, Rect, Change, Component, Pen, Cell, TapGesture

## What it is
The base every component is built on: geometry, the `Change` that comes out, the `Component` interface, the drawing pen, the cached cell and the tap detector. Read this first; every other page assumes it.

## Header and types
`shell-mpc/ui/core.h` (includes only `overlay/gpu/scene.h`, `overlay/sdf_font.h`, `theme.h` and standard headers; free of SynthyBoy engine code). Namespace `synthyboy_mpc::ui`; aliases `gpu` (the scene) and `sdf` (the font).

## Types and fields
| Type | Content |
|---|---|
| `Size` | `int w, h` |
| `Rect` | `int x, y, w, h`; `right()`, `bottom()`, `empty()`, `contains (x, y)`, `contains (Rect)`, `overlaps`, `inset (d)`, `Rect::unite (a, b)` |
| `ChangeKind` | `None, Value, Index, Request, Depth, ModsRequest, AddRequest` |
| `Change` | `kind`, `id` (the owner's opaque integer), `value` (double), `index` (int); `explicit operator bool` is true when `kind != None`; factories `Change::none()`, `normalised (id, v)`, `indexed (id, i, v)`, `request (id)`, `depth (id, source, d)`, `modsRequest (id)`, `addRequest (id, source)` |
| `ModEntry` | `source`, `name[7]`, `depth` (-1 to +1), `bidirectional`, `bypassed`, `macro`; `colour()` |
| `ModInput` | `mods[kMaxMods = 8]`, `numMods`, `selectedSource` (`kNoSource`), `add (...)`, `selected()` |
| `TabState` | `Absent, Grey, Coloured, Hollow`; `tabState (ModInput)`, `depthArc`, `tabName`, `tabRect`, `tabHitRect` |
| `colour::` | `rgba`, `background()`, `panel()`, `rest()`, `accent()`, `accentDark()`, `text()`, `white()`, `q()` (read from the active look) |
| `kQLane` | 16: the strip above a Q member that holds its chip |
| `setText (dst, cap, src)` | bounded copy, never cuts a UTF-8 sequence |

## The Change kinds
- `Value`: `value` 0 to 1 (knob, bars, faders).
- `Index`: `index` is a whole number (list position, switch 0 / 1, steps from `min` of a number field); `value` is the same as a double (a number field: the value in its units).
- `Request`: the user asked for a menu, page or details; the owner opens it.
- `Depth`: `index` = modulation source id, `value` = new depth, -1 to +1.
- `ModsRequest`: a long press asks for the list of modulations of the control.
- `AddRequest`: `index` = the source id the owner offered (the add row of a modulations page, a chip of the board).

## Component interface
`Component` (abstract; non-copyable):
- Input: `place (Rect)`, `setEnabled (bool)` (disabled: dim, no reaction, a running gesture is dropped), `setQ (frame, index)` (the member of the active Q group, index 1 to 4), `setId (int)`.
- Size: `minSize()` (pure virtual), `sizeOk()` (rect >= `minSize()`; a `LedList` says true when any arrangement fits).
- Drawing: `draw (gpu::SceneBuilder&) const`. Only ops inside the rectangle; no allocation, no locks, no I/O; the same state gives the same ops.
- Events, each returns a `Change`: `touchDown (x, y, nowMs)`, `touchMove (x, y)`, `touchUp (x, y)`, `tick (nowMs)` (call it regularly while a finger is down: long press), `turn (delta)` (a hardware knob), `wheel (notches)`, `wheelOver (x, y, notches, Change& out)` (a composite asks its children). `wheelUnit()` says what one notch is.
- A gesture that went down inside the rectangle is kept until touch up, even outside it; a touch that goes down outside is ignored (`capturing()`).
- Structure: `touchTargets (Rect* out, int max)`, `collectLeaves (Component** out, int n, int max)`, `primaryChild()`.
- To write a component: derive, implement `minSize()`, `drawBody (Pen&) const`, and `onDown`, `onMove`, `onUp`, `onTurn`. A composite derives from `Composite` ([composites.md](composites.md)).

## Touch rules
A tap is a gesture that never left a square of `TapGesture::kSlop` = 6 px. A double tap: a second tap within `kDoubleMs` = 400 ms and `kDoubleDist` = 30 px. A long press: 600 ms without leaving the square (`Knob::kLongPressMs`, needs `tick`). Touch targets are at least 56 px in the smaller direction where the rectangle allows.

## Pen
`Pen (SceneBuilder&, enabled)`; every colour passes through `c (...)` which dims a disabled component. Shapes: `rect`, `frame`, `line`, `arc`, `disc`, `rrect`, `block (r, hi)` (a panel), `control (r, q, hi)` (a control box; a Q member draws no frame of its own), gradients `paintX`, `paintY`, `paintArc`, `paintY2`, `paintX2`, state `style`, `plain`, `paintOff`. Text: `text`, `textCentered`, `textLeft`, `textRight` (fit with an ellipsis, never below `minSize` argument), `fitSize`, `textWidth`. Icons: `chevron`, `iconGear`, `iconBell`, `iconExpand`, `iconEnvelope`, `iconRoute`; chips: `modTab`, `modTabBox`, `qIndex`, `qChip`. Every function that sets a state puts it back.

## Cell
`Cell cell (sb, rect, pad = 2)` (RAII): everything drawn while it lives is a cached cell of the scene; the helper redraws it only when its content or rectangle changed. The rectangle must contain everything drawn inside. `Component::draw` makes one cell per leaf; a `Composite` makes one for the under- and over-drawing. See [docs/MPC.md](../MPC.md) "Cached cells and layers".

## Minimal example
See the example in [README.md](README.md): a knob on a page. A custom component:
```cpp
class Dot : public Component
{
public:
    bool on = false;
    Size minSize() const override { return { 56, 56 }; }
protected:
    void drawBody (Pen& p) const override { p.disc (rect_.x + 28.0f, rect_.y + 28.0f, 10.0f, p.c (on ? 0xffffffffu : 0x666666ffu)); }
    Change onUp (int, int) override { return Change::indexed (id_, on ? 0 : 1, on ? 0.0 : 1.0); }
};
```

## Limits and pitfalls
- The component never stores the truth. After a change the owner writes the new value back.
- A change that would not change the value is not reported.
- Always call `place` before events; `Composite::place` lays out the children.
- Draw inside the cell rectangle: the layout lint (rule d) flags text wider than its cache cell.
