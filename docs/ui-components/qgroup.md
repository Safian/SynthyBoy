# QGroup

## What it is
One bank of up to four Q-Link controls. On four-knob devices, a framed active group draws one outline with a single unnumbered Q on its upper-left edge. Each knob keeps its routing to the same group member. Use it for every block of up to four values on a page; reading order of the groups decides which knob reaches what ([PAGES.md](PAGES.md)).

## Header and types
`shell-mpc/ui/qgroup.h`: `QGroup` (not a `Component`), `QGroup::kSlots` = 4, `kMaxCheck` = 64.

## State
| Member | Meaning |
|---|---|
| `set (slot, Component*)` | put a member into slot 0 to 3 (null empties it); a member of an active group gets `setQ (true, slot + 1)` |
| `member (slot)`, `numMembers()` | read |
| `setActive (bool)`, `active()` | active: members get the Q colour on four-knob devices and the group draws its frame |
| `setFrameRect (Rect)`, `frameRect()` | a frame rectangle other than the union of the members |
| `setFrameless (bool)` | members lie apart, with Q-coloured labels on the active four-knob bank |
| `setBlock (int)`, `block()` | which block of four knobs the group has when it is NOT active: 0 active, 1 to 3 the next groups of the page, -1 none |
| `static setWideDevice (bool)`, `wideDevice()` | The MPC XL can map sixteen knobs at once and keeps fixed slots on pages with at most four groups. The overlay leaves its bank frames hidden in this mode. |
| `rect()` | the union of the members' rectangles |

## Size
The default frame encloses the union of its members. The Q marker sits on the upper-left frame edge and shares the frame's cache cell.

## Visual rule (owner, 2026-11-24)
Keep the single upper-left Q marker on a visible bank frame on Gen 1 devices. Components and previews have no numbered badges or reserved number lanes.
Preserve functional hardware mappings and meaningful parameter values.
- Controls keep their clean 48 px total height without adding an extra 16 px Q-lane or drawing Q1–Q4 / Q1–Q16 chips.
- Authentic historical before screenshots remain as evidence; remove numbers from our designed after state.

### Generation build policy (owner, 2026-11-24)
- **Gen 1 (ARMv7 32-bit standalone MPCs: Key 61, Live, One, X, etc.):** Q-Link framing is enabled (`SYNTHYBOY_MPC_UI_GEN=1`, `SYNTHYBOY_MPC_QFRAME_ENABLED=1`, `QVisualPolicy::kHasQFrame == true`). The active bank on 4-knob setups renders a factory-style bank frame and a single unnumbered Q marker at its upper-left edge (`Pen::qFocus`). Members get the Q accent colour on four-knob devices. Zero numbered chips.
- **Gen 2 (AArch64 64-bit standalone MPCs: XL, Live III):** NO bank frame and NO Q marker/label on any screen. The specific Q-frame drawing routines (`Pen::qFocus`, `Pen::qMarkerRect`, and `QGroup::draw` frame branch) are compiled out at compile time (`SYNTHYBOY_MPC_UI_GEN=2`, `SYNTHYBOY_MPC_QFRAME_ENABLED=0`, `QVisualPolicy::kHasQFrame == false`).
- **Knob routing preserved:** Hardware rotary knob delta routing (`turn(knobIndex, delta)`) and Q slot mappings are identical across all generations and knob configurations (both 4-knob and 16-knob). Generation visual policy is a compile/visual choice, not a feature restriction.
- **Build and test configuration:** Configured via `SYNTHYBOY_MPC_UI_GEN` (`auto`, `gen1`, `gen2`) in `shell-mpc/CMakeLists.txt` and `shell-mpc/ui/q_visual_policy.h`. Offline tests on host verify both policies via `q_visual_policy_gen1_host` and `q_visual_policy_gen2_host`.

## Behaviour
- `draw (sb, around = nullptr)` renders the active four-knob bank frame and its marker. An optional `around` rectangle lets the frame enclose the cards containing those controls.
- The overlay leaves idle groups clear. The release skin uses an empty `qlinkBoundsData` to suppress the SDK-provided bank highlight. Individual host control focus is a separate skin setting.
- `turn (knobIndex, delta)`: routes the knob (0 to 3) to the member of that slot; nothing when the group is not active or the slot is empty. A composite member passes it to its primary child.
- `check (all, n, &first)`: the adjacency check: how many components of `all` (the leaves, from `collectLeaves`) are not members, not parts of a member and overlap the union. 0 means the layout is correct.

## Touch and knob behaviour
The group itself takes no touch. The page makes a group active when a touch goes down on one of its members, and then calls `QGroup::turn`.

## Q-Link
This is the Q-Link object. Members 1 to 4 = Q-Link 1 to 4 of the active group; a page with several groups publishes them in reading order.

## Options and variants
`frameless` for apart members; `setFrameRect`; `setBlock` and `setWideDevice` for hardware routing.

## Minimal example
```cpp
Knob a ("A"), b ("B");
QGroup g;
g.set (0, &a);
g.set (1, &b);
g.setActive (true);                      // active bank frame and Q marker on four-knob devices
// in the draw: a.draw (sb); b.draw (sb); g.draw (sb);
Change c = g.turn (1, 0.01);             // hardware knob 2 -> member b, Value change
// layout test:
Component* leaves[64];
const int n = page.collectLeaves (leaves, 0, 64);
const int offenders = g.check (leaves, n);   // must be 0
```

## Used on
Every full screen: `OscillatorFullScreen::groups`, `EnvFullScreen::groups`, `LfoFullScreen::groups`, `FilterFullScreen::groups`, `MixerFullScreen::groups` (one per strip), `MasterScreen::groups`, `VoiceScreen::groups`, `ModSlotDetail::groups`, `MacroView::qgroup`, `ModMatrixView::qgroup`, `FxSlotEditor::groups`, `DetailPage::groups`.

## Limits and pitfalls
- Members must be adjacent: the layout test calls `check`; a foreign control inside the union is a defect.
- Yellow (the Q colour) is only for Q frames and chips.
- Slots follow reading order, left to right and top to bottom.
- Do not put a boxed frame on a member: the group draws the one frame.
