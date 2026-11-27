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
