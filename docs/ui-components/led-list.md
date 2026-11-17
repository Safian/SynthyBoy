# LedList

## What it is
A radio list of 2 to 8 options. Every option is visible as a row (flat dot + name), one is lit. One tap selects. Use it whenever a parameter has 2 to 8 named values and there is room for all of them. For more than 8, or when the rows do not fit, use a [Stepper](stepper.md) with [ListPopup](list-popup.md). For a pure on / off use [LedToggle](toggle.md).

## Header and types
`shell-mpc/ui/led_list.h`: `LedList`, `LedList::State`. Uses `namespace led` of `led_toggle.h`.

## State fields
| Field | Type | Meaning | Default |
|---|---|---|---|
| `count` | `int` | number of options, 0 to `kMaxOptions` (8) | 0 |
| `index` | `int` | the lit option | 0 |
| `disabledMask` | `uint8_t` | bit i set: option i is not valid now (dim, no reaction, skipped by steps) | 0 |
| `names[8][24]` | `char` | option names (23 characters) | empty |
| `title` | `char[24]` | small title above the list | empty |
| `titleInLane` | `bool` | the title is drawn in the lane of the Q chip above the rectangle and takes no room | false |

Helpers: `setOption (i, name)`, `setOptions (const char* const* names, n)`, `setTitle (s)`, `optionEnabled (i)`. Constants `kMaxOptions` 8, `kNameLen` 24, `kTitleH` 24, `kMaxColumns` 3.

## Size
The list chooses 1, 2 or 3 columns from its rectangle: the fewest where each row is at least 56 px high and each column as wide as the widest name needs (`columnNeed()`; static `LedList::columnsFor (n, w, h, titleH, columnNeed)`). `minSize()` is the arrangement of least area (for example 56 x n high in one column); `sizeOk()` is true when ANY arrangement fits, so check `sizeOk()` and not `minSize()` in a layout test. `columns()`, `rows()`, `rowRect (i)` give the result. Where nothing fits, the choice with the highest rows is used and names are cut with "...".

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| tap on an enabled row (on touch up) | `Index`: `index` = `value` = the option |
| `turn (delta)` | moves over the ENABLED options, no wrap, `Index` |
| mouse wheel | `wheelUnit()` is -1: the wheel up goes up the list |

Tapping the lit option gives nothing. `touchTargets` lists every row (`rowRect`). `optionAt (x, y)` finds a row.

## Q-Link
Can be a Q member, one slot. A turn is stepped. The title and the lit name take the Q colour in the active group.

## Options and variants
`title`, `titleInLane` (a title next to the Q chip), `disabledMask` (options that are not valid in the current state).

## Minimal example
```cpp
LedList mode;
static const char* const names[] = { "Forward", "Reverse", "Ping pong" };
mode.setOptions (names, 3);
mode.setTitle ("DIRECTION");
mode.setId (50);
mode.place ({ 20, 100, 200, 200 });          // 3 rows of 56 px + title fit one column
mode.state.index = current;
if (! mode.sizeOk()) { /* the layout gave too little room */ }

Change c = mode.touchDown (60, 170, nowMs);
c = mode.touchUp (60, 170);
if (c.kind == ChangeKind::Index && c.id == 50) current = c.index;
```

## Used on
`VoiceScreen::rowList`, `scaleLfoMode`; `MasterScreen::voiceOverride`, `sameNote`, `stealPriority`, `scaleMode`, `tuningScale`, `mpeMacro`; `FilterFullScreen::instances` (CURVES), `routing`; `FilterRoutePanel::destination`; `LfoFullScreen::direction`, `modeList`; `MacroView::bankList`; `SyncSwitch::mode` and `RateControl::modeCells` (HZ / BPM / KEY); `FxSlotEditor::lists`. Pictures: `docs/user-guide/img/master-main.png`, `voice.png`.

## Limits and pitfalls
- At most 8 options: more is a Stepper with a popup.
- The names must be the shown names; for parameters whose engine labels carry product names use `shownChoice (key, index, engineName)` from `choice_names.h`.
- The choice of columns depends on the rectangle: give the list a cell that fits (rows >= 56 px) or the lint flags it.
- A list inside a Q group takes the whole list as ONE member (one chip), not one per option.
