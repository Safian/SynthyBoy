# MixerFullScreen, MixerStrip, MixerRoutingBar

## What it is
The mixer page: a routing bar on top and ten channel strips side by side (SUB, A, B, C, NOISE, F1, F2, BUS1, BUS2, MASTER). A strip has a header (power with the strip name, the destination dropdown) and a Q group column: PAN slider, two send sliders and the level fader with its live peak meter and per-channel peak hold line. `MixerRoutingBar` draws the signal flow from the sources to FLT 1, FLT 2, MAIN FX, BUS 1, BUS 2, DIRECT, MASTER with the selected strip highlighted.

## Header and types
`shell-mpc/ui/mixer_screen.h`: `MixerFullScreen : Composite`, `MixerRoutingBar : Component`. `shell-mpc/ui/mixer_strip.h`: `MixerStrip : Composite`, `StripPower`, `RouteDropdown`, `Fader`, `SliderBar` ([bars.md](bars.md)).

## State
`MixerFullScreen`: `routingBar`, `strips[kStrips = 10]`, `groups[kMaxGroups]` (three Q groups for the ten level faders); `assignIds (baseId)` (strip i gets `baseId + i * 10`), `setStripMembers (strip, level, pan, send1, send2)` (a member the engine has no parameter for, such as pan on a bus, is not drawn, takes no touch and is no group member), `setActiveGroup (g)`, `activeGroup()`, `numGroups()`, `groupName (g)`, `turnQ`.
`MixerStrip`: `title`, `power` (`StripPower`), `destButton` (`RouteDropdown`, "MAIN"), `pan`, `send1`, `send2` (`SliderBar`), `fader` (`Fader`); `setTitle (t)`, `setPresent (...)`, `setHasDestination (bool)` (the MASTER strip has none: the button is hidden, no rectangle, no touch), `hasDestination()`, `assignIds (baseId)` (power +0, dest +1, pan +2, send1 +3, send2 +4, fader +5), `kCellH` 56, `kMinH`.
`MixerRoutingBar`: `destinations[10]`, `selectedStrip`, `setDestination (strip, dest)`, `setSelectedStrip (s)`.

## Size
`MixerFullScreen::minSize()` 1280 x 628; `MixerStrip::minSize()` 96 x `kMinH` (header, then the Q column with the fader at 96 px and three 56 px slider cells, each under a 16 px lane for its chip); `MixerRoutingBar` 400 x 56.

## Touch and knob behaviour
Tap FLT 1, FLT 2, MAIN FX, BUS 1 or BUS 2 in the routing bar to open its PEEK panel over the Mixer. CLOSE returns to the same Mixer strip. DIRECT and MASTER are labels.

A touch on a strip makes its group active (the routing bar follows). Changes come out with the strip's ids: fader and sliders `Value`, power `Index`, destination `Request` (the owner opens the list of 3 to 5 destinations).

## Q-Link
The Mixer maps level faders in three groups: SUB/A/B/C, NOISE/F1/F2/MAIN, and BUS 1/BUS 2. On the XL those ten slots stay fixed. While a PEEK panel is open, the Q-Links drive its controls. CLOSE restores the Mixer mapping.

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
`shell-mpc/overlay/mixer_ui.h` (`MixerUi`, `kElementPage` 5). Picture: `docs/screens/mixer.jpg`.

## Real Native Widgets and Single Input Ownership
The dedicated skin extra `scripts/mpc-skin-extras/84_mixer.py` configures real native widgets on the Mixer tab for available strip bindings from the generated skin (`mix_routing`, `mix_bus`):
- `filter1.destination` -> `Mixer F1 Dest` (radio3, strip 5 dest, 645 134 115 56)
- `filter2.destination` -> `Mixer F2 Dest` (radio3, strip 6 dest, 772 134 115 56)
- `bus1.fx_bypass` -> `Mixer Bus 1 Bypass` (toggle, strip 7 power, 899 74 115 56)
- `bus1.destination` -> `Mixer Bus 1 Dest` (radio3, strip 7 dest, 899 134 115 56)
- `bus1.level` -> `Mixer Bus 1 Level` (faderBi, strip 7 fader, 899 210 115 414)
- `bus2.fx_bypass` -> `Mixer Bus 2 Bypass` (toggle, strip 8 power, 1026 74 115 56)
- `bus2.destination` -> `Mixer Bus 2 Dest` (radio3, strip 8 dest, 1026 134 115 56)
- `bus2.level` -> `Mixer Bus 2 Level` (faderBi, strip 8 fader, 1026 210 115 414)
- `main.fx_bypass` -> `Mixer Main Bypass` (toggle, strip 9 power, 1153 74 115 56)

### Single Input Ownership:
- When rendered by the host, the host owns the touch and gestures for these native widgets.
- `MixerUi::touch` identifies native-owned controls via `isNativeOwned(strip, controlType)` and yields touch input directly to the host (`return false`), while updating the active strip selection and published Q groups.
- Plugin-owned controls (SUB, OSC A/B/C, NOISE levels, power, pan, and sends, as well as Master volume) are owned and processed by the overlay.
- Modal PEEK panels (`kPagePeek`: FLT 1, FLT 2, MAIN FX, BUS 1, BUS 2) and destination popup lists (`kPageList`) take modal input ownership over the tab until closed.

### Residuals and Gaps Against Generated Skin:
- The generated skin's kick pages for the Mixer tab (`mix_routing` and `mix_bus`) provide native controls for the 9 parameters above, plus `osc1..3.filter_blend`, `noise.filter_blend`, and `direct.level`.
- Strips 0..4 (SUB, OSC A, OSC B, OSC C, NOISE) power, level, pan, and sends, as well as `master.volume`, are not present on `mix_routing`/`mix_bus`, and therefore remain plugin-owned overlay controls.
- Live peak meters and peak hold ballistics across all 10 channels remain drawn by the overlay plane.

## Limits and pitfalls
- The destination of a strip stays a dropdown (a list of 3 to 5 rows would take the height of the fader); the strip power stays a glyph with the name.
- `MixerRoutingBar` is declared but the strip destinations must be given by the owner with `setDestination`.
- `Fader` writes `state.value` inside `touchMove`, `touchUp` and `turn`: write the parameter value back to `state.value` on every update so it does not drift.
