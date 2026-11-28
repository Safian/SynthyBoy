# BUTTON (catalogue, native look)

The native text button. The catalogue shows the old Play-panel look at four heights. The skin keeps its flat buttons for now (the skin switch is a separate, later round). Source: `components/native/button/button.py` (`action_art`), samples: `components/native/button/catalog.json` (`button.samples`), test: `scripts/tests/test_native_button_catalog.py`. The C++ buttons are in [buttons.md](buttons.md).

## Looks (`action_art(look=...)`)

- `legacy` (the old Play-panel face, the catalogue's one look): rounded box, vertical gradient fill `button.gradient.top` #131930 down to `button.gradient.bottom` #0d1122, light-blue keyline `button.gradient.border` #151c3a (top) to `button.gradient.border.bottom` #161e42 (bottom). Measured on the owner's "+ ADD" crop: keyline (21,28,58), fill constant (19,25,48) for the first third, then linear to (13,17,34).
- `ghost`: no fill, no keyline, only the label or icon on a transparent picture (see [icon_button.md](icon_button.md)).
- `flat` (default; today's skin face `button.surface` / `button.border`): marked KIVEZETÉSRE; the skin keeps using it until its switch. No flat example is in the catalogue.

The gradient shape is the shared `control.gradient` (`start` 1/3, `end` 2/3 of the box height: constant above `start`, linear to `end`, constant below). The dropdown uses the same token (`selector.dropdown.gradient.*` refer to it) and the same drawing function `button.gradient_face`; the dropdown pictures stayed byte-identical.

## Sizes

`button.size.<px>` tokens (`height` ref to `height.small|intermediate|medium|high`, `font`, `radius`, text `padding`): 32 (font.label 14, radius.control 6, padding 16), 40 (font.body 16, radius.field 8, padding 20), 48 (font.button 18, `button.face.radius` 8, `button.text.padding` 24), 56 (font.value 20, radius.menu 10, padding 28). Width = text width + padding. The text and radius values for 32 / 40 / 56 are proposals; 48 is today's.

## Disabled

The disabled look is `style.dim_artwork` of the face (existing `inactive.preview.*` tokens), no new colour.

## Add a sample

Add an object to `button.samples` (`id`, `file`, `alt`, `caption`, `height` token, `text`, `state` = `enabled`|`disabled`), then:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.
