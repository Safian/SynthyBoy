# AGENTS.md: the MPC UI component library (`shell-mpc/ui/`)

For AI coding agents. Imperative and terse. Human docs with one page per component: `docs/ui-components/` (start at `docs/ui-components/README.md`). Design record: `docs/MPC.md` "UI components". Licence of every file here: AGPL-3.0-or-later (keep the header `// Copyright (C) 2026 Safian Szabolcs` and `// SPDX-License-Identifier: AGPL-3.0-or-later` on a new file).

## Rules that must hold
1. THE CONTRACT. A component takes a plain `state` struct, a `Rect` (`place`), the flags `setQ` / `setEnabled`, and an opaque `setId`. It draws only inside its rectangle (`draw (gpu::SceneBuilder&)`), takes `touchDown / touchMove / touchUp / tick / turn / wheel`, and returns a `Change { kind, id, value, index }` (kind `None` = nothing).
2. NEVER STORE THE TRUTH in a component. The value comes in through `state` every update; the owner writes the parameter and the next update shows it. A change that would not change the value is not reported. Private and transient gesture data (the handle held, the start of a drag, the last position reported) is not the truth and may stay in the component; view state that is not a parameter (a selection, a lock, a zoom) is the owner's too: the component asks for it with a `Request` or a change that names it, and the owner sets it in `state` on the next update. A component writes none of its `state`.
3. TOUCH TARGETS are at least 56 px in the smaller direction (`touchTargets` must list the real rectangles; a component with inner cells overrides it). `minSize()` is the smallest legible size; the layout gives at least that.
4. YELLOW (`theme::L().q`) is only for Q frames and chips. The accent colour is `flow[0]`.
5a. DEVICE RULE FOR Q GROUPS. The MPC XL keeps fixed hardware slots on pages of at most four groups, with no overlay bank frame. On four-knob devices a touch selects the bank. The active framed bank draws one outline and one unnumbered Q on its upper-left edge.
5b. Q-LINK VISUAL RULE. Keep numbered badges and dedicated number lanes out of components and previews. Preserve the hardware mappings and keep historical screenshots as evidence.
5. FRAMELESS LED CONTROLS: use `LedToggle` (on / off) and `LedList` (2 to 8 choices) instead of boxed toggles and steppers. More than 8 choices, or no room: `Stepper` with `listable` + `ListPopup`.
6. Q GROUPS. Use one bank frame for up to four adjacent members in reading order. A member never draws its own bank frame. `QGroup::check` must report zero offenders. A page publishes up to sixteen hardware slots through `QSlots::publish`.
7. ONE CONTROL PER VALUE. Never show the same parameter twice on a page.
8. NO THIRD-PARTY PRODUCT NAMES in shown text. Names of choices that carry such a name go through `choice_names.h` (`shownChoice`, `shownLabel`).
9. EVERY ENGINE PARAMETER MUST STAY REACHABLE: a control on some page and a member of a Q group. `shell-mpc/overlay/tests/coverage_host.cpp` fails on a new gap and on a gap that was closed but not removed from its table.
10. THE LAYOUT LINT MUST STAY CLEAN (`shell-mpc/overlay/tests/layout_lint_host.cpp`, baseline `layout_lint_baseline.json` is empty): a overlapping controls; b touch rectangle < 56 px; c outside the tab (1280 x 628) or the parent; d text cut, wider than its cell or control, overlapping texts; e chip covers a knob arc or handle; f same id on two controls of a page; g titled block under 50 % covered; h more than three knob diameters on a page.
11. CACHE CELLS. Each leaf draws in one `Cell` whose rectangle contains all its drawing. The bank frame and its Q marker share a single cell. Start and end a cell in the default style and paint.
12. NO ALLOCATION, NO LOCKS, NO I/O in `draw`. The same state gives the same ops. `draw` is `const`.
13. TEXT MUST FIT ITS BOX: use `Pen::textCentered / textLeft / textRight` (they fit with an ellipsis); give a name its width (`LedToggle::widthFor`).
14. THE THEME ONLY: every colour, size, line width and radius is a field of `theme::Look` (all three looks); no literal in a component. No glow on a line, value or text.
15. A NEW PRESET OR LAYOUT IS DATA (a table row: `PanelPreset`, `fxl::Layout`, `pictogram::kTable`), not new layout code.
16. NEVER BUILD OR RUN HEAVY JOBS YOURSELF unless your task is the build: see "How changes are verified".
17. CURVE WINDOW ACTIONS (owner, 2026-11-24). Where the module has an actual activation/on-off control, place its existing native power button inside the curve window's upper-left corner, as on Play. Place the existing expand/editor action inside the upper-right corner. Keep both real touch targets at least 56 px and clear of graph handles and captions. Apply this arrangement to curve-based screens where those actions exist; do not invent activation parameters for modules without one. Power remains operable when the module is off.
18. OFF MEANS INACTIVE (owner, 2026-11-24). When a module or conditional function is off, dim/desaturate its affected controls and curve, and disable their parameter edits through touch, drag, wheel and Q-Link, including queued or stale gestures. Dim styling alone is insufficient. Retain stored values and restore editing when re-enabled. Keep power, module navigation and purely read-only viewing available; independent modules remain operable. Apply this only where a real on/off state exists, without inventing enable parameters.

19. NATIVE CONTROL OWNERSHIP (owner, 2026-11-24). Once a control is replaced by the native MPC component, remove its obsolete GPU base drawing and fallback input path. Do not retain a second control UI behind a native-availability flag or silently revive it when a binding is wrong. Fix the native contract instead. Keep genuinely required graphs, spectra, modulation/live indicators and non-rendering state/Q adapters. The following GPU component table applies only to overlay elements that still have an actual production owner; use `components/native/` for native widgets.

## Which component for which job
| Job | Component |
|---|---|
| on / off | `LedToggle` |
| 2 to 8 named choices | `LedList` |
| more than 8 choices | `Stepper` (`listable`) + `ListPopup` |
| choice shown as a picture | `PictogramSelector` |
| continuous value | `Knob` (row cell: `SliderBar`, `HBar`) |
| whole number | `NumberField` (small integer: `UpDownStepper`) |
| level + meter | `Fader` |
| set of notes | `NoteMask` |
| open a page / popup | `MenuButton`, `DetailButton`, `RouteButton` |
| name something | `TextEntry` |
| four values on four knobs | `QGroup` |
| grid of knobs from a table | `KnobPanel` |
| details page | `DetailPage` |
| modulation of one control / of one source | `ModulationsPage` / `ModTargetsPage` |
| pick a modulation source | `ModSourceBoard` |
| envelope by touch | `EnvGraph` |
| drawn LFO shape | `ShapeEditorPage` |
| NOTE / VELO transfer curve | `ScalarCurveEditor` |
| filter response + handle | `FilterDisplay` |
| oscillator / LFO picture | `display::draw`, `lfodisplay::draw` |
| effect chain and page | `FxChainBar`, `FxSlotEditor`, `FxFullScreen` |

## Where things are
- Library: `shell-mpc/ui/*.h` (index below). Contract: `core.h`. Look: `theme.h`. Q: `qgroup.h`. Tests: `shell-mpc/ui/tests/` (`COVERAGE.md` inventories every class). Docs: `docs/ui-components/`.
- Pages: `shell-mpc/overlay/*_ui.h` (one per tab), `page_view.h` (the template that hosts a page), `ui_bind.h` (parameter binding helpers), `ui_stack.h` (full / detail pages), `q_publish.h` (Q slots), `pages.h` (factory), `overlay_layout.json` (views).
- Tests of pages and the lint: `shell-mpc/overlay/tests/` (`*_ui_host.cpp`, `coverage_host.cpp`, `qslots_host.cpp`, `layout_lint_host.cpp`, `ui_gallery_host.cpp`, `ui_gallery_extra_host.cpp`). Pictures of every page: `docs/user-guide/img/*.png`.

## How to add a control (step by step)
1. Pick the component from the table. Read its page in `docs/ui-components/`.
2. Add it as a public member of the screen class; `addKid` it in the constructor; place it in `layout()` at or above `minSize()`.
3. Put it in a `QGroup` (reading order, adjacent) or leave it out on purpose and say why in a comment.
4. In the page's Ui class (`shell-mpc/overlay/<page>_ui.h`): add an id to the enum, `bind (id, &screen.member, T::..., "engine.key")`, publish the Q target in `publishQGroup`. `pull` reads the parameter into `state`; `handle` writes through `uibind::writeNorm` / `writePlain` only.
5. Write the test first: every gesture gives exactly the documented change; clamps at the ends; nothing when disabled; the gesture is kept outside the rectangle; drawing stays inside the rectangle at the minimum and at a large size.
6. Q GROUPS. Use one bank frame for up to four adjacent members in reading order. A member never draws its own bank frame. `QGroup::check` must report zero offenders. A page publishes up to sixteen hardware slots through `QSlots::publish`.

## How to add a page (step by step)
1. Screen: `shell-mpc/ui/<name>_screen.h`, a `Composite` over library components, one table of Q groups, 1280 x 628.
2. Ui class: `shell-mpc/overlay/<name>_ui.h` after `voice_ui.h` (ids enum, `T` table, `configure`, `pull`, `handle`, `publishQGroup`, `touch`, `wheel`, `tick`, `draw`, `fingerDown`, `dropGesture`, `forgetQGroup`, `topPage`, `qTargets`, `qSlotCount`, `kElementPage`).
3. Register: a view kind in `overlay_layout.json`, regenerate `overlay_layout.h` (`scripts/gen-overlay-layout-header.py`), a `case` in `pages.h`, the skin script `scripts/mpc-skin-extras/83_pages.py`.
4. Tests: a host test of the screen in `shell-mpc/ui/tests/`, a host test of the page, `coverage_host.cpp`, `qslots_host.cpp`, the gallery states, the lint states.
5. Docs: `docs/MPC.md` Q-group table, `docs/ui-components/`.

## How changes are verified in this project
- The WORKER does targeted checks only: the one test file of the one component, once, not after every edit.
- The INTEGRATION SESSION (or one runner) runs, once per integration round on the merged tree, through the build gate `bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh <command>` (at most two heavy jobs at a time, `-j4`): `shell-mpc/accept.sh` (builds and runs the UI tests under AddressSanitizer and UBSan), the overlay host tests, `layout_lint_host`, `coverage_host`, `qslots_host`, and the gallery render (`scripts/render-ui-gallery.py --build`, `scripts/render-ui-gallery-extra.py`). A worker does NOT run the full suite, a cross build, a sanitizer build or the gallery.
- Which tests cover what: `ui_base_host.cpp` (base controls), `ui_composite_host.cpp` (composites), `ui_screen_host.cpp` (the two oscillator views, rows of the thirds), `ui_cov_*_host.cpp` (knobs and values, steppers and lists, toggles and buttons, popups and scrolling, Q groups and chips, layout and text, composites and panels, new parts), `ui_led_host.cpp` (LED controls), `ui_carousel_host.cpp` (the selector of the thirds), per-screen tests `ui_env_host`, `ui_lfo_host`, `ui_fx_editor_host`, `ui_fx_tables_host`, `ui_display_host`, `ui_list_host`, `ui_text_entry_host`, `ui_time_ruler_host`; `ui_demo.cpp` shows real usage; `shell-mpc/overlay/tests/` for pages, Q slots, coverage, lint, cached cells (`cell_plan_host`, `cell_cache_host`, `gpu_scene_host`).

## Common mistakes found in this project's reviews
- A control that is correct on one page and wrong on another: fix the component, not the page.
- Touch rectangle smaller than 56 px: a component with inner cells (a list, a board, a grid) reports one big rectangle; override `touchTargets` with the real cells.
- Chips of one Q group at two heights: the Q member must be the CELL (the same rectangle as a knob cell, chip at its top left), not a smaller control centred inside it.
- Q group frame that encloses non-members (HZ / BPM / KEY inside the LFO group): make them members or move them out; run `QGroup::check`.
- Chips not in reading order (2, 1, 3, 4 left to right): reorder the members.
- Partial rows without idle chips: a row that is published to the router must show its chips; a value that cannot be reached from the knobs must be published.
- The same value twice on a page (a stepper and a list for one parameter): keep one.
- A list that shrinks the display or the knob row to fit: a list never costs the display or the knob row their size; a page with one row does not stretch the row over the board (rows have a maximum height).
- A toggle that flips on any turn: `turn` up = on, down = off, nothing when already so.
- A grid that reads column-first: lists and grids read in rows (row-major).
- Value text bugs: rates under 1 Hz shown as "0 Hz" (use two decimals under 10 Hz, one under 100), "-0.0 dB" (a value that rounds to zero has no sign), one value written differently in the header, the cell and the strip (one text for one value).
- A selector that does not follow the instance (the filter 2 page showing F1 selected): write the selector index from the instance in `update`.
- A switched-off module drawn as a bright live curve: draw it dim with an OFF tag.
- A full-screen button covering the peak of a wave: the curve rectangle ends left of the button.
- Cache hazards: ops outside the cell rectangle, text wider than its cell, Q chip digits outside the cell of `drawIdle`: open one `Cell` per chip.
- A change in a library header without a picture: new or changed pages must be in the gallery so someone has seen them rendered.
- A baseline that absorbs defects: never add a lint finding to `layout_lint_baseline.json` to make a test pass; fix the layout.
- Documents that disagree with code: when you change behaviour, change `docs/MPC.md`, the header comment and `docs/ui-components/` in the same commit.

## Index of components (machine friendly)
Format: `Name | header | one line | Change kinds`. Kinds: V Value, I Index, R Request, D Depth, M ModsRequest, A AddRequest, - none.
```
Component | core.h | base class of every component | -
Composite | composite.h | base of components made of components | -
QGroup | qgroup.h | four members, one frame, chips, knob routing | - (turn gives member's)
Knob | base.h | rotary 0..1 with modulation arc and chip | V D M I(led)
NumberField | base.h | whole number or step value, drag | I D M
Stepper | base.h | prev / next choice, listable name | I R
Carousel | carousel.h | current entry in the middle, neighbours at the sides, tap / drag steps, name opens the list | I R
UsedSelector | used_selector.h | used + selected cells in number order, one '+' for the next free (Env / LFO tabs) | I R
ScalarCurveEditor | scalar_curve_editor.h | NOTE / VELO curve: graph, points, legato, used-by (needs the core; not in ui.h) | I R
UpDownStepper | base.h | small integer up / down | I
ToggleIcon | base.h | boxed on / off with icon | I
LedToggle | led_toggle.h | frameless dot + name on / off | I
LedList | led_list.h | 2..8 visible choices, one lit | I
MenuButton | base.h | label button, icon | R
DetailButton | base.h | gear / bell / route button, optional drag value | R V I
RouteButton | base.h | 3 char target + 2 segments | R
RouteDropdown | mixer_strip.h | frameless dropdown with hairline | R
HBar | base.h | horizontal bipolar bar | V
SliderBar | mixer_strip.h | horizontal slider in a 56 px cell | V
Fader | mixer_strip.h | vertical level fader with meter | V
StripPower | mixer_strip.h | power glyph + name | I
PictogramSelector | base.h | row of pictograms | I
NoteMask | note_mask.h | 12 note LED cells, mask | I
ListPopup | list.h | popup list with backdrop, scroll | I R
TextEntry | text_entry.h | on-screen keyboard page | R
ModSourceBoard | mod_board.h | chips of modulation sources | I
ModulationsPage | panel.h | modulations of one control | V I R A
ModTargetsPage | mod_targets.h | targets of one source | V I R I(page)
UnisonBox | composite.h | gear + voices stepper | R I
WarpSlot / WarpPair | composite.h | warp knob + type + var | V I
KnobRandPair | composite.h | knob + RAND knob | V
SourcePicker | composite.h | stepper + dropdown | I R
BasicControls | composite.h | Basic Q group of an oscillator | V I R
RoutePanel | composite.h | route popup: target, balance, buses | V I R
KnobPanel | panel.h | preset grid of parts | per part
DetailPage | panel.h | back + display + panel in Q groups | R per part
OscillatorCompact | screen.h | third of the Play page | I R V
OscillatorFullScreen | screen.h | full oscillator page | I R V M
EnvGraph | env_graph.h | DAHDSR graph with handles | V R
EnvCarousel / EnvSelector | env_screen.h | envelope 1..6 selector (carousel with target dots) / cells | I R
TypeSelector | base.h | PictogramSelector that can be a drop-down (type of an oscillator) | I R
EnvCompact / EnvFullScreen | env_screen.h | envelope views | V I R
FilterDisplay | filter_display.h | response + cutoff/reso handle, formant pad | V (x, then y by takePending)
FilterOutReadout | filter_display.h | read-only DEST / BUS 1 / BUS 2 / LEVEL / PAN of the Filter page's OUT block | -
FilterKnob / FilterToggle | filter_display.h | knob / toggle with dim look | V / I
FilterFullScreen | filter_display.h | the ONE Filter page: states F1 / F2 / NOTE / VELO, F1+F2 view toggle | V I R
display::draw | display.h | oscillator picture (function) | -
lfodisplay::draw | lfo_display.h | LFO picture (function) | -
ShapePicker | lfo_parts.h | grid of LFO shapes or styles | I
SyncSwitch | lfo_parts.h | HZ / BPM / KEY logic (not a component) | -
RateControl | lfo_parts.h | rate knob or division stepper | V I
LfoCompact / LfoFullScreen | lfo_screen.h | LFO views | V I R
ToolSelector | shape_editor.h | POINT FLAT RAMP+ RAMP- SELECT SOFT BEND | I
ShapeCanvas | shape_editor.h | drawing surface of the shape, the selection, the marker | I (Op)
ShapeEditorPage | shape_editor.h | LFO shape editor page (3 Q groups) | V I R
PointModPage | shape_editor.h | modulation of the selected points of a shape | V I R
MixerStrip | mixer_strip.h | one channel strip | V I R
MixerRoutingBar | mixer_screen.h | signal flow bar | -
MixerFullScreen | mixer_screen.h | ten strips + routing bar | V I R
ModRowSelector | mod_matrix.h | row number, steps the row | I
ModRow | mod_matrix.h | one matrix row | V I R
ModMatrixView | mod_matrix.h | matrix page, 64 slots | V I R
ModSlotDetail | mod_detail.h | one slot page | V I R
SpectrumStrip | mod_macros.h | spectrum line | -
MacroCard / MacroView | mod_macros.h | macro cards and page | V R I
VoiceGrid | voice_grid.h | 8 x 6 step grid | V (-1..+1)
VoiceScreen | voice_screen.h | voice page | V I
PortamentoCurveDisplay | voice_master.h | glide curve | -
MiniKeyboardDisplay | voice_master.h | octave with scale, picks key | I
PeakMeterDisplay | voice_master.h | two peak meters | -
VoiceMeterDisplay | voice_master.h | sounding voices bar | -
MasterScreen | voice_master.h | master page, two sides | V I R
FxSlotCard | fx_chain_bar.h | effect card (index = action) | I
FxChainBar | fx_chain_bar.h | chain of cards | I
FxBandTile / FxBandBoard | fx_band_board.h | band lanes of a splitter | I
FxDisplay | fx_display.h | effect picture (state, no touch) | -
FxGallery | fx_gallery.h | cards of effect types | I (type number)
FxSlotEditor | fx_slot_editor.h | page of the open effect | V I R
FxFullScreen | fx_screen.h | FX page | V I R
Helpers | theme.h play_rows.h time_ruler.h lfo_model.h choice_names.h | look, rows, ruler, shape model, shown names | -
```
Engine-bound (need `synthyboy/fx_types.h`, `params.h`): `fx_layout.h`, `fx_chain_bar.h`, `fx_display.h`, `fx_gallery.h`, `fx_slot_editor.h`, and by inclusion `fx_band_board.h`, `fx_screen.h`, `ui.h`.
