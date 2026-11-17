# Instructions for an AI agent that builds a plugin UI from this template

Terse. Human documentation: `README.md` (this folder), `docs/ui-components/TEMPLATE.md`. Which component to use for which control: **`shell-mpc/ui/AGENTS.md`** (the decision table) and `docs/ui-components/README.md`; do not invent components, do not restyle them (every colour, size and radius is in `shell-mpc/ui/theme.h`).

## Steps

1. Read `src/demo_params.h`, `src/pages/main_page.h` (the how-to), then the page that matches what you need (`shape_page.h`: a display component; `list_page.h`: stepper / fader / own pop-up).
2. Define the parameters first: `ParamId` + `kParams` in `src/demo_params.h`, the same order in `skin/params.json`. Keys are permanent.
3. For every page: copy the closest page file, rename the class, set `kElementPage` (unique), change members, `bind` (id below 700, parameter KEY), `group` (up to 4 per Q group), `layout`.
4. Register the page in four places: `src/demo_page_set.h`, `skin/overlay_layout.json` (new marker colours), `skin/pages.json` (host widgets; a parameter on one tab only), the page factory (after migration step 8; see `src/overlay_hook.h`). Keep `tests/pages_host.cpp` in step (a `commonChecks<MyPage>` line).
5. Do NOT build or run anything unless your task says so; when it does: `mpckit_demo_pages_host` first (native, seconds), the cross build and the package last, once.

## Rules

- A page file contains ONLY components from `shell-mpc/ui/`, `mpckit::PageBase` / `Binder`, and parameter keys. No drawing code, no parameter ranges, no engine calls, no locks, no allocation per frame in a page.
- Components never store the truth: the value comes in through `state`, a `Change` goes out. Write through `inst.writeParam (index, normalised)` (or the Binder), never to your engine directly from a page.
- Every member of a Q group needs 16 pixels free above it (the index chip); a group is 1 .. 4 components; stepped parameters are driven by the Q-Link in steps (the router does it).
- The tab is `{ 0, 110, 1280, 628 }` in the 1280 x 800 canvas. Touch targets are at least 56 x 56. Check `component.minSize()` against the rectangle you give it.
- Names the host shows: at most 31 bytes; page labels: at most 8 characters for a knob.
- Marker colours must differ from every other marker and instance colour by more than 4 in at least one channel (`mpckit::validatePages`); never reuse SynthyBoy's.
- Log through `MPCKIT_LOG`; never `printf`; the release build strips the log.
- Threads: pages run on the overlay thread (lock free, no allocation in `update` / `draw` / `touch`); the audio thread only reads the parameter mirror; the display thread talks to the host.
- Licence: keep the `AGPL-3.0-or-later` header in every new file.

## When something is missing

If the kit lacks what you need (a component, an interface method), stop on that part and say what is missing. Do not copy runtime code from `shell-mpc/overlay/` into the plugin: the migration plan (`docs/ui-components/TEMPLATE.md`) moves it into the kit.
