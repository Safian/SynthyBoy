# NoteMask

Native component `components/native/note_mask/note_mask.py` (owner decision 2026-11-27: no C++ control; the pictures and the skin definition come from the Button recipe).

A set of notes of an octave (the transpose snap of the oscillators, OPTIONS page, block TRANSPOSE SNAP): twelve toggle buttons laid out like a piano.

- Layout: the seven white notes (C D E F G A B) in the lower row, the five black ones (C# D# F# G# A#) in the upper row, centred on the border between the two white notes around them. A cell is 56 px high (`note_mask.row_h`), the rows are `header.gap` (8) px apart: the mask is 120 px high; the width is free (at least 7 x 60 px; the OPTIONS page gives it three cells, 474 px). Every cell is at least 56 px wide, a face is `note_mask.inset` px narrower than its hit rectangle on each side.
- Faces: a note that is not in the set is the Button legacy gradient face; a note that is in the set is the "active" face (accent fill). Disabled (the oscillator is OFF): `style.dim_artwork`, and the page publishes no cell, so no edit gets through.
- Skin: `build_definition(skins_dir, width)` is a local definition of twelve Buttons bound to the handles `Note0` .. `Note11`; the instance remaps them to the twelve handles `nativeui::kNoteMask + 0..11` (`scripts/mpc-skin-extras/72_osc_lists.py`) and gates the whole mask by the thirteenth (1 while the page shows the mask).
- Binding: the host writes 1 or 0 to a note handle; `OscUi::publishNativeNoteMask` takes it and sets or clears that bit of the `transpose_snap` parameter (bit 12, SNAP NOTE, stays). The displayed state of every cell is bit `n` of the parameter, read by the host from the engine value.
- Catalogue: `components/native/note_mask/catalog.json` (`approved: false` until the owner approves the look); test `scripts/tests/test_native_note_mask.py`.
