# ICON BUTTON (catalogue)

The Button recipe with an icon where the text is: square (width = height) at 32 / 40 / 48 / 56 px. Source: `components/native/icon_button/icon_button.py`, which calls `button.action_art(icon=..., height=..., look=...)`; samples: `components/native/icon_button/catalog.json` (`icon_button.samples`); test: `scripts/tests/test_native_button_catalog.py`. Face tokens and the gradient: [button.md](button.md).

## Icons and states

- Icons: the recipes POWER and CLOSE (geometry in `button.art`, scaled about the box centre for the other heights). CLOSE is an X: two diagonals, `button.art.close.half` 9.6 (the X is 40% of the 48 px button height), round ends (`button.art.close.stroke` = 2.5). MORE has no icon recipe and stays a text button; a new icon is a new recipe in `action_art`, not a catalogue-only drawing.
- POWER on: `accent.green`; off: `text.icon`; disabled: `style.dim_artwork` of the face (no new colour).
- Look `filled`: the Button's legacy face. Look `ghost`: no fill, no keyline, transparent, only the icon; for example the on/off at the top of the mixer. `action_art(look="ghost")` is a parameter of the Button recipe, not separate code.

## Samples in the catalogue

POWER on and off and CLOSE at 32 / 40 / 48 / 56; POWER and CLOSE disabled at 48; POWER on and off and CLOSE in ghost at 40 and 48. The list is the catalogue data: a new state or size is one more entry (`id`, `file`, `alt`, `caption`, `icon` = `power`|`close`, `height` token, `state` = `on`|`off`|`disabled`, `look` = `filled`|`ghost`), then:

Two transparent list-arrow glyphs live under `glyphs` in `catalog.json` (`render_glyph`): `back_arrow` (64x64 cell, 28 px arrow, `skin/native_list_back_arrow.png`) for the deeper-level list header and `row_arrow` (40x40, 20 px arrow, `skin/native_list_row_arrow.png`) for rows that open a deeper level.

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.
