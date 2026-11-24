# Knob

## What it is
A rotary control for a continuous value from 0 to 1: a dial with a name, an optional value text, an optional modulation arc and a modulation chip. Use it for any continuous parameter (cutoff, mix, pan). Do not use it for whole numbers (use [NumberField](number-field.md)), levels with a meter (use [Fader](bars.md)) or on / off (use [LedToggle](toggle.md)).

## Header and types
`shell-mpc/ui/base.h`, `synthyboy_mpc::ui::Knob` (derives from `Component`, see [core.md](core.md)). State: `Knob::State` (derives from `ModInput`).

## State fields
| Field | Type | Meaning | Default |
|---|---|---|---|
| `label` | `char[24]` | name under the dial (about 8 characters fit; longer is cut with "...") | empty |
| `valueText` | `char[16]` | optional value text under the label, formatted by the owner | empty |
| `value` | `double` | the value, 0 to 1 | 0.0 |
| `modValue` | `double` | the live modulated value, 0 to 1; a marker is drawn when it differs from `value` | 0.0 |
| `defaultValue` | `double` | the value a double tap sets | 0.0 |
| `bipolar` | `bool` | detent mark at the centre, the arc runs from the centre | false |
| `small` | `bool` | small variant with a centre dot (the secondary RAND knob) | false |
| `large` | `bool` | the dial may grow to 190 px instead of 140 px | false |
| `dialMax` | `int` | side variant: largest dial in pixels (0 = as large as the row allows, 120) | 0 |
| `side` | `bool` | medium variant: dial at the left, name and value in a column at its right | false |
| `led` | `bool` | a LED on the label: the knob carries the on / off switch of what it sets | false |
| `ledOn` | `bool` | the switch is on; off = drawn dim and no touch except the switch | true |
| `isDb` | `bool` | whether the parameter is in dB (enables 0.1 dB / 2 px fine tuning) | false |
| `dbMin`, `dbMax` | `double` | dB range when `isDb` is true (e.g. -60.0 to +6.0) | 0.0 |
| `skew` | `double` | non-linear curve exponent (e.g. 2.5 for volume) | 1.0 |
| `mods[8]`, `numMods`, `selectedSource` | from `ModInput` | the modulations of this control (source id, name of 6 characters, depth -1 to +1, polarity, bypassed) and the selected source | none, `kNoSource` |

Add a modulation with `state.add (sourceId, "LFO 1", depth, bidirectional, bypassed, macro)`.

## Size
`minSize()`: 100 x 72; small variant 72 x 56; side variant 172 x 72. Typical: 120 x 160 (stacked), 184 x 72 (side). Larger: the dial grows up to 140 px (190 with `large`), the rest is margin. The whole rectangle is the touch target.

## Touch and knob behaviour
| Gesture | Change |
|---|---|
| vertical drag (no dead zone: any movement >= 1 px changes value; fine mode < 80 px/s changes 0.1 dB per 2 px or 1/1000 range per 2 px; fast movement uses coarse mapping 200 px = full range; transitions apply to deltas smoothly) | `Value`, `id`, `value` 0 to 1 |
| tap on value text (`valueHit()`) | `Request`, `id`: requests numeric entry (owner opens `TextEntry`) |
| double tap | `Value` with `defaultValue` (not when already there) |
| drag that starts on the modulation chip (only when `selectedSource` is in `mods`) | `Depth`: `index` = source id, `value` = new depth -1 to +1 |
| long press, 600 ms (`tick (nowMs)` must be called) | `ModsRequest` |
| tap on the label or LED (only with `led`) | `Index` 0 / 1 (`value` the same), made on touch up |
| `turn (delta)` | `Value`, `value + delta`, clamped |
| mouse wheel | one notch = 1/128 (`wheelUnit()`) |

A change that does not change the value (end stop, default already set) is not reported. A tap-vs-drag threshold of at most 2 px ensures tapping value text or double tapping works reliably. A sideways slide is not a drag.

## Q-Link
Can be a Q member (`QGroup::set (slot, &knob)`), one slot. A turn adds `delta` to the normalised value (continuous). In a Q group the label takes the Q colour.

## Options and variants
- `side`: dial left, texts right (the chip sits in the text column). Min 172 x 72.
- `dialMax`: caps the dial of the side variant (a PHASE knob next to a bigger one uses 52).
- `small`: secondary knob.
- `led`: a SMOOTH knob whose switch and amount are one control.
- `bipolar`: pan-like; set `defaultValue = 0.5` and `value = modValue = 0.5`.
- `geom()` returns the dial geometry (public for tests).

## Minimal example
```cpp
using namespace synthyboy_mpc::ui;
enum { kCutoff = 7 };
double stored = 0.2;                      // the truth is the owner's

Knob k ("CUTOFF");
k.place ({ 50, 40, 120, 160 });
k.setId (kCutoff);
k.state.defaultValue = 0.5;
k.state.value = k.state.modValue = stored;   // every update, from the parameter

Change c = k.touchDown (110, 150, nowMs);
c = k.touchMove (110, 100);                  // 50 px up: +0.25
if (c.kind == ChangeKind::Value && c.id == kCutoff)
    stored = c.value;                        // write back; the next update shows it
k.touchUp (110, 100);
```

## Used on
Every page. Examples: `VoiceScreen::randPan` (`shell-mpc/ui/voice_screen.h`, bound in `shell-mpc/overlay/voice_ui.h` as `bind (kRandPan, &full_.randPan, T::Knob, "voicectl.rand.pan")`), `EnvFullScreen::attack`, `MasterScreen::volume`, `FilterFullScreen::cutoff` (`FilterKnob`), `FxSlotEditor::knobs`, `MacroCard::knob`. Pictures: `docs/screens/env.jpg`, `filter.png`, `voice.png`.

## Limits and pitfalls
- Never keep the value in the knob: write `state.value` every update, or the knob does not follow host automation.
- A modulated knob needs both `value` and `modValue`; set both equal when nothing modulates.
- Call `tick (nowMs)` while a finger is down, else there is no long press.
- Label over about 8 characters is cut. Value text over 15 characters is cut.
- The `Index` change of a `led` knob has the same `id` as the `Value` change: test `kind`.
