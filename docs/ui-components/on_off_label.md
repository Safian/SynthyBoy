# ON/OFF LABEL (catalogue)

A label that is a switch: the switchable caption placed next to a dial. Source: `components/native/on_off_label/on_off_label.py`, examples: `components/native/on_off_label/catalog.json` (`on_off.examples`), test: `scripts/tests/test_native_label_catalog.py`.

- ON: `active_text.on` (cyan, highlighted). OFF: `active_text.off` (dim). DISABLED: the OFF face in the disabled look (`style.dim_artwork`); next to a dial the dial takes its inactive look too.
- Faces come from the ActiveText recipe (`render_active_text_art`, font `active_text.font`); the dial is the text-free Pan (bipolar) or Level dial graphic (`render_dial_frame`) at `dial.diameter`.
- Layouts: `right` (label centred beside the dial) and `bottom` (label centred under it), `gap.column` between.
- Smooth: the SMOOTH caption of the Smooth dial will be an instance of this component. The smooth tokens (`smooth.*`) stay until that switch is made.

Add an example: an object in `on_off.examples` (`id`, `file`, `alt`, `caption`, `state`, optional `dial` = `level`|`pan` with `layout`, optional `text`), then `r.render_on_off_cards()` (as in label.md) and `python3 scripts/gen-native-component-catalog.py`.
