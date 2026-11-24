# ModulationsPage and ModTargetsPage

## What it is
Two pages about modulation.
- `ModulationsPage` opens on a long press of a knob or number field. The toolbar shows the control name and value, BACK and paging. Each route shows its coloured source name, amount, UNI / BI, ON / OFF and REMOVE. Native ADD opens the [ModSourceBoard](mod-source-board.md). The fallback list offers a quick source step and ADD below its rows. An empty list opens the board directly.
- `ModTargetsPage` lists the selected source's targets. Each row has the target name, depth, polarity, ON / OFF and REMOVE. The toolbar supplies paging and ADD TARGET.

## Header and types
`shell-mpc/ui/panel.h`: `ModulationsPage` (`Row`). `shell-mpc/ui/mod_targets.h`: `ModTargetsPage` (`Entry`, `Row`).

## State
`ModulationsPage` has `back`, `page`, six `rows`, `add`, `addSource`, `sourceIds`, `board` and the active `group`. The owner passes the route window, total count and page to `load (entries, title, total, pageIndex)`. Depth is represented as `(depth + 1) / 2`.
`ModTargetsPage`: `back`, `page` (`Stepper`), `add` ("ADD TARGET"), `rows[kPerPage = 6]`, `group`; `Entry { name (40), depth -1..1, bidirectional, bypassed }`; `load (entries, n, title, colour, total, pageIndex)`, `setRowIds (row, depth, polarity, bypass, remove)`, `selectRow`, `numRows()`, `activeRow()`, `setRowBlock`, `turnQ`.

## Size
Both screens use `ModulationRowsGeometry`. The toolbar starts at y=32 inside a 1280 × 628 tab, below the detector markers. Six rows fit below it, with 56 px controls and a 16 px Q-Link lane per row. The fallback ADD row fits below the sixth route.

## Touch and knob behaviour
| Source | Change |
|---|---|
| depth bar drag | `Value` of the row's depth id (0 to 1: the owner converts to -1..+1) |
| polarity / on-off toggle | `Index` 0 / 1 |
| remove (X) | `Request` of its id |
| BACK | `Request` of its id |
| ADD (add stepper) | `AddRequest`: `id` of the button, `index` = the source id of the current item (`sourceIds[i]`) |
| chip of the board | `AddRequest` with `index` = the source id |
| page stepper | `Index` selects the window the owner will load |
A touch on a row makes it the active Q row.

## Q-Link
The row touched last is ONE group [depth, polarity, on / off]. On `ModTargetsPage` too. The other rows show idle chips (`setRowBlock` with `QRouter::blockOfGroup`).

## Options and variants
The SDK shares the amount, toggle, remove, and toolbar widgets across both screens. The overlay draws the coloured source names, Q-Link decorations and source board. Pointer input over a native cell goes to the host. Pressing a native row can also select its Q-Link group. The shared bindings validate the owner, destination or source, preset, visible page and route.

## Minimal example
```cpp
ModulationsPage page;
page.place ({ 0, 110, 1280, 628 });
ModInput mi;
mi.add (7, "LFO 1", 0.35);                    // source id, short name, depth
page.load (mi, "CUTOFF", 1, 0);
page.setTargetValue ("420 Hz");
page.rows[0].depth.setId (810);  page.rows[0].polarity.setId (811);
page.rows[0].bypass.setId (812); page.rows[0].remove.setId (813);
page.back.setId (800);  page.add.setId (814);
Change c = page.touchDown (x, y, nowMs);
c = page.touchUp (x, y);
if (c.kind == ChangeKind::AddRequest) addModulation (c.index);
```

## Used on
`ModulationsPage`: `shell-mpc/overlay/osc_ui.h`, `filter_ui.h`, `env_ui.h`, `lfo_ui.h` (the page above a full page, opened by a `ModsRequest`). `ModTargetsPage`: `env_ui.h`, `lfo_ui.h`, `macro_ui.h`, `mod_ui.h`. Pictures: `docs/screens/osc_a.jpg`, `env-targets.png`.

## Limits and pitfalls
- The owner writes the entries back after every change (`load` again): the page keeps no truth.
- Names of sources are at most 6 characters on the rows; the colour comes from the name (`theme::sourceColour`) or the `macro` flag.
- At most six rows are visible in either editor. Paging reaches every assigned modulation slot.
