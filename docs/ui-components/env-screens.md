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
`EnvFullScreen`: `close`, `selector` ([UsedSelector](used-selector.md): ENV 1 .. 6, "used + one free"; `syncSelectorLayout()`, `captionRect()`), `targets` (`MenuButton`), `graph`, `attack`, `decay`, `sustain`, `release`, `aCurve`, `dCurve`, `rCurve`, `delay`, `hold` (`Knob`), `beats`, `legato` (label RETRIG) (`LedToggle`), `groups[3]`, `targetLines[3]` (`TargetLine { text[40] }`), `numTargetLines`, `numTargets`; `setNumTargets (n)`, `tagsRect()`, `setPageMode (bool)` (the tab is the page of its own: the selector takes the place of CLOSE), `block (i)`, `activeGroup()`, `setActiveGroup (i)`, `turnQ`, `drawLive (sb)`; `groupName (i)`: ADSR, SHAPE, TIME; `kMaxGroups` 3, `kBottomH` 176, `kTopH` 72.

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
`shell-mpc/overlay/env_ui.h` (`EnvUi`, `kElementPage` = `kElementEnv`; `T::Knob, Field, Toggle, Select, Pick, Graph, Inert`), `play_view.h`. Current image: [ENV](../screens/env.jpg).

## Limits and pitfalls
- `EnvFullScreen::selector` is a `UsedSelector` (since 2026-11-21; `EnvSelector` is used by no page now); the compact view uses `EnvCarousel` (`Carousel` is the selector of all three Play thirds).
- Call `drawLive` after `draw` or the running dots do not move.
- Target tags are strings the owner builds, at most 3 lines.

## Production native migration (2026)

The standalone Env screen host controls have been migrated to use SynthyBoy's
approved native component factories:

- **Dials** (ADSR + Delay + Hold): `components.native.level.level.compact_dial_definition()`
  with `synthyboy_dial_data()` FilmStrip. Unipolar for ADSR/Delay/Hold, bipolar
  (distinct FilmStrip file) for Attack/Decay/Release curves.
- **BEATS / RETRIG switches**: `components.native.active_text.active_text` with
  shared gray (OFF) and cyan (ON) state images. Each has its own definition
  (`envNativeBeatsSwitch`, `envNativeRetrigSwitch`).
- **Selector strip**: `components.native.radio_button.radio_button.render_segmented_button_art()`
  for the six ENV instance cells with off/selected/used semantics.
- **TARGETS button**: `components.native.active_text.active_text` with
  "TARGETS" label.

### Implementation files
- the Env tabs of `skin/TUI.json` (formerly installed by a skin script, now edited directly) use
  public native component factories. No local ImageDraw drawing.
- the Play expanded editor of `skin/TUI.json` reuses ENV definitions for
  expanded Play via `full-controls/selector` extraction.

### Authentic offline preview and review

`scripts/render_ui_screen_env_preview.py` builds a small `env_render_fixture.cpp` against the existing core library, then composites the actual generated TUI with the current C++ `EnvUi` scene through `scene_cpu`. It never draws substitute knobs or crops an older graph. Canonical output is only `docs/screens/env.jpg`; the old reference was retired after integration. MS/BEATS and Gen1/Gen2 diagnostics require an explicit temporary directory outside the repository. These synthetic fixtures do not verify hardware input, drawers or performance.

Independent review of the accepted local MLX result corrected three production defects: switch children now bind the real Data proxy and have one click owner instead of duplicate parent toggles; TARGETS uses its existing EnvUi action proxy instead of a nonexistent skin popup; native dial captions are restored through the shared Level template. Bipolar strips use the public factory data rather than a copied knob schema. Compact A/D/R CURVE titles fit their cells. Parameter IDs, units and instance routing remain unchanged.

Targeted check after these fixes:20ENV own-component/hook tests passed. Existing19MLX acceptance passes were retained as prior evidence, without a full-suite or engine rerun. The new renderer compiled for both generations and produced matching MS/BEATS fixtures. Hardware verification remains open.
