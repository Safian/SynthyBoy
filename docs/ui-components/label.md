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

Size and token table only (no pictures for the sizes). Tokens `label.dial.<px>` in `components/sizes.json` (`diameter`, `caption`, `value`, `proposal`); shown as a small table on the catalogue page (`label.dial_sizes`):

| Dial | Token | Caption | Value | Status |
|---|---|---|---|---|
| 36 | `label.dial.36` | `font.annotation` 11 | `font.small` 12 | PROPOSAL |
| 40 (compact) | `label.dial.40` | 14 | 16 | in use (`pan.compact_pan_definition`) |
| 48 | `label.dial.48` | `font.caption` 15 | `font.value` 20 | PROPOSAL |
| 56 | `label.dial.56` | `font.caption` 15 | `font.value.large` 22 | PROPOSAL |
| 90 (normal) | `label.dial.90` | 15 | 24 | in use (`pan.sub_dial_definition`) |

A new size: a `label.dial.<px>` token and one `dial_sizes` entry (`id`, `label`, `token`) in the catalogue file.

### 48 px dial, three layouts, enabled and disabled

One sample size (48 px, mid state of the text-free dial strip, Pan texts `PAN` / `L 100`), six pictures from `label.dial_examples` (`{id, layout, state, file, alt, caption}`; captions live in the HTML, not the image):

| Layout (`layout`) | Content |
|---|---|
| Lent (`bottom`) | only the value under the dial |
| Fent és lent (`top`) | title above, the dial, the value below |
| Jobbra (`right`) | title above value, to the right of the dial |

Title and value share one text style (`level.DIAL_TEXT_STYLE`). `disabled` = the inactive dial look (`inactiveGrey`, no gradient arc) with the text colour dimmed like every other disabled control (`style.dim_artwork`, as the On/Off label). Drawn by `level.render_dial_labelled(..., disabled=True)` (default off, so the Pan/Level images are unchanged). Files: `components/native/label/img/label-dial-48-<lent|fent-lent|jobbra>-<enabled|disabled>.png`. More examples: add an item to `dial_examples`.

## Add a variant

Add an object to `label.variants` (`id`, `label`, `text`, `font`, `unit` = `tui` or `em`, `colour`, `case`, `file`, `alt`), then:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly, there is no generator.
