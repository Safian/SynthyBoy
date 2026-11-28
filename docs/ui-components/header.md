# HEADER (design proposal v0; the oscillator screens use it)

The full-width top row of a page or of a Play third. The oscillator screen family (Osc A/B/C, Sub, Noise), Env 1-6 and LFO 1-10 / RND 1-4, the Note / Velo curve screens, the Mod matrix page, and Master, Tuning, Voice and Macros use it in the skin (see "Skin: the oscillator headers", "Skin: the Env and LFO headers", "Skin: the curve and Mod headers" and "Skin: Master, Tuning, Voice, Macros" below); FX and Play do not yet ("Not yet: FX and Play"). The catalogue (`kepes-katalogus.html`) shows nineteen examples. Source: `components/native/header/header.py`, examples: `components/native/header/catalog.json`, test: `scripts/tests/test_native_header_catalog.py`.

## Geometry (tokens in `components/sizes.json`)

| Token | px | Meaning |
|---|---|---|
| `header.height` | 56 | the row |
| `header.padding` | 8 | the one margin, the same on all four sides (left, right, top, bottom); the content row is `height - 2 * padding` = 40 px (`height.intermediate`) |
| `header.gap` | 8 | between components |
| `header.page.width` | 1280 (`screen.width`) | a page; the display tab area is 1280 x 628 |
| `header.panel.width` | 400 | a Play third |
| `header.dropdown.min_width` | 220 | width of a fixed dropdown, minimum width of the flexible (fill) one |

## Zones

Order in a page header: [title] [POWER] [page controls] ... [common buttons].

- LEFT: the screenTitle first, at the padding (the Label component's screenTitle: `font.page_title`, `text.value`; its width comes from the text), then the module POWER button (40 x 40; only on pages or panels of modules that can be switched off) one `header.gap` after the title. Modulators (Env, LFO, RND) and Master have no POWER: the header is just the title.
- CENTRE: page-specific controls after one gap (40 px: dropdown, text buttons; all as high as the content row): dropdown (selector recipe), text button (button recipe). The centre contents per page in the catalogue are EXAMPLES, not a decision.
- Flexible control: at most one centre control (the dropdown) is marked `"fill": true` in the catalogue data. It takes all the room between the left and the right group (width = row - side paddings - the other widths - the gaps); `header.dropdown.min_width` is its minimum. Title, POWER and the common buttons keep their size.
- RIGHT: the common buttons, right-aligned, `header.padding` from the edge: MORE.
- The header of a Play third is the module selector dropdown at third width (`header.panel.width`); the oscillator's header also holds the POWER button and the ROUTE choice.
- POWER off: the POWER icon is unlit and the centre controls take the disabled look (`style.dim_artwork`, the artwork of `apply_control_state`). Title and common buttons stay.

## Recipes used (no new look)

The Button and IconButton recipes (`button.render_sample`, `icon_button.render_sample`; look legacy: gradient fill, no flat button anywhere in the header), `label.render_label` (title), `selector.render_dropdown_variant` (dropdown: value only, centred, no label: the Play-third look; placement none, align center) `font.page_title` title text. POWER is an IconButton, MORE is a text Button (no more icon).

## The spec (owner decision 2026-11-28: a spec list, not a growing number of types)

A header is DESCRIBED by one plain JSON spec; the catalogue examples in `components/native/header/catalog.json` ARE such specs, and the skin definitions are produced from them (`header.header_definitions(spec)`), never written by hand:

```
{"id": "...", "file": "header-x.png", "width": "page" | "panel", "caption": "...", "alt": "...",
 "zones": {"left": [slot, ...], "centre": [slot, ...], "right": [slot, ...]},
 "skin": {"def": "<form definition name>", "page_gate": ["<handle>", ...]}}        <- only for a header that is built into the skin
```

One example per slot kind (`kind`, then the fields; every slot may carry `role` and, for the skin, a `skin` block):

| Zone | kind | Example | Fields |
|---|---|---|---|
| left | `title` | `{"kind": "title", "text": "OSC A", "slot": 67}` | `text`; `slot` = reserved width in px (a family of pages with different titles keeps POWER fixed) |
| left | `power` | `{"kind": "power", "state": "on"}` | `state` on / off (the catalogue picture; the skin always gets both looks from the one slot) |
| centre | `dropdown` | `{"kind": "dropdown", "role": "type", "value": "Wavetable", "width": "header.dropdown.min_width", "fill": true}` | `width` (size token) or `width_px`; `fill` (at most one); `height` token |
| centre | `field` | `{"kind": "field", "role": "oct", "name": "OCT", "value": "+0", "width_px": 112, "box_px": 68}` | name outside, value box inside; `caption` (`"Name"` or `"Value"`): the skin face is blank and the host draws the name in a Label bound by the handle `Hdr<Role>Caption` (the Voice step value shows the selected row's name) |
| centre | `button` | `{"kind": "button", "role": "browse", "text": "BROWSE"}` | `text`; `width_px` (a fixed width); `on_text` (a toggle: its own text while pressed / active, same width; only in a header without POWER) |
| right | `more` | `{"kind": "more"}` | the common MORE button |
| right | `dropdown` | `{"kind": "dropdown", "role": "route", "value": "Filter 1", "width_px": 150}` | as the centre dropdown |
| centre | `segment` | `{"kind": "segment", "role": "modeAll", "text": "ALL", "selected": true}` | one button of a switch group: plain look, and in the skin also the selected look (`action_art` look `active`, the RadioGrid selected button); `selected` only draws the catalogue picture |
| right (or centre) | `button` | `{"kind": "button", "role": "targets", "text": "TARGETS"}` | a page text button; the right zone takes the same text button as the centre (the modulator screens' TARGETS, the curve screens' INIT / UNDO / DEL POINT, the Mod ADD) |
| centre / right | `label` | `{"kind": "label", "role": "count", "width_px": 180}` | a live value the host draws (Mod slot count): room in the layout, no picture; the skin block is `{"label": {"size", "colour", "justify"}}` |

Another screen is only a spec. Env 1 (title + TARGETS), no POWER:

```
{"id": "env-1", "width": "page", "zones": {"left": [{"kind": "title", "text": "ENV 1"}],
 "centre": [{"kind": "button", "role": "targets", "text": "TARGETS"}]}}
```

and Filter 1 (title, POWER, MODEL dropdown filling the row, MORE): `left` title + power, `centre` one `dropdown` with `"fill": true`, `right` `[{"kind": "more"}]`.

### The skin block of a slot

A slot that is built into the skin names its cells: `"skin": {"cells": [{"name": "Type", "def": "oscHeaderType", "overlay": "<overlay name>", "gate": ["<handle>"]}]}`.

| Cell field | Meaning |
|---|---|
| `name` | the instance name inside the form |
| `def` | the cell definition (one per role and width; shared by every form that names it) |
| `overlay` / `toggle` | the action on Mouse Down and Enter Pressed: Show Overlay with this overlay name, or Toggle Switch; none = the control is a Button |
| `hit` | `{"id", "group"}`: a transparent hit Button over a dropdown (the Table cell) |
| `gate` | extra `additionalInvalidatingHandles` of the cell (which page state shows it); several cells of one slot are alternatives (Table / Source) |
| `page` | also gated by the form's `page_gate` (the Route stays visible when POWER is off) |
| `wrapper` | `NativeActive`: keep the proven NativeActive / NativeInactive pattern (see below) |
| `focus`, `show` | a Focus child; the instance's showWhenDataModelInvalid (default `Show`) |
| `id`, `group` | `buttonId` / `numButtonsInGroup` of a `button` or `segment` cell (default 1 / 1) |
| `role` | the cell binds its own handle `Hdr<Role>` instead of the slot's (LFO: WAVEFORM and STYLE are two alternative cells of one dropdown slot, each with its own parameter; Mod: source / target group) |
| `hw_focus` | the cell instance's acceptsHWFocus in the form (default `No`; `Yes` for cells whose own definition holds `Show Overlay` / `Toggle Switch` actions such as Osc TYPE / SOURCE / WAVEFORM / OCT / KIND / SOUND and Voice controls, `Children` for cells wrapping an inner `Button` with `acceptsHWFocus: Yes` such as Osc Table / BROWSE or a `NativeActive` wrapper such as ROUTE, paired with `acceptsHWFocus: Children` on the Osc header instances and `ignoreMousePresses: true` on every header form) |
| `inert` | the cell definition ignores mouse presses (the former cell sat in an inert parent; the touch is handled elsewhere) |
| `button_focus` | the inner Button's acceptsHWFocus (default `Yes`; `No` also shows it always: the Env TARGETS) |

The form's `skin` block may carry `title_gate`: a handle name the title Label follows alone (the instance remaps it as `HdrTitleGate`; `true` names `HdrTitleGate`), with `title_gate_show` as its showWhenDataModelInvalid (default `Hide`, Env / LFO: the list page that replaces the caption hides it; the curve screens and Mod use `Show`). A spec WITHOUT a `power` slot (a modulator, the curve screens, Mod) gets no POWER button, no `HdrOn` / `HdrOff` gating and no disabled look: the cells are gated only by their own `gate`.

`role` gives the remap name `Hdr<Role>` (`type` -> `HdrType`) that the instance binds to a parameter. A `gate` entry is a remap name as well (`HdrPage`, `HdrView`): the instance binds it to the gating handle.

A form whose `skin` block has `static_title: true` (Master, Tuning, Voice, Macros; it requires a header WITHOUT a `power` slot, which has no `HdrOn` / `HdrOff` state and no disabled look): its title is a static Text Label (the spec's text; `HdrTitle` is bound to `Parameter 0` as the former caption was) as wide as the room up to the next control, the cells carry only their own gates.

### Instances and the tool

The instances (which parameters, which page gate) of one screen family are a list in `components/native/header/skin-<family>.json` (`skin-osc.json`): `{"name", "form" (an example id), "title", "power": "Parameter N", "data", "gate", "remap": {"Hdr<Role>": handle, ...}}`. Without POWER `"title_handle"` (any always valid parameter; the Name label of it carries the title text) replaces `power`, or, for a form with `skin.static_title`, there is no `title` and the `remap` names `HdrTitle`; optional `title_gate` (one handle the title alone follows, remapped as `HdrTitleGate`), `show` (default `Hide`), `focus` (acceptsHWFocus of the instance, default `Yes`; `Children` where the cells are their own focus stops, `No` for a title only) and `gate` (omit it for version 1 bounds without the additional handles; an empty list keeps the version 2 bounds). Instead of the family's `layer`, `"tab"` (the instance sits in that tab of `skin/TUI.json`, family key `tab_file` names another file) with `"replaces"` (the names of the former ad hoc items it takes the place of; they are removed, the instance takes the place of the first one). `"retire": {"definitions", "components", "images"}` (family level) lists what the header replaces and the tool removes. The tool derives `HdrTitle` (`WithName[<title>]/<power>`), `HdrPower`, `HdrOn` / `HdrOff` (`IndexedEnabling/1|0/2/<power>`) from `power`. Hand-run, nothing at build time:

```
python3 scripts/header-skin.py components/native/header/skin-osc.json     # definitions + instances + faces (skin/hdr_*.png)
python3 scripts/skin-format.py && python3 scripts/skin-baseline-update.py   # as after every skin edit
```

The tool replaces every definition and instance whose name starts with the family prefix (`oscHeader`), in place, and removes `hdr_*.png` faces no skin file names any more (the families share the directory); the test `HeaderSkinSpecTests` fails if the committed skin differs from what the spec produces. Rolling the Header out to another screen = one `skin` block per example, one `skin-<family>.json`, one tool run.

### Why the number of definitions is small

The Osc family needed 29 header definitions (four forms each with an `...Off` twin, nine cells each with an `...Off` twin, three route definitions); now 16 (4 forms, 9 cells, 3 route definitions), the 18 faces (`hdr_*.png`) are the same. The power state is no longer a separate definition: the form holds the enabled cell instance and the inert pictures (Image + value Label) of the disabled look side by side, gated by the instance's `HdrOn` / `HdrOff` names (the same IndexedEnabling gates the Off twins used). What is NOT folded, and why:

- The cells stay one per role and width: the Show Overlay action names its overlay in `additionalData`, a definition-level string that no handle can change, and a face of another width is another picture. A cell shrunk to a transparent hit area (one per overlay, no width) needs a device probe that a definition without a visible child still receives Mouse Down (not measured; open in TODO.md).
- The route keeps the NativeActive / NativeInactive wrapper (3 definitions shared by all forms): its disabled look follows the Filter routing gates, not POWER, and its focus chain passes `acceptsHWFocus: Children` down from the header instance (inside the `ignoreMousePresses: true` header form) to the `NativeActive oscHeaderRoute` wrapper instance (`ignoreMousePresses: true`), then `Children` on the active `oscHeaderRoute` child (`No` on `NativeInactive`); folding it into the forms would repeat the inactive look in four forms.
- The inert disabled `Button`s of the old Off cells (Browse, the Table's Open hit button; ignoreMousePresses on, no hardware focus) are plain pictures in the form now: a Button in the form would take presses; a never-pressed one has no function.

## Skin: the Env and LFO headers (owner order 2026-11-28)

`skin/defs_env.json` (`envHeader`, `envHeaderTargets`) and `skin/defs_lfo.json` (`lfoHeader`, `lfoHeaderWave`, `lfoHeaderStyle`, `lfoHeaderTargets`) are generated from the examples `env-page` and `lfo-page` by `scripts/header-skin.py` with `skin-env.json` and `skin-lfo.json`. Both are modulators: title only, no POWER.

- One instance per tab (`Env 1 Header` .. `Env 6 Header`, `LFO 1 Header` .. `RND 4 Header`) sits in the tab itself, in place of the caption (`Native Caption env<n> 0`, `Native Caption <view> title`): the title is the Name label `WithName[ENV 1]/Parameter 2934` (LFO: `Parameter 2820`), text 40 px, so the title is a Header title now, not a static caption. The Env title keeps the caption's gate (`IndexedEnabling/0/2/Parameter 3588`, hidden while the list page shows) as `HdrTitleGate`.
- Env: title left, the TARGETS button right-aligned (right zone `button`); the cell keeps the former `envNativeTargets` behaviour (inert definition, inner Button id 1 of 1 on `Parameter 2934`, gate `NonZero/Parameter 2921`, instance focus `Yes`).
- LFO / RND: title (slot 79 px = "LFO 10", so the dropdown never moves), the dropdown filling the row and the TARGETS button right. The dropdown slot holds two alternative cells with the former gates: `Waveform` (overlay `lfoNativeChoiceDrawer1`, `Parameter 2820`, `RangedEnabling/1/3/7/Parameter 2825`) and `Style` (overlay `lfoNativeChoiceDrawer2`, `Parameter 2821`, `RangedEnabling/4/6/7/Parameter 2825`); the button is the former `LFO 1 Native Action 1` (id 1 of 1 on `FromGesture/Parameter 3662`, gate `RangedEnabling/1/6/7/Parameter 2825`, inert definition as the editor it sat in).
- Geometry follows the Header: 40 px controls 8 px from the top (the former cells were 56 px high); the TARGETS button is the Button recipe at its text width (84 px; it was 160 / 200 px); the dropdown shows the value only (the WAVEFORM / STYLE caption inside the box is gone). The C++ rectangles (`env_ui.h` `geo_.targets`, `lfo_layout.h` `targets` / `namedPicker`) are not drawn or hit in the skin build and were not changed.
- The target tags of the Env row (`envScreenTop`, `Env Native Tag`) are page content between the title and TARGETS, not Header slots; they stay as they are.
- Proof: a script resolved, per tab (Env 1-6, LFO 1-10, RND 1-4), the (handle remap, button id, group, focus, gates, actions) records of the header items before and after: all 116 records match.

## Add an example

Add a spec to `header.examples` in `components/native/header/catalog.json` (format above), then redraw the pictures.

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.

The test checks that every example is 56 px high, content 40 px with 8 px on all four sides (left, right, top, bottom), zones in order without overlap, nothing wider than the room.

## Dropdown height and look

The Header dropdown is the Selector's value-only, centred variant (`selector.render_dropdown_variant`, placement `none`, align `center`) with the Voice selector's contrast: fill `selector.dropdown.top` #141a38 to `selector.dropdown.bottom` #0b0f24, keyline #27306a. It exists at 32 / 40 / 48 px (`selector.dropdown.size.<px>`: value em 18 / 22 / 24, chevron, `pad_x`); an example picks its height with the optional `height` field (a `height.*` token), default `height.intermediate` (40). The skin's dropdowns keep the `neon` look.

## No left/right stepper

The Header has no left/right stepper (arrow selector, carousel): every choice is the dropdown (value centred, 40 px). The Osc A example's SOURCE control is a dropdown; the Header module has no stepper kind. A value that does not fit is left-aligned and cut with an ellipsis, ending `selector.dropdown.chevron_gap` (4 px) before the chevron lane (available width = box - `pad_x` - chevron lane - chevron gap).

## Padding setting: 8 / 40 or 4 / 48

`header.padding` is the only margin token (there is no other margin token) and the header stays 56 px high, so the content row is `56 - 2 x padding`. Two settings are supported: padding 8 / content 40 (current: POWER and the buttons 40 x 40 or 40 high, dropdown 40) and padding 4 / content 48 (the buttons and the dropdown 48 high). Switching is the change of this one token; the Button, IconButton and dropdown recipes have both sizes (`button.size.40|48`, `selector.dropdown.size.40|48`). The tests assert the margin against the token, so both settings keep the same 8-or-4 px all around.

## Skin: the oscillator headers (owner observation 2026-11-28)

The skin definitions `oscHeaderAbc`, `oscHeaderSub`, `oscHeaderNoise`, `oscHeaderNoiseSample` in `skin/defs_osc.json` are 1280 x 56 Header instances of the catalogue examples `osc-abc`, `osc-sub`, `osc-noise`, `osc-noise-sample`; each holds BOTH power states (no `...Off` twin). They are generated by `scripts/header-skin.py` from the specs above (layout from `header.layout()`, textless faces from `header.skin_parts()` to `skin/hdr_*.png`). The cells inside (`oscHeaderType`, `Table`, `Source`, `Browse`, `Waveform`, `Oct`, `Kind`, `KindWide`, `Sound`, `Route`) keep the former ad hoc cells' actions, overlays and handles; only the faces, the value label and the bounds follow the Header. Everything is bound by handle remap names (`HdrTitle`, `HdrPower`, `HdrOn`, `HdrOff`, `HdrType`, `HdrSource`, `HdrBrowse`, `HdrRoute`, `HdrWave`, `HdrOct`, `HdrKind`, `HdrSound`, `HdrGate1/2`, `HdrOscGate`); one instance per form sits in `oscNativeSettingsLayer` with the gating of the former `Osc A` / `Sub` / `Noise` cells (`skin-osc.json`).

- The page title is the header's Name label of the power parameter (`WithName[OSC A]/...`), not a tab caption any more; the three titles OSC A/B/C share one slot (`title_slot` 67 px) so POWER never moves between the pages.
- Layout (px, row 8 px from the top): POWER 83 (Sub 63, Noise 86 by the title width), TYPE 131..351, SOURCE 359..1023, BROWSE 1031..1114, the Filter route dropdown (150 px) 1122..1272 on every form. Sub: WAVEFORM fills, OCT is a number field (name outside, 68 px value box). Noise: KIND 220 + SOUND fills; with a sample the KIND dropdown takes the whole centre.
- SOURCE is a dropdown (value centred, chevron); its hit area is the former "Open" button (id 2 of 4). The Previous / Next arrow buttons (ids 1 and 3) are gone with the stepper (dropdowns replace left/right selectors).
- Recipe extensions made for it: right-zone `dropdown` (the Filter route), centre `field` (name + number box), `title_slot`, and `skin_parts()` (textless faces plus the disabled look).
- C++: `layoutFull` (`shell-mpc/overlay/osc_model.h`) follows the TYPE / SOURCE / BROWSE rectangles of the abc column case. The Sub / Noise rows of that function keep their old generic layout (plain data only).
- The examples 7-11 of the catalogue are new and not yet approved by the owner (`approved` in `catalog.json` stays as it was).

## Skin: the curve and Mod headers (owner order 2026-11-28)

Two more skin families use the same tool (`skin-curve.json`, `skin-mod.json`); their headers have NO POWER, so the form has no `Power` button and no `HdrOn` / `HdrOff` gates, and no disabled look (an instance without `power` gives `title_handle`, any always-valid parameter: the title is `WithName[<title>]/<title_handle>`; `skin.title_gate: true` with `title_gate_show: "Show"` lets the title alone follow `HdrTitleGate`). The instances sit in the tab bodies (`"tab"`, `"replaces"` in the skin spec) and replace the ad hoc cells; the tool also deletes those from their definitions (`"retire"`).

- **Note / Velo** (`curveHeader`, one form, two instances `Note Header` / `Velo Header` in the tabs Note and Velo; example `curve`): title FILTER NOTE / FILTER VELO (slot 146 px, the title gate is the list page `IndexedEnabling/0/2/Parameter 3588` as the old caption's), right zone INIT (id 8 of 13), UNDO (9), DEL POINT (10), all on `Parameter 2870`, gated by the curve surface `RangedEnabling/3/4/5/Parameter 2842`. The three ad hoc buttons left `filterNativeCurveSurface`. The buttons are the Button recipe at 40 px (INIT 48, UNDO 63, DEL POINT 94 px wide, right aligned) instead of the 130 / 132 x 56 pictures.
- **Mod matrix page** (`modHeader`, example `mod`, instance `Mod Header` in the Mod tab): title MOD MATRIX (title gate `NonZero/Parameter 3806`), the mode switch ALL / BY SOURCE / BY DEST (`segment`, ids 1..3 of 4 on `Parameter 2936`, the selected one in the RadioGrid selected look), the group dropdown (260 px; the source cell `IndexedEnabling/2/4/Parameter 2936` on `Parameter 2937` and the target cell `IndexedEnabling/3/4/...` on `2938` are alternatives, both Show Overlay `fx editor choice menu`), right: the slot count (`label`, `Parameter 3807`) and ADD (`FromGesture/Parameter 2939`); all gated by the matrix page `RangedEnabling/1/3/4/Parameter 2936`. `modNative_mode1/2/3_80x56` and `modNative_group_260x56` (and their pictures) are gone. NOT in the header: the detail page row (BACK, DELETE, SOURCE, TARGET ..., detail title) and the picker row (BACK, GROUPS, title): they are other pages of the same screen with other contents in the same row; the spec describes ONE row.

Proof (per tab Note, Velo, Mod): the resolved records (Buttons and value Labels: handle remap resolved, button id and group, the union of the gates, the enclosing actions) before and after are the same set; the focus flags and the unused `Caption` remaps of the old mode / group cells are not compared.

### Filter 1 / 2: NOT built into the skin (example `filter-screen`, catalogue only)

The Filter screen has no header band: POWER (16, 80) and the title (82, 80) sit beside the response display, the chain selector (72, 20), the F1 + F2 button (364, 16), MODEL (464, 16), STYLE (859, 16) and CLEAN (1176, 16) are 56 px high controls on a 72 px toolbar (the C++ rects in `filter_geometry.h` and `filter_ui.h` were not checked here). The example shows what the Header would be. It cannot be wired as the Osc pattern without a decision: (1) the style dropdown and CLEAN exist once per filter model (about 20 style cells and several CLEAN cells, each bound to another parameter and gated by `IndexedEnabling/<k>/27/Parameter 2860`): a form binds ONE parameter per role, so every model would need its own form instance; (2) MODEL / STYLE / CLEAN have a disabled twin per filter (F1 off, F2 off) with the displayed text of the OTHER filter's parameters, not a POWER gate; (3) moving POWER and the title up into a 56 px row shifts the response display and the Q-link area: a C++ geometry change.

## Skin: Master, Tuning, Voice, Macros (owner order 2026-11-28)

The four screens have no POWER: the header is the title and, on Tuning and Voice, controls. Each is a 1280 x 56 instance of a form in the screen's family file (`skin/defs_master.json` `masterHeader`, `defs_tuning.json` `tuningHeader` + cell `tuningHeaderScale`, `defs_voice.json` `voiceHeader` + cells `voiceHeaderStep`, `voiceHeaderLength`, `voiceHeaderStepValue`, `defs_macro.json` `macroHeader`), generated from `skin-master.json`, `skin-tuning.json`, `skin-voice.json`, `skin-macro.json` (examples `master`, `tuning`, `voice`, `macros`). Every handle, gate, button id and action is the former one (checked: the resolved Button / Label / action records of every tab before and after are identical, label font sizes aside); positions and faces follow the Header (title at 8 px, controls 40 px high in the content row; the old rows were 56 px high at y 4, the old title at x 64).

- **Master**: the title `MASTER` (the former `Native Caption master header 0`, same font and colour, now the Header title slot).
- **Tuning**: `TUNING & MPE` and the scale-input toggle (`Master Scale Input` -> cell `Scale Input`, handle `Parameter 3657`, gated by the Tuning view): a text button, 520 px wide, resting text `ACTIVATE SCALE INPUT` (offImage) and, while the mode is on, `KEY MODE · SELECT ROOT · TAP TO PLAY` (onImage). The old picture separated the three phrases with a small square; the Header's own font draws a middle dot.
- **Voice**: `VOICE CONTROL`, STEP dropdown (220 px, overlay `fx editor choice menu`), LENGTH number field (200 px, box 80, `fx editor number menu`), the selected row's step value (228 px, box 112, name from `FromGesture/Parameter 2961`). All three keep their former gate (`RangedEnabling/0/18/33/Parameter 2675`, bound as `HdrPage`) and are their own hardware focus stops (instance `Children`, cells `Yes`); the old three definitions `voiceNativeStepSelect`, `voiceNativeNumber`, `voiceNativeStepValue` and their faces are gone.
- **Macros**: the title `MACROS`, gated as before (`RangedEnabling/0/18/33/Parameter 2675`, `IndexedEnabling/0/2/Parameter 3835`). The targets sub-page and the picker have their own titles and are page content, not this header.
- **Page markers**: the title now starts at x 8, over the old marker spots (tab y 24), so the markers and the instance picture of these four views moved to tab y 1 (page y 111, the spot Play and FX use; x unchanged): `skin/TUI.json`, `shell-mpc/overlay/overlay_layout.json` and `overlay_layout.h`, `docs/dsp-knowledge/mpc-display.md`. Device check of the detection: open.

## Not yet: FX and Play (the spec cannot express them)

- **FX** (`fxNativeHeader`, the row shown while an effect is open): power 56 x 56 at x 8, a TWO-line title (the rack path above the effect name, `Name` Labels of the `Rack` and `Power` handles), a notice Label that replaces the title in some states, the RACK selector as a two-line caption-over-value box (`fxNativeHeaderRack` / `fxNativeHeaderPoolRack`, two overlays by rack kind) and a REMOVE text button on the right. The Header has a one-line title, a 40 px POWER, a value-only dropdown and no right-zone text button. The panel page strip and the band tiles in the same definition (y 64 and 376) and the chain cards, MOVE, +ADD are page content, not the header.
- **Play** (the three column headers: power 6,10 56 x 56; selector 62,10 360 / 288 x 56 with the 72 x 56 route field; envelope 446,10 388 x 56; LFO 864,10 396 x 56): the order is to keep these rects exactly (they align with the display containers and are mirrored in `shell-mpc/ui/play_rows.h`, `osc_model.h`, `lfo_layout.h`, `play_osc_native_contract.h`). The Header row is 56 high with 8 px padding and 40 px controls, the dropdown sizes are 32 / 40 / 48, and the column x offsets are not a zone flow, so the rects cannot come out of a spec. Needed first: an owner decision between the Header geometry in the Play thirds (rects move, the C++ mirrors change with them) and a Header variant with a 56 px content row, no padding and per-slot x.
