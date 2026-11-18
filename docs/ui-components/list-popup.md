# ListPopup

## What it is
A popup list of choices with a dimmed backdrop: a box of rows of 56 px; tap a row to choose, tap outside to close. Scrolls when the list is longer than the box. Use it as the list behind a [Stepper](stepper.md) with `listable` (its `Request`), a dropdown button, or a file list (a header with a back arrow). Not for 2 to 8 values (use [LedList](led-list.md)).

## Header and types
`shell-mpc/ui/list.h`: `ListPopup`, `ListPopup::State`.

## State fields
| Field | Type | Meaning | Default |
|---|---|---|---|
| `items` | `const char* const*` | the owner's array of names, NOT copied (for example the engine's choice labels) | null |
| `count` | `int` | number of items | 0 |
| `current` | `int` | the current item (highlighted) | 0 |
| `header` | `const char*` | a bar of its own above the rows (a path); null = none | null |
| `headerBack` | `bool` | the header is also the way up: back arrow and a `Request` on tap | false |
| `icons` | `const int*` | pictogram id (`pictogram::Id`) of each row (< 0 or null = none) | null |
| `dotMask` | `unsigned` | bit i set: a small dot at the right end of row i | 0 |
| `dotColours` | `const uint32_t*` | colour of the dot of each row (rgb); null = accent | null |

Setup: `place (box)`, `setPopup (parentRect)` (the rectangle the dimmed backdrop covers), `setBackdropId (id)`, `setHeaderId (id)`. `showCurrent()` scrolls the current item into the window (call it when opening).

## Size
`minSize()` = 240 x (3 x 56 + 8). `kRowH` 56, `kPad` 4, `kZoneW` 44, `kBarW` 10. Typical: 600 x (8 rows) as in `VoiceUi::setCanvas`. Whole rows only: nothing is cut at the box edge. `visibleRows()` says how many fit.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap on a row | `Index`: `index` = `value` = the row |
| tap on the backdrop (inside `parent`, outside the box) | `Request` with `setBackdropId` id (close requested) |
| tap on the header (`headerBack`) | `Request` with `setHeaderId` id |
| vertical drag | scrolls by whole rows (no change out) |
| touch in the scroll zone (44 px at the right edge, when the list is longer than the box) | the thumb follows the finger; a touch on the track pages by a window less one row; nothing selects a row |
| `turn (delta)` | moves the current item, `Index` |
| mouse wheel | `wheelUnit()` = -1: wheel up goes up |

## Q-Link
Not a Q member. A turn moves the current item and the list follows it.

## Options and variants
Header with back arrow (content browser); dots (the envelopes that have targets); scrolling zone.

## Minimal example
```cpp
ListPopup list;
list.place ({ 340, 130, 600, 8 * ListPopup::kRowH + 2 * ListPopup::kPad });
list.setPopup ({ 0, 110, 1280, 628 });        // the tab: dimmed behind the box
list.setId (700);
list.setBackdropId (701);
list.state.items = names;                      // const char* const names[]
list.state.count = numNames;
list.state.current = current;
list.showCurrent();
// route the events of the page to it while it is open:
Change c = list.touchDown (x, y, nowMs);
c = list.touchUp (x, y);
if (c.id == 700 && c.kind == ChangeKind::Index) { /* write c.index; close */ }
else if (c.id == 701) { /* close */ }
```
The change arrives on touch up: use the result of `touchUp`.

## Used on
Every `*_ui.h` that has a stepper with a list: `shell-mpc/overlay/voice_ui.h` (`list_`, `kListBackdrop`, opened by `stack.openDetail (kPageList)`), `filter_ui.h`, `fx_ui.h`, `lfo_ui.h`, `master_ui.h`, `macro_ui.h`, `mixer_ui.h`, `mod_ui.h`, `env_ui.h`, `osc_ui.h` (also with `header` for the content browser).

## Limits and pitfalls
- `items` is a pointer to the owner's array: it must outlive the popup.
- A popup is drawn over everything: draw it last. It is not a `Composite` child of the page.
- Item text is fitted to the row box with `Pen::textLeft` (with an ellipsis when it does not fit).
