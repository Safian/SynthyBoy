# ICON BUTTON (catalogue)

The Button recipe with an icon where the text is: square (width = height) at 32 / 40 / 48 / 56 px. Source: `components/native/icon_button/icon_button.py`, which calls `button.action_art(icon=..., height=..., look=...)`; samples: `components/native/icon_button/catalog.json` (`icon_button.samples`); test: `scripts/tests/test_native_button_catalog.py`. Face tokens and the gradient: [button.md](button.md).

## Icons and states

- Icons: the existing recipes POWER and EXPAND (geometry in `button.art`, scaled about the box centre for the other heights). CLOSE and MORE have no icon recipe (the Header uses text buttons for them); a new icon is a new recipe in `action_art`, not a catalogue-only drawing.
- POWER on: `accent.green`; off: `text.icon`; disabled: `style.dim_artwork` of the face (no new colour).
- Look `filled`: the Button's legacy face. Look `ghost`: no fill, no keyline, transparent, only the icon; for example the on/off at the top of the mixer. `action_art(look="ghost")` is a parameter of the Button recipe, not separate code.

## Samples in the catalogue

POWER on and off, EXPAND at 32 / 40 / 48 / 56; POWER and EXPAND disabled at 48; POWER on and off in ghost at 40 and 48. The list is the catalogue data: a new state or size is one more entry (`id`, `file`, `alt`, `caption`, `icon` = `power`|`expand`, `height` token, `state` = `on`|`off`|`disabled`, `look` = `filled`|`ghost`), then:

```sh
python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_button_cards()"
python3 scripts/gen-native-component-catalog.py
```
