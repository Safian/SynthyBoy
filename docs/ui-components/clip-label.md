# ClipLabel

## What it is
A compact rounded badge indicator displaying the text `CLIP` next to or above audio level meters, reminiscent of hardware mixer clip LEDs. It indicates when the engine's always-on master safety knee acts (magnitude exceeding 1.8 entering the master limiting stage).

When active, it lights up in bright alert red (`theme::L().negative`) with crisp white/light text. When inactive, it is rendered in a subtle dim style (`theme::L().panel1` background, `theme::L().keyline` outline, `theme::L().textDim` text) so that its presence and position remain clearly visible on the dark panel.

It incorporates a `ClipHold` state machine that holds any hit for 2000 ms (2.0 seconds). Consecutive hits within the hold window re-arm the timer for an additional 2000 ms from the latest hit. A direct finger tap on the label immediately clears the hold.

## Header and types
- `shell-mpc/ui/clip_hold.h`: `ClipHold`
- `shell-mpc/ui/clip_label.h`: `ClipLabel : public Component`

## State & API
`ClipHold`:
- `static constexpr double kHoldDurationMs = 2000.0;`
- `void update(bool hit, double nowMs) noexcept`: updates hold timer with hit flag and current time in milliseconds.
- `bool active(double nowMs) const noexcept`: returns whether the hold timer is currently active.
- `void clear() noexcept`: immediately resets hold state to inactive.

`ClipLabel`:
- `State state`: public field with `state.active` (`bool`).
- `ClipHold hold`: public embedded hold state tracker.
- `void update(bool hit, double nowMs) noexcept`: updates `hold` and syncs `state.active`.
- `bool active(double nowMs) const noexcept`: queries active status.
- `void clear() noexcept`: resets hold and `state.active`.
- `Rect badgeRect() const noexcept`: returns the centered badge rectangle (50 x 20 px) inside the component's touch bounds.

## Size
- `minSize()`: 56 x 56 px.
- Follows the library's 56 px touch target rule (`AGENTS.md`). While the visual badge itself is compact (50 x 20 px with `theme::L().rChip` rounded corners), the enclosing touch bounding box is at least 56 x 56 px so that physical finger taps reliably register without miss-touches.

## Touch behaviour
- Responds to touch events (`touchDown`, `touchMove`, `touchUp`).
- A tap on the label immediately clears the hold state (`clear()`) and emits `Change::indexed(id_, 0, 0.0)`.
- Re-arming: subsequent hits after a touch clear will light the indicator again.

## Minimal example
```cpp
#include "shell-mpc/ui/clip_label.h"

synthyboy_mpc::ui::ClipLabel clip;
clip.setId(105);
clip.place({ 14, 220, 100, 56 });

// In display update tick:
bool engineClipped = inst.takeClipFlag();
clip.update(engineClipped, nowMs);

// Drawing:
clip.draw(sceneBuilder);

// Touch routing:
Change c = clip.touchDown(x, y, nowMs);
if (c.kind == ChangeKind::Index && c.id == 105) {
    // Touch handled, hold cleared
}
```

## Used on
- Master page (`MasterScreen::clip`, `shell-mpc/ui/voice_master.h` and `shell-mpc/overlay/master_ui.h`) placed directly above the output peak meters (`meters`).
- Documented in `docs/MPC.md`.
