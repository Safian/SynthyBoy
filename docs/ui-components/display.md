# Oscillator display and LFO display (display.h, lfo_display.h)

## What it is
The pictures of an oscillator (a wave, a wavetable stack, unison voices, a sample band with loop range and grain window, noise) and of an LFO (one cycle, the lead-in of rise and delay, the trace of a random LFO). They are FUNCTIONS over a plain data struct, not `Component` classes: no touch, no state of their own, nothing that knows an engine. Use them for the display rectangle of a page. Also `display::drawModule` for the small envelope and LFO panels of the compact view.

## Header and types
`shell-mpc/ui/display.h`, namespace `synthyboy_mpc::ui::display`: `Data`, `Module`, `draw`, `drawLive`, `drawModule`, `drawModuleLive`, helpers `stopsOf`, `cycleCrop`, `smoothCycle`, `framesDiffer`, `noiseShape`.
`shell-mpc/ui/lfo_display.h`, namespace `synthyboy_mpc::ui::lfodisplay`: `Data`, `draw`, `drawLive`, `plotRect`, `stripRect`, `leadWidth`, `hasLeadStrip`, `rowOf`, `colOf`, `restPath`.

## State fields: display::Data
| Field | Meaning | Default |
|---|---|---|
| `kind` | `None, Wave, MinMax, Spectrum, Message, Noise` | `None` |
| `cycle`, `cycleMax`, `n` | wave: `n` samples of one period (-1 to 1); MinMax: lower and upper row; Spectrum: dB values | null, 0 |
| `stack[kMaxStack = 9]`, `numStack`, `pos` | wavetable frames at i / (numStack - 1) and the current position 0 to 1 | none |
| `voices`, `detune`, `blend` | unison fan | 1, 0, 0 |
| `loopOn`, `loopStart`, `loopEnd`, `grain`, `grainPos`, `grainSize`, `playhead` | sample and granular marks (`playhead` < 0 = unknown) | false, 0, 1, false, 0, 0, -1 |
| `heads[kMaxHeads = 8]`, `numGrainsLive`, `liveGrainPos[32]`, `liveGrainLife[32]`, `phases[8]`, `noiseHead` | live read-outs of the engine (all optional, < 0 = none) | none |
| `spectrum`, `numSpectrum` | the faint spectrum under a wave (magnitudes 0 to 1) | null |
| `colour` | the instance colour `0xRRGGBB` | `0x2ee6f0` |
| `message` | text for `Kind::Message` | "" |
| `noiseColour`, `dim` | noise picture 0 (white) to 8; `dim` = source switched off (a veil and an OFF tag) | 0, false |
| `area` | the plot area the picture is drawn in, edge to edge (the compact view of the Play page: `playrows::plotRect (display)`, below the header band that holds the power, route and full screen buttons); empty = the display rectangle with its 8 px margin (the full screens and pages) | empty |

`display::Module`: `rect`, `plot`, `env` (true = envelope), `highlighted`, `title`, `v` (n values 0 to 1), `n`, `dot` (< 0 = none).
`lfodisplay::Data`: `kind { None, Shape, Random }`, `y`, `n` (one cycle, -1 to 1), `delayShare`, `riseShare`, `gridX`, `gridY`, `loopX`, `phase`, `envelope`, `numCursors`, `cx[]`, `cy[]`, `trace`, `traceN`, `style` (0 to 4), `label`, `dim`, `editing`. A display narrower than `lfodisplay::kCompactMaxW` (520 px, `isCompact`: the Play page's third) is the compact view: `plotRect` is `playrows::plotRect (display)` (the lead-in strip is the top of it), `stripRect` lies in it and the label is written in the header band (`playrows::labelRect`); the full page and the shape editor (`editing`) keep their rectangle.

## Size
The Play page's displays have a header band at the top (`playrows::bandRect`, 72 px) with the buttons in it; the picture is drawn below it (`display::Data::area`, `lfodisplay::plotRect` of the compact view, `EnvGraph::plotArea`), so no curve runs under a button. The functions take the rectangle: `display::draw (sb, rect, data, cached = true)`. No minimum is enforced; the pages give them the display rectangle (`playrows::displayRect (third)`, 292 px high on the Play page).

## Touch and knob behaviour
None. Interaction comes from the controls around it.

## Q-Link
Not applicable.

## Options and variants
- `cached` true: the data is a cached cell (drawn again only when the data changes); false: drawn with the picture each time (a display whose data changes every picture, a knob dragged along a wavetable position).
- `drawLive (sb, rect, data)`: the live read-outs, always drawn over `draw` and never cached.
- Level of detail (`theme::D()`): "reduced" has 3 stack frames, 2 unison hairlines, no spectrum, no glow.
- A new display kind: add a `Kind` and a case in `draw`.

## Minimal example
```cpp
display::Data d;
d.kind = display::Data::Kind::Wave;
d.cycle = cycleSamples;  d.n = 256;          // owner's array, one period
d.colour = theme::instanceColour (1);
display::draw (sb, displayRect, d);          // the block and the curve
display::drawLive (sb, displayRect, d);      // read heads and grains, if any

lfodisplay::Data l;
l.kind = lfodisplay::Data::Kind::Shape;
l.y = shapeSamples;  l.n = 128;
lfodisplay::draw (sb, lfoRect, l);
```

## Used on
`shell-mpc/overlay/osc_display.h` (builds `display::Data` from the engine's cycle), `osc_ui.h`, `play_view.h` and `lfo_ui.h` (`lfodisplay::Data`). The picture sits in `OscillatorCompact::displayRect()`, `OscillatorFullScreen::displayRect()`, `LfoCompact::displayRect()`, `LfoFullScreen::displayRect()`. Pictures: `docs/user-guide/img/osc-a.png`, `osc-type-sample.png`, `lfo.png`.

## Limits and pitfalls
- The pointers in `Data` are the owner's: they must stay valid during `draw`.
- A display draws its own block (gradient, keyline, grid) as a cell of its own.
- What the display does not show because the data is not there: the real playing position of a sample, the real positions and times of grains.
- No glow on lines, values or text; the only light is a faint back glow behind a display (`Look::backGlow`).
