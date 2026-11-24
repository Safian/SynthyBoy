# Carousel and EnvCarousel

## What it is
`Carousel`: the selector at the top of the three thirds of the Play pages (the oscillator source, the envelope, the LFO). The current entry stands in the middle at full size and brightness, the previous entry at its left and the next entry at its right, smaller and dimmer and faded towards the edges, as in a carousel. A tap on a neighbour steps to it (the neighbours are the arrows; there is no room for chevrons), a tap on the name asks the owner to open the list of all entries ([ListPopup](list-popup.md)), a horizontal drag across it steps once per 60 px. It replaces the `Stepper` in those three places and is a drop-in for it: the same state fields, the same changes. Use it for a short list of named entries that the player steps through often (up to a few dozen); the plain [Stepper](stepper.md) stays the control of a parameter's choice (a model, a shape, a sync mode).
`EnvCarousel`: the six envelopes with the colour dot of the ones that have modulation targets.

## Header and types
On the Play page the Env and LFO carousels step over the cells of the tabs' `UsedSelector` (used + selected + one `+`, `uibind::SelectorCells`), not over all entries; the oscillator's carousel keeps the five sources. `shell-mpc/ui/carousel.h`: `Carousel`, `Carousel::State`. `shell-mpc/ui/env_screen.h`: `EnvCarousel : Carousel`.

## State fields
`Carousel::State`:
| Field | Type | Meaning | Default |
|---|---|---|---|
| `count` | `int` | number of entries | 0 |
| `index` | `int` | the current entry | 0 |
| `text` | `char[48]` | the name of the current entry (used when `names` is null) | empty |
| `names` | `const char* const*` | the owner's array of the names of all `count` entries (not copied): the neighbours are named from it | null |
| `wrap` | `bool` | the list goes round: the neighbour of the last entry is the first. Without it the missing neighbour is empty and the end does not step | false |
| `dim` | `bool` | the name drawn dim | false |
| `dotMask` | `unsigned` | bit i: a dot beside the name of entry i (a smaller one beside a neighbour's) | 0 |
| `dotColours` | `const uint32_t*` | the colour (`0xRRGGBB`) of the dot of entry i; null = the accent colour | null |

Members: `bool listable = true` (a tap on the name is a `Request`; false = it does nothing), `setItem (text)`, `neighbour (step)` (the entry `step` places away, -1 = none), `nameOf (i)`, `zone (0 .. 2)` (left, middle, right rectangle). Constants `kDragStepPx` 60, `kMinW` 240. `EnvCarousel`: `setTargetMask (mask)`, `hasTargets (i)`, `targetMask()`, static `colours()` (the six envelope colours, also for the list's dots); six entries "ENV 1" .. "ENV 6", `wrap` true.

## Size
`minSize()` = { 240, 56 }. In the thirds of the Play pages 408 x 56 (the top row of a third). The neighbour zones are a quarter of the width each, at least 56 px; the name has the half in the middle. Names are fitted into their zone (`fValue` in the middle, `fLabel + 2` at the sides, never below `fMin`); the fixed entry names (Sub, Osc A .. Noise, ENV 1 .. 6, LFO 1 .. 10, RND 1 .. 4) fit without an ellipsis at 12 px or more.

## Touch and knob behaviour
The change is made on the touch up (a tap) or while the finger moves (a drag), like the Stepper's.
| Gesture | Change |
|---|---|
| tap the left neighbour (the left quarter) | `Index`: `index` = `value` = previous (wrapped with `wrap`, nothing at the first entry without it) |
| tap the right neighbour | `Index` next |
| tap the name (the middle half, `listable`) | `Request` with the carousel's `id` |
| drag across | `Index` once per 60 px, counted from where the finger went down; to the right = the previous entries (the row follows the finger); a drag is no tap. The labels snap (no animation: the picture changes only when the index does, so tests and the cached cells stay deterministic and nothing is redrawn between touches) |
| `turn (delta)` | `Index`, `delta` steps (rounded) |

Nothing when `count <= 1`. `touchTargets` lists the three zones (each at least 56 px).

## Q-Link
Can be a Q member (one slot); a turn steps like the Stepper's. On the Play pages the selectors are not Q members. The mouse wheel is one step per notch.

## Options and variants
`wrap` (the three selectors of the thirds wrap, as the Steppers did), `listable`, `dim`, `names`, `dotMask` / `dotColours`. The inactive look (`setInactive`, [theme.md](theme.md)) greys it like any component. `EnvCarousel` adds the target dots.

## Minimal example
```cpp
static const char* const names[5] = { "Sub", "Osc A", "Osc B", "Osc C", "Noise" };
Carousel source;
source.place ({ 14, 120, 408, 56 });
source.setId (7);
source.state.count = 5;
source.state.names = names;                 // the neighbours are named from this array
source.state.wrap = true;
source.state.index = current;
source.setItem (names[current]);

Change c = source.touchDown (380, 140, nowMs);
c = source.touchUp (380, 140);              // the right neighbour: next
if (c.kind == ChangeKind::Index && c.id == 7)  current = c.index;
else if (c.kind == ChangeKind::Request)         { /* open a ListPopup of the names */ }
```

## Used on
`OscillatorCompact::instances` (Sub, Osc A, Osc B, Osc C, Noise), `EnvCompact::instances` (`EnvCarousel`), `LfoCompact::instance` (LFO 1 .. 10, RND 1 .. 4): the top row of the three thirds of the Play pages (`play_rows.h`). Test: `shell-mpc/ui/tests/ui_carousel_host.cpp`; pages: `ui_screen_host.cpp`, `ui_env_host.cpp`, `env_ui_host.cpp`, `osc_ui_host.cpp`, `lfo_ui_host.cpp`.

## Limits and pitfalls
- The carousel does not know the list: the owner sets `index`, `text` and `names` on every update, and opens the `ListPopup` on the `Request` (the popup's `Index` change then sets the value).
- Without `wrap` the first and last entries have an empty neighbour; the owner decides (the Play selectors wrap, as before).
- The fade at the outer edges is three strips of the control's own colour over the outer ends of the neighbours' zones; it does not hide a name that fits, but a long neighbour name is cut by its zone like any text: keep names short.
