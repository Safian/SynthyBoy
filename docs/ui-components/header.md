# HEADER (design proposal v0; the oscillator screens use it)

The full-width top row of a page or of a Play third. The oscillator screen family (Osc A/B/C, Sub, Noise) uses it in the skin (see "Skin: the oscillator headers" below), and so do Master, Tuning, Voice and Macros ("Skin: Master, Tuning, Voice, Macros"); FX and Play do not yet ("Not yet: FX and Play"). The catalogue (`kepes-katalogus.html`) shows fourteen examples. Source: `components/native/header/header.py`, examples: `components/native/header/catalog.json`, test: `scripts/tests/test_native_header_catalog.py`.

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
| `hwfocus` | the cell instance's acceptsHWFocus in the form (default `No`: the Osc cells are reached through the header instance; `Yes` for a cell that is its own focus stop, the Voice controls) |

`role` gives the remap name `Hdr<Role>` (`type` -> `HdrType`) that the instance binds to a parameter. A `gate` entry is a remap name as well (`HdrPage`, `HdrView`): the instance binds it to the gating handle.

A header WITHOUT a `power` slot (Master, Tuning, Voice, Macros) has no `HdrOn` / `HdrOff` state and no disabled look: its title is a static Text Label (the spec's text; `HdrTitle` is bound to `Parameter 0` as the former caption was) as wide as the room up to the next control, the cells carry only their own gates.

### Instances and the tool

The instances (which parameters, which page gate) of one screen family are a list in `components/native/header/skin-<family>.json` (`skin-osc.json`): `{"name", "form" (an example id), "title", "power": "Parameter N", "data", "gate", "remap": {"Hdr<Role>": handle, ...}}`; optional `show` (default `Hide`), `focus` (acceptsHWFocus of the instance, default `Yes`; `Children` where the cells are their own focus stops, `No` for a title only) and, instead of the family's `layer`, `tab` (the instance sits in that tab of `skin/TUI.json`) with `replaces` (the names of the former ad hoc items it takes the place of; they are removed). The tool derives `HdrTitle` (`WithName[<title>]/<power>`), `HdrPower`, `HdrOn` / `HdrOff` (`IndexedEnabling/1|0/2/<power>`) from `power`. Hand-run, nothing at build time:

```
python3 scripts/header-skin.py components/native/header/skin-osc.json     # definitions + instances + faces (skin/hdr_*.png)
python3 scripts/skin-format.py && python3 scripts/skin-baseline-update.py   # as after every skin edit
```

The tool replaces every definition and instance whose name starts with the family prefix (`oscHeader`), in place, and removes `hdr_*.png` faces no skin file names any more (the families share the directory); the test `HeaderSkinSpecTests` fails if the committed skin differs from what the spec produces. Rolling the Header out to another screen = one `skin` block per example, one `skin-<family>.json`, one tool run.

### Why the number of definitions is small

The Osc family needed 29 header definitions (four forms each with an `...Off` twin, nine cells each with an `...Off` twin, three route definitions); now 16 (4 forms, 9 cells, 3 route definitions), the 18 faces (`hdr_*.png`) are the same. The power state is no longer a separate definition: the form holds the enabled cell instance and the inert pictures (Image + value Label) of the disabled look side by side, gated by the instance's `HdrOn` / `HdrOff` names (the same IndexedEnabling gates the Off twins used). What is NOT folded, and why:

- The cells stay one per role and width: the Show Overlay action names its overlay in `additionalData`, a definition-level string that no handle can change, and a face of another width is another picture. A cell shrunk to a transparent hit area (one per overlay, no width) needs a device probe that a definition without a visible child still receives Mouse Down (not measured; open in TODO.md).
- The route keeps the NativeActive / NativeInactive wrapper (3 definitions shared by all forms): its disabled look follows the Filter routing gates, not POWER, and its focus chain (`acceptsHWFocus` No on the wrapper, Children on the active child) is the proven one; folding it into the forms would repeat the inactive look in four forms and change the focus chain on no evidence.
- The inert disabled `Button`s of the old Off cells (Browse, the Table's Open hit button; ignoreMousePresses on, no hardware focus) are plain pictures in the form now: a Button in the form would take presses; a never-pressed one has no function.

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
