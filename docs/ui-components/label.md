# LABEL (catalogue)

The read-only text styles of the UI, shown first in the catalogue. Source: `components/native/label/label.py`, variants: `components/native/label/catalog.json` (`label.variants`, `label.dial_sizes`), test: `scripts/tests/test_native_label_catalog.py`. No screen uses this module; it draws today's styles from the tokens.

| Variant | Font token | Unit | Colour | Case |
|---|---|---|---|---|
| screenTitle (page title; the Header uses it) | `font.page_title` (26) | em | `text.value` | as written |
| Block title (panel) | `panel.title:font` (16) | TUI height | `text.caption` | as written |
| Block title (TUNE header) | `font.small` (12) | TUI height | `text.label` | as written |
| Dial caption (PAN, LEVEL) | `pan.sub_dial_definition.style.font.height` (15) | TUI height | `text.caption` | upper |
| Dial value (`50%`, `L 100`) | `pan.sub_dial_definition.value` (24) | TUI height | `text.value` | as written |

A TUI height is a JUCE font height; the host draws em = height / `font.host.height.per.em` (`style.native_font_em`). `font.page_title` is an em (the overlay's page-title size).

The UNISON / WARP / CHARACTER block titles are drawn by the overlay panel (C++); their native twins are the two block-title variants above (panel title and TUNE header). The pictures show the native recipes, not the C++ draw.

## Text next to a dial (36 / 40 / 48 / 56 px, plus the normal 90 px)

Caption sits above the value, to the right of the dial. Tokens `label.dial.<px>` in `components/sizes.json` (`diameter`, `caption`, `value`, `proposal`):

| Dial | Caption | Value | Status |
|---|---|---|---|
| 36 | `font.annotation` 11 | `font.small` 12 | PROPOSAL |
| 40 (compact) | 14 | 16 | in use (`pan.compact_pan_definition`) |
| 48 | `font.caption` 15 | `font.value` 20 | PROPOSAL |
| 56 | `font.caption` 15 | `font.value.large` 22 | PROPOSAL |
| 90 (normal) | 15 | 24 | in use (`pan.sub_dial_definition`) |

A new size: a `label.dial.<px>` token and one `dial_sizes` entry in the catalogue file.

## Add a variant

Add an object to `label.variants` (`id`, `label`, `text`, `font`, `unit` = `tui` or `em`, `colour`, `case`, `file`, `alt`), then:

```sh
python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_label_cards()"
python3 scripts/gen-native-component-catalog.py
```
