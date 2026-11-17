# FxSlotEditor, FxFullScreen and the effect tables (fx_layout.h)

> Bound to SynthyBoy's parameter table and effect types: `fx_layout.h` includes `synthyboy/fx_types.h`, `synthyboy/params.h` and `choice_names.h`; `fx_slot_editor.h` includes `synthyboy/fx_types.h`, `synthyboy/params.h`; `fx_screen.h` includes `fx_chain_bar.h`, `fx_gallery.h`, `fx_slot_editor.h`. A control is named by its engine KEY ("chorus.rate"): the name, range, unit and choices come from `synthyboy::ParamInfo`. To reuse: replace the `layoutOf` tables (your keys) and give `FxSlotEditor` your own `ParamInfo`-like lookup (see [REUSE.md](REUSE.md)); the row, cell and live-condition mechanism is generic.

## What it is
`FxSlotEditor`: the page of the selected effect: power, name, rack, page stepper, REMOVE, the effect's display, and the controls in rows of four. WHAT stands WHERE comes from ONE table per effect (`fxl::Layout`). `FxFullScreen`: the whole FX page: the chain bar on top, the editor under it, or the gallery of types while the chain is empty.

## Header and types
`shell-mpc/ui/fx_layout.h`, namespace `fxl`: `Live`, `Alt`, `Cell`, `Row`, `Layout`, `layoutOf (kind)`, `kKindSplitterLh`, `kKindSplitterLmh`, `kKindSplitterMs`, `kNumKinds`, `kMaxRows` 8, `kMaxPageRows` 4, sync masks `kSyncFree`, `kSyncTempo`, the table builders in `fxl::dsl` (`k`, `when`, `c`, `pair`, `none`, `row`, `rate`), band helpers (`bandsOfSplitter`, `bandRack`, `bandOfRack`, `bandName`, `rackName`, `controlName`).
`shell-mpc/ui/fx_slot_editor.h`: `FxSlotEditor : Composite`, `FxSlotEditor::Kind { Knob, Stepper, List, Toggle, Number }`.
`shell-mpc/ui/fx_screen.h`: `FxFullScreen : Composite`.

## State fields
`fxl::Layout`: `kind`, `name`, `rowsPerPage` (3 or 4 without a display, 2 or 3 under one), `displayH`, `numRows`, `row[kMaxRows]`; `Row`: `cell[4]`, `title` (the name of the block = the name of the Q group); `Cell`: `alt[2]` (one control, or two that exclude each other); `Alt`: `key`, `live` (`Live { key, modes }`: live when the parameter `key` of the same effect rounds to a value whose bit is set in `modes`), `name`.
THE RULE: a control that is not live is HIDDEN when no live control follows it in its row (the row closes up; a row with nothing live is gone), else it is drawn DIM in its place and takes no touch.
`FxSlotEditor` members: `power`, `rack`, `page`, `remove`, `display` (`FxDisplay`), `bands` (`FxBandBoard`), arrays of `kMaxParams` (24) `knobs`, `steppers`, `lists`, `toggles`, `numbers`; `groups[kMaxRows]`. A control is shown by kind: continuous = knob, whole number = number field, on / off = LED toggle, 2 named to 8 values = LED list, longer (or not fitting) = stepper with popup. API: `setType (kind, removable)`, `setState (valueOf)` (a template; `valueOf (key)` returns the plain value), `setPage (p)`, `setPath`, `setActiveRow`, `activeRow()`, `turnQ`, `numRows()`, `numPages()`, `rowTitle (row)`, `controlInCell`, `memberControl`, `memberOf (p)`, `controlOfKey (key)`, `nameOf`, `isLive`, `kindOf`, `assignIds (baseId, powerId, rackId, pageId, removeId)` (control p has id `baseId + p` whatever its kind).
`FxFullScreen`: `chainBar`, `slotEditor`, `gallery`; `assignIds (chainBaseId, editorBaseId)` (editor: power `+100`, rack `+101`, page `+102`, remove `+103`), `setEditorShown (bool)`, `galleryShown()`, `groupName (g)`, `activeGroup()`, `setActiveGroup (g)`, `turnQ`.

## Size
`FxSlotEditor::minSize()` 1200 x 400, `kHeaderH` 56, `kDisplayH` 176. `FxFullScreen::minSize()` 1280 x 628, `kBarH` 88.

## Touch and knob behaviour
Changes come out with the id of the child; the owner maps `id - baseId` to the control and reads its key with `infoOf (p)`: knob `Value`, stepper `Index` or `Request` (list), list `Index`, toggle `Index`, number `Index`. Chain bar changes: see [fx-chain.md](fx-chain.md). A touch on a control of a row makes the row the active Q group.

## Q-Link
One Q group per visible row of the page (`groups[]`, `memberControl (row, col)`): the live controls of the row, 1 to 4 members. `turnQ (knobIndex, delta)` goes to the active row; the other rows are published in reading order (see [PAGES.md](PAGES.md)).

## Options and variants
Splitters have a table too (`kKindSplitterLh`, `Lmh`, `Ms`) and show the band board in the display's place. A cell can hold RATE / DIVISION (`dsl::rate`).

## Minimal example
```cpp
FxFullScreen fx;
fx.assignIds (3000, 4000);
fx.place ({ 0, 110, 1280, 628 });
fx.slotEditor.setType (synthyboy::kFxTypeChorus, true);          // kind of the open effect
fx.slotEditor.setState ([&] (const char* key) { return plainValueOf (key); });
for (int p = 0; p < fx.slotEditor.numParams(); ++p)
    if (fx.slotEditor.kindOf (p) == FxSlotEditor::Kind::Knob)
        static_cast<Knob*> (fx.slotEditor.memberOf (p))->state.value = normOf (fx.slotEditor.infoOf (p));  // memberOf: the control to fill
```
`shell-mpc/overlay/fx_ui.h` is the real example.

## Used on
`shell-mpc/overlay/fx_ui.h` (`FxUi`, `kElementPage` 6, ids `kChainBase` 3000, `kBandBase` 3100, `kEditorBase` 4000). Pictures: `docs/user-guide/img/fx-chorus.png`, `fx-eq.png`, `fx-reverb.png`, `fx-compressor.png`.

## Limits and pitfalls
- Adding an effect parameter: add it to the table row of its effect, never a hand-made page; the coverage test fails until every engine parameter has a control.
- At most 24 controls per effect, 8 rows per effect, 4 rows per page.
- Names shown to the player go through `choice_names.h` (`shownChoice`, `shownLabel`).
- A row with partial members still publishes its chips: do not leave live controls out of a Q group.
