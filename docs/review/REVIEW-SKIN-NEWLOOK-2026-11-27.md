# Skin New Look Review (`ca1ad4b0^..80ccd996`) — 2026-11-27

**Scope:** Read-only code and token audit of the host-skin migration to the "legacy gradient" look:
- `ca1ad4b0` (*Skin controls take the legacy gradient look*)
- `3328da40` (*Play and skin controls from the components: dropdown faces, RadioGrid cells for HZ/BPM/KEY, one dial text style, drawer rows and S/M/FX on the Button recipe*)
- Merged in `80ccd996` (`git diff ca1ad4b0^..80ccd996 -- components scripts`: 30 files changed, +426 / -134 lines, also spanning `6363e68b` overlay control look and `d123fc75` / `6f4d8296` Osc source list cleanup).

**Project rules checked against:** `AGENTS.md`, `components/AGENTS.md`, and `docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md`.  
**Verification constraint:** Strictly read-only review — no builds run, no tests executed, no source files modified.

---

## 1. Behaviour Regressions

### 1.1 Invisible `Previous`/`Next` hit zones under a down-chevron (`v`) in `command_stepper_definition` and dead center on `oscDetailUnisonMode`

- `components/native/selector/selector.py:1410`:
  ```python
  face = selector_definition(None, "", slot, skins_dir, caption_text=caption, justification="horizontallyCentred verticallyCentred")   # a dropdown face; the Previous / Open / Next hit zones below stay
  ```
- `components/native/selector/selector.py:1413-1419`:
  ```python
  edge = _style_size("selector.hit.minimum")
  for command, (name, x, span) in enumerate((
      ("Previous", 0, edge),
      ("Open", edge, width - 2 * edge),
      ("Next", width - edge, edge),
  ), 1):
      face["componentsData"].append(button(name, (x, 0, span, height), hit, index=command, count=4))
  ```
- `components/native/selector/selector.py:508`:
  ```python
  base = render_dropdown_face(width, height, chevron=not (stepper or arrows or navigable), inset=0, neutral=neutral,
                              radius=_style_size('radius.control'))
  ```
- `scripts/mpc-skin-extras/57_osc_settings.py:152-153`:
  ```python
  if kind == "table":
      return command_stepper_definition(skins_dir, (width, height), caption)
  ```
- `scripts/mpc-skin-extras/58_osc_detail.py:146-147`:
  ```python
  mode = command_stepper_definition(ctx.skins_dir, (w, h), "MODE")
  mode["componentsData"] = [c for c in mode["componentsData"] if c["componentData"]["name"] != "Open"]
  ```

**Regression:** Dropping `arrows=True` in `command_stepper_definition` causes `selector_definition` (`components/native/selector/selector.py:508`) to pass `chevron=True` to `render_dropdown_face`, painting a dropdown down-chevron (`v`) at `x = width - 17` (`components/sizes.json:151`, `inset=3`, `chevron_lane=28`) while removing the visible `<` and `>` arrows. However, the three transparent `Button` hit zones remain:
1. On OSC table selectors (`scripts/mpc-skin-extras/57_osc_settings.py:153`), the visible dropdown `v` chevron sits **inside the 56 px `Next` hit zone** (`width - 56 .. width`), so tapping the chevron steps **Next** instead of opening the picker, while tapping the left 56 px (`0 .. 56`) unexpectedly steps **Previous** with no arrow affordance.
2. On `oscDetailUnisonMode` (`scripts/mpc-skin-extras/58_osc_detail.py:146-147`, `158×92` cell from `scripts/mpc-skin-extras/osc-native-detail.json:10`), deleting `"Open"` leaves a **46 px dead center (`x = 56..102`)** with no touch target at all, invisible `Previous`/`Next` zones on the flanks, and a misleading dropdown `v` chevron drawn directly over `Next`.

---

### 1.2 `unify_dial_text` clobbers disabled/inactive dial states on Mixer, Filter, and Play LFO Smooth

- `components/native/level/level.py:1124-1131`:
  ```python
  key = definition["key"]
  inactive = key.endswith("Off") or key.startswith("NativeInactive")
  for child in children:
      data = child["componentData"]
      if data["type"] != "Label" or data["data"].get("type") not in ("Name", "Value"):
          continue
      text_style = data["data"]["textStyle"]
      (dial_caption_style if data["data"]["type"] == "Name" else apply_dial_text_style)(text_style)
  ```
- `scripts/mpc-skin-extras/99_zz_dial_text.py:4-6`:
  ```python
  def apply(tui, qlinks, ctx):
      from components.native.level.level import unify_dial_text
      unify_dial_text(tui)
  ```

**Regression:** `unify_dial_text` runs after every screen hook and checks only `key.endswith("Off") or key.startswith("NativeInactive")`. Three Knob-containing disabled definitions do not match that naming check, so their labels are overwritten to full active `text.value` (`#eef1ff`, `font.value = 20.0`) instead of remaining dimmed:
1. **Mixer off/muted dials** (`components/native/mixer_strip/mixer_strip.py:705`): `inactive = kind + "Inactive"` produces `mixerSliderPAN56Inactive`, `mixerSliderB156Inactive`, and `mixerSliderB256Inactive` (ends with `"Inactive"`, not `"Off"`).
2. **Filter bypassed/off dials** (`scripts/mpc-skin-extras/98_filter_native.py:287`): `kind = f"filterNativeDial_{layout_tag}_{'bi' if bipolar else 'uni'}_{'on' if active else 'off'}_{w}x{h}"` (ends with `_{w}x{h}`, not `"Off"`), wiping out `smooth.value.off` (`#6e7a9b`, `98_filter_native.py:321`) and `text.status.off` (`#b9bdc9`, `apply_control_state` at `components/style.py:180`).
3. **Play LFO Smooth Off** (`scripts/mpc-skin-extras/82_play.py:232` & `scripts/native_play/env_lfo.py:117`): `"compactNativeSmoothReadout"` does not match either check, overwriting `smooth.value.off` (`#6e7a9b`) and font `14` (`components/native/level/level.py:911-912`) to active `#eef1ff` at `20.0`.

*(Meanwhile, `53_lfo_smooth.py:277` `lfoNativeField4_*Off` and `style.py:212` `NativeInactive *` do match, leaving inconsistent dimmed colours across screens: `#a3a3a3` from `dim_artwork` in `unify_dial_text` vs `#b9bdc9` `text.status.off` in `apply_control_state`.)*

---

### 1.3 `render_grid_cell_art` vertical-mode axis bug, asymmetric gaps, and crash/clipping risks

- `components/native/radio_button/radio_button.py:83-99`:
  ```python
  gap = _style_size("radio_grid")["gap"]
  along = width if not vertical else height
  left = 0 if ends[0] else gap // 2
  right = 0 if ends[1] else gap - gap // 2
  extent = (along - left - right)
  across = height if not vertical else width
  box = max(size for size in (32, 40, 48) if size <= across) if across >= 32 else across
  if vertical:
      face = action_art(text=label, width=width - 0, height=box, look="active" if active else "legacy",
                        colour=_style_color("radio.active.text")) if active else action_art(text=label, width=width, height=box, look="legacy")
      slot = Image.new("RGBA", (width, height))
      slot.alpha_composite(face, (0, left))
      return slot
  ```
- `scripts/mpc-skin-extras/53_lfo_smooth.py:150-152`:
  ```python
  vertical = width < 3 * _ui_style.size("lfo.dial.option_min_width")
  children = create_segmented_radio_button_group("LFO Sync", (0, 0, width, height),
                                                 ("HZ", "BPM", "KEY"), handle, ctx.skins_dir, vertical=vertical, grid=True)
  ```
- `scripts/mpc-skin-extras/lfo-native-contract.json:9`: `[1, 10, 0, 396, 368, 150, 168]` (`width = 150 < 3 * 56 = 168`, so `vertical=True`; tiled into three `150×56` slots).

**Regression:**
1. **Wrong axis for `box` in vertical mode:** `across` is `width` (`150`), so `box` selects `48` regardless of cell `height` (`56`) or `extent` (`52`, `48`, `52`).
2. **Asymmetric gaps on LFO Sync (`150×168`):** Each `48 px` face is composited at `(0, left)` with `left = 0, 4, 4`, placing `HZ` at `y = 0..48`, `BPM` at `y = 60..108`, and `KEY` at `y = 116..164`. The visual gaps are **12 px between HZ and BPM**, **8 px between BPM and KEY**, and **4 px empty at the bottom** instead of uniform `radio_grid.gap = 8 px`.
3. **Silent clipping / invalid token crash:** If any vertical cell has `height < 48`, `box = 48` overflows the slot vertically and clips; if any cell has `across < 32`, `box = across` passes a non-existent `button.size.<across>` token to `action_art` (`components/native/button/button.py:103`) and raises `ValueError`.
4. **Horizontal outer-vs-inner width asymmetry (`scripts/native_play/env_lfo.py:214-216`):** On Play (`210×56` from `scripts/mpc-skin-extras/play-env-lfo-contract.json:7`, three `70×56` cells), `HZ` and `KEY` lose `4 px` (`extent = 66`), while `BPM` loses `4 + 4 = 8 px` (`extent = 62`), leaving `BPM` only **1 px** above `action_art`'s `61 px` minimum width (`font.button = 18`, `padding = 24`; `components/native/button/button.py:106-109`).

---

### 1.4 `selector_definition` `plain` branch flip, `render_dropdown_face` chevron inset bug, and active Play OSC selector drawn grey (`neutral=True`)

- `scripts/mpc-skin-extras/82_play.py:233-243`:
  ```python
  for component_name, overlay_name, frame_asset, neutral, width in (
      (SELECTOR_COMPONENTS[0], OSC_SOURCE_OVERLAY_NAME, "play_osc_selector_frame.png", True, SELECTOR_RECTS[0][2]),
      (SELECTOR_COMPONENTS[1], "fx editor choice menu", "play_selector_frame.png", False, SELECTOR_RECTS[1][2]),
  ):
      bounds = (0, 0, width, _style_size("height.high"))
      selector_def = selector_definition(
          selector_source, overlay_name, bounds[2:], ctx.skins_dir,
          title="Data", frame_name="Play selector frame",
          filename=frame_asset, neutral=neutral, arrows=False, label_height=_style_size("play.font.24"),
          justification="horizontallyCentred verticallyCentred"
      )
  ```
- `scripts/native_play/env_lfo.py:175-187`: passes `neutral=False, arrows=False` on `(396, 56)` for `playEnvLfoChoice Select`.
- `components/native/selector/selector.py:468-471,480`:
  ```python
  elif slot_height == _style_size('selector.selector_definition.geometry') and not neutral and not stepper and not arrows and not neighbors and not steps:
      height = _style_size('selector.selector_definition.height')
  else:
      height = _style_size('selector.selector_definition.height.offset2')
  ...
  plain = not (frame or play_face or icon or name or caption_text or stepper or arrows or navigable or neutral)
  ```
- `components/native/selector/selector.py:350-352,368-375`:
  ```python
  `inset` overrides the inset (0: the box fills the slot, as the Play-panel selectors do). `radius` overrides the corner radius (the Play selectors use radius.control). `neutral` is the inactive / read-only
  face (selector.neutral.surface and border, flat grey): the Play OSC source selector and the disabled variants."""
  ...
  if neutral:
      fill = (_style_rgb("selector.neutral.surface"),) * 2
      line = (_style_rgb("selector.neutral.border"),) * 2
  ...
  if chevron:
      cx = (width - geometry["inset"] - geometry["chevron_lane"] / 2) * scale
  ```

**Regressions:**
1. **Top-bar geometry & radius mismatch on Play:** Setting `arrows=False` flipped `playModSelector` (`82_play.py:241`) and `playEnvLfoChoice Select` (`env_lfo.py:184`) from the non-plain branch into `plain=True`, which delegates to `dropdown_definition` (`selector.py:488`): **`56 px` tall (`top=0`), `inset=3`, `radius=theme.neon.rControl=10`, text rect `(5, 5, w-36, 46)`**. Meanwhile, `playOscSelector` right next to them has `neutral=True`, keeping `plain=False`: **`48 px` tall (`top=4`), `inset=0`, `radius=6` (`radius.control`), text rect `(14, 4, 362, 48)`**.
2. **Active Play OSC selector looks disabled:** Because `82_play.py:234` passes `neutral=True`, `render_dropdown_face` (`selector.py:368-370`) draws the active OSC source selector in flat grey (`#202020` fill, `#5a5a5a` border, `#909090` chevron), while its value text remains `#eef1ff` and its touch/popup actions remain active — visually presenting an active selector as disabled/read-only.
3. **Chevron inset bug in `render_dropdown_face` (`selector.py:374`):** When `inset=0` is passed (every non-plain `selector_definition` at `selector.py:508`), `cx` still subtracts `geometry["inset"]` (`3`), shifting the chevron 3 px inward relative to the box edge.
4. **Ignored `dropdown_look` (`selector.py:508`):** The non-plain `render_dropdown_face` call omits `look=dropdown_look`.
5. **Non-filter0 PNG save (`selector.py:548-551`):** `play_face=True` saves via `resized.save(...)` rather than `save_png_filter0(...)` (pre-existing).

---

### 1.5 `mod_row._label_entry` applies dial text style to non-dial Mod Matrix controls

- `components/native/mod_row/mod_row.py:86-95`:
  ```python
  def _label_entry(kind, bounds_rect, justification="horizontallyCentred verticallyCentred", font_size=_style_size("mod_row.font.label")):
      from components.native.name_value.name_value import _label_component
      name_mode = kind == "Name"
      label = _label_component(kind.lower(), kind, "Caption" if name_mode else "Data", bounds_rect,
                               font_size, _style_argb("mod_row.text_label" if name_mode else "mod_row.text_value"),
                               justification)
      from components.native.level.level import apply_dial_text_style, dial_caption_style
      (dial_caption_style if name_mode else apply_dial_text_style)(label["componentData"]["data"]["textStyle"])   # the dial text style
      label["bounds"]["showWhenDataModelInvalid"] = "Hide"
      return label
  ```
- Callers inside `mod_row.py`:
  - `mod_row.py:143-148`: `modNative_bar` amount readout passes `font_size=_style_size("mod_row.font.amount")` (`34`), which is immediately overwritten to `20.0`.
  - `mod_row.py:157-158`: `modNative_toggle` split-cell `Name` label loses `mod_row.text_label` (`#8fa2c0`) and becomes `#eef1ff` + Upper Case at `20.0`, identical in colour and size to the `Value` beside it.
  - `mod_row.py:161,180,187`: `modNative_toggle_72x56` (`18 -> 20`), `modNative_action_76x56` (`16 -> 20`, Upper Case).
  - `scripts/mpc-skin-extras/70_mod.py:213`: `title = _label("Value", _style_size("mod_picker.title"), ...)` (calls `editor_label` -> `_label_entry`) forced to `20.0`.

---

### 1.6 Non-dial labels in `57_osc_settings.py` and `58_osc_detail.py` forced to `font.value = 20.0`

- `scripts/mpc-skin-extras/57_osc_settings.py:72-76,149,157`:
  ```python
  if text is not None:
      data["type"] = "Text"
      data["text"] = text
  else:
      apply_dial_text_style(data["textStyle"])   # a dial's Name and Value labels: the one dial text style
  ```
  The only `text=None` callers of `_label` in `57_osc_settings.py` are `kind == "toggle"` (`line 149`: `_label("Name", (0,0,1,.25))`, a `14 px`-tall proportional label on `125×56`) and `kind == "action"` (`line 157`: `height=13` overwritten to `20.0`). Neither is a dial.
- `scripts/mpc-skin-extras/58_osc_detail.py:52-57`:
  ```python
  for name, y, size, colour in (("Name", 0, 14, style.argb("text.caption")), ("Value", 0.18, 17, style.argb("text.icon"))):
      data = {"version": 1, "type": name, "handleName": "Data", "textStyle": {"version": 1,
              "colour": colour, "font": {"version": 1, "name": _ui_style.size("font.family"), "style": _ui_style.size("font.style.semibold"), "height": size},
              "justification": "horizontallyCentred verticallyCentred", "case": "Original"}}
      apply_dial_text_style(data["textStyle"])
      kids.append(entry(name, "Label", (0.02, y, 0.96, 0.21), data, focus="No", proportional=True))
  ```
  `oscDetailStepper` is a stepper, not a dial: `apply_dial_text_style` turns `Name` bright white `#eef1ff` at `20.0` (losing `text.caption` differentiation, without Upper Case), and in a `158×92` tile both rects are `152×19.3 px` with vertical overlap (`Name` `y = 0..19.3`, `Value` `y = 16.6..35.9`).

---

### 1.7 Lost/degraded pressed & disabled states and standalone `compact_level`/`compact_pan` mismatch

- **Tune MORE pressed state (`components/native/tune/tune.py:226-235,282-287`):**
  In legacy mode, `render_tune_more_button(pressed=True)` calls `_gradient_button_face((w, h), scale, pad, pressed=pressed)`, which keeps the identical `button.gradient.top/bottom` fill as unpressed (losing `field.focus` `#202a44`) and replaces `tune.accent` (`#5bdee9`) with `accent.cyan` (`#2ee6f0`).
- **Solo/Mute disabled & pressed states (`components/native/solo_mute/solo_mute.py:123-126,143`):**
  `STATE_DISABLED` paints `smooth.caption.off` (`#384252`) over the active blue `button.gradient.top/bottom` fill instead of a dimmed/grey disabled face. `STATE_PRESSED` passes `fill=(bg, bg)` (flat `surface.focus`), losing the gradient.
- **`text_face` stroke/inset mismatch (`components/native/button/button.py:319-321,328-331`):**
  `text_face(..., gradient=True)` hardcodes `inset=0` and `frame=metrics["stroke"] * scale // 2` (`1 px` border instead of `2 px`), whereas `gradient=False` (used by disabled PZ faces at `scripts/mpc-skin-extras/98_filter_native.py:249-251`) uses `metrics["inset"]=1, end_inset=2, stroke=2` (`components/sizes.json:1168`).
- **Standalone `compact_level_definition(None)` / `compact_pan_definition(None)` (`components/native/level/level.py:568-569`, `components/native/pan/pan.py:218-219`):**
  When `template=None` (default), `"value"` is appended after the loop at `font = 16.0`, leaving `Name = 20.0` and `Value = 16.0` outside a full `99_zz_dial_text.py` skin pass.
- **Focus, Q-Link, and handle names:** `build_choice_definition` (`selector.py:116-121`), `selector_definition`'s `plain` branch (`selector.py:484-502`), `unify_dial_text` (`level.py:1114-1140`), `solo_mute` (`solo_mute.py:288-351`), `54_osc_selector.py`, and `82_play.py` preserve existing `Focus` children, `acceptsHWFocus`, `handleName`, `handle remapping`, and `Q-Links.json` entries (no focus or Q-Link handle regression found beyond `command_stepper_definition` hit-zones and `unify_dial_text` disabled-state clobbering above).

---

## 2. Text Fit — Dial Caption / Value Rects vs `DIAL_TEXT_STYLE` (`font.value = 20.0`)

- `components/native/level/level.py:333`:
  ```python
  DIAL_TEXT_STYLE = {"font": "font.value", "colour": "text.value"}
  ```
- `components/sizes.json:191`:
  ```json
  "font.value": 20,
  ```
- `components/sizes.json:906` & `components/style.py:104`:
  ```python
  return float(height) / size("font.host.height.per.em")
  ```
  (`"font.host.height.per.em": 1.521`, so JUCE TUI font height `20.0` draws at **`em = 13.15 px`** and requires a **`>= 20 px`** vertical line box for ascent + descent in `TitilliumWeb-SemiBold.ttf`).

**Summary:** Across all 134 measured dial/affected label rects, **26 distinct rects have height `< 20 px`** (24 compact dial `Name` rects at `18 px`, `oscDetailStepper` at `19 px`, and `oscNativeSetting_toggle` at `14 px`). Below are the **worst 15** ordered by vertical clipping (`H < 20`) and tightest horizontal margin (`W - rendered_text_width`):

| # | Screen / Widget | Role | Rect (`W×H` px) | Old → New Height | Worst Sample & Width (`px` @ `13.15` em) | W Margin | H Margin (`H-20`) | Evidence (`file:line` & quote) |
|---|---|---|---|---|---|---|---|---|
| 1 | **OSC Detail** `oscNativeSetting_toggle` | `Name` | `125×14` *(prop `(0,0,1,0.25)` on `125×56`)* | `14 → 20` | `"ON"` (`17.5 px`) | `+107.5 px` | **`-6.0 px`** | `scripts/mpc-skin-extras/57_osc_settings.py:76,149` — `_label("Name", (0,0,1,.25))` calls `apply_dial_text_style(data["textStyle"])` |
| 2 | **OSC Detail / Blend** `oscNativeSetting_dial_*` (default `125×56`) | `Name` | `77×18` | `14 → 20` | `"RAND PHASE"` (`75.3 px`) | **`+1.7 px`** | **`-2.0 px`** | `scripts/mpc-skin-extras/57_osc_settings.py:120,179` (`size` None → `125×56`), `components/sizes.json:256-261` (`[48, 4, 150, 18]`) |
| 3 | **OSC Settings** `oscNativeSetting STEREO` (`107×128`) | `Name` | `59×18` | `14 → 20` | `"STEREO"` (`45.4 px`) | `+13.6 px` | **`-2.0 px`** | `scripts/mpc-skin-extras/57_osc_settings.py:93,108` (`w = width - 48`), `components/sizes.json:256-261` (`h = 18`) |
| 4 | **OSC Settings** `oscNativeSetting FILTER` (`107×128`) | `Name` | `59×18` | `14 → 20` | `"FILTER"` (`39.2 px`) | `+19.8 px` | **`-2.0 px`** | `scripts/mpc-skin-extras/57_osc_settings.py:93,108`, `components/sizes.json:256-261` |
| 5 | **OSC Settings** `oscNativeSetting START` (`107×128`) | `Name` | `59×18` | `14 → 20` | `"START"` (`37.1 px`) | `+21.9 px` | **`-2.0 px`** | `scripts/mpc-skin-extras/57_osc_settings.py:93,108`, `components/sizes.json:256-261` |
| 6 | **Mixer** `mixerSliderPAN56 / B1 / B2` | `Name` | `63×18` | `14 → 20` | `"PAN"` (`24.7 px`) | `+38.3 px` | **`-2.0 px`** | `components/sizes.json:1084` (`"mixer_strip.rect.dial_name": [48, 6, 63, 18]`), `components/native/mixer_strip/mixer_strip.py:281` |
| 7 | **OSC Settings** `PHASE` (`136×48`), `RAND` (`138×48`), `FRAME` (`146×48`) | `Name` | `88×18` / `90×18` / `98×18` | `14 → 20` | `"PHASE"` (`38.9 px`) / `"FRAME"` (`41.4 px`) | `+49.1 px` | **`-2.0 px`** | `scripts/mpc-skin-extras/57_osc_settings.py:93,108`, `components/sizes.json:256-261` |
| 8 | **Voice** `voiceNativeDial` (`116×146`) & `voiceNativeScaleDial/Env` (`170×132`) | `Name` | `116×18` / `170×18` | `15 → 20` | `"DETUNE"` (`47.5 px`) / `"ENV TIME"` (`55.9 px`) | `+68.5 px` | **`-2.0 px`** | `components/sizes.json:1379` (`"voice.controls.dial_caption_height": 18`), `scripts/native_voice/voice_tab.py:107`, `components/native/level/level.py:1092` |
| 9 | **Level / Pan** `COMPACT_48_RECTS` (`202×48` OSC compact dials) | `Name` | `150×18` | `14 → 20` | `"UNISON DETUNE"` (`96.4 px`) | `+53.6 px` | **`-2.0 px`** | `components/sizes.json:256-261` (`[48, 4, 150, 18]`), `components/native/level/level.py:552-553`, `components/native/pan/pan.py:202-203` |
| 10 | **OSC Detail** `oscDetailStepper` (`158×92`) | `Name` & `Value` | `152×19` *(prop `0.96×0.21`)* | `14→20` & `17→20` | `"UNISON MODE"` (`84.4 px`) / `"SuperSaw"` (`56.7 px`) | `+67.6 px` | **`-1.0 px`** *(+2.7 px overlap)* | `scripts/mpc-skin-extras/58_osc_detail.py:52-57` (`y=0..0.21` vs `y=0.18..0.39`: Name `y=0..19.3`, Value `y=16.6..35.9`) |
| 11 | **LFO** `lfoNativeField4 Smooth` (`124×112`) | `Value` | `48×28` | `20 → 20` | `"250 ms"` (`42.2 px`) / `"1000 ms"` (`49.9 px`) | **`+5.8 px`** / **`-1.9 px`** | `+8.0 px` | `scripts/mpc-skin-extras/53_lfo_smooth.py:85-86` (`width-size-lfo.dial.side_left-lfo.dial.smooth_value_gap-2` = `124-60-6-8-2 = 48`) |
| 12 | **OSC Settings** `oscNativeSetting FILTER` (`107×128`) | `Value` | `59×22` | `16 → 20` | `"20000 Hz"` (`54.6 px`) | **`+4.4 px`** | `+2.0 px` | `scripts/mpc-skin-extras/57_osc_settings.py:93,108`, `components/sizes.json:262-267` (`[48, 22, 150, 22]`) |
| 13 | **Smooth (default)** `_SMOOTH_RECTS` (`116×72`) | `Value` | `50×24` | `14 → 20` | `"250 ms"` (`42.2 px`) / `"1000 ms"` (`49.9 px`) | **`+7.8 px`** / **`+0.1 px`** | `+4.0 px` | `components/sizes.json:344-349` (`"level._smooth_rects.value": [64, 42, 50, 24]`), `components/native/level/level.py:911,1131` |
| 14 | **Mixer** `mixerSliderPAN56 / B1 / B2` | `Value` | `63×22` | `16 → 20` | `"100.0% L"` (`49.2 px`) / `"-12.0 dB"` (`48.8 px`) | **`+13.8 px`** | `+2.0 px` | `components/sizes.json:1085` (`"mixer_strip.rect.dial_value": [48, 26, 63, 22]`), `components/native/mixer_strip/mixer_strip.py:282` |
| 15 | **Play LFO Smooth** `PLAY_SMOOTH_RECTS` (`196×72`) | `Value` | `62×28` | `14 → 20` | `"250 ms"` (`42.2 px`) / `"1000 ms"` (`49.9 px`) | **`+19.8 px`** / **`+12.1 px`** | `+8.0 px` | `components/sizes.json:376-385` (`[130, 22, 62, 28]`), `components/native/level/level.py:911,1131` |

**Additional tight non-dial fit noted:**
- Play Env/LFO `HZ`/`BPM`/`KEY` RadioGrid (`scripts/native_play/env_lfo.py:214-216`, `scripts/mpc-skin-extras/play-env-lfo-contract.json:7`: `[1, 9, 1050, 466, 210, 56]`): `210 px` divided into 3 slots (`70 px` each) subtracts `radio_grid.gap = 8 px` (`4 + 4`) on the middle `BPM` cell (`components/native/radio_button/radio_button.py:85-87`), leaving `extent = 62 px` against `action_art`'s `61 px` minimum width at `height=48` (`font.button=18`, `padding=24` at `components/native/button/button.py:106-109`) — **only `1 px` spare**.

---

## 3. Contrast Analysis

### 3.1 Legacy Button Keyline vs Page Ground (`#0a0e1a` / `overlay_canvas.png` `#090d19`)

- `components/colors.json:42-45`:
  ```json
  "button.gradient.top": "#131930",
  "button.gradient.bottom": "#0d1122",
  "button.gradient.border": "#151c3a",
  "button.gradient.border.bottom": "#161e42",
  ```
- `components/native/button/button.py:61`:
  ```python
  # The keyline darkens down the box with the fill (bright top edge, dark bottom edge).
  ```
- `shell-mpc/ui/core.h:256-257` (after `6363e68b`):
  ```cpp
  the keyline follows the same ramp (lighter blue at the top).
  ```
- `AGENTS.md:53`:
  ```markdown
  a dark blue fill with a subtle vertical gradient (a thin highlight line at the top, the lower half darkening slightly: measured #131930 at the top to #0d1122 at the bottom on the page ground #0c0f18, a lighter blue border)
  ```

| Pair | Hexes | WCAG Contrast Ratio | ΔRGB | Assessment |
|---|---|---:|---|---|
| `button.gradient.border` vs page ground `#0a0e1a` | `#151c3a` vs `#0a0e1a` | **1.15 : 1** | `(+11, +14, +32)` | **Near-invisible** vs `#090d19` (`overlay_canvas.png` pixel): **1.16 : 1** |
| `button.gradient.border` vs `button.gradient.top` | `#151c3a` vs `#131930` | **1.04 : 1** | `(+2, +3, +10)` | **Indistinguishable** top keyline from button fill |
| `button.gradient.bottom` vs `#0a0e1a` | `#0d1122` vs `#0a0e1a` | **1.03 : 1** | `(+3, +3, +8)` | Lower half of buttons melts into the dark canvas |
| `button.gradient.border.bottom` vs `#0a0e1a` | `#161e42` vs `#0a0e1a` | **1.19 : 1** | `(+12, +16, +40)` | Bottom border is *brighter* than top border (`#151c3a`) |
| `button.gradient.border.bottom` vs `button.gradient.bottom` | `#161e42` vs `#0d1122` | **1.16 : 1** | `(+9, +13, +32)` | Low edge contrast |
| *Reference:* Old `button.border` vs `#0a0e1a` | `#26304b` vs `#0a0e1a` | **1.47 : 1** | `(+28, +34, +49)` | Substantially clearer edge |
| *Reference:* `selector.dropdown.border` vs `#0a0e1a` | `#27306a` vs `#0a0e1a` | **1.57 : 1** | `(+29, +34, +80)` | `1.39:1` vs `dropdown.top` (`#141a38`), `1.55:1` vs `dropdown.bottom` (`#0b0f24`) |
| *Reference:* Old `control.border` vs `#0a0e1a` | `#2c456d` vs `#0a0e1a` | **2.00 : 1** | `(+34, +55, +83)` | Clear "lighter blue border" |

**Defects:**
1. **Near-invisible button border:** `button.gradient.border` (`#151c3a`) differs from `button.gradient.top` (`#131930`) by only **(+2, +3, +10)** (`1.04:1`) and from `#0a0e1a` by **1.15:1**. Unlabelled/quiet icon buttons and RadioGrid off-cells lose their box boundary on the MPC LCD.
2. **Inverted keyline ramp:** `#151c3a` (top) → `#161e42` (bottom) brightens downward instead of darkening downward, directly contradicting `button.py:61` ("bright top edge, dark bottom edge"), `shell-mpc/ui/core.h:256-257` ("lighter blue at the top"), and `AGENTS.md:53` ("a thin highlight line at the top").
3. **Zero-contrast `mod_row` toggle cell in preview:** `components/native/mod_row/mod_row.py:100,412` paints flat `button.gradient.top` (`#131930`) on `mod_row.surface` (`#121930`), yielding `ΔRGB = (+1, 0, 0)` (**1.00 : 1**).

---

### 3.2 Disabled & State Readability

- **Solo / Mute `STATE_DISABLED` (`components/native/solo_mute/solo_mute.py:123-126,143`):**
  ```python
  elif state == STATE_DISABLED:
      bg = _style_rgb("button.surface")
      border = _style_rgb("smooth.border.off")
      fg = _style_rgb("smooth.caption.off")
  ...
  fill = (bg, bg) if state == STATE_PRESSED else (_style_rgb("button.gradient.top"), _style_rgb("button.gradient.bottom"))
  ```
  - `smooth.caption.off` (`#384252`) on `button.gradient.top` (`#131930`): **1.71 : 1** (on bottom `#0d1122`: **1.85 : 1**).
  - `smooth.border.off` (`#232d44`) vs `#0a0e1a`: **1.40 : 1**. Disabled S/M/FX labels are nearly illegible and sit on an active blue gradient.
- **`dim_artwork` disabled buttons (`components/style.py:137-141`, `components/colors.json:99-100`):**
  ```python
  grey = ImageOps.colorize(image.convert("L"), color("inactive.preview.dark"), color("inactive.preview.light"))
  ```
  - Text `dim(text.icon)` (`#9b9b9b`) on `dim(button.gradient.top)` (`#1f1f1f`): **5.93 : 1** (on bottom `#1a1a1a`: **6.26 : 1**) — text stays clearly readable.
  - Box border `dim(button.gradient.border)` (`#212121`) vs `dim(button.gradient.top)` (`#1f1f1f`): **1.01 : 1** (`ΔRGB = (+2, +2, +2)`), and vs `#0a0e1a`: **1.20 : 1** — the disabled button border vanishes completely.
- **Disabled dials (`unify_dial_text` vs `apply_control_state`):**
  - `unify_dial_text` (`components/native/level/level.py:1136-1137`) dims `#eef1ff` to `#a3a3a3` (**7.63 : 1** vs `#0a0e1a`), whereas `apply_control_state` (`components/style.py:180`) uses `text.status.off` (`#b9bdc9`, **10.26 : 1** vs `#0a0e1a`) and Filter off dials used `smooth.value.off` (`#6e7a9b`, **4.51 : 1**).
  - Worse, because of the key-matching bug in Section 1.2, Mixer (`*Inactive`), Filter (`*_off_*`), and Play LFO Smooth (`compactNativeSmoothReadout`) disabled dials are overwritten to active `#eef1ff` (**17.5 : 1** vs `#0a0e1a`), making them visually indistinguishable from enabled dials (`AGENTS.md:15` violation).
- **Neutral selector (`selector.neutral.*` at `components/colors.json:40,85-86`):**
  - `selector.neutral.ink` (`#909090`) on `selector.neutral.surface` (`#202020`): **5.10 : 1**; border `#5a5a5a` vs `#0a0e1a`: **2.79 : 1** — readable, which causes active `playOscSelector` (`82_play.py:234`, `neutral=True`) to look flat grey/disabled yet higher-bordered than the active blue gradient `playModSelector` beside it.
- **Active cyan RadioGrid / segmented face (`components/native/button/button.py:116-117`, `components/native/radio_button/radio_button.py:45`):**
  - `radio.active.text` (`#06101e`) on `accent.cyan` (`#2ee6f0`) top: **12.44 : 1**; on `radio.active.bottom` (`#127a8a`) bottom: **3.79 : 1** (below 4.5:1 AA normal text at the lower half, borderline for bold 18 px).

---

## 4. Rules Violations (`AGENTS.md`, `components/AGENTS.md`, `BUTTON-MUTATION-AUDIT-2026-11-27.md`)

### 4.1 Duplicate Token & Duplicate Function Definitions
- `components/sizes.json:154-155`:
  ```json
      "control.look": "legacy",
      "control.look": "legacy",
  ```
  Duplicate JSON key in the canonical size token file.
- `components/style.py:39-64`:
  `CONTROL_LOOKS = ("legacy", "flat")` and `def control_look():` are defined twice verbatim at lines `39-50` and `53-64` (likely from a merge conflict resolution).

### 4.2 Literals Instead of Tokens (`components/AGENTS.md:15`, `AGENTS.md:53`)
- `components/native/selector/selector.py:1353-1356` (`append_choice_rows`):
  ```python
  face = (action_art(text=label, width=width, height=48, look="active", colour=_style_color("radio.active.text"))
          if state == "on" else action_art(text=label, width=width, height=48, look="legacy"))
  image = Image.new("RGBA", (width, height))
  image.alpha_composite(face, (0, (height - 48) // 2))
  ```
  Hardcodes `48` twice instead of `_style_size("button.face.height")` / `_style_size("height.medium")`.
- `components/native/radio_button/radio_button.py:89,91`:
  ```python
  box = max(size for size in (32, 40, 48) if size <= across) if across >= 32 else across
  if vertical:
      face = action_art(text=label, width=width - 0, height=box, ...
  ```
  Hardcodes `(32, 40, 48)` and `32` instead of `height.small`, `height.intermediate`, `height.medium`; stray `width - 0`.
- `components/native/level/level.py:1137`:
  ```python
  dim = "ff%02x%02x%02x" % dim_artwork(Image.new("RGBA", (1, 1), (r, g, b, 255))).getpixel((0, 0))[:3]
  ```
  Builds an ARGB string inline with literal `255` and `"ff..."` instead of a shared helper in `components/style.py`.
- `scripts/mpc-skin-extras/57_osc_settings.py:66` (`(14 if caption else 18)`) and `scripts/mpc-skin-extras/58_osc_detail.py:52` (`14`, `17`) retain pre-existing pixel literals now masked by `apply_dial_text_style`.

### 4.3 "Every Control Comes from Its Component" Bypassed (`AGENTS.md:63-65`, `BUTTON-MUTATION-AUDIT-2026-11-27.md`)
- **Direct `gradient_face()` re-implementations instead of `button.py` face recipe:**
  - `components/native/solo_mute/solo_mute.py:140-151` reads `button.gradient.*` itself and calls `gradient_face(..., radius=radius * scale, frame=scale)` instead of going through `button.py` (bypassing `button.art` / `button.size.*` geometry tokens).
  - `components/native/tune/tune.py:226-235` (`_gradient_button_face`) defines its own button painter with `radius.field * scale`, `inset=pad`, `frame=scale`, duplicating `button.py`.
  - `components/native/radio_button/radio_button.py:42-51` duplicates `action_art`'s `legacy`/`active` fill & border logic inside `render_segmented_button_art` instead of delegating to `action_art` (note: `render_grid_cell_art` at lines `91-97` correctly calls `action_art`).
- **Mutation-audit bypasses still left unfixed:**
  - `components/native/tune/tune.py:692-713` (`render_tune_card`): still draws flat `draw.rounded_rectangle(..., fill=BG_CELL, outline=BORDER_CELL, width=scale)`, ignoring `button.gradient.*`.
  - `components/native/selector/selector.py:1141-1177` (`render_compact_button_face`): still draws flat `route.button.surface` / `button.surface`.
  - `components/native/selector/selector.py:786` (`filter_chain_definition` "CHAIN" chip):
    ```python
    face = render_dropdown_face(*size, chevron=False)
    ```
    Flagged in `docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md:41-42` as a button that should use `action_art`; because it calls `render_dropdown_face`, a `button.gradient.*` mutation does **not** reach the Filter CHAIN button (`selector.dropdown.*` is a separate token set).

### 4.4 Flat Faces Left (`AGENTS.md:51-53`: "the old gradient design, not flat")
- `components/native/mod_row/mod_row.py:100,109,153-165,412`: `modNative_toggle_*` (`POLARITY`, `ROUTE`, and Mod Detail `INVERT`/`STEREO`/`SYNC`) are still **flat unbordered rectangles** whose background colour is `"button.gradient.top"` (`#131930`), with no gradient and no border.
- `components/native/button/button.py:344-347`: `write_switch_asset` draws a flat `pen.rounded_rectangle(..., fill=_style_color("accent.cyan" if active else ("button.gradient.top" if legacy else "button.surface")), outline=_style_color("button.gradient.border" if legacy else "button.switch.border"))` instead of a gradient face.
- `scripts/mpc-skin-extras/82_play.py:234` + `components/native/selector/selector.py:368-370`: active `playOscSelector` passes `neutral=True`, rendering a flat `#202020` box.
- `components/native/solo_mute/solo_mute.py:143`: `STATE_PRESSED` passes `fill=(bg, bg)` (flat `#172038`) to `gradient_face`.
- `components/native/mixer_strip/mixer_strip.py:331-350,495-575`: `mixerDestination56` uses flat `field.surface` (`#12182d`) instead of `selector.dropdown_definition`, and strip wells use flat `button.surface`/`button.border`.

### 4.5 Left/Right Steppers & Phased-Out Variants (`AGENTS.md:55-61`)
- **Left/right steppers remaining (`AGENTS.md:55-57`):**
  - `scripts/native_play/osc.py:151-162` (`playOscTableField` keeps `previous` / `next` chevron buttons).
  - `scripts/mpc-skin-extras/58_osc_detail.py:45-58` (`oscDetailStepper` keeps left/right chevron buttons) and `146-151` (`oscDetailUnisonMode` via `command_stepper_definition`).
  - `components/native/selector/selector.py:1407-1420` (`command_stepper_definition` keeps `Previous` / `Open` / `Next` hit zones under a dropdown face).
- **Inside-box caption variant (`AGENTS.md:57`: "being phased out"):** `selector.py:583-593` (`caption_text` / `name` inside the selector box) is still emitted by `build_shape_selector`, `command_stepper_definition`, `53_lfo_smooth.py:_choice`, `57_osc_settings.py`, `98_filter_native.py`, and `native_play/osc.py:148`.
- **Old segmented `RadioButton` (`AGENTS.md:59-61`: "being phased out"):** `render_segmented_button_art` / `create_segmented_radio_button_group(..., grid=False)` remains on FX (`20_fx.py:438,651,659`), Mod (`99_ui_screen_mod.py:34`), LFO tools (`53_lfo_smooth.py:360`), Filter `F1+F2` (`98_filter_native.py:618`), Env selector (`96_env_native.py:112`), Warp picker (`native_warp_picker.py:44`), and Voice (`voice_tab.py:111`, `sequence_readouts.py:50`). Only LFO Sync (`53_lfo_smooth.py:152` and `native_play/env_lfo.py:216`) was switched to `grid=True`.

### 4.6 Lists Keep Factory Behaviour (`AGENTS.md:67-70`)
- **Compliant on Osc source list:** `6f4d8296` removed the select-and-close probes and `d123fc75` / `3328da40` stripped the ON/OFF badges (`components/native/selector/selector.py:1006-1013`, `scripts/mpc-skin-extras/54_osc_selector.py:34`, `scripts/mpc-skin-extras/82_play.py:246`).
- **Note on `append_choice_rows` (`components/native/selector/selector.py:1322-1376`):** Used by Play Sub octave/wave (`scripts/native_play/osc.py:103,112`) and Filter chain (`scripts/mpc-skin-extras/98_filter_native.py:382`). It preserves header-tap apply/close (`header_title`), but paints Button-styled `action_art` rows (`look="legacy"`/`"active"`) + `render_pipa_icon` checkmarks on top of `MenuOverlay` rather than plain factory `MenuOverlay` rows.

### 4.7 Component `img/` Sync (`components/AGENTS.md:6`)
- Only `components/native/filter_selector/img/filter-selector-*.png` (in `ca1ad4b0`) and `components/native/selector/img/dropdown-inside-left.png` (in `3328da40`) were updated.
- Components whose rendering changed in `ca1ad4b0..3328da40` without refreshing their `components/native/*/img/` assets: `solo_mute`, `tune`, `radio_button`, `mod_row`, `level`, and `pan`.

---

## 5. Dead Code, Dead Tokens & Test Quality

### 5.1 Is `SYNTHYBOY_CONTROL_LOOK=flat` Worth Keeping? **Verdict: No — Remove It**

`SYNTHYBOY_CONTROL_LOOK=flat` (`components/style.py:47`, `components/sizes.json:154`) does **not** restore the pre-`ca1ad4b0` / pre-`3328da40` look, nor does it produce a consistent flat skin:
1. **Selectors stay gradient in `"flat"` mode:** `components/native/selector/selector.py:353-354`:
   ```python
   if look == "neon" and _control_look() == "legacy":
       look = "legacy"   # the skin default follows control.look (A/B: SYNTHYBOY_CONTROL_LOOK=flat)
   ```
   When `SYNTHYBOY_CONTROL_LOOK=flat`, `look` stays `"neon"`, which still calls `gradient_face()` with `theme.neon.panel0/panel1` (`#141a38` → `#0c1026`, `selector.py:340,371`) — **not** the old flat `selector.surface` (`#0e1b33`) + `control.border` (`#2c456d`).
2. **Hardcoded `"legacy"` callers ignore the flag:**
   - `components/native/radio_button/radio_button.py:91-92,96` (`render_grid_cell_art` hardcodes `look="active"` / `look="legacy"`).
   - `components/native/selector/selector.py:1353-1354` (`append_choice_rows` hardcodes `look="active"` / `look="legacy"`).
   - `components/native/button/button.py:362` (`render_sample` hardcodes `look="legacy"`).
3. **C++ overlay ignores the flag:** `shell-mpc/ui/core.h:266-277` and `shell-mpc/ui/theme_tokens.h:37-40` always render `button.gradient.*` (`controlTop`/`controlBottom`/`controlBorder`) regardless of `SYNTHYBOY_CONTROL_LOOK`.
4. **Maintenance cost without benefit:** The flag duplicates drawing branches across 7 files (`style.py`, `button.py`, `selector.py`, `radio_button.py`, `solo_mute.py`, `tune.py`, `mod_row.py`), keeps dead flat tokens alive, and already caused duplicate definitions (`sizes.json:154-155`, `style.py:39-64`). The owner has already committed to the legacy gradient look (`AGENTS.md:51-53`), and `old-ui-3bb27424` is preserved in `.hw-local/mpc-builds/` (`AGENTS.md:49`).

### 5.2 Dead Code Introduced or Left
- `components/style.py:39-50` & `53-64`: duplicate `CONTROL_LOOKS` and `def control_look():`.
- `components/sizes.json:154-155`: duplicate `"control.look": "legacy"`.
- `components/native/radio_button/radio_button.py:82`: unused `from components.native.button.button import in_slot` inside `render_grid_cell_art`.
- `components/native/selector/selector.py:364`: unused local `box = (inset, inset, size[0] - inset - 1, size[1] - inset - 1)` inside `render_dropdown_face` (`gradient_face` computes `box` itself).
- `components/native/selector/selector.py:508`: `dropdown_look` parameter on `selector_definition` (`line 436`) is ignored in the non-plain branch.
- **Overwritten font assignments (dead work):**
  - `components/native/level/level.py:494,523,552,557,618,623,1042-1043,1091` and `components/native/pan/pan.py:144,173,202,207` compute/assign per-variant font heights and colours that are immediately overwritten by `dial_caption_style` / `apply_dial_text_style`. In particular, `blue_dial_definition` (`level.py:1032,1042-1052`) accepts `caption_height` and `value_height` (still passed by `scripts/native_play/env_lfo.py:109,113,139`) and discards both.
  - `scripts/mpc-skin-extras/53_lfo_smooth.py:91,111`, `96_env_native.py:198`, `98_filter_native.py:319`, and `components/native/macro_card/macro_card.py:68-69` set custom dial font heights/colours that `scripts/mpc-skin-extras/99_zz_dial_text.py` (`unify_dial_text`) unconditionally overwrites at the end of skin generation.

### 5.3 Dead Tokens (`0` Production / Test References)
- `components/colors.json:41`: `"selector.surface": "#0e1b33"` (0 prod, 0 test)
- `components/colors.json:134`: `"mod_row.button_border": "#26304b"` (0 prod, 0 test)
- `components/colors.json:141-142`: `"selector.choice.on": "#175a76"`, `"selector.choice.off": "#303237"` (0 prod, 0 test)
- `components/sizes.json:393`: `"level.sub_dial_definition.style.font.height": 15.0` (0 prod, 0 test)
- `components/sizes.json:676`: `"selector.selector_definition.rounded_rectangle": 1` (0 prod, 0 test)
- `components/sizes.json:691`: `"selector.selector_definition.x": 18` (0 exact references; only prefix-matched by `.x.l`, `.x.r`, `.x.v`)
- `components/sizes.json:708-711`: `"selector.selector_definition.line.offset9"` .. `"offset12"` (0 prod, 0 test)
- **Effectively dead** (only read in overwritten assignments or `"flat"`-only fallback): `level.level_definition.font.height`, `level.compact_level_definition.font.height*`, `level.compact_dial_definition.font.height*`, `pan.pan_definition.font.height`, `pan.compact_pan_definition.font.height*`, `level.blue.caption.font`, `level.blue.value.font`, `button.switch.border`, `mod_row.button_surface`, `radio.surface.top/bottom` (aside from `99_ui_screen_lfo.py:181`), `filter.pz.active.surface/border`.

### 5.4 Test Quality Audit
1. **Brittle exact-colour assertions (fail on any legitimate palette tweak instead of verifying the token path):**
   - `scripts/tests/test_native_button_catalog.py:45-57` (`test_measured_old_colours`) hardcodes RGB `(21, 28, 58)`, `(19, 25, 48)`, `(13, 17, 34)` and hexes `"#131930"`, `"#0d1122"`.
   - `scripts/tests/test_native_label_catalog.py:176-187` (`test_legacy_fill_has_the_voice_selector_contrast_and_neon_is_untouched`) hardcodes RGB `(20, 26, 56)`, `(11, 15, 36)`, `(38, 46, 103)` and hexes `"#141a38"`, `"#0b0f24"`.
2. **False-positive test (`scripts/tests/test_native_label_catalog.py:183-184`):**
   ```python
   neon = selector.render_dropdown_face(220, 48)
   self.assertEqual(neon.convert("RGB").getpixel((20, 10)), (20, 26, 56))  # theme.neon.panel0 as before
   ```
   `selector.py:353-354` silently diverted `look="neon"` to `look="legacy"` (`selector.dropdown.top`), so `neon` was **not** untouched — the test passed only by coincidence because `selector.dropdown.top == theme.neon.panel0 == "#141a38"`.
3. **Stale default coverage (`scripts/tests/test_native_selector.py:99-108`):**
   `test_shape_variant` explicitly passes `stepper=True, arrows=True`, leaving the new default `build_shape_selector(..., stepper=False, arrows=False)` (`selector.py:611-612`) untested.
4. **Good pattern to follow (`scripts/tests/test_overlay_control_look.py:54-67`):**
   `test_changing_a_button_token_changes_the_generated_theme` mutates `button.gradient.top` to `#ff00ff` and asserts the generated C++ header updates, matching `AGENTS.md:65`.
5. **Coverage gaps (no tests added by `ca1ad4b0` / `3328da40` for):**
   - `unify_dial_text` / `99_zz_dial_text.py` (especially inactive-state detection across `Off`, `Inactive`, `_off_`, `Readout`).
   - `render_grid_cell_art` (vertical and horizontal gaps, cell widths, minimum-width fit).
   - `command_stepper_definition` (visual chevron vs `Previous`/`Open`/`Next` hit zones).
   - Skin-side token mutation propagation across `button`, `radio_button`, `solo_mute`, `tune`, `mod_row`, `selector`, and `append_choice_rows`.
   - JSON duplicate-key check (`sizes.json:154-155`) / duplicate-symbol check (`style.py:39-64`).

---

## 6. Top Risks for the Next Device Check (Ordered by Hardware Impact)

1. **Unusable / misleading OSC table & Unison MODE controls (`components/native/selector/selector.py:1410-1419`, `scripts/mpc-skin-extras/58_osc_detail.py:146-151`):**
   Tapping the visible dropdown `v` chevron steps `Next` instead of opening the menu, tapping the left 56 px unexpectedly steps `Previous` without arrows, and on `oscDetailUnisonMode` (`158×92`) the center `46 px` (`x = 56..102`) is completely dead.
2. **Buttons visually disappearing on the MPC LCD (`components/colors.json:42-45`):**
   `button.gradient.border` (`#151c3a`) has **1.15:1** contrast against `#0a0e1a` (**1.04:1** against `button.gradient.top`), `button.gradient.bottom` (`#0d1122`) has **1.03:1** against `#0a0e1a`, and the keyline ramp is inverted (`#151c3a` → `#161e42`). Quiet/unlabelled buttons and RadioGrid off-cells risk reading as borderless floating glyphs.
3. **Disabled dials appearing active on Mixer, Filter, and Play LFO Smooth (`components/native/level/level.py:1125`):**
   `unify_dial_text` misses `mixerSlider*Inactive`, `filterNativeDial_*_off_*`, and `compactNativeSmoothReadout`, turning their labels bright `#eef1ff` instead of dimmed (`AGENTS.md:15`).
4. **Clipped / overlapping dial labels on OSC, Mixer, Voice, and OSC Detail (`components/sizes.json:256,1084,1379`, `57_osc_settings.py:76,149`, `58_osc_detail.py:52-57`):**
   26 label rects are shorter than `font.value = 20.0` (`14 px` on `oscNativeSetting_toggle`, `18 px` on 24 compact dial captions, `19 px` on `oscDetailStepper` with `2.7 px` vertical overlap), and narrow `Value` rects (`lfoNativeField4 Smooth` `48 px`, `_SMOOTH_RECTS` `50 px`, `oscNativeSetting FILTER` `59 px`, `oscNativeSetting_dial` default `77 px`) risk truncation (`+1.7 px` down to `-1.9 px` horizontal margin).
5. **Play top-bar visual inconsistency (`scripts/mpc-skin-extras/82_play.py:234-243`, `components/native/selector/selector.py:468-502`):**
   `playOscSelector` renders as a `48 px` tall (`radius=6`, `inset=0`) flat-grey `#202020` "inactive" box right next to `56 px` tall (`radius=10`, `inset=3`) dark-blue gradient Env/LFO selectors.
6. **LFO Sync RadioGrid visual & touch-fit defects (`components/native/radio_button/radio_button.py:84-99`, `53_lfo_smooth.py:150-152`, `native_play/env_lfo.py:214-216`):**
   Standalone LFO Sync (`150×168` vertical) has `12 px` / `8 px` / `4 px` uneven gaps; Play LFO Sync (`210×56` horizontal) has `66 / 62 / 66 px` unequal widths with `BPM` only `1 px` above `action_art`'s minimum width.
7. **Mod Matrix regression (`components/native/mod_row/mod_row.py:92-93,100,109,147`):**
   Amount readout shrunk from `34` to `20`, split toggle Name/Value hierarchy is erased, and `POLARITY`/`ROUTE`/`INVERT`/`STEREO`/`SYNC` remain flat unbordered `#131930` rectangles (`1.00:1` vs `mod_row.surface` in preview).
8. **Unreadable disabled Mixer S/M/FX buttons (`components/native/solo_mute/solo_mute.py:123-126,143`):**
   `#384252` text on `#131930` active blue gradient yields **1.71:1** contrast on disabled channels.
