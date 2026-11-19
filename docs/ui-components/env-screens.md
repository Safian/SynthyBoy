# EnvCompact, EnvFullScreen, EnvCarousel, EnvSelector

## What it is
The two views of the envelope element ENV 1 to 6.
- `EnvCompact`: one third (400 x 620) in the middle of the Play page: the instance stepper, the [EnvGraph](env-graph.md) with a header band at its top (the full-screen button at its right end; the curve lies in the plot area below the band, `playrows::plotRect`), and the ADSR Q group in one frame.
- `EnvFullScreen`: top bar (close or selector, targets button, target tags), the big graph, and the bottom band of three Q groups: ADSR, SHAPE, TIME.
`EnvCarousel`: a [Carousel](carousel.md) of the six envelopes with a dot for the ones that have targets. `EnvSelector`: the row of six cells in the top bar of `EnvFullScreen` (a `PictogramSelector`).

## Header and types
`shell-mpc/ui/env_screen.h`: `EnvCarousel : Carousel`, `EnvSelector : PictogramSelector`, `EnvCompact : Composite`, `EnvFullScreen : Composite`, `kNumEnvs`.

## State
`EnvCarousel`: `setTargetMask (mask)` (bit i = envelope i has targets), `hasTargets (i)`, static `colours()` (the six colours for list dots); `count` 6, wraps, `listable`.
`EnvCompact`: `instances` (`EnvCarousel`), `graph` (`EnvGraph`), `full` (`MenuButton`, `Expand`), `attack`, `decay`, `sustain`, `release` (`Knob`), `group` (`QGroup`); `graphRect()`, `drawLive (sb)`; constants `kKnobH` 84, `kSelectorH` 56, `kMargin` 6.
`EnvFullScreen`: `close`, `instances` (`EnvSelector`), `targets` (`MenuButton`), `graph`, `attack`, `decay`, `sustain`, `release`, `aCurve`, `dCurve`, `rCurve`, `delay`, `hold` (`Knob`), `beats`, `legato` (`LedToggle`), `groups[3]`, `targetLines[3]` (`TargetLine { text[40] }`), `numTargetLines`, `numTargets`; `setNumTargets (n)`, `tagsRect()`, `setPageMode (bool)` (the tab is the page of its own: the selector takes the place of CLOSE), `block (i)`, `activeGroup()`, `setActiveGroup (i)`, `turnQ`, `drawLive (sb)`; `groupName (i)`: ADSR, SHAPE, TIME; `kMaxGroups` 3, `kBottomH` 176, `kTopH` 72.

## Size
`EnvCompact` about 400 x 620 (the shared Play rows, [helpers.md](helpers.md)); `EnvFullScreen` the whole tab (1280 x 628). The graph switches to its compact chrome below 520 x 260.

## Touch and knob behaviour
Children keep their own changes (see [knob.md](knob.md), [env-graph.md](env-graph.md)): knobs `Value`, graph `Value` and `edit()`, `instances` `Index` or `Request` (list), `full` / `close` / `targets` `Request`, toggles `Index`.

## Q-Link
Compact: ADSR [ATTACK, DECAY, SUSTAIN, RELEASE]. Full: ADSR, SHAPE [A CURVE, D CURVE, R CURVE], TIME [DELAY, HOLD, BEATS, LEGATO].

## Options and variants
`setPageMode`, the target tags (the first that fit are drawn, the others as "+n").

## Minimal example
```cpp
EnvFullScreen env;
env.place ({ 0, 110, 1280, 628 });
env.setPageMode (true);
env.attack.setId (310);  env.graph.setId (300);
env.attack.state.value = env.attack.state.modValue = attackNorm;
env.graph.state.shape.t[1] = attackNorm;
env.setNumTargets (2);
std::snprintf (env.targetLines[0].text, sizeof env.targetLines[0].text, "CUTOFF  +35%%");
env.numTargetLines = 1;
// draw: env.draw (sb); env.drawLive (sb);
```

## Used on
`shell-mpc/overlay/env_ui.h` (`EnvUi`, `kElementPage` = `kElementEnv`; `T::Knob, Field, Toggle, Select, Pick, Graph, Inert`), `play_view.h`. Pictures: `docs/user-guide/img/env.png`, `play-env.png`, `env-targets.png`.

## Limits and pitfalls
- `EnvFullScreen::instances` is an `EnvSelector`; the compact view uses `EnvCarousel` (`Carousel` is the selector of all three Play thirds).
- Call `drawLive` after `draw` or the running dots do not move.
- Target tags are strings the owner builds, at most 3 lines.
