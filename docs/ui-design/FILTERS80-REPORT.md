# FILTERS80 - WP-03: the remaining 43 Serum filter types and the PZ SVF curve editor

Branch `agy/filters80`. Started by a Gemini worker (4 commits: models 22..26 + cap bit, DSP wiring, UI menu / labels / curve editor, a docs regeneration), reviewed, corrected and finished afterwards. No new keys; spec in `13-new-keys.md` 4.24, assumptions A299 .. A308, morning questions 44 .. 47.

## What was built

- **Models 22 .. 26** appended to `kFilterModels` (`Normal`, `Multi Morph`, `Flanges`, `Misc`, `New`; 12 / 4 / 9 / 12 / 6 styles), shared by `filter1.model`, `filter2.model`, `fxfilter.model` (`core/src/param_table.cpp`). `kCapFilterModels2` = bit 51 (`engine_caps.h`), a model value >= 22 needs it (`engine_caps.cpp`), `vital` reports it, `own` does not.
- **One mapping table** (model, style) -> `FilterType` in `core/filters-serum/src/filter_types.cpp` (`serumFilterTypeOf`, `serumModelStyleCount`); `SerumFilterBank::filterTypeOf` (`core/engine-vital/upstream/src/synthesis/modules/serum_filter_bank.cpp`) and the UI use it. Before, the engine had a private copy in the fork.
- **PZ SVF curve in the engine** is now chosen by the resolved type (PZ SVF), not by `model == kLastModel` (which would have broken the PZ curve the moment models 22+ existed).
- **UI**: hierarchical type menu (`FilterPanels.cpp` `showModelMenu`) and touch picker sheet (`FilterViews.h` `FilterTypePickerSheet`) in the manual's categories; Var label per type and style (`FilterModels.h`); the model button shows the type name; the response view of models 22 .. 26 is measured from the DSP (`FilterMeasure.h`); PZ SVF editor with add (max 16) / delete / reset / index-stable drag, saved through `content.filter_curves`.
- **State**: the filter curves already had the additive `filter_curves` content section (state version 3, `core/src/state.cpp`, round trip in `core/tests/test_state_v3.cpp`); the editor writes it through `UiHost::setContent`, so it is saved, loaded and validated (<= 16 points, y within +-60).

## What the review found and fixed

| finding | fix |
|---|---|
| The response view drew an LP / BP / HP morph of the old closed form for every new type (a facade: High 12 and Low 12 looked alike) | measured response from the real DSP for models 22 .. 26 (`FilterMeasure.h`), test proves High vs Low vs Bandreject shapes, cutoff and resonance dependence |
| Blend labels "LP - BP - HP", "Comb / Flange Blend", ... for types that ignore Blend; menu picks overwrote the Blend knob with arbitrary 0 / 50 / 100 | plain "Blend" label, picks leave the knob alone (`defaultBlend < 0`) |
| PZ editor: unlimited points (the 17th made `content` invalid), dragged point index went stale after crossing a neighbour, double tap also cycled the display mode, no way to delete a point on touch | max 16, `setPoint` / `addPoint` return the new index, double tap on a point deletes, no mode switch on a PZ view |
| `getFilterTypeNameForModelAndStyle` showed a style name ("Style B (24 dB)") instead of "Ladder" on the button for classic models | classic models show their own name |
| Merge: Gemini's `ParamHandle::plain()` read the parameter directly (needed inside `onChange`, where the tree's raw atomic is not yet updated); develop added `storedNormalised` for pool slots | both: read the parameter, then apply `fromNormalised` when the key is stored normalised |
| Tests: "different from each other" only compared neighbouring styles; extremes only at random | all 43 + 37 pairs (two settings, equal only if equal at both), deterministic corner sweep of every type x 3 slots x cutoff / resonance / drive / var extremes, curve ignored by every non-PZ type, menu / picker reach all 80 types in all 3 slots, name and Var label compared with the core metadata |
| Five pairs of types rendered the same signal (Low 24 / German LP, Band 12 / Exp BPF, LNH / Exp MM / New Wsp) | own signal paths for German LP, New Wsp, Exp MM, Exp BPF (A306; morning question 45) |
| The `wip` commit | contained only the regenerated `docs/params.json` / `PARAMETERS.md` (6 lines); complete; the regeneration after the merge produced no further change |

## Tests

- `core/filters-serum`: `serum_filters_model_style_table_reaches_every_type_exactly_once`.
- `core/engine-vital/tests/test_wp6.cpp`: `wp6_capabilities_are_reported`, `all_43_remaining_types_are_stable_and_differ_from_each_other_and_from_analog`, `remaining_models_survive_extreme_settings_and_work_in_all_three_filter_slots`, `all_43_remaining_types_stay_finite_and_bounded_at_the_corners_of_every_control_in_all_three_slots`, `the_drawn_filter_curve_is_ignored_by_every_type_but_the_pz_svf`; the earlier `pz_svf_follows_the_drawn_curve_from_the_content` (the drawn curve moves the render by 36 dB).
- `core/tests`: `test_engine_caps.cpp` (cap per model value), `test_new_keys.cpp` (27 entries, append only), `golden_v2.h`.
- `shell-juce/ui/tests/test_ui_gaps.cpp`: `uig_pz_svf_ui_curve_editor_moves_rendered_response` (drag on the view -> content -> rendered noise level changes), `uig_pz_svf_curve_has_at_most_16_points_and_a_double_tap_on_a_point_deletes_it`, `uig_filter_menu_reaches_every_serum_type_and_every_type_shows_its_name_and_var_label_in_all_three_slots`, `uig_filter_type_picker_sheet_sets_model_and_style_of_each_of_the_80_types_in_each_slot`, `uig_filter_view_shows_the_measured_dsp_response_of_the_new_types`, `uig_filter_hierarchical_catalog_and_var_labels`.

## Not done / open

- ASan / UBSan: no sanitizer script exists in `scripts/`; `filter.cpp` was touched (four type paths, no buffer handling changed). Not run.
- The Q13 listening (final names) and the A/B of the four re-pathed types stay with the owner (questions 44, 45).
- `kick-pages.json` is unchanged on purpose: the keys `filterN.model` / `.style` are on the pages already (`filterN_main`), the page test needs only keys; choice entries are not in the file.
