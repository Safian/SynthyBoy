# NoteMask

## What it is
The twelve notes of an octave as LED cells in piano arrangement (seven white notes below, five black above): a lit cell is a note in the set. Use it to choose a set of pitch classes (a custom scale). Not for a single note (use [Stepper](stepper.md)).

## Header and types
`shell-mpc/ui/note_mask.h` (includes `led_toggle.h`; included by `ui.h`): `NoteMask`, `NoteMask::State`.

## State fields
| Field | Type | Meaning | Default |
|---|---|---|---|
| `mask` | `int` | bit 0 = C ... bit 11 = B; a set bit is a note in the set | 0 |
| `label` | `char[24]` | small label (`setLabel`) | empty |

Static helpers: `isBlack (note)`, `noteName (note)`, `whiteIndex (note)`; `cellRect (i)`, `cellAt (x, y)`.

## Size
`minSize()` = 434 x 120 (7 x 62 by 2 x 56 + 8). Every cell is at least 56 px wide and high at that size.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap on a cell (on touch up) | `Index`: `index` = `value` = the NEW MASK (that cell toggled) |
| `turn (delta)` and the wheel | steps the mask as a number by whole steps |

## Q-Link
Can be a Q member, one slot; a Q-Link step moves the mask as a number (what a knob mapped to the mask parameter does).

## Options and variants
None beyond `label`.

## Minimal example
```cpp
NoteMask scale;
scale.setLabel ("NOTES");
scale.setId (80);
scale.place ({ 20, 20, 434, 120 });
scale.state.mask = mask;                      // bit per note
Change c = scale.touchDown (60, 100, nowMs);
c = scale.touchUp (60, 100);
if (c.kind == ChangeKind::Index && c.id == 80) mask = c.index;
```

## Used on
`shell-mpc/overlay/osc_ui.h` (`T::Mask`: the notes of the transpose snap, bits 0 to 11 of the parameter); no library screen contains it, the owner places it. Look: same flat dot as the other LED controls.

## Limits and pitfalls
- The change carries the whole mask, not the note.
- At the minimum size nothing may be smaller than 56 px; the lint checks it.
