# UI Map: Mod Page
Refreshed 2026-11-23 against the matrix groups, native badges and detail-page slot order.

## Purpose
The "Mod" page (`shell-mpc/overlay/overlay_layout.json:749-785`, tab "Mod", kind "mod", view index 1) is the MPC overlay modulation matrix screen for SynthyBoy. It implements the interface for all 64 modulation slots (`synthyboy::kNumModSlots = 64`, `core/include/synthyboy/modulation.h:17,54-62`, `shell-mpc/overlay/mod_ui.h:1-14`).
- When no modulation slot is active (`totalActive_ == 0`), the page displays a quick-add source board (`ui::ModSourceBoard`, `shell-mpc/ui/mod_board.h:16-257`, `shell-mpc/ui/mod_matrix.h:198`) showing the engine modulation sources arranged in groups (the count 57 of the earlier text is unverified). Tapping any source opens the destination picker list and targets page for that source (`shell-mpc/overlay/mod_ui.h:329-339,529-533`).
- When one or more slots are active, the 1280 × 628 matrix displays up to seven routes at a time. Its scrollbar narrows the matrix from 1268 to 1224 pixels. The Amount, polarity and bypass controls follow the seven actual slots in the filtered and scrolled window.
- It provides filtering by all slots, by source group, or by destination module (`shell-mpc/ui/mod_matrix.h:221-227`, `shell-mpc/overlay/mod_ui.h:522-527`).
- Each slot row allows direct editing of source, destination, depth amount, polarity (unipolar/bipolar), and active state (bypass on/off), and includes a "MORE" button to open the full-tab slot detail page (`ui::ModSlotDetail`, `shell-mpc/ui/mod_detail.h:24-261`, `shell-mpc/overlay/mod_ui.h:230-235`).

The host owns the scalar controls in the active view. MORE opens the selected route and hides all matrix widgets. The route editor exposes its own scalars, with the delay control switching between milliseconds and beats. Filter navigation, scrolling, MORE, Back and Delete belong to the overlay. The Source, Target and Aux pickers use the overlay's grouped navigation. The SDK `MenuOverlay` takes one flat `replacementStrings` list (MPC SDK UI reference §§3.3 and 6.11), and the target domain has 1,249 choices in `docs/params.json`. Opening a grouped picker hides the native controls until it closes.

## Layout
The overlay tab occupies a 1280 x 628 canvas at screen offset `tabOriginY = 110` (`shell-mpc/overlay/overlay_layout.json:9,753-758`, `shell-mpc/overlay/mod_ui.h:65,80`):
- **Top Bar (`x = 0, y = 114, w = 1280, h = 56`, `shell-mpc/ui/mod_matrix.h:562-571`):**
  - Title text "MOD MATRIX" at `x = 14, y = 114, w = 250, h = 56` (`shell-mpc/ui/mod_matrix.h:587`).
  - Filter mode buttons: "ALL" (`x = 280, w = 80`), "BY SOURCE" (`x = 368, w = 140`), "BY DEST" (`x = 516, w = 120`) (`shell-mpc/ui/mod_matrix.h:564-566`). An accent rectangle is drawn around the active mode button (`shell-mpc/ui/mod_matrix.h:592-594`).
  - Active filter stepper (`x = 648, y = 114, w = 260, h = 56`): displays `sourceFilter` when in "BY SOURCE" mode, or `destFilter` when in "BY DEST" mode (`shell-mpc/ui/mod_matrix.h:567-570`).
  - Slot counter: text readout `"%d / %d SLOTS"` at `x = add.x - 190, y = 114, w = 180, h = 56` (`shell-mpc/ui/mod_matrix.h:589-590`).
  - Add button: "+ ADD" (`x = 1148, y = 114, w = 120, h = 56`, `shell-mpc/ui/mod_matrix.h:571`).
- **Column Header Strip (`y = 174, h = 22`: `kBarH = 64`, `kHeaderH = 22`, `shell-mpc/ui/mod_matrix.h:169`, drawn at `shell-mpc/ui/mod_matrix.h:596-607`):**
  - Faint divider line across the canvas width at `y = 174`.
  - Column labels aligned above row widgets: "SOURCE", "TARGET", "AMOUNT", "POLARITY", "STATE" (`shell-mpc/ui/mod_matrix.h:602-606`).
- **Main List Area (`x = 0, y = 196, w = 1280, h = 538`, `listRect`, `shell-mpc/ui/mod_matrix.h:382`):**
  - **Quick-Add Board (`board`, `shell-mpc/ui/mod_board.h:16-257`, `shell-mpc/ui/mod_matrix.h:573-574`):** Shown only when `totalActive_ == 0`. Features headline "TAP A SOURCE TO START A MODULATION" (`shell-mpc/ui/mod_board.h:95,157`), category caption column (`w = 120`, `shell-mpc/ui/mod_board.h:20,116-120`), and horizontal wrapping rows of source chips (`h = 56, w = 64..260`, `shell-mpc/ui/mod_board.h:20,173-177,233-248`).
  - **Slot Rows (`windowRows[0..7]`, `shell-mpc/ui/mod_matrix.h:63-159,576-580`):** Shown when slots are active. Displays up to 8 rows (`kWindowRows = 8`, `shell-mpc/ui/mod_matrix.h:168`). Each row has height 72 (`kRowH = 72`, `shell-mpc/ui/mod_matrix.h:66`), consisting of a 16 px lane (`kQLane = 16`, `shell-mpc/ui/mod_matrix.h:128`) for Q-Link indicator chips and a 56 px control bar (`kCtlH = 56`).
    - Row elements: row number selector (56x56), source MenuButton (176x56), arrow glyph, target MenuButton (244x56), amount HBar (at least 380x56), polarity toggle (72x56), bypass toggle (72x56), MORE button (at least 56x56) (`shell-mpc/ui/mod_matrix.h:128-146`).
  - **Vertical Scrollbar Zone (`x = 1224, y = 200, w = 56, h = 530`, `zoneRect`, `shell-mpc/ui/mod_matrix.h:511-514`):** Appears at the right edge when active slots exceed visible rows (`scrolls() == true`). Shows track and touch-draggable thumb (`w = 24`, `shell-mpc/ui/mod_matrix.h:171,516-523`).

## Controls
The controls shown on the main Mod Matrix page (top bar, matrix rows, quick-add board, and scrollbar):

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| ALL | - | Tap | Switches matrix filter to show all active slots | `shell-mpc/overlay/mod_ui.h:522` |
| BY SOURCE | - | Tap | Switches matrix filter to filter by source group; shows the native source-group menu | `shell-mpc/overlay/mod_ui.h:523` |
| BY DEST | - | Tap | Switches matrix filter to filter by destination module; shows the native destination-module menu | `shell-mpc/overlay/mod_ui.h:524` |
| ALL SOURCES / group name | - | Tap and select a menu entry | Filters routes by source group (All, Envelopes, LFOs, Macros, etc.) | `shell-mpc/overlay/mod_ui.h:525` |
| ALL MODULES / module name | - | Tap and select a menu entry | Filters routes by destination module (All, Osc A/B/C, Filters, Envelopes, etc.) | `shell-mpc/overlay/mod_ui.h:526` |
| + ADD | - | Tap | Finds first unused slot, populates defaults (ENV 1 to first destination at +50%), and opens slot detail page | `shell-mpc/overlay/mod_ui.h:566` |
| Source chip | - | Tap | On empty board, selects tapped source, opens targets page and destination picker popup | `shell-mpc/overlay/mod_ui.h:529` |
| 01 .. 64 (Row number) | - | Tap or turn Q1 | Selects the modulation slot row as active Q group and scrolls it into view | `shell-mpc/overlay/mod_ui.h:136` |
| SOURCE (Row source) | `mod01.source` | Tap | Opens grouped source picker list popup to select modulation source | `shell-mpc/overlay/mod_ui.h:636` |
| TARGET (Row target) | `mod01.destination` | Tap | Opens grouped destination picker list popup to select target parameter | `shell-mpc/overlay/mod_ui.h:638` |
| AMOUNT (Row depth) | `mod01.amount` | Drag horizontally or turn Q4 | Adjusts modulation depth from -100% to +100% | `shell-mpc/overlay/mod_ui.h:640` |
| UNI / BI (Row polarity) | `mod01.bipolar` | Tap | Toggles modulation polarity between unipolar (0) and bipolar (1) | `shell-mpc/overlay/mod_ui.h:642` |
| ON / OFF (Row state) | `mod01.bypass` | Tap | Toggles slot active state (ON = bypass 0, OFF = bypass 1) | `shell-mpc/overlay/mod_ui.h:644` |
| MORE | - | Tap | Opens full slot detail page (`ModSlotDetail`) for this modulation slot | `shell-mpc/overlay/mod_ui.h:646` |
| Matrix list area | - | Drag vertically or mouse wheel | Scrolls visible rows up or down | `shell-mpc/overlay/mod_ui.h:195` |
| Scrollbar thumb / track | - | Drag thumb or tap track | Scrolls through matrix rows by page or continuous drag | `shell-mpc/ui/mod_matrix.h:384-423` |

The host owns the three mode buttons, the active group menu, ADD and the row controls. Source, Target and MORE resolve the visible row to its current route, including after filtering or scrolling. Source and Target open the grouped overlay picker. The overlay draws the empty source board, row numbers, scroll bar and live modulation markers. The header remains available on the empty board and when a filter matches no routes.

*(Note: Parameter keys `mod01.source`, `mod01.destination`, `mod01.amount`, `mod01.bipolar`, `mod01.bypass` represent slot 0; slots 1 through 63 bind corresponding parameters `mod02.*` through `mod64.*` via `synthyboy::modSlotParams`, `core/include/synthyboy/modulation.h:17,54-62`).*

## Opens
The Mod page opens three distinct overlays managed by the `UiStack` (`shell-mpc/overlay/ui_stack.h:1-50`, `shell-mpc/overlay/mod_ui.h:47-48,106-110`):

### 1. Slot Detail Page (`ModSlotDetail`, `kPageDetail = 20`)
- **Opened by:** Tapping "MORE" button on any matrix row (`shell-mpc/overlay/mod_ui.h:646-647`) or tapping "+ ADD" on the top bar (`shell-mpc/overlay/mod_ui.h:575`).
- **Layout:** Replaces the matrix view over the full tab canvas (`1280 x 628`, `shell-mpc/ui/mod_detail.h:80,195-237`). Contains top bar (`y = 114, h = 56`), the slot's summary row (`y = 174, h = 72`), and three parameter blocks (`y = 252, h = 480`): AUXILIARY SOURCE (36% width), SHAPE (26% width), and TIME (38% width).
- **Controls:**

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Closes detail page and returns to modulation matrix | `shell-mpc/overlay/mod_ui.h:581` |
| DELETE | - | Tap twice | First tap prompts "CONFIRM?"; second tap clears slot parameters and returns to matrix | `shell-mpc/overlay/mod_ui.h:582` |
| Row Number | - | Display | Shows slot index number (e.g. "01") | `shell-mpc/ui/mod_detail.h:202` |
| Summary Source | `mod01.source` | Tap | Opens grouped source picker list popup for this slot | `shell-mpc/overlay/mod_ui.h:636` |
| Summary Target | `mod01.destination` | Tap | Opens grouped destination picker list popup for this slot | `shell-mpc/overlay/mod_ui.h:638` |
| Summary Amount | `mod01.amount` | Drag horizontally | Adjusts modulation depth (-100% to +100%) | `shell-mpc/overlay/mod_ui.h:640` |
| Summary Polarity | `mod01.bipolar` | Tap | Toggles unipolar (0) / bipolar (1) | `shell-mpc/overlay/mod_ui.h:642` |
| Summary State | `mod01.bypass` | Tap | Toggles active state (ON = bypass 0, OFF = bypass 1) | `shell-mpc/overlay/mod_ui.h:644` |
| SOURCE (Aux source) | `mod01.aux_source` | Tap or turn Q1 in AUX block | Opens grouped auxiliary source picker list popup | `shell-mpc/overlay/mod_ui.h:657` |
| SCALE (Aux scale) | `mod01.aux_output` | Drag vertically or turn Q2 in AUX block | Adjusts auxiliary source input scale (0% to 200%) | `shell-mpc/overlay/mod_ui.h:658` |
| INVERT (Aux invert) | `mod01.aux_inv` | Tap or turn Q3 in AUX block | Inverts auxiliary modulation source signal (0 = normal, 1 = inverted) | `shell-mpc/overlay/mod_ui.h:659` |
| CURVE (Aux curve) | `mod01.aux_power` | Drag vertically or turn Q4 in AUX block | Adjusts auxiliary curve response shape (-10 to +10) | `shell-mpc/overlay/mod_ui.h:660` |
| CURVE (Shape curve) | `mod01.power` | Drag vertically or turn Q1 in SHAPE block | Adjusts primary modulation transfer curve shape (-10 to +10) | `shell-mpc/overlay/mod_ui.h:661` |
| STEREO (Shape stereo) | `mod01.stereo` | Tap or turn Q2 in SHAPE block | Toggles stereo modulation inversion spread (0 = off, 1 = on) | `shell-mpc/overlay/mod_ui.h:662` |
| RISE (Time rise) | `mod01.rise` | Drag vertically or turn Q1 in TIME block | Sets rising modulation slew rate smoothing (0 to 1000 ms) | `shell-mpc/overlay/mod_ui.h:663` |
| FALL (Time fall) | `mod01.fall` | Drag vertically or turn Q2 in TIME block | Sets falling modulation slew rate smoothing (0 to 1000 ms) | `shell-mpc/overlay/mod_ui.h:664` |
| DELAY (Time delay) | `mod01.delay` | Drag vertically or turn Q3 in TIME block | Sets modulation delay time in milliseconds (0 to 1000 ms; shown when SYNC is off) | `shell-mpc/overlay/mod_ui.h:665` |
| BEATS (Time delay beats) | `mod01.delay_beats` | Drag vertically or turn Q3 in TIME block | Sets tempo-synced modulation delay in beats (shown when SYNC is on) | `shell-mpc/overlay/mod_ui.h:666` |
| SYNC (Time delay sync) | `mod01.delay_sync` | Tap or turn Q4 in TIME block | Toggles tempo sync for modulation delay (0 = free ms, 1 = synced beats) | `shell-mpc/overlay/mod_ui.h:667` |

### 2. Targets Page (`ModTargetsPage`, `kPageTargets = 22`)
- **Opened by:** Tapping any source chip on the empty source board (`shell-mpc/overlay/mod_ui.h:329-339,529-533`).
- **Layout:** Replaces matrix view over the full tab canvas (`1280 x 628`, `shell-mpc/ui/mod_targets.h:112,141-154`). Top bar contains "BACK", source title (e.g. "ENV 1"), target count readout, page stepper (visible when > 6 targets), and "ADD TARGET" button. Below are up to 6 target rows (`kPerPage = 6`, `shell-mpc/ui/mod_targets.h:18`).
- **Controls:**

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| BACK | - | Tap | Closes targets page and returns to modulation matrix | `shell-mpc/overlay/mod_ui.h:536` |
| 1 / N (Page stepper) | - | Tap left/right or turn knob | Steps through pages of targets (when targets > 6) | `shell-mpc/overlay/mod_ui.h:538` |
| ADD TARGET | - | Tap | Opens destination picker list popup to route another parameter from this source | `shell-mpc/overlay/mod_ui.h:537` |
| Target Depth | `mod01.amount` | Drag horizontally or turn Q1 | Adjusts modulation depth (-100% to +100%) for target row | `shell-mpc/overlay/mod_ui.h:550` |
| UNI / BI (Target polarity) | `mod01.bipolar` | Tap or turn Q2 | Toggles unipolar (0) / bipolar (1) for target row | `shell-mpc/overlay/mod_ui.h:552` |
| ON / OFF (Target state) | `mod01.bypass` | Tap or turn Q3 | Toggles active state (ON = bypass 0, OFF = bypass 1) for target row | `shell-mpc/overlay/mod_ui.h:554` |
| X (Remove target) | - | Tap | Clears this modulation slot in the engine | `shell-mpc/overlay/mod_ui.h:556` |

### 3. List Popup (`ListPopup`, `kPageList = 21`)
- **Opened by:** Tapping "SOURCE", "TARGET", or "MORE" -> "SOURCE" / "AUX SOURCE", or tapping "ADD TARGET" / board source chip (`shell-mpc/overlay/mod_ui.h:340,427-438`).
- **Layout:** Centered modal popup at `x = 380, y = 130, w = 520, h = 528` (`shell-mpc/overlay/mod_ui.h:84`) over a dark backdrop covering the entire tab (`shell-mpc/overlay/mod_ui.h:85-87`).
- **Controls:**

| Label on screen | Parameter key | Gesture | Effect | Source |
| --- | --- | --- | --- | --- |
| Backdrop area | - | Tap outside popup | Cancels selection and dismisses popup | `shell-mpc/overlay/mod_ui.h:591` |
| Header (group name) | - | Tap header | Navigates back from sub-group item list to category group list | `shell-mpc/overlay/mod_ui.h:592` |
| List items | - | Tap row or vertical drag | At group level: opens group entries. At item level: assigns parameter/source choice and closes popup | `shell-mpc/overlay/mod_ui.h:598` |

## Q-Link groups
The hardware Q-Link knob assignments follow the active top page on the stack (`ModUi::publishQGroup`, `shell-mpc/overlay/mod_ui.h`):
1. **Main Matrix View (`pageTop_ == 0`):**
   - When slots exist and row `sel` is selected: Q group name is `"SLOT %02d"` (`shell-mpc/overlay/mod_ui.h:782`):
     - **Q1:** Virtual row selector (`kVirtualRow = 0`, `QRouter::encode(kVirtualRow, QRouter::kVirtual)`, `shell-mpc/overlay/mod_ui.h:66`, `shell-mpc/overlay/q_publish_mod.h:36`). Turning Q1 steps through visible matrix rows (`shell-mpc/overlay/mod_ui.h:136-138`, `ModRowSelector::turn`, `shell-mpc/ui/mod_matrix.h:44-51`).
     - **Q2:** Slot source choice (sp.source, e.g. `mod01.source`, `shell-mpc/overlay/q_publish_mod.h:36`).
     - **Q3:** Slot destination parameter choice (sp.destination, e.g. `mod01.destination`, `shell-mpc/overlay/q_publish_mod.h:36`).
     - **Q4:** Slot modulation amount (sp.amount, e.g. `mod01.amount`, `shell-mpc/overlay/q_publish_mod.h:36`).
     - **Q-Link mapping extension (2026-11-21, `matrixGroupTargets`, `shell-mpc/overlay/q_publish_mod.h:24-50`, published by `ModUi::publishQGroup` as FOUR groups, `shell-mpc/overlay/mod_ui.h:783-785`; the matrix has no active group of its own, so the XL has them on Q 1 .. 16 and the Key 61 as banks 1 .. 4):** group 2 (Q5 .. Q8 / bank 2) = `modNN.bipolar` (UNI / BI), `modNN.bypass` (ON / OFF), `modNN.aux_source` (AUX SOURCE), `modNN.aux_output` (AUX SCALE) of the selected slot (the aux pair has no control on this page: it is the AUXILIARY SOURCE block of the slot page, so it has no chip); groups 3 and 4 (Q9 .. Q12, Q13 .. Q16 / banks 3, 4) = the next two visible rows as [SOURCE, TARGET, AMOUNT, UNI / BI]; a row that does not exist leaves its group empty. The overlay-owned matrix can show chips on the selected and following rows (`ModMatrixView::qmore`). The matrix uses the same Q-Link chip and target mapping when host-native scalar controls are visible. See `ModMatrixView::draw` in `shell-mpc/ui/mod_matrix.h`. UNI / BI and ON / OFF are toggles: relative stepping, one step per 6 clicks. Test: `qslots_host.cpp` `testModMatrix`.
   - When no slot is selected or matrix is empty: Q group name is `"MOD MATRIX"`, publishes 0 slots (`shell-mpc/overlay/mod_ui.h:789-790`).
2. **Slot Detail Page (`pageTop_ == kPageDetail`):**
   - Active block is selected by touch on any control within that block (`shell-mpc/ui/mod_detail.h:134-141`). Q group name is `"SLOT %02d <BLOCK_NAME>"` (`shell-mpc/overlay/mod_ui.h:757`):
     - **AUXILIARY SOURCE Block (`BlockAux = 0`):**
       - **Q1:** `mod01.aux_source` (Auxiliary modulation source) (`shell-mpc/overlay/mod_ui.h:763`).
       - **Q2:** `mod01.aux_output` (Auxiliary scale amount) (`shell-mpc/overlay/mod_ui.h:763`).
       - **Q3:** `mod01.aux_inv` (Auxiliary invert switch) (`shell-mpc/overlay/mod_ui.h:763`).
       - **Q4:** `mod01.aux_power` (Auxiliary curve shape) (`shell-mpc/overlay/mod_ui.h:763`).
     - **SHAPE Block (`BlockShape = 1`):**
       - **Q1:** `mod01.power` (Primary modulation transfer curve) (`shell-mpc/overlay/mod_ui.h:767`).
       - **Q2:** `mod01.stereo` (Stereo spread toggle) (`shell-mpc/overlay/mod_ui.h:767`).
       - **Q3 - Q4:** Unused (`-1`).
     - **TIME Block (`BlockTime = 2`):**
       - **Q1:** `mod01.rise` (Rise slew smoothing) (`shell-mpc/overlay/mod_ui.h:771`).
       - **Q2:** `mod01.fall` (Fall slew smoothing) (`shell-mpc/overlay/mod_ui.h:771`).
       - **Q3:** `mod01.delay` (when `mod01.delay_sync` is 0) or `mod01.delay_beats` (when `mod01.delay_sync` is 1) (`shell-mpc/overlay/mod_ui.h:771`).
       - **Q4:** `mod01.delay_sync` (Tempo sync switch) (`shell-mpc/overlay/mod_ui.h:771`).
   - The Q1–Q4 labels above are the positions within each block. On XL, AUXILIARY SOURCE is on Q1–Q4, SHAPE on Q5–Q8 and TIME on Q9–Q12. Q13–Q16 are empty. On Key 61 the active block is bank 1 and the other two follow in order, wrapping if needed.
3. **Targets Page (`pageTop_ == kPageTargets`):**
   - Q group name is `"<SOURCE_NAME> TARGETS"` (e.g. `"ENV 1 TARGETS"`, `shell-mpc/overlay/mod_ui.h:750`).
   - Active row controls mapped via `modRowTargets` (`shell-mpc/overlay/q_publish_mod.h:14-22`):
     - **Q1:** Target modulation amount sp.amount (e.g. `mod01.amount`).
     - **Q2:** Target polarity sp.bipolar (e.g. `mod01.bipolar`).
     - **Q3:** Target state sp.bypass (e.g. `mod01.bypass`).
     - **Q4:** Unused (`-1`).
   - Other rows mapped to hardware Q-Links 5..16 (`shell-mpc/overlay/q_publish_mod.h:26-30`).
4. **List Popup (`pageTop_ == kPageList`):**
   - Q group name is `"LIST"`, publishes 0 slots (`shell-mpc/overlay/mod_ui.h:743-744`). Knobs are disabled while modal list is open.

## Live parts
The page renders dynamic modulation deflection live on every frame:
- **Amount bar live marker (`amountBar.state.modValue`, `shell-mpc/ui/mod_matrix.h:116`):** On every update cycle, `ModUi::update` (`shell-mpc/overlay/mod_ui.h:112-126`) inspects incoming `synthyboy::LiveValues* live` (`shell-mpc/overlay/mod_ui.h:112,122-126`). For each slot with a valid source, it computes `amount * liveSource` and sets `liveVal` on the slot entry (`shell-mpc/ui/mod_matrix.h:185,349,551`). The `amountBar` draws a dynamic live deflection needle reflecting the real-time modulated signal position alongside the static amount fill bar (`shell-mpc/ui/mod_matrix.h:115-117`).
- **Slot detail live marker (detail_.summary, `shell-mpc/overlay/mod_ui.h:388-392`):** When the slot detail page is open, `pullDetail` updates detail_.summary with `liveSource * amount`, displaying the real-time deflection needle in the slot's summary row.
- **Dynamic macro name reloads (`macroNames_.load`, `shell-mpc/overlay/mod_ui.h:96-105`):** If user preset macro names change, the source board chips and pickers are flagged dirty and automatically refreshed.

## Duplicates
The slot parameters of the matrix are also edited, for the slots that have the page's own object as source or destination, on the modulation lists of the other pages (each page's ModulationsPage / ModTargetsPage rows: depth, polarity, on / off, remove; verified by the write lines below):
- `mod01.amount` (and `mod02.amount` .. `mod64.amount`): Macros targets rows (`shell-mpc/overlay/macro_ui.h:548`), Env targets and modulations rows (`shell-mpc/overlay/env_ui.h:939,956`), LFO rows (`shell-mpc/overlay/lfo_ui.h:2234`, depth `shell-mpc/overlay/lfo_ui.h:2216-2220`), Filter rows (`shell-mpc/overlay/filter_ui.h:388`), Osc modulations rows (`shell-mpc/overlay/osc_ui.h:1510`).
- `mod01.bipolar`: `shell-mpc/overlay/macro_ui.h:549`, `shell-mpc/overlay/env_ui.h:940,957`, `shell-mpc/overlay/lfo_ui.h:2235`, `shell-mpc/overlay/filter_ui.h:390`, `shell-mpc/overlay/osc_ui.h:1511`.
- `mod01.bypass`: `shell-mpc/overlay/macro_ui.h:550`, `shell-mpc/overlay/env_ui.h:941,958`, `shell-mpc/overlay/lfo_ui.h:2236`, `shell-mpc/overlay/filter_ui.h:392`, `shell-mpc/overlay/osc_ui.h:1512`.
- `mod01.source` and `mod01.destination` are written (new row) and cleared (remove) by the same pages: `shell-mpc/overlay/macro_ui.h:437-441,502`, `shell-mpc/overlay/env_ui.h:864,876`, `shell-mpc/overlay/lfo_ui.h:647-651,2130,2142`, `shell-mpc/overlay/filter_ui.h:395,607-611`, `shell-mpc/overlay/osc_ui.h:1452`.
- **Unique controls with no duplicate on any other page** (the slot detail page `ModSlotDetail`):
  - `mod01.aux_source`, `mod01.aux_output`, `mod01.aux_inv`, `mod01.aux_power` (Auxiliary modulation block, `shell-mpc/overlay/mod_ui.h:657-660`). Since the Q-Link mapping extension `aux_source` and `aux_output` are also Q members of matrix group 2 (AUX SRC, AUX SCALE) without a control on the matrix screen.
  - `mod01.power`, `mod01.stereo` (Modulation transfer curve and stereo spread, `shell-mpc/overlay/mod_ui.h:661-662`).
  - `mod01.rise`, `mod01.fall`, `mod01.delay`, `mod01.delay_beats`, `mod01.delay_sync` (Modulation timing, slew, and delay, `shell-mpc/overlay/mod_ui.h:663-667`).
  None of these 11 parameters per slot are accessible anywhere else in the entire MPC overlay skin (searched in the overlay headers: only `mod_ui.h` writes them; `ui_bind.h` and `scalar_curve_ui.h` only read the aux source).

## Questionable
The following controls and design patterns appear questionable, redundant, or confusing:
0. (status 2026-11-21: BIPOLAR and BYPASS are on Q5 / Q6 of the matrix page now, see the extension above; the finding below stays for the row stepping)
1. **Virtual row stepping on hardware Q1 knob (`shell-mpc/overlay/mod_ui.h:786`, `shell-mpc/ui/mod_matrix.h:44-51,297-312`):**
   In the main matrix view, Q1 is bound to a virtual target that steps through slot rows. On a standard 4-knob MPC layout (MPC One / Live / Key 61), dedicating Q1 to scrolling/selecting rows leaves only Q2 (source), Q3 (destination), and Q4 (amount). Polarity (`mod01.bipolar`) and Bypass (`mod01.bypass`) cannot be turned from the hardware knobs on the matrix screen. Furthermore, tapping any row on the touch screen selects it directly (`shell-mpc/ui/mod_matrix.h:393-395`), making Q1 row-stepping largely redundant and wasting a valuable hardware knob.
2. **One-way disappearance of the Quick-Add Board (`shell-mpc/ui/mod_matrix.h:198,573-574`):**
   The quick-add source board (`ui::ModSourceBoard`) is shown only when `totalActive_ == 0`. As soon as a single modulation slot exists anywhere in the synth, the board is completely hidden and cannot be accessed. Users who want to browse modulation sources visually by group must delete all 64 slots or open the popup picker from a button. Adding a view toggle button to view the source board even when slots exist would improve discoverability.
3. **Implicit default assignment on "+ ADD" (`shell-mpc/overlay/mod_ui.h:566-578`):**
   Tapping "+ ADD" immediately writes hardcoded defaults into the engine: `modNN.source = 1.0` (ENV 1), `modNN.destination = 1.0` (first destination parameter in table), `amount = 0.75` (+50% normalized), and `bypass = 0.0`. It immediately creates an active modulation routing to an arbitrary parameter before the user has selected what they want to modulate. In contrast, tapping a chip on the source board opens the destination picker first (`shell-mpc/overlay/mod_ui.h:329-339`), which is much safer and more intuitive.
4. **Duplicate summary row on Slot Detail page (`shell-mpc/ui/mod_detail.h:34,202`, `shell-mpc/overlay/mod_ui.h:384-393`):**
   The slot detail page duplicates the entire matrix row (`summary`) at the top of the detail screen (source button, destination button, amount bar, polarity toggle, bypass toggle). While convenient for seeing slot context, the amount, polarity, and bypass toggles are already accessible on the previous screen, consuming 72 px of vertical space that compresses the auxiliary, shape, and time parameter blocks below.
5. **No explicit Cancel on Slot Detail delete (`shell-mpc/ui/mod_detail.h:148-165`):**
   Tapping "DELETE" changes the button text to "CONFIRM?". If the user decides not to delete, there is no "CANCEL" button; they must tap elsewhere on the screen to reset the confirmation state.
