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

---

## 2. Exact C++ and Skin Change Inventory

### 2.1 C++ Places That Must Change

#### A. Shared Play Row & Header Geometry (`shell-mpc/ui/play_rows.h`)
- **CRITICAL GUARDRAIL:** Standalone full-screen views ALSO call `playrows::powerRect`, `playrows::fullRect`, `playrows::plotRect` (`shell-mpc/ui/screen.h:517`, `shell-mpc/ui/lfo_screen.h:495,514`, `shell-mpc/overlay/osc_ui.h:240`, `shell-mpc/overlay/filter_ui.h:264`). **Do NOT change or remove** `kBandH`, `bandRect`, `powerRect(display)`, `fullRect(display)`, `routeRect(display)`, or `plotRect(display)` for display-band callers.
- `shell-mpc/ui/play_rows.h:15-20`:
  - Change `kControlsH` from `232` to **`196`**.
  - Add Play header & compact-row constants: `kHeaderPad = 8`, `kHeaderGap = 8`, `kHeaderButtonSlot = 56`, `kHeaderRouteSlot = 72`, `kControlGap = 8`, `kCompactRowH = 60`, `kCompactTwoRowH = 94`.
  - Keep `kMargin = 6`, `kSelectorH = 56`, `kSelectorGap = 8`, `kDisplayGap = kQLane + 4` (`20`).
- `shell-mpc/ui/play_rows.h:22-59`:
  - `displayRect(third)` (`:22-27`) and `controlsLine(third)` (`:59`) automatically yield `y = 74..402` (`h = 328`) and `controlsLine = 422` (tab) once `kControlsH = 196`.
  - Add Play-panel header placement helpers operating on `third`:
    - `headerBandRect(third)` -> `{ third.x + kMargin, third.y + kMargin, third.w - 2 * kMargin, kSelectorH }`
    - `headerPowerRect(third)` -> `{ h.x, h.y, kHeaderButtonSlot, kSelectorH }` (`(14, 120, 56, 56)` screen)
    - `headerExpandRect(third)` -> `{ h.right() - kHeaderButtonSlot, h.y, kHeaderButtonSlot, kSelectorH }` (`(366/778/1204, 120, 56, 56)` screen)
    - `headerRouteRect(third)` -> `{ h.right() - kHeaderButtonSlot - kHeaderRouteSlot, h.y, kHeaderRouteSlot, kSelectorH }` (`(294, 120, 72, 56)` screen, Option A)
    - `headerSelectorRect(third, hasPower, hasRoute)` -> `{ x0, h.y, x1 - x0, kSelectorH }` where `x0 = hasPower ? h.x + kHeaderButtonSlot : h.x + kHeaderPad`, `x1 = hasRoute ? h.right() - kHeaderButtonSlot - kHeaderRouteSlot : h.right() - kHeaderButtonSlot`.
  - Add `compactPlotRect(display)` -> `{ display.x + kPlotPadX, display.y + kPlotPadTop, std::max(0, display.w - 2 * kPlotPadX), std::max(0, display.h - kPlotPadTop - kPlotPadBottom) }` (no `kBandH` subtraction -> `y_tab = 84..382`, `h = 298`).

#### B. Compact UI Components (`shell-mpc/ui/`)
- `shell-mpc/ui/screen.h:61-64,84-96` (`OscillatorCompact`):
  - `:62`: `plotRect()` returns `playrows::compactPlotRect(display_)` (instead of `playrows::plotRect(display_)`).
  - `:89-93`: Place `power` at `playrows::headerPowerRect(rect_)`, `full` at `playrows::headerExpandRect(rect_)`, `route` at `sub ? Rect {} : playrows::headerRouteRect(rect_)` (Option A), and `instances` at `playrows::headerSelectorRect(rect_, true, !sub)`.
  - `:94-95`: `basic.place({ rect_.x + m, by, W, std::max(0, rect_.bottom() - m - by) })` automatically gets `y_tab = 422..618` (`h = 196`).
- `shell-mpc/ui/composite.h:467-483` (`BasicControls::layout`, `stacked_` branch):
  - Replace internal row/column gaps (`kQLane = 16`, `kGap = 4`) with `playrows::kControlGap` (`8`):
    - `rowH = (rect_.h - 8) / 2` = `94`, `row2Y = rect_.y + 94 + 8` = `524` (tab), `cw = (rect_.w - 8) / 2` = `200`.
    - Row 1 height `h1 = 60`, `y1 = rect_.y + (rowH - h1) / 2` = `439` (tab).
    - Sub special widths (`inst_ == OscInstance::Sub`): `first` (`WAVE`) `{ rect_.x, y1, 284, 60 }`, `second` (`OCT`) `{ rect_.x + 292, y1 + 12, 116, 56 }` (`(306, 451, 116, 56)` tab; matches `play-osc-native-contract.json`).
    - A/B/C & Noise: `first` `{ rect_.x, y1, 200, 60 }`, `second` `{ rect_.x + 208, y1, 200, 60 }`.
    - Row 2 (`pan`, `level`): `dialMax = 56`, `pan` `{ rect_.x, row2Y, 200, 94 }`, `level` `{ rect_.x + 208, row2Y, 200, 94 }`.
    - Update `BasicControls::minSize()` (`composite.h:454-462`) so `stacked_` minimum width/height stays `<= 408 × 196` (also resolves Noise FILTER minimum mismatch noted in `TODO.md:297`).
- `shell-mpc/ui/env_screen.h:67,74,119-134` (`EnvCompact`):
  - `:74`: `k->state.dialMax = 56;` (was `96`).
  - `:122`: `instances.place(playrows::headerSelectorRect(rect_, false, false))` (`(454, 120, 324, 56)` screen).
  - `:126`: `graph.plotArea = playrows::compactPlotRect(gr);` (was `playrows::plotRect(gr)`).
  - `:127`: `full.place(playrows::headerExpandRect(rect_));` (was `playrows::fullRect(gr)`).
  - `:128-133`: Use `const int gap = playrows::kControlGap;` (`8`), `kh = (rect_.bottom() - kMargin - by - gap) / 2` (`94`), `cw = (W - gap) / 2` (`190`): `attack (446, 422, 190, 94)`, `decay (644, 422, 190, 94)`, `sustain (446, 524, 190, 94)`, `release (644, 524, 190, 94)` (tab).
- `shell-mpc/ui/lfo_screen.h:78,87-124` (`LfoCompact`):
  - `:78`: Update `kQuadH = 212` (`196 + 16`), `kQuadRandomH = 280` (`264 + 16`), `kRowH = 60`, `kRateH = 60`.
  - `:90`: `instance.place(playrows::headerSelectorRect(rect_, false, false))` (`(872, 120, 332, 56)` screen).
  - `:94-101`: Standard LFO uses `display_ = playrows::displayRect(rect_)` (`y_tab = 74..402`, `h = 328`), `qy = display_.bottom() + 4`; Random LFO uses `qy = rect_.bottom() - m - kQuadRandomH` (`y_tab = 338`), `display_ = { rect_.x + m, dy, W, std::max(0, qy - 4 - dy) }` (`y_tab = 74..334`, `h = 260`).
  - `:102`: `full.place(playrows::headerExpandRect(rect_));` (was `playrows::fullRect(display_)`).
  - `:103-124`: Use internal row gap `const int rg = playrows::kControlGap;` (`8`) instead of `kQLane` (`16`) between rows inside the block (keeping `y = qy + kQLane` at the top of the block for the Gen 1 Q-bank badge!):
    - Random (`random_`): `style` `354..414`, `rate` `422..482`, `sync` `490..550`, `stereo` `558..618` (each `h = 60`, `8 px` gaps).
    - Standard: `shape` `422..482`, `rate` `490..550`, `rise` `(864, 558, 194, 60)` + `smoothTime` `(1066, 558, 194, 60)` (`cw = (W - 8) / 2 = 194`).
- `shell-mpc/ui/lfo_parts.h:198,239-268` (`RateControl`):
  - `:198`: Change `kModeW` from `3 * 70` (`210`) to **`208`** (`64 + 8 + 64 + 8 + 64`).
  - `:242-247`: In `showMode_` (compact), use `const int mg = playrows::kControlGap;` (`8`) instead of `kGap` (`4`) between `main` and `modeCells` -> `main.w = 396 - 208 - 8 = 180` (`(864, 490, 180, 60)` tab), `modeCells` `(1052, 492, 208, 56)` tab (`h = 56` centered in `60`).
  - `:263`: In compact `else` branch, `const int h = std::min(main.h, 60);` -> `division` gets `(864, 490, 180, 60)` tab (matching `RATE`).
- `shell-mpc/ui/lfo_display.h:299-308`:
  - `:301-305`: `if (isCompact(r, d) && !d.pageTitle)` currently calls `pen.textLeft(playrows::labelRect(r, false, false), d.label, ...)` which draws inside `y = r.y + 4..68`, overlapping the new `compactPlotRect` (`y = r.y + 10`)! Suppress compact band text when `d.area.y < r.y + playrows::kBandH` (the SHAPE/STYLE dropdown right below already displays the shape/style name).
- `shell-mpc/ui/display.h:215-224` & `shell-mpc/ui/native_apertures.h:14-49`:
  - **Verify-only (no change required for Option A):** `outsideNativeCells` (`native_apertures.h:21`) checks `base.overlaps(cell)`. When `POWER` and `EXPAND` (and Option A `ROUTE`) move to `y_tab = 10..66`, none of them overlaps `displayRect` (`y_tab = 74..402`), so `paintAroundNativeCells` paints the full unbroken `drawNativeGraphFrame` while keeping `!nativeHeader[0].empty()` true.

#### C. Overlay Views, Apertures, Touch Routing & Generated Contract Header (`shell-mpc/overlay/` & `shell-mpc/`)
- `shell-mpc/play_osc_native_contract.h:14-16` (**regenerated from JSON** via `python3 scripts/native_play/osc.py`):
  - `kSubContractRects`: `{ { 14, 10, 56, 56 }, { 366, 10, 56, 56 }, { 0, 0, 0, 0 }, { 14, 439, 284, 60 }, { 306, 451, 116, 56 }, { 14, 524, 200, 94 }, { 222, 524, 200, 94 } }`
  - `kAbcContractRects`: `{ { 14, 10, 56, 56 }, { 366, 10, 56, 56 }, { 294, 10, 72, 56 }, { 14, 439, 200, 60 }, { 222, 439, 200, 60 }, { 14, 524, 200, 94 }, { 222, 524, 200, 94 } }`
  - `kNoiseContractRects`: identical to `kAbcContractRects`.
- `shell-mpc/overlay/play_view.h`:
  - `:43`: `kNativeSelectorY = 10, kNativeSelectorH = 56` — **unchanged**; continues to pass all touches (`:347-348`) and wheel events (`:373-374`) across the entire `56 px` panel header band (`POWER`, `dropdown`, `ROUTE`, `EXPAND`) to the host, and continues to bound `drawNativeMarkerCovers(sb, "play1")` (`:733-734`).
  - `:337-342` (`onTouch`): `nativeEnvLfo_.coversNative` already checks `EnvExpand` and `LfoExpand` via `compact_.full.rect()`; once `full` moves to the header band, check order (`:337` before `:347`) still returns `false` to the host.
  - `:687-710` (`compactGround`, fallback path when native compact Osc is off): update `:700` `84.0f -> 70.0f`, `:703` `dialTop = 634.0f` (was `620.0f`), `:707` `214.0f..222.0f` (was `216.0f..220.0f`).
  - `:534-567` (`publishQ`, `extraTargets`): **unchanged** — Q-Link banks 1..4 (`Osc Basic`, `Env ADSR`, `LFO Rate`, `Extra [LFO SYNC, OSC POWER, LFO MONO, -]`) stay identical.
- `shell-mpc/overlay/play_env_lfo.h:17-20,90-108`:
  - `rect(env, lfo, field)` reads `env.compact().full.rect()` and `lfo.compactNativeRect(field)`, so `coversNative` and `drawApertures` automatically track the new header & body rects without hardcoded constants.
- `shell-mpc/overlay/osc_ui.h`:
  - `:115,120,361`: `kCompactX = 8, kCompactW = 420` — unchanged.
  - `:356-360` (`covers`): reads `compactNativeCells_` (populated from `playoscui::contractRect` at `:573-576`) and `compact_.basic.pan/level.rect()` — automatically updated once `play_osc_native_contract.h` and `BasicControls::layout()` update.
  - `:579`: `compact_.setNativeHeader(...)` still populates non-empty `nativeHeader_[0]` (`POWER`), preserving native-background mode in `PlayView::compactGround` (`play_view.h:687`) and `OscillatorCompact::draw` (`screen.h:69`).
  - `:743-755` (`applyNativeSetting`, `SettingsSource` for Wavetable): verify that single-tap dropdown sends `SourceBrowse = 2` (`value = 2.0f / 3.0f` or `>= 0.5f`) and opens the wavetable browser cleanly (no `SourcePrevious`/`SourceNext` arrows emitted in skin, while retaining `SourcePrevious`/`SourceNext` support for Q-Link/tests).
  - `:2838,2860`: `openSourceList`/`openTypeList` (legacy overlay fallback list): `third.y + 6 + kSelectorH` remains `y_tab = 66` — unchanged.
- `shell-mpc/overlay/env_ui.h`:
  - `:79,138,255-284,349-354,666`: `kCompactX = 440, kCompactW = 400`, `kGroundX0 = 434, kGroundX1 = 848`, `drawCompact` and `covers` read `compact_.attack/decay/sustain/release.rect()` dynamically — automatically updated; no hardcoded rect changes needed.
- `shell-mpc/overlay/lfo_ui.h`:
  - `:114,125,350,492-494,796`: `drawCompact` (`:350`) hardcodes `header { kCompactX + 6, kTabY + 10, kCompactW - 12, 56 }` (covers the entire `864..1260, y=10..66` header band including EXPAND, still valid!). `covers` (`:492`) reads `compact_.rise.rect()` and `compact_.smoothTime.rect()` dynamically.
  - `:714-720` (`compactData`): change `d.area = ui::playrows::plotRect(compact_.displayRect())` (`:717`) to **`d.area = ui::playrows::compactPlotRect(compact_.displayRect())`**. Keep `d.aperture = compact_.full.rect()` (non-empty) so `lfodisplay::draw` (`lfo_display.h:154-170`) continues to render `drawNativeGraphFrame` instead of the legacy gradient fill!
- **Detection Markers & Sentinels (NO POSITION CHANGES):**
  - `shell-mpc/overlay/overlay_layout.json:46-82` & `overlay_layout.h:25`: Play markers `(33, 1, 6, 6)`, `(1241, 1, 6, 6)`, instance `(45, 1, 6, 6)` — **unchanged**.
  - `shell-mpc/overlay/backgrounds_manifest.h:23` & `scripts/mpc-skin-extras/99_backgrounds.py:62`: Play sentinel `(900, 2, 5, 5)` — **unchanged**.
  - `shell-mpc/overlay/controller.h:120-137` & `scripts/mpc-skin-extras/95_popup_markers.py:11`: Modal/action markers at `y = 120` (`x = 16, 28, 40, 52, 64, 76, 88, 100, 112, 124, 136`) live on full-screen `MenuOverlay` popups — **unchanged**.
  - `shell-mpc/overlay/native_marker_covers.h:17-41`: covers the same top 10 px strip — **unchanged**.

---

### 2.2 Skin & Component Places That Must Change

#### A. Tokens in `components/sizes.json`
Update existing `play.*` / `level.play_smooth_rects.*` / `smooth.*` tokens and add explicit Phase 2 tokens (zero hardcoded pixel literals in scripts, per `components/AGENTS.md:15`):

| Token Name in `components/sizes.json` | Old Value (line) | New Value | Purpose |
|---|---|---|---|
| `play.selector.rects` | `[[14, 10, 408, 56], [446, 10, 388, 56], [864, 10, 396, 56]]` (`:1136`) | `[[70, 10, 296, 56], [454, 10, 324, 56], [872, 10, 332, 56]]` | Panel header dropdown slots (Sub, Env, LFO) |
| `play.selector.osc_routed.rect` *(NEW)* | — | `[70, 10, 224, 56]` | Osc A/B/C/Noise header dropdown slot (Option A, leaves `294..366` for ROUTE) |
| `play.header.icon.size` *(NEW)* | — | `{"ref": "height.intermediate"}` (`40`) | Visual `40 × 40` size of POWER / EXPAND inside `56 × 56` header slot |
| `play.header.icon.slot` *(NEW)* | — | `[56, 56]` | Touch slot size for Play header POWER / EXPAND |
| `play.header.dropdown.size` *(NEW)* | — | `"selector.dropdown.size.40"` | Shared 40 px dropdown token for all 3 panel headers |
| `play.titled_dropdown.title_height` *(NEW)* | — | `20` | Title above left (`16 px` label + `4 px` `gap.field`) for WAVE/TYPE/SOURCE/SOUND/SHAPE/DIV/STYLE/SYNC |
| `play.titled_dropdown.box_height` *(NEW)* | — | `{"ref": "height.intermediate"}` (`40`) | 40 px dropdown face under title (`20 + 40 = 60 px` outer cell) |
| `play.env.attack.rect` | `[446, 386, 192, 108]` (`:1140`) | `[446, 422, 190, 94]` | Env Attack 56 px dial cell |
| `play.env.decay.rect` | `[642, 386, 192, 108]` (`:1141`) | `[644, 422, 190, 94]` | Env Decay 56 px dial cell |
| `play.env.sustain.rect` | `[446, 510, 192, 108]` (`:1142`) | `[446, 524, 190, 94]` | Env Sustain 56 px dial cell |
| `play.env.release.rect` | `[642, 510, 192, 108]` (`:1143`) | `[644, 524, 190, 94]` | Env Release 56 px dial cell |
| `play.lfo.rise.rect` | `[864, 546, 196, 72]` (`:1139`) | `[864, 558, 194, 60]` | LFO Rise 56 px dial cell |
| `play.lfo.smooth.rect` | `[1064, 546, 196, 72]` (`:1144`) | `[1066, 558, 194, 60]` | LFO Smooth 56 px dial + On/Off label cell |
| `play.env.dial.geometry` | `[192, 108, 90, [2, 9, 90, 90], [96, 32, 92, 22], [96, 54, 92, 32]]` (`:1145`) | `[190, 94, 56, [2, 19, 56, 56], [66, 22, 120, 22], [66, 46, 120, 28]]` | Env ADSR 56 px dial internal rects |
| `play.osc.dial.geometry` *(NEW)* | — | `[200, 94, 56, [2, 19, 56, 56], [66, 22, 130, 22], [66, 46, 130, 28]]` | Osc PAN/LEVEL 56 px dial internal rects (`playSubPan`/`playOscPan`/`playSubLevel`/`playOscLevel`) |
| `play.osc.filter.dial.geometry` *(NEW)* | — | `[200, 60, 56, [2, 2, 56, 56], [66, 4, 130, 22], [66, 28, 130, 28]]` | Noise FILTER 56 px dial internal rects |
| `play.rise.dial.geometry` | `[196, 72, 60, [2, 6, 60, 60], [68, 12, 124, 22], [68, 34, 124, 28]]` (`:1146`) | `[194, 60, 56, [2, 2, 56, 56], [66, 4, 124, 22], [66, 28, 124, 28]]` | LFO Rise 56 px dial internal rects |
| `play.rise.seed_rects` | `{"focus":[0,0,196,72],"knob":[2,0,60,60],"name":[66,16,126,36]}` (`:1138`) | `{"focus":[0,0,194,60],"knob":[2,2,56,56],"name":[66,4,124,22]}` | Seed rects in `82_play.py:193` |
| `play.rate.dial.geometry` | `[182, 72, 60, [2, 6, 60, 60], [68, 12, 110, 22], [68, 34, 110, 28]]` (`:1147`) | `[180, 60, 56, [2, 2, 56, 56], [66, 4, 110, 22], [66, 28, 110, 28]]` | LFO/RND RATE 56 px dial internal rects |
| `play.osc.routing.size` | `[64, 64]` (`:1153`) | `[72, 56]` | Osc A/B/C/Noise header ROUTE slot (64×40 visual inside 72×56 slot, Option A) |
| `play.osc.choice.size` | `[202, 72]` (`:1154`) | `[200, 60]` | Osc TYPE / SOUND titled dropdown size |
| `play.lfo.selector.size` | `[396, 56]` (`:1177`) | `[396, 60]` | LFO SHAPE / STYLE / SYNC titled dropdown size |
| `play.lfo.division.size` *(NEW)* | — | `[180, 60]` | LFO/RND DIVISION titled dropdown size |
| `play.lfo.sync_grid` *(NEW)* | — | `{"button_width": 64, "button_height": {"ref": "height.intermediate"}, "slot_height": {"ref": "height.high"}, "gap": {"ref": "radio_grid.gap"}}` | LFO HZ/BPM/KEY RadioGrid 1×3 (`64+8+64+8+64 = 208`) |
| `smooth.play.width` / `smooth.play.height` *(NEW)* | `smooth.play.width: 196` (`:43`), reused `smooth.height: 72` (`:37`) | `smooth.play.width: 194`, `smooth.play.height: 60` | Keep `smooth.height: 72` untouched for non-Play callers; Play uses `60` |
| `smooth.play.value.width` | `62` (`:46`) | `66` | Widens Smooth value box inside `194 × 60` cell |
| `level.play_smooth_rects.*` | `focus [0,0,196,72]`, `knob [2,6,60,60]`, `name [66,8,60,56]`, `value [130,22,62,28]` (`:350-385`) | `focus [0,0,194,60]`, `knob [2,2,56,56]`, `name [62,2,60,56]`, `value [126,16,66,28]` | 56 px knob + 60×56 On/Off label Button + 66×28 value Label, all strictly disjoint |
| `play.qlink.bounds_yh` | `[301, 323]` (`:1137`) | Keep `[301, 323]` (or evaluate `[422, 196]` only with device test — see `TODO.md:247`) | `HYPOTHESIS`: changing `qlinkBoundsData` without device check may alter host Q-Link focus rectangle behavior |

#### B. Shared Native Components (`components/native/`)
- `components/native/selector/selector.py`:
  - `:374` (`render_dropdown_face`): **Bug fix** noted in `docs/review/REVIEW-SKIN-NEWLOOK-2026-11-27.md:511-512` — compute `inset_px = geometry["inset"] if inset is None else inset` and use `inset_px` in `cx = (width - inset_px - geometry["chevron_lane"] / 2) * scale` so chevrons align consistently across `inset=0` and `dropdown_size`.
  - `:457-475` (`selector_definition`): When `dropdown_size` is passed with `slot_height > _style_size(dropdown_size)["height"]` (e.g. `slot=(296, 56)` with `dropdown_size="selector.dropdown.size.40"`), set `height = _style_size(dropdown_size)["height"]` (`40`) and center vertically via `top = (slot_height - height) // 2` (`8`) so the 40 px dropdown renders cleanly inside the 56 px header touch slot!
  - `:640-763` (`build_titled_selector`):
    1. `:687-689`: Allow `child_h = 40` (`height.intermediate`) when `total_height >= height.high (56)` and `dropdown_size` is set (today it hard-errors if `child_h < 56`).
    2. `:701-710`: Support `play_face=False, dropdown_look="legacy", dropdown_size="selector.dropdown.size.40"` so the child uses the plain legacy dropdown box (centered value, chevron right, no caption inside box).
    3. `:742`: Add `title_justification="left verticallyCentred"` parameter (currently hardcoded `"horizontallyCentred verticallyCentred"`) and `title_case="Upper Case"` with `DROPDOWN_LABEL_FONT` (`font.caption` 15) so `WAVE`, `TYPE`, `SOURCE`, `SOUND`, `SHAPE`, `DIVISION`, `STYLE`, `SYNC` render **above left** per `AGENTS.md` and `selector.py:1475-1517`.
- `components/native/level/level.py`:
  - `:55-60`: `PLAY_SMOOTH_RECTS` already reads `level.play_smooth_rects.*` from `sizes.json`.
  - `:1031-1054` (`blue_dial_definition`): Add optional `bipolar=False` parameter (calling `_dial_for_polarity(skins_dir, "BiPolar" if bipolar else "UniPolar", frame_size)` instead of hardcoding `bipolar=False` at `:1041`) so `playSubPan`/`playOscPan` can use the exact same 56 px `blue_dial_definition` factory (also closing `TODO.md:60` duplicate Pan definition!).
  - `:1125` (`unify_dial_text`): **Bug fix** noted in `docs/review/REVIEW-SKIN-NEWLOOK-2026-11-27.md:501` — include `key.endswith("Readout")` (specifically `compactNativeSmoothReadout`) in the `inactive` check so `99_zz_dial_text.py` does not overwrite the dimmed `smooth.value.off` colour on `compactNativeSmoothReadout` with bright `text.value`! Also decide whether `unify_dial_text` keeps `font.value` (20) on all 56 px dials or reads `label.dial.56` (`caption: 15, value: 22`) — see Risk 3.5.
- `components/native/on_off_label/on_off_label.py`:
  - `:28-108`: `label_face`, `on_off_label_child`, and `on_off_label_definition` already exist! Use `on_off_label.label_face("SMOOTH", "on"/"off", width=60, height=56)` + `on_off_label_child("name", on_img, off_img, PLAY_SMOOTH_RECTS["name"], handle="Active")` in BOTH `82_play.py` AND `env_lfo.py` (replacing `active_text_child`), and `on_off_label_definition("STEREO", (396, 60), handle="Data")` for `playRndStereo`.
- `components/native/radio_grid/radio_grid.py`:
  - Add a native skin entry builder `build_radio_grid_entries(name, rect, labels, handle, skins_dir, *, height=40, gates=(), focus="Yes")` (implementing `docs/ui-components/radio_grid.md:25-27` / using `action_art(text=..., width=64, height=40, look="legacy"/"active")` placed inside disjoint `>= 56 px` touch slots via `button.in_slot` / explicit x-offsets `0/+4/+4` so all 3 faces are **identical 64 × 40** with exact `8 px` gaps, unlike `radio_button.py:157` which produced unequal `65/61/66 px` widths on `width=208`).
- `components/native/button/button.py`:
  - `:222-238` (`_store_action_face`, `action_button`): Add optional `height=None` parameter forwarded to `action_art(..., height=height)` so Play header EXPAND (`action_button(..., rect=(366,10,56,56), height=40, icon="expand")`) renders a `40 × 40` icon centered in the `56 × 56` slot (`in_slot`).

#### C. Play Skin Extra Scripts & Contracts (`scripts/mpc-skin-extras/` & `scripts/native_play/`)
- `scripts/mpc-skin-extras/play-osc-native-contract.json:1-29`: Update all 3 groups (`sub`, `abc`, `noise`) to the exact Section 1.2 tab slots, then regenerate `shell-mpc/play_osc_native_contract.h` via `python3 scripts/native_play/osc.py`.
- `scripts/mpc-skin-extras/play-env-lfo-contract.json:1-39`: Regenerate from `lfo_ui_host --play-env-lfo-skin-contract` (after Batch 2B C++ changes) so every `[view, field, x, y, w, h]` matches Section 1.3 & 1.4 exact rects (`ENV Expand (778,10,56,56)`, `LFO Expand (1204,10,56,56)`, `Select (872,10,332,56)`, `SHAPE (864,422,396,60)`, `RATE (864,490,180,60)`, `DIVISION (864,490,180,60)`, `StandardSync (1052,492,208,56)`, RND `STYLE/RATE/DIV/SYNC/STEREO`).
- `scripts/mpc-skin-extras/82_play.py`:
  - `:198-232`: Switch `compactNativeSmoothWithIndicator`/`Readout` from `active_text_child` to `on_off_label.label_face` + `on_off_label_child` (using updated `PLAY_SMOOTH_RECTS` 56 px knob + 60×56 On/Off label + 66×28 value).
  - `:233-246`: Fix `playOscSelector` `neutral=True` -> **`neutral=False`** (`REVIEW-SKIN-NEWLOOK:511-512`), pass `dropdown_look="legacy"`, `dropdown_size="selector.dropdown.size.40"` to both `playOscSelector` (`w=296`), `playOscSelectorRouted` (`w=224`, Option A), and `playModSelector` (`w=324`).
  - `:260-276`: Emit Sub-gated `Play Osc Select` (`w=296`, `IndexedEnabling/0/5/Parameter 2676`) and ABC/Noise-gated `Play Osc Select Routed` (`w=224`, `RangedEnabling/1/4/5/Parameter 2676`) under Option A; keep `Play Env Select` `(454, 10, 324, 56)` and `Play LFO Select` `(872, 10, 332, 56)`.
- `scripts/native_play/osc.py`:
  - `:123-128`: Replace `playOscChoiceField` with `build_titled_selector(..., slot=(200, 60), title_height=20, selector_height=40, title_justification="left verticallyCentred", dropdown_look="legacy", dropdown_size="selector.dropdown.size.40")` (or per-caption `playOscTypeField` `"TYPE"` / `playNoiseSoundField` `"SOUND"` so each has its exact title above left). Update `playOscRouteField` to `40 px` visual face inside `(72, 56)` slot (Option A, `8 px` left pad).
  - `:129-146`: `playOscNumberField` (`OCT`): reads updated `geometry["sub"]["second"] = [306, 451, 116, 56]` -> `oct_w = 116, oct_visual_h = 40, oct_top = 8`.
  - `:147-150`: Replace `playSubWaveField` with `build_titled_selector(..., slot=(284, 60), title_height=20, selector_height=40, caption="WAVE", title_justification="left verticallyCentred", dropdown_look="legacy", dropdown_size="selector.dropdown.size.40")` (removes caption-inside-box & wave icon inside box per `AGENTS.md` dropdown rules).
  - `:151-162`: Replace `playOscTableField` (`previous`/`browse`/`next` arrow steppers!) and `playOscContentField` with titled `SOURCE` dropdown (`(200, 60)`, title `"SOURCE"` above left, `40 px` legacy dropdown face, value centered, chevron right, single full-box transparent browse `Button` with `buttonId=2, count=4` so C++ `SourceBrowse = 2` contract is preserved with zero arrows).
  - `:164-166`: Replace 90 px `pan_definition`/`level_definition` with 56 px `blue_dial_definition(ctx.skins_dir, *_style_size("play.osc.dial.geometry"), bipolar=True/False)`.
  - `:168-171,212-214`: Generate 40×40-in-56×56 Play header POWER (`play_header_power_on.png`, `play_header_power_off.png` — do NOT overwrite `osc_native_power_*.png` which is `64 × 56` and shared with standalone Osc `56_osc_power.py:20`!) and EXPAND (`action_button(..., height=40, icon="expand")`).
    - **Owner confirmation item (POWER OFF colour):** `icon_button.md:8` / `header.md:25` specifies POWER off = `text.icon`, whereas `56_osc_power.py:30` and `TODO.md:215` / `ui_screen_host.cpp:328` specify red (`accent.red`) when off. Default to `accent.red` on `play_header_power_off.png` to preserve `TODO.md:215` until owner explicitly unifies with `icon_button.md:8`.
  - `:176-185`: Bind Noise `Filter` to `playOscFilterDial` (`200 × 60`, 56 px dial), `pan` to `playOscPan` (`200 × 94`, 56 px), `level` to `playOscLevel` (`200 × 94`, 56 px).
- `scripts/native_play/env_lfo.py`:
  - `:73-118`: **CRITICAL:** Mirror the exact `on_off_label` Smooth definition update here (remember `88_play_env_lfo.py` runs AFTER `82_play.py` and overwrites `compactNativeSmoothWithIndicator`/`Readout` in `localComponentDefinitions`!). Update `compactNativeDial` and `compactNativeRiseDial` to read the new 56 px `play.env.dial.geometry` and `play.rise.dial.geometry`.
  - `:137-141`: `playEnvLfoRate` reads updated `play.rate.dial.geometry` (`180 × 60`, 56 px).
  - `:144-151`: `playRndStereo` uses `on_off_label_definition("STEREO", stereo_size, skins_dir=ctx.skins_dir, handle="Data")` (`396 × 60`).
  - `:163-194`:
    - `CHOOSE`: `selector_definition(..., selectors[2:], ..., dropdown_look="legacy", dropdown_size="selector.dropdown.size.40")` (`(332, 56)` slot, `40 px` centered face).
    - `SHAPE`, `DIVISION`, `STYLE`, `SYNC`: use `build_titled_selector(..., title_height=20, selector_height=40, caption=LABELS[field], title_justification="left verticallyCentred", dropdown_look="legacy", dropdown_size="selector.dropdown.size.40")` (`(396, 60)` for SHAPE/STYLE/SYNC, `(180, 60)` for DIVISION).
  - `:212-216`: Replace `create_segmented_radio_button_group(..., grid=True)` with `radio_grid.build_radio_grid_entries(...)` (equal `64 × 40` buttons, `8 px` gaps, disjoint `68/72/68 × 56` slots).
  - `:217-219`: Generate dedicated `play_header_expand.png` (`40 × 40` icon in `56 × 56` slot) — do **NOT** overwrite `lfo_native_expand.png` (`64 × 56`, shared with standalone LFO tabs in `53_lfo_smooth.py:168`)!
- `scripts/render_play_screen_preview.py:42-55` & `scripts/render_play_native_snapshot.py:13,329-363`:
  - Update `DISPLAY_BOXES` (`y = 74..402`), `NATIVE_BUTTON_APERTURES` (move to header `y = 10..66`), and `render_play_native_snapshot.py` handling of `playOscSelectorRouted`, titled above-left dropdowns, 56 px dials (`play_sub_level_56.png`, `play_sub_pan_56.png`), `on_off_label`, and `radio_grid`.

---

## 3. Risks, Host-Freeze Guardrails, and Verification Split

### 3.1 Host-Freeze & Regression Risks (Lessons Learned)

1. **MPC Host Freeze / Key 61 Reboot Pattern (`TODO.md:383`, commits `ac2f3acb` / `dffa4cdf`):**
   - *Fact:* Merging `agy/mixer-labels` froze MPC XL project load at `"Loading Track Outputs 31/32"` (18%) and rebooted Key 61 ~1 min after install. The TUI diff showed `99_shared_labels.py:47-52` had rewritten a `Label` inside a component definition to a `sharedNativeLabel*` wrapper and flipped its `showWhenDataModelInvalid` from `"Hide"` to `"Show"`.
   - *Rule for Play Phase 2:*
     - **Do NOT alter component definition nesting structure** on existing Play controls (`Knob` + `Label` + `Focus` directly inside dial definitions; `Image` + `Label` inside selector definitions; `Button` directly on tab for POWER/EXPAND/RadioGrid).
     - **Keep all existing handle names and remapping keys intact:** `Data`, `Caption`, `Active`, `State`, `NativeActiveGate0`, `NativeActiveGate1`, `FromGesture/Parameter N`. Never change `showWhenDataModelInvalid` from `"Hide"` to `"Show"` on gated/remapped wrappers.
     - **Audit `99_shared_labels.py:31-35` output diff** before any device deploy: `99_shared_labels.py` only matches Labels with `showWhenDataModelInvalid == "Hide"`. New titled-dropdown `name` labels (`selector.py:753`) and dial `name`/`value` labels use `showWhenDataModelInvalid == "Show"`, so `99_shared_labels.py` will skip them — verify in the generated TUI.json diff that zero Play labels were wrapped in `sharedNativeLabel*`.
2. **Hook Execution Order Clobbering (`82_play.py` vs `88_play_env_lfo.py` vs `99_zz_dial_text.py`):**
   - *Fact:* `88_play_env_lfo.py` (`env_lfo.py:106-133`) overwrites `compactNativeDial`, `compactNativeRiseDial`, `compactNativeSmoothWithIndicator`, `compactNativeSmoothTime`, and `compactNativeSmoothReadout` in `localComponentDefinitions` AFTER `82_play.py` creates them (root cause of `"the SMOOTH On/Off change has no effect in TUI.json"`, commit `ac2f3acb`). Both scripts MUST build from the same single helper or `82_play.py` must not duplicate the Smooth definition.
   - *Fact:* `99_zz_dial_text.py` (`level.py:1114-1140`) runs last and forces every dial `Name` and `Value` `Label` to `DIAL_TEXT_STYLE` (`font.value = 20`, `text.value`). Because `level.py:1125` only treats `key.endswith("Off") or key.startswith("NativeInactive")` as inactive, `compactNativeSmoothReadout` currently gets overwritten to bright `text.value` (`REVIEW-SKIN-NEWLOOK:501`). Fix `level.py:1125` to include `key.endswith("Readout")` (and preserve `label.dial.56` caption/value font heights `15 / 22` if owner wants distinct caption/value sizes rather than uniform `20`).
3. **Standalone Tab Collateral Damage via Shared Assets & `playrows::`:**
   - *Fact:* `osc_native_power_on/off.png` (`56_osc_power.py:20`) and `lfo_native_expand.png` (`53_lfo_smooth.py:168`) are `64 × 56` and shared by standalone Osc/LFO tabs. Play Phase 2 MUST write new `56 × 56` assets (`play_header_power_on/off.png`, `play_header_expand.png`) instead of overwriting the standalone PNGs.
   - *Fact:* Standalone `OscillatorFullScreen` (`screen.h:517`, `osc_ui.h:240`), `LfoFullScreen` (`lfo_screen.h:495,514`), and `FilterUi` (`filter_ui.h:264`) call `playrows::powerRect`, `playrows::fullRect`, `playrows::plotRect`. Changing those functions in-place would break standalone pages — add dedicated `header*Rect` and `compactPlotRect` helpers instead.
4. **Page Detection, Standby/Blur, and Popup Markers:**
   - *Fact:* Play page markers sit at tab `y = 1..6` (`overlay_layout.json:60-81`) and sentinel at tab `y = 2..6` (`99_backgrounds.py:62`); popup markers sit at screen `y = 120` on full-screen `MenuOverlay` popups (`controller.h:120-137`, `95_popup_markers.py:11`).
   - *Risk:* Any header widget whose bounds creep above tab `y = 10` (e.g., `y = 0..8`) will overwrite markers/sentinel and break `PageDetector::check` and `HostBackgroundGate` (causing permanent opaque overlay or missing overlay). Keeping header slots strictly at tab `y = 10..66` guarantees `3 px` clearance (`y = 7..9`).
5. **TUI.json Size & Decoded Filmstrip RAM (`DIAL-MEASUREMENT-2026-11-27.md:94,103,118-121`):**
   - *Fact:* Adding `play_sub_level_56.png`, `play_sub_level_56_disabled.png`, `play_sub_level_56_inactive.png`, `play_sub_pan_56.png`, `play_sub_pan_56_disabled.png` adds ~356 KB PNG / ~7.97 MB decoded while other screens still hold 60/90 px strips (full 4-size unification later saves 50% PNG / 61% decoded).
   - *Fact:* Global `TUI_JSON_BYTES_MAX = 750000` (`test_mpc_skin_extras.py:32`) is already exceeded on HEAD (1,859,999 B, `TODO.md:75,290`). Replacing `playOscTableField`'s 3-button arrow stepper with 1 dropdown and deduplicating `playSubPan`/`playOscPan` keeps Play Phase 2 TUI.json delta near zero or negative (`HYPOTHESIS`: `-1..3 KB` net; measure on generated `TUI.json` before deploy).
6. **`play_env_lfo_host.cpp:545` Pre-existing Failure (`TODO.md:301`):**
   - *Fact:* Two variants of ENV3/LFO4/RND2 expanded CLOSE checks already fail on baseline `develop`. Do not confuse this pre-existing expanded-view bug with compact Play Phase 2 geometry regressions.
7. **Osc A/B/C/Noise Option A Dual Selector Gating (`HYPOTHESIS`):**
   - Splitting `Play Osc Select` into Sub (`w=296`, `IndexedEnabling/0/5/Parameter 2676`) and A/B/C/Noise (`w=224`, `RangedEnabling/1/4/5/Parameter 2676`) introduces an extra `RangedEnabling` gate on the panel selector. On MPC hardware, verify that switching `Sub <-> Osc A` immediately swaps selector width without a blank/unresponsive frame; if the host drops focus on swap, fall back to Option C (`ROUTE` at graph top-right `(350, 82, 64, 56)`).
8. **Touch Slop on `40 px` Visual Faces Inside `56 px` Slots (`HYPOTHESIS`):**
   - Every slot is `>= 56 × 56`, satisfying lint rule (b), but the visible face of POWER/EXPAND is `40 × 40` and dropdown boxes are `40 px` high. On Key 61's capacitive screen, verify that rapid thumb taps on the outer `8 px` padding reliably trigger POWER, EXPAND, and RadioGrid cells without accidental neighbor hits.

### 3.2 Offline Verification vs Device Verification

| Check | Offline (Mac) | Device (Key 61 `armv7`) |
|---|---|---|
| Exact rect containment, zero overlaps, `>= 56 × 56` touch targets across all 5 Osc × 6 Env × 14 LFO/RND states | Yes (`test_native_play_osc.py`, `test_mpc_skin_play_env_lfo.py`, `test_play_native_smooth_active_text.py`, `layout_lint_host`) | Spot-check visual alignment |
| C++ <-> JSON contract parity (`play_osc_native_contract.h`, `play-env-lfo-contract.json`) | Yes (`scripts/native_play/osc.py --check`, `lfo_ui_host --play-env-lfo-skin-contract`) | — |
| `TUI.json` structural diff (no `sharedNativeLabel*` on Play, no flipped `showWhenDataModelInvalid`, `validate_synthyboy_skin.py`, SDK schema) | Yes (`test_mpc_skin_no_fallback.py`, `test_mpc_skin_overlay.py`, `validate_synthyboy_skin.py`, TUI diff script) | — |
| Overlay graph transparency / zero aperture leaks / `plotRect` parity across all 3 thirds | Yes (`ui_screen_host`, `ui_env_host`, `ui_lfo_host`, `osc_ui_host`, `controller_host`, `page_view_host`, `render_play_screen_preview.py`) | Confirm host + overlay composite via `mpc-screen-photo` |
| Host boot/project-load stability (no 18% freeze, no reboot) | No (cannot emulate Akai JUCE host) | **Required (Key 61 first, owner approval before XL)** |
| Physical touchscreen ergonomics (`40 px` visual in `56 px` slot, `64 × 40` RadioGrid, `60 × 56` SMOOTH On/Off label) | No | **Required** |
| Real JUCE font rendering (`font.host.height.per.em = 1.521`), ellipsis truncation before chevron, no clipping on long wavetable/shape names | Approximate in Pillow (`render_play_native_snapshot.py`) | **Required** |
| Hardware Q-Link banks 1..4 (4-knob Key 61 bank switching, Gen 1 unnumbered `Q` frame clearance above `controlsLine = 422`) | Partial (`qslots_host`, `controller_host`) | **Required** |
| Standby dim/blur recovery & page-switch markers (`y = 1..6`) with new header at `y = 10..66` | Partial (`page_detector_host`, `standby_host`) | **Required** |

---

## 4. Disjoint-File Batch Plan for Parallel Execution

> **Execution Topology:**
> - **Batch 1 (Primitives & Tokens)** lands first (small foundation touching `components/`).
> - **Batch 2A (Skin & Osc Contract)** and **Batch 2B (C++ Play Geometry & Overlay)** own **100% disjoint file sets** and execute **in parallel** once Batch 1 merges (sharing the exact Section 1 coordinates as their agreed interface).
> - **Batch 3 (Env/LFO Contract Sync, Previews & Cross-Layer Tests)** runs after 2A + 2B merge to regenerate `play-env-lfo-contract.json` from the compiled `lfo_ui_host`, update shared tests/previews, and run the targeted offline gate.

### Batch 1 — Shared Tokens & Native Component Primitives *(Sequential prerequisite)*
- **Owned files (disjoint):**
  - `components/sizes.json` (add/update `play.*`, `smooth.play.*`, `level.play_smooth_rects.*` tokens from Section 2.2A)
  - `components/native/selector/selector.py` (fix `:374` chevron inset bug; allow `40 px` dropdown centered in `56 px` slot at `:457-475`; extend `build_titled_selector` `:640-763` with `title_justification="left verticallyCentred"`, `40 px` child box, `legacy` look)
  - `components/native/button/button.py` (add `height=None` pass-through in `_store_action_face` / `action_button` `:222-238`)
  - `components/native/level/level.py` (add `bipolar=False` to `blue_dial_definition` `:1031-1054`; fix `unify_dial_text` `:1125` for `*Readout`)
  - `components/native/radio_grid/radio_grid.py` (add `build_radio_grid_entries` native skin builder with equal `64 × 40` faces and `>= 56 px` disjoint slots)
- **Exact verification command:**
  ```sh
  python3 -m unittest scripts/tests/test_native_selector.py scripts/tests/test_native_titled_factories.py scripts/tests/test_native_button_catalog.py scripts/tests/test_native_radio_grid_catalog.py scripts/tests/test_native_label_catalog.py scripts/tests/test_native_dial_catalog.py
  ```

### Batch 2A — Play Skin & Osc Contract *(Parallel with Batch 2B)*
- **Owned files (strictly disjoint from Batch 2B):**
  - `scripts/mpc-skin-extras/play-osc-native-contract.json`
  - `shell-mpc/play_osc_native_contract.h` *(generated by `python3 scripts/native_play/osc.py` — Batch 2B reads it via `contractRect()` without editing it)*
  - `scripts/mpc-skin-extras/82_play.py`
  - `scripts/native_play/osc.py`
  - `scripts/native_play/env_lfo.py`
  - `scripts/mpc-skin-extras/play-env-lfo-contract.json` *(written with the exact Section 1.3–1.4 numbers so skin tests pass immediately; re-verified byte-for-byte against `lfo_ui_host --play-env-lfo-skin-contract` in Batch 3)*
- **Exact verification command:**
  ```sh
  python3 scripts/native_play/osc.py --check && \
  MPC_SDK=/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk \
  python3 -m unittest scripts/tests/test_native_play_osc.py scripts/tests/test_mpc_skin_play_env_lfo.py scripts/tests/test_play_native_smooth_active_text.py scripts/tests/test_native_play_smooth.py scripts/tests/test_mpc_skin_no_fallback.py
  ```
  *(Plus a one-off read-only check comparing generated `TUI.json` before/after to confirm zero `sharedNativeLabel*` wrapping on Play, zero `showWhenDataModelInvalid` flips, and valid `validate_synthyboy_skin.py`.)*

### Batch 2B — C++ Play Geometry, Apertures & Touch *(Parallel with Batch 2A)*
- **Owned files (strictly disjoint from Batch 2A):**
  - `shell-mpc/ui/play_rows.h` (`kControlsH = 196`, `kControlGap = 8`, `header*Rect`, `compactPlotRect`; standalone `powerRect`/`fullRect`/`plotRect` untouched)
  - `shell-mpc/ui/screen.h` (`OscillatorCompact::plotRect`, `layout`)
  - `shell-mpc/ui/composite.h` (`BasicControls::layout` stacked branch, `minSize`)
  - `shell-mpc/ui/env_screen.h` (`EnvCompact` `dialMax = 56`, header & 2×2 190×94 grid, `compactPlotRect`)
  - `shell-mpc/ui/lfo_screen.h` (`LfoCompact` `kQuadRandomH = 280`, `kRowH = 60`, `kRateH = 60`, header & 8 px row gaps)
  - `shell-mpc/ui/lfo_parts.h` (`RateControl::kModeW = 208`, `8 px` mode gap, `60 px` compact division height)
  - `shell-mpc/ui/lfo_display.h` (suppress overlapping compact band text when `d.area.y < r.y + playrows::kBandH`)
  - `shell-mpc/overlay/lfo_ui.h` (`compactData` uses `compactPlotRect`)
  - `shell-mpc/overlay/play_view.h` (`compactGround` fallback `70.0f`, `dialTop = 634.0f`, `214..222`)
- **Exact verification command (via `buildslot.sh` on integration / targeted host builds):**
  ```sh
  bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh \
    "cmake --build build --target ui_screen_host ui_env_host ui_lfo_host ui_display_host osc_ui_host lfo_ui_host controller_host page_view_host layout_lint_host -j4 && \
     ./build/shell-mpc/ui/ui_screen_host && ./build/shell-mpc/ui/ui_env_host && ./build/shell-mpc/ui/ui_lfo_host && ./build/shell-mpc/ui/ui_display_host"
  ```

### Batch 3 — Tests, Contract Lock-Step & Preview Sync *(Sequential after 2A + 2B merge)*
- **Owned files:**
  - `scripts/tests/test_native_play_osc.py` (update expected rects, 56 px strip checks, titled WAVE/TYPE/SOURCE/SOUND checks, remove arrow-button assertions on `playOscTableField`)
  - `scripts/tests/test_mpc_skin_play_env_lfo.py` (update RadioGrid 68/72/68 slot assertions, `play_header_expand.png`, and `SYNTHYBOY_LFO_UI_HOST` contract lock-step check)
  - `scripts/tests/test_play_native_smooth_active_text.py` & `scripts/tests/test_native_play_smooth.py` (update `(1066, 558, 194, 60)`, `play_sub_level_56[_inactive].png`, `on_off_label` assets)
  - `scripts/tests/test_mpc_skin_no_fallback.py` (update Play expected definition types if `playOscSelectorRouted` / `playOscFilterDial` / `playOscLevel` are added)
  - `shell-mpc/ui/tests/ui_screen_host.cpp`, `ui_env_host.cpp`, `ui_lfo_host.cpp`, `ui_display_host.cpp` (update compact Play assertions to `header*Rect` / `compactPlotRect`, keeping standalone assertions on `fullRect` / `powerRect`)
  - `shell-mpc/overlay/tests/osc_ui_host.cpp`, `lfo_ui_host.cpp`, `controller_host.cpp`, `page_view_host.cpp` (update compact rect assertions from `404`/`72` to `439`/`60` and `524`/`94`)
  - `scripts/render_play_screen_preview.py`, `scripts/render_play_native_snapshot.py`, `scripts/tests/test_render_play_screen_preview.py`
- **Exact verification commands:**
  1. Contract lock-step check:
     ```sh
     SYNTHYBOY_LFO_UI_HOST=./build/shell-mpc/overlay/lfo_ui_host \
     MPC_SDK=/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk \
     python3 -m unittest scripts/tests/test_mpc_skin_play_env_lfo.py scripts/tests/test_native_play_osc.py scripts/tests/test_play_native_smooth_active_text.py scripts/tests/test_native_play_smooth.py scripts/tests/test_mpc_skin_no_fallback.py scripts/tests/test_render_play_screen_preview.py
     ```
  2. Overlay & layout lint check (through `buildslot.sh`):
     ```sh
     bash /Users/szabi/Documents/Projects/Safian/.hw-local/tools/buildslot.sh \
       "./build/shell-mpc/overlay/osc_ui_host && ./build/shell-mpc/overlay/lfo_ui_host && ./build/shell-mpc/overlay/controller_host && ./build/shell-mpc/overlay/page_view_host && ./build/shell-mpc/overlay/layout_lint_host"
     ```

---

## 5. Key 61 Walk — Acceptance Checklist

> **Pre-deploy gate:** Build `--arch arm32` ONLY (`AGENTS.md` release packaging rule). Save the currently working Key 61 package under `/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-builds/` before installing. Capture both host-only and host+overlay composite photos (`mpc-screen-photo` skill) and store raw measurements under `.hw-local/mpc-perf/`.

### 5.1 Boot, Load & Page Detection Stability
- [ ] **Clean project load:** MPC Key 61 loads `Sbtest` / `Acid Saw Reso` to 100% with zero freeze, zero reboot, and `overlay-status.txt` reporting `play1`, valid slot, and `host_background: on (sentinel read)`.
- [ ] **Top 10 px marker/sentinel strip:** No header widget clips or covers tab `y = 0..9`; `drawNativeMarkerCovers` hides markers cleanly; leaving Play (`Play -> Osc A -> Env -> LFO -> Play`) shows zero stale overlay frames.
- [ ] **Overlay off / back on (`AGENTS.md` display-path rule):** With overlay disabled, the host-only Play screen shows all 3 headers, all native dials, dropdowns, RadioGrid, and clean dark graph wells with no stray host artifacts; re-enabling overlay restores all 3 live curves cleanly inside the `y = 74..402` graph frames.
- [ ] **Standby dim & wake:** 1-minute screen dim keeps overlay aligned; waking returns to the exact Play state and selected instances.

### 5.2 Panel Header Band (`y_tab = 10..66`, `40 px` Content, `8 px` Pad All Round)
- [ ] **Visual consistency across all 3 panels:** `Play Osc Select` (blue gradient, NOT flat grey), `Play Env Select`, and `Play LFO Select` align on the exact same `y_screen = 128..168` (`40 px`) baseline with centered value text and right chevron; zero `<`/`>` stepper arrows anywhere.
- [ ] **Osc panel header (`Sub`):** `[POWER 40×40][Sub dropdown 296×40][EXPAND 40×40]` with visible `8 px` margins on all 4 sides and `8 px` gaps.
- [ ] **Osc panel header (`Osc A/B/C/Noise`):** `[POWER 40×40][dropdown 224×40][ROUTE 64×40][EXPAND 40×40]` (Option A) with zero overlap, readable route label (`F1`, `F1+F2`, `M`), and zero blank frame when switching `Sub <-> Osc A`.
- [ ] **Env & LFO panel headers:** `[Env 1 dropdown 324×40][EXPAND 40×40]` and `[LFO 1 dropdown 332×40][EXPAND 40×40]` — no POWER button, right edges aligned with graph frames.
- [ ] **Touch ergonomics:** Tapping the outer `8 px` edge of POWER and EXPAND triggers reliably without opening the adjacent dropdown.

### 5.3 Graphs & Freed Height (`+108 px` Plot Height)
- [ ] **Three-panel alignment:** Osc, Env, and LFO display frames share the exact same top (`y_screen = 184`) and bottom (`y_screen = 512`), and all 3 live plots span `y_screen = 194..492` (`298 px` tall) with zero button holes inside the graph frames.
- [ ] **Osc live waveforms:** Sub sine/phase dots, Osc A wavetable cycle + stack, Sample/Granular min-max overview + playhead, Spectral spectrum, and Noise colour fill the full `298 px` plot cleanly; when POWER is OFF, curve dims and live animation stops.
- [ ] **Env interactive graph:** All DAHDSR handles and curve handles are draggable across the taller `298 px` plot; voice dots track playing notes; no clipping of ruler labels at the bottom edge (`y_tab = 382..402`).
- [ ] **LFO / RND live display:** Built-in & drawn LFO shapes + voice cursors fill the `298 px` plot without any overlapping top-left text; RND 1..4 traces fill the `230 px` plot cleanly.

### 5.4 Panel 1 (Osc) Body Controls
- [ ] **Sub:** `WAVE` titled dropdown (title `WAVE` above left, value centered, chevron right, factory `MenuOverlay` list — row tap marks, header tap applies & closes); `OCT` `name_value` box aligned flush with `WAVE` box (`y_tab = 459..499`), opens `-4..+4` keypad; `PAN` & `LEVEL` `56 px` dials with right-side caption/value.
- [ ] **Osc A / B / C:** `TYPE` titled dropdown (Wavetable..Spectral); `SOURCE` titled dropdown (no `<`/`>` arrows!) opens the wavetable/content browser on tap, long names ellipsize `4 px` before chevron without touching it; `PAN` & `LEVEL` `56 px` dials.
- [ ] **Noise:** `SOUND` titled dropdown; `FILTER` `56 px` dial aligned beside it; `PAN` & `LEVEL` `56 px` dials.
- [ ] **POWER OFF write protection (`TODO.md:215`):** Switching POWER OFF dims `WAVE`/`TYPE`/`SOURCE`/`SOUND`/`OCT`/`FILTER`/`PAN`/`LEVEL` (and `ROUTE`) to disabled styling; dragging/tapping/wheel/Q-Link on disabled controls produces zero parameter writes; POWER and `Play Osc Select` remain operable; turning POWER back ON restores stored values.

### 5.5 Panel 2 (Env) Body Controls
- [ ] **2 × 2 `56 px` ADSR grid:** `ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE` all render `56 px` dials with right-side caption/value, readable on Key 61 without truncation, bottom row aligned flush with Osc `PAN`/`LEVEL`.
- [ ] **Env switching & EXPAND/CLOSE:** Switching `Env 1 -> Env 3 -> Env 1` via header dropdown updates curve and ADSR dials; tapping header `EXPAND` opens full Env editor, tapping `CLOSE` returns cleanly to compact Play.

### 5.6 Panel 3 (LFO / RND) Body Controls
- [ ] **`SHAPE` titled dropdown:** Title `SHAPE` above left, centered value, chevron right, no `<`/`>` arrows; opens factory `MenuOverlay` list; selecting `Drawn` shows drawn curve.
- [ ] **`HZ | BPM | KEY` RadioGrid `1 × 3`:** Three equal `64 × 40` legacy/active gradient buttons with visible `8 px` gaps:
  - `HZ` active: `RATE` `56 px` dial visible and editable.
  - `BPM` active: `DIVISION` titled dropdown visible, opens factory division list.
  - `KEY` active: `RATE`/`DIVISION` slot hidden; Q-Link slot 2 does not write.
- [ ] **`RISE` & `SMOOTH` (`56 px` dials + `on_off_label`):**
  - `RISE` `56 px` dial edits `fade_in`.
  - `SMOOTH` `on_off_label` button (`60 × 56`) toggles ON (cyan `active_text.on`, active `56 px` dial, bright value) <-> OFF (dim `active_text.off`, inactive grey `56 px` dial, dimmed value — confirming `unify_dial_text` fix!); dragging dial while OFF writes zero parameter changes; tapping `SMOOTH` button does not accidentally drag the `56 px` knob or hit the value label.
- [ ] **`RND 1..4` via header dropdown:** Switching `LFO 1 -> RND 1 -> LFO 1` swaps to `STYLE` / `RATE` (or `DIVISION`) / `SYNC` titled dropdowns + `STEREO` On/Off label, hiding `RISE` and `SMOOTH`; switching back restores `LFO 1` controls cleanly.
- [ ] **LFO EXPAND/CLOSE:** Tapping header `EXPAND` opens full LFO editor; `CLOSE` returns cleanly to compact Play.

### 5.7 Hardware Q-Link & Screen Capture Sign-off
- [ ] **4-knob Q-Link banks (Key 61):** Tapping Osc / Env / LFO bodies moves the Gen 1 unnumbered `Q` bank frame to that panel's controls block (`y_tab = 422..618`), with the unnumbered `Q` marker sitting cleanly inside the `20 px` gap (`y_tab = 402..422`) without overlapping the display frame. Turning physical knobs Q1..Q4 writes the active panel's 4 targets (Osc Basic / Env ADSR / LFO Rate).
- [ ] **Refresh canonical screen image (`AGENTS.md` Screen image library):** Capture updated Key 61 composite photo to `docs/screens/play.jpg` (never touching `docs/screens/play_old.jpg`).
