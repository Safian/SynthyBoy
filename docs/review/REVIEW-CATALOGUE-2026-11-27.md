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
