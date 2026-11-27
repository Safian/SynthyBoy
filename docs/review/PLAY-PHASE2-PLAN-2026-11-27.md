# Play Phase 2 Plan (Geometry, Native Components & Overlay Apertures)

**Date:** 2026-11-27
**Baseline:** `develop` / `agy/play-phase2-plan` (`e2a53999`), host-only reference capture `/private/tmp/claude-501/now/k61-play-host.png`
**Scope:** Concrete, implementable geometry plan for the compact Play screen (`Play` tab, 1280 × 628 tab area at screen `y = 110..738`) across both the MPC host skin (`scripts/mpc-skin-extras/82_play.py`, `scripts/native_play/osc.py`, `scripts/native_play/env_lfo.py`, `components/native/*`, `components/sizes.json`) and the C++ overlay (`shell-mpc/ui/play_rows.h`, `screen.h`, `composite.h`, `env_screen.h`, `lfo_screen.h`, `lfo_parts.h`, `lfo_display.h`, `shell-mpc/overlay/play_view.h`, `play_env_lfo.h`, `osc_ui.h`, `env_ui.h`, `lfo_ui.h`).
**Legend:** Every factual statement cites `file:line`. Unverified device or layout assumptions are explicitly tagged `HYPOTHESIS`.

## Coordinate Conventions & Shared Rules

- **Tab vs screen coordinates:** Tab area is `1280 × 628` (`x = 0..1280`, `y_tab = 0..628`), mounted at `kTabY = 110` (`y_screen = y_tab + 110`, `110..738`) (`shell-mpc/overlay/overlay_layout.json:9`, `shell-mpc/overlay/play_view.h:44`). TUI.json, `components/sizes.json`, `play-osc-native-contract.json`, and `play-env-lfo-contract.json` use **tab coordinates**; C++ `Rect`s (`PlayView`, `OscUi`, `EnvUi`, `LfoUi`) use **screen coordinates**.
- **Detection strip (invariant):** Tab `y = 0..9` (screen `y = 110..119`) holds left marker `(33, 1, 6, 6)`, instance filmstrip `(45, 1, 6, 6)`, right marker `(1241, 1, 6, 6)` (`shell-mpc/overlay/overlay_layout.json:60-81`), and background sentinel `(900, 2, 5, 5)` (`scripts/mpc-skin-extras/99_backgrounds.py:62`, `shell-mpc/overlay/backgrounds_manifest.h:23`). No control may enter tab `y < 10`.
- **Panel horizontal spans (unchanged):**
  - Oscillator panel (Sub / A / B / C / Noise): inner `x = 14..422` (`W = 408`, `OscUi::kCompactX = 8, kCompactW = 420`, `kMargin = 6`, `shell-mpc/overlay/osc_ui.h:115,120`).
  - Envelope panel (Env 1..6): inner `x = 446..834` (`W = 388`, `EnvUi::kCompactX = 440, kCompactW = 400`, `kMargin = 6`, `shell-mpc/overlay/env_ui.h:79,138`).
  - LFO panel (LFO 1..10 / RND 1..4): inner `x = 864..1260` (`W = 396`, `LfoUi::kCompactX = 858, kCompactW = 408`, `kMargin = 6`, `shell-mpc/overlay/lfo_ui.h:114,125`).
  - Gutters: `422..446` (24 px, `PlayView::onTouch` split at `x = 434`) and `834..864` (30 px, split at `x = 850`) (`shell-mpc/overlay/play_view.h:350`).
- **Header rule (`docs/ui-components/header.md:7-14,24,52`, `components/sizes.json:855-860`):** `header.height = 56`, `header.padding = 8` on all four sides, `header.gap = 8`, content height `40` px (`height.intermediate`).
  - **Slot vs visual geometry:** Because MPC touch targets must be `>= 56 × 56` (`shell-mpc/ui/AGENTS.md:8`, `scripts/tests/test_native_play_osc.py:75`), every `40 × 40` header icon button (`POWER`, `EXPAND`) sits centered inside a `56 × 56` transparent slot (`8 px` pad on every side: `8 + 40 + 8 = 56`), and every `40 px` header dropdown sits centered vertically inside a `56 px` tall slot (`8 px` top/bottom pad). This satisfies both the visual `8 px` padding / `8 px` gap rule and the `>= 56 × 56` touch target rule with zero slot overlap.
- **Dial size mapping (`docs/review/DIAL-MEASUREMENT-2026-11-27.md:144,148,152`, `docs/ui-components/label.md:21-27`):**
  - Every `90 px` Play compact dial (`playSubPan`, `playSubLevel`, `playOscPan`, `compactNativeDial` for Env ADSR & Osc Level) maps to **`56 px`** (`label.dial.56`, `play_sub_level_56.png` / `play_sub_pan_56.png`).
  - Every `60 px` Play dial (`compactNativeRiseDial` for LFO Rise & Noise Filter, `playEnvLfoRate`, `compactNativeSmoothWithIndicator`/`Readout`) maps to **`56 px`** (`play_sub_level_56.png` / `play_sub_level_56_inactive.png`).
  - Placement: `'right'` (caption above value, left-aligned, right of the 56 px knob; SMOOTH uses the `on_off_label` component instance to the right of the 56 px knob with value label to its right).
  - Result: **1 distinct dial diameter (56 px)** across the entire Play page (was 60 px + 90 px).
- **Dropdown & Radio rules (`AGENTS.md`, `docs/ui-components/radio_grid.md:7-9,25-27`, `components/native/selector/selector.py:1473-1517`):**
  - Left/right arrow steppers (`playOscTableField` `<`/`>` buttons, legacy SHAPE arrows) are replaced by dropdowns (value centered, chevron right, ellipsis with `selector.dropdown.chevron_gap = 4` px).
  - Body dropdowns (`WAVE`, `TYPE`, `SOURCE`, `SOUND`, `SHAPE`, `DIVISION`, `STYLE`, `SYNC`) place their title **above left** (`20 px` title strip = `16 px` caption line + `4 px` `gap.field`) above a `40 px` dropdown box (`selector.dropdown.size.40`, `legacy` gradient look) -> total cell height **`60 px`** (`>= 56 px` touch height).
  - `HZ | BPM | KEY` uses **RadioGrid** `1 × 3`: three equal `64 × 40` buttons (`button.size.40`, `legacy` unselected / `active` selected) with `radio_grid.gap = 8` px (`64 + 8 + 64 + 8 + 64 = 208` px).
  - Lists keep factory `MenuOverlay` behaviour (tap row selects, tap header applies and closes).

---

## 1. Target Layout per Panel (Old Rect -> New Rect)

### 1.1 Shared Vertical Bands (All Three Panels)

| Band | Old Tab Rect (`y..y+h`, `h`) | New Tab Rect (`y..y+h`, `h`) | New Screen `y` | Delta / Notes |
|---|---|---|---|---|
| Marker & sentinel strip | `0..10` (`10`) | `0..10` (`10`) | `110..120` | Unchanged (`overlay_layout.json:60-81`, `99_backgrounds.py:62`) |
| Panel header band | `10..66` (`56`, selector only) | `10..66` (`56`, 8 px pad all round, visual `18..58`, 40 px) | `120..176` (visual `128..168`) | Holds `[POWER][dropdown][EXPAND]` (Osc) / `[dropdown][EXPAND]` (Env, LFO) |
| Header-to-graph gap | `66..74` (`8`) | `66..74` (`8`) | `176..184` | Unchanged (`play_rows.h:17` `kSelectorGap = 8`) |
| Display frame (Std Osc / Env / LFO) | `74..366` (`292`) | `74..402` (`328`) | `184..512` | **+36 px** frame height (`kControlsH` shrinks `232 -> 196`) |
| Internal display button band | `74..146` (`72`, `kBandH`) | **Removed (`0`)** | — | POWER / EXPAND moved to panel header (`+72 px` freed at top) |
| **Live plot area (Std Osc / Env / LFO)** | `156..346` (`190`) | `84..382` (`298`) | `194..492` | **+108 px (+56.8%)** plot height (`kPlotPadTop=10`, `kPlotPadBottom=20`) |
| Display frame / plot (LFO RND 1..4) | Frame `74..306` (`232`), plot `156..286` (`130`) | Frame `74..334` (`260`), plot `84..314` (`230`) | Frame `184..444`, plot `194..424` | **+100 px (+76.9%)** RND plot height (`kQuadRandomH: 304 -> 280`) |
| Display-to-controls Q-marker lane | `366..386` (`20`, `kQLane+4`) | `402..422` (`20`, `kQLane+4`) | `512..532` | Preserves Gen 1 unnumbered `Q` badge clearance above `controlsLine` (`qgroup.h:105`) |
| Controls block (Std Osc / Env / LFO) | `386..618` (`232`) | `422..618` (`196`) | `532..728` | Uniform `8 px` row & column gaps; bottom `618` unchanged |
| Bottom margin | `618..628` (`10`) | `618..628` (`10`) | `728..738` | Unchanged (`4 + kMargin 6`) |

---

### 1.2 Panel 1 — Oscillator (`x = 14..422`, `W = 408`)

> **Design note on Osc A/B/C/Noise `ROUTE`:** Sub has no route button (`play-osc-native-contract.json:5`, `screen.h:87`). Osc A/B/C and Noise have `routing` (`playOscRouteField`, currently `(282, 78, 64, 64)` inside the display band). **Primary plan (Option A):** place `ROUTE` in the A/B/C/Noise panel header band right of the dropdown (`[POWER 40][gap 8][dropdown 224][gap 8][ROUTE 64][gap 8][EXPAND 40]`, keeping `224 >= header.dropdown.min_width = 220` and **zero apertures** inside all oscillator graphs), using two source-gated `Play Osc Select` entries (Sub `w=296`, ABC/Noise `w=224`). **Fallback (Option C, `HYPOTHESIS` if single un-gated `Play Osc Select` is required by owner):** keep `Play Osc Select` at `296 px` across all 5 sources and place `ROUTE` at `(350, 82, 64, 56)` inside the top-right corner of the A/B/C/Noise graph (1 aperture `nativeHeader[2]`). Both options leave Sub identical.

| Control (Source / Definition) | Old Tab Rect `(x, y, w, h)` | Old Screen Rect | New Tab Slot `(x, y, w, h)` | New Tab Visual `(x, y, w, h)` | New Screen Slot | Component / Geometry Details |
|---|---|---|---|---|---|---|
| `POWER` (`Play Native OSC <src> power`) | `(18, 82, 64, 56)` (`play-osc-native-contract.json:3,12,21`) | `(18, 192, 64, 56)` | `(14, 10, 56, 56)` | `(22, 18, 40, 40)` | `(14, 120, 56, 56)` | `icon_button` (`power`, `height=40`, `legacy`) centered in `56×56` slot via `button.in_slot` (`8 px` pad all round) |
| `Play Osc Select` — Sub (`playOscSelector`) | `(14, 10, 408, 56)` (`sizes.json:1136`, `neutral=True`) | `(14, 120, 408, 56)` | `(70, 10, 296, 56)` | `(70, 18, 296, 40)` | `(70, 120, 296, 56)` | `selector` dropdown (`neutral=False`, `dropdown_look="legacy"`, `dropdown_size="selector.dropdown.size.40"`, value centered, chevron right); `8 px` gap from POWER & EXPAND |
| `Play Osc Select` — A/B/C/Noise (`playOscSelectorRouted`, Option A) | `(14, 10, 408, 56)` | `(14, 120, 408, 56)` | `(70, 10, 224, 56)` | `(70, 18, 224, 40)` | `(70, 120, 224, 56)` | Same 40 px legacy dropdown (`224 >= 220` `header.dropdown.min_width`); gated `RangedEnabling/1/4/5/Parameter 2676` |
| `ROUTE` — A/B/C/Noise (`playOscRouteField`, Option A) | `(282, 78, 64, 64)` (`play-osc-native-contract.json:14,23`) | `(282, 188, 64, 64)` | `(294, 10, 72, 56)` | `(302, 18, 64, 40)` | `(294, 120, 72, 56)` | `40 px` legacy dropdown/route face (`8 px` left pad inside slot `294..302`, `0` right pad because EXPAND slot owns `366..374`) |
| `EXPAND` (`Play Native OSC <src> expand`) | `(354, 82, 64, 56)` (`play-osc-native-contract.json:4,13,22`) | `(354, 192, 64, 56)` | `(366, 10, 56, 56)` | `(374, 18, 40, 40)` | `(366, 120, 56, 56)` | `icon_button` (`expand`, `height=40`, `legacy`) centered in `56×56` slot (`8 px` pad all round) |
| Osc Display Frame (overlay) | `(14, 74, 408, 292)` (`play_rows.h:22-27`) | `(14, 184, 408, 292)` | `(14, 74, 408, 328)` | `(14, 74, 408, 328)` | `(14, 184, 408, 328)` | Full unbroken `drawNativeGraphFrame`; 0 button apertures (Option A) |
| Osc Plot Area (overlay) | `(28, 156, 380, 190)` (`play_rows.h:52-56`) | `(28, 266, 380, 190)` | `(28, 84, 380, 298)` | `(28, 84, 380, 298)` | `(28, 194, 380, 298)` | `compactPlotRect(display)`: `+108 px` vertical room |
| Sub `first` — `WAVE` (`playSubWaveField`) | `(14, 404, 292, 72)` (`play-osc-native-contract.json:6`, caption inside box) | `(14, 514, 292, 72)` | `(14, 439, 284, 60)` | Title `(14, 439, 284, 16)`, Box `(14, 459, 284, 40)` | `(14, 549, 284, 60)` | Titled dropdown: `WAVE` left-aligned above box (`20 px` = `16 + 4 gap.field`), `40 px` legacy dropdown box, centered value, chevron right |
| Sub `second` — `OCT` (`playOscNumberField`) | `(310, 412, 112, 56)` (`play-osc-native-contract.json:7`, box `y+8..y+48`) | `(310, 522, 112, 56)` | `(306, 451, 116, 56)` | `(306, 459, 116, 40)` | `(306, 561, 116, 56)` | `name_value` (`height.intermediate = 40` centered in `56 px` slot); visual `y = 459..499` aligns flush with `WAVE` box `y = 459..499`; `8 px` gap at `298..306` |
| A/B/C `first` — `TYPE` (`playOscChoiceField`) | `(14, 404, 202, 72)` (`play-osc-native-contract.json:15`) | `(14, 514, 202, 72)` | `(14, 439, 200, 60)` | Title `(14, 439, 200, 16)`, Box `(14, 459, 200, 40)` | `(14, 549, 200, 60)` | Titled dropdown: `TYPE` above left + `40 px` legacy dropdown |
| A/B/C `second` — `SOURCE` (`playOscTableField` / `playOscContentField`) | `(220, 404, 202, 72)` (`play-osc-native-contract.json:16`, `<`/`>` steppers) | `(220, 514, 202, 72)` | `(222, 439, 200, 60)` | Title `(222, 439, 200, 16)`, Box `(222, 459, 200, 40)` | `(222, 549, 200, 60)` | Titled dropdown replacing `<`/`>` arrow steppers: `SOURCE` above left + `40 px` legacy dropdown (tap sends `SourceBrowse = 2` to open browser); `8 px` gap `214..222` |
| Noise `first` — `SOUND` (`playOscChoiceField`) | `(14, 404, 202, 72)` (`play-osc-native-contract.json:24`) | `(14, 514, 202, 72)` | `(14, 439, 200, 60)` | Title `(14, 439, 200, 16)`, Box `(14, 459, 200, 40)` | `(14, 549, 200, 60)` | Titled dropdown: `SOUND` above left + `40 px` legacy dropdown |
| Noise `second` — `FILTER` (`compactNativeRiseDial`) | `(220, 404, 202, 72)` (`play-osc-native-contract.json:25`, 60 px dial) | `(220, 514, 202, 72)` | `(222, 439, 200, 60)` | Knob `(224, 441, 56, 56)` | `(222, 549, 200, 60)` | 56 px dial (`play_sub_level_56.png`), placement `'right'`: knob `(2, 2, 56, 56)`, caption `(66, 4, 130, 22)`, value `(66, 28, 130, 28)` |
| All Osc `pan` (`playSubPan` / `playOscPan`) | `(14, 510, 202, 108)` (`play-osc-native-contract.json:8,17,26`, 90 px dial) | `(14, 620, 202, 108)` | `(14, 524, 200, 94)` | Knob `(16, 543, 56, 56)` | `(14, 634, 200, 94)` | 56 px bipolar dial (`play_sub_pan_56.png`), placement `'right'`: knob `(2, 19, 56, 56)`, caption `(66, 22, 130, 22)`, value `(66, 46, 130, 28)` |
| All Osc `level` (`playSubLevel` / `compactNativeDial`) | `(220, 510, 202, 108)` (`play-osc-native-contract.json:9,18,27`, 90 px dial) | `(220, 620, 202, 108)` | `(222, 524, 200, 94)` | Knob `(224, 543, 56, 56)` | `(222, 634, 200, 94)` | 56 px unipolar dial (`play_sub_level_56.png`), placement `'right'`: knob `(2, 19, 56, 56)`, caption `(66, 22, 130, 22)`, value `(66, 46, 130, 28)`; `8 px` gap `214..222` |

---

### 1.3 Panel 2 — Envelope (`x = 446..834`, `W = 388`)

| Control (Definition) | Old Tab Rect `(x, y, w, h)` | Old Screen Rect | New Tab Slot `(x, y, w, h)` | New Tab Visual `(x, y, w, h)` | New Screen Slot | Component / Geometry Details |
|---|---|---|---|---|---|---|
| `Play Env Select` (`playModSelector`) | `(446, 10, 388, 56)` (`sizes.json:1136`) | `(446, 120, 388, 56)` | `(454, 10, 324, 56)` | `(454, 18, 324, 40)` | `(454, 120, 324, 56)` | `selector` dropdown (`dropdown_look="legacy"`, `dropdown_size="selector.dropdown.size.40"`, value centered, chevron right); `8 px` left pad `446..454`, `8 px` gap before EXPAND `778..786` |
| `ENV Expand` (`Play Native EnvLfo ENV Expand 1-6`) | `(766, 82, 64, 56)` (`play-env-lfo-contract.json:2`) | `(766, 192, 64, 56)` | `(778, 10, 56, 56)` | `(786, 18, 40, 40)` | `(778, 120, 56, 56)` | `icon_button` (`expand`, `height=40`, `legacy`) centered in `56×56` slot (`8 px` pad all round; right edge `834`) |
| Env Display Frame (`EnvGraph`, overlay) | `(446, 74, 388, 292)` (`env_screen.h:124-125`) | `(446, 184, 388, 292)` | `(446, 74, 388, 328)` | `(446, 74, 388, 328)` | `(446, 184, 388, 328)` | Zero button apertures inside frame |
| Env Plot Area (`EnvGraph::plotArea`, overlay) | `(460, 156, 360, 190)` (`env_screen.h:126`) | `(460, 266, 360, 190)` | `(460, 84, 360, 298)` | `(460, 84, 360, 298)` | `(460, 194, 360, 298)` | `compactPlotRect(gr)`: `+108 px` vertical drag room for DAHDSR handles |
| `Play Env Attack` (`compactNativeDial`) | `(446, 386, 192, 108)` (`sizes.json:1140`, 90 px) | `(446, 496, 192, 108)` | `(446, 422, 190, 94)` | Knob `(448, 441, 56, 56)` | `(446, 532, 190, 94)` | 56 px dial (`play_sub_level_56.png`), placement `'right'`: knob `(2, 19, 56, 56)`, caption `(66, 22, 120, 22)`, value `(66, 46, 120, 28)` |
| `Play Env Decay` (`compactNativeDial`) | `(642, 386, 192, 108)` (`sizes.json:1141`, 90 px) | `(642, 496, 192, 108)` | `(644, 422, 190, 94)` | Knob `(646, 441, 56, 56)` | `(644, 532, 190, 94)` | Same 56 px dial; `8 px` col gap `636..644` |
| `Play Env Sustain` (`compactNativeDial`) | `(446, 510, 192, 108)` (`sizes.json:1142`, 90 px) | `(446, 620, 192, 108)` | `(446, 524, 190, 94)` | Knob `(448, 543, 56, 56)` | `(446, 634, 190, 94)` | Same 56 px dial; `8 px` row gap `516..524`; aligns flush with Osc PAN/LEVEL (`y = 524..618`) |
| `Play Env Release` (`compactNativeDial`) | `(642, 510, 192, 108)` (`sizes.json:1143`, 90 px) | `(642, 620, 192, 108)` | `(644, 524, 190, 94)` | Knob `(646, 543, 56, 56)` | `(644, 634, 190, 94)` | Same 56 px dial; bottom `618`, right edge `834` |

---

### 1.4 Panel 3 — LFO 1..10 & RND 1..4 (`x = 864..1260`, `W = 396`)

#### 1.4a Header & Standard LFO 1..10 (Views `1=LfoHz`, `2=LfoBpm`, `3=LfoKey`)

| Control (Definition) | Old Tab Rect `(x, y, w, h)` | Old Screen Rect | New Tab Slot `(x, y, w, h)` | New Tab Visual `(x, y, w, h)` | New Screen Slot | Component / Geometry Details |
|---|---|---|---|---|---|---|
| `Play LFO Select` / `Play Native EnvLfo Select 1-6` (`playEnvLfoChoice Select`) | `(864, 10, 396, 56)` (`sizes.json:1136`, `play-env-lfo-contract.json:6`) | `(864, 120, 396, 56)` | `(872, 10, 332, 56)` | `(872, 18, 332, 40)` | `(872, 120, 332, 56)` | `selector` dropdown (`dropdown_look="legacy"`, `dropdown_size="selector.dropdown.size.40"`, value centered, chevron right); `8 px` left pad `864..872`, `8 px` gap before EXPAND `1204..1212`; MUST match `play.selector.rects[2]` (`env_lfo.py:199`) |
| `LFO Expand` (`Play Native EnvLfo LFO Expand 1-6`) | `(1192, 82, 64, 56)` (`play-env-lfo-contract.json:3`) | `(1192, 192, 64, 56)` | `(1204, 10, 56, 56)` | `(1212, 18, 40, 40)` | `(1204, 120, 56, 56)` | `icon_button` (`expand`, `height=40`, `legacy`) centered in `56×56` slot (`8 px` pad all round; right edge `1260`) |
| LFO Display Frame (overlay, views 1..3) | `(864, 74, 396, 292)` (`lfo_screen.h:99`) | `(864, 184, 396, 292)` | `(864, 74, 396, 328)` | `(864, 74, 396, 328)` | `(864, 184, 396, 328)` | Full unbroken `drawNativeGraphFrame`; 0 button apertures inside frame |
| LFO Plot Area (overlay, views 1..3) | `(878, 156, 368, 190)` (`lfo_ui.h:717`) | `(878, 266, 368, 190)` | `(878, 84, 368, 298)` | `(878, 84, 368, 298)` | `(878, 194, 368, 298)` | `compactPlotRect(display)`: `+108 px` vertical room; redundant band label (`lfo_display.h:304`) omitted on compact |
| `SHAPE` (`playEnvLfoChoice SHAPE`, views 1..3) | `(864, 386, 396, 56)` (`play-env-lfo-contract.json:4`, caption inside box) | `(864, 496, 396, 56)` | `(864, 422, 396, 60)` | Title `(864, 422, 396, 16)`, Box `(864, 442, 396, 40)` | `(864, 532, 396, 60)` | Titled dropdown: `SHAPE` above left (`20 px` = `16 + 4 gap.field`), `40 px` legacy dropdown box, value centered, chevron right, no arrows |
| `RATE` (`playEnvLfoRate`, view 1 `LfoHz`) | `(864, 458, 182, 72)` (`play-env-lfo-contract.json:5`, 60 px) | `(864, 568, 182, 72)` | `(864, 490, 180, 60)` | Knob `(866, 492, 56, 56)` | `(864, 600, 180, 60)` | 56 px dial (`play_sub_level_56.png`), placement `'right'`: knob `(2, 2, 56, 56)`, caption `(66, 4, 110, 22)`, value `(66, 28, 110, 28)` |
| `DIVISION` (`playEnvLfoChoice DIVISION`, view 2 `LfoBpm`) | `(864, 462, 182, 64)` (`play-env-lfo-contract.json:11`) | `(864, 572, 182, 64)` | `(864, 490, 180, 60)` | Title `(864, 490, 180, 16)`, Box `(864, 510, 180, 40)` | `(864, 600, 180, 60)` | Titled dropdown: `DIVISION` above left + `40 px` legacy dropdown; hidden in view 3 `LfoKey` |
| `HZ \| BPM \| KEY` (`Play Native EnvLfo SYNC 1-3`, views 1..3, **RadioGrid 1×3**) | `(1050, 466, 210, 56)` (`play-env-lfo-contract.json:7`, 3×`70×56`) | `(1050, 576, 210, 56)` | `(1052, 492, 208, 56)` (cells: HZ `1052,492,68,56`; BPM `1120,492,72,56`; KEY `1192,492,68,56`) | HZ `(1052, 500, 64, 40)`; BPM `(1124, 500, 64, 40)`; KEY `(1196, 500, 64, 40)` | `(1052, 602, 208, 56)` | `radio_grid` 1×3: equal `64 × 40` faces (`button.size.40`, `legacy` off / `active` on), exact `8 px` gaps (`64+8+64+8+64 = 208`), `8 px` gap from RATE (`1044..1052`), touch slots `>= 68 × 56` |
| `Play LFO Rise` (`compactNativeRiseDial`, views 1..3) | `(864, 546, 196, 72)` (`sizes.json:1139`, 60 px) | `(864, 656, 196, 72)` | `(864, 558, 194, 60)` | Knob `(866, 560, 56, 56)` | `(864, 668, 194, 60)` | 56 px dial (`play_sub_level_56.png`), placement `'right'`: knob `(2, 2, 56, 56)`, caption `(66, 4, 124, 22)`, value `(66, 28, 124, 28)` |
| `Play LFO Smooth` / `Off` (`compactNativeSmoothWithIndicator` / `Readout`, views 1..3) | `(1064, 546, 196, 72)` (`sizes.json:1144,350-385`, 60 px + `active_text`) | `(1064, 656, 196, 72)` | `(1066, 558, 194, 60)` | Knob `(1068, 560, 56, 56)`, On/Off `(1128, 560, 60, 56)`, Value `(1192, 574, 66, 28)` | `(1066, 668, 194, 60)` | 56 px dial (`play_sub_level_56[_inactive].png`) + **`on_off_label`** Button `"name"` `(62, 2, 60, 56)` (`>= 56×56`, disjoint from knob `2..58` & value `126..192`) + Value Label `(126, 16, 66, 28)`; `8 px` gap from RISE (`1058..1066`) |

#### 1.4b Random LFO `RND 1..4` (Views `4=RndHz`, `5=RndBpm`, `6=RndKey`)

| Control (Definition) | Old Tab Rect `(x, y, w, h)` | Old Screen Rect | New Tab Slot `(x, y, w, h)` | New Tab Visual `(x, y, w, h)` | New Screen Slot | Component / Geometry Details |
|---|---|---|---|---|---|---|
| RND Display Frame (overlay, views 4..6) | `(864, 74, 396, 232)` (`lfo_screen.h:96`) | `(864, 184, 396, 232)` | `(864, 74, 396, 260)` | `(864, 74, 396, 260)` | `(864, 184, 396, 260)` | `+28 px` frame height (`kQuadRandomH: 304 -> 280`) |
| RND Plot Area (overlay, views 4..6) | `(878, 156, 368, 130)` | `(878, 266, 368, 130)` | `(878, 84, 368, 230)` | `(878, 84, 368, 230)` | `(878, 194, 368, 230)` | `+100 px` vertical trace height |
| `STYLE` (`playEnvLfoChoice STYLE`, views 4..6) | `(864, 330, 396, 56)` (`play-env-lfo-contract.json:22`) | `(864, 440, 396, 56)` | `(864, 354, 396, 60)` | Title `(864, 354, 396, 16)`, Box `(864, 374, 396, 40)` | `(864, 464, 396, 60)` | Titled dropdown: `STYLE` above left + `40 px` legacy dropdown |
| `RATE` (`playEnvLfoRate`, view 4 `RndHz`) | `(864, 402, 182, 72)` (`play-env-lfo-contract.json:21`) | `(864, 512, 182, 72)` | `(864, 422, 180, 60)` | Knob `(866, 424, 56, 56)` | `(864, 532, 180, 60)` | Shared `playEnvLfoRate` 56 px dial |
| `DIVISION` (`playEnvLfoChoice DIVISION`, view 5 `RndBpm`) | `(864, 406, 182, 64)` (`play-env-lfo-contract.json:28`) | `(864, 516, 182, 64)` | `(864, 422, 180, 60)` | Title `(864, 422, 180, 16)`, Box `(864, 442, 180, 40)` | `(864, 532, 180, 60)` | Shared `playEnvLfoChoice DIVISION` titled dropdown; hidden in view 6 `RndKey` |
| `SYNC` (`playEnvLfoChoice SYNC`, views 4..6) | `(864, 490, 396, 56)` (`play-env-lfo-contract.json:23`) | `(864, 600, 396, 56)` | `(864, 490, 396, 60)` | Title `(864, 490, 396, 16)`, Box `(864, 510, 396, 40)` | `(864, 600, 396, 60)` | Titled dropdown: `SYNC` above left + `40 px` legacy dropdown |
| `STEREO` (`playRndStereo`, views 4..6) | `(864, 562, 396, 56)` (`play-env-lfo-contract.json:24`) | `(864, 672, 396, 56)` | `(864, 558, 396, 60)` | `(864, 558, 396, 60)` | `(864, 668, 396, 60)` | `on_off_label_definition("STEREO", (396, 60), handle="Data")` (bottom `618`) |

### 1.5 Dial Internal Geometry Verification (`56 px` Dial, Placement `'right'`)

| Definition | Cell `(w, h)` | Focus Rect | Knob Rect (`56×56`) | Caption Rect (`name`) | Value Rect (`value`) | Filmstrip Asset |
|---|---|---|---|---|---|---|
| `compactNativeDial` (Env ADSR) | `(190, 94)` | `(0, 0, 190, 94)` | `(2, 19, 56, 56)` | `(66, 22, 120, 22)` | `(66, 46, 120, 28)` | `play_sub_level_56.png` |
| `playSubPan` / `playOscPan` (Osc PAN) | `(200, 94)` | `(0, 0, 200, 94)` | `(2, 19, 56, 56)` | `(66, 22, 130, 22)` | `(66, 46, 130, 28)` | `play_sub_pan_56.png` |
| `playSubLevel` / Osc `level` | `(200, 94)` | `(0, 0, 200, 94)` | `(2, 19, 56, 56)` | `(66, 22, 130, 22)` | `(66, 46, 130, 28)` | `play_sub_level_56.png` |
| `compactNativeRiseDial` (LFO Rise) | `(194, 60)` | `(0, 0, 194, 60)` | `(2, 2, 56, 56)` | `(66, 4, 124, 22)` | `(66, 28, 124, 28)` | `play_sub_level_56.png` |
| `playOscFilterDial` (Noise FILTER, 200×60) | `(200, 60)` | `(0, 0, 200, 60)` | `(2, 2, 56, 56)` | `(66, 4, 130, 22)` | `(66, 28, 130, 28)` | `play_sub_level_56.png` |
| `playEnvLfoRate` (LFO/RND RATE) | `(180, 60)` | `(0, 0, 180, 60)` | `(2, 2, 56, 56)` | `(66, 4, 110, 22)` | `(66, 28, 110, 28)` | `play_sub_level_56.png` |
| `compactNativeSmoothWithIndicator` / `Readout` | `(194, 60)` | `(0, 0, 194, 60)` | `(2, 2, 56, 56)` | On/Off Button `(62, 2, 60, 56)` | `(126, 16, 66, 28)` | `play_sub_level_56[_inactive].png` |
