# HEADER (design proposal v0; the oscillator screens use it)

The full-width top row of a page or of a Play third. The oscillator screen family (Osc A/B/C, Sub, Noise) uses it in the skin (see "Skin: the oscillator headers" below); no other screen does yet. The catalogue (`kepes-katalogus.html`) shows eleven examples. Source: `components/native/header/header.py`, examples: `components/native/header/catalog.json`, test: `scripts/tests/test_native_header_catalog.py`.

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

## Add an example

Add an object to `header.examples` in `components/native/header/catalog.json` (`id`, `file`, `alt`, `caption`, `width` = `page` or `panel`, optional `power` = `on` or `off`, `title`, `centre` list of `{kind: dropdown|button, name?, value?, width: size token, text?}`, `right` list of `more`). Then render and generate from the repo root:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.

The test checks that every example is 56 px high, content 40 px with 8 px on all four sides (left, right, top, bottom), zones in order without overlap, nothing wider than the room.

## Dropdown height and look

The Header dropdown is the Selector's value-only, centred variant (`selector.render_dropdown_variant`, placement `none`, align `center`) with the Voice selector's contrast: fill `selector.dropdown.top` #141a38 to `selector.dropdown.bottom` #0b0f24, keyline #27306a. It exists at 32 / 40 / 48 px (`selector.dropdown.size.<px>`: value em 18 / 22 / 24, chevron, `pad_x`); an example picks its height with the optional `height` field (a `height.*` token), default `height.intermediate` (40). The skin's dropdowns keep the `neon` look.

## No left/right stepper

The Header has no left/right stepper (arrow selector, carousel): every choice is the dropdown (value centred, 40 px). The Osc A example's SOURCE control is a dropdown; the Header module has no stepper kind. A value that does not fit is left-aligned and cut with an ellipsis, ending `selector.dropdown.chevron_gap` (4 px) before the chevron lane (available width = box - `pad_x` - chevron lane - chevron gap).

## Padding setting: 8 / 40 or 4 / 48

`header.padding` is the only margin token (there is no other margin token) and the header stays 56 px high, so the content row is `56 - 2 x padding`. Two settings are supported: padding 8 / content 40 (current: POWER and the buttons 40 x 40 or 40 high, dropdown 40) and padding 4 / content 48 (the buttons and the dropdown 48 high). Switching is the change of this one token; the Button, IconButton and dropdown recipes have both sizes (`button.size.40|48`, `selector.dropdown.size.40|48`). The tests assert the margin against the token, so both settings keep the same 8-or-4 px all around.

## Skin: the oscillator headers (owner observation 2026-11-28)

The skin definitions `oscHeaderAbc`, `oscHeaderSub`, `oscHeaderNoise`, `oscHeaderNoiseSample` (each with an `...Off` twin for POWER off) in `skin/defs_osc.json` are 1280 x 56 Header instances of the catalogue examples `osc-abc`, `osc-sub`, `osc-noise`, `osc-noise-sample`: the layout comes from `header.layout()`, the textless faces from `header.skin_parts()` (written to `skin/hdr_*.png` by the build script kept in the commit message of the change). The cells inside (`oscHeaderType`, `Table`, `Source`, `Browse`, `Waveform`, `Oct`, `Kind`, `KindWide`, `Sound`, `Route`, each in an active and an `Off` / `NativeInactive` look) are built from the former ad hoc cells' actions, overlays and handles; only the faces, the value label and the bounds follow the Header. Everything is bound by handle remap names (`HdrTitle`, `HdrPower`, `HdrType`, `HdrSource`, `HdrBrowse`, `HdrRoute`, `HdrWave`, `HdrOct`, `HdrKind`, `HdrSound`, `HdrGate1/2`, `HdrOscGate` and the matching `...Caption` names); one instance per form (and per power state) sits in `oscNativeSettingsLayer` with the gating of the former `Osc A` / `Sub` / `Noise` cells.

- The page title is the header's Name label of the power parameter (`WithName[OSC A]/...`), not a tab caption any more; the three titles OSC A/B/C share one slot (`title_slot` 67 px) so POWER never moves between the pages.
- Layout (px, row 8 px from the top): POWER 83 (Sub 63, Noise 86 by the title width), TYPE 131..351, SOURCE 359..1023, BROWSE 1031..1114, the Filter route dropdown (150 px) 1122..1272 on every form. Sub: WAVEFORM fills, OCT is a number field (name outside, 68 px value box). Noise: KIND 220 + SOUND fills; with a sample the KIND dropdown takes the whole centre.
- SOURCE is a dropdown (value centred, chevron); its hit area is the former "Open" button (id 2 of 4). The Previous / Next arrow buttons (ids 1 and 3) are gone with the stepper (dropdowns replace left/right selectors).
- Recipe extensions made for it: right-zone `dropdown` (the Filter route), centre `field` (name + number box), `title_slot`, and `skin_parts()` (textless faces plus the disabled look).
- C++: `layoutFull` (`shell-mpc/overlay/osc_model.h`) follows the TYPE / SOURCE / BROWSE rectangles of the abc column case. The Sub / Noise rows of that function keep their old generic layout (plain data only).
- The examples 7-11 of the catalogue are new and not yet approved by the owner (`approved` in `catalog.json` stays as it was).
