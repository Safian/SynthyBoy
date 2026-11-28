# AGENTS.md: the plugin-drawn layer (`shell-mpc/ui/`)

For AI coding agents. Imperative and terse. Licence of every file here: AGPL-3.0-or-later (keep the header `// Copyright (C) 2026 Safian Szabolcs` and `// SPDX-License-Identifier: AGPL-3.0-or-later` on a new file).

## What this layer is (owner, 2026-11-27)
There is NO C++ widget library any more. Every control (button, dial, dropdown, field, radio, switch, fader, list header) is a native host component, defined in `skin/TUI.json` from the recipes in `components/native/`. The plugin draws only what the host cannot: graphs, meters, curve displays, animation and live indicators, plus the binding data that connects the native cells to the engine. Do not add a C++ control, a C++ hit-test or a fallback input path for a control the skin already has; fix the native contract instead (owner, 2026-11-24, native control ownership).

The skin is committed and edited directly: `skin/TUI.json`, `skin/Q-Links.json`. There is no skin generation; `theme_tokens.h`, `master_native_layout.h`, `voice_native_layout.h`, `macro_card_contract.h` and the other headers of this folder are hand-kept sources (`components/colors.json` / `sizes.json` are the Python-side tokens; keep the two in step by hand until the TODO entry about a single owner is closed).

## What survives here
- Binding data: `binding.h` is one plain `Row` per native control (engine key, kind, range, step, default, text format, Q-Link group and slot) and tables of row indices for the Q banks. No drawing, no touch, no state. `model.h` (`ControlModel`, `NumberFieldModel`, `Change`, `ModInput`) and `qgroup.h` (Q tables and the adjacency check) are the parameter / Q-Link model the pages and the Q router use. A page owns its `constexpr` row table; the engine index is resolved by key (`uibind::indexOfKey`).
- Displays, graphs and editors drawn by the plugin on the overlay plane: `display.h` (oscillator picture), `lfo_display.h` and `lfo_model.h` (LFO picture and shape model), `env_graph.h`, `filter_display.h`, `fx_display.h` with `fx_layout.h` and `fx_gallery_picture.h`, `scalar_curve_editor.h` (NOTE / VELO curve), `shape_editor.h` (LFO shape), `pz_curve_editor.h`, `voice_grid.h` (plain functions), `time_ruler.h`, `clip_hold.h`.
- Drawing core: `core.h` (`Pen`, `Cell`, `Rect`, `TapGesture`, colours), `theme.h` and `theme_tokens.h` (the look; `Pen::control` draws the Button gradient from the same tokens while any overlay control remains).
- Kept popups (owner): `list.h` (`ListPopup`) and `text_entry.h` (`TextEntry`, our own C++ on-screen keyboard for Macro rename and LFO shape name; owner, 2026-11-28: no system keyboard exists for a plugin, the native Button keyboard was tried and deleted, do not rebuild it).
- Helpers: `choice_names.h` (`shownChoice`, `shownLabel`), `content_name.h`, `lfo_names.h`, `play_rows.h`, `native_apertures.h`, `q_visual_policy.h`.
- Pages that use these live in `shell-mpc/overlay/*_ui.h` (one per tab), with `page_view.h`, `ui_bind.h`, `q_publish.h`, `pages.h` and `overlay_layout.json`.

## Rules that must hold
1. A DISPLAY TAKES A PLAIN STATE and a `Rect`, draws only inside it (`draw (gpu::SceneBuilder&)`), and keeps no parameter truth: the owner writes the parameter and the next update shows it. A gesture that would not change the value is not reported.
2. TOUCH TARGETS of what the plugin still handles (curve handles, editors) are at least 56 px in the smaller direction.
3. YELLOW (`theme::L().q`) is only for Q frames and chips. The accent colour is `flow[0]`.
4. Q-LINK RULES. The MPC XL keeps fixed hardware slots on pages of at most four groups, with no overlay bank frame; on four-knob devices a touch selects the bank. One bank frame per group of up to four adjacent members in reading order; a member never draws its own frame. Keep numbered badges out of the drawing; preserve the hardware mappings. A page publishes up to sixteen slots.
5. ONE CONTROL PER VALUE: never show the same parameter twice on a page.
6. NO THIRD-PARTY PRODUCT NAMES in shown text; go through `choice_names.h`.
7. EVERY ENGINE PARAMETER MUST STAY REACHABLE: a native control on some page and a member of a Q group.
8. NO ALLOCATION, NO LOCKS, NO I/O in `draw`; the same state gives the same ops; `draw` is `const`.
9. TEXT MUST FIT ITS BOX: `Pen::textCentered / textLeft / textRight` fit with an ellipsis.
10. THE THEME ONLY: every colour, size, line width and radius is a `theme::Look` field; no literal. No glow on a line, value or text.
11. CURVE WINDOW ACTIONS (owner, 2026-11-24). Where the module has a real on/off control, its native power button sits in the curve window's upper-left corner, the expand/editor action in the upper-right corner, both at least 56 px and clear of handles and captions. Do not invent activation parameters. Power stays operable when the module is off.
12. OFF MEANS INACTIVE (owner, 2026-11-24). When a module or conditional function is off, dim its controls and curve and block parameter edits through touch, drag, wheel and Q-Link, including queued or stale gestures; dimming alone is not enough. Keep stored values, power, navigation and read-only viewing. Only where a real on/off state exists.
13. NATIVE CONTROL OWNERSHIP (owner, 2026-11-24). Once a control is native, its GPU drawing and fallback input path are gone. Keep only genuinely required graphs, spectra, modulation / live indicators and non-rendering state / Q adapters.
14. A NEW PRESET OR LAYOUT IS DATA (a table row), not new layout code.

## How to add a native control to a page
1. Pick or extend the recipe in `components/native/<name>` (add a variant there, never in the screen). Define the instance in `skin/TUI.json` by editing it directly.
2. Add a `binding::Row` (id, engine key, kind, range, format, Q group and slot) to the page's table and put it in a Q group, or leave it touch-only on purpose.
3. In the page's Ui class (`shell-mpc/overlay/<page>_ui.h`) read the parameter through the row and write only through `uibind::writeNorm / writePlain`; publish the Q targets in `publishQGroup`.
4. Update `docs/ui-map/<page>.md` and `docs/MPC.md` (Q-group table) in the same commit.

## Verification
- During a UI rewrite (the removal of overlay controls, the conversion of pages to native cells, skin work) run NO tests and no builds (owner decision). Fix what the compile of the next integration round shows. Never add a baseline that absorbs a defect.
- Outside rewrites the WORKER runs only the one test of the one file it changed, once. The integration session runs the build and the touched tests once per round through `bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh <command>` (at most two heavy jobs, `-j4`). Tests that remain here: `shell-mpc/ui/tests/` (`ui_list_host`, `ui_text_entry_host`, `ui_time_ruler_host`), `shell-mpc/tests/`, `shell-mpc/overlay/tests/`.

## Common mistakes
- A control correct on one page and wrong on another: fix the recipe, not the page.
- Q group frame that encloses non-members, or chips not in reading order: make them members or move them out.
- Value text bugs: rates under 1 Hz shown as "0 Hz", "-0.0 dB", one value written differently in two places.
- A switched-off module drawn as a bright live curve: draw it dim with an OFF tag.
- Cache hazards: ops outside the cell rectangle, text wider than its cell.
- Documents that disagree with code: change `docs/MPC.md`, the header comment and `docs/ui-map/` in the same commit.
