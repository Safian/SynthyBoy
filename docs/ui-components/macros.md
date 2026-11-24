# MacroView, MacroCard, SpectrumStrip

## What it is
The Macros page uses a full-width upper spectrum display paired with a single bottom row of eight compact macro cards (zero paging, zero hidden bank, and no header bank switcher):
- **Clean Header**: `"MACROS"` title at `{14, 4, 260, 56}` with divider line at `y = 62`. No `BANK 1 / BANK 2` switcher control in overlay or skin.
- **SpectrumStrip**: Large full-width studio spectrum display across the upper area `{8, 72, 1264, 290}` (256-point log-frequency curve, 20 Hz .. 20 kHz, harmonic stroke and shaded cyan fill, no touch).
- **Bottom 8-Card Single Row (Macros 1 to 8)**: `{8, 382, 1264, 238}` holding 8 compact `152 × 238 px` `MacroCard` instances side by side (`gap = 6 px` within a 4-card bank, `bank_gap = 12 px` between card 4 and card 5, `x = 8, 166, 324, 482, 646, 804, 962, 1120`).
- Each compact `MacroCard` (`152 × 238 px`, shared via [sizes.json](../../components/sizes.json) -> [macro_card_contract.h](../../shell-mpc/ui/macro_card_contract.h)) stacks four non-overlapping elements vertically:
  1. Centered square `90 × 90 px` unipolar blue rotary dial (`#3568c0`, `play_sub_level_90.png`, hitbox `{29, 2, 94, 92}` in `dial_rect` `{0, 0, 152, 118}`).
  2. Centered single value readout (`{8, 96, 136, 22}`, 20 px font, host-owned).
  3. Centered macro name button (`nameLabelRect` `{8, 136, 136, 24}`, 56 px tall `nameRect` hitbox `{4, 120, 144, 56}`, adaptive underline, overlay-owned).
  4. Compact `48 px` native action button (`"TARGETS"`) inside a 56 px touch slot (`nativeTargetArea` `{4, 178, 144, 56}`, host-owned).

## Header and types
[mod_macros.h](../../shell-mpc/ui/mod_macros.h) + [macro_card_contract.h](../../shell-mpc/ui/macro_card_contract.h): `MacroView : Composite`, `MacroCard : Composite`, `SpectrumStrip : Component`.

## State
- `MacroView`: `cards[kNumMacros = 8]`, `spectrum`, `qgroup`; `setBank (b)`, `bank()`, `updateQGroup()`, `turnQ`, `cardsRect()`; `kBarH` 64, `kSpectrumH` 290.
- `MacroCard`: `macroIndex` (0 to 7), `macroName_[24]`, `knob` (state/Q adapter only, outside overlay children; `Knob`, vertical stacked `side = false` with empty knob label so `Knob::geom()` stays fixed at 90 px), `nameId` (500 + i); `setMacro (idx, name)` (empty name = `"MACRO"`), `macroName()`, `setValue (val, text)`, `nameLabelRect()` `{ rect_.x + 8, rect_.y + 136, 136, 24 }`, `nameRect()` `{ rect_.x + 4, rect_.y + 120, 144, 56 }`, `nativeTargetArea()` `{ rect_.x + 4, rect_.y + 178, 144, 56 }`, `nativeKnobArea()` `{ rect_.x + 29, rect_.y + 2, 94, 92 }`.
- `SpectrumStrip`: `levels` (`const float*`, `kPoints` = 256 values 0 to 1, 20 Hz to 20 kHz; null draws only the static grid/frame).

## Size
`MacroView::minSize()` 1280 × 628; `MacroCard::minSize()` 152 × 238; `SpectrumStrip::minSize()` 400 × 100 (layout uses 1264 × 290).

## Touch and knob behaviour
| Gesture | Change | Action |
|---|---|---|
| drag on a macro dial | host parameter binding | Host writes `macro.1` .. `macro.8`; overlay never captures the dial |
| turn a mapped Q-Link | parameter/Q adapter | Writes the existing mapped macro parameter |
| tap on the centered macro name (`nameId` set; 56 px tall hitbox) | `Request` with `nameId` | Owner opens [TextEntry](text-entry.md) (`kPageKeyboard = 52`) to rename the macro |
| tap on standalone action button (`TARGETS`, 48 px face in 144 × 56 px slot) | `Request` via `kMacroTarget` | Opens modulation targets page (`kPageTargets = 50`) for this macro; does not alter macro value |

## Fixed Hardware Q-Link Routing & Gen1/Gen2 Focus
- **Fixed Q-Link Parameter Mapping** (`MacroUi::publishQGroup` via `qSlots_.publish(v, 2, 0, ...)`):
  - **Q 1..4** (Hardware Bank 1) always map to `macro.1` .. `macro.4`.
  - **Q 5..8** (Hardware Bank 2) always map to `macro.5` .. `macro.8`.
  - **Q 9..16** are unmapped (`-1` / `kNone`).
- Touching any card or turning a hardware Q-Link knob updates only the visual bank focus (`view_.setBank(0 | 1)`); Bank 1 (`1..4`) and Bank 2 (`5..8`) hardware target assignments never swap.
- **Device & Build Policy (`QGroup` / `QVisualPolicy`)**:
  - **Gen1 + 4-knob hardware (Key 61, `wideDevice() == false`)**: Draws one yellow focus frame around the active 4-card bank (`{8, 382, 626, 238}` or `{646, 382, 626, 238}`) with a single unnumbered `'Q'` chip marker at its top-left corner and highlights the active bank's macro names. Zero numbered Q badges.
  - **MPC XL (16 encoders, `wideDevice() == true`)**: Focus frame and `'Q'` marker are suppressed; all 8 macros remain directly mapped.
  - **Gen2 (`SYNTHYBOY_MPC_QFRAME_ENABLED == 0`)**: Focus frame and `'Q'` marker compile out completely.

## Layer and Aperture Ownership
- **Host Skin Layer**: Renders the `90 × 90 px` unipolar blue rotary dial (`macroSubLevel` / `play_sub_level_90.png`), the centered single value readout (`{8, 96, 136, 22}`, e.g. `"35.0%"`), and the compact `48 px` action button (`"TARGETS"`, `{4, 178, 144, 56}`). Leaves `nameRect` clear (`include_name=False`) to avoid double-painting.
- **Overlay Layer**: Owns the header `"MACROS"` title, centered interactive macro name text + adaptive underline (`nameLabelRect` / `nameRect`), live spectrum curve fill (`SpectrumStrip`), and Gen1 Key 61 active bank frame/marker (drawn before `spectrum.drawLive` to preserve single-cell-layer ordering in GPU scenes).
- The host-availability interface controls input handoff only. Disabling it does not restore GPU dials, values, target buttons or destination chips. Macro names and their rename action remain overlay-owned.
- **Single Value Principle**: Exactly one value readout per macro card; no duplicate value or name labels between host skin and overlay.

## Sub-Pages
- **Targets page** (`kPageTargets = 50`): Tapping a macro card's `TARGETS` action button opens its modulation targets list.
- **Keyboard page** (`kPageKeyboard = 52`): Tapping a macro name button opens `TextEntry` for renaming.

## Used on
[macro_ui.h](../../shell-mpc/overlay/macro_ui.h) (`MacroUi`, `kElementPage` 8; macro names loaded via `macro_names.h` from preset/chunk state).  
Previews: [macros-screen-preview.png](../screens/macros.jpg), [macros-component-comparison.png](https://github.com/Safian/SynthyBoy/blob/b91c23c3/docs/ui-map/macros-component-comparison.png).

## Limits and pitfalls
- Historical 4-chip destination list inside `MacroCard`, the 2×2+1×4 Terv A split, and the top `BANK 1 / BANK 2` switcher are retired.
- `SpectrumStrip` draws every picture (owner refreshes `levels`).
- Synthetic fixtures used in offline PNG previews are verification fixtures, not hardware screenshots or measurements.
- Documentation links are repo-relative so they remain valid across worktree lifecycles.
