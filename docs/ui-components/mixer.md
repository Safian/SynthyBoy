# MixerFullScreen, MixerStrip, MixerRoutingBar

## What it is
The mixer page: a routing bar on top and ten channel strips side by side (SUB, A, B, C, NOISE, F1, F2, BUS1, BUS2, MASTER). A strip has a header (power with the strip name, the destination dropdown) and a Q group column: PAN slider, two send sliders and the level fader with its peak meter. `MixerRoutingBar` draws the signal flow from the sources to FLT 1, FLT 2, MAIN FX, DIRECT, MASTER with the selected strip highlighted.

## Header and types
`shell-mpc/ui/mixer_screen.h`: `MixerFullScreen : Composite`, `MixerRoutingBar : Component`. `shell-mpc/ui/mixer_strip.h`: `MixerStrip : Composite`, `StripPower`, `RouteDropdown`, `Fader`, `SliderBar` ([bars.md](bars.md)).

## State
`MixerFullScreen`: `routingBar`, `strips[kStrips = 10]`, `groups[10]` (`QGroup` per strip: slot 0 fader LEVEL, 1 pan, 2 send 1, 3 send 2); `assignIds (baseId)` (strip i gets `baseId + i * 10`), `setStripMembers (strip, level, pan, send1, send2)` (a member the engine has no parameter for, such as pan on a bus, is not drawn, takes no touch and is no group member), `setActiveGroup (g)`, `activeGroup()`, `numGroups()`, `groupName (g)`, `turnQ`.
`MixerStrip`: `title`, `power` (`StripPower`), `destButton` (`RouteDropdown`, "MAIN"), `pan`, `send1`, `send2` (`SliderBar`), `fader` (`Fader`); `setTitle (t)`, `setPresent (...)`, `assignIds (baseId)` (power +0, dest +1, pan +2, send1 +3, send2 +4, fader +5), `kCellH` 56, `kMinH`.
`MixerRoutingBar`: `destinations[10]`, `selectedStrip`, `setDestination (strip, dest)`, `setSelectedStrip (s)`.

## Size
`MixerFullScreen::minSize()` 1280 x 628; `MixerStrip::minSize()` 96 x `kMinH` (header, then the Q column with the fader at 96 px and three 56 px slider cells, each under a 16 px lane for its chip); `MixerRoutingBar` 400 x 48.

## Touch and knob behaviour
A touch on a strip makes its group active (the routing bar follows). Changes come out with the strip's ids: fader and sliders `Value`, power `Index`, destination `Request` (the owner opens the list of 3 to 5 destinations).

## Q-Link
One group per strip, in strip order (the selected strip is the active one, the next three follow).

## Options and variants
`setStripMembers` for strips with fewer members. A `Fader` scale from the parameter range via `state.numTicks`.

## Minimal example
```cpp
MixerFullScreen mx;
mx.assignIds (1000);                          // strip i: 1000 + 10 i + 0..5
mx.setStripMembers (7, true, false, false, false);   // BUS1: a level only
mx.place ({ 0, 110, 1280, 628 });
MixerStrip& s = mx.strips[1];
s.fader.state.value = s.fader.state.modValue = levelNorm;
std::snprintf (s.fader.state.valueText, sizeof s.fader.state.valueText, "%+.1f dB", db);
s.fader.state.peak = peak;                    // live: every picture
Change c = mx.touchDown (x, y, nowMs);
c = mx.touchUp (x, y);
if (c.kind == ChangeKind::Value && c.id == 1015) writeLevel (1, c.value);   // strip 1 fader
```

## Used on
`shell-mpc/overlay/mixer_ui.h` (`MixerUi`, `kElementPage` 5). Picture: `docs/user-guide/img/mixer.png`.

## Limits and pitfalls
- The destination of a strip stays a dropdown (a list of 3 to 5 rows would take the height of the fader); the strip power stays a glyph with the name.
- `MixerRoutingBar` is declared but the strip destinations must be given by the owner with `setDestination`.
- The header comment of `mixer_strip.h` says "SendBar"; the class is `SliderBar`.
