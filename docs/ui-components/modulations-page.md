# ModulationsPage and ModTargetsPage

## What it is
Two pages about modulation.
- `ModulationsPage` (the view from a CONTROL): opened by a long press on a knob or number field. A top bar with BACK, the control's name and value; one row per modulation of that control (source name in its colour, depth bar, UNI / BI toggle, ON / OFF toggle, remove button); the last row "add source" (a stepper over the sources and an ADD button). While no modulation exists it shows the [ModSourceBoard](mod-source-board.md) instead.
- `ModTargetsPage` (the view from a SOURCE): the targets of one envelope or LFO: a row per target with name, depth, polarity, on / off, remove; a stepper for paging when more than 6; "ADD TARGET".

## Header and types
`shell-mpc/ui/panel.h`: `ModulationsPage` (`Row`). `shell-mpc/ui/mod_targets.h`: `ModTargetsPage` (`Entry`, `Row`).

## State
`ModulationsPage`: `back`, `rows[kMaxRows = 8]` (`Row { depth (HBar), polarity, bypass (ToggleIcon), remove (MenuButton), source, name, macro }`), `board`, `addSource` (`Stepper`), `add` (`MenuButton` "ADD"), `sourceIds[8]`, `group` (`QGroup`: [depth, polarity, on / off] of the row touched last). `load (const ModInput&, title)` builds the rows from the entries; `setTargetValue (text)`, `setRowIds (...)`, `selectRow (i)`, `activeRow()`, `boardShown()`, `turnQ`, `setRowBlock (row, block)`, static `setRowState (row, bidirectional, bypassed)`. Depth bar: `value = (depth + 1) / 2`.
`ModTargetsPage`: `back`, `page` (`Stepper`), `add` ("ADD TARGET"), `rows[kPerPage = 6]`, `group`; `Entry { name (40), depth -1..1, bidirectional, bypassed }`; `load (entries, n, title, colour, total, pageIndex)`, `setRowIds (row, depth, polarity, bypass, remove)`, `selectRow`, `numRows()`, `activeRow()`, `setRowBlock`, `turnQ`.

## Size
`kRowH` 72, controls `kCtlH` 56, bar `kBarH` 64; `ModulationsPage` rows from the top (max 8); `ModTargetsPage` `kNameW` 340. They fill a tab (1280 x 628).

## Touch and knob behaviour
| Source | Change |
|---|---|
| depth bar drag | `Value` of the row's depth id (0 to 1: the owner converts to -1..+1) |
| polarity / on-off toggle | `Index` 0 / 1 |
| remove (X) | `Request` of its id |
| BACK | `Request` of its id |
| ADD (add stepper) | `AddRequest`: `id` of the button, `index` = the source id of the current item (`sourceIds[i]`) |
| chip of the board | `AddRequest` with `index` = the source id |
| page stepper (`ModTargetsPage`) | `Index` = the new page; the owner loads that page's rows |
A touch on a row makes it the active Q row.

## Q-Link
The row touched last is ONE group [depth, polarity, on / off]. On `ModTargetsPage` too. The other rows show idle chips (`setRowBlock` with `QRouter::blockOfGroup`).

## Options and variants
Board vs rows; paging for targets.

## Minimal example
```cpp
ModulationsPage page;
page.place ({ 0, 110, 1280, 628 });
ModInput mi;
mi.add (7, "LFO 1", 0.35);                    // source id, short name, depth
page.load (mi, "CUTOFF");
page.setTargetValue ("420 Hz");
page.rows[0].depth.setId (810);  page.rows[0].polarity.setId (811);
page.rows[0].bypass.setId (812); page.rows[0].remove.setId (813);
page.back.setId (800);  page.add.setId (814);
Change c = page.touchDown (x, y, nowMs);
c = page.touchUp (x, y);
if (c.kind == ChangeKind::AddRequest) addModulation (c.index);
```

## Used on
`ModulationsPage`: `shell-mpc/overlay/osc_ui.h`, `filter_ui.h`, `env_ui.h`, `lfo_ui.h` (the page above a full page, opened by a `ModsRequest`). `ModTargetsPage`: `env_ui.h`, `lfo_ui.h`, `macro_ui.h`, `mod_ui.h`. Pictures: `docs/user-guide/img/osc-mods.png`, `env-targets.png`.

## Limits and pitfalls
- The owner writes the entries back after every change (`load` again): the page keeps no truth.
- Names of sources are at most 6 characters on the rows; the colour comes from the name (`theme::sourceColour`) or the `macro` flag.
- At most 8 rows (`ModulationsPage`) and 6 per page (`ModTargetsPage`): the rest is for a paging stepper.
