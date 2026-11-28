# Filter screens: Filter 1 / Filter 2 (`filterScreen`, `filterScreenTop`) and the Note / Velo curve screens

## Filter 1 and Filter 2

**Tabs:** Filter 1 (`filter1`), Filter 2 (`filter2`); the view index is the filter number. Each tab holds `Screen` (`filterScreen`), `Screen Top` (`filterScreenTop`), the markers, the instance picture, the caption `Native Caption filter<n> 1` (1084 412 184 20) and the sentinel. **Picture:** `../../screens/filter.jpg`.

**Shows.** `filterScreen`: the modulation editor and targets title (shared), `filterNativeEditorSurface` (0 0 1280 628: power, MODEL, STYLE, OUT, the dials CUTOFF, RESO, DRIVE, MIX, KEYTRACK, the ENV 1 button, VAR, COMB, BLEND, the FORMANT dials X, Y, TRANSPOSE, F RESO, SPREAD, CLEAN, each with an `off` twin per filter), the filter chain selector `filterNativeChain` (Parallel, F1 to F2, F2 to F1), the F1 + F2 dual-view button and the PZ toolbars and the `Edit PZ Curve` button. `filterScreenTop`: the title (`Filter Native Title` / `Off`), the four block labels (MAIN, TRACK, OUT, FORMANT; each with an active twin), the PZ title and hint, and the list pages (`oscNativeList`). The response display is drawn by the plugin ([../filter-display.md](../filter-display.md)).

**Variants.** The filter model decides which controls apply: the Formant model shows the FORMANT dials, the Comb model the COMB dial; the VAR dial is the formant RESONANCE under the Formant model. The F1 + F2 view shows both filters' curves; the PZ curve editor applies to the selected filter.

**Data (blocks of `native_ui_params.h`).** `kFilterBase` (the view), `kFilterValueBase` (28 value slots, `FilterSlot` in `shell-mpc/native_filter_controls.h`), `kFilterIndicatorBase` (5 indicators), `kFilterChainMenu` (the routing menu), `kPzAction` / `kPzSource` / `kPzAvailability` (PZ editor), `kModTargets`, `kNativeText`. Handles are the same for both tabs; the bound filter is the view index.

**Q groups.** MAIN [CUTOFF, RESO, DRIVE, MIX], TRACK [KEYTRACK, ENV 1, VAR, COMB], OUT [BLEND, ROUTING, MODEL, STYLE], FORMANT [X, Y, TRANSPOSE, SPREAD].

## Filter header

The Filter 1 / 2 header is a catalogue example only (`filter-screen`); why it is not in the skin: [../header.md](../header.md), last section.

## Note and Velo

**Tabs:** Note (`filternote`), Velo (`filtervelo`): one tab body each, not instances of `filterScreen`; the view index 3 / 4 is the curve source. **Picture:** none.

**Shows.** The title and the INIT / UNDO / DEL POINT row are the Header component (instances `Note Header` / `Velo Header`, `curveHeader*` in `defs_mod.json`; [../header.md](../header.md)). The modulation editor and targets title (shared), `filterNativeGlide` (874 82 396 36; read-only GLIDE chip), `filterNativeCurveSurface` (`defs_mod.json`: the curve editor controls), the block labels (`Curve Native Block 0/1`), the "USED BY" list of up to four destinations with dots, names and amounts and a "more" and an empty line (`Curve Native Used *`), and the list pages (`oscNativeList`). The curve canvas is drawn by the plugin (`scalar-curve-editor`, [../scalar-curve-editor.md](../scalar-curve-editor.md)).

**Q groups.** EDIT [POINT, X, Y, CURVE]; LEGATO AND GRID [LEGATO, GRID X, GRID Y, SNAP].

## C++ owners

`shell-mpc/overlay/filter_ui.h` (rows of both filters, Q banks, native bridge, PZ and curve editors), `filter_geometry.h`, `scalar_curve_ui.h`, `mod_rows_page.h`, `native_mod_targets_ui.h`; `shell-mpc/native_filter_controls.h`, `shell-mpc/filter_curve_edits.h`; `shell-mpc/ui/filter_display.h`, `pz_curve_editor.h`, `scalar_curve_editor.h`.
