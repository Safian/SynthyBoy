# ShapePicker, SyncSwitch, RateControl (lfo_parts.h)

## What it is
The parts of the LFO element that the oscillator did not need.
- `ShapePicker`: a grid of cells with the shape drawn in each (13 shapes of an LFO, or the 5 styles of a random LFO).
- `SyncSwitch`: the LOGIC of HZ / BPM / KEY with TRIP and DOT (no component).
- `RateControl`: the rate knob in Hz mode, the division stepper in BPM mode, the knob dim in KEY mode.
- The LFO / random LFO selector is [UsedSelector](used-selector.md) (`InstanceBar`, the 14 fixed cells, was deleted with the LFO merge).

## Header and types
`shell-mpc/ui/lfo_parts.h`: `ShapePicker`, `SyncSwitch`, `RateControl : Composite`, `styleName (style)`. Uses `lfo_model.h` (`lfo::kNumShapes` 13, `lfo::SyncMode`).

## State
`ShapePicker::State { index 0, cols 4, styles false }`; `kCells` 13, `kStyles` 5; `count()`, `rows()`, `cell (i)` (i may exceed the last item: free cells for the owner's own buttons).
`SyncSwitch`: `mode` (`LedList` HZ, BPM, KEY), `setSync (sync)`, `sync()`, `trip()`, `dot()`, `nextSync (part, change)` with `Part { Mode, Trip, Dot }`. The sync value: 0 Free (Hz), 1 Tempo, 2 dotted, 3 triplet, 4 Key follow.
`RateControl`: `rate` (`Knob` "RATE"), `division` (`Stepper`), `modeCells` (`LedList`), `trip`, `dot` (`LedToggle`); `setShowMode (bool)`, `setTripDot (bool)`, `setSync (sync)`, `setMode (lfo::SyncMode)`, `mode()`, `kModeW` 210; the primary child is whichever of knob and stepper is shown.

## Size
The others follow their children.

## Touch and knob behaviour
`ShapePicker`: tap on a cell gives `Index` (new item, on touch up); `turn` steps (no wrap).

## Q-Link
`ShapePicker` can be a Q member (the shape grid is a member of the RATE group; the group is frameless because the members lie apart). `RateControl` is one member whose `turn` goes to the shown child.

## Options and variants
`styles` true for the random LFO; `setTripDot (true)` on the full screen.

## Minimal example
```cpp
ShapePicker picker;
picker.state.cols = 5;
picker.setId (420);
picker.place ({ 700, 120, 540, 240 });
picker.state.index = shapeIndex;
Change c = picker.turn (+1);
if (c.kind == ChangeKind::Index && c.id == 420) shapeIndex = c.index;

SyncSwitch sync;  sync.setSync (syncParam);
// on a change of sync.mode (id 421): syncParam = sync.nextSync (SyncSwitch::Mode, change);
```

## Used on
`LfoFullScreen::picker`, `syncSwitch`, `bar`; `LfoCompact::rate`. `shell-mpc/overlay/lfo_ui.h` uses `SyncSwitch` directly. Pictures: `docs/screens/lfo.jpg`, `lfo-random.png`.

## Limits and pitfalls
- `SyncSwitch` is not a component: the screen owns the controls; do not `place` it.
- A mode change keeps the TRIP / DOT switches; one switch excludes the other.
- In the KEY mode the rate knob is dim: the rate follows the key.
