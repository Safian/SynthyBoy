# WAVE ICONS (catalogue: "Hullámforma ikonok")

The waveform pictograms: sine, saw, square, pulse (narrow square), triangle, sh (stepped random, S&H) and noise (jagged random). Recipes: `selector.render_wave_icon(width, height, color, shape=...)` in `components/native/selector/selector.py` (`WAVE_SHAPES`); sheets: `components/native/wave_icons/wave_icons.py`, `catalog.json` (`wave_icons.samples`); test: `scripts/tests/test_native_wave_icons_catalog.py`.

## Recipe

- Every shape uses the sine's box (default 28 x 24, the RotarySelector uses `rotary_selector.icon` 24 x 12), the margins `selector.wave_icon.points` (4 px each side), the amplitude (38% of the height around the middle), the stroke `stroke.icon` (2 px) and the supersampling `selector.wave_icon.scale`, drawn as a polyline with round joints and round end caps. `shape="sine"` is the original drawing, unchanged.
- Shapes (t along the icon 0..1, y -1..1): saw = two ramps with a drop; square = one period starting up; pulse = high between t 0.4 and 0.6; triangle = one period; sh = 5 steps with the levels `wave_icons.sh_levels`; noise = 11 jagged points `wave_icons.noise_levels`. The levels are fixed token lists (no random generator in the render).
- Colours: no new colour. Selected `accent.cyan`, the others `selector.wave` (blue-grey), as in the RotarySelector. The check mark `render_pipa_icon` is a separate recipe.

## Catalogue

Two sheets with all seven icons in the order sine, saw, square, pulse, triangle, sh, noise: 1:1 (the saw selected) and magnified 4 times (nearest neighbour, the real pixels). Footprints in the catalogue table. A new sheet is one more entry (`id`, `file`, `alt`, `caption`, `zoom`, `selected` index or null), then:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly, there is no generator.

## Native skin use

One static picture per icon (shape x selected / not) at the icon box size, or the host draws them from the same recipe; the RotarySelector places them on the ring at the stop angles.
