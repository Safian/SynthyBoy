# NumberField

## What it is
A box with a label (or a small icon) and a whole number or a value in steps: octave, semitone, voices, grid size. Drag up or down to change it by steps. Use it for a whole number or a decimal with a fixed step. Not for a 0 to 1 amount (use [Knob](knob.md)), not for a list of names (use [Stepper](stepper.md)).

## Header and types
`shell-mpc/ui/base.h`: `NumberField`, `NumberField::State` (derives from `ModInput`), `NumberField::Format { IntSigned, Decimal2, Degrees, IntPlain }`, `NumberField::Icon { None, Phase, Rand }`.

## State fields
| Field | Type | Meaning | Default |
|---|---|---|---|
| `label` | `char[24]` | head text (8 characters) | empty |
| `icon` | `Icon` | a small icon in the head (alone or beside the label) | `None` |
| `value`, `min`, `max`, `step` | `double` | value and range in its own units | 0, 0, 1, 1 |
| `defaultValue` | `double` | set by a double tap | 0 |
| `format` | `Format` | `IntSigned` "+2", `Decimal2` "-64.00", `Degrees` "180" + degree sign, `IntPlain` "17" | `IntSigned` |
| `notSet` | `bool` | drawn as "-" | false |
| `readOnly` | `bool` | drawn normally, no reaction | false |
| `pxPerStep` | `int` | pixels of drag per step | 8 |
| `text` | `char[16]` | a text that replaces the formatted value ("MID") | empty |
| `mods`, `numMods`, `selectedSource` | `ModInput` | modulations: chip at the top left, an underline bar with the depth segment | none |

Member `Size maxBox {0, 0}`: limits the drawn box (at the left, centred in height) while the touch zone stays the whole cell.
Constructor: `NumberField (const char* label, Format f)`. `formatValue (state, out)` is static and public.

## Size
`minSize()` 80 x 56. Typical 100 x 72. A larger rectangle just draws a bigger box; use `maxBox` to keep it small inside a big cell.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| vertical drag | `Index`: `index` = steps from `min` (0 to (max - min) / step), `value` = `min + index * step` |
| `turn (delta)` | `delta` steps (rounded), same `Index` |
| double tap | `Index` of `defaultValue` |
| drag from the chip (56 x 52 px zone) | `Depth` |
| long press 600 ms (`tick`) | `ModsRequest` |

Nothing when `readOnly`, at the ends, or when the value would not change.

## Q-Link
Can be a Q member, one slot. A turn is stepped (whole steps).

## Options and variants
`Format` and `text` pick the look; `icon = Phase` or `Rand` draws a die or phase mark; `maxBox` for big cells.

## Minimal example
```cpp
NumberField oct ("OCT", NumberField::Format::IntSigned);
oct.place ({ 0, 0, 100, 72 });
oct.setId (21);
oct.state.min = -4;  oct.state.max = 4;
oct.state.value = stored;                     // from the parameter every update
Change c = oct.turn (+1);                     // or touch events
if (c.kind == ChangeKind::Index && c.id == 21)
    stored = c.value;                         // the value in its own units
```
Note: a parameter with step 1 and `min` 0: `index == value`.

## Used on
`TuneRow::oct/sem/fin/crs` (`composite.h`, Osc pages), `VoiceScreen::length` and `stepValue`, `MasterScreen::transpose`, `polyphony`, `tuningA4`, `LfoFullScreen::keyTranspose`, `ShapeEditorPage::fx`, `FxSlotEditor::numbers` (voices).

## Limits and pitfalls
- `Index.index` is steps from `min`, not the value. Use `value` for the engine unit.
- A step of 0 or less counts as 1.
- `Decimal2` with `step` 0.01 needs a small `pxPerStep` (the CRS field uses 2).
