# Macros screen

**Tab:** Macros (`macros`, function key Play, second sub-tab): one tab body. **Picture:** `../../screens/macros.jpg`.

## What it shows

- The studio spectrum frame across the upper area (`macroSpectrumFrame`, 8 72 1264 290; the spectrum is drawn by the plugin, `spectrum_curve.h`) and the eight macro cards in one bottom row (`macroCard`, 152 x 238: dial, value, target chips) with the card name and its underline (`macroNameLine`) under each.
- The targets page of a macro (title, count, empty hint, and the modulation editor `nativeModulationEditor` with its keyline), the inline add-target picker in `skin/TUI.json` (seven list rows `modNative_list_1200x56`, BACK, GROUPS, PREVIOUS, NEXT, title; migration to the shared two-level `oscNativeList` pattern is tracked in [TODO.md](../../../TODO.md); [AGENTS.md](../../../AGENTS.md#lists-owner-2026-11-28)) and the native keyboard (`native keyboard overlay`) for renaming a macro. The sub pages are gated by the macro sub-page handle.

## Header

The title `MACROS` is the Header component (`Macro Header`, form `macroHeader`; [../header.md](../header.md)).

## Definitions

`skin/defs_macro.json` (9: `macroHeader` (the title, Header form), `macroCard`, `macroSubLevel`, `macroSpectrumFrame`, `macroNameLine`, `macroLineGrid`, `macroLineGridTop`, `macroLineDecade`, `macroLineRule`); the picker rows, the modulation editor and the keyline in `defs_common.json` and `defs_mod.json`.

## Data

Blocks of `shell-mpc/native_ui_params.h`: `kMacroText` (card names, title, count, empty hint; `nativetext::MacroLine`), `kMacroSubpage` (exactly 0 on the cards page, 1 while the targets, add-target list or keyboard is open), `kMacroTarget` (eight targets), `kModTargets`, `kTextEdit` (keyboard context, key, text; `native_text_entry.h`), `kTextGate`. The macro parameters are `macro.1` .. `macro.8`.

## Tab exit and page loading

On leaving the tab (`MacroUi::leftTab`, `shell-mpc/overlay/macro_ui.h:369-376`), `publishNativeMacroTargets(false)` disarms targets, modal lists and targets are hidden, spectrum is reset, and `setUiFullOpen(false)` ensures Frame 24 is never asserted on exit. Macro card values, names (`kMacroText`), and subpage state (`kMacroSubpage = 0`) stay valid in host parameter memory.

## Q groups

MACROS 1-4 and MACROS 5-8 (the shown bank is the active one); the targets list: one group per row.

## C++ owners

`shell-mpc/overlay/macro_ui.h`, `macro_names.h`, `spectrum_curve.h`, `mod_rows_page.h`, `native_mod_targets_ui.h`; `shell-mpc/ui/macro_card_contract.h`, `shell-mpc/native_text.h`, `native_text_entry.h`, `native_mod_targets.h`. Retired unreferenced C++ symbols (`eec28436`): command rows, picker/target geometry helpers, `writeCell`, and unused `macro_card_contract` constants.
