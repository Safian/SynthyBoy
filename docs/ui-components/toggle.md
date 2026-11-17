# LedToggle and ToggleIcon

## What it is
`LedToggle`: a two-state control as one frameless row of 56 px: a small flat dot (lit = accent colour, unlit = thin dim ring) and the name. Use it for every on / off setting. This is the default for toggles; do not draw boxed toggle buttons.
`ToggleIcon`: the older boxed toggle with an icon (power, pitch tracking, generic). Use it only where a glyph says more than a name (the power glyph of a strip or a header).

## Header and types
`shell-mpc/ui/led_toggle.h`: `LedToggle` (derives from `ToggleIcon`), namespace `led` (`kRowH` 56, `kMinToggleW` 96, `dot`, `widthFor`, `nameSize`). `shell-mpc/ui/base.h`: `ToggleIcon`, `ToggleIcon::State`, `ToggleIcon::Icon { Power, PitchTrack, Generic }`. Other users: `StripPower` (`mixer_strip.h`), `FilterToggle` (`filter_display.h`).

## State fields
`ToggleIcon::State` (shared by `LedToggle`):
| Field | Type | Meaning | Default |
|---|---|---|---|
| `on` | `bool` | the state | false |
| `icon` | `Icon` | `Power`, `PitchTrack`, `Generic` (LedToggle sets `Generic`, no icon drawn) | `Generic` |
| `label` | `char[24]` | name (23 characters) | empty |
| `showState` | `bool` | ToggleIcon only: " ON" / " OFF" after the label | false |
| `knobLike` | `bool` | ToggleIcon only: no box, in a row of knobs | false |

`LedToggle::centre` (float, default 0.5): the height of dot and name as a fraction of the rectangle.

## Size
`LedToggle::minSize()` = { `LedToggle::widthFor (label)`, 56 }, width at least 96. `static constexpr kBandW` = 100 for a slot in a band of knobs. `ToggleIcon::minSize()` 56 x 56, 72 x 56 with a label. A larger rectangle is a bigger invisible touch area; the dot stays at the left, the name follows.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap (on touch up) | `Index`: `index` 0 / 1, `value` the same, the opposite of `on` |
| `turn (delta)` (both classes) | delta > 0 switches on, < 0 off, nothing when already so (the code; see Limits) |
| mouse wheel | one notch = `turn (+/-1)` |

## Q-Link
Can be a Q member, one slot. In the active Q group the lit name takes the Q colour and the unlit ring too; the chip comes from the group. The Q proxy still reports On / Off from the engine parameter.

## Options and variants
`LedToggle::widthFor (name)` for layouts; `centre` to align with knob dials next to it; `ToggleIcon` icons `Power` and `PitchTrack`; `showState` and `knobLike` for `ToggleIcon`.

## Minimal example
```cpp
LedToggle clean;
clean.setLabel ("CLEAN MODE");
clean.setId (41);
clean.place ({ 20, 300, LedToggle::widthFor ("CLEAN MODE"), 56 });
clean.state.on = paramIsOn;                  // every update

Change c = clean.touchDown (30, 320, nowMs);
c = clean.touchUp (30, 320);
if (c.kind == ChangeKind::Index && c.id == 41)
    paramIsOn = c.index != 0;
```

## Used on
`VoiceScreen::randDetune10x`, `oscA` .. `oscNoise`; `MasterScreen::legato`, `bendLink`, `mpeOn`; `FilterFullScreen::clean`, `env1`; `EnvFullScreen::beats`, `legato`; `LfoFullScreen::monoQ`; `ModSlotDetail::auxInv`, `stereo`, `delaySync`; `FxSlotEditor::toggles`; `TuneRow::track`, `ToggleNumber::toggle`. `ToggleIcon` with `Power`: `OscillatorCompact::power`, `FilterFullScreen::power`, `MixerStrip::power` (`StripPower`).

## Limits and pitfalls
- The header comment of `led_toggle.h` says "a Q-Link step and a wheel notch FLIP the state (either direction)", but `LedToggle::onTurn` only switches in the direction of the delta (up = on, down = off). The code is the truth; see the defect list in the report.
- Name up to 23 characters; the width follows the name, so use `widthFor` in the layout.
- Do not box a LedToggle and do not draw an ON / OFF word.
- A disabled toggle (`setEnabled (false)`) is drawn dim; "off" is not "not available".
