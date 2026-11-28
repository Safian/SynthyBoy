# AGENTS.md: the display layer of the MPC plugin (`shell-mpc/ui/`)

For AI coding agents. Imperative and terse. Native components and their catalogue: `docs/ui-components/README.md` and `components/AGENTS.md`. Licence of every file here: AGPL-3.0-or-later (keep the header `// Copyright (C) 2026 Safian Szabolcs` and `// SPDX-License-Identifier: AGPL-3.0-or-later` on a new file).

## What lives here
Header-only C++17, namespace `synthyboy_mpc::ui`. It holds only what the host cannot draw (graphs, meters, curve and spectrum displays, animation, indicators) and the parameter / Q-Link model of the native cells. Every control is a native component (`components/native/<name>`).
- Contract and drawing: `core.h` (`Component`, `Pen`, `Cell`, `TapGesture`), `model.h` (`ControlModel`, `NumberFieldModel`, `ModInput`: the pure parameter / Q-Link model, no drawing), `theme.h` + `theme_tokens.h` (`Look`, three tables), `qgroup.h` (`QGroup`: the four values of one Q-Link set), `q_visual_policy.h`.
- Displays: `display.h` (oscillator), `lfo_display.h` (LFO), `env_graph.h` (`EnvGraph`, with `time_ruler.h`), `filter_display.h` (`FilterDisplay`), `fx_display.h` + `fx_gallery_picture.h` + `fx_layout.h` (effect pictures and the tables of the effect pages), `voice_grid.h`, `clip_hold.h`.
- Editors: `scalar_curve_editor.h` (NOTE / VELO curve), `pz_curve_editor.h` (PZ content curve), `shape_editor.h` + `lfo_model.h` (drawn LFO shape), `list.h` (`ListPopup`). The on-screen keyboard is the native component `components/native/keyboard` (no C++ keyboard).
- Data and layout contracts of the native cells: `binding.h`, `native_apertures.h`, `play_rows.h`, `macro_card_contract.h`, `master_native_layout.h`, `voice_native_layout.h`, `choice_names.h`, `content_name.h`, `lfo_names.h`.
- `ui.h` / `ui_generic.h`: umbrella includes.

## Rules that must hold
1. THE CONTRACT. A component takes a plain `state` struct, a `Rect` (`place`), the flags `setQ` / `setEnabled` and an opaque `setId`. It draws only inside its rectangle (`draw (gpu::SceneBuilder&)`), takes `touchDown / touchMove / touchUp / tick / turn / wheel`, and returns a `Change { kind, id, value, index }` (kind `None` = nothing).
2. NEVER STORE THE TRUTH in a component. The value comes in through `state` every update; the owner writes the parameter and the next update shows it. Private and transient gesture data (the handle held, the start of a drag) may stay in the component. A component writes none of its `state`.
3. TOUCH TARGETS are at least 56 px in the smaller direction.
4. YELLOW (`theme::L().q`) is only for Q frames and chips. The accent colour is `flow[0]`.
5a. DEVICE RULE FOR Q GROUPS. The MPC XL keeps fixed hardware slots on pages of at most four groups, with no overlay bank frame. On four-knob devices a touch selects the bank. The active framed bank draws one outline and one unnumbered Q on its upper-left edge.
5b. Q-LINK VISUAL RULE. Keep numbered badges and dedicated number lanes out of components and previews. Preserve the hardware mappings and keep historical screenshots as evidence.
6. Q GROUPS. Use one bank frame for up to four adjacent members in reading order. A member never draws its own bank frame. `QGroup::check` must report zero offenders.
7. ONE CONTROL PER VALUE. Never show the same parameter twice on a page.
8. NO THIRD-PARTY PRODUCT NAMES in shown text. Names of choices that carry such a name go through `choice_names.h` (`shownChoice`, `shownLabel`).
9. CACHE CELLS. Each leaf draws in one `Cell` whose rectangle contains all its drawing. Start and end a cell in the default style and paint.
10. NO ALLOCATION, NO LOCKS, NO I/O in `draw`. The same state gives the same ops. `draw` is `const`.
11. TEXT MUST FIT ITS BOX: use `Pen::textCentered / textLeft / textRight` (they fit with an ellipsis).
12. THE THEME ONLY: every colour, size, line width and radius is a field of `theme::Look` (all three looks); no literal in a component. No glow on a line, value or text.
13. NEVER BUILD OR RUN HEAVY JOBS YOURSELF unless your task is the build: see "How changes are verified".
17. CURVE WINDOW ACTIONS (owner, 2026-11-24). Where the module has an actual activation/on-off control, place its existing native power button inside the curve window's upper-left corner, as on Play. Place the existing expand/editor action inside the upper-right corner. Keep both real touch targets at least 56 px and clear of graph handles and captions. Apply this arrangement to curve-based screens where those actions exist; do not invent activation parameters for modules without one. Power remains operable when the module is off.
18. OFF MEANS INACTIVE (owner, 2026-11-24). When a module or conditional function is off, dim/desaturate its affected controls and curve, and disable their parameter edits through touch, drag, wheel and Q-Link, including queued or stale gestures. Dim styling alone is insufficient. Retain stored values and restore editing when re-enabled. Keep power, module navigation and purely read-only viewing available; independent modules remain operable. Apply this only where a real on/off state exists, without inventing enable parameters.
19. NATIVE CONTROL OWNERSHIP (owner, 2026-11-24). Fix the native contract when a binding is wrong; the overlay holds graphs, spectra, modulation / live indicators and non-rendering state / Q adapters only.

## Where things are
- Library: `shell-mpc/ui/*.h`. Pages: `shell-mpc/overlay/*_ui.h` (one per tab; parameter binding rows, Q groups, the native bridge, the graph), `page_view.h` (hosts a page), `ui_bind.h` (binding helpers), `ui_stack.h`, `q_publish.h` (Q slots), `pages.h` (factory), `overlay_layout.json` (views).
- Tests: `shell-mpc/ui/tests/` (`ui_list_host`, `ui_time_ruler_host`), `shell-mpc/overlay/tests/`.

## How changes are verified in this project
- The WORKER does targeted checks only: the one test file of the one component, once, not after every edit.
- The INTEGRATION SESSION (or one runner) runs the full set once per round on the merged tree, through the build gate `bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh <command>` (at most two heavy jobs at a time, `-j4`): `shell-mpc/accept.sh` and the overlay host tests. A worker does NOT run the full suite, a cross build or a sanitizer build.
- When you change behaviour, change the header comment and the matching page in `docs/ui-components/` in the same commit.
