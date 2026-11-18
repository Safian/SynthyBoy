# OscillatorCompact and OscillatorFullScreen

## What it is
The two views of one oscillator instance (Sub, A, B, C, Noise), built only from library components and layout presets, drawn for 1280 x 800 (other sizes scale in proportion).
- `OscillatorCompact`: one third of the width on the Play page (about 420 x 620): header row (power, source selector, route, full-screen button), the display area, and the Basic group.
- `OscillatorFullScreen`: the whole tab: top band, display area with the type bar under it, the tuning column at the right, and the knob panel of the oscillator type at the bottom.
Use them as the reference for a screen that is a layout preset over a set of components.

## Header and types
`shell-mpc/ui/screen.h`: `OscillatorCompact`, `OscillatorFullScreen` (both `Composite`). Uses `OscInstance`, `OscKind`, `Carousel`, `BasicControls`, `TypeBar`, `TuneRow`, `KnobPanel`, `playrows`.

## State
`OscillatorCompact` members: `instances` (`Carousel`: Sub, Osc A, Osc B, Osc C, Noise; `listable`, wraps), `power` (`ToggleIcon`, `Power`), `route` (`RouteButton`), `full` (`MenuButton`, `Expand`), `basic` (`BasicControls`, stacked). API: `setInstance (OscInstance, OscKind)`, `displayRect()` (the rectangle shared by the three thirds, reserved and reported: the owner draws the picture), `group()`, static `sourceName (i)`.
`OscillatorFullScreen` members: `close`, `power`, `basic`, `route`, `bar` (`TypeBar`), `tune` (`TuneRow`, vertical), `panel` (`KnobPanel`), `groups[kMaxGroups = 8]`. API: `setInstance (inst, kind)` (swaps only the type bar, tuning row, bottom band and groups; the active group goes back to Basic), `instance()`, `kind()`, `displayRect()`, `barBlock()`, `tuneBlock()`, `numGroups()`, `groupName (i)`, `activeGroup()`, `turnQ (knobIndex, delta)`, `rebuildGroups()`, `leaves (out, max)`, `kBottomH` 220.

## Size
`OscillatorCompact::minSize()` 420 x 700 (about 420 x 620 at 1280 x 800). `OscillatorFullScreen::minSize()` 1280 x 800.

## Touch and knob behaviour
Changes of the children come out with the ids the owner gave: `instances` `Index` (or `Request` for the name: open the list of `OscillatorCompact::sourceName (i)`), `full` `Request` (full screen wanted), `route` `Request`, `close` `Request`, details buttons `Request`, knobs `ModsRequest` (long press). A touch that goes down on a control of a Q group makes that group active (the frame moves).

## Q-Link
Groups of the full screen: Basic, Type (the type bar), Tune, then the groups of the panel (wavetable and multisample: Unison, Warp; granular: Grain, Playback, Random). Compact: one group, Basic. `turnQ (knobIndex, delta)` routes to the active group.

## Options and variants
Type: `OscKind { Wavetable, Multisample, Granular, Sample, Spectral }`. Sub: waveform pictograms, CRS only; Noise: no type bar, `kNoiseGenFull` panel.

## Minimal example
```cpp
OscillatorFullScreen osc;
osc.place ({ 0, 0, 1280, 800 });
osc.setInstance (OscInstance::A, OscKind::Wavetable);
osc.basic.assignIds (200);                    // ids of the Basic group
osc.close.setId (100);
const Rect picture = osc.displayRect();       // draw display::draw here
// every picture: osc.draw (sb);
Change c = osc.touchDown (x, y, nowMs);
c = osc.touchUp (x, y);
if (c.kind == ChangeKind::Request && c.id == 100) stack.back();
c = osc.turnQ (2, +0.01);                     // hardware knob 3 of the active group
```

## Used on
`shell-mpc/overlay/osc_ui.h` (`OscUi`, ids and `T::` table), `play_view.h`, `osc_page.h`. Pictures: `docs/user-guide/img/osc-a.png`, `play.png`, `xl-osc-a.png`, `osc-sub.png`, `osc-noise.png`.

## Limits and pitfalls
- The display picture is not part of the component: the owner draws it into `displayRect()`.
- After `setInstance` the ids and states of the swapped parts are the owner's again.
- The wavetable bar preset list in docs/MPC.md is older than `barpresets::kWavetableItems`.
