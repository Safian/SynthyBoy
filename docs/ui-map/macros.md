# Macros page

## Purpose

The MACROS page shows the eight macro knobs of the synthesizer as cards in two rows of four, each with its value display and chips showing what modulation targets the macro drives. Under the cards is a live spectrum display of the output. The page supports renaming macros, viewing and editing the modulation targets of each macro, and adding new targets.

## Layout

The page occupies the full tab region (1280 × 628 px): `shell-mpc/overlay/overlay_layout.json:417`.

The `MacroView` (`shell-mpc/ui/mod_macros.h:237`) draws:
- A title bar at the top with "MACROS" text and a `LedList` for BANK 1 / BANK 2 selection (`shell-mpc/ui/mod_macros.h:242,250-251`).
- Eight `MacroCard` components in a 2 × 4 grid (`shell-mpc/ui/mod_macros.h:244,322-326`). Each card contains a `Knob` (`shell-mpc/ui/mod_macros.h:85`) and up to four target chips (`shell-mpc/ui/mod_macros.h:87-90`).
- A `SpectrumStrip` at the bottom (`shell-mpc/ui/mod_macros.h:245,320`) showing 256 points of the output spectrum on a log frequency axis.

The `MacroUi` class (`shell-mpc/overlay/macro_ui.h:42`) manages the page state, the three sub-pages (targets, list, keyboard), and the Q-Link publishing.

## Controls

| Label on screen | Parameter key | Gesture | Effect | Source |
|---|---|---|---|---|
| BANK 1: 1-4 / BANK 2: 5-8 | - | Tap on a bank LED | Switches the Q-Link group to the selected bank of four macros | `shell-mpc/overlay/macro_ui.h:469`, `shell-mpc/ui/mod_macros.h:264-269` |
| Macro 1 knob | `macro.1` | Drag the dial | Writes normalised value to `macro.1` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 2 knob | `macro.2` | Drag the dial | Writes normalised value to `macro.2` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 3 knob | `macro.3` | Drag the dial | Writes normalised value to `macro.3` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 4 knob | `macro.4` | Drag the dial | Writes normalised value to `macro.4` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 5 knob | `macro.5` | Drag the dial | Writes normalised value to `macro.5` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 6 knob | `macro.6` | Drag the dial | Writes normalised value to `macro.6` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 7 knob | `macro.7` | Drag the dial | Writes normalised value to `macro.7` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro 8 knob | `macro.8` | Drag the dial | Writes normalised value to `macro.8` | `shell-mpc/overlay/macro_ui.h:492-497` |
| Macro name (under dial) | - | Tap | Opens the keyboard page to rename the macro | `shell-mpc/ui/mod_macros.h:138-143,161-164` |
| Spectrum display | - | None (read-only) | Shows the live output spectrum; no touch handling | `shell-mpc/ui/mod_macros.h:22-76` |

## Opens

- **Targets page** (detail page, `kPageTargets = 50`): Tapping on a macro card's chips area (`shell-mpc/overlay/macro_ui.h:449-454`) opens the modulation targets page for that macro. The targets page (`shell-mpc/ui/mod_targets.h:15-174`) shows:
  - BACK button (`shell-mpc/ui/mod_targets.h:32`): returns to the macros page.
  - Page stepper (`shell-mpc/ui/mod_targets.h:33`): when more than 6 targets exist, selects which page of 6 rows to show.
  - ADD TARGET button (`shell-mpc/ui/mod_targets.h:34`): opens the destination list popup.
  - For each row (up to 6 per page):
    - Target name label: text showing the destination parameter name.
    - Depth HBar (`shell-mpc/ui/base.h:963`): bipolar slider from -100% to +100%, writes `modN.amount` (normalised).
    - Polarity ToggleIcon (`shell-mpc/ui/base.h:800`): shows "BI" (bidirectional) or "UNI" (unipolar), writes `modN.bipolar`.
    - Bypass ToggleIcon (`shell-mpc/ui/base.h:800`): shows "ON" (active) or "OFF" (bypassed), writes `modN.bypass` (inverted logic).
    - Remove button "X" (`shell-mpc/ui/base.h:903`): clears the slot by writing 0 to `modN.source`, `modN.destination`, and `modN.amount`.
  - The parameter keys written are `modN.amount`, `modN.bipolar`, `modN.bypass`, and `modN.source` / `modN.destination` (where N is the slot index, 0-63).

- **Destination list popup** (`kPageList = 51`): Tapping ADD TARGET on the targets page opens a `ListPopup` (`shell-mpc/ui/list.h:18-253`) showing groups of modulation destinations. Tapping a group shows the parameters within it; tapping a parameter adds it as a new target at 50% depth with default polarity. The list has:
  - Header bar with back arrow (when inside a group): returns to the group list.
  - List rows: tap to select a group or parameter.
  - Backdrop: tap to close the popup.

- **Keyboard page** (`kPageKeyboard = 52`): Tapping a macro name button opens a full-screen `TextEntry` (`shell-mpc/ui/text_entry.h:23-334`) for renaming the macro. The page shows:
  - Title bar: "NAME OF MACRO N".
  - Text field: shows the current name with a caret at the end.
  - Keyboard keys (QWERTY layout, 5 rows):
    - Letter keys (a-z): type the letter (shifted on first key if text is empty).
    - Shift key: toggles shift mode (once / caps / off).
    - ?123 key: switches to the symbols layer.
    - ABC key: switches back to the letters layer.
    - SPACE: inserts a space character.
    - BACKSPACE (left arrow icon): deletes the last character.
    - CLEAR: erases the entire text.
    - CANCEL: closes the page without saving.
    - OK: closes the page and stores the trimmed name via `IInstanceView::setMacroName`.

## Q-Link groups

- **MACROS 1-4** (bank 0): Q1-Q4 map to `macro.1` through `macro.4` (`shell-mpc/overlay/macro_ui.h:532-533`).
- **MACROS 5-8** (bank 1): Q1-Q4 map to `macro.5` through `macro.8` (`shell-mpc/overlay/macro_ui.h:532-533`).
- **MACRO N TARGETS** (targets page open): Q1-Q4 map to the depth, polarity, and bypass of the active row (`shell-mpc/overlay/macro_ui.h:519-523`). The fourth slot is unused (null).

## Live parts

- **Spectrum display**: updated every ~16 ms from the `SpectrumRing` provided by the instance view (`shell-mpc/overlay/macro_ui.h:300-318`). The `SpectrumCurve` (`shell-mpc/overlay/spectrum_curve.h`) accumulates 256 log-frequency bins.
- **Macro card chips**: refreshed every 100 ms (or on modulation changes) by scanning the modulation matrix for slots whose source is the macro (`shell-mpc/overlay/macro_ui.h:265-288`).
- **Targets page rows**: reloaded when the page arrives or when modulation changes (`shell-mpc/overlay/macro_ui.h:323-360`).

## Duplicates

- `macro.1` through `macro.8`: these parameters are only used on the macros page. No duplicates found.
- The modulation parameters (`modN.source`, `modN.destination`, `modN.amount`, `modN.bipolar`, `modN.bypass`) are shared with the envelope targets page, LFO targets page, and the modulation input pages of other pages (filter, osc). The targets page component (`shell-mpc/ui/mod_targets.h`) is shared across all of them.

## Questionable

- **Macro name button opens a full keyboard page**: Tapping the name under a macro dial opens a full-screen on-screen keyboard just to edit a short label. This is deep (one page down from the main view) and the keyboard takes the entire tab area, making it hard to see what you are renaming. (`shell-mpc/ui/mod_macros.h:138-143`, `shell-mpc/overlay/macro_ui.h:432-436`)
- **"+N" chip count with no way to view more**: When a macro has more than 4 targets, a "+N" label is shown on the card (`shell-mpc/ui/mod_macros.h:219-224`) but there is no control to view or interact with those extra targets — you must open the targets page to see them.
- **Spectrum display is read-only**: The spectrum strip draws the output but has no touch targets, no controls, and no way to interact with it. It appears to be a visual remnant from the host skin's 32 meters (`shell-mpc/ui/mod_macros.h:21-22`).
- **Bypass toggle label is inverted**: The toggle shows "ON" when the slot is active and "OFF" when bypassed, but the parameter written is `modN.bypass` (0 = active, 1 = bypassed). The label says the opposite of the parameter value, which is confusing. (`shell-mpc/ui/mod_targets.h:55-56`, `shell-mpc/overlay/macro_ui.h:509`)
