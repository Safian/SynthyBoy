# Theme (theme.h)

## What it is
ONE table (`struct Look`) with every colour, gradient, line width, type size, radius and spacing the components draw with. Components take their numbers from nowhere else. Three looks are three tables, not branches in the code. Next to it the level of detail (`struct Detail`): how much of a display is drawn.

## Header and types
`shell-mpc/ui/theme.h`, namespace `synthyboy_mpc::ui::theme`. Includes only `<cstdint>`, `<cstring>`: free of SynthyBoy code. Types: `Look`, `Detail`. Tables: `kLook1` (`"neon"`, the reference), `kLook2` (`"aurora"`, calmer), `kLook3` (`"plasma"`, boldest), `kDetailFull`, `kDetailReduced`.

## State fields (struct Look)
| Group | Fields |
|---|---|
| surfaces | `bg0`, `bg1` (ground top, bottom), `panel0`, `panel1` (block fill), `keyline`, `keylineHi`, `track`, `grid` |
| ink | `text`, `textDim`, `textFaint`, `onBright`, `q` (the Q-Link group colour, yellow `0xffc23d` in all looks) |
| gradients and colours | `flow[4]` (the main signal gradient), `inst[5]` (Sub, A, B, C, Noise), `src[kNumSources = 8]` (env, lfo, velocity, key, wheel, pressure, macro, random), `env[2]`, `lfo[2]`, `filter[2]`, `negative` |
| strokes | `curve`, `curveHi`, `curveBack`, `hair`, `arcTrack`, `arcValue`, `arcMod` (fractions of the dial diameter), `frame` |
| light | `backGlow`, `afterglow` |
| type (pixels per em) | `fTitle` 28, `fValue` 24, `fLabel` 15, `fItem` 22, `fSmall` 12, `fMin` 9 |
| shape | `rBlock`, `rControl`, `rChip`, `gap`, `pad` |
Colours are `0xRRGGBB`; alpha is the drawing's business (`theme::rgba (rgb, a)` makes `0xRRGGBBAA`).

`Detail`: `name`, `stackFrames` (wavetable frames behind the current one), `unisonHairs`, `spectrum`, `glow`, `afterglow`. "full" = 7, 6, true, true, true; "reduced" = 3, 2, false, false, false.

## Functions
| Function | Meaning |
|---|---|
| `L()` | the active `Look` (read on every draw) |
| `setLook (n)` | 1 to 3, anything else = 1; one pointer assignment |
| `D()`, `detailLevel()`, `setDetail (n)` | the active detail; 1 = full, 2 = reduced |
| `sourceColour (name)` | colour of a modulation source by its short name (ENV, LFO, VEL, KEY, MOD, PRES, MAC, RND; unknown names get a stable colour of the table) |
| `instanceColour (i)` | 0 Sub, 1 A, 2 B, 3 C, 4 Noise |
| `mix (a, b, t)` | mixes two `0xRRGGBB` colours |

**The inactive look.** A component of a switched-off thing (an oscillator that is off) is drawn grey: `Component::setInactive (true)` (a composite hands it to its parts, `QGroup::setInactive` does the frame and the chips). The pen then turns every colour into a grey of its own brightness at `Pen::kInactivePercent` (70) percent (`Pen::inactiveGrey (rgba)`); alpha stays. The component stays operable (it is not `setEnabled (false)`, which dims by alpha). One rule for every component; displays that draw their own colours (the oscillator picture) call `Pen::inactiveGrey` on their instance colour. The power button of an oscillator is the exception: `ToggleIcon::State::negativeOff` draws its icon in `Look::negative` (red) while it is off.

Components read the look through `colour::` (core.h) or `theme::L()`; `Pen::look()` returns it.

## Size
Not applicable.

## Touch and knob behaviour
Not applicable.

## Q-Link
`Look::q` is the only yellow. Use it for Q frames and chips only (rule).

## Options and variants
Add a look: add a `Look` table (all fields, in order) and one case in `setLook`. A new type size is a new field, not a literal in a component.

## Minimal example
```cpp
theme::setLook (2);                               // calmer look, takes effect on the next draw
const theme::Look& T = theme::L();
pen.text (x, y, T.fValue, "VALUE", colour::rgba (T.text));
```
The overlay reads the look from the file `/tmp/synthyboy-overlay-look` once a second and the detail from `/tmp/synthyboy-overlay-detail` (see docs/MPC.md "Overlay look and drawing").

## Used on
Every component. `shell-mpc/overlay/page_view.h` uses `ui::theme::L()` for the ground of a page.

## Limits and pitfalls
- `g_look` and `g_detail` are plain global pointers, not atomics: set them from the thread that draws, or before drawing starts.
- Never write a colour literal in a component; add it to the table (and to all three looks).
- docs/MPC.md "UI components" still names an older palette (1e1d1e, ff2e63); `theme.h` is the truth (a neon dark blue palette).
