# OscillatorCompact and OscillatorFullScreen

## What it is
The two views of one oscillator instance (Sub, A, B, C, Noise), built only from library components and layout presets, drawn for 1280 x 800 (other sizes scale in proportion).
- `OscillatorCompact`: one third of the width on the Play page (about 420 x 620): header row (the source selector), the display area with its header band (power button at the left, the route button and the full-screen button at the right end; `playrows::bandRect`) and the plot area under the band (`plotRect()`: the wave never runs under a button; `labelRect()` is the room of the "OFF" tag and of the "content missing" line), and the Basic group.
- `OscillatorFullScreen`: the whole tab, the default view of the bold cut (2026-11-21): the top band (close or the title, POWER, `OscTopRow` = the first Q group, `OscMixIndicator` = the read-only destination / level / pan), the display over the whole width, the bottom band (`KnobPanel` blocks = the other Q groups) and the ONE `more` button. A control that does not apply is hidden, not dimmed, and leaves its Q group.
Use them as the reference for a screen that is a layout preset over a set of components.

## Header and types
`shell-mpc/ui/screen.h`: `OscillatorCompact`, `OscillatorFullScreen`, `OscTopRow`, `OscMixIndicator` (all `Composite` or `Component`). Uses `OscInstance`, `OscKind`, `Carousel`, `BasicControls` (the compact view), `KnobPanel`, `playrows`.

## State
`OscillatorCompact` members: `instances` (`Carousel`: Sub, Osc A, Osc B, Osc C, Noise; `listable`, wraps), `power` (`ToggleIcon`, `Power`), `route` (`RouteButton`), `full` (`MenuButton`, `Expand`), `basic` (`BasicControls`, stacked). API: `setInstance (OscInstance, OscKind)`, `displayRect()` (the rectangle shared by the three thirds, reserved and reported: the owner draws the picture), `plotRect()` (the plot area under the header band: where the picture is drawn), `labelRect()` (the room of the text in the band between the power button and the route / full screen button), `group()`, static `sourceName (i)`.
`OscillatorFullScreen` members: `close`, `power`, `top` (`OscTopRow`: `selector`, `kind`, `sound`, `source`, `oct`, `pos`, `crs`, `phase`, `filter`, `stereo`, `group`; `configure (inst, kind, noiseSample)` shows the four members of the instance), `mix` (`OscMixIndicator`: `setDest`, `setInfo`, `state.segment`, no touch target), `more` (`DetailButton`), `panel` (`KnobPanel`), `groups[kMaxGroups = 8]`. API: `setInstance (inst, kind, noiseSample = false)` (swaps the top row, the bottom band and the groups; the active group goes back to Basic), `setWarpApplies (slot, applies)` (hide the amount of a warp that is Off), `setPartShown (slot, on)`, `refreshGroups()`, `instance()`, `kind()`, `displayRect()`, `numGroups()`, `groupName (i)`, `activeGroup()`, `turnQ (knobIndex, delta)`, `rebuildGroups()`, `leaves (out, max)`, `kBottomH` 220, `kMoreW` 120.

## Size
`OscillatorCompact::minSize()` 420 x 700 (about 420 x 620 at 1280 x 800). `OscillatorFullScreen::minSize()` 1280 x 800.

## Touch and knob behaviour
Changes of the children come out with the ids the owner gave: `instances` `Index` (or `Request` for the name: open the list of `OscillatorCompact::sourceName (i)`), `full` `Request` (full screen wanted), `route` `Request`, `close` `Request`, details buttons `Request`, knobs `ModsRequest` (long press). A touch that goes down on a control of a Q group makes that group active (the frame moves).

## Q-Link
Groups of the full screen: Basic (the top row), then the blocks of the panel in reading order (wavetable: Unison, Warp; multisample, sample, spectral: Unison, Warp with warp 1 only; granular: Grain, Playback; Sub: Behaviour; noise: Tune, Playback). A hidden member is no member (`QGroup` members are the live ones, `buildGroups`). Compact: one group, Basic [type, source, PAN, LEVEL]. `turnQ (knobIndex, delta)` routes to the active group.

## Options and variants
Type: `OscKind { Wavetable, Multisample, Granular, Sample, Spectral }`. Presets (`panel.h`): `kWavetableFull` (also used by the multisample and the sample type), `kSpectralFull`, `kGranularFull`, `kSubFull`, `kNoiseGenFull`, `kNoiseSampleFull`; the multisample, sample and spectral screens set the warp pair `setSingle` (warp 1 only). The one-row blocks (Sub, Noise) are at the maximum row height (`KnobPanel::kOneRowH`; the bottom band is `kBlockTitleH + kOneRowH + kBlockPad` = 172 px) and the room that frees goes to the display. `KnobPanel` has `PartKind::Number` (FIN, the noise's PITCH and FINE) and `setShown (slot, on)` / `shown (slot)`; `WarpSlot::setApplies`, `WarpPair::setSingle`.

## Minimal example
```cpp
OscillatorFullScreen osc;
osc.place ({ 0, 0, 1280, 800 });
osc.setInstance (OscInstance::A, OscKind::Wavetable);
osc.top.assignIds (200);                      // ids of the top row (the owner overrides the ones that differ)
osc.close.setId (100);
const Rect picture = osc.displayRect();       // draw display::draw here
// every picture: osc.draw (sb);
Change c = osc.touchDown (x, y, nowMs);
c = osc.touchUp (x, y);
if (c.kind == ChangeKind::Request && c.id == 100) stack.back();
c = osc.turnQ (2, +0.01);                     // hardware knob 3 of the active group
```

## Used on
`shell-mpc/overlay/osc_ui.h` (`OscUi`, ids and `T::` table), `play_view.h`, `osc_page.h`. Pictures: `docs/screens/osc_a.jpg`, `play.png`, `xl-osc-a.png`, `osc-sub.png`, `osc-noise.png`.

## Limits and pitfalls
- The display picture is not part of the component: the owner draws it into `displayRect()`.
- After `setInstance` the ids and states of the swapped parts are the owner's again.
- The table selector of a wavetable oscillator is `OscUi::T::Table` (a `Stepper` with `listable` and `contentName`; its dropdown is enabled on the full screen and on the compact view): the name opens the wavetable browser (`docs/MPC.md` "The wavetable browser"). With a table FILE the stepper has three places (first, middle, last file of the category: the arrows dim at the ends) or one (a category of one file); the owner writes nothing for it but the request of the neighbouring file. No new component: the browser is the `ListPopup` with a header.

The native OSC header puts PAN, LEVEL and ROUTE above Basic. Touching one of the three selects the Mix Q-Link group for the current source. Its slots control pan, level and destination in that order. Switching the oscillator off closes all three host apertures.
