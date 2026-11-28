# Reusing the library in another plugin

This file is the basis for a later template project. It says exactly what depends on what. Every statement comes from the `#include` lines of the files named; where a file was not read in full, the line says so.

## 1. The library headers (`shell-mpc/ui/`)
The 43 headers (plus the umbrella `ui.h`) are header-only C++17. They include only standard headers, each other, and three files of `shell-mpc/overlay/`: `gpu/scene.h` (the scene ops), `sdf_font.h` with `sdf_font_data.h` (the font) and, for the tests, `scene_cpu.h`. Those three are self-contained (standard headers only).

### Free of SynthyBoy's engine (36 headers, without `ui.h`)
`core.h`, `theme.h`, `base.h`, `carousel.h`, `content_name.h`, `led_toggle.h`, `led_list.h`, `list.h`, `qgroup.h`, `composite.h`, `panel.h`, `text_entry.h`, `note_mask.h`, `mod_board.h`, `time_ruler.h`, `play_rows.h`, `display.h`, `env_graph.h`, `filter_display.h`, `lfo_display.h`, `lfo_model.h`, `lfo_parts.h`, `shape_editor.h`, `mod_matrix.h`, `mod_detail.h`, `mod_macros.h`, `mod_targets.h`, `mixer_strip.h`, `mixer_screen.h`, `voice_grid.h`, `voice_master.h`, `voice_screen.h`, `screen.h`, `env_screen.h`, `lfo_screen.h`, `choice_names.h`.
Two groups inside it:
- GENERIC widgets and mechanisms (reusable as they are): `core.h` (the contract, `Pen`, `Cell`, `TapGesture`), `theme.h`, `base.h` (Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, HBar, DetailButton, RouteButton, PictogramSelector), `carousel.h`, `content_name.h`, `led_toggle.h`, `led_list.h`, `list.h`, `qgroup.h`, `text_entry.h`, `note_mask.h`, `mod_board.h`, `time_ruler.h`, the `Composite` base and `layoutRow` of `composite.h`, the `KnobPanel` / `DetailPage` mechanism of `panel.h`.
- FREE OF ENGINE CODE BUT SHAPED LIKE SYNTHYBOY: they have no `synthyboy/` include, but their content is this synth's structure (five oscillator instances and five types, 6 envelopes, 10 LFOs and 4 random LFOs, 64 modulation slots, 10 mixer strips, 8 macros, 6 voice rows, scale tables, the pictogram table, names and ranges). Use them as examples or copy and adapt: `composite.h` (BasicControls, UnisonBox, WarpPair), the presets of `panel.h`, `screen.h`, `env_screen.h`, `env_graph.h` (time laws of the envelope parameters), `lfo_*`, `shape_editor.h` (the point model mirrors the engine's content shape), `filter_display.h`, `display.h`, `mixer_*`, `mod_*`, `voice_*`, `play_rows.h`. `choice_names.h` has no include but its table is keyed by SynthyBoy parameter keys.

### Bound to SynthyBoy's parameter table and effect types (7 headers)
Direct includes of `synthyboy/fx_types.h` and / or `synthyboy/params.h` (from `core/include/`):
| Header | Includes | Bound to |
|---|---|---|
| `fx_layout.h` | `choice_names.h`, `synthyboy/fx_types.h`, `synthyboy/params.h` | the engine keys ("chorus.rate") and `synthyboy::ParamInfo`; `kFxType...` numbers |
| `fx_chain_bar.h` | `synthyboy/fx_types.h` | the card pictures switch on `synthyboy::kFxType...`, splitters = `kNumFxInstanceTypes + 0..2` |
| `fx_display.h` | `synthyboy/fx_types.h` | `kindOfType` maps `kFxType...` to a display kind |
| `fx_gallery.h` | `fx_display.h`, `synthyboy/fx_types.h` | `drawPicture` has a case per effect type |
| `fx_slot_editor.h` | `fx_layout.h`, `fx_display.h`, `fx_band_board.h`, `synthyboy/fx_types.h`, `synthyboy/params.h`, ... | `ParamInfo` (name, range, unit, choices, `numChoices`) of the open effect |
Bound by inclusion: `fx_band_board.h` (includes `fx_chain_bar.h`), `fx_screen.h` (includes `fx_chain_bar.h`, `fx_gallery.h`, `fx_slot_editor.h`).
What you would replace: the effect type numbers and names; the `layoutOf (kind)` tables (your keys); the lookup of a control's `ParamInfo` (key, min, max, default, unit, decimals, `choices`, `numChoices`); the picture switch of `FxSlotCard` and `FxGallery::drawPicture`; the filter-response source of `FxDisplay`. The mechanism (rows of four cells, live conditions, dim or hide rule, band lanes, drag reorder) is generic.
NOTE: `core.h` does not include `synthyboy/...`: it is generic. The engine-bound set is the five headers above plus `fx_band_board.h` and `fx_screen.h`.
NOTE: `ui.h` includes `fx_screen.h`, so including `ui.h` pulls in `synthyboy/fx_types.h` and `synthyboy/params.h`. A reuse project includes the single headers, or the library is split (section 5).

## 2. The overlay runtime (`shell-mpc/overlay/`)
It puts the pictures on the MPC's display: a DRM plane above the host's UI, a GPU helper process, input from the touch screen and a mouse, Q-Link proxies, page detection by marker squares of the skin, and standby handling.
What it provides:
| Need | Files |
|---|---|
| drawing on the DRM plane | `discovery.h` (finds CRTC and planes at run time), `drm_interfaces.h`, `drm_linux.h` (raw ioctls, no libdrm), `presenter.h`, `geometry.h` (landscape 1280 x 800 onto the portrait panel), `stacking.h`, `vblank_linux.h`, `vblank_pacer.h` |
| scene and renderers | `gpu/scene.h` (protocol 8, shared with the helper), `draw.h`, `scene_cpu.h` (CPU, reference), `sdf_font.h`, `sdf_font_data.h`, `gpu/gpu_overlay.h`, `gpu/helper_transport_linux.h`, `gpu/cell_plan.h`, `gpu/cell_cache.h`, `gpu-helper/main.cpp` (+ `gl_api.h`, `CMakeLists.txt`: the helper executable, loads the firmware's EGL / GLES2 at run time), `gpu_clock.h` |
| page detector with the skin markers | `page_detector.h`, `instance_slots.h`, `overlay_layout.json` -> `overlay_layout.h` (edited directly), the skin side in `skin/TUI.json` |
| touch and mouse input | `touch_decoder.h`, `touch_input.h`, `touch_linux.h`, `mouse_input.h`, `mouse_linux.h`, `mouse_router.h` |
| Q-Link proxies | `../q_router.h`, `../q_step_rule.h`, `q_publish.h`, `q_device.h`, `qlink_setting.h`, `panel_key.h` (the host's Q-Link panel strip) |
| standby handling | `standby.h` (the host's blurred, dimmed repaint after about ten minutes) |
| pages | `view.h`, `page_view.h`, `pages.h`, `ui_stack.h`, `ui_bind.h` |
| orchestration | `controller.h` (one tick of the overlay thread), `service.h` (process wide thread and instances), `cpu_plan_linux.h` |

### Per file: generic, needs an adapter, or specific
Generic = no SynthyBoy include; reusable for another MPC plugin (Linux, MPC OS, the overlay build).
- GENERIC: `discovery.h`, `drm_interfaces.h`, `drm_linux.h` (needs `shell-mpc/safe_file.h`), `presenter.h`, `geometry.h`, `stacking.h`, `vblank_linux.h`, `vblank_pacer.h`, `draw.h`, `scene_cpu.h`, `sdf_font.h`, `sdf_font_data.h` (OFL font), `gpu/scene.h`, `gpu/cell_plan.h`, `gpu/cell_cache.h`, `gpu/gpu_overlay.h` (needs `safe_file.h`), `gpu/helper_transport_linux.h`, `gpu-helper/*`, `gpu_clock.h` (needs `safe_file.h`), `touch_decoder.h`, `touch_input.h`, `touch_linux.h`, `mouse_input.h`, `mouse_linux.h`, `mouse_router.h`, `standby.h`, `panel_key.h`, `qlink_setting.h` (needs `safe_file.h`), `q_device.h`, `ui_stack.h`, `cpu_plan_linux.h`, `../q_router.h` (pure logic, the plugin passes in how to read and write a real parameter), `../safe_file.h`.
- NEEDS AN ADAPTER (generic mechanism, but tied to SynthyBoy through one include): `view.h`, `controller.h`, `service.h`, `page_view_base.h`, `page_detector.h`, `instance_slots.h` are generic since migration steps 5 to 8 of TEMPLATE.md (they speak `mpckit::IInstance`, `IPageSet`, `IPageFactory`, `Branding`); `page_view.h` is SynthyBoy's hooks (`MacroNameHooks`, `page::Input`) over the generic view, `page_detector.h` and `instance_slots.h` (need your own `overlay_layout.json`, marker colours and skin), `q_publish.h` (includes `curve_source.h`, `synthyboy/params.h`), `../q_step_rule.h` (includes `synthyboy/params.h`: which parameters are stepped), `service.h` (includes `../device_profile.h`, `../output_peak.h`, `../play_log.h`, `../startup_timing.h`, `../poll_guard.h`, `../thread_policy.h`: the plugin shell).
- SPECIFIC: `instance_view.h` (defines `IInstanceView`, which derives from the kit's `mpckit::IInstance`; includes `synthyboy/content_state.h`, `content_store.h`, `readout.h`, `state.h`), `curve_source.h` (the filter curve), `ui_bind.h` (`synthyboy::ParamInfo`, the modulation table), `overlay_layout.json` / `overlay_layout.h` (SynthyBoy's views and markers), `pages.h`, every `*_ui.h`, `osc_page.h`, `osc_display.h`, `osc_marks.h`, `osc_spec_page.h`, `play_view.h`, `play_osc.h`, `views.h`, `renderer.h`, `env_geometry.h`, `spectrum_curve.h`, `content_browser.h`, `macro_names.h`.
(`gpu/` files and `*_linux.h` were classified from their include lists; the helper's `main.cpp` includes only `gpu_clock.h`, `sdf_font.h`, `gpu/*`, `vblank_*`, `gl_api.h` and `write_files.h`.)

### What a page needs from the host plugin
`IInstanceView` (`instance_view.h`; the generic half is `mpckit::IInstance` of `shell-mpc/kit/instance.h`): `param (tableIndex)`, `writeParam (tableIndex, normalised)`, `live (LiveValues&)`, `sampleRate()`, `bpm()`, `setQGroup (targets, n)`, `setQVirtual`, `takeQVirtual`, `setUiFullOpen`, plus SynthyBoy-only calls (`content()`, `setLfoShape`, `setMacroName`, `contentRoots`, `requestContent`, `padPressure`, `setOutputLimiter`, `playSelection`, ...). A new plugin implements the first group and ignores the rest (they have default bodies).

## 3. Licence
The repository is AGPL-3.0-or-later: the file `LICENSE` is the GNU Affero General Public License version 3, `NOTICE` says "version 3 or (at your option) any later version (SPDX: AGPL-3.0-or-later)", and every header of `shell-mpc/ui/` and `shell-mpc/overlay/` carries `Copyright (C) 2026 Safian Szabolcs` and `SPDX-License-Identifier: AGPL-3.0-or-later`. `sdf_font_data.h` says `AGPL-3.0-or-later AND OFL-1.1`: the font (Titillium Web SemiBold, `shell-mpc/overlay/fonts/OFL.txt`) keeps its own licence. In plain words: you may use, change and distribute the code under the AGPL; a plugin that contains these files is distributed under the AGPL terms, with its source. This is not legal advice; read `LICENSE` and `THIRD_PARTY.md`.

## 4. Verified by the project's own tests
`shell-mpc/ui/tests/` (interface tests, the demonstration `ui_demo.cpp`), `shell-mpc/overlay/tests/` (pages, gallery, lint, Q slots). They need `shell-mpc/accept.sh` (host build) and are run by the integration session.

## 5. The starter template (status)
This list was the plan for a starter plugin; the kit (`shell-mpc/kit/`) and the template (`template-mpc/`) now exist (`docs/ui-components/TEMPLATE.md` has the design, the migration and what is not verified). Items 1, 2, 4, 5, 6 and 7 are done there (a parameter source, `IInstance`, the Q proxies of the kit, the page set / factory / generated layout, the example plugin, a controller and service without SynthyBoy includes: all written and syntax-checked, not built or run); 8 is the template's host test `mpckit_demo_pages_host`; 3 and 9 are open. The text below is the original list.
1. A parameter adapter. `ui_bind.h` talks to `synthyboy::ParamInfo` (key, min, max, def, unit, decimals, `choices`, `numChoices`, `group`) and `synthyboy::toNormalised`, `constrainValue`, `findParamByKey`, `paramTable()`. Needed: a small interface (`ParamInfo`-like struct, lookup by key, normalise, constrain) that SynthyBoy implements and a new plugin implements for its own table.
2. A host interface. `IInstanceView` mixes the small set every plugin needs with SynthyBoy-only calls and includes engine headers. Needed: split into a minimal `IParamHost` and a SynthyBoy extension.
3. An effect-type adapter for the seven engine-bound headers (types, names, pictures, display kinds, the `layoutOf` tables), or a split of the library into a core and an `fx` part, and `ui.h` without `fx_screen.h`.
4. The Q router. `q_router.h` is generic but `q_step_rule.h` (which parameters are stepped) is not; the plugin shell glue that maps the 16 Q proxies to host parameters is in `shell-mpc/synthyboy_mpc.cpp`, not in the overlay.
5. Registration of pages. Today a page is a `kKind` constant in `overlay_layout.h` (kept in step with `overlay_layout.json` by hand), a case in `pages.h`, and the tab in the skin JSON of the plugin (`skin/TUI.json`, edited directly). A template needs these made data-driven.
6. An example plugin with two or three pages (for example a knob page, a list page with a popup, a detail page) built only from the generic headers, with its own parameter table, its own skin and tests, to show the whole chain.
7. Controller and service. `controller.h` and `service.h` include SynthyBoy shell files (`../device_profile.h`, `../play_log.h` ...).
8. A standalone build target for the header-only library and its host tests (today the tests are built by `shell-mpc/accept.sh` inside the SynthyBoy tree).
9. Documentation debt: the first paragraph of docs/MPC.md "UI components" still names an older palette and the 5 x 7 font; `theme.h` and `sdf_font.h` are the truth.
