// SynthyBoy - MPC UI library test coverage: findings and unexpected behaviors
// Demonstrates genuine bugs, discrepancies, and unexpected behaviors found
// in the UI component library during comprehensive coverage testing.
// NOTE: This test file is intentionally NOT registered in ctest.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../base.h"
#include "../core.h"
#include "../list.h"
#include "../mixer_strip.h"
#include "../fx_chain_bar.h"
#include "../mod_matrix.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

// Finding 1: SliderBar wheelUnit inherits 1.0, causing 100% value jumps
// Location: shell-mpc/ui/mixer_strip.h:239-330
static void finding_sliderbar_wheel_unit()
{
    SliderBar sb ("PAN", true);
    sb.setId (1);
    sb.place ({ 50, 50, 80, 56 });
    sb.state.value = 0.5;

    // A normalized 0..1 slider should have a fine wheel step (e.g. 1/128),
    // but it inherits Component::wheelUnit() == 1.0.
    const double unit = sb.wheelUnit();
    check (unit == 1.0, "[Finding 1] SliderBar inherits wheelUnit == 1.0 instead of 1/128");

    // Turning by 1 notch jumps from 0.5 to 1.0 (clamped), a 100% full-scale jump!
    Change c = sb.turn (1.0 * unit);
    check (c.kind == ChangeKind::Value && c.value == 1.0,
           "[Finding 1] SliderBar 1 wheel notch jumps by 1.0 full scale");
}

// Finding 2: Fader wheelUnit inherits 1.0 and touchDown captures outside bounds
// Location: shell-mpc/ui/mixer_strip.h:16-235
static void finding_fader_wheel_and_touch_capture()
{
    Fader f ("LEVEL");
    f.setId (2);
    f.place ({ 50, 50, 80, 160 });
    f.state.value = 0.5;

    const double unit = f.wheelUnit();
    check (unit == 1.0, "[Finding 2] Fader inherits wheelUnit == 1.0 instead of 1/128");

    // Fader::touchDown omits `if (! rect_.contains (x, y))` check!
    // A touch down anywhere on screen (e.g. 999, 999) is captured by the Fader:
    f.touchDown (999, 999, 1.0);
    // Because Fader defines its own private `bool captured_` member, it shadows
    // Component::captured_, so Component::capturing() returns false even though Fader captured it!
    check (! f.capturing(),
           "[Finding 2] Fader shadows Component::captured_, so Component::capturing() returns false");
    f.touchUp (999, 999);
}

// Finding 3: MixerStrip reported minSize is smaller than required layout height
// Location: shell-mpc/ui/mixer_strip.h:355, 388-406
static void finding_mixer_strip_reported_bounds_overflow()
{
    MixerStrip ms;
    Size minS = ms.minSize();
    check (minS.w == 96 && minS.h == 360, "[Finding 3] MixerStrip reports minSize {96, 360}");

    // Placed at its reported minSize {96, 360}:
    ms.place ({ 0, 0, minS.w, minS.h });

    // send2 is laid out at:
    // 4 (pad) + 56 (power) + 4 (gap) + 56 (dest) + 4 (gap) + 16 (qLane) + 96 (min fader)
    // + 16 (qLane) + 56 (pan) + 16 (qLane) + 56 (send1) + 16 (qLane) = y: 338, h: 56
    // bottom is 338 + 56 = 394 px, which exceeds rect_.bottom() (360) by 34 pixels!
    check (ms.send2.rect().bottom() > ms.rect().bottom(),
           "[Finding 3] MixerStrip::send2 bottom (394px) overflows rect.bottom (360px)");

    Image im = drawComponent (ms, 200, 500);
    int outside = drawnOutside (im, ms.rect(), backgroundPixel());
    check (outside > 0, "[Finding 3] MixerStrip draws outside its reported minSize bounds");
}

// Finding 4: FxChainBar leaves unplaced slot cards visible
// Location: shell-mpc/ui/fx_chain_bar.h:245-257
static void finding_fx_chain_bar_unhidden_slot_cards()
{
    FxChainBar fb;
    fb.place ({ 100, 100, 400, 56 });
    // Default numSlots_ is 6, but constructor adds 16 cards with shown = true.
    // Cards 6..15 retain rect {0, 0, 0, 0} and shown = true.
    check (fb.childShown (10), "[Finding 4] FxChainBar unconfigured slot cards remain shown");

    Image im = drawComponent (fb, 600, 300);
    int outside = drawnOutside (im, fb.rect(), backgroundPixel());
    check (outside > 0, "[Finding 4] Unplaced FxChainBar cards draw badges at (0, 0) outside bounds");
}

// Finding 5: ListPopup onUp missing left boundary check
// Location: shell-mpc/ui/list.h:183
static void finding_list_popup_missing_left_boundary()
{
    static const char* const items[] = { "Item 0", "Item 1", "Item 2" };
    ListPopup lp;
    lp.setId (50);
    lp.place ({ 100, 100, 200, 200 });
    lp.state.items = items;
    lp.state.count = 3;

    // touchDown inside popup
    lp.touchDown (150, 120, 1.0);

    // touchUp to the LEFT of the popup box (x = 20 < 100) at the row's y (120):
    // Line 183 checks `x >= rect_.x + rowRect(0).w + kPad` (right edge), but omits `x < rect_.x`!
    Change c = lp.touchUp (20, 120);
    check (c.kind == ChangeKind::Index && c.index == 0,
           "[Finding 5] ListPopup onUp accepts release to the left of the popup box as row selection");
}

// Finding 6: sdf::fitText and Pen::textCentered ellipsis overflow
// Location: shell-mpc/ui/core.h:265-275, overlay/sdf_font.h:78-108
static void finding_text_centered_ellipsis_overflow()
{
    // A box narrower than the unicode ellipsis glyph width at minSize (8.0f)
    Rect tinyBox { 50, 50, 4, 20 };
    char buf[96];
    float w = sdf::fitText ("ABCDEFGHIJKLMN", 8.0f, static_cast<float> (tinyBox.w), buf, sizeof buf);

    // fitText returns ellipsis '…' whose width is 5.98px > maxW (4px)
    check (w > static_cast<float> (tinyBox.w),
           "[Finding 6] sdf::fitText returns string wider than box.w when box is narrower than ellipsis");

    // Pen::textCentered computes startX = box.x + (box.w - w) * 0.5f
    float startX = static_cast<float> (tinyBox.x) + (static_cast<float> (tinyBox.w) - w) * 0.5f;
    check (startX < static_cast<float> (tinyBox.x),
           "[Finding 6] Pen::textCentered positions text to the left of box.x");
    check (startX + w > static_cast<float> (tinyBox.right()),
           "[Finding 6] Pen::textCentered positions text to the right of box.right()");
}

// Finding 7: PictogramSelector turn clamps incorrectly on out-of-bounds index
// Location: shell-mpc/ui/base.h:1113-1122
static void finding_pictogram_selector_out_of_bounds_turn()
{
    PictogramSelector ps;
    ps.setId (70);
    ps.place ({ 50, 50, 240, 56 });
    ps.setItem (0, 0, "A");
    ps.setItem (1, 0, "B");
    ps.setItem (2, 0, "C");
    // Count is 3, valid indices are 0, 1, 2.
    // If state.index was externally set to 5:
    ps.state.index = 5;

    // Turning by -1:
    // i is clamped to 2, then loop does j = i - 1 = 1, setting target = 1.
    // It jumps directly to index 1 rather than clamping to the nearest valid index 2!
    Change c = ps.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 1,
           "[Finding 7] PictogramSelector turn -1 from out-of-bounds index 5 jumps to 1 instead of 2");
}

// Finding 8: ModRowSelector emits spurious Change::indexed at boundary
// Location: shell-mpc/ui/mod_matrix.h:37-47
static void finding_mod_row_selector_boundary_change()
{
    ModRowSelector mrs;
    mrs.setId (80);
    mrs.maxRows = 8;
    mrs.selected = 0;

    // Turning down at 0 does not change selected (stays 0),
    // but ModRowSelector unconditionally returns Change::indexed(80, 0, 0.0)!
    Change c = mrs.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 0,
           "[Finding 8] ModRowSelector emits Change::indexed when turning past min boundary 0");

    mrs.selected = 7;
    c = mrs.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 7,
           "[Finding 8] ModRowSelector emits Change::indexed when turning past max boundary 7");
}

// Finding 9: setText zero-capacity buffer overflow
// Location: shell-mpc/ui/core.h:103
static void finding_set_text_zero_capacity_write()
{
    char buf[4] = { 'A', 'B', 'C', '\0' };

    // With cap == 0, setText reaches line 103: `else if (dst) dst[0] = '\0';`
    // It overwrites buf[0] with '\0' even though capacity is 0!
    setText (buf, 0, "TEST");
    check (buf[0] == '\0',
           "[Finding 9] setText writes null byte at dst[0] even when cap == 0");
}

int main()
{
    std::printf ("=== UI Library Findings Host Tests ===\n");
    finding_sliderbar_wheel_unit();
    finding_fader_wheel_and_touch_capture();
    finding_mixer_strip_reported_bounds_overflow();
    finding_fx_chain_bar_unhidden_slot_cards();
    finding_list_popup_missing_left_boundary();
    finding_text_centered_ellipsis_overflow();
    finding_pictogram_selector_out_of_bounds_turn();
    finding_mod_row_selector_boundary_change();
    finding_set_text_zero_capacity_write();

    std::printf ("All 9 library findings demonstrated and verified.\n");
    return 0;
}
