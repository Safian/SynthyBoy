# Master and Voice displays (voice_master.h)

## What it is
Four small displays of the Master page: `PortamentoCurveDisplay` (the glide slope curve), `MiniKeyboardDisplay` (an octave of keys with the in-scale notes lit; a tap picks the scale key), `PeakMeterDisplay` (two output peak meters with a dB scale and hold lines), `VoiceMeterDisplay` (the number of sounding voices as a bar). Use them as read-outs beside the controls of a master page.

## Header and types
`shell-mpc/ui/voice_master.h`: `PortamentoCurveDisplay`, `MiniKeyboardDisplay`, `PeakMeterDisplay`, `VoiceMeterDisplay` (all `Component`s with public fields instead of a `State` struct).

## State fields
| Class | Fields (default) |
|---|---|
| `PortamentoCurveDisplay` | `slope` (0.0; convex, linear or concave for the parameter -8 to +8) |
| `MiniKeyboardDisplay` | `scaleType` (0, Chromatic to 19), `scaleKey` (0 = C to 11 = B), `scaleMode` (0 Conform, 1 Mark only); static `isScaleNote (type, key, note)`; `hitTestNote (x, y)`, `computeRects()` |
| `PeakMeterDisplay` | `peakL`, `peakR` (0 = -60 dB to 1 = full scale), `holdL`, `holdR`; `updateHold (dtSeconds)`; `scaleLabels (box, shown)`; `kNumMarks` 5, `kLabelH` 14 |
| `VoiceMeterDisplay` | `activeVoices` (0), `polyphony` (16) |

## Size
`minSize()`: `PortamentoCurveDisplay` 100 x 60; `MiniKeyboardDisplay` 240 x 90; `PeakMeterDisplay` 76 x 120; `VoiceMeterDisplay` 120 x 24. The scale numbers of a short meter that would run into each other are not all written.

## Touch and knob behaviour
Only `MiniKeyboardDisplay` reacts: a touch on a note gives `Index`, `index` = `value` = the note 0 to 11, on touch DOWN, again when a drag enters another note and on a tap up. The other three take no touch.

## Q-Link
None of them is a Q member.

## Options and variants
`scaleMode` colours the in-scale notes for Conform or Mark only.

## Minimal example
```cpp
PeakMeterDisplay meters;
meters.place ({ 400, 120, 76, 200 });
// every picture, from the engine:
meters.peakL = meters.peakR = outputPeak;     // 0 to 1
meters.updateHold (dtSeconds);                // the hold lines fall 0.5 per second
meters.draw (sb);

MiniKeyboardDisplay kb;
kb.place ({ 20, 400, 240, 90 });
kb.setId (61);
kb.scaleType = type;  kb.scaleKey = key;
Change c = kb.touchDown (x, y, nowMs);
if (c.kind == ChangeKind::Index && c.id == 61) writeKey (c.index);
```

## Used on
`MasterScreen::portaCurve`, `keyboard`, `meters`, `voiceMeter` (`shell-mpc/ui/voice_master.h`), wired in `shell-mpc/overlay/master_ui.h`. Pictures: `docs/user-guide/img/master-main.png`, `master-tuning.png`.

## Limits and pitfalls
- `MiniKeyboardDisplay` writes `scaleKey` itself on a touch and acts on touch down; set `scaleKey` from the parameter every update so a rejected write does not stay.
- Meters are live: draw them every picture, not from a cache. Call `updateHold` once per picture.
- The engine measures the larger of the two channels: both bars may show the same value.
