# QGroup

## What it is
The four values one set of hardware knobs (Q-Link) controls at once. It draws ONE frame in the Q colour around its members, the index chip 1 to 4 on each member, and routes a hardware knob to the member. Use it for every block of up to four values on a page; reading order of the groups decides which knob reaches what ([PAGES.md](PAGES.md)).

## Header and types
`shell-mpc/ui/qgroup.h`: `QGroup` (not a `Component`), `QGroup::kSlots` = 4, `kMaxCheck` = 64.

## State
| Member | Meaning |
|---|---|
| `set (slot, Component*)` | put a member into slot 0 to 3 (null empties it); a member of an active group gets `setQ (true, slot + 1)` |
| `member (slot)`, `numMembers()` | read |
| `setActive (bool)`, `active()` | active: members get the Q colour and chips, the group draws its frame |
| `setFrameRect (Rect)`, `frameRect()` | a frame rectangle other than the union of the members |
| `setFrameless (bool)` | members lie apart (no frame, the chips and the Q colour mark the quad; no adjacency check) |
| `setBlock (int)`, `block()` | which block of four knobs the group has when it is NOT active: 0 active, 1 to 3 the next groups of the page, -1 none |
| `static setWideDevice (bool)`, `wideDevice()` | process wide: the device has 16 knobs (idle chips show 5 to 16), set once by the overlay |
| `rect()` | the union of the members' rectangles |

## Size
A group has no size of its own: its rectangle is the union of its members. Layouts leave `kQLane` = 16 px above each member for its chip.

## Behaviour
- `draw (sb, around = nullptr)`: the frame (nothing when not active) and the chips of the active group. `around`: the frame runs around cards that contain the members.
- `drawIdle (sb)`: quiet chips of a group that is not active. `drawMapped (sb)`: idle chips only when `block() >= 1`. Static `drawMappedMembers (sb, block, members, n)`: the same for the rows of a list page. Static `chip`, `chipRect`.
- `turn (knobIndex, delta)`: routes the knob (0 to 3) to the member of that slot; nothing when the group is not active or the slot is empty. A composite member passes it to its primary child.
- `check (all, n, &first)`: the adjacency check: how many components of `all` (the leaves, from `collectLeaves`) are not members, not parts of a member and overlap the union. 0 means the layout is correct.

## Touch and knob behaviour
The group itself takes no touch. The page makes a group active when a touch goes down on one of its members, and then calls `QGroup::turn`.

## Q-Link
This is the Q-Link object. Members 1 to 4 = Q-Link 1 to 4 of the active group; a page with several groups publishes them in reading order (see [PAGES.md](PAGES.md)).

## Options and variants
`frameless` for apart members; `setFrameRect`; `setBlock` and `setWideDevice` for idle chips.

## Minimal example
```cpp
Knob a ("A"), b ("B");
QGroup g;
g.set (0, &a);
g.set (1, &b);
g.setActive (true);                      // frame + chips 1, 2 + Q colour
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
- Chips read 1 to 4 in reading order, left to right and top to bottom.
- Do not put a boxed frame on a member: the group draws the one frame.
- A mapped group that is not the active one shows chips 5 to 16 on the 16-knob device only when `setBlock (1..3)` was called.
