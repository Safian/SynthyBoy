# UsedSelector

## What it is
The selector of the merged Env and LFO tabs ("used + one free", `docs/ui-map/env-filter-lfo-plan.md` section (b)). One row of cells: every USED candidate, the SELECTED one (even when unused) and a candidate marked `always` (ENV 1, the amplitude envelope), in the order the owner added them (number order), then ONE `+` cell while a candidate is neither shown nor free-looking taken. A selected unused item disappears from the row when another item is selected; its parameters are never touched.

## Header and types
`shell-mpc/ui/used_selector.h`: `UsedSelector`, `UsedSelector::State`, `UsedSelector::Candidate`. The Play page's carousels (Env, LFO) are made FROM the filled selector (`uibind::cellsFromSelector`: the same cells, one source function; `uibind::cellChoice` for a tap, `uibind::fallbackSelection` / `leaveSelector` when the tab is left). The data source (pure functions and the view-state helpers) is `shell-mpc/overlay/ui_bind.h`: `usedSourcesOf`, `selectorCandidates`, `scanUsed`, `fillSelector`, `selectedEnv / selectedLfo`, `chooseEnv / chooseLfo`.

## State fields
| Field | Type | Meaning |
|---|---|---|
| `items[count]` | `Candidate` | `id` (the owner's number: Env 1 .. 6, LFO 1 .. 10, random LFO 11 .. 14), `label` (7 characters), `used`, `always`, `group` (0 standard, 1 random LFO) |
| `selected` | `int` | the id of the selected candidate (0 = none) |

Owner calls: `clear()`, `add (id, label, used, always, group)`, `setSelected (id)`.

## Changes
| Gesture | Change |
|---|---|
| tap on a shown candidate other than the selected one | `Index { id, index = the candidate's id }` |
| tap on `+`, ONE free candidate (Env: the lowest free number) | `Index` with that candidate: it is only SELECTED, nothing is written until the user edits |
| tap on `+`, several choices (LFO: the lowest free LFO and the lowest free random LFO) | `Request { id }`: the owner opens a `ListPopup` with the rows of `plusChoices()` |
| turn | steps over the shown candidates (not `+`), clamped at the ends |

## Geometry
The cells share the width evenly, at least 56 x 56 each (`minSize` = cells x 56; the worst case 10 LFOs + 4 random + `+` = 15 cells = 840 px). `touchTargets` lists the real cells.

## What is used (the owner's rule, `uibind`)
A source is used when a slot has it as source and has a destination (bypassed or switched off still counts), or as the aux source of such a slot (an aux of a slot without source or destination does not count). An Env or LFO whose parameters differ from the defaults, or an LFO with a drawn shape, also counts. One scan per refresh (`scanUsed`), never per frame.

## Tests and pictures
`shell-mpc/overlay/tests/ui_used_selector_host.cpp`; pictures `docs/ui-map/env-filter-lfo-plan/used_selector_{empty,partly,all}.png`.

## Where it is used (2026-11-21)
`EnvFullScreen::selector` (the Env tab, ENV 1 .. 6) and `LfoFullScreen::selector` (the LFO tab, LFO 1 .. 10 and R1 .. R4); the owners fill it with `uibind::fillSelector` after the scan of `refreshMods` and call `syncSelectorLayout()` (the row takes the width of its cells). The LFO page opens a two-row `ListPopup` on the `Request` of the `+` (`LfoUi::openPlusList`). Pictures of the pages with it: `docs/ui-map/env-lfo-bold-cut/after/`.
