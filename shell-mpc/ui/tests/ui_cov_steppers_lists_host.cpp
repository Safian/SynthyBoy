// SynthyBoy - MPC UI library test coverage: steppers and lists
// Tests untested public behaviors of Stepper, UpDownStepper, PictogramSelector,
// SourcePicker, EnvSelector, ShapePicker, ToolSelector, and ModRowSelector.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../base.h"
#include "../composite.h"
#include "../env_screen.h"
#include "../lfo_parts.h"
#include "../shape_editor.h"
#include "../mod_matrix.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_stepper_basic_and_clamping()
{
    Stepper s;
    s.setId (101);
    s.place ({ 100, 100, 208, 56 });
    s.state.count = 4;
    s.state.index = 0;
    s.state.wrap = false;
    s.setItem ("SAW");
    s.setCaption ("WAVE");

    check (s.minSize().w == 208 && s.minSize().h == 56, "Stepper minSize is 208x56");
    s.minWidth = 150;
    check (s.minSize().w == 150, "Stepper minSize respects custom minWidth");
    s.minWidth = Stepper::kMinW;

    Rect targets[4];
    int nt = s.touchTargets (targets, 4);
    check (nt == 2, "Stepper reports 2 touch targets");
    check (targets[0].w == 104 && targets[1].w == 104, "Stepper targets divide width equally");

    // Clamped at 0: turning down does nothing
    Change c = s.turn (-1.0);
    check (c.kind == ChangeKind::None, "Stepper turn down at 0 does nothing (no wrap)");

    // Turn up
    c = s.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "Stepper turn up moves to 1");
    s.state.index = 1;

    // Turn multiple steps
    c = s.turn (2.0);
    check (c.kind == ChangeKind::Index && c.index == 3, "Stepper turn +2 moves to 3");
    s.state.index = 3;

    // Clamped at max: turning up does nothing
    c = s.turn (1.0);
    check (c.kind == ChangeKind::None, "Stepper turn up at max does nothing (no wrap)");

    // Touch left half: steps down
    c = s.touchDown (120, 120, 1.0);
    check (c.kind == ChangeKind::None, "Stepper touchDown returns None");
    c = s.touchUp (120, 120);
    check (c.kind == ChangeKind::Index && c.index == 2, "Stepper left tap steps down to 2");
    s.state.index = 2;

    // Touch right half: steps up
    s.touchDown (250, 120, 1.0);
    c = s.touchUp (250, 120);
    check (c.kind == ChangeKind::Index && c.index == 3, "Stepper right tap steps up to 3");
    s.state.index = 3;

    // Drag away cancels tap
    s.touchDown (120, 120, 1.0);
    s.touchMove (300, 120);
    c = s.touchUp (300, 120);
    check (c.kind == ChangeKind::None, "Stepper drag away cancels tap");

    // Single item stepper: turn does nothing
    s.state.count = 1;
    s.state.index = 0;
    c = s.turn (1.0);
    check (c.kind == ChangeKind::None, "Stepper with count <= 1 ignores turn");

    // Drawing within bounds
    s.state.count = 4;
    s.state.index = 1;
    Image im = drawComponent (s, 400, 300);
    check (drawnOutside (im, s.rect(), backgroundPixel()) == 0, "Stepper draws strictly inside rect");
}

static void test_stepper_wrap_mode()
{
    Stepper s;
    s.setId (102);
    s.place ({ 100, 100, 208, 56 });
    s.state.count = 3;
    s.state.index = 0;
    s.state.wrap = true;

    // Wrap backward from 0 -> 2
    Change c = s.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 2, "Stepper wrap backward from 0 gives count-1");

    // Tap left half at 0 wraps to 2
    s.touchDown (120, 120, 1.0);
    c = s.touchUp (120, 120);
    check (c.kind == ChangeKind::Index && c.index == 2, "Stepper left tap at 0 wraps to 2");

    s.state.index = 2;
    // Wrap forward from 2 -> 0
    c = s.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 0, "Stepper wrap forward from count-1 gives 0");

    // Tap right half at 2 wraps to 0
    s.touchDown (250, 120, 1.0);
    c = s.touchUp (250, 120);
    check (c.kind == ChangeKind::Index && c.index == 0, "Stepper right tap at count-1 wraps to 0");
}

static void test_stepper_listable_center_tap()
{
    Stepper s;
    s.setId (103);
    s.place ({ 100, 100, 208, 56 });
    s.state.count = 6;
    s.state.index = 2;
    s.listable = true;

    // Center zone: [rect.x + rect.w/4, rect.right() - rect.w/4) = [100 + 52, 308 - 52) = [152, 256)
    // Tap at x = 200 (center) -> Request change
    s.touchDown (200, 120, 1.0);
    Change c = s.touchUp (200, 120);
    check (c.kind == ChangeKind::Request && c.id == 103, "Stepper listable center tap yields Request");

    // Outer left quarter: x = 120 (< 152) -> steps down
    s.touchDown (120, 120, 1.0);
    c = s.touchUp (120, 120);
    check (c.kind == ChangeKind::Index && c.index == 1, "Stepper listable left quarter steps down");

    // Outer right quarter: x = 280 (>= 256) -> steps up
    s.touchDown (280, 120, 1.0);
    c = s.touchUp (280, 120);
    check (c.kind == ChangeKind::Index && c.index == 3, "Stepper listable right quarter steps up");
}

static void test_up_down_stepper()
{
    UpDownStepper uds;
    uds.setId (104);
    uds.place ({ 100, 100, 80, 120 });
    uds.state.min = -12;
    uds.state.max = 12;
    uds.state.value = 0;
    uds.setCaption ("SEMI");

    check (uds.minSize().w == 72 && uds.minSize().h == 112, "UpDownStepper minSize is 72x112");

    Rect targets[4];
    int nt = uds.touchTargets (targets, 4);
    check (nt == 2, "UpDownStepper reports 2 touch targets");
    check (targets[0].y == 100 && targets[0].h == 60, "Upper target occupies top half");
    check (targets[1].y == 160 && targets[1].h == 60, "Lower target occupies bottom half");

    // Upper tap: +1
    uds.touchDown (140, 120, 1.0);
    Change c = uds.touchUp (140, 120);
    check (c.kind == ChangeKind::Index && c.index == 1, "UpDownStepper upper tap steps +1");
    uds.state.value = 1;

    // Lower tap: -1
    uds.touchDown (140, 180, 1.0);
    c = uds.touchUp (140, 180);
    check (c.kind == ChangeKind::Index && c.index == 0, "UpDownStepper lower tap steps -1");
    uds.state.value = 0;

    // Turning by delta
    c = uds.turn (5.4);
    check (c.kind == ChangeKind::Index && c.index == 5, "UpDownStepper turn +5.4 steps +5");
    uds.state.value = 5;

    // Clamping at max
    uds.state.value = 12;
    c = uds.turn (1.0);
    check (c.kind == ChangeKind::None, "UpDownStepper turn at max does nothing");
    uds.touchDown (140, 120, 1.0);
    c = uds.touchUp (140, 120);
    check (c.kind == ChangeKind::None, "UpDownStepper upper tap at max does nothing");

    // Clamping at min
    uds.state.value = -12;
    c = uds.turn (-1.0);
    check (c.kind == ChangeKind::None, "UpDownStepper turn at min does nothing");
    uds.touchDown (140, 180, 1.0);
    c = uds.touchUp (140, 180);
    check (c.kind == ChangeKind::None, "UpDownStepper lower tap at min does nothing");

    // Inverted bounds min > max (handles gracefully)
    uds.state.min = 10;
    uds.state.max = 0;
    uds.state.value = 5;
    c = uds.turn (10.0);
    check (c.kind == ChangeKind::Index && c.index == 10, "UpDownStepper inverted bounds clamped to 10");

    Image im = drawComponent (uds, 300, 300);
    check (drawnOutside (im, uds.rect(), backgroundPixel()) == 0, "UpDownStepper draws strictly inside rect");
}

static void test_pictogram_selector_horizontal_and_disabled()
{
    PictogramSelector ps;
    ps.setId (105);
    ps.place ({ 50, 50, 320, 56 });
    ps.setItem (0, 0, "SIN", true);
    ps.setItem (1, 0, "TRI", true);
    ps.setItem (2, 0, "SAW", false);  // disabled!
    ps.setItem (3, 0, "SQR", true);

    check (! ps.vertical(), "PictogramSelector w > h is horizontal");
    check (ps.count() == 4, "PictogramSelector count is 4");

    Rect targets[6];
    int nt = ps.touchTargets (targets, 6);
    check (nt == 3, "PictogramSelector omits disabled items from touchTargets");

    // Tap cell 1 (enabled)
    Rect c1 = ps.cell (1);
    ps.touchDown (c1.x + 10, c1.y + 10, 1.0);
    Change c = ps.touchUp (c1.x + 10, c1.y + 10);
    check (c.kind == ChangeKind::Index && c.index == 1, "PictogramSelector tap cell 1 selects 1");
    ps.state.index = 1;

    // Tap cell 2 (disabled) -> no change
    Rect c2 = ps.cell (2);
    ps.touchDown (c2.x + 10, c2.y + 10, 1.0);
    c = ps.touchUp (c2.x + 10, c2.y + 10);
    check (c.kind == ChangeKind::None, "PictogramSelector tap disabled cell does nothing");

    // Tap cell 1 again (already selected) -> no change
    ps.touchDown (c1.x + 10, c1.y + 10, 1.0);
    c = ps.touchUp (c1.x + 10, c1.y + 10);
    check (c.kind == ChangeKind::None, "PictogramSelector tap selected cell does nothing");

    // Turn forward from 1: skips disabled item 2 to item 3
    c = ps.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 3, "PictogramSelector turn forward skips disabled item 2");
    ps.state.index = 3;

    // Turn at end -> no change
    c = ps.turn (1.0);
    check (c.kind == ChangeKind::None, "PictogramSelector turn at end does nothing");

    // Turn backward from 3: skips disabled item 2 to item 1
    c = ps.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "PictogramSelector turn backward skips disabled item 2");

    Image im = drawComponent (ps, 420, 200);
    check (drawnOutside (im, ps.rect(), backgroundPixel()) == 0, "PictogramSelector draws strictly inside rect");
}

static void test_pictogram_selector_vertical()
{
    PictogramSelector ps;
    ps.setId (106);
    ps.place ({ 50, 50, 70, 240 });
    ps.setItem (0, 0, "V1");
    ps.setItem (1, 0, "V2");
    ps.setItem (2, 0, "V3");

    check (ps.vertical(), "PictogramSelector h > w is vertical");
    check (ps.minSize().h == 3 * PictogramSelector::kCellH, "Vertical minSize height is 3 * kCellH");

    Rect c2 = ps.cell (2);
    check (c2.y > ps.cell (1).y, "Vertical cell 2 is below cell 1");
    check (c2.w == ps.rect().w, "Vertical cell width matches rect width");

    ps.touchDown (c2.x + 5, c2.y + 5, 1.0);
    Change c = ps.touchUp (c2.x + 5, c2.y + 5);
    check (c.kind == ChangeKind::Index && c.index == 2, "Vertical tap on cell 2 selects 2");
}

static void test_source_picker()
{
    SourcePicker sp;
    sp.place ({ 50, 50, 280, 56 });

    check (sp.minSize().w == Stepper::kMinW + kGap + 56, "SourcePicker minSize width");
    check (sp.minSize().h == 56, "SourcePicker minSize height");

    check (sp.rect().contains (sp.stepper.rect()), "SourcePicker stepper is inside bounds");
    check (sp.rect().contains (sp.dropdown.rect()), "SourcePicker dropdown is inside bounds");
    check (sp.stepper.rect().right() < sp.dropdown.rect().x, "SourcePicker stepper and dropdown do not overlap");

    Image im = drawComponent (sp, 400, 200);
    check (drawnOutside (im, sp.rect(), backgroundPixel()) == 0, "SourcePicker draws strictly inside rect");
}

static void test_env_selector()
{
    EnvSelector es;
    es.setId (107);
    es.place ({ 50, 50, 360, 56 });

    check (es.count() == 6, "EnvSelector count is 6");
    es.setTargetMask (0b000101); // ENV 1 and ENV 3 have targets
    check (es.targetMask() == 0b000101, "EnvSelector targetMask getter");
    check (es.hasTargets (0), "EnvSelector ENV 1 has targets");
    check (! es.hasTargets (1), "EnvSelector ENV 2 has no targets");
    check (es.hasTargets (2), "EnvSelector ENV 3 has targets");
    check (! es.hasTargets (6), "EnvSelector out-of-range index has no targets");

    check (es.minSize().w >= 56 * 6, "EnvSelector minSize width accommodates 6 envelopes");

    Image im = drawComponent (es, 500, 200);
    check (drawnOutside (im, es.rect(), backgroundPixel()) == 0, "EnvSelector draws strictly inside rect");
}

static void test_shape_picker()
{
    ShapePicker spk;
    spk.setId (108);
    spk.place ({ 50, 50, 240, 240 });

    check (spk.count() == lfo::kNumShapes, "ShapePicker default count is 13 (lfo::kNumShapes)");
    check (spk.rows() == 4, "ShapePicker 13 cells in 4 cols is 4 rows");
    check (spk.minSize().w == 4 * 56 && spk.minSize().h == 4 * 56, "ShapePicker minSize is 224x224");

    Rect targets[20];
    int nt = spk.touchTargets (targets, 20);
    check (nt == lfo::kNumShapes, "ShapePicker touchTargets returns 13 rects");

    // Tap cell 5
    Rect c5 = spk.cell (5);
    spk.touchDown (c5.x + 5, c5.y + 5, 1.0);
    Change c = spk.touchUp (c5.x + 5, c5.y + 5);
    check (c.kind == ChangeKind::Index && c.index == 5, "ShapePicker tap cell 5 selects 5");
    spk.state.index = 5;

    // Turn +1
    c = spk.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 6, "ShapePicker turn +1 moves to 6");

    // Styles mode
    spk.state.styles = true;
    check (spk.count() == 5, "ShapePicker styles mode count is 5");
    check (spk.rows() == 2, "ShapePicker 5 items in 4 cols is 2 rows");

    Image im = drawComponent (spk, 350, 350);
    check (drawnOutside (im, spk.rect(), backgroundPixel()) == 0, "ShapePicker draws strictly inside rect");
}

static void test_tool_selector()
{
    ToolSelector ts;
    ts.setId (109);
    ts.place ({ 50, 50, 288, 56 });

    check (ts.minSize().w == 4 * 72 && ts.minSize().h == 56, "ToolSelector minSize is 288x56");
    check (std::strcmp (ToolSelector::name (0), "POINT") == 0, "Tool 0 is POINT");
    check (std::strcmp (ToolSelector::name (1), "FLAT") == 0, "Tool 1 is FLAT");
    check (std::strcmp (ToolSelector::name (2), "RAMP+") == 0, "Tool 2 is RAMP+");
    check (std::strcmp (ToolSelector::name (3), "RAMP-") == 0, "Tool 3 is RAMP-");

    // Tap tool 2
    Rect c2 = ts.cell (2);
    ts.touchDown (c2.x + 5, c2.y + 5, 1.0);
    Change c = ts.touchUp (c2.x + 5, c2.y + 5);
    check (c.kind == ChangeKind::Index && c.index == 2, "ToolSelector tap cell 2 selects RAMP+");
    ts.state.index = 2;

    // Turn down
    c = ts.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "ToolSelector turn down moves to FLAT");

    Image im = drawComponent (ts, 400, 200);
    check (drawnOutside (im, ts.rect(), backgroundPixel()) == 0, "ToolSelector draws strictly inside rect");
}

static void test_mod_row_selector()
{
    ModRowSelector mrs;
    mrs.setId (110);
    mrs.place ({ 50, 50, 50, 50 });
    mrs.maxRows = 8;
    mrs.selected = 0;

    check (mrs.minSize().w == 56 && mrs.minSize().h == 56, "ModRowSelector minSize is 56x56 (a touch target)");

    // Turn up advances selection
    Change c = mrs.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 1 && mrs.selected == 1, "ModRowSelector turn up advances to 1");

    // Turn down retreats selection
    c = mrs.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 0 && mrs.selected == 0, "ModRowSelector turn down retreats to 0");

    // Zero delta does nothing
    c = mrs.turn (0.0);
    check (c.kind == ChangeKind::None, "ModRowSelector turn 0 does nothing");

    // When maxRows <= 0, turn does nothing
    mrs.maxRows = 0;
    c = mrs.turn (1.0);
    check (c.kind == ChangeKind::None, "ModRowSelector with maxRows 0 does nothing");

    Image im = drawComponent (mrs, 200, 200);
    check (drawnOutside (im, mrs.rect(), backgroundPixel()) == 0, "ModRowSelector draws strictly inside rect");
}

int main()
{
    test_stepper_basic_and_clamping();
    test_stepper_wrap_mode();
    test_stepper_listable_center_tap();
    test_up_down_stepper();
    test_pictogram_selector_horizontal_and_disabled();
    test_pictogram_selector_vertical();
    test_source_picker();
    test_env_selector();
    test_shape_picker();
    test_tool_selector();
    test_mod_row_selector();

    std::printf ("ui_cov_steppers_lists_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
