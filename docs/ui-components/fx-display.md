# FxDisplay

> Bound to SynthyBoy's effect types: `fx_display.h` includes `synthyboy/fx_types.h` (`kFxTypeEq`, `kFxTypeCompressor`, `kFxTypeDelay`, `kFxTypeFilter`, `kFxTypeDistortion` ...). To reuse it, replace `kindOfType()` (the map from your effect type to a `Kind`) and the types the models know; the five models themselves (`bandDb`, `compOut`, `shape` ...) are plain functions.

## What it is
The picture of what an effect does, from the parameters of the selected slot, drawn with thin smooth lines: the EQ frequency response, the compressor transfer curves, the echoes of a delay, the response of an FX filter, the waveshaper curve of a distortion (thick curve, dashed faint identity diagonal, and the area between them with a faint fill, so a curve at 0 dB drive still shows what it does). Effects without a display (and the splitters) get none: the editor gives their knobs the room.

## Header and types
`shell-mpc/ui/fx_display.h`: `FxDisplay`, `FxDisplay::Kind { None, Eq, Compressor, Delay, Filter, Distortion }`, `EqBand`, `EqState`, `CompBand`, `CompState`, `DelayState`, `FilterState`, `DistState`.

## State fields
Public members of `FxDisplay`:
| Member | Content (defaults) |
|---|---|
| `kind` | `Kind::None` |
| `eq` | `EqState { low, mid, mid2, high }`, each `EqBand { used true, mode 0, freq 1000, gainDb 0, resonance 30 }` |
| `comp` | `CompState { band[3], bands 0 (multiband), activeBand 1 }`, `CompBand { lowerThr -40, upperThr -20, lowerRatio 0, upperRatio 0.5, gainDb 0 }` |
| `delay` | `DelayState { timeMs1 300, timeMs2 300, feedback 0.4, style 1 }` (0 mono, 1 stereo, 2 ping pong, 3 mid ping pong, 4 tap to delay) |
| `filter` | `FilterState { db (const float*), n, cutoffHz 1000 }` (dB at 20 Hz to 20 kHz, log axis) |
| `dist` | `DistState { type, driveDb, mix 1, inLevel -1 }` (`inLevel` < 0 = not known) |
| `title` | `char[24]` |
Set the kind with `setKind (kind, name)` or `setKindFromType (fxType)` (uses `kindOfType`). Static helpers: `bandDb (band, EqBand, f)`, `compOut (CompBand, x)`, `shape (type, driveDb, x)`; `kPoints` 192.

## Size
`minSize()` 200 x 120; the editor gives it `fxl::Layout::displayH` (160, 176 or 220 px; `FxSlotEditor::kDisplayH` = 176) under the header. Larger: more plot room. A table with `fxl::Layout::side` (now the Distortion only) puts the display at the LEFT over the whole page height (628 x 412 px on the full page) and the controls at the right; the Mixer peek keeps the stacked layout (see `fx-editor.md`).

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
- The EQ curve models are textbook biquads at 48 kHz; if your engine differs, the picture lies.
