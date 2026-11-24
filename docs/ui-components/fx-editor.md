# FX editor: functional panels

The chain and selected-effect header stay fixed while the parameter panel changes.
Small effects show all applicable controls in one panel. Larger effects use the
shared native segmented radio component for explicitly named tabs; there is no
MORE/LESS action. Tabs select UI state and do not write an engine parameter.

| Effect | Panels | Assignment of the existing rows |
| --- | --- | --- |
| Chorus | MAIN | Rate/sync/depth/feedback; voices/mix/delay1/delay2; filter cutoff/spread/level (11 live cells) |
| Compressor | MAIN / BANDS / UPWARD | Global + selected upper threshold/ratio; output/crossovers/gains; lower pairs |
| Delay | MAIN / TONE | Timing/style/feedback/cutoff/mix; quality/spread/offsets/output |
| Distortion | MAIN | Distortion and every applicable filter/shape control |
| EQ | MAIN / TONE | Band cutoff/gain/output; modes/law/resonance/mid band2 |
| FX Filter | MAIN / TONE | Model/style/basic response; formant/shape/output |
| Flanger | MAIN | All modulation, feedback, phase and output controls |
| Phaser | MAIN | All modulation, feedback, sweep and output controls |
| Reverb | MAIN / TONE / MOD | Algorithm/size/decay/predelay/width/room/feedback/output; prefilters/shelves; early size/diffusion/chorus |
| Hyper/Dimension | MAIN | Hyper, Dimension, retrigger and output |
| Utility | MAIN | Stereo, filters, mono bass, polarity and output |
| Bode | MAIN / TIMING | Shift/direction/balance/feedback/mix; delay/sync/blur/stereo/output |
| Convolve | MAIN / TAIL | Impulse/size/tone/predelay/envelope/mix; sync/phase/damping/gain/law/output |
| Splitter L/H, L/M/H, M/S | MAIN | Existing crossover/band/output controls and band-rack display |

`fxl::panelsOf()` in `shell-mpc/ui/fx_layout.h` owns the row-to-panel mapping.
Rows retain their Q groups, condition predicates and canonical parameter keys.
The compressor BAND choice is view-local and remains on MAIN. Single-band mode
hides its selector and nonexistent band controls; Limit hides the unused upper
ratio. Conditional rows compact within their assigned panel. Tab navigation is
available while bypassed; bypassed parameter/rack/remove actions stay disabled.

## Native controls and protocol

The native skin uses the current shared Level filmstrip for continuous values,
ActiveText for switches, segmented radio artwork for tabs and BAND, and the
shared action/power buttons for REMOVE and POWER. The generated skin registers
each control family once and binds its instances. Unipolar and bipolar Level assets have distinct
filenames, and every dial has a bound value readout.

The existing source-bound header `Page` handle carries the explicit normalized
panel index `index / 2`: MAIN=0, the second panel=.5, the third=1. The parameter ABI
indices and all subsequent non-FX offsets are unchanged. Header layout frames
identify effect kind and power; panel state participates in action identity so
a queued action cannot follow a panel, slot or preset replacement. No tab is
mapped to a physical Q-Link. Native FX uses the existing Gen1 single-Q bank frame; Gen2 compiles that drawing out. The native editor no longer suppresses the Gen1 frame.

## Actual offline pictures

Run `python3 scripts/render_ui_screen_fx_preview.py`. It generates the production
TUI, renders the matching `FxUi`/`FxSlotEditor` scene through `scene_cpu.h`, and
atomically saves the one canonical [current FX image](../screens/fx.jpg).
It is a1280x628 synthetic offline fixture, **not a hardware capture**.
The old reference was removed after the functional native panel implementation was integrated.

To inspect all nine states (Chorus Gen1/Gen2, three Reverb tabs, three Compressor
tabs and Limit), explicitly export outside the checkout:

```sh
python3 scripts/render_ui_screen_fx_preview.py --out-dir /tmp/synthyboy-fx-fixtures
```

Only that external temporary directory receives state variants, matching host
handle JSON files and `manifest.json` with measured generated TUI/PNG byte counts.
No diagnostic variants or metadata belong in `docs/screens/` or elsewhere in the
repository. The current render never crops an old screenshot or adds a VERIFIED
badge. Device acceptance of touch, Q-Link navigation and popup stacking remains
a separate hardware check.

## Component API reference

## Header and types
`shell-mpc/ui/fx_layout.h`, namespace `fxl`: `Live`, `Alt`, `Cell`, `Row`, `Layout`, `layoutOf (kind)`, `kKindSplitterLh`, `kKindSplitterLmh`, `kKindSplitterMs`, `kNumKinds`, `kMaxRows` 8, `kMaxPageRows` 4, sync masks `kSyncFree`, `kSyncTempo`, the table builders in `fxl::dsl` (`k`, `when`, `c`, `pair`, `none`, `row`, `rate`), band helpers (`bandsOfSplitter`, `bandRack`, `bandOfRack`, `bandName`, `rackName`, `controlName`).
`shell-mpc/ui/fx_slot_editor.h`: `FxSlotEditor : Composite`, `FxSlotEditor::Kind { Knob, Stepper, List, Toggle, Number }`.
`shell-mpc/ui/fx_screen.h`: `FxFullScreen : Composite`.

## State fields
`fxl::Layout`: `kind`, `name`, `rowsPerPage` (historical table metadata; the current editor derives panel capacity from `panelsOf()` row assignments), `displayH`, `numRows`, `row[kMaxRows]`; `Row`: `cell[4]`, `title` (the name of the block = the name of the Q group); `Cell`: `alt[3]` (up to three mutually exclusive alternatives); `Alt`: `key`, `live` (`Live { key, modes }`: live when the parameter `key` of the same effect rounds to a value whose bit is set in `modes`), `name`.
THE RULE: every control that is not live is hidden. Remaining cells compact left and empty rows disappear inside their assigned panel.
`FxSlotEditor` members: `power`, `rack`, `tabs`, `remove`, `display` (`FxDisplay`), `bands` (`FxBandBoard`), arrays of `kMaxParams` (24) `knobs`, `steppers`, `lists`, `toggles`, `numbers`; `groups[kMaxRows]`. A control is shown by kind: continuous = knob, whole number = number field, on / off = LED toggle, 2 named to 8 values = LED list, longer (or not fitting) = stepper with popup. API: `setType (kind, removable)`, `setState (valueOf)` (a template; `valueOf (key)` returns the plain value), `setPage (p)`, `pageIndex()`, `panelName (p)`, `setPath`, `setActiveRow`, `activeRow()`, `turnQ`, `numRows()`, `numPages()`, `rowTitle (row)`, `controlInCell`, `memberControl`, `memberOf (p)`, `controlOfKey (key)`, `nameOf`, `isLive`, `kindOf`, `assignIds (baseId, powerId, rackId, tabsId, removeId)` (control p has id `baseId + p` whatever its kind).
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
    {
        const synthyboy::ParamInfo& pi = *fx.slotEditor.infoOf (p);
        static_cast<Knob*> (fx.slotEditor.memberOf (p))->state.value = synthyboy::toNormalised (pi, plainValueOf (pi.key));
    }
```
`plainValueOf` is the owner's own function (it reads the instance).
`shell-mpc/overlay/fx_ui.h` is the real example.

## Used on
`shell-mpc/overlay/fx_ui.h` (`FxUi`, `kElementPage` 6, ids `kChainBase` 3000, `kBandBase` 3100, `kEditorBase` 4000). Pictures: `docs/screens/fx.jpg`, `fx-eq.png`, `fx-reverb.png`, `fx-compressor.png`.

## Limits and pitfalls
- Adding an effect parameter: add it to the table row of its effect, never a hand-made page; the coverage test fails until every engine parameter has a control.
- At most 24 controls per effect, 8 rows per effect, 4 rows per page.
- Names shown to the player go through `choice_names.h` (`shownChoice`, `shownLabel`).
- A row with partial members still publishes its chips: do not leave live controls out of a Q group.
