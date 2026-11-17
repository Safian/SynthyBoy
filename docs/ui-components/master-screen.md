# MasterScreen

## What it is
The Master page: two sides switched by MAIN / TUNING & MPE buttons. MAIN: OUTPUT (volume, peak meters, spread, stereo knob, voice override list), PITCH (tune, transpose, bend up and down, link), VOICES (polyphony, legato switches, same-note and steal lists, velocity tracking, voice meter, pad pressure), GLIDE (time, slope, always, scaled, the curve display). TUNING: SCALE (type, key, mode, mini keyboard), TUNING (A4, scale, MTS options), MPE (on, bend range, macro, Y options).

## Header and types
`shell-mpc/ui/voice_master.h`: `MasterScreen : Composite`, `MasterScreen::Side { SideMain = 0, SideTuning = 1 }`; displays in [voice-displays.md](voice-displays.md).

## State
Members: `sideMain`, `sideTuning` (`MenuButton`); OUTPUT: `volume`, `spread`, `stereoRouting` (`Knob`), `meters`, `voiceOverride` (`LedList`); PITCH: `tune` (`Knob`), `transpose`, `bendUp`, `bendDown` (`NumberField`), `bendLink`; VOICES: `polyphony`, `velocityTrack`, `padAmount` (`NumberField`), `legato`, `polyLegato`, `phaseLegato`, `padPressure` (`LedToggle`), `sameNote`, `stealPriority` (`LedList`), `voiceMeter`; GLIDE: `portaTime`, `portaSlope` (`Knob`), `portaAlways`, `portaScaled`, `portaCurve`; SCALE: `scaleType`, `scaleKey` (`Stepper`: 20 scales, 12 keys), `scaleMode` (`LedList`), `keyboard`; TUNING: `tuningA4` (`NumberField`), `tuningScale` (`LedList`), `mtsEsp`, `mtsNoteOn`; MPE: `mpeOn`, `mpeYModwheel`, `mpeBendToX`, `mpeYBipolar`, `mpeBend`, `mpeMacro`; `groups[kMaxGroups = 10]`.
API: `side()`, `setSide (s)`, static `sideOfGroup (g)`, `numGroups()`, `activeGroup()`, `setActiveGroup (g)`, static `groupName (g)` (OUTPUT, PITCH, LINK, VOICES, VOICE OPTS, GLIDE, SCALE, TUNING, MPE, MPE Y), static `blockName (b)`, `turnQ`. Constants `kBlocks` 7, `kHeaderH` 64, `kTopBlockH` 228, `kTopLeftW` 640, `kBottomLeftW` 780.

## Size
`minSize()` 1280 x 628.

## Touch and knob behaviour
Children keep their changes: knobs `Value`, number fields `Index`, lists and toggles `Index`, steppers `Index` or `Request` (a list), the side buttons `Request`, the keyboard `Index` (the key). A touch on a control makes its group active.

## Q-Link
Main side: OUTPUT, PITCH, LINK, VOICES, VOICE OPTS, GLIDE; tuning side: SCALE, TUNING, MPE, MPE Y. The side that is up publishes its groups in reading order.

## Options and variants
`setSide`. STEREO is a knob (it is a percentage); the long lists SCALE and KEY stay steppers with a popup.

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
c = master.touchUp (x, y);
if (c.kind == ChangeKind::Request && c.id == kSideTuningId) master.setSide (MasterScreen::SideTuning);
```
(`kSideTuningId` is the id the owner gave `sideTuning`.)

## Used on
`shell-mpc/overlay/master_ui.h` (`MasterUi`, `kElementPage` 9; `T::Knob, Number, Toggle, Stepper, List, Inert`). Pictures: `docs/user-guide/img/master-main.png`, `master-tuning.png`.

## Limits and pitfalls
- The pad pressure layer (`padPressure`, `padAmount`) is a plugin-wide setting of the shell, not an engine parameter: it is no Q member.
- Do not add a second control for a value: one control per value.
- The meters need a call to `updateHold` once per picture.
