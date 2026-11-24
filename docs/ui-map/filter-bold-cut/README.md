# Filter of the bold cut: before / after pictures

Owner decision 2026-11-21 ("Bold cut", the Env / Filter / LFO tab merges), step 4 of `docs/ui-map/env-filter-lfo-plan.md`; spec `docs/ui-map/SIMPLIFY.md` (E Filter, safe list K: U4, U5, D9, D10, Q3). Pictures: `scripts/render-ui-gallery.py` (`ui_gallery_host`, state Init) and `scripts/render-ui-gallery-extra.py` (`ui_gallery_extra_host`), 1280 x 800, 16 Q-Link knobs (XL chips), quantised to 128 colours. `before/` = develop 7147266b (steps 0 - 3 merged) before any change of the Filter pages; `after/` = this branch. Nothing is removed from the engine: every parameter is still reachable (the removed controls are read-only indicators here, their home is the Mixer; `coverage_host` proves it).

The page list is 15 pages now (22 -> 16 -> 15): Filter 1 and Filter 2 are ONE tab `Filter` (the view `filter1` keeps its name and marker colours, `filter2` is gone), still 9 function keys (the Filter key has one tab). The page has FOUR STATES, the chunk-only view state `mpcTabView.filter` (saved in the chunk, no host or engine parameter): F1 | F2 | NOTE | VELO.

## Controls per state

| state | before | after |
|---|---|---|
| F1 / F2, a normal model | 19 (POWER, CURVES list, MODEL, model list button, STYLE, CLEAN, OUTPUT button; MAIN 4, TRACK 4 with COMB dimmed, OUT 4) + 5 in the OUTPUT popup = 24 | **15**: POWER, state selector, F1+F2 toggle, MODEL, STYLE, CLEAN; MAIN 4; TRACK 3; OUT 2 (BLEND, ROUTING) |
| Comb model | 19 (+ 5 popup) | **16** (COMB shown, in the fourth TRACK slot) |
| Formant model | 24 (+ 5 popup): VAR and COMB dimmed, CLEAN dimmed, FORMANT block 5 | **18**: VAR, COMB, CLEAN hidden; TRACK = KEYTRACK, ENV 1, formant RESONANCE; FORMANT block X, Y, TRANSPOSE, SPREAD |
| NOTE / VELO | did not exist (`MPC-n nincs szerkeszto`) | **13** + the graph: state selector, LEGATO, INIT, UNDO, DELETE POINT, TARGETS, POINT, X, Y, CURVE, GRID X, GRID Y, SNAP |
| Q groups | MAIN 4, TRACK 4, OUT 4, FORMANT 4 (the formant resonance in none) | MAIN [CUTOFF, RESO, DRIVE, MIX], TRACK [KEYTRACK, ENV 1, VAR or formant RESONANCE, COMB (Comb only)], OUT [BLEND, ROUTING, MODEL, STYLE] (frameless: MODEL and STYLE stand in the top bar), FORMANT [X, Y, TRANSPOSE, SPREAD]; NOTE / VELO: EDIT [POINT, X, Y, CURVE], LEGATO AND GRID [LEGATO, GRID X, GRID Y, SNAP] |

| control | before | after | where |
|---|---|---|---|
| Filter 1 / Filter 2 tabs | two tabs | ONE tab `Filter`, the four-state selector in its top bar (`UsedSelector` of four fixed cells with a dot: F1 / F2 on, NOTE / VELO routed) | `overlay_layout.json`, `mpc-ui-pages-overlay.json`, `filter_display.h` `view` |
| CURVES list F1 \| F2 \| F1+F2 | one list that was both the view and the edited filter | F1 and F2 are states; F1+F2 is a view-only toggle (`both`): the display draws both curves and the resultant, the controls edit the SELECTED filter, nothing is written; hidden in NOTE / VELO | `filter_ui.h` `isDual` |
| `modelList` arrow button (D10) | 56 px button next to the stepper | removed (the centre of the MODEL stepper opens the list) | `filter_display.h` |
| OUTPUT button + popup (U5, Q3) | the popup held DEST, BUS 1, BUS 2, LEVEL, PAN | removed; LEVEL, PAN, DEST, BUS 1, BUS 2 are read-only indicators in the OUT block (`FilterOutReadout`, "SET ON MIXER"); the Mixer strips F1 / F2 are their home | `filter_display.h`, `filter_ui.h` `pullReadout` |
| LEVEL, PAN knobs of the OUT block | knobs, Q members | indicators | same |
| BLEND, ROUTING | OUT block | unchanged (no Mixer home) | - |
| COMB (U4) | dimmed for 26 of 27 models, always a Q slot | hidden unless the Comb model | `filter_display.h` `setCombMode` |
| VAR, CLEAN | dimmed with the Formant model (CLEAN also with Comb) | hidden | `applyVisibility` |
| FORMANT RESONANCE | in the FORMANT block, in no Q group | in the TRACK block, in the place of VAR (the free Q slot), a Q member; label `F RESO` (a 9 character name does not fit a modulated 100 px cell, lint rule d) | `filter_display.h` |
| MODEL, STYLE | no Q member | Q members of OUT | `buildGroups` |
| NOTE / VELO | none | `ScalarCurveUi` per source embedded under the top bar (1280 x 556): graph with points, bend, INIT / UNDO / DELETE, LEGATO with the GLIDE chip, USED BY with TARGETS (the `ModTargetsPage` of source 23 / 24 with ADD TARGET), the live x marker of the last note / velocity | `filter_ui.h` |

Pictures. F1 [before](../../screens/filter_old.jpg) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter1.png); F2 [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/before/Init_filter2.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter2.png); F1+F2 toggle (parallel) [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_dual_parallel.png); Formant [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/before/Init_filter1_model_formant.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter1_model_formant.png); Comb [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/before/Init_filter1_model_comb.png) / [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter1_model_comb.png); the OUTPUT popup [before](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/before/Init_filter1_output.png) (gone); NOTE identity [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_note_identity.png), custom with LEGATO, glide chip and USED BY [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_note_custom.png), targets [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_note_targets.png); VELO identity [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_velo_identity.png), custom [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_velo_custom.png), targets [after](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/filter-bold-cut/after/Init_filter_velo_targets.png).

## Decisions

- F1 / F2 / NOTE / VELO is the chunk-only `mpcTabView.filter` (step 1): a tap writes it through `IInstanceView::setTabView`, a project reload restores it, the page follows it on every update. F1+F2 is NOT saved (a view).
- In the dual view a finger on the handle of the other filter on the display selects that filter (the selector follows), as before.
- Formant: the plan's UNVERIFIED point (is the main RESO the formant resonance?) is answered by the engine table: `filterN.formant.resonance` is a separate parameter (`own_support.cpp`), so it stays a control; no second Q group is needed because VAR and COMB do not exist in that model: the formant RESONANCE takes the free third TRACK slot and is a Q member there.
- The OUT Q group is frameless (`QGroup::setFrameless`): MODEL and STYLE stand in the top bar, BLEND and ROUTING in the block; the chips and the Q colour mark the quad. The top bar starts one chip lane (16 px) below the top of the tab so the chips of MODEL and STYLE stay inside the tab.
- The NOTE axis is named by the ONE key-name function `keyName` (`note_mask.h`, the MPC convention: key 60 = C3, key 0 = C-2): C-2, C1, C3, C5, G8; the test pins key 60 = C3.
- The live x marker: `IInstanceView::lastPlayed` (one lock-free atomic per source, two relaxed stores in `effProcessEvents`), drawn by `drawLive` only (the cached picture does not change).
- The mixer chips (tap to navigate) and the disabled-block rule are NOT part of this step.

## Native standalone controls

The release skin owns the Filter view selector, power and parameter controls, and the NOTE and VELO fields and buttons. The overlay draws the response, curve canvas, live marker, read-only indicators and contextual editors. The native MODEL and STYLE controls open the MPC MenuOverlay with the same named domains as the FX editor. The selected model supplies the STYLE list. A MODEL choice uses the same style-clamping edit as the overlay fallback.

The Filter page publishes its current view and target table to transient UI proxies. Turning a filter off disarms its edit cells and keeps power and all four view choices active. A preset, instance, filter, or model change invalidates queued control input. The normal VST parameters retain their existing automation path. The background sentinel and the two page markers sit outside the touchable skin controls.

`filter_ui_host` verifies the binding, rendered apertures and gesture transitions. The skin regression checks all 27 model gates in both filter states, NOTE/VELO and the offline marker geometry. Hardware composition and native gestures are part of the final grouped MPC pass.

## Current split implementation (2026-11-24)

This document above preserves the 2026-11-21 bold-cut design history. The current root is the independent 60/40 Filter + NOTE/VELO split, documented in [filter.md](../filter.md). Its expand actions retain the original full Filter and scalar editor functions.

- Current offline composition: [filter.jpg](../../screens/filter.jpg).
- Fixed historical reference: [filter_old.jpg](../../screens/filter_old.jpg).
- Production TUI/component assets plus C++ fixture: `scripts/render_filter_screen_preview.py` and `shell-mpc/overlay/tests/filter_render_fixture.cpp`.
- Variants and before/after sheets are temporary external outputs (`--out`, `--compare`, `--state-json`), not additional project screenshots.

The rejected GenericLegacy/PIL dial reconstruction is removed. The renderer uses the actual generated FilmStrip/ActiveText/Selector/Radio/Button/NameValue assets and fails for missing or unsupported dial art. The offline host interpreter does not prove pixel-identical MPC rasterisation; real device composition and gestures remain a hardware acceptance check.
