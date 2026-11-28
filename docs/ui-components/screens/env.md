# Env screen (`envScreen`, `envScreenTop`)

**Tabs:** Env 1 .. Env 6 (`env1` .. `env6`; the view index is the envelope number). Each tab holds `Screen` (`envScreen`), `Screen Top` (`envScreenTop`), the markers, the instance picture, the title header instance `Env <n> Header` (Header component, [../header.md](../header.md): title and the TARGETS button), the captions `Native Caption env<n> 1..3` (the three group labels) and the sentinel. **Picture:** `../../screens/env.jpg`.

## What it shows

`envScreen`: the modulation editor and targets title (shared, `defs_common.json`) and `envNativeFullFields` (0 0 1280 628): the dials Attack, Decay, Sustain, Release, Delay, Hold, the three curve cells (attack, decay, release), the BEATS and RETRIG switches (the targets button is the Env header now). `envScreenTop`: the target tags (three tag dots and labels plus the "more" label, `Env Native Tag`) and the list pages (`oscNativeList`). The envelope graph with the live position is drawn by the plugin (`env-graph`, [../env-graph.md](../env-graph.md)).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kEnvView` (the shown envelope), `kEnvFields` (`shell-mpc/native_env_controls.h`: the value of every cell), `kModTargets` (the targets pages), `kNativeText`/`kDrawText` (tags and captions). The shown envelope is bound by the view index of the tab; the screen's handles are the same for all six tabs. The Play page keeps its own envelope selection (see [play.md](play.md)).

## Q groups

ADSR, SHAPE, TIME (three groups; Q 13 .. 16 stay empty): `EnvUi::publishQGroup`.

## C++ owners

`shell-mpc/overlay/env_ui.h` (`EnvUi`: Q banks, geometry, native bridge, detail pages; it also serves the compact envelope view of the Play page, see [play.md](play.md)), `shell-mpc/overlay/mod_rows_page.h` and `shell-mpc/overlay/native_mod_targets_ui.h` (targets), `shell-mpc/native_mod_targets.h` (the targets handles), `shell-mpc/native_env_controls.h` (the cell handles; the `Selection` slot is unused by the skin but kept so the later handle numbers do not shift: `env_ui.h` and `scripts/xl-ui-test` still use it as a boundary/offset).
