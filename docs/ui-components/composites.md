# Composite and the small composites (composite.h)

## What it is
`Composite` is the base class of every component that is built from other components: it places its children inside its rectangle (`layout()`), draws them, routes a touch to the child it went down on until the touch up (also outside the rectangle), and sends `turn` to its PRIMARY child. The ids of the children are the owner's (`child.setId`); a change comes out with the id of the child. The small composites below are ready-made combinations. Use them as they are, or derive your own.

## Header and types
`shell-mpc/ui/composite.h`: `Composite`, `ToggleNumber`, `KnobRandPair`, `SourcePicker`, `UnisonBox`, `WarpSlot`, `WarpPair`, `RoutePanel`, enums `OscInstance { Sub, A, B, C, Noise }`, `OscKind { Wavetable, Multisample, Granular, Sample, Spectral }`, `instanceName (i)`. Bigger composites: [type-bar.md](type-bar.md) (`TypeBar`, `TuneRow`, `BasicControls`). Needs `base.h`, `led_toggle.h`, `qgroup.h`.

## Composite (the base)
Protected API for writing one: `layout()` (pure virtual), `drawUnder (Pen&)`, `drawExtra (Pen&)`, `syncKids()`, `addKid (Component&, shown = true)`, `clearKids()`, `show (component, bool)`, `setPrimary (Component*)`, `kMaxKids` 160. Public: `primary()`, `numChildren()`, `child (i)`, `childShown (i)`. A hidden child (`shown` false) takes no touch, no drawing, no layout leaf. `setEnabled` disables all children. `setQ` gives the children the Q colour (index 0). `draw` makes one cached cell for the under- and one for the over-drawing; each child draws its own.

## State
Composites have public child members; the state is the children's. Per class:
| Class | Children (public members) | Primary child (`turn`) |
|---|---|---|
| `ToggleNumber` | `toggle` (`LedToggle`), `amount` (`NumberField` "AMOUNT", max 100) | `amount` |
| `KnobRandPair` | `main` (`Knob`), `rand` (`Knob` "RAND", small); ctor `KnobRandPair (label)` | `main` |
| `SourcePicker` | `stepper` (`Stepper`), `dropdown` (`MenuButton`, `Icon::Down`) | `stepper` |
| `UnisonBox` | `gear` (`DetailButton`, "MORE"), `count` (`UpDownStepper`, 1 to 16, caption "VOICES") | `count` |
| `WarpSlot` | `amount` (`Knob` "WARP", side), `type` (`Stepper`), `var` (`Knob` "VAR", small, shown only after `setHasVar (true)`); `setTypeOff (bool)`, `setMirrored (bool)` | `amount` |
| `WarpPair` | `first`, `second` (`WarpSlot`, the second mirrored); `setStacked (bool)` | none |
| `RoutePanel` | `target` (`Stepper`), `balance` (`Knob` "F1 F2", bipolar), `bus1`, `bus2` (`Knob`), `close` (`MenuButton`), `group` (`QGroup`: target, balance, bus1, bus2); `setTargetIsFilter (bool)`, `setPopup (popup, parentRect)`, `setBackdropId (id)` | `target` |

## Size
`minSize()`: `ToggleNumber` { `LedToggle::widthFor (label)` + 4 + 80, 56 }; `KnobRandPair` 96 x 136; `SourcePicker` stepper min + 4 + 56 by 56; `UnisonBox` 116 x 172; `WarpSlot` 184 + 4 + 208 (+ 140 with VAR) by 72 (the stepper is at most 300 px wide; the slot is centred in a larger room); `RoutePanel` 308 x 192. `kGap` = 4 between children; `layoutRow (...)` splits a row (see [helpers.md](helpers.md)).

## Touch and knob behaviour
Every change comes out with the child's id (see the child's page). `RoutePanel` in popup mode: a tap on the dimmed backdrop (inside `parentRect`, outside the box) is a `Request` with the `setBackdropId` id (close). `WarpSlot` with the type OFF: the knob and VAR take no touch, the stepper still works.

## Q-Link
`RoutePanel` has its own `QGroup group` (4 members). The others are members of a group of the screen that holds them (a composite as member passes `turn` to its primary child). `WarpPair` has no primary: put its two slots into two groups.

## Options and variants
`KnobRandPair` for a value with a random spread; `WarpSlot` hides VAR when the type has none; `RoutePanel::setPopup`.

## Minimal example
```cpp
KnobRandPair pair ("PHASE");
pair.main.setId (11);  pair.rand.setId (12);
pair.place ({ 40, 100, 120, 160 });
pair.main.state.value = phase;  pair.rand.state.value = randAmount;
QGroup g;  g.set (0, &pair);   g.setActive (true);
Change c = g.turn (0, 0.01);     // goes to pair.main -> Value with id 11
```
A new composite:
```cpp
class Row2 : public Composite
{
public:
    Knob a, b;
    Row2() { addKid (a); addKid (b); setPrimary (&a); }
    Size minSize() const override { return { 200, 72 }; }
protected:
    void layout() override { a.place ({ rect_.x, rect_.y, rect_.w / 2, rect_.h }); b.place ({ rect_.x + rect_.w / 2, rect_.y, rect_.w - rect_.w / 2, rect_.h }); }
};
```

## Used on
`UnisonBox`, `WarpPair`, `RoutePanel`, `ToggleNumber`, `BasicControls` in `shell-mpc/overlay/osc_ui.h`; `KnobRandPair` in `KnobPanel` presets; `SourcePicker` inside `BasicControls::source`. Pictures: `docs/user-guide/img/osc-a.png`, `osc-unison.png`.

## Limits and pitfalls
- A child must be added with `addKid` or it gets no touch, draw or layout leaf.
- Place children in `layout()` only; `place()` of the composite calls it.
- A Q group needs adjacent members: the children of a composite count as part of the member (`QGroup::check` accepts them).
- `PanelSlot`-style tables (`KnobPanel`) are better than hand layout for a grid of knobs (see [knob-panel.md](knob-panel.md)).
