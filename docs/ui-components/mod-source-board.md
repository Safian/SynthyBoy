# ModSourceBoard

## What it is
A quick-add board of modulation sources: every source as a chip, in groups with a caption at the left; one tap on a chip asks the owner to create a modulation from that source. It fills the page while no modulation exists. The board knows no engine: the owner hands it the sources.

## Header and types
`shell-mpc/ui/mod_board.h`: `ModSourceBoard`. (The helper that fills it from the SynthyBoy source table is `uibind::fillSourceBoard` in `shell-mpc/overlay/ui_bind.h`.)

## State fields
No state struct. Fill it with:
- `clear()`;
- `setGroupName (group, name)`;
- `add (id, label, colourName, group)` returns false when full (`kMaxSources` 64, `kMaxGroups` 8): `id` is the owner's id, `label` the chip text, `colourName` picks the colour (`theme::sourceColour`), `group` 0 to 7 (groups are drawn in this order);
- `setHeadline (text)` (default "TAP A SOURCE TO START A MODULATION").
Readers: `numSources()`, `sourceId (i)`, `sourceLabel (i)`, `sourceGroup (i)`, `chipRect (i)`, `chipRectOfId (id)`, `numGroups()`.

## Size
`minSize()` = 900 x (height needed at 900 px). A chip is at least 56 px high and 64 px wide (`kChipH`, `kChipMinW`; `kChipMaxW` 260); the width follows the label. When the sources need more height than the rectangle has, the gaps shrink, never the chips.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap on a chip (touch up inside the same chip) | `Index`: `index` = `value` = the chip's `id` |

`touchTargets` lists the chips.

## Q-Link
Not a Q member.

## Options and variants
The headline text: the page of one control says what the tap modulates.

## Minimal example
```cpp
ModSourceBoard board;
board.setHeadline ("TAP A SOURCE TO MODULATE THIS VALUE");
board.setGroupName (0, "ENVELOPES");
board.add (1, "ENV 1", "ENV 1", 0);
board.add (2, "ENV 2", "ENV 2", 0);
board.setGroupName (1, "LFOS");
board.add (7, "LFO 1", "LFO 1", 1);
board.setId (90);
board.place ({ 0, 174, 1280, 400 });
Change c = board.touchDown (x, y, nowMs);
c = board.touchUp (x, y);
if (c.kind == ChangeKind::Index && c.id == 90) addModulation (c.index);   // the source id
```

## Used on
Inside `ModMatrixView::board` (the empty Mod page, `docs/screens/mod.jpg`) and `ModulationsPage::board` (the modulations of one control). Filled by `shell-mpc/overlay/mod_ui.h` through `uibind::fillSourceBoard`. Inside `ModulationsPage` a chip tap comes out as an `AddRequest` (id of `add`, `index` = source id), see [modulations-page.md](modulations-page.md).

## Limits and pitfalls
- The matrix shows the board when it has no routes. The destination editor shows it when a parameter has no assigned sources or when the user presses native ADD on its route list.
- 64 sources at most.
