# LfoCompact and LfoFullScreen

## What it is
The two views of the LFO element. The element has two types: an LFO (LFO 1 to 10, a shape of one cycle) and a RANDOM LFO (R1 to R4, a style); `configure (random)` swaps the parts and the Q groups, the places stay.
- `LfoCompact`: one third (about 408 x 620): the instance carousel, the display (a header band at its top with the label and the full-screen button at its right end; the curve lies in the plot area below the band), and the Q group RATE under it.
- `LfoFullScreen`: top bar (close, the 14 instance cells, HZ / BPM / KEY with TRIP and DOT, MONO, targets), display at the left, shape or style picker at the right, and a bottom band of titled blocks, one Q group each.

## Header and types
`shell-mpc/ui/lfo_screen.h`: `LfoCompact`, `LfoFullScreen`, `LedSwitchProxy`. Parts in [lfo-parts.md](lfo-parts.md); the display in [display.md](display.md) (`lfodisplay`).

## State
`LfoCompact` members: `instance` (`Carousel`, 14 entries, `listable`, wraps), `full` (`MenuButton`, `Expand`), `shape`, `style`, `sync` (`Stepper`, listable), `rate` (`RateControl`), `rise`, `smoothTime` (`Knob`), `stereo` (`LedToggle`), `group` (`QGroup`). API: `configure (bool random)`, `random()`, `displayRect()`, `qGroup()`; constants `kQuadH` 248, `kQuadRandomH` 304, `kRowH` 56, `kRateH` 72. LFO group: [SHAPE, RATE or DIVISION, RISE, SMOOTH time]; random: [STYLE, RATE or DIVISION, SYNC, STEREO].
`LfoFullScreen` members: `close`, `bar` (`InstanceBar`), `syncSwitch`, `monoTop` (`ToggleIcon`), `targets` (`DetailButton`, Route), `picker` (`ShapePicker`), `hostPick` (`LedToggle`), `smoothSwitch` (`LedSwitchProxy`), `edit` (`MenuButton`), RATE group: `shape`, `style`, `rate`, `rise`, `smoothTime`; TIMING / TRIGGER: `sync`, `mode`, `modeList`, `phase`, `delay`; MOTION: `direction` (`LedList`), `swing`, `stereo`, `monoQ`, `stereoToggle`; KEY: `keyTranspose`, `keyTune` (`NumberField`); `groups[kMaxGroups = 6]`. API: `configure (random)`, `setSync (sync)`, `setKeyActive (on)`, `keyActive()`, `keyGroup()`, `displayRect()`, `block (i)`, `numBlocks()`, `numGroups()`, `groupName (i)`, `activeGroup()`, `setActiveGroup (i)`, `turnQ`, `setPageMode (bool)`, `leaves`. Constants `kBandH` 290, `kBandRandomH` 196, `kTopH` 64, `kPickerCols` 5.
`LedSwitchProxy`: the Q member of the LED on the label of a knob (`Knob::state.led`): a `LedToggle` that is not drawn and takes no touch; `follow (const Knob&)` places it; a Q-Link step on it switches (up on, down off).

## Size
`LfoCompact::minSize()` 400 x 560 (about 408 x 620 on the Play page); `LfoFullScreen::minSize()` 1200 x 600 (1280 x 628 on the tab). The compact view's SHAPE / RATE / RISE / SMOOTH block (232 px) sets the shared height of the three Play thirds.

## Touch and knob behaviour
Children keep their changes: `Carousel` and steppers `Index` / `Request`, `LedList` `Index`, knobs `Value`, `full`, `close`, `edit` `Request`, `targets` `Request`. The sync value is the owner's: use `SyncSwitch::nextSync (part, change)` to turn a mode or TRIP / DOT change into the sync parameter value.

## Q-Link
Full: RATE (shape grid, RATE, RISE, SMOOTH), KEY (TRANSPOSE, TUNE; only in the key follow sync), TIMING (HZ / BPM / KEY list, PHASE, DELAY, MODE), MOTION (DIRECTION, SWING, STEREO, MONO), SHAPE (HOST, SMOOTH); random LFO: RANDOM (style grid, RATE, HZ / BPM / KEY, STEREO), KEY, TRIGGER. The edit page: EDIT, GRID ([shape-editor.md](shape-editor.md)).

## Options and variants
`configure (random)`, `setKeyActive` (TRANSPOSE and TUNE replace the RATE knob in the key follow sync), `setPageMode`.

## Minimal example
```cpp
LfoFullScreen lfo;
lfo.place ({ 0, 110, 1280, 628 });
lfo.configure (false);                       // an LFO (true: a random LFO)
lfo.setSync (syncParam);                     // HZ / BPM / KEY list and TRIP / DOT follow it
lfo.rate.rate.setId (410);                   // RateControl::rate is the knob
lfo.bar.lfoCells.setId (400);
Change c = lfo.touchDown (x, y, nowMs);
c = lfo.touchUp (x, y);
if (c.id == 400 && c.kind == ChangeKind::Index) selectLfo (c.index);
```

## Used on
`shell-mpc/overlay/lfo_ui.h` (`LfoUi`, `kElementPage` 3, the largest UI file), `play_view.h`. Pictures: `docs/user-guide/img/lfo.png`, `lfo-bpm.png`, `lfo-key.png`, `lfo-random.png`, `play-lfo.png`.

## Limits and pitfalls
- A control that appears twice for one parameter (for example the sync stepper and the HZ / BPM / KEY list) is a design error: one control per value.
- `LedSwitchProxy` is no child of a composite; place it with `follow` after the knob is placed.
- TRIP and DOT only work in the BPM mode.
