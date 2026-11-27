# HSLIDER

A horizontal (left-right) slider: title left and value right above the slot, the contrasty fader thumb on the slot. Unipolar (0..100 %, sends B1, B2) or bipolar (PAN, -100 L .. +100 R; centre tick at 50 % of the travel). Catalogue only; the MixerStrip uses it as bottom-row variant B ([mixer_strip.md](mixer_strip.md)). Source: `components/native/hslider/hslider.py`, examples: `components/native/hslider/catalog.json`, test: `scripts/tests/test_native_mixer_strip_design.py`.

## Geometry (tokens in `components/sizes.json`)

`hslider.width` 144, `hslider.height` 40 (`height.intermediate`), `hslider.text_row` 18 (title `font.caption` upper case `text.caption`, value `font.body` `text.value`), `hslider.slot` 6 high (`surface.control`, `field.border`), `hslider.tick` 2 x 14 (`text.muted`, bipolar only; the thumb covers it at the centre), `hslider.thumb` 14 x 22 (the contrasty thumb with a vertical index line: `fader.thumb.fill` / `.edge` / `.line`). The thumb centre runs from half a thumb width to the width minus half: centre x = left + value x (width - thumb width); at 0.5 it is the width / 2 where the tick is.

## States

Enabled and disabled (`style.dim_artwork`).
