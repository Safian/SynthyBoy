# FilterDisplay

## What it is
The frequency response of a filter on a log axis (20 Hz to 20 kHz, plot -48 to +30 dB) with a handle you drag for cutoff (x) and resonance (y); with two filters it draws both curves and the result; in formant mode it is an XY pad with vowel markers. Use it as the display of a filter page.

## Header and types
`shell-mpc/ui/filter_display.h`: `FilterDisplay`, `FilterDisplay::State`. Constants `kMaxCurve` 256, `kFMin` 20.0, `kFMax` 20000.0, `kDbTop` 30.0, `kDbBottom` -48.0.

## State fields (FilterDisplay::State)
| Field | Meaning | Default |
|---|---|---|
| `dual` | two filters: both curves and the result | false |
| `activeFilter` | 1 or 2 (the handle the touch moves in dual mode) | 1 |
| `isFormant` | the XY pad instead of the response | false |
| `curve1`, `numPoints1`, `curve2`, `numPoints2`, `curveResult`, `numPointsResult` | dB values (the owner computes them with the engine's filter) | null, 0 |
| `cutoffHz`, `resonancePct` | cutoff and resonance of the active filter | 12000.0, 20.0 |
| `cutoffHz1`, `resonancePct1`, `cutoffHz2`, `resonancePct2` | both filters in dual mode | 12000.0, 20.0 |
| `filter1On`, `filter2On` | a switched-off filter is drawn dim | true |
| `liveCutoffHz`, `liveResoPct` | modulated live values (< 0 = none) | -1.0 |
| `numVoices`, `voiceCutoffs[8]` | live voice markers | 0 |
| `formantX`, `formantY`, `liveFormantX`, `liveFormantY` | 0 to 100 percent | 50.0, 50.0, -1.0, -1.0 |

`assignIds (idCutoff, idReso, idFormantX, idFormantY)` sets the ids.

## Size
`minSize()` = 240 x 140. The plot has a margin at the left for the dB scale and at the bottom for the frequencies (the formant pad uses the whole inside).

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| touch down or drag (response mode) | `Value`, `id` = `idCutoff`, `value` = the x position 0 to 1 on the log axis |
| touch down or drag (formant mode) | `Value`, `id` = `idFormantX`, `value` = x 0 to 1 |
| in dual mode a touch picks the nearer handle | sets `state.activeFilter` |

The y position (resonance or formant Y) does NOT come out in the `Change`: read it from the component after each change: `cutoffHzFor (f)`, `resonanceNorm()`, `formantXNorm()`, `formantYNorm()`, `cutoffNorm()`. `idReso` and `idFormantY` are stored but never emitted. `dragging()` tells a finger is down.

## Q-Link
Not a Q member (the knobs next to it are).

## Options and variants
Single, dual (`F1+F2`) and formant modes.

## Minimal example
```cpp
FilterDisplay d;
d.assignIds (150, 151, 152, 153);
d.place ({ 20, 80, 600, 320 });
d.state.curve1 = dbCurve;  d.state.numPoints1 = 256;
d.state.cutoffHz = cutoff;  d.state.resonancePct = reso;
Change c = d.touchDown (x, y, nowMs);
if (c.kind == ChangeKind::Value && c.id == 150)
{
    writeCutoff (d.cutoffHzFor (1));            // x comes in the change
    writeReso   (d.resonanceNorm());            // y only through the getter
}
```

## Used on
`FilterFullScreen::display` (`shell-mpc/ui/filter_display.h`), wired in `shell-mpc/overlay/filter_ui.h` (`kDisplayCutoff`, `kDisplayFormant`; the curves from `synthyboy::FilterResponse`). Picture: `docs/user-guide/img/filter.png`.

## Limits and pitfalls
- The component writes the touched cutoff and resonance into its own `state` while dragging (see the report: this departs from "the component never stores the truth"); the next `update` must overwrite them from the parameters.
- The curve arrays are the owner's: valid during `draw`.
- The curve is cut at the plot's bottom edge (-48 dB) and never runs along it.
