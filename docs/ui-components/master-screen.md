# MasterScreen

## What it is
The Master page: two sides switched by MAIN / TUNING & MPE buttons. MAIN: OUTPUT (volume, peak meters, spread, stereo knob, voice override list), PITCH (tune, transpose, bend up and down, link), VOICES (polyphony, legato switches, same-note and steal lists, velocity tracking, voice meter, pad pressure), GLIDE (time, slope, always, scaled, the curve display). TUNING: SCALE (type, key, mode), TUNING (A4, scale, MTS options), MPE (on, bend range, macro, Y options).

## Header and types
`shell-mpc/ui/voice_master.h`: `MasterScreen : Composite`, `MasterScreen::Side { SideMain = 0, SideTuning = 1 }`; displays in [voice-displays.md](voice-displays.md).

## State
Members: `sideMain`, `sideTuning` (`MenuButton`); OUTPUT: `clip` (`ClipReadout`), `volume`, `spread`, `stereoRouting` (`Knob`), `meters`, `voiceOverride` (`LedList`), `outputLimiter`, `noSmoothing`, `oversample4x` (`LedToggle`); PITCH: `tune` (`Knob`), `transpose`, `bendUp`, `bendDown` (`NumberField`), `bendLink`; VOICES: `polyphony`, `velocityTrack`, `padAmount` (`NumberField`), `legato`, `polyLegato`, `phaseLegato`, `padPressure` (`LedToggle`), `sameNote`, `stealPriority` (`LedList`), `voiceMeter`; GLIDE: `portaTime`, `portaSlope` (`Knob`), `portaAlways`, `portaScaled`, `portaCurve`; SCALE: `scaleType`, `scaleKey` (`Stepper`: 20 scales, 12 keys), `scaleMode` (`LedList`), the KEY input activation button; TUNING: `tuningA4` (`NumberField`), `tuningScale` (`LedList`), `mtsEsp`, `mtsNoteOn`; MPE: `mpeOn`, `mpeYModwheel`, `mpeBendToX`, `mpeYBipolar`, `mpeBend`, `mpeMacro`; `groups[kMaxGroups = 9]`.
API: `side()`, `setSide (s)`, static `sideOfGroup (g)`, `numGroups()`, `activeGroup()`, `setActiveGroup (g)`, static `groupName (g)` (OUTPUT, PITCH, VOICES, VOICE OPTS, GLIDE, SCALE, TUNING, MPE, MPE Y), `setBendLinked (bool)` (LINK on: BEND DOWN hidden, BEND UP reads BEND), `setMpeOn (bool)` (MPE off: MPE BEND and BEND TO X hidden), constant `kFirstTuningGroup` 5, static `blockName (b)`, `turnQ`. Constants `kBlocks` 7, `kHeaderH` 64, `kTopBlockH` 228, `kTopLeftW` 640, `kBottomLeftW` 780.

## Size
`minSize()` 1280 x 628.

## Touch and knob behaviour
The MPC host owns the knobs, fields, lists, toggles, meters, CLIP and both side selectors. Their C++ objects retain geometry, formatting and Q/state data outside the overlay children. Native labels supply the page title and section names on both panels. The overlay draws the glide curve and hardware Q-Link group indicators. Native panel callbacks select MAIN or TUNING; parameter callbacks use their original engine indices. A touch on a native aperture can select its four-knob Q group without editing the value.

Parent enabled/inactive state explicitly propagates to the adapters. Disabled MPE and linked BEND parameters leave their Q groups; their stored values remain intact.

## Q-Link
Main side: OUTPUT, PITCH, VOICES, VOICE OPTS, GLIDE; tuning side: SCALE, TUNING, MPE, MPE Y. The side that is up publishes its groups in reading order. LINK is a touch-only toggle inside PITCH (bold cut, owner 2026-11-21), no Q member. A control that does not apply (BEND DOWN while LINK is on, MPE BEND and BEND TO X while MPE is off) is hidden, not dimmed; its row closes up from the left and so does its group.

## Options and variants
`setSide` updates the host-native panel state. There is no selectable GPU-control mode.
Subpage exclusivity: `covers(x, y)` grants touch passthrough strictly to native controls of the currently active side (`SideMain` vs `SideTuning`); inactive subpage controls and margins are absorbed by the overlay.
Touch target identity: exact component rectangles (no broad block catch-alls), respecting conditionality (`bendDown` disabled when `bendLinked_`, `mpeBend`/`mpeBendToX` disabled when `!mpeOn_`).
Caption separation: every native component has a distinct uppercase caption (`WithName[<CAPTION>]/Parameter <idx>`), and legacy fallback components are purged from `TUI.json` upon migration to eliminate ghost fallbacks. Block headers remain touchable in native mode to switch active Q-Link groups. STEREO is a knob (it is a percentage); the long lists SCALE and KEY stay steppers with a popup.

## Minimal example
```cpp
MasterScreen master;
master.place ({ 0, 110, 1280, 628 });
master.volume.setId (700);
master.volume.state.value = master.volume.state.modValue = volNorm;
master.meters.peakL = master.meters.peakR = peak;      // live
master.meters.updateHold (dt);
master.setSide (MasterScreen::SideMain);
Change c = master.touchDown (x, y, nowMs);
c = master.touchUp (x, y);                             // tapping sideTuning calls setSide (SideTuning)
if (c.kind == ChangeKind::Value && c.id == 700) volNorm = c.value;
```

## Used on
`shell-mpc/overlay/master_ui.h` (`MasterUi`, `kElementPage` 9; `T::Knob, Number, Toggle, Stepper, List, Inert`). [Current Master hardware capture](../screens/master.jpg).

## Limits and pitfalls
- The pad pressure layer (`padPressure`, `padAmount`), the output limiter (`outputLimiter`) and the quality switches (`noSmoothing`, `oversample4x`) are not Q members.
- Do not add a second control for a value: one control per value.
- The meters need a call to `updateHold` once per picture.

The full GPU-control mode, overlay choice list and Master numeric-entry keyboard have been removed. The host supplies numeric entry and menus for its native controls.
