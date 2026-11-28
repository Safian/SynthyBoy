# RADIO GRID (catalogue)

A group of text buttons in rows x columns, exactly one selected, with an optional group title (the "gombrács"). Source: `components/native/radio_grid/radio_grid.py`; samples: `components/native/radio_grid/catalog.json` (`radio_grid.samples`); test: `scripts/tests/test_native_radio_grid_catalog.py`. Which radio-family component when, with the footprint comparison: [radio_family.md](radio_family.md). This replaces the flat segmented faces of `radio_button.py` (marked KIVEZETÉSRE there; the skin switch is a separate round).

## Structure

- Every cell is the Button recipe, not a new drawing: unselected = `action_art(text=..., look="legacy")` (old gradient face, `control.gradient`, `button.gradient.*`); selected = `action_art(look="active", colour=radio.active.text)`: the same gradient shape filled `accent.cyan` to `radio.active.bottom`, keyline `radio.active.border`, text `radio.active.text` (the existing "Aktív szöveg"). `look="active"` is the one addition to the Button (`button.LOOKS`); the other looks are unchanged.
- Buttons are 32 / 40 / 48 px high (`button.size.<px>` radius and font) and all of the same width: `(width - (cols - 1) x gap) / cols`, so the grid fills `width` exactly (a width that does not divide equally is refused). Gap `radio_grid.gap` = `header.gap` = 8 px, in both directions.
- Title (optional): Label block-title style (`panel.title` font, `text.caption`), left aligned above the grid, `radio_grid.title_gap` 6 px.
- Shapes: 2 x 3 (TYPE), 1 x N (horizontal row), N x 1 (vertical), 2 x 2, ... 2 to 8 items, row-major.

## States and tokens

selected (accent), unselected (legacy face), disabled (`style.dim_artwork` of the whole grid, existing `inactive.preview.*`). New colours: none. New size token: `radio_grid` {`title_gap` 6, `gap` ref `header.gap`}.

## Catalogue samples

TYPE 2 x 3 (BELL selected, 40 px; also 32 and 48 px), 1 x 4 (HZ BPM KEY FREE, BPM selected, 40 px), 3 x 1 vertical, disabled. Footprints in the catalogue table. A new example is one more entry (`id`, `file`, `alt`, `caption`, `rows`, `cols`, `items`, `selected`, `height` token, `width`, `title`, `state`), then:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly.

## Native skin item (not implemented)

One native Button group (`buttonId` 1..N, `numButtonsInGroup` N, Data handle), built with the Button component, not with a private face. Per distinct button size: 2 images (off = legacy face, on = active face) without text, plus 2 dimmed ones for disabled; the texts are host Labels on the buttons (`text.icon`, selected `radio.active.text`). The title is a host Label.
