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
| `header.dropdown.width`, `header.selector.width` | 220, 200 | example widths of the centre controls |

## Zones

- LEFT: the module POWER button (48 x 48; only on pages or panels of modules that can be switched off), then the screenTitle (page title: `font.page_title`, `text.value`, the style of the page captions). Modulators (Env, LFO, RND) have no POWER: the title starts at the side padding, no empty slot.
- CENTRE: page-specific controls (48 px; a dropdown is 40 px by default, centred in the 48 px content, so 8 px margin above and below: PROPOSAL, the owner chooses the height from the catalogue's three dropdown heights) after one gap: dropdown (selector recipe), text button (button recipe). The centre contents per page in the catalogue are EXAMPLES, not a decision.
- RIGHT: the common buttons, right-aligned to the side padding: EXPAND (maximise icon; in the full-screen state the same slot shows CLOSE) and MORE.
- A Play panel uses the same header at panel width: POWER, a selector dropdown (Sub, Env 1, LFO 1), EXPAND.
- POWER off: the POWER icon is unlit and the centre controls take the disabled look (`style.dim_artwork`, the artwork of `apply_control_state`). Title and common buttons stay.

## Recipes used (no new look)

`button.action_art` (power icon, expand icon, text buttons), `selector.render_dropdown_variant` (dropdown: value only, centred, no label: the old Play-panel look; placement none, align center) and `selector.render_selector_face` (stepper), `font.page_title` title text. CLOSE and MORE are text buttons; there is no close or more icon recipe.

## Add an example

Add an object to `header.examples` in `components/native/header/catalog.json` (`id`, `file`, `alt`, `caption`, `width` = `page` or `panel`, optional `power` = `on` or `off`, `title`, `centre` list of `{kind: dropdown|stepper|button, name?, value?, width: size token, text?}`, `right` list of `expand|close|more`). Then render and generate from the repo root:

```sh
python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_header_cards()"
python3 scripts/gen-native-component-catalog.py
```

The test checks that every example is 56 px high, content 48 px at y = 4, zones in order without overlap, nothing wider than the room.

## Dropdown height and look

The Header dropdown is the Selector's value-only, centred variant (`selector.render_dropdown_variant`, placement `none`, align `center`) with the Voice selector's contrast: fill `selector.dropdown.top` #141a38 to `selector.dropdown.bottom` #0b0f24, keyline #27306a. It exists at 32 / 40 / 48 px (`selector.dropdown.size.<px>`: value em 18 / 22 / 24, chevron, `pad_x`); an example picks its height with the optional `height` field (a `height.*` token), default `height.intermediate` (40). The skin's dropdowns keep the `neon` look.

## No left/right stepper

The Header has no left/right stepper (arrow selector, carousel): every choice is the dropdown (value centred, 40 px). The Osc A example's SOURCE control is a dropdown; the stepper kind was removed from the Header module. A value that does not fit is left-aligned and cut with an ellipsis, ending `selector.dropdown.chevron_gap` (10 px) before the chevron lane (available width = box - `pad_x` - chevron lane - chevron gap).
