# HEADER (design proposal v0, catalogue only)

The full-width top row of a page or of a Play panel. No screen, skin or overlay uses it yet; the catalogue (`kepes-katalogus.html`) shows eight examples. Source: `components/native/header/header.py`, examples: `components/native/header/catalog.json`, test: `scripts/tests/test_native_header_catalog.py`.

## Geometry (tokens in `components/sizes.json`)

| Token | px | Meaning |
|---|---|---|
| `header.height` | 56 | the row |
| `header.padding` | 4 | top and bottom; all content is `height - 2 * padding` = 48 px (`height.medium`) |
| `header.gap` | 8 | between components |
| `header.side` | 8 | left and right padding |
| `header.page.width` | 1280 (`screen.width`) | a page; the display tab area is 1280 x 628 |
| `header.panel.width` | 400 | a Play panel |
| `header.dropdown.min_width` | 220 | width of a fixed dropdown, minimum width of the flexible (fill) one |

## Zones

Order in a page header: [title] [POWER] [page controls] ... [common buttons].

- LEFT: the screenTitle first, at the side padding (the Label component's screenTitle: `font.page_title`, `text.value`; its width comes from the text), then the module POWER button (48 x 48; only on pages or panels of modules that can be switched off) one `header.gap` after the title. Modulators (Env, LFO, RND) and Master have no POWER: the header is just the title.
- CENTRE: page-specific controls after one gap (48 px; a dropdown is 40 px by default, centred in the 48 px content, so 8 px margin above and below: PROPOSAL, the owner chooses the height from the catalogue's three dropdown heights): dropdown (selector recipe), text button (button recipe). The centre contents per page in the catalogue are EXAMPLES, not a decision.
- Flexible control: at most one centre control (the dropdown) is marked `"fill": true` in the catalogue data. It takes all the room between the left and the right group (width = row - side paddings - the other widths - the gaps); `header.dropdown.min_width` is its minimum. Title, POWER and the common buttons keep their size.
- RIGHT: the common buttons, right-aligned to the side padding: EXPAND (maximise icon; in the full-screen state the same slot shows CLOSE, an X) and MORE.
- A Play panel has the order [POWER] [selector dropdown, fills the room: it is the panel's title] [EXPAND] at panel width (`header.panel.width`): POWER 8 px from the side, the dropdown 8 px after POWER and 8 px before EXPAND.
- POWER off: the POWER icon is unlit and the centre controls take the disabled look (`style.dim_artwork`, the artwork of `apply_control_state`). Title and common buttons stay.

## Recipes used (no new look)

The Button and IconButton recipes (`button.render_sample`, `icon_button.render_sample`; look legacy: gradient fill, no flat button anywhere in the header), `label.render_label` (title), `selector.render_dropdown_variant` (dropdown: value only, centred, no label: the old Play-panel look; placement none, align center) `font.page_title` title text. EXPAND and CLOSE (an X) are IconButtons, MORE is a text Button (no more icon).

## Add an example

Add an object to `header.examples` in `components/native/header/catalog.json` (`id`, `file`, `alt`, `caption`, `width` = `page` or `panel`, optional `power` = `on` or `off`, `title`, `centre` list of `{kind: dropdown|button, name?, value?, width: size token, text?}`, `right` list of `expand|close|more`). Then render and generate from the repo root:

```sh
python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_header_cards()"
python3 scripts/gen-native-component-catalog.py
```

The test checks that every example is 56 px high, content 48 px at y = 4, zones in order without overlap, nothing wider than the room.

## Dropdown height and look

The Header dropdown is the Selector's value-only, centred variant (`selector.render_dropdown_variant`, placement `none`, align `center`) with the Voice selector's contrast: fill `selector.dropdown.top` #141a38 to `selector.dropdown.bottom` #0b0f24, keyline #27306a. It exists at 32 / 40 / 48 px (`selector.dropdown.size.<px>`: value em 18 / 22 / 24, chevron, `pad_x`); an example picks its height with the optional `height` field (a `height.*` token), default `height.intermediate` (40). The skin's dropdowns keep the `neon` look.

## No left/right stepper

The Header has no left/right stepper (arrow selector, carousel): every choice is the dropdown (value centred, 40 px). The Osc A example's SOURCE control is a dropdown; the stepper kind was removed from the Header module. A value that does not fit is left-aligned and cut with an ellipsis, ending `selector.dropdown.chevron_gap` (4 px) before the chevron lane (available width = box - `pad_x` - chevron lane - chevron gap).
