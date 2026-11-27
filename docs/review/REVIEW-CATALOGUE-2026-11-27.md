# Component Catalogue Review (2026-11-27)

- **Branch / HEAD**: `agy/review-cat-2` at `2d59b1d9`
- **Pre-2026-11-27 baseline**: `605ef0e2`
- **Scope**: Read-only audit of the 2026-11-27 native component catalogue work (`components/native/*`, `components/colors.json`, `components/sizes.json`, `components/style.py`, `docs/ui-components/*.md`, `docs/ui-components/kepes-katalogus.html`, `scripts/gen-native-component-catalog.py`, and `scripts/tests/test_native_*catalog*.py`) against the owner decisions in `AGENTS.md:51-70`. No builds or tests were run and no source files were modified.

---

## 1. Catalogue Images & Examples vs Owner Decisions

### 1.1 Control look (old gradient design, not flat) & stale cards

- **PASS (2026-11-27 rendered components)**: All 16 components updated or added on 2026-11-27 (`label`, `on_off_label`, `pan`, `level`, `button`, `icon_button`, `selector`, `filter_selector`, `header`, `radio_list`, `radio_grid`, `slide_switch`, `stepped_dial`, `rotary_selector`, `wave_icons`, `mixer_strip`, `hslider`) have stored PNGs in `components/native/*/img/` that are byte-identical to their current renderers.
- **DEVIATION — `MacroCard` catalogue image still shows a flat button**: `components/native/macro_card/img/catalog-macro-card.png` (last committed `7f16db44`, 2026-11-26) contains **3,099 pixels** of flat `button.surface` (`#151a2d` = `(21, 26, 45)`) and **0 pixels** of `button.gradient.top` (`#131930`) or `button.gradient.bottom` (`#0d1122`). It was not regenerated after `ca1ad4b0` switched `control.look` to `"legacy"`.
- **DEVIATION — `RadioButton (KIVEZETÉSRE)` images are stale (2026-11-24)**: `components/native/radio_button/img/catalog-radio-56.png`, `catalog-radio-env-group.png`, and `catalog-radio-voice-group.png` (last committed `27e711cc`) still show the old `radio.surface.top` / `radio.surface.bottom` gradient (`(24, 31, 52)` → `(16, 21, 36)`), not the `button.gradient.*` look now produced by `components/native/radio_button/radio_button.py:42-51`.
- **DEVIATION — `Tune` card predates the gradient switch**: `components/native/tune/img/osc-tune-compact-card.png` was last committed `4fd1ab00` (2026-11-25), before `ca1ad4b0` switched `tune.py` MORE/TRACK buttons to the legacy gradient.
- **DEVIATION — Dropdown vs Button gradient mismatch vs `AGENTS.md:53`**: `AGENTS.md:53` states dropdowns keep `#131930` at the top to `#0d1122` at the bottom and cites tokens `selector.surface` and `selector.border`. In code:
  - `components/native/button/button.py:113-114` uses `button.gradient.top` (`#131930`) → `button.gradient.bottom` (`#0d1122`) with border `button.gradient.border` (`#151c3a`) → `button.gradient.border.bottom` (`#161e42`). Note that `(21, 28, 58)` → `(22, 30, 66)` gets slightly **lighter** toward the bottom, contradicting the comment at `components/native/button/button.py:61` ("bright top edge, dark bottom edge").
  - `components/native/selector/selector.py:339-344` uses `selector.dropdown.top` (`#141a38`) → `selector.dropdown.bottom` (`#0b0f24`) with border `selector.dropdown.border` (`#27306a`), ignoring `selector.surface` (`#0e1b33`, `components/colors.json:41`) and referencing no `selector.border` (missing token).
- **DEVIATION — Catalogue palette (`kepes-katalogus.html` Színpaletta)**: `components/colors.json:271-308` still renders `button.surface` (`#151a2d`, "Gombfelület"), `button.border` (`#26304b`, "Gombkeret"), and `radio.surface.top` (`#1a2238`, "Inaktív felület") instead of the active legacy gradient tokens (`button.gradient.*`, `selector.dropdown.*`).

### 1.2 Header (`components/native/header/header.py`, `catalog.json`)

- **Matches**:
  - Title comes first on the left, followed by POWER (`components/native/header/header.py:122-127`).
  - Single margin token `header.padding = 8` on all four sides, `header.height = 56`, content row height = `40` px (`components/native/header/header.py:43-47`; `components/sizes.json:855-857`).
  - Flexible dropdown (`"fill": true`) expands to fill the available space between left and right zones (`components/native/header/header.py:137-143`).
  - No arrow steppers; `CLOSE` is an X `IconButton`; `MORE` is a text `Button`.
- **Minor deviations / observations**:
  - The Play panel example (`components/native/header/catalog.json:102-119`) has no separate `title` — POWER is at `x = 8` and the dropdown serves as the panel title (documented exception in `docs/ui-components/header.md:24`).
  - `components/native/header/header.py:118` centres `title_face` vertically inside the 40 px content row; because Titillium Web line box (`ascent + descent`) at `font.page_title = 26` em can exceed 40 px by 1 px, `y` can be `7` while ink stays inside the 40 px band (`scripts/tests/test_native_header_catalog.py:49-50`).

### 1.3 Dropdown / Selector (`components/native/selector/selector.py`, `catalog.json`)

- **Matches**:
  - Short values centred, title above left-aligned with the box (`components/native/selector/selector.py:1515`), overflow truncated at the end with `…` and left-aligned (`components/native/selector/selector.py:1423-1427,1460-1464`).
  - No left/right arrow steppers in `components/native/selector/catalog.json`.
  - Variant 3 (`inside-left`, `components/native/selector/catalog.json:77-87`) is marked `KIVEZETÉSRE`.
- **Deviations**:
  1. **Value centering is shifted ~10–11.5 px left of box centre**: `components/native/selector/selector.py:402` sets the value rect to `(pad, 0, width - pad - chevron_lane - chevron_gap, height)`. At 220×40 px (`pad_x = 12`, `chevron_lane = 28`, `chevron_gap = 4`), the text rect spans `x = [12..188]` (midpoint `100.0`) instead of the 220 px box midpoint `110.0`. Measured on `"Env 1"`: ink spans `x = [75..122]` (midpoint `98.5`).
  2. **Actual chevron gap is 11.5–26.0 px to the chevron ink, not 4 px**: `components/native/selector/selector.py:374` places the chevron centre at `width - chevron_lane / 2 = 206` (`chevron_width = 13` at 40 px → left ink at `199.5`), while line `402` subtracts `chevron_gap` (`4`) from the *chevron lane* edge (`192`). Measured on `"Filter 1 Cutoff Modulation Amount"` at 220 px width: `text_x = [11..183]` (32 px), `[13..183]` (40 px), `[15..172]` (48 px), leaving `18.0 px`, `16.5 px`, and `26.0 px` to chevron ink respectively (minimum enforced clearance to chevron ink is `11.5 px` at 40 px).
  3. **No disabled dropdown sample**: `components/native/selector/catalog.json` has 7 variants (none disabled), even though `render_dropdown_face(..., neutral=True)` (`components/native/selector/selector.py:347,368-370`) and `dim_artwork` (Header POWER-off, MixerStrip POWER-off) exist.
  4. **Skin factory `build_titled_selector` centre-aligns its title**: `components/native/selector/selector.py:742` sets `"justification": "horizontallyCentred verticallyCentred"` on the outer title `Label`, violating `AGENTS.md:57` ("the title is left aligned (above the box)"). Similarly `selector_definition` (`components/native/selector/selector.py:577`) defaults non-navigable value justification to `"left verticallyCentred"`.

### 1.4 Button & IconButton (`components/native/button/`, `components/native/icon_button/`)

- **Matches**:
  - `Button` (`components/native/button/catalog.json:8-54`) shows 32 / 40 / 48 / 56 px in the legacy gradient look plus 48 px disabled.
  - `IconButton` (`components/native/icon_button/catalog.json:8-259`) shows `POWER` on/off, `EXPAND`, `CLOSE` (X) at 32 / 40 / 48 / 56 px filled, 48 px disabled, and ghost at 40 / 48 px.
- **Deviations**:
  1. `components/native/button/catalog.json` has disabled only at 48 px (not 32 / 40 / 56 px) and **no `look="active"` sample**, even though `components/native/button/button.py:79,90-91,115-117` defines `look="active"` (used by `RadioGrid` and drawer rows).
  2. `components/native/icon_button/catalog.json` has **no ghost `EXPAND` sample**, **no ghost disabled sample**, and **no ghost 32 px sample** — even though `MixerStrip` uses a 32 px ghost `POWER` button (`components/native/mixer_strip/mixer_strip.py:1086`).
  3. `components/native/icon_button/icon_button.py:43-45`: section predicates `("Disabled", lambda i: i["state"] == "disabled")` and `("Ghost: ...", lambda i: i["look"] == "ghost")` are not mutually exclusive; any future ghost+disabled sample will appear twice in the HTML.

### 1.5 Dials (`pan`, `level`, `label`, `stepped_dial`)

- **MAJOR DEVIATION — `Pan` and `Level` cards omit 36 / 48 / 56 px sizes**: `components/native/pan/catalog.json:15-30` and `components/native/level/catalog.json:15-30` contain ONLY `normal` (`90 px`) and `compact` (`40 px`). The 36 / 40 / 48 / 56 px dial sizes are shown visually only in `SteppedDial` (`components/native/stepped_dial/catalog.json:50-78`) and as a single 48 px example + text-only table in `Label` (`components/native/label/catalog.json:76-152`), not on `Pan` or `Level`.
- **DEVIATION — `Pan` and `Level` text variants and `state_images` lack disabled states**: `components/native/pan/catalog.json:31-82` and `components/native/level/catalog.json:31-82` only render enabled `right` / `bottom` / `top` layouts at 90 px; disabled labelled dial examples exist only at 48 px inside `components/native/label/catalog.json:103-152`.

### 1.6 MixerStrip (`components/native/mixer_strip/`) & HSlider (`components/native/hslider/`)

- **Matches**: Name + ghost `POWER` on top, `S` / `M` row on channel strip (`NOISE`), omitted on `MASTER`, no tab bar/peek, variant A (`knobs`) vs variant B (`sliders` / `HSlider`), contrasty fader thumb, stereo meter, POWER-off disabled state, S/M active state.
- **Deviations**:
  1. **MASTER `-12.0 dB` thumb does NOT match the `-12` scale tick**: `components/native/mixer_strip/catalog.json:104-105` sets `"level": 0.4511` (`((-12 + 60) / 66) ** 2.5`), but `components/native/fader/fader.py:258` places the `-12` tick at `unity_n * 0.7 = 0.5516` (+0.1005 too high) and `-24` at `unity_n * 0.45 = 0.3546` (true skew-dB value is `0.2197`, +0.1349 error). In `mixer-strip-master.png`, the thumb at `-12.0 dB` sits visibly below the `-12` mark.
  2. **MASTER has `"destination": "OUT"` dropdown (`components/native/mixer_strip/catalog.json:102`)** despite `components/native/mixer_strip/mixer_strip.py:24` stating MASTER lacks a destination selector (terminal output). Needs owner confirmation whether `OUT` is a real dropdown or static text.
  3. **No `BUS` strip example** (`S`/`M` + destination + full-height fader + no bottom sends) in `components/native/mixer_strip/catalog.json`.
  4. **Active `S`/`M` rendering bypasses `button.action_art` for its keyline**: `components/native/mixer_strip/mixer_strip.py:1062-1073` (`sm_face`) draws via `action_art(..., look="legacy")`, downsamples to 1x, then re-upscales 4x to draw the `accent.cyan` / `accent.red` outline using hardcoded `"button.size.32"` radius and downsamples again.
  5. **`HSlider`** (`components/native/hslider/catalog.json:8-18`): matches spec (bipolar centre/left-50 with centre tick, unipolar 60%, disabled); minor: no min (`0.0`) or max (`1.0`) edge sample.

### 1.7 Radio Family (`radio_list`, `radio_grid`, `slide_switch`, `stepped_dial`, `rotary_selector`, `wave_icons`)

- **Matches**: All five compact selector components use the legacy gradient / Button recipe, include disabled states, and output pixel-accurate footprint tables (`Helyigény`).
- **Minor gaps**:
  - `components/native/stepped_dial/catalog.json` omits a 2-stop example (only 3, 4, 6, 8 stops shown, though `max_stops = 8` and `2..8` is supported).
  - `components/native/rotary_selector/catalog.json` has no `sh` or `pipa` icon example (though `components/native/rotary_selector/rotary_selector.py:30` registers `"pipa"` and docs mention `sh`).
  - `components/native/wave_icons/catalog.json` has no disabled sheet.

---

## 2. Token Hygiene

### 2.1 Duplicate Definitions / Duplicate JSON Keys (Bugs)

- `components/sizes.json:154-155`: Duplicate JSON key `"control.look": "legacy"` appears twice consecutively.
- `components/style.py:39-64`: `CONTROL_LOOKS = ("legacy", "flat")` and `def control_look():` are duplicated verbatim at lines `39-50` and lines `53-64`.

### 2.2 Literals in Renderers & Templates Instead of Tokens

- `scripts/templates/native-component-catalog.html:29,33,34,37`: Hardcoded hex `#465064` (literal copy of `catalog.divider`) at line 29, plus literal pixel CSS values (`1px` at line 29; `320px`, `12px`, `8px` at line 33; `8px` at line 34; `16px` at line 37). Violates `docs/ui-components/README.md:57` and `components/AGENTS.md:15`.
- `components/native/selector/selector.py:1353-1356`: Hardcoded `height=48` and `(height - 48) // 2` in `append_choice_rows` instead of `_style_size("height.medium")`.
- `components/native/selector/selector.py:789`: Hardcoded `font_size > 8` and `w - 8` in `filter_chain_definition`.
- `components/native/selector/selector.py:979`: Hardcoded `800` instead of `_style_size("screen.height")`.
- `components/native/selector/selector.py:1343`: Hardcoded `"colour": "0"` instead of `_style_native_color("transparent")`.
- `components/native/mixer_strip/mixer_strip.py:1083,1097,1110,1117` and `components/native/hslider/hslider.py:78`: Hardcoded `width=1` instead of `_style_size("stroke.hair")` (and `mixer_strip.py:1070` hardcodes `"button.size.32"` instead of `f"button.size.{height}"`; `mixer_strip.py:1120` uses `cell_gap * 2`).
- `components/native/hslider/hslider.py:115`: Hardcoded `"144 px széles"` in section heading instead of `{size:hslider.width}`.
- `components/native/filter_selector/catalog.json:14,20,26,32`: Hardcoded `"56 × 48 px"` instead of `{size:...}` placeholders.
- `components/native/label/label.py:106,127`: Hardcoded default `diameter=48` and heading `"48 px-es tárcsa"`.
- `components/native/radio_list/radio_list.py:24`, `components/native/radio_grid/radio_grid.py:22`, `components/native/slide_switch/slide_switch.py:25`, `components/native/rotary_selector/rotary_selector.py:28`: Hardcoded height/diameter tuples `(32, 40, 48)` and `(48, 56)` instead of resolving from `height.*` tokens (unlike `components/native/stepped_dial/stepped_dial.py:27`, which reads `_style_size("stepped_dial")["diameters"]`).
- `components/native/radio_button/radio_button.py:89`: Hardcoded `(32, 40, 48)` and `32`.
- `components/native/fader/fader.py:260-261,265,268`: Hardcoded tick offsets `12`, `4`, `3` and stroke widths `2`/`1`.
- `components/native/active_text/active_text.py:69`: Hardcoded `4 * scale` right padding for `anchor="rm"`.
- `components/native/solo_mute/solo_mute.py:101`: Dead expression `_style_size("scale.half") if False else 2`.

### 2.3 All New Colour Tokens Added on 2026-11-27 (vs `605ef0e2`)

All **11** colour tokens added to `components/colors.json` on 2026-11-27, their stated justifications, and Euclidean RGB distance checks ($\le 6.0$) against pre-existing and sibling tokens:

| Token (`components/colors.json`) | Value | Justification | Duplicate / Close Existing Tokens ($\le 6$ RGB Euclid) |
|---|---|---|---|
| `button.gradient.top` (`:42`) | `#131930` | Measured top fill of old "+ ADD" button (`button.md:7`) | **FLAG**: `surface.panel` / `mod_matrix.surface` / `mod_row.surface` / `mod_detail.panel` `#121930` (**1.00**), `theme.aurora.panel0` `#121a30` (**1.41**), `field.surface` / `mod_row.field_surface` `#12182d` (**3.32**), `button.surface` / `mod_row.button_surface` `#151a2d` (**3.74**), `menu.surface` `#141a34` (**4.24**), `filter.pz.active.surface` `#161d31` (**5.10**), `mixer.sentinel` `#141a35` (**5.20**), `screen.lfo_smooth.colour` `#171d32` (**6.00**) |
| `button.gradient.bottom` (`:43`) | `#0d1122` | Measured bottom fill of old "+ ADD" button (`button.md:7`) | **FLAG**: `route.button.surface` `#0d1222` (**1.00**), `radio.surface.bottom` `#0e1220` (**2.45**), `selector.dropdown.bottom` `#0b0f24` (**3.46**, new), `theme.aurora.panel1` `#0a1020` (**3.74**), `theme.neon.panel1` `#0c1026` (**4.24**), `theme.neon.bg0` `#0b1026` (**4.58**), `theme.aurora.bg0` `#0a0f1e` (**5.39**) |
| `button.gradient.border` (`:44`) | `#151c3a` | Measured top keyline of old "+ ADD" button (`button.md:7`) | **FLAG**: `route.surface` / `theme.neon.panel0` / `selector.dropdown.top` `#141a38` (**3.00**), `menu.row` `#161e38` (**3.00**), `fader.thumb.line` `#181f39` (**4.36**, new), `surface.focus` `#172038` (**4.90**), `mixer.sentinel` `#141a35` (**5.48**) |
| `button.gradient.border.bottom` (`:45`) | `#161e42` | Measured bottom keyline of old "+ ADD" button (`button.md:7`) | **FLAG**: `theme.aurora.track` `#182044` (**3.46**), `theme.aurora.grid` `#182046` (**4.90**). Also only 8.31 from `button.gradient.border` (`#151c3a`) and *lighter*, not darker |
| `selector.dropdown.top` (`:46`) | `#141a38` | Legacy dropdown top fill (`selector.py:337`) | **EXACT DUP (0.00)** of `route.surface` `#141a38` and `theme.neon.panel0` `#141a38`! Also `mixer.sentinel` `#141a35` (**3.00**), `button.gradient.border` `#151c3a` (**3.00**), `menu.surface` `#141a34` (**4.00**), `menu.row` `#161e38` (**4.47**) |
| `selector.dropdown.bottom` (`:47`) | `#0b0f24` | Legacy dropdown bottom fill (`selector.py:337`) | **FLAG**: `theme.neon.bg0` `#0b1026` (**2.24**), `theme.neon.panel1` `#0c1026` (**2.45**), `button.gradient.bottom` `#0d1122` (**3.46**, new), `route.button.surface` `#0d1222` (**4.12**), `theme.aurora.panel1` `#0a1020` (**4.24**), `radio.surface.bottom` `#0e1220` (**5.83**) |
| `selector.dropdown.border` (`:48`) | `#27306a` | Legacy dropdown keyline (`selector.py:337`) | **EXACT DUP (0.00)** of `dial.track`, `mod_row.track_border`, and `theme.neon.keyline` (all `#27306a`)! |
| `selector.dropdown.border.bottom` (`:49`) | `#27306a` | Legacy dropdown bottom keyline (`selector.py:343`) | **EXACT DUP (0.00)** of `selector.dropdown.border`, `dial.track`, `mod_row.track_border`, and `theme.neon.keyline` (`#27306a`)! Redundant (top == bottom) |
| `fader.thumb.fill` (`:115`) | `#c2d1e8` | Contrasty fader thumb body measured from `docs/screens/mixer.jpg` (`mixer_strip.md:34`) | None ($\le 6$ RGB) — genuinely distinct high-contrast thumb fill |
| `fader.thumb.edge` (`:116`) | `#edf4fa` | Contrasty fader thumb edge (`mixer_strip.md:34`) | **FLAG**: `catalog.text` `#f0f2fa` (**3.61**), `text.value` / `mod_row.amount_thumb` / `mod_row.text_value` / `theme.neon.text` `#eef1ff` (**5.92**) |
| `fader.thumb.line` (`:117`) | `#181f39` | Dark centre line on contrasty fader thumb (`mixer_strip.md:34`) | **FLAG**: `surface.focus` `#172038` (**1.73**), `menu.row` `#161e38` (**2.45**), `radio.surface.top` `#1a2238` (**3.74**), `route.segment.off` `#19223c` (**4.36**), `button.gradient.border` `#151c3a` (**4.36**, new) |

**Summary**: **10 of 11** new colour tokens fall within 6 RGB Euclidean distance of existing tokens, and **3 of 11** (`selector.dropdown.top`, `selector.dropdown.border`, `selector.dropdown.border.bottom`) are **exact 0.0-distance duplicates** of existing `theme.neon.panel0` / `theme.neon.keyline` (with `selector.dropdown.bottom` only 2.45 away from `theme.neon.panel1`). Only `fader.thumb.fill` (`#c2d1e8`) introduces a distinct visual colour.

### 2.4 Unused / Redundant Tokens Introduced Today & Missing Referenced Tokens

- **Redundant / catalogue-only tokens introduced on 2026-11-27**:
  - `components/colors.json:49` (`selector.dropdown.border.bottom`): Exact duplicate of `selector.dropdown.border`; causes `button.py:58` (`line_top == line_bottom`) to skip the border gradient completely.
  - `components/sizes.json:157-158` (`selector.dropdown.gradient.neon`, `selector.dropdown.gradient.legacy`): Both are `{"ref": "control.gradient"}`; identical indirections.
  - `components/sizes.json:861-865` (`label.dial.{36,40,48,56,90}`): Read ONLY by `components/native/label/label.py:86` to render the static text table in `label/catalog.json`; unused by `level.py` or `pan.py` dial renderers.
  - Note on dynamic size tokens: `button.size.{32,40,48,56}` (`sizes.json:159-162`) and `selector.dropdown.size.{32,40,48}` (`sizes.json:169-171`) are looked up dynamically via `f"...size.{height}"`. All are referenced at runtime (`selector.dropdown.size.32` is used by `selector/catalog.json:10` and `mixer_strip.py:1094`).
  - Note on `named_sizes` (`components/sizes.json:1442-1455`): Omits `height.intermediate` (40 px) despite 40 px being the primary size across Button, IconButton, Dropdown, Header, RadioList, RadioGrid, SlideSwitch, SteppedDial, and HSlider.
- **Tokens referenced in docs/code but MISSING from `colors.json` / `sizes.json`**:
  - `AGENTS.md:53` cites `selector.border` — **missing** from `components/colors.json` (`selector.surface` exists at `colors.json:41` but is unused by dropdowns).
  - `components/native/solo_mute/solo_mute.py:101` references `"scale.half"` inside `if False` — **missing** from `components/sizes.json`.
  - `scripts/render_mod_screen_preview.py:372` calls `style.size("selector.dropdown.font")` — **missing** (`selector.dropdown` at `sizes.json:151` is a dict containing `"font"`).
  - `docs/ui-components/native-mixer-strip.md:152` cites `focus.field` — **missing** from `components/colors.json` (`field.focus` exists at `colors.json:20`).
  - (`scripts/tests/test_native_header_catalog.py:30` references `"header.side"` inside `assertRaises(ValueError)` — intentional negative check, not a bug.)

---

## 3. Docs vs Code Accuracy & Orphaned Files

### 3.1 Wrong or Stale Statements in `docs/ui-components/*.md` (and Module Docstrings)

- **`docs/ui-components/button.md:3,5-9`**:
  - Lines 3 & 9 claim "The skin keeps its flat buttons for now (the skin switch is a separate, later round)" and "`flat` (default; today's skin face...)". **WRONG/STALE**: `components/sizes.json:154` sets `control.look = "legacy"`, and `components/native/button/button.py:87,98` defaults `look=None` to `_control_look()` = `"legacy"`.
  - Lines 5–9 omit `look="active"` (`components/native/button/button.py:79,90-91,115-117`), which is already used by `RadioGrid` and choice drawers.
  - Line 7 calls `button.gradient.border` (`#151c3a`) → `button.gradient.border.bottom` (`#161e42`) a "light-blue keyline", and `button.py:61` comments "bright top edge, dark bottom edge", whereas `#161e42` is brighter than `#151c3a`.
- **`docs/ui-components/header.md:29,33,44` (plus `components/native/header/header.py:5,69`)**:
  - `header.md:44` claims "The skin's dropdowns keep the `neon` look." **WRONG/STALE**: `components/native/selector/selector.py:353-354` redirects default `look="neon"` to `"legacy"` whenever `control.look == "legacy"`.
  - `header.md:29` ends with a dangling fragment `` `font.page_title` title text. `` after the parenthetical.
  - `header.md:33` lists `name?` as a `centre` field (`{kind: dropdown|button, name?, value?, ...}`), which `header.py:85-99` never reads, while omitting the supported `fill?` and `height?` fields (`header.py:93,140`).
  - `components/native/header/header.py:5` docstring says `LEFT = POWER ... and the page title` (reversed order; `header.py:122-127` puts title first, then POWER); line `69` says `POWER 48 x 48` (stale; `power_face` renders at `height` = `40` px).
- **`docs/ui-components/label.md:3,10-11,21-27` (plus `components/native/label/catalog.json:43-74`)**:
  - `label.md:3` states "No screen uses this module"; commit `194e84a8` updated `mixer_strip.py:222-246` to follow shared Label styles (`font.value.large` / `DIAL_TEXT_STYLE`), though via tokens rather than importing `label.py`.
  - `label.md:10-11,24,27` claim `pan.sub_dial_definition` (`15`/`24`, `text.caption`/`text.value`) and `pan.compact_pan_definition` (`14`/`16`) are currently "in use". **STALE**: commit `3328da40` (`level.py:172-184,333,492,496,524,553,558,1114-1140` and `pan.py:142,146,174,203,208`) unified all skin dial captions and values onto `DIAL_TEXT_STYLE` (`font.value = 20`, `text.value = #eef1ff`). `label/catalog.json:43-74` still shows the superseded `15`/`24` sizes as `Tárcsa-felirat` / `Tárcsa-érték`.
- **`docs/ui-components/on_off_label.md:8` (plus `components/native/on_off_label/on_off_label.py:3`)**:
  - Claims "Smooth: the SMOOTH caption of the Smooth dial will be an instance of this component. The smooth tokens (`smooth.*`) stay until that switch is made." **STALE**: commits `0a1c8eeb` and `4d466bd2` already added `on_off_label_definition` (`on_off_label.py:67-108`) and wired `scripts/mpc-skin-extras/53_lfo_smooth.py` to use it.
- **`docs/ui-components/filter_selector.md:7`**:
  - Claims the chip face uses "`theme.neon` panel gradient, keyline". **STALE**: `components/native/selector/selector.py:786` calls `render_dropdown_face(*size, chevron=False)` with default `look="neon"`, which `selector.py:353-354` upgrades to `"legacy"` (`selector.dropdown.top/bottom/border`); the stored PNGs were regenerated in `ca1ad4b0`.
- **`docs/ui-components/radio_list.md:3`**:
  - Claims "The existing RadioButton entry is unchanged." **STALE**: `components/native/radio_button/catalog.json:4-5,38-47` was modified in `dd609247` (`order: 51`, title `"RadioButton (KIVEZETÉSRE)"`, footprint table added).
- **`docs/ui-components/radio_grid.md:3`**:
  - Claims "the skin switch is a separate round" and calls `radio_button.py` "flat segmented faces". **STALE**: `ca1ad4b0` switched `radio_button.py` to legacy gradient, and `3328da40` already switched LFO/Play HZ/BPM/KEY in the skin to `render_grid_cell_art` (RadioGrid cells).
- **`docs/ui-components/radio_family.md:3,30`**:
  - Line `3` calls `radio_button.py` "flat cells" (now legacy gradient) and claims "the catalogue pages keep the old RadioButton at the end of the list" — **WRONG**: `order: 51` places it at #17 of 23 (in the middle, before `NameValue`, `MacroCard`, `ActiveText`, `Tune`, `Meter`, `Header`).
  - Line `30` says "Every example of the four new entries has its own 'Helyigény' table" — **WRONG**: there are **five** new entries listed above (`RadioList`, `RadioGrid`, `SlideSwitch`, `SteppedDial`, `RotarySelector`), and all five have a `Helyigény` table. (All 8 numeric footprint rows at `radio_family.md:21-28` were verified against `sample_size()` and are accurate.)
- **`docs/ui-components/rotary_selector.md:30`**:
  - Native skin recipe states `render_dial_frame(diameter, i / (N - 1))` — **WRONG**: omits `value_arc=False, stops=N` (or `stepped_dial.render_frame(diameter, i, N)`), which would draw a continuous gradient value arc without stop dots.
- **`docs/ui-components/mixer_strip.md:16,38`**:
  - Line `16` claims MASTER fader height is **442 px** ("254 px with variant A, 294 px with B, 442 px on MASTER"). **WRONG**: `mixer_strip.layout(master)` produces `track = (91, 88, 8, 478)` (**478 px**, off by 36 px; 254 and 294 are accurate).
  - Line `38` states MASTER scale is `(+6, 0, -12, -24, -60)` — matches `fader.py:258` labels, but `fader.py:258` misplaces `-12` and `-24` using `unity_n * 0.7` and `unity_n * 0.45` instead of `skew_db`, whereas `mixer_strip.py:442` draws `(-60, -30, -18, -6, 0, 6)` with true `skew_db`.
- **`docs/ui-components/native-mixer-strip.md:37-39,152,178`**:
  - Stale pre-2026-11-27 doc: claims `mixer_strip/catalog.json` has `approved: false` (`:178`, actually `approved: true`), describes amber Solo `#e5a93c` (`:37`, actually `accent.cyan` in `solo_mute.py:113`), references non-existent token `focus.field` (`:152`), and links old `catalog-mixer-strip.png` (`:111`).
- **`docs/ui-components/icon_button.md`, `slide_switch.md`, `stepped_dial.md`, `wave_icons.md`, `hslider.md`**: Verified accurate against their implementations.
- **`docs/ui-components/README.md:23,57,172-188`**:
  - Line `23`: Lists `catalog-order.json` `first` as only 6 items (`Label, On/Off label, Pan, Level, Button, Selector`); `components/native/catalog-order.json:2-13` actually has **10** items (adds `icon_button`, `filter_selector`, `mixer_strip`, `hslider`).
  - Line `23`: Lists `catalog_sections(entry)` keys as only 4 (`label, on_off, dropdown, header`); `scripts/gen-native-component-catalog.py:24` has **14** keys in `EXAMPLE_KEYS`.
  - Line `57`: Claims `scripts/templates/native-component-catalog.html` contains no copied palette; line `29` has literal `#465064`.
  - Lines `172-188`: Still documents the old `Selector` with optional inside `name` caption and `build_titled_selector` (which centre-aligns the title).
- **`scripts/render_native_catalog_cards.py:263-277`**: `main()` does NOT call 8 of the new 2026-11-27 renderers (`render_radio_list_cards`, `render_slide_switch_cards`, `render_wave_icons_cards`, `render_stepped_dial_cards`, `render_rotary_selector_cards`, `render_radio_grid_cards`, `mixer_strip.render_catalog`, `hslider.render_catalog`), despite `docs/ui-components/README.md:59` presenting it as the single card-refresh script.

### 3.2 Dropdown Docs & the `10 px` vs `4 px` Chevron Gap

- **Missing dedicated dropdown doc**: Unlike every other 2026-11-27 component, there is **no `docs/ui-components/selector.md` or `dropdown.md`**; dropdown documentation is scattered across `docs/ui-components/header.md:42-48` and `docs/ui-components/README.md:23,170-201`.
- **`10 px` vs `4 px` chevron gap**: `components/sizes.json:153` was updated from `10` (`f5460dbe`) to `4` (`d6a3032b`), `docs/ui-components/header.md:48` matches (`4 px`), and `components/native/selector/catalog.json:50,73` substitutes `{size:selector.dropdown.chevron_gap}` (`4 px`). No active doc still says `10 px`. **However**, `selector.py:402` subtracts `chevron_gap` (`4 px`) from the *chevron lane* (`chevron_lane = 28 px`, starting at `width - 28 = 192`), NOT from the chevron glyph ink (`width - 14 - chevron_width/2 = 199.5` at 40 px). Thus the actual minimum clearance to chevron ink is **10–13 px** (`11.5 px` at 40 px; measured `16.5 px` in `dropdown-center-none-40-long.png` and `18.0 px` at 32 px), and `scripts/tests/test_native_label_catalog.py:153,157` tests against chevron ink (`199.5`) rather than the lane edge (`192`).

### 3.3 Orphaned Files, Broken Symlinks, Dead Code & Stale Test References

- **10 broken symlinks in `docs/ui-components/komponensek/`**:
  `level-90.png`, `level-native-{32,40,48,56}.png`, `pan-90.png`, and `pan-native-{32,40,48,56}.png` point to removed files in `components/native/{level,pan}/img/` — and all 10 are still listed in `scripts/screen-assets.json:5-9,14,16-19`.
- **25 orphaned images in `components/native/*/img/` (not in any `catalog.json` and 0 code/doc references)**:
  - `components/native/selector/img/osc_routing_frame.png`, `osc_routing_frame_48.png` (2 files)
  - `components/native/meter/img/mixer_meter_{192,408}_{idle,peak}.png` (4 files)
  - `components/native/meter_block/img/mixer_meter_{192,408}_{idle,peak}.png` (4 files; `meter_block/img/` also duplicates all 8 files of `meter/img/`)
  - `components/native/solo_mute/img/solo_mute_{fx,m,s}_{disabled,off,on,implicit}.png` (11 files, superseded by filmstrips)
  - `components/native/mod_row/img/mod_amount_464x56.png`, `mod_slider_revealed_{348,392}x56.png`, `mod_slider_track_348x56.png` (4 files)
- **Superseded preview images in `mixer_strip/img/`**: `catalog-mixer-strip.png`, `source_strip_before_after.png`, `variants_comparison.png` (only referenced by stale `native-mixer-strip.md` / `render_native_mixer_strip_preview.py`), plus duplicate `mixer_fader_cap.png` (same asset as `fader/img/mixer_fader_cap.png`).
- **Stale test referencing removed/shrunk file & defunct worktree**: `scripts/tests/test_native_katalogus.py:11-12,40` targets `docs/ui-components/native-katalogus.html` (which is only 7,303 bytes while line 40 asserts `> 50000` bytes) and hardcodes `/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-pan-level-lfo-preview/docs/ui-components/native-katalogus.html`.
- **Dead function**: `components/native/filter_selector/filter_selector.py:46-51` defines `catalog_sections()` taking 0 args; it is not in `gen-native-component-catalog.py:24` `EXAMPLE_KEYS` and is never called by the generator (only compared against static `filter_selector/catalog.json` in `test_native_filter_selector.py:47`).

---

## 4. Test Quality

### 4.1 Tautology / Self-Comparison Bug

- **`scripts/tests/test_native_header_catalog.py:138-141`** (`test_page_headers_fill_their_flexible_control_up_to_the_right_group`):
  When `fills[0]` is NOT the last item in `example["centre"]` — which applies to **4 of the 6** fill examples (`osc-a`, `lfo-9`, `osc-a-off`, `lfo-9-full`, all followed by a `button`) — the `else` branch evaluates:
  ```python
  self.assertEqual(..., max(p.x + p.face.width for p in centre) + m["gap"] * 0, max(p.x + p.face.width for p in centre))
  ```
  This asserts `X == X` and **never checks** that the centre zone reaches `right[0].x - m["gap"]` when a button follows the flexible dropdown.

### 4.2 Vacuous / Weak Assertions & Side Effects

- **`scripts/tests/test_native_label_catalog.py:183-184`** (`test_legacy_fill_has_the_voice_selector_contrast_and_neon_is_untouched`):
  Calls `selector.render_dropdown_face(220, 48)` (default `look="neon"`) and asserts the top pixel `(20, 10)` equals `(20, 26, 56)` (`#141a38`) as proof that `neon` is untouched. Because `selector.dropdown.top` (`#141a38`) is an exact duplicate of `theme.neon.panel0` (`#141a38`), the test completely misses that `selector.py:353-354` actually redirected `look="neon"` to `"legacy"` (bottom `#0b0f24` instead of `theme.neon.panel1` `#0c1026`).
- **`scripts/tests/test_native_label_catalog.py:153-162`** (`test_long_value_is_cut_left_aligned_and_keeps_chevron_gap`):
  Computes `chevron_left` from the chevron glyph ink (`199.5`) rather than the chevron lane edge (`192`), and only requires `max(ink) > width - 28 - gap - 40` (`> 148 px`). It never verifies that an ellipsis character (`…`) is actually drawn or that `chevron_gap = 4` (not `10`) is tight against the lane.
- **`scripts/tests/test_native_dial_catalog.py:106-109`** (`test_html_is_current`):
  Executes `scripts/gen-native-component-catalog.py` directly on the live working tree, overwriting `docs/ui-components/kepes-katalogus.html` as a side effect of running unit tests.
- **`scripts/tests/test_native_mixer_strip_design.py:79-86`** (`test_thumb_follows_the_level_and_is_the_contrasty_one`):
  Only verifies the thumb's pixel position matches `strip_design.layout(example)["thumb"]` (the renderer's own placement calculation), not that the thumb aligns with the scale tick for `level_text`. Consequently, the `-12.0 dB` thumb vs `-12` tick misalignment in `fader.py:258` went undetected. Also line `53` wraps `self.assertEqual(...) if "send0" in r else None` in a statement expression.

### 4.3 Stored-vs-Renderer Self-Comparisons & Missing Token-Mutation Tests

- **Every** catalogue test file (`test_native_{button,dial,header,label,radio_grid,radio_list,rotary_selector,slide_switch,stepped_dial,wave_icons}_catalog.py`, `test_native_filter_selector.py`, `test_native_mixer_strip_design.py`) verifies image bytes by comparing `stored(file)` against `module.render_sample(sample)` (a renderer self-check that catches stale PNGs, not renderer bugs).
- **0 of 12** catalogue test files include a **token-mutation check** (per `AGENTS.md:65`: changing a component's tokens loudly must change every rendered control) or exercise `SYNTHYBOY_CONTROL_LOOK=flat`.

### 4.4 Missing Negative Checks

- **3 of 12 catalogue test suites have zero `assertRaises` / negative validation checks**:
  - `scripts/tests/test_native_wave_icons_catalog.py`: never tests invalid `shape` (`render_wave_icon(..., shape="bad")`), `zoom < 1`, out-of-range `selected`, or duplicate IDs (`sheet_samples`).
  - `scripts/tests/test_native_filter_selector.py`: never tests invalid state inputs or missing assets.
  - `scripts/tests/test_native_mixer_strip_design.py`: never tests invalid `power` / `bottom` / `scale`, missing `sends` when `bottom != "none"`, duplicate IDs (`strip_examples`), invalid HSlider `state` or out-of-range `value`, or invalid `thumb_face` `index`.
- **Partial negative-check gaps in the remaining suites**:
  - `scripts/tests/test_native_button_catalog.py`: tests invalid `look` and `text + icon`, but never tests `button_samples()` validation (bad height token, bad state, duplicate IDs) or `width < minimum` (`button.py:108-109`).
  - `scripts/tests/test_native_header_catalog.py`: never tests `> 1` `"fill": true` centre controls (`header.py:135,179`), insufficient room for `"fill"` (`header.py:141`), invalid `power`, or missing `title`/`power`/`centre` (`header.py:177`).
  - `scripts/tests/test_native_rotary_selector_catalog.py`: never verifies that the active pictogram is `accent.cyan` and inactive pictograms are `selector.wave` (unlike `test_native_wave_icons_catalog.py:66-74`), nor that disabled has no cyan.
  - `scripts/tests/test_native_slide_switch_catalog.py`: never tests `length <= thickness` rejection (`slide_switch.py:133`), nor that disabled has no cyan.
  - `scripts/tests/test_native_katalogus.py`: broken against current `docs/ui-components/native-katalogus.html` (`len <= 50000` fails line `40`) and checks an obsolete worktree path (`line 12`).
