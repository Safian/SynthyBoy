# UI gaps report: closing the NOT DONE / partial rows of UI-GESTURE-AUDIT.md

Branch cut from `develop` (84b1f27). Status per row with file:line: `UI-GESTURE-AUDIT.md` (count at the top). Tests: `shell-juce/ui/tests/test_ui_gaps.cpp` (headless components, synthesised mouse events; target `synthyboy_test_ui_gaps`), pure logic in `test_ui_model.cpp`. Screenshots: `/tmp/ui-shots/gaps/` (phone 390x844, tablet 1024x768, 26 files).

## Result

124 audit rows: 46 closed in this change, 2 NOT DONE with a hard reason, 7 GAP (design has no key / feature, left), 0 partial.

NOT DONE, hard reasons:
- **02-28 noise sample `< >`**: `docs/params.json` has no noise sample key (only level, pan, on, destination, filter blend, bus sends). A control would need an invented key.
- **05-22 Modulate X / Y**: the engine honours only global sources for point modulation (WP123-REPORT.md "BLOCKED" 1); the editor would write data nothing reads. `core/` is not mine to change.

Limits that stay written down in the rows (the gesture is done, the data behind it is missing):
- The shell has no `IContentStore`, so no sample bytes: the sample view draws a procedural waveform, and Auto slice MARKERS cannot be drawn (the engine computes onsets from the bytes, `IEngine` does not hand them out). Manual slices are `content().samples[osc].slices`, already reachable through `UiHost::content ()`; they need a loaded sample (ContentSample) to be editable. The threshold line is drawn and draggable.
- Wavetable import progress is an indeterminate bar (the importer reports no fraction), work runs on a thread, Cancel discards.
- The voice badge needs a 96 pt name area in the top bar: on phone / tablet landscape the count is on the GLB tab (`GLB 3`) and in GLOBAL > Scope.

## What was built

Kit: `GestureModel` (pure: `Viewport` zoom / pan, slice list, loop pair, transpose split, warp groups, warp pictures, `TouchTracker` two-finger tap / swipe / pinch / pan, number parsers, CPU level), `GestureKit` (`Loupe`, `TwoFingerInput`, `LongPressDetector`, `Toast`, `ModuleMenuButton`), `ModuleSheet` (Copy / Copy with mods / Paste / Paste with mods / Swap with / Init / Lock = disabled GAP; `module_ops` use `ModuleClipboard` and `swapModules`).

Panels: every module panel header (osc, filter 1 / 2, env, LFO, random LFO) has long-press = module sheet, a `...` button, and header drag onto another header = swap (Alt copy, Alt + Shift with mods). `OscTabsPanel` chip row; `TransposeControl` OCT / SEM; grouped WARP list; sample view zoom / pan / slices / threshold / loop pair / loupe; mixer strip header sheet, swap / copy, pan band; wavetable view 3D tilt / pinch; wavetable editor (canvas zoom / pan / loupe / hold = nudge, FFT numeric bin entry, nudge all phases, thumbnail reorder, swipe-down close, two-finger tap undo, async import with progress); LFO shape editor (Steps, Brush with Vital's five paint patterns, marquee + Prop, All curves, segment menu, X / Y fields, Loop here / Start here, grid fields); envelope tabs (select, long-press destinations, drop on a control, magnifier slider); modulation (depth badge target, macro chip swap, matrix row handle + menu); GLOBAL scope panel, voice badge, toast with real Init / Init LFOs / Revert; FX chain slot power (dot, double tap, `[ON]`, Alt), hold-to-reorder.

Host interface (`UiHost.h` + `PluginProcessor.*`): `activeVoiceCount ()`, `cpuLoad ()`, `copyScope ()` (lock-free ring written in `processBlock`), `revertToSaved ()` (snapshot of the last loaded state). Tested with a real `processBlock` in `uig_voice_badge_cpu_pill_and_scope` and `uig_settings_buttons_act_and_confirm_with_a_toast`.

## Behaviour changes to know about

- The three oscillator panels, the six envelopes fold away unless chosen on their chips / tabs (same pattern as the LFO tabs).
- Init Preset now resets every parameter and empties the content (it reset four keys before); Init LFOs and Revert really act (they only changed their label).
- The FX chain slot is dragged only after a hold (design); `< Move Left` / `Move Right >` remain.
- The old warp picture mapped the first table indices to the wrong modes; it follows `oscN.warp.type` now.
- `Panel::headerHeight ()` is 44 pt for module panels (room for the `...` button).
- Contract tests (`test_ui_render`, `test_glb`) accept key-less display panels (`OscTabsPanel`, `GlbScopePanel`).

## Gates

`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8 && ctest --test-dir build`: see the final message of the worker for the run. No compiler warning in the changed sources.
