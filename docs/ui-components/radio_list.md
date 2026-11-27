# RADIO LIST (catalogue)

A list of 2 or more named options, each a row with a small round LED and a label; exactly one row is selected (its LED is lit). Vertical or horizontal. Source: `components/native/radio_list/radio_list.py`; samples: `components/native/radio_list/catalog.json` (`radio_list.samples`); test: `scripts/tests/test_native_radio_list_catalog.py`. The existing [RadioButton](radio-kizaro-valaszto-preview.md) entry is unchanged.

## Structure

- Row = the Button's legacy face (`button.gradient_face`, `control.gradient`, `button.gradient.top/bottom/border/border.bottom`), height 32 / 40 / 48 px (`button.size.<px>` radius and font). Rows are `radio_list.gap` (4) apart. The list has no panel of its own.
- LED: diameter `radio_list.led_ratio` (0.4) of the row height (13 / 16 / 19 px), centred vertically, `radio_list.pad_x` (12) from the row's left edge, rim `radio_list.rim` (1 px).
- Label: Label recipe (`label.render_label`, colour `text.value`, font of `button.size.<px>` as em, the size a Button of that height uses), left aligned, its left ink edge `radio_list.led_gap` (10) right of the LED, vertically centred on its ink.
- Width: `width` is the row width (vertical list) or the item width (horizontal row).

## States and tokens

| State | LED | Row |
|---|---|---|
| selected | fill `accent.cyan`, rim `radio.active.border`, soft glow (blurred disc of the same colour: `radio_list.glow` spread 0.3 and blur 0.2 of the LED diameter, alpha 150) | gradient face plus a faint `accent.cyan` highlight (`radio_list.highlight_alpha` 36 of 255) |
| unselected | fill `surface.control`, rim `control.border` | gradient face |
| disabled | whole list through `style.dim_artwork` (existing `inactive.preview.*`), selection still visible as the brighter LED | same |

New colours: none. New size tokens (`components/sizes.json`, `radio_list`): `scale` 4 (supersampling), `gap` 4 (between rows), `pad_x` 12, `led_gap` 10, `led_ratio` 0.4 (the owner's 40%), `rim` 1, `glow` {`spread_ratio`, `blur_ratio`, `alpha`} (no existing LED glow recipe: the filter chain lamps are flat bars, so the glow is a plain blurred disc in `accent.cyan`), `highlight_alpha` 36.

## Catalogue samples

six rows TRI SIN SAW SQR S&H RND with TRI selected at 40 px; the same at 32 and 48 px with SAW selected; disabled six rows; horizontal HZ BPM KEY FREE with BPM selected at 40 px; two rows Mono / Poly. A new example is one more entry (`id`, `file`, `alt`, `caption`, `orientation`, `items`, `selected`, `height` token, `width`, `state`), then:

```sh
python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_radio_list_cards()"
python3 scripts/gen-native-component-catalog.py
```

## Native skin item (not implemented)

A host skin builds it from the same recipe with N rows as one native item per row (Button group, `buttonId` 1..N, `numButtonsInGroup` N, shared Data handle). Per row at the chosen height and width, 2 frames (a Button's off / on image):

1. `off`: row face + dark LED, no label (the label is a host Label, `text.value`, left edge at `label_x` = 12 + LED diameter + 10);
2. `on`: row face + highlight + lit LED with glow, no label.

So 2 images per distinct row size (for example 120 x 40: 2 images); a 6-row list reuses them for all 6 rows. Disabled: the same two images through `dim_artwork` (apply_control_state does this for native widgets), 2 more images. Hit areas come from the Button bounds.
