# FILTER SELECTOR (catalogue)

The 56 x 48 chip at the top of the Filter page: a button face with two small lamp segments above it. It is not a Selector: it opens the filter routing menu (Parallel, F1 to F2, F2 to F1) and shows the power state of both filters. Source: `components/native/filter_selector/filter_selector.py` (composes the existing recipe `selector.filter_chain_definition`, which stays in `components/native/selector/selector.py` because the skin build uses it), states: `STATES` in the module, pictures: `components/native/filter_selector/img/`, test: `scripts/tests/test_native_filter_selector.py`.

## Size and tokens

`filter.chain` in `components/sizes.json`: `width` 56, `height` 48, `lamp_y` 1, `lamp_height` 5, `lamp_gap` 4, `face_y` 9 (face 56 x 39 below the lamps). Colours: lamp on `accent.cyan`, lamp off `theme.neon.track`; the face is the dropdown face without a chevron (`theme.neon` panel gradient, keyline) with the text `CHAIN` in `theme.neon.text`.

## States

- Face: the routing chip. Since commit 23aeee14 it reads CHAIN, not F1/F2: the edited filter is the hardware sub-tab, so the face does not show which filter is selected. The `Edited` handle still exists (four film-strip frames), but all frames draw the same face.
- Lamp bar: two segments, left F1, right F2. Cyan = filter on, dim = filter off. The catalogue shows the four on/off combinations (F1 on / F2 off, both on, both off, F1 off / F2 on).
- Touch: one tap target; Mouse Down or Enter opens the routing drawer (`Show Overlay`, popup `filterNativeChainChoices` on the Filter page, `skin/TUI.json`). The F1/F2 switch of the Filter page is the sub-tab, not this chip. `docs/dsp-knowledge/filters.md` and `docs/engine-spec/05-filters.md` describe the filter engine, not this control.

A face that names the selected filter (F1 / F2) is not drawn by any recipe; it would be a new look and is not in the catalogue.
