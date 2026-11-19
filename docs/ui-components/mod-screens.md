# ModMatrixView, ModRow, ModRowSelector, ModSlotDetail

## What it is
The modulation matrix page and the page of one slot.
- `ModMatrixView`: the slots as rows (scrolled, 8 visible), filtered ALL / BY SOURCE / BY DEST; a row is: its number, SOURCE and TARGET buttons, AMOUNT bar, UNI / BI, ON / OFF, MORE. While no slot is in use the [ModSourceBoard](mod-source-board.md) takes the list's place.
- `ModRow`: one row. `ModRowSelector`: the row number as the first member of the Q group; it steps the selected row.
- `ModSlotDetail`: the page of one slot: BACK, title, DELETE, the slot's own row, and three blocks, one Q group each: AUXILIARY SOURCE [SOURCE, SCALE, INVERT, CURVE], SHAPE [CURVE, STEREO], TIME [RISE, FALL, DELAY or BEATS, SYNC].

## Header and types
`shell-mpc/ui/mod_matrix.h`: `ModRowSelector`, `ModRow : Composite`, `ModMatrixView : Composite` (`FilterMode { FilterAll, FilterSource, FilterDest }`, `SlotEntry`). `shell-mpc/ui/mod_detail.h`: `ModSlotDetail : Composite` (`Block { BlockAux, BlockShape, BlockTime }`).

## State
`ModMatrixView`: `tabAll`, `tabSource`, `tabDest`, `add` ("+ ADD") (`MenuButton`), `sourceFilter`, `destFilter` (`Stepper`), `windowRows[8]` (`ModRow`), `board`, `qgroup`. Feed it with `setSlotData (slot, src, dst, amount, bi, bypassed, srcGroup, destModule, hasLive, liveVal)` for all 64 slots, then `refresh()`. API: `setFilterMode`, `filterMode()`, `selectRow (idx)`, `selectedSlot()`, `selectedRow()`, `firstRow()`, `activeSlots()`, `visibleSlotCount()`, `slotAtWindowIndex`, `boardShown()`, `listRect()`, `turnQ`, `setSourceFilterChoice`, `setDestModuleFilter`. Constants `kMaxSlots` 64, `kWindowRows` 8, `kBarH` 64, `kRowH` 72.
`ModRow`: `slotIndex`, `selected`, `active`, `number`, `sourceBtn`, `destBtn`, `amountBar` (`HBar`), `biToggle`, `bypassToggle`, `detailBtn`; `setSlot (idx)`, `updateData (src, dst, amount, bi, bypassed, hasLive, liveAmount)`, `showDetailButton (bool)`; `kRowH` 72, `kCtlH` 56. `ModRowSelector`: `selected`, `maxRows`, `text`.
`ModSlotDetail`: `back`, `deleteBtn`, `summary` (`ModRow`), `auxSource`, `auxOutput`, `auxInv`, `auxPower`, `power`, `stereo`, `rise`, `fall`, `delay`, `delaySync`, `delayBeats`, `groups[3]`; `setTitle (idx, src, dst)`, `setDelaySync (bool)`, `setActiveBlock (b)`, `activeBlock()`, `blockName (b)`, `block (b)`, `isDeleteConfirmed()`, `turnQ`.

## Size
`ModRow::minSize()` 900 x 72; both pages 1280 x 628.

## Touch and knob behaviour
Changes come out with the ids the owner gave to each child (`shell-mpc/overlay/mod_ui.h`: `kRowBase` 100 + window row x 10 + offset: number 0, source 1, dest 2, amount 3, bi 4, bypass 5, detail 6). `ModRowSelector::turn` gives `Index` (the new row). Source and target buttons `Request` (the owner opens a list), amount `Value` (0 to 1 = (depth + 1) / 2), toggles `Index`. A scroll zone (56 px) and the wheel scroll the rows.

## Q-Link
Matrix: ONE group, the selected row: [row number, SOURCE, TARGET, AMOUNT]. Detail: AUXILIARY SOURCE, SHAPE, TIME (a touch on a member makes its block the group).

## Options and variants
`FilterMode`; a detail page shows the row without MORE.

## Minimal example
```cpp
ModMatrixView m;
m.place ({ 0, 110, 1280, 628 });
for (int s = 0; s < ModMatrixView::kMaxSlots; ++s)
    m.setSlotData (s, srcName (s), dstName (s), amount (s), bi (s), bypass (s), srcGroup (s), dstModule (s));
m.refresh();
for (int i = 0; i < ModMatrixView::kWindowRows; ++i)
    m.windowRows[i].amountBar.setId (100 + i * 10 + 3);
Change c = m.touchDown (x, y, nowMs);
c = m.touchUp (x, y);
```

## Used on
`shell-mpc/overlay/mod_ui.h` (`ModUi`, `kElementPage` 7, the list popup `kListPopup` 60). Pictures: `docs/user-guide/img/mod.png`, `mod-board.png`, `mod-slot.png`.

## Limits and pitfalls
- Polarity and state cells are 72 px wide: a LED toggle needs 96 px, so these stay `ToggleIcon` cells.
- `ModRowSelector` stores nothing: `turn` reports the next row (`Index`) from `state.selected` and leaves `state` alone; `ModMatrixView::turnQ` selects the row from the change and `updateQGroup` sets `state.selected`, `state.maxRows` (and the page sets `state.text`) on the next update.
- The ALL / BY SOURCE / BY DEST buttons are a tab row, not a list.
