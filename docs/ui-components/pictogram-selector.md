# PictogramSelector

## What it is
A row (or column) of cells, each a small drawn picture with a short label under it; one is highlighted. Use it for a choice whose values are better seen than read: the oscillator type (wavetable, sample, granular ...), the sub waveform, the LFO or envelope instance numbers. For plain text choices use [LedList](led-list.md).

## Header and types
`shell-mpc/ui/base.h`: `PictogramSelector`, `PictogramSelector::Item`, `PictogramSelector::State`, namespace `pictogram` (`Id`, `Def`, `kTable`). Derived: `TypeSelector` (`base.h`), `EnvSelector` (`env_screen.h`).

## State fields
`PictogramSelector::State`: `count` (0), `index` (0), `items[kMaxItems]` (`kMaxItems` = 10).
`Item`: `pictogram` (an id of `pictogram::Id`), `label` (`char[16]`, 6 characters shown), `enabled` (true).
`pictogram::Id`: `Sine, Wavetable, Granular, Multisample, Noise, HalfSine, Triangle, Saw, Square, Pulse, Sample, Spectral`, then `Count`. An unknown id draws a text cell.
Members: `int narrowestCell = 56`. Helper `setItem (i, pictogramId, label, enabled = true)` (grows `count`).

## Size
`kCellW` 76 and `kCellH` 56; `cellMinW()` follows the longest label (at least `narrowestCell`). `minSize()` is `count` cells. The cells share the width equally; a rectangle taller than wide (`vertical()`) stacks them in a column.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap on an enabled cell (on touch up, from the cell the touch went down in) | `Index`: `index` = `value` = the cell |
| `turn (delta)` | steps through the ENABLED cells, no wrap, `Index` |

`cell (i)` is the rectangle of a cell.

## Q-Link
Can be a Q member (it is the first member of the Basic group of an oscillator). One slot, stepped.

## Options and variants
A new pictogram is a new row of `pictogram::kTable` (id, name, draw function, around a 28 x 24 px box); a drawing function uses the `Pen` helpers. `narrowestCell` lowers the minimum width for a bar of many short labels (the LFO instance bar uses 60 px cells).

## Minimal example
```cpp
PictogramSelector type;
type.setItem (0, pictogram::Wavetable, "WAVE");
type.setItem (1, pictogram::Sample, "SAMPLE");
type.setItem (2, pictogram::Granular, "GRAIN");
type.setId (70);
type.place ({ 20, 20, 3 * PictogramSelector::kCellW, 56 });
type.state.index = current;
Change c = type.turn (+1);                   // or touch events
if (c.kind == ChangeKind::Index && c.id == 70) current = c.index;
```

## Used on
`BasicControls::selector` (oscillator type, sub waveform), `InstanceBar::lfoCells` and `rndCells`, `EnvFullScreen::instances` (`EnvSelector`; `EnvCompact::instances` uses `EnvCarousel`). `TypeSelector` (base.h): a `PictogramSelector` with `dropdown`: one box with the current item's pictogram and name and a chevron; a tap is a `Request`, the turn and the Index change are the selector's; the compact oscillator view uses it with a `ListPopup` whose `icons` carry the pictograms. Pictures: `docs/user-guide/img/osc-a.png`, `osc-sub.png`, `lfo.png`.

## Limits and pitfalls
- More than 10 items are dropped. The MPC.md section says "up to 8 items"; the header says 10.
- A disabled cell is dim and skipped by `turn`, but `index` may still point at one: the owner keeps `index` valid.
- Labels over 6 characters make the cells wider and can break the layout.
