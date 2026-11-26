# FxDisplay

> Bound to SynthyBoy's effect types: `fx_display.h` includes `synthyboy/fx_types.h` (`kFxTypeEq`, `kFxTypeCompressor`, `kFxTypeDelay`, `kFxTypeFilter`, `kFxTypeDistortion` ...). To reuse it, replace `kindOfType()` (the map from your effect type to a `Kind`) and the types the models know; the five models themselves (`bandDb`, `compOut`, `shape` ...) are plain functions.

## What it is
The picture of what an effect does, from the parameters of the selected slot, drawn with thin smooth lines: the EQ frequency response, the compressor transfer curves, the echoes of a delay, the response of an FX filter, the waveshaper curve of a distortion (thick curve, dashed faint identity diagonal, and the area between them with a faint fill, so a curve at 0 dB drive still shows what it does). Effects without a display (and the splitters) get none: the editor gives their knobs the room.

Every model is the engine's (FX screen review 2026-11-26), and `ui_fx_editor_host` (`testDisplayModels`) holds them to it:
- EQ: `bandDb (band, EqBand, f, law, fs)` is the response of the engine's equalizer (`core/engine-own/fx/fx_eq.h`) in both laws: STATE VARIABLE (the trapezoidal section, exactly the analog section at s = j tan (pi f / fs) / tan (pi fc / fs), Q = 0.5 + 15.5 r^3, the shelf / bell weights from A = 10^(gain / 20)) and COOKBOOK (Q = 0.4 + 1.7417 r^3 for a shelf, 0.1 + 18.9053 r^4 for the others). Measured against the engine's impulse response: within 0.05 dB for every mode. A band's dot stands at its gain in a gain mode (shelf, peak) and on the curve otherwise.
- Compressor: `compOut (band, x, mode, mix)`: the upward stage under the lower threshold (lowerRatio of the distance), the downward stage above the upper one (upperRatio of it; both at once where the thresholds cross), Limit: the ceiling at the upper threshold, then the make-up gain and the linear dry / wet mix; thresholds clamped to -120 .. +12 dB like the engine. The axis is the PEAK of a steady tone: the limiter holds the peak, and the envelope of the Compress mode (a mean square with a faster attack than release) holds a 1 kHz sine 2.4 dB above its RMS at the default times (measured; an RMS axis was 0.7 to 1.5 dB off). Measured: within 0.36 dB (Compress and Limit, one band, -60 to -3 dB).
- Delay: `delayEchoes (state, span, out)`: the echoes of a click on both inputs through the engine's network of the style (Mono and Stereo: each line into itself; Ping pong: the right input and line into the left line, the left input and the fed back left line into the right one; Mid ping pong: the mono sum into the right line, the right line into the left one, whose time is the sum of both; Tap -> delay: the tap of the mono sum feeds a delay that repeats itself, both on both outputs), the times scaled by OFFSET L / R. Measured: every echo of the five styles in time, side and strength (the loop band wide open), and no other.

## Header and types
`shell-mpc/ui/fx_display.h`: `FxDisplay`, `FxDisplay::Kind { None, Eq, Compressor, Delay, Filter, Distortion }`, `EqBand`, `EqState`, `CompBand`, `CompState`, `DelayState`, `FilterState`, `DistState`.

## State fields
Public members of `FxDisplay`:
| Member | Content (defaults) |
|---|---|
| `kind` | `Kind::None` |
| `eq` | `EqState { low, mid, mid2, high, law 0, sampleRate 48000 }`, each `EqBand { used true, mode 0, freq 1000, gainDb 0, resonance 30 }` |
| `comp` | `CompState { band[3], bands 0 (multiband), activeBand 1, mode 0 (Compress / Limit / Peak), mix 1 }`, `CompBand { lowerThr -40, upperThr -20, lowerRatio 0, upperRatio 0.5, gainDb 0 }` |
| `delay` | `DelayState { timeMs1 300, timeMs2 300, feedback 0.4, style 1, offsetL 1, offsetR 1 }` (0 mono, 1 stereo, 2 ping pong, 3 mid ping pong, 4 tap to delay) |
| `filter` | `FilterState { db (const float*), n, cutoffHz 1000 }` (dB at 20 Hz to 20 kHz, log axis) |
| `dist` | `DistState { type, driveDb, mix 1, inLevel -1 }` (`inLevel` < 0 = not known) |
| `title` | `char[24]` |
Set the kind with `setKind (kind, name)` or `setKindFromType (fxType)` (uses `kindOfType`). Static helpers: `bandDb (band, EqBand, f, law, fs)`, `compOut (CompBand, x, mode, mix)`, `delayLines (DelayState, left, right)`, `delayEchoes (DelayState, spanMs, Echo*)`, `shape (type, driveDb, x)`; `kPoints` 192, `kMaxEchoes` 48.

## Size
`minSize()` 200 x 120; in the stacked layout the editor gives it `fxl::Layout::displayH` (160, 176 or 220 px; `FxSlotEditor::kDisplayH` = 176) under the header. Larger: more plot room. Every effect with a display has `fxl::Layout::side`: the display stands at the LEFT over the whole page height (628 x 412 px on the full page, 628 x 348 under the panel tabs) and the controls at the right; the Mixer peek and a panel whose rows do not fit keep the stacked layout (see `fx-editor.md`). The compressor draws one plot per band on a wide strip and one square plot with the bands' numbers at its left on the side page.

## Touch and knob behaviour
None (a state in, a picture out). The knobs around it edit the values.

## Q-Link
Not a Q member.

## Options and variants
Kinds as above; the compressor draws three bands in three colours in the multiband case. Draw Distortion input-level marks with `drawLive(sb)` after the page’s cached cells. The transfer curve stays in `draw(sb)`.

## Minimal example
```cpp
FxDisplay d;
d.place ({ 20, 200, 1240, 176 });
d.setKind (FxDisplay::Kind::Delay, "DELAY");
d.delay.timeMs1 = leftMs;  d.delay.timeMs2 = rightMs;   // tempo sync already turned into ms
d.delay.feedback = fb;     d.delay.style = style;
d.draw (sb);
```

## Used on
`FxSlotEditor::display` (`shell-mpc/ui/fx_slot_editor.h`), filled in `shell-mpc/overlay/fx_ui.h`. Pictures: `docs/screens/fx.jpg`, `fx-compressor.png`.

## Limits and pitfalls
- The filter response comes from the engine: the owner computes it (`synthyboy::FilterResponse`) and passes `db`; it must stay valid during `draw`.
- A tempo-synced delay time must be turned into milliseconds by the owner.
- The EQ, compressor and delay models are this engine's (see above); with another engine, replace them, or the picture lies.
- The delay display leaves out the loop band pass and the soft limit of the feedback path: with a narrow loop band a bright sound decays faster than the dots show (the timing and the first echo stay exact).
- The compressor curve is static: the attack and release, and the crossovers of the multiband mode, are not in it.
