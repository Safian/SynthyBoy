# STEPPED DIAL (catalogue)

Our dial face without the gradient value arc, with 2 to 8 stops: a dark track ring, a small dot on the track at each stop angle, the white thumb and the pointer line. The pointer jumps to a stop angle (discrete frames). Source: `components/native/stepped_dial/stepped_dial.py`; samples: `components/native/stepped_dial/catalog.json` (`stepped_dial.samples`); test: `scripts/tests/test_native_stepped_dial_catalog.py`. The RotarySelector is built on it: [rotary_selector.md](rotary_selector.md); family overview: [radio_family.md](radio_family.md).

## Structure

- It is the Level / Pan dial recipe: `level.render_dial_frame(diameter, position / (N - 1), value_arc=False, stops=N)` (the Level dial geometry). `value_arc=False` leaves out the flow-gradient arc, `stops=N` adds the dots; with the defaults (`value_arc=True`, `stops=0`) the Pan and Level pictures stay byte-identical (tested by test_native_dial_catalog.py and the default-equality test here).
- Stop angle: `knob.start.degrees` (135) + i x `knob.sweep.degrees` (270) / (N - 1), clockwise from 3 o'clock; the thumb sits on the track at the selected angle.
- Stop dots: radius = half the value stroke x `knob.stop.ratio` (1.0); the selected stop in the pointer colour (under the thumb), the others in the dial's dim text colour (`textDim`).
- Diameters: `stepped_dial.diameters` = 36 / 40 / 48 / 56; stops 2 to `stepped_dial.max_stops` (8).

## States and tokens

enabled; disabled: the inactive dial (`active=False`) and `style.dim_artwork` of the picture (existing `inactive.preview.*`). New colours: none. New size tokens: `knob.stop.ratio` 1.0, `stepped_dial` {`diameters`, `max_stops`}.

## Catalogue samples

4 stops (2nd stop) at 36 / 40 / 48 / 56 px, 3 stops, 6 stops, 8 stops, disabled. Footprint table in the catalogue. A new example is one more entry (`id`, `file`, `alt`, `caption`, `stops`, `position`, `diameter`, `state`), then:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.

## Native skin item (not implemented)

An N-frame strip (N = 2..8): frame i = `render_frame(diameter, i, N)`, a diameter x diameter picture with the pointer at stop i; the inactive strip N more frames through `dim_artwork` (or the host's inactive handling). The Data handle is the stop index. Texts and pictograms are separate (see RotarySelector).
