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

static void test_up_down_stepper_compact()
{
    UpDownStepper uds;
    uds.compact = true;
    uds.setCaption ("VOICES");
    uds.setId (104);
    const Size size = uds.minSize();
    check (size.w == 192 && size.h == 48, "Compact stepper is 48 px high");
    uds.place ({ 20, 20, size.w, size.h });
    uds.state.min = 1;
    uds.state.max = 16;

    Rect targets[2];
    check (uds.touchTargets (targets, 2) == 2, "Compact stepper reports both arrow targets");
    const Rect down = targets[0];
    const Rect up = targets[1];
    check (down.w == 56 && up.w == 56 && down.h == 48 && up.h == 48
           && down.right() == up.x && up.right() == uds.rect().right(),
           "Compact arrows have separate fixed targets on the right");

    auto tap = [&uds] (int x, int y)
    {
        uds.touchDown (x, y, 1.0);
        return uds.touchUp (x, y);
    };
    const int midY = down.y + down.h / 2;
    const int downX = down.x + down.w / 2;
    const int upX = up.x + up.w / 2;
    const auto at = [&uds] (int value)
    {
        uds.state.value = value;
        return drawComponent (uds, 240, 86);
    };
    const auto differentPixels = [] (const Image& first, const Image& second, Rect r)
    {
        int changed = 0;
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x)
                changed += first.at (x, y) != second.at (x, y);
        return changed;
    };

    const Image minimum = at (1);
    check (tap (downX, midY).kind == ChangeKind::None, "Hidden down arrow does nothing at 1");
    check (tap (upX, midY).index == 2, "Visible up arrow steps to 2");
    const Image middle = at (7);
    check (tap (downX, midY).index == 6 && tap (upX, midY).index == 8, "Both arrows step at 7");
    check (tap (uds.rect().x + 12, midY).kind == ChangeKind::None, "Caption is outside the arrow targets");
    const Image maximum = at (16);
    check (tap (upX, midY).kind == ChangeKind::None, "Hidden up arrow does nothing at 16");
    check (tap (downX, midY).index == 15, "Visible down arrow steps to 15");
    check (uds.turn (1).kind == ChangeKind::None && uds.turn (-1).index == 15,
           "Encoder turning keeps its previous clamped behavior");
    check (differentPixels (minimum, middle, down) > 0 && differentPixels (minimum, middle, up) == 0,
           "Only down arrow disappears at minimum");
    check (differentPixels (maximum, middle, up) > 0 && differentPixels (maximum, middle, down) == 0,
           "Only up arrow disappears at maximum");
    check (drawnOutside (middle, uds.rect(), backgroundPixel()) == 0, "Compact stepper draws inside its bounds");
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
    ts.place ({ 50, 50, 504, 56 });

    check (ts.minSize().w == 7 * 72 && ts.minSize().h == 56, "ToolSelector minSize is 504x56 (seven tools)");
    check (std::strcmp (ToolSelector::name (0), "POINT") == 0, "Tool 0 is POINT");
    check (std::strcmp (ToolSelector::name (1), "FLAT") == 0, "Tool 1 is FLAT");
    check (std::strcmp (ToolSelector::name (2), "RAMP+") == 0, "Tool 2 is RAMP+");
    check (std::strcmp (ToolSelector::name (3), "RAMP-") == 0, "Tool 3 is RAMP-");
    check (std::strcmp (ToolSelector::name (4), "SELECT") == 0 && std::strcmp (ToolSelector::name (5), "SOFT") == 0 && std::strcmp (ToolSelector::name (6), "BEND") == 0, "Tools 4, 5, 6 are SELECT, SOFT, BEND");

    // Tap tool 2
    Rect c2 = ts.cell (2);
    ts.touchDown (c2.x + 5, c2.y + 5, 1.0);
    Change c = ts.touchUp (c2.x + 5, c2.y + 5);
    check (c.kind == ChangeKind::Index && c.index == 2, "ToolSelector tap cell 2 selects RAMP+");
    ts.state.index = 2;

    // Turn down
    c = ts.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "ToolSelector turn down moves to FLAT");

    Image im = drawComponent (ts, 640, 200);
    check (drawnOutside (im, ts.rect(), backgroundPixel()) == 0, "ToolSelector draws strictly inside rect");
}

static void test_mod_row_selector()
{
    ModRowSelector mrs;
    mrs.setId (110);
    mrs.place ({ 50, 50, 50, 50 });
    mrs.state.maxRows = 8;
    mrs.state.selected = 0;

    check (mrs.minSize().w == 56 && mrs.minSize().h == 56, "ModRowSelector minSize is 56x56 (a touch target)");

    // Turn up reports the next row; the selector stores nothing: with no owner writing back `state` stays and the same turn reports the same row, the picture is the same
    mrs.state.text[0] = '0'; mrs.state.text[1] = '1'; mrs.state.text[2] = '\0';
    const Image before = drawComponent (mrs, 200, 200);
    Change c = mrs.turn (1.0);
    check (c.kind == ChangeKind::Index && c.id == 110 && c.index == 1 && mrs.state.selected == 0, "ModRowSelector turn up reports row 1 and leaves its state alone (the owner selects)");
    c = mrs.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "ModRowSelector: with no write back the same turn reports the same row (no step is kept in the component)");
    check (drawComponent (mrs, 200, 200).px == before.px, "ModRowSelector: the picture does not change by a turn");

    // The owner sets the row; a turn down retreats from it
    mrs.state.selected = 1;
    c = mrs.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 0 && mrs.state.selected == 1, "ModRowSelector turn down reports row 0 from the row the owner set");

    // Zero delta does nothing
    c = mrs.turn (0.0);
    check (c.kind == ChangeKind::None, "ModRowSelector turn 0 does nothing");

    // When maxRows <= 0, turn does nothing
    mrs.state.maxRows = 0;
    c = mrs.turn (1.0);
    check (c.kind == ChangeKind::None, "ModRowSelector with maxRows 0 does nothing");

    Image im = drawComponent (mrs, 200, 200);
    check (drawnOutside (im, mrs.rect(), backgroundPixel()) == 0, "ModRowSelector draws strictly inside rect");
}

// Regression (library finding 7 and 8 of the first coverage round). 7: a pictogram selector whose index is out of range (the options shrank) turns from the nearest valid cell: a step down from index 5 of 3
// cells is a step down from cell 2, to cell 1 (the first demonstration expected 2: a step that does not move); a step up has nowhere to go and reports nothing. 8: the row selector reports a change only
// when the row changed, at the first and the last row a further step is silent.
static void test_pictogram_out_of_range_turn_and_row_selector_boundaries()
{
    PictogramSelector ps;
    ps.setId (70);
    ps.place ({ 50, 50, 240, 56 });
    ps.setItem (0, 0, "A");
    ps.setItem (1, 0, "B");
    ps.setItem (2, 0, "C");
    ps.state.index = 5;
    Change c = ps.turn (1.0);
    check (! c, "pictogram selector: a step up from an index past the last cell has nowhere to go");
    c = ps.turn (-1.0);
    check (c.kind == ChangeKind::Index && c.index == 1, "pictogram selector: a step down from an index past the last cell goes from the last cell, to the one before it");
    ps.state.index = 1;
    c = ps.turn (1.0);
    check (c.kind == ChangeKind::Index && c.index == 2, "pictogram selector: in range a step up is the next cell");

    ModRowSelector mrs;
    mrs.setId (80);
    mrs.state.maxRows = 8;
    mrs.state.selected = 0;
    check (! mrs.turn (-1.0) && mrs.state.selected == 0, "row selector: a step down at the first row reports nothing");
    mrs.state.selected = 7;
    check (! mrs.turn (1.0) && mrs.state.selected == 7, "row selector: a step up at the last row reports nothing");
    mrs.state.selected = 3;
    Change d = mrs.turn (1.0);
    check (d.kind == ChangeKind::Index && d.index == 4 && mrs.state.selected == 3, "row selector: a step inside the rows reports the new row, the component keeps the row the owner set");
    check (! mrs.turn (0.0), "row selector: no step, no change");
}


/** A content name in a stepper (`contentName`): the middle goes when it does not fit, ONCE. The text is drawn at the size the middle cut was made for; drawn at a larger maximum the pen cut the end of the cut text again ("drum…take…"). Every width, with and
    without a caption, long names, names that just fit and non-ASCII names: at most one ellipsis, never at the end, the start and the end of the name stay, and the text fits the zone at the size it is drawn at. */
static void test_stepper_content_name_is_cut_once()
{
    static const char kEll[] = "\xE2\x80\xA6";
    auto ellipses = [&] (const std::string& t, size_t& last)
    {
        int n = 0;
        for (size_t at = t.find (kEll); at != std::string::npos; at = t.find (kEll, at + 3))
        {
            ++n;
            last = at;
        }
        return n;
    };
    const char* const names[] = { "voice sweep aei long name final take 2", "Harmonic Series", "A fairly long wavetable title that does not fit", "short", "very long name with \xC3\xA4\xC3\xB6\xC3\xBC accents inside it", "Warm Evolving Pad Texture Long", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" };
    int checked = 0, cut = 0, bad = 0;
    for (int w : { 208, 240, 300, 420 })
        for (bool caption : { false, true })
            for (const char* name : names)
            {
                Stepper s;
                s.contentName = true;
                s.state.count = 3;
                s.place ({ 20, 20, w, 56 });
                s.setItem (name);
                if (caption)
                    s.setCaption ("TABLE");
                Scene sc (500, 100, kBackgroundRgba);
                s.draw (sc.builder());
                const int zoneW = w - 4 - 2 * 28;   // the stepper draws in its rectangle inset by 2, the arrows take 28 px at each side
                bool found = false;
                for (uint32_t k = 0; k < sc.data->numOps; ++k)
                {
                    if (sc.data->ops[k].op != gpu::kOpText)
                        continue;
                    const std::string t = gpu::textString (*sc.data, sc.data->ops[k]);
                    if (caption && t == "TABLE")
                        continue;
                    found = true;
                    ++checked;
                    size_t last = 0;
                    const int n = ellipses (t, last);
                    const std::string whole = name;
                    const bool wholeShown = t == whole;
                    cut += ! wholeShown;
                    const bool start = wholeShown || (n == 1 && t.compare (0, 2, whole, 0, 2) == 0), end = wholeShown || (n == 1 && last + 3 < t.size() && whole.compare (whole.size() - 2, 2, t, t.size() - 2, 2) == 0);
                    if (n > 1 || (n == 1 && last + 3 >= t.size()) || ! start || ! end || sdf::textWidth (t.c_str(), sc.data->ops[k].f[2]) > static_cast<float> (zoneW) + 0.5f)
                    {
                        ++bad;
                        check (false, std::string ("content name '") + name + "' at width " + std::to_string (w) + (caption ? " with caption" : "") + " is drawn as '" + t + "' (" + std::to_string (n) + " ellipses)");
                    }
                }
                check (found, std::string ("content name '") + name + "' is drawn");
            }
    check (checked > 40 && cut > 10 && bad == 0, "a content name too wide for its stepper loses its middle once (" + std::to_string (cut) + " of " + std::to_string (checked) + " drawn names were cut): one ellipsis, never at the end, the start and the end of the name stay, the text fits at the size it is drawn at");
}

int main()
{
    test_stepper_basic_and_clamping();
    test_stepper_content_name_is_cut_once();
    test_stepper_wrap_mode();
    test_stepper_listable_center_tap();
    test_up_down_stepper();
    test_up_down_stepper_compact();
    test_pictogram_selector_horizontal_and_disabled();
    test_pictogram_selector_vertical();
    test_source_picker();
    test_env_selector();
    test_shape_picker();
    test_tool_selector();
    test_mod_row_selector();

    test_pictogram_out_of_range_turn_and_row_selector_boundaries();
    std::printf ("ui_cov_steppers_lists_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
