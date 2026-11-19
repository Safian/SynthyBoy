# HBar, SliderBar, Fader

## What it is
Three linear value controls.
- `HBar`: a horizontal bar for a value that runs both ways from a zero in the middle (a modulation depth), with the value as text at the right end.
- `SliderBar`: a horizontal slider in a 56 px cell, name at the left and value at the right on the top line, track with a thumb under them (strip pan and sends).
- `Fader`: a vertical level fader with a scale and an integrated peak meter (the mixer level).
Use them where a knob does not fit: a row of values (HBar), a compact row cell (SliderBar), a level with a meter (Fader). For a free continuous value on a page with room use a [Knob](knob.md).

## Header and types
`shell-mpc/ui/base.h`: `HBar`, `HBar::State`. `shell-mpc/ui/mixer_strip.h`: `SliderBar`, `SliderBar::State`, `Fader`, `Fader::State`.

## State fields
`HBar::State`: `value` 0.5, `modValue` 0.5 (live marker), `defaultValue` 0.5 (all 0 to 1; 0.5 = zero), `bipolar` true, `text` (`char[16]`, empty = the percent of the bar). `setValueText (s)`.

`SliderBar::State`: `label` (`char[8]`), `valueText` (`char[16]`, the owner formats it), `value` 0, `defaultValue` 0, `bipolar` false. Constructor `SliderBar (label, bipolar = false)`.

`Fader::State`: `label` (`char[16]`), `valueText` (`char[16]`, dB text), `value` / `modValue` / `defaultValue` 0.75, `peak` and `peakHold` (0 to 1), `hasMeter` true, `isFine` false (8 scale lines instead of 5), `hasPeakHold` false, scale: `numTicks` (max `kMaxTicks` 10), `tickPos[]` (0 to 1 along the track), `tickText[][6]`. Constructor `Fader (label)`.

## Size
`HBar` 200 x 56 (the text takes 96 px at the right). `SliderBar` 96 x 56 (track 14 px high under the texts). `Fader` 56 x 120; the track gets the rest. Larger: the track grows.

## Touch and knob behaviour
| Component | Gesture | Change |
|---|---|---|
| `HBar` | horizontal drag (whole track = whole range, relative to the value at touch down); double tap = default; `turn` adds delta | `Value`, 0 to 1 |
| `SliderBar` | horizontal drag, double tap = default, `turn` adds delta | `Value`, 0 to 1 |
| `Fader` | vertical drag (up = more, 200 px), double tap = default, `turn` adds delta | `Value`, 0 to 1 |

The mouse wheel is one notch = 1/128 for all three.

## Q-Link
All three can be Q members, one slot each. A turn adds `delta` to the normalised value (continuous). The depth of `HBar` in the modulation pages is `value = (depth + 1) / 2`: the page does not convert; the owner does.

## Options and variants
`HBar::bipolar` false puts the zero at the left. `SliderBar::bipolar` draws from the middle (pan). `Fader` scale ticks are set by the owner from the parameter range and unit; `hasMeter` false hides the meter.

## Minimal example
```cpp
Fader level ("LEVEL");
level.place ({ 40, 20, 96, 300 });
level.setId (60);
level.state.value = level.state.modValue = norm;        // every update
std::snprintf (level.state.valueText, sizeof level.state.valueText, "%+.1f dB", db);
level.state.peak = meterPeak;                           // from the engine, every picture

Change c = level.touchDown (88, 200, nowMs);
c = level.touchMove (88, 150);
if (c.kind == ChangeKind::Value && c.id == 60) norm = c.value;
```

## Used on
`MixerStrip::fader`, `pan`, `send1`, `send2` (the ten strips on `docs/user-guide/img/mixer.png`); `HBar` in `ModRow::amountBar` (`mod_matrix.h`), `ModulationsPage::Row::depth` (`panel.h`), `ModTargetsPage::Row::depth` (`mod_targets.h`).

## Limits and pitfalls
- `Fader::state.peak` is a live value: update it every picture, or it stops moving (see `MixerUi::update` in `shell-mpc/overlay/mixer_ui.h`).
- `HBar` needs 200 px: in a narrower cell use a `SliderBar`.
