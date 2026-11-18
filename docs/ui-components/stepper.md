# Stepper and UpDownStepper

## What it is
`Stepper`: a box with a left arrow, the name of the current choice and a right arrow. Tap the left or right half to go to the previous or next choice. With `listable` set, a tap on the middle half asks the owner to open the whole list ([ListPopup](list-popup.md)). Use it for a long list (more than 8 choices) or when there is no room for a list of lines. For 2 to 8 choices use [LedList](led-list.md); for on / off use [LedToggle](toggle.md).
`UpDownStepper`: a vertical box with a number and an up and a down half: a small integer (the unison voices 1 to 16).

## Header and types
`shell-mpc/ui/base.h`: `Stepper`, `Stepper::State`, `UpDownStepper`, `UpDownStepper::State`. The envelope selector of the Play page is an `EnvCarousel` ([carousel.md](carousel.md)).

## State fields
`Stepper::State`:
| Field | Type | Meaning | Default |
|---|---|---|---|
| `count` | `int` | number of choices | 0 |
| `index` | `int` | the current choice | 0 |
| `text` | `char[48]` | the name of the current choice (about 12 characters fit at full size) | empty |
| `dim` | `bool` | the name drawn dim ("OFF") | false |
| `wrap` | `bool` | the list goes round at the ends | false |
| `caption` | `char[16]` | small name of the value at the top (MODE, STACK) | empty |

Members: `bool listable = false`, `bool contentName = false`, `int minWidth = kMinW` (208). Helpers `setItem (text)`, `setCaption (text)`.

`UpDownStepper::State`: `value`, `min`, `max` (ints, all 0 by default), `caption` (`char[12]`).

## Size
`Stepper::minSize()` = { min (208, `minWidth`), 56 }. Typical 208 x 56 to 300 x 64. A short-choice stepper in a grid may set `minWidth` lower (the compact oscillator's `source.stepper` uses 150, `FxSlotEditor::steppers` 100). Larger: the name gets more room.
`UpDownStepper::minSize()` = 72 x 112.

## Touch and knob behaviour
`Stepper` (the change is made on touch up, from the half the touch went down in):
| Gesture | Change |
|---|---|
| tap left half | `Index`: `index` = `value` = previous (clamped, or wrapped with `wrap`) |
| tap right half | `Index` next |
| tap middle half (`listable`; the outer quarters still step) | `Request` with the stepper's `id` |
| `turn (delta)` | `Index`, `delta` steps (rounded) |

Nothing at an end without `wrap`, nothing when `count <= 1`. Arrows are dim where they cannot step. `touchTargets` lists the two halves.
`UpDownStepper`: tap upper half = +1, lower half = -1 (`Index`, `index` = the new integer, clamped to `min` / `max`); `turn (delta)` adds `delta`.

## Q-Link
Both can be Q members, one slot. A turn is stepped. The mouse wheel is one step per notch.

## Options and variants
`wrap`, `dim`, `caption`, `listable`, `contentName`, `minWidth`. The Play pages' three top selectors (source, envelope, LFO) are [Carousels](carousel.md), not Steppers.

## Minimal example
```cpp
Stepper model;
model.place ({ 20, 20, 240, 56 });
model.setId (30);
model.setCaption ("MODEL");
model.listable = true;                      // a tap on the name asks for the list
model.state.count = names.size();           // the owner's list of choices
model.state.index = current;
model.setItem (names[current]);

Change c = model.touchDown (200, 40, nowMs);
c = model.touchUp (200, 40);                // right half: next
if (c.kind == ChangeKind::Index && c.id == 30)  current = c.index;
else if (c.kind == ChangeKind::Request)         { /* open a ListPopup */ }
```

## Used on
`FilterFullScreen::model`, `style` (listable, with `modelList` button); `MasterScreen::scaleType`, `scaleKey`; `FxSlotEditor::steppers`, `rack`, `page`; the table / content selector of `OscillatorCompact` (`BasicControls::source`); `UnisonBox::count` (`UpDownStepper`); `VoiceScreen::qStepSelect`; `ModMatrixView::sourceFilter`. Pictures: `docs/user-guide/img/filter.png`, `master-main.png`.

## Limits and pitfalls
- The stepper does not know the list: the owner sets `text` on every update from `index`.
- After a `Request` the owner opens a `ListPopup` with `showCurrent()`; the popup's `Index` change then sets the value.
- Both arrows have the colour of a label; the gradient means position and value only (a design rule).
- A name over about 14 characters at 22 px is cut with "...".
