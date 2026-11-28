# Mod screen

**Tab:** Mod (`mod`, function key Mod): one tab body. **Picture:** `../../screens/mod.jpg`.

## What it shows

- **Matrix page**: the header (the Header component, instance `Mod Header`, `modHeader*` in `defs_mod.json`: title, the mode switch ALL / BY SOURCE / BY DEST, the source and target group dropdown opening `nativeListModal`, the count, ADD; [../header.md](../header.md), [AGENTS.md](../../../AGENTS.md#lists-owner-2026-11-28)) and seven rows (`modNativeRowNormal`, or `modNativeRowScroll` with the scrollbar) with their column headers.
- **Slot page** (one slot): BACK, DELETE, SOURCE, TARGET, AUX SOURCE lists, AMOUNT bar, POLARITY and ROUTE toggles, SCALE, INVERT, AUX CURVE, SHAPE CURVE, STEREO, RISE, FALL, DELAY / BEATS, SYNC (`modNative_*` dials, toggles, bars).
- **Source board** (add a modulation): chips for ENV 1 .. 3, LFO 1 .. 4, MACRO 1 .. 4, WHEEL, VELO, AT, KEY, PITCH, FIXED and ALL SOURCES (the rest through the inline picker in `skin/TUI.json`: seven list rows `modNative_list_1200x56`, BACK, GROUPS, PREVIOUS, NEXT, title; migration to the shared two-level `oscNativeList` pattern is tracked in [TODO.md](../../../TODO.md)).
- The modulation editor and targets keyline (shared), the texts (count, detail title, empty lines, block labels).

## Definitions

`skin/defs_mod.json` (31: `modNative_*` controls, `modNativeRowNormal`, `modNativeRowScroll`, `modHeader*`, `filterNativeGlide`, `filterNativeNv_151x60`, `filterNativeCurveSurface`), and the picker rows and the modulation editor in `defs_common.json`. The view is gated by `kModView` (`mod::View`: Hidden, Matrix, MatrixScroll, DetailDelay, DetailBeats).

## Page-loading / suspend

On `leftTab` (`ModUi::leftTab` / `suspendNative`), `ModUi` disarms touch/wheel/Q input (`suspended_ = true`), invalidates in-flight gestures (`dropGesture`), clears `setUiFullOpen(false)`, hides modal `nativeTargets_` (`TargetsTitle`/`Count`/`Empty`), and clears `nativeModBoard` only when the shared modal picker (`kPageList`, `ListGate`) was open—keeping the last published Matrix rows, navigation, detail, quick-add board, and `Mod*` `nativeText` lines readable for the host without clearing other screens' `nativeText` lines. On return (`ModUi::update`), pending native writes are drained (`drainNativePending`) before re-arming (`suspended_ = false`) and republishing. Remaining `synthyboy_mpc.cpp` items (`enteringLayout` paint-window protection and `NativeCellBank::renewInputRevisions()` / cold pre-population) are tracked in [TODO.md](../../../TODO.md).

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kModView`, `kModRows`, `kModFields`, `kModNavigation`, `kModDetailControl` (`moddetail`), `kModBoardControl` (`modboard`), `kModTargets`, `kNativeText`. The slots are the engine parameters `modNN.*` (64 slots).

## Q groups

Matrix: four groups, [ROW, SOURCE, TARGET, AMOUNT] of the selected slot, [UNI / BI, ON / OFF, AUX SOURCE, AUX SCALE], and the two next visible rows as [SOURCE, TARGET, AMOUNT, UNI / BI] (`q_publish_mod.h`). Slot page: AUXILIARY SOURCE, SHAPE, TIME. Targets list: one group per row.

## C++ owners

`shell-mpc/overlay/mod_ui.h`, `mod_model.h`, `mod_rows_page.h`, `native_mod_targets_ui.h`, `q_publish_mod.h`; `shell-mpc/native_mod_controls.h`, `native_mod_targets.h`. Retired unreferenced C++ symbols (`b5183f24`): Matrix/Detail aperture helpers, write-only slot fields, and unused `ModUi` accessors/constants.
