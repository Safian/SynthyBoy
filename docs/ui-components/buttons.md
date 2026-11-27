# MenuButton, DetailButton, RouteButton, RouteDropdown

## What it is
Buttons that ask the owner to open something. `MenuButton`: a labelled button with an optional icon (BACK, CLOSE, a dropdown with a current value). `DetailButton`: a "summary and details" button (gear, bell, route icon or a label) with an optional main value you can drag. `RouteButton`: a 64 x 64 button with a 3 character target and two indicator segments (the output route). `RouteDropdown`: a frameless dropdown with a hairline (the strip destination).
Use them for navigation and for opening pages or popups. They never change a parameter themselves.

## Header and types
`shell-mpc/ui/base.h`: `MenuButton` (`State`, `Icon { Down, Gear, None, Expand }`), `DetailButton` (`State`, `Icon { None, Gear, Bell, Route, Envelope }`), `RouteButton : DetailButton` (`State`). `shell-mpc/ui/mixer_strip.h`: `RouteDropdown : MenuButton`.

## State fields
`MenuButton::State`: `label` (`char[24]`), `icon` (`Icon`, default `Down`), `value` (`char[24]`, optional second line, the current choice). Constructor `MenuButton (label, icon = Icon::Down)`; `setLabel`, `setValue`.

`DetailButton::State`: `label` (`char[24]`), `icon` (default `None`), `hasValue` (false), `integer` (false), `text` (`char[16]`, the shown main value, empty = the number), `value` (0), `min` (0), `max` (1), `pxPerStep` (8). Constructor `DetailButton (Icon, label = "")`; `setLabel`, `setValueText`.

`RouteButton::State`: `target` (`char[8]`, 3 characters shown: "M", "F1", "F2", "D", "-"), `segment[2]` (two indicators, their meaning is the owner's). Constructor `RouteButton (target)`; `setTarget`.

## Size
`MenuButton` 56 x 56 minimum. `DetailButton` 72 x 56, or 116 x 56 with icon and label. `RouteButton` 64 x 64. Larger: the drawing grows with the box; the touch target is the whole rectangle.

## Touch and knob behaviour
| Component | Gesture | Change |
|---|---|---|
| `MenuButton` | tap | `Request` with `id` (turn does nothing) |
| `DetailButton` | tap (never left the 6 px square) | `Request` (details wanted) |
| `DetailButton` with `hasValue` | vertical drag, `turn (delta)` | `Value` (0 to 1, 200 px = full range) or, with `integer`, `Index` (`pxPerStep`) |
| `RouteButton` | tap | `Request` (drag and turn do nothing) |

## Look
Every box these buttons (and Stepper, UpDownStepper, ToggleIcon, Carousel, TextEntry, UsedSelector, PictogramSelector) draw goes through `Pen::control`: the legacy gradient face of the skin Button (flat top colour to a third of the height, linear to the bottom colour at two thirds, flat below; the keyline ramps the same way; a focused control takes `keylineHi`). The colours and the shape are the skin tokens (`button.gradient.*`, `control.gradient`) through the generated `theme_tokens.h`, so a Button token change reaches the overlay (test `scripts/tests/test_overlay_control_look.py`).

## Q-Link
`DetailButton` with a main value and `MenuButton` can be Q members (a `DetailButton` turn changes its main value; without a value it does nothing). The label takes the Q colour.

## Options and variants
`MenuButton::Icon::None` = text only; `Expand` = four corner brackets (full screen); `Gear`; `Down`. `DetailButton` icons draw with `Pen::iconGear`, `iconBell`, `iconRoute`, `iconEnvelope`. `RouteDropdown` is a `MenuButton` with another look, not a new behaviour.

## Minimal example
```cpp
MenuButton back ("BACK", MenuButton::Icon::None);
back.setId (900);
back.place ({ 8, 8, 120, 56 });
Change c = back.touchDown (40, 30, nowMs);
c = back.touchUp (40, 30);
if (c.kind == ChangeKind::Request && c.id == 900)
    stack.back();                              // owner decides what a request means

DetailButton gear (DetailButton::Icon::Gear, "MORE");
gear.setId (901);
```

## Used on
`MenuButton`: BACK, CLOSE, REMOVE, TARGETS, EDIT, UNDO on almost every page (`DetailPage::back`, `ModTargetsPage::add`, `ShapeEditorPage::back`), `OscillatorCompact::full` (Expand), `FilterFullScreen::modelList` (Down). `DetailButton`: `UnisonBox::gear`, `LfoFullScreen::targets`. `RouteButton`: `OscillatorCompact::route`, `FilterFullScreen::route`. `RouteDropdown`: `MixerStrip::destButton`.

## Limits and pitfalls
- A button is a request, not a switch: the owner opens the page and keeps the state.
- `RouteButton::State::segment` has no hard-coded meaning; document it in your owner code.
- Use `MenuButton` with a `value` text for a "caption + current choice" button, not a second Stepper.
