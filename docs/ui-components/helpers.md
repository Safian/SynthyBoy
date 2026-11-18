# Helpers: choice_names.h, play_rows.h, time_ruler.h, lfo_model.h, layoutRow, ui.h

## What it is
Small header-only helpers that are not components: shown names for engine choices, the shared rows of the Play thirds, round time marks, the editable LFO shape model, the row layout function and the umbrella include.

## Header and types
| Header | Content |
|---|---|
| `shell-mpc/ui/ui.h` | includes `display.h`, `filter_display.h`, `led_list.h`, `led_toggle.h`, `note_mask.h`, `list.h`, `panel.h`, `screen.h`, `text_entry.h`, `mixer_screen.h`, `fx_screen.h`, `voice_grid.h`, `voice_master.h`, `voice_screen.h` (so `core.h`, `base.h`, `composite.h`, `qgroup.h` and the rest come with them). It pulls in `fx_screen.h`, hence `synthyboy/fx_types.h` and `synthyboy/params.h`. To reuse the library without the engine, include the single headers instead. |
| `choice_names.h` | `choicenames::lawOf (key)`, `shownChoice (key, index, engineName)`, `shownLabel (key)`, `filterStyleNames (model)`, `shownFilterStyleNames (model, engineNames)` |
| `play_rows.h` | namespace `playrows`: `kMargin` 6, `kSelectorH` 56, `kSelectorGap` 8, `kControlsH` 232, `kDisplayGap` (= `kQLane` + 4), `displayRect (third)`, `controlsLine (third)` |
| `time_ruler.h` | `RulerTick`, `TimeRuler` (`kMaxTicks` 32), `rulerNextStep`, `rulerStepMs`, `formatRulerTime`, `makeTimeRuler (totalMs, x0, widthPx, minSpacingPx)` |
| `lfo_model.h` | namespace `lfo`: `Point`, `ShapeModel`, `Undo`, `SyncMode { Hz, Bpm, Key }`, `syncModeOf`, `syncDot`, `syncTrip`, `syncOf`, `builtinAt`, `powerScale`, `powerForMid`, `snapTo`, `cellOf`, `inferGrid` |
| `composite.h` | free function `layoutRow (rect, fixed, weight, n, gap, out)` and `constexpr int kGap = 4` |

## choice_names.h
The engine's choice lists carry working names of other products' models. The screen never shows a name of another product: every page that shows such a parameter asks `shownChoice (key, index, engineName)` and `shownLabel (key)` (and `shownFilterStyleNames (model, engineNames)` for filter models 0..7). Today: `eq.law`, `convolve.law`, `oscN.unison.law`, `reverb.type`, `compressor.limit`, `oscN.sample.loop_mode`, `oscN.sample.slice_play`. It is bound to SynthyBoy parameter keys (replace the table when you reuse it). No includes except `<cstring>`.

## play_rows.h
One table so the three thirds of the Play page (oscillator, envelope, LFO) have the same selector row (56 px), the same display top and height, and controls that start on one line: `displayRect (third)` and `controlsLine (third)`. Pure functions of a `Rect`.

## time_ruler.h
Round time marks (1-2-5 series, ms or s) under a graph whose x axis is time: `makeTimeRuler (totalMs, x0, widthPx, minSpacingPx)` fills a `TimeRuler` with at most 32 ticks (`x`, `ms`, `label`). Used by `EnvGraph`. Pure.

## lfo_model.h
The drawn shape of a custom LFO: up to `kMaxPoints` = 128 points `Point { x, y, power, mx, my, mc }` (`mx`, `my`, `mc` are `PointMod { source, amount }`: the modulation of the time, the level and the curve of the point by a modulation source id, as the content state's `modX / modY / modCurve`) plus `loop` (-1 = none; the index of the point the loop marker sits on) and `smooth`. Editing operations (every one keeps the invariants of the engine: 2 to 128 points, x ascending, the first x 0, the last x 1, y 0 to 1, |power| at most 20): `eval (phase)`, `insert`, `move`, `remove`, `setPower`, `segmentAt`, `replaceRange` (a flat piece or ramp over an interval), `fromBuiltin (shape)`, `initLinear()`; the selection: `PointSet` (a set of point indices that follows `onInsert` / `onRemove`), `spanOf`, `removeSet` (the loop point follows), `moveSet` (a group moves as a whole, clamped so that the order and the form stay), `scaleSetY` (the height of a group around its centre), `falloffWeights` / `moveWeighted` (the soft drag); the whole shape: `flipH` (mirror in time: the curves change sign, the loop point is the mirrored point), `flipV`, `shift (d)` (turns the shape in time, the cut segment is split exactly), `resetCurves`, `bendable`, `meanCurve`, `bendAll (delta)`, `nearestByX`, `setMod`; `operator==` compares everything. `Undo` keeps `kDepth` = 32 states (`push`, `pop`, `depth`, `clear`). The shape editor ([shape-editor.md](shape-editor.md)) calls these; the owner converts to and from its own storage (`LfoUi::modelOf` / `contentOf`, and the saved shapes in `shell-mpc/overlay/lfo_shape_files.h`: `LfoShapeLibrary`, the `.lfoshape.json` files of `SynthyBoy/LFO Shapes/`). No dependencies, no allocation.

## layoutRow
`layoutRow (r, fixed, weight, n, gap, out)` splits `r` into a row of n rectangles: an entry with `fixed[i] > 0` has exactly that width, the others share the rest in proportion to `weight[i]`, `gap` pixels apart.

## Minimal example
```cpp
Rect cells[3];
const int fixed[3]  = { 56, 0, 0 };        // first cell 56 px, the others share the rest
const int weight[3] = { 0, 1, 2 };
layoutRow (area, fixed, weight, 3, kGap, cells);
power.place (cells[0]);  name.place (cells[1]);  value.place (cells[2]);

const Rect display = playrows::displayRect (third);
const char* shown = shownChoice ("eq.law", 0, "engine name");   // "State variable"
```

## Used on
`play_rows.h`: `OscillatorCompact`, `EnvCompact`, `LfoCompact`. `choice_names.h`: `shell-mpc/overlay/osc_ui.h`, `filter_ui.h`, `fx_ui.h`, `ui/fx_layout.h`. `time_ruler.h`: `ui/env_graph.h`. `lfo_model.h`: `ui/shape_editor.h`, `ui/lfo_parts.h`, `overlay/lfo_ui.h`.

## Limits and pitfalls
- `ui.h` is not free of the engine: it includes `fx_screen.h`. Include single headers for reuse.
- `rulerNextStep` has a comment that describes the 1-2-5 series; read the code before relying on its rounding.
- No third-party product names in shown text: add the name to `choice_names.h`, not to a page.
