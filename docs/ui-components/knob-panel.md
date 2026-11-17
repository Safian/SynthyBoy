# KnobPanel and DetailPage (panel.h)

## What it is
`KnobPanel`: lays out the parts of a PRESET (a table: kind, slot, label, grid cells) on a grid inside its rectangle: knobs, knob + RAND pairs, unison box, warp pair, menu, steppers, toggles; optionally in titled, bordered blocks (one Q group each). The owner finds a part by slot. A new layout is a new table row, not new code. `DetailPage`: a full-rectangle page for the details of one control: a top bar with BACK and a title, an optional display area, and the values from a `KnobPanel` preset in Q groups of four.

## Header and types
`shell-mpc/ui/panel.h`: `PanelSlot` (the places of the parts: `WtPos, Unison, Detune, Blend, Warp, Pan, Level, ...`, `Count` = empty), `PartKind { Knob, RandPair, Unison, WarpPair, Menu, Stepper, Toggle }`, `PanelItem { kind, slot, label, col, row, colSpan, rowSpan, bipolar, block }`, `PanelBlock { title, col, row, colSpan, rowSpan, innerCols, innerRows, firstRowH }`, `QSpec { slot[4] }`, `PanelPreset`, namespace `presets` (`kWavetable`, `kGranular`, `kNoise`, `kWavetableFull`, `kMultisampleFull`, `kGranularFull`, `kSampleFull`, `kSpectralFull`, `kSubFull`, `kNoiseFull`, `kNoiseGenFull`, `kNoiseSampleFull`, `kEmptyFull`, `kUnisonDetail`, `kUnisonDetailMultisample`, `kUnisonDetailGranular`, `kGrainDetail`, `kEnvelopeDetail`), `KnobPanel`, `DetailPage`, `fillGroup`.

## State
`PanelPreset`: `name`, `cols`, `rows`, `separatorBefore` (-1 none), `items`, `numItems`, `q` (the Q groups by slot), `numQ`, `displayH` (a `DetailPage`: height of the display area), `blocks`, `numBlocks`.
`KnobPanel`: `load (preset)` (every part returns to its defaults: labels from the table, no ids), `part (PanelSlot)` (the child, null when the preset has none), `preset()`, `blockRect (i)`, `numBlocks()`. Limits `kMaxKnobs` 12, `kMaxPairs` 3, `kMaxItems` 12, `kMaxSteppers` 4, `kMaxToggles` 6, `kMaxBlocks` 4. Equal columns and rows with `kGap` between cells; a wider gap and a thin line at the separator.
`DetailPage`: `back` (`MenuButton`), `panel` (`KnobPanel`), `groups[kMaxGroups = 4]` (`QGroup`); `load (preset, title)`, `displayRect()`, `numGroups()`, `activeGroup()`, `setActiveGroup (i)`, `turnQ (knobIndex, delta)`, `kBarH` 64. The page draws the frame of the display area; the owner draws the picture in it.

## Size
`KnobPanel::minSize()` follows the preset. `DetailPage::minSize()` = max (panel width, 200) x (`kBarH` + display + panel height). The display area takes the room the blocks do not need (never less than `displayH`).

## Touch and knob behaviour
Changes come out with the id of the part (give the ids after `load`: `part (slot)->setId (...)`). `DetailPage`: BACK is a `Request` of its id.

## Q-Link
`DetailPage` groups come from the preset's `q` list, one active at a time (`setActiveGroup`; a touch on a part of a group makes it active, by the owner). A `TypeBar` becomes a group with `fillGroup (group, bar)`.

## Options and variants
Presets with blocks (full screen bottom band: UNISON | WARP, GRAIN | PLAYBACK | RANDOM) and without (the older grids). A preset with `displayH` is a detail page with a picture above.

## Minimal example
```cpp
DetailPage page;
page.load (presets::kUnisonDetail, "UNISON");
page.place ({ 0, 110, 1280, 628 });
page.back.setId (800);
page.panel.part (PanelSlot::Mode)->setId (801);      // a Stepper part of the preset
page.setActiveGroup (0);                             // frame + chips on the first QSpec
Rect pictureArea = page.displayRect();               // draw the picture here
Change c = page.touchDown (x, y, nowMs);
c = page.touchUp (x, y);
if (c.kind == ChangeKind::Request && c.id == 800) stack.back();
```

## Used on
`OscillatorFullScreen::panel`, and `DetailPage` for the Unison, Grain and Envelope pages in `shell-mpc/overlay/osc_ui.h`. Pictures: `docs/user-guide/img/osc-unison.png`, `osc-page-grain.png`.

## Limits and pitfalls
- Which `PanelSlot` a preset has decides what `part()` returns: null for a missing slot, so test it.
- At most 12 knobs, 3 pairs, 4 steppers and 6 toggles per preset; more is a new preset type.
- A preset is DATA in `panel.h`: add the row there, with a `QSpec` so every part is in a Q group.
