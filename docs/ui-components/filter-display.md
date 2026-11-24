# FilterDisplay

## What it is
The frequency response of a filter on a log axis (20 Hz to 20 kHz, plot -48 to +30 dB) with a handle you drag for cutoff (x) and resonance (y); with two filters it draws both curves and the result; in formant mode it is an XY pad with vowel markers. Use it as the display of a filter page.

## Header and types
`shell-mpc/ui/filter_display.h`: `FilterDisplay`, `FilterDisplay::State`. Constants `kMaxCurve` 256, `kFMin` 20.0, `kFMax` 20000.0, `kDbTop` 30.0, `kDbBottom` -48.0.

## State fields (FilterDisplay::State)
| Field | Meaning | Default |
|---|---|---|
| `dual` | two filters: both curves and the result | false |
| `activeFilter` | 1 or 2 (the filter being edited: its handle and its text are the active ones; the owner sets it, from the `index` of a change in the dual mode) | 1 |
| `isFormant` | the XY pad instead of the response | false |
| `curve1`, `numPoints1`, `curve2`, `numPoints2`, `curveResult`, `numPointsResult` | dB values (the owner computes them with the engine's filter) | null, 0 |
| `cutoffHz`, `resonancePct` | cutoff and resonance of the active filter | 12000.0, 20.0 |
| `cutoffHz1`, `resonancePct1`, `cutoffHz2`, `resonancePct2` | both filters in dual mode | 12000.0, 20.0 |
| `filter1On`, `filter2On` | a switched-off filter is drawn dim | true |
| `liveCutoffHz`, `liveResoPct` | modulated live values (< 0 = none) | -1.0 |
| `numVoices`, `voiceCutoffs[8]` | live voice markers | 0 |
| `formantX`, `formantY`, `liveFormantX`, `liveFormantY` | 0 to 100 percent | 50.0, 50.0, -1.0, -1.0 |

`assignIds (idCutoff, idReso, idFormantX, idFormantY)` sets the ids of the four kinds of change (use four different ids that no component of the page has). The display never writes `state`: the owner sets every field on every update, also while a finger is down.

## Size
`minSize()` = 240 x 140. The plot has a margin at the left for the dB scale and at the bottom for the frequencies (the formant pad uses the whole inside).

## Touch and knob behaviour
A touch reports the finger as TWO changes: the first is returned by `touchDown` / `touchMove`, the second is taken with `takePending (Change&)` right after (false: there is none).
| Gesture | Change |
|---|---|
| touch down (response mode) | `Value` `id` = `idCutoff`, `value` = the x position 0 to 1 on the log axis (`FilterDisplay::hzOfNorm (value)` is the frequency), then (pending) `Value` `id` = `idReso`, `value` = y 0 to 1 (resonance / 100) |
| touch down (formant mode) | `Value` `id` = `idFormantX`, `value` = x 0 to 1, then (pending) `Value` `id` = `idFormantY`, `value` = y 0 to 1 |
| drag | the same, but only the axis that moved since the last report (a move that changed nothing reports nothing) |
| `index` of every change | dual mode: the filter whose handle the finger holds (1 or 2: the nearer handle at touch down, kept for the whole gesture); else 0 (formant) or `state.activeFilter` |

The component stores nothing: `state` and the picture are unchanged by every gesture (while a finger is down the values of `state` are drawn next to the point, as the owner set them). The owner writes the parameters from the two changes (`hzOfNorm` to the cutoff parameter, the resonance 0 to 1 as it is), switches the filter being edited to `index` and sets `state` on the next update: the point follows the parameter the finger wrote. Private and transient: the handle held, the position of the last report, the pending change. `cutoffHzFor (f)`, `resonanceNorm()`, `formantXNorm()`, `formantYNorm()`, `cutoffNorm()` read `state`. `dragging()` tells a finger is down.

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
    writeCutoff (FilterDisplay::hzOfNorm (c.value));   // x: the first change
Change y;
while (d.takePending (y))
    writeReso (y.value);                               // y: the second change (id 151, 0 to 1)
// the next update: d.state.cutoffHz / resonancePct from the parameters
```

## Used on
`FilterFullScreen::display` (`shell-mpc/ui/filter_display.h`), wired in `shell-mpc/overlay/filter_ui.h` (`kDisplayCutoff`, `kDisplayFormant`, `kDisplayReso`, `kDisplayFormantY`; the curves from `synthyboy::FilterResponse`). Picture: `docs/screens/filter.jpg`.

## Limits and pitfalls
- Forget `takePending` and the resonance (or the formant Y) is never written. Do not skip the update of `state` while a finger is down: the point follows the parameter, nothing else moves it.
- The curve arrays are the owner's: valid during `draw`.
- The curve is cut at the plot's bottom edge (-48 dB) and never runs along it.
