# SLIDE SWITCH (catalogue)

A positional slider switch with 2 to 4 positions, horizontal or vertical: a recessed track with a round thumb that jumps to one of N evenly spaced positions. Source: `components/native/slide_switch/slide_switch.py`; samples: `components/native/slide_switch/catalog.json` (`slide_switch.samples`); test: `scripts/tests/test_native_slide_switch_catalog.py`. Overview of the radio family: [radio_family.md](radio_family.md).

## Structure

- Track: pill (radius = thickness / 2), thickness 32 / 40 / 48 px (`button.size.<px>`), `length` px long (horizontal: width, for example 3 positions 160 px; vertical: height, for example 3 x 40 = 120 px). Recessed: the Button gradient turned over, `surface.control` at the top to `button.gradient.top` at the bottom (`control.gradient` span), keyline `button.gradient.border` over `button.gradient.border.bottom`; the top edge is the dark one.
- Thumb: diameter thickness - 2 x `slide_switch.inset` (4), vertical gradient `text.icon` to `text.caption`, a thin shadow (`ink.black`, `slide_switch.shadow` offset 1, blur 1.5, alpha 130), and a thin `accent.cyan` line across its centre (`slide_switch.line`: 50% of the thumb radius each way, 2 px): a vertical line in a horizontal switch, a horizontal line in a vertical one.
- Positions: thumb centres at thickness / 2 + i x (length - thickness) / (N - 1) along the track; the thumb touches the track ends with an even `inset`.
- Labels: Label recipe (`label.render_label`, `font.label` as em, selected `text.value`, others `text.caption`). Horizontal: above the track, centred on each position (clamped to the picture edge), `slide_switch.label_gap` (6) above the track. Vertical: right of the track, `label_gap` from it, left aligned, centred on the position height.
- Title (optional, vertical examples): Label block-title style (`panel.title` font, `text.caption`), top left above the track, `slide_switch.title_gap` (6).

## States and tokens

enabled: as above; disabled: `style.dim_artwork` of the whole picture (existing `inactive.preview.*`). New colours: none. New size tokens (`slide_switch` in `components/sizes.json`): `scale` 4, `inset` 4, `label_gap` 6, `title_gap` 6, `label_font` (ref `font.label`), `shadow`, `line`.

## Catalogue samples

L M H horizontal at all three positions (40 px track, 160 wide); Mono / Poly at both positions (32 px, 112 wide); 1 2 3 4 at position 3 (48 px, 240 wide); vertical PWM LFO / MAN / ENV at all three positions (40 px, 120 high); vertical two positions (VOICE); disabled horizontal and vertical. The footprint of every example is in the catalogue table. A new example is one more entry (`id`, `file`, `alt`, `caption`, `orientation`, `labels`, `position`, `size` token, `length`, `title`, `state`), then:

```sh
python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_slide_switch_cards()"
python3 scripts/gen-native-component-catalog.py
```

## Native skin item (not implemented)

One native item with N frames (a filmstrip, Data handle = position index): frame i = track + thumb at position i + its centre line, without labels, at the track's size (horizontal: length x thickness; vertical: thickness x length). N = 2 to 4 frames. The labels and the title are host Labels (static, placed at `layout()` label boxes; the selected one switches colour by the same handle) so the strip carries no text. Disabled: the same N frames through `dim_artwork` (N more) or the host's inactive handling of native widgets.
