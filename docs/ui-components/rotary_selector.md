# ROTARY SELECTOR (catalogue)

A stepped rotary dial with pictograms around it: 3 to 6 positions, the pointer jumps to the angle of the chosen one. Source: `components/native/rotary_selector/rotary_selector.py`; samples: `components/native/rotary_selector/catalog.json` (`rotary_selector.samples`); test: `scripts/tests/test_native_rotary_selector_catalog.py`. Overview of the radio family: [radio_family.md](radio_family.md).

## Structure

- Dial: the [SteppedDial](stepped_dial.md) (the Level dial recipe without the gradient value arc: dark track, a dot per stop, white thumb and pointer), diameter 48 or 56 px (`height.medium` / `height.high`), N = 3 to 6 here. The pointer angle is `knob.start.degrees` (135) + i x `knob.sweep.degrees` (270) / (N - 1): N evenly spaced stops, discrete frames.
- Pictograms: on a ring around the dial at the same angles, `rotary_selector.ring_gap` (4) clear of the dial. Boxes `rotary_selector.icon` 24 x 12. The selected one `accent.cyan`, the others `selector.wave`.
- Title: Label dial-caption style (`pan.sub_dial_definition.style.font.height`, `text.caption`, upper case), centred on top, `title_gap` 4 above the ring.

## Pictograms

The recipes of [wave_icons.md](wave_icons.md): `selector.render_wave_icon(..., shape=...)` with the shapes sine, saw, square, pulse, triangle, sh and noise, plus the check mark `render_pipa_icon`. A sample names one icon per stop in `icons` (3 to 6). The catalogue's WAVE examples show saw, square, pulse and noise (4 stops), sine, square, saw (3) and sine, saw, square, pulse, triangle, noise (6).

## States and tokens

enabled / disabled (`render_dial_frame(active=False)` and `style.dim_artwork` of the whole picture, existing `inactive.preview.*`). New colours: none. New size tokens (`rotary_selector` in `components/sizes.json`): `icon` {width 24, height 12}, `ring_gap` 4, `title_gap` 4, `margin` 1, `title_font` (ref to the dial-caption font).

## Catalogue samples

4 stops at stop 2 and stop 3, 3 stops, 6 stops (48 px dial), 4 stops at 56 px, disabled. The footprint of each is in the catalogue table. A new example is one more entry (`id`, `file`, `alt`, `caption`, `icons`, `position`, `diameter` token, `title`, `state`), then:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.

## Native skin item (not implemented)

An N-frame strip: frame i = the dial with the pointer at stop i (the dial part only, `render_dial_frame(diameter, i / (N - 1))`, 48 x 48 or 56 x 56; N = 3..6 frames; inactive strip N more through `dim_artwork`). The pictograms are one static picture (the ring of N glyphs with no dial, the size of `layout()["size"]`) and the title a host Label; the selected glyph's `accent.cyan` is a second static picture per stop (N pictures) or the host's highlight of the glyph. The dial frame is placed at `layout()["centre"]`.
