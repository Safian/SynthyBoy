// SynthyBoy - MPC UI library test coverage: text and layout helpers
// Tests untested public behaviors of Rect math, layoutRow fixed and weighted
// distribution, setText bounds and safety, TapGesture slop and timings,
// ModInput depth arc math, tab state/rect, and Pen text fitting.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui_test.h"
#include "../core.h"
#include "../composite.h"

using namespace uit;
using namespace synthyboy_mpc::ui;

static void test_rect_math_and_operations()
{
    Rect a { 10, 20, 100, 50 };
    check (a.right() == 110, "Rect right edge is x + w");
    check (a.bottom() == 70, "Rect bottom edge is y + h");
    check (! a.empty(), "Rect with positive w and h is not empty");

    Rect emptyRect { 0, 0, 0, 50 };
    check (emptyRect.empty(), "Rect with w <= 0 is empty");

    // contains (px, py) - exclusive of right and bottom
    check (a.contains (10, 20), "Rect contains top-left pixel");
    check (a.contains (109, 69), "Rect contains bottom-right interior pixel");
    check (! a.contains (110, 50), "Rect does not contain pixel on right edge");
    check (! a.contains (50, 70), "Rect does not contain pixel on bottom edge");
    check (! a.contains (9, 20), "Rect does not contain pixel to the left");

    // contains (Rect)
    Rect inside { 20, 30, 50, 20 };
    check (a.contains (inside), "Rect contains inner rect");
    Rect outside { 5, 20, 50, 20 };
    check (! a.contains (outside), "Rect does not contain overflowing rect");

    // overlaps
    Rect overlapping { 50, 40, 100, 100 };
    check (a.overlaps (overlapping), "Overlapping rects detected");
    Rect touching { 110, 20, 50, 50 };
    check (! a.overlaps (touching), "Touching edge does not count as overlapping");
    check (! a.overlaps (emptyRect), "Empty rect never overlaps");

    // inset
    Rect ins = a.inset (5);
    check (ins.x == 15 && ins.y == 25 && ins.w == 90 && ins.h == 40, "Rect inset shrinks by 2*d");
    Rect bigIns = a.inset (100);
    check (bigIns.empty(), "Large inset clamps w and h to 0");

    // unite
    Rect b { 50, 30, 100, 60 };
    Rect u = Rect::unite (a, b);
    check (u.x == 10 && u.y == 20 && u.w == 140 && u.h == 70, "Rect unite computes correct bounding box");
    check (Rect::unite (a, emptyRect) == a, "Unite with empty rect returns non-empty");
    check (Rect::unite (emptyRect, emptyRect).empty(), "Unite of two empty rects is empty");
}

static void test_layout_row_math()
{
    Rect row { 100, 50, 500, 40 };

    // Case A: purely weighted [1, 2, 1] with gap 10
    // free = 500 - 2 * 10 = 480. Weights: 1/4 = 120, 2/4 = 240, 1/4 = 120
    const int fixedA[3] = { 0, 0, 0 };
    const int weightA[3] = { 1, 2, 1 };
    Rect outA[3];
    layoutRow (row, fixedA, weightA, 3, 10, outA);

    check (outA[0].x == 100 && outA[0].w == 120 && outA[0].h == 40, "Row A item 0 width is 120");
    check (outA[1].x == 230 && outA[1].w == 240 && outA[1].h == 40, "Row A item 1 width is 240");
    check (outA[2].x == 480 && outA[2].w == 120 && outA[2].h == 40, "Row A item 2 width is 120");
    check (outA[2].right() == row.right(), "Row A covers exactly the row width");

    // Case B: mixed fixed and weighted [fixed 100, weighted 1, fixed 50] with gap 5
    // free = 500 - 150 - 2 * 5 = 340
    const int fixedB[3] = { 100, 0, 50 };
    const int weightB[3] = { 0, 1, 0 };
    Rect outB[3];
    layoutRow (row, fixedB, weightB, 3, 5, outB);

    check (outB[0].x == 100 && outB[0].w == 100, "Row B item 0 fixed width 100");
    check (outB[1].x == 205 && outB[1].w == 340, "Row B item 1 weighted width 340");
    check (outB[2].x == 550 && outB[2].w == 50, "Row B item 2 fixed width 50");
    check (outB[2].right() == row.right(), "Row B covers exactly the row width");

    // Case C: overflow handling (fixed exceeds row width)
    Rect narrow { 0, 0, 100, 40 };
    const int fixedC[2] = { 80, 80 };
    const int weightC[2] = { 0, 0 };
    Rect outC[2];
    layoutRow (narrow, fixedC, weightC, 2, 10, outC);
    check (outC[0].w == 80, "Overflow row item 0 gets its fixed width");
    check (outC[1].w <= 10, "Overflow row item 1 is clamped to remaining row space");
}

static void test_set_text_safety_and_bounds()
{
    char buf[16];

    // Normal copy
    setText (buf, sizeof buf, "HELLO");
    check (std::strcmp (buf, "HELLO") == 0, "setText normal copy");

    // Null source sets empty string
    setText (buf, sizeof buf, nullptr);
    check (buf[0] == '\0', "setText null source produces empty string");

    // Capacity 1 writes null terminator only
    buf[0] = 'X';
    setText (buf, 1, "ABC");
    check (buf[0] == '\0', "setText cap 1 writes null terminator only");

    // Truncation at cap - 1
    char small[6];
    setText (small, sizeof small, "0123456789");
    check (std::strlen (small) == 5, "setText truncates to cap - 1 characters");
    check (std::strcmp (small, "01234") == 0, "setText truncated string is zero terminated");

    // Exact fit (5 characters into capacity 6)
    setText (small, sizeof small, "ABCDE");
    check (std::strcmp (small, "ABCDE") == 0, "setText exact fit copy");
}

static void test_tap_gesture_slop_and_double_tap()
{
    TapGesture tg;

    // 1. Move within slop (<= 6 pixels) does not count as move
    tg.down (100, 100, 1000.0);
    tg.move (103, 105); // dx = 3, dy = 5 <= 6
    check (! tg.moved(), "Move within 6px slop is not marked as moved");

    // Move outside slop (> 6 pixels)
    tg.move (107, 100); // dx = 7 > 6
    check (tg.moved(), "Move beyond 6px slop is marked as moved");
    check (tg.up() == 0, "Moved gesture up() reports drag (0)");

    // 2. Single tap
    tg.down (100, 100, 2000.0);
    tg.move (102, 102);
    check (tg.up() == 1, "Static gesture up() reports single tap (1)");

    // 3. Double tap within 400ms and 30px
    tg.down (105, 105, 2200.0); // 200ms later, 5px away
    check (tg.up() == 2, "Second tap within 400ms and 30px reports double tap (2)");

    // 4. Subsequent tap after double tap reports single tap
    tg.down (105, 105, 2300.0);
    check (tg.up() == 1, "Subsequent tap reports single tap (pair was consumed)");

    // 5. Timeout (> 400ms) prevents double tap
    tg.down (100, 100, 3000.0);
    tg.up();
    tg.down (100, 100, 3500.0); // 500ms later > 400ms
    check (tg.up() == 1, "Tap after timeout reports single tap (not double)");

    // 6. Distance too large (> 30px) prevents double tap
    tg.down (100, 100, 4000.0);
    tg.up();
    tg.down (150, 100, 4100.0); // 50px away > 30px
    check (tg.up() == 1, "Tap outside distance threshold reports single tap");

    // 7. Reset clears state
    tg.down (100, 100, 5000.0);
    tg.up();
    tg.reset();
    tg.down (100, 100, 5100.0);
    check (tg.up() == 1, "Tap after reset reports single tap");
}

static void test_mod_input_and_depth_arc()
{
    ModInput mi;
    check (tabState (mi) == TabState::Absent, "Empty ModInput tabState is Absent");
    check (std::strcmp (tabName (mi), "") == 0, "Empty ModInput tabName is empty string");

    mi.add (1, "LFO1", 0.3, false, false);
    mi.add (2, "ENV1", -0.4, false, false);
    mi.add (3, "VEL", 0.5, true, false);   // bidirectional
    mi.add (4, "MACRO", 0.2, false, true); // bypassed

    check (mi.numMods == 4, "ModInput has 4 entries");

    // Nothing selected
    check (tabState (mi) == TabState::Grey, "Unselected ModInput tabState is Grey");
    check (std::strcmp (tabName (mi), "LFO1") == 0, "Unselected tabName shows first entry");

    double from = 0.0, to = 0.0;
    check (! depthArc (mi, 0.5, from, to), "depthArc returns false when no source is selected");

    // Select source 1: positive unidirectional depth (+0.3) at v = 0.5
    mi.selectedSource = 1;
    check (tabState (mi) == TabState::Coloured, "Selected active source tabState is Coloured");
    check (std::strcmp (tabName (mi), "LFO1") == 0, "Selected tabName shows source name");
    check (depthArc (mi, 0.5, from, to), "depthArc returns true for active source");
    check (std::fabs (from - 0.5) < 1.0e-6 && std::fabs (to - 0.8) < 1.0e-6, "Positive unidirectional arc [0.5, 0.8]");

    // Select source 2: negative unidirectional depth (-0.4) at v = 0.5
    mi.selectedSource = 2;
    check (depthArc (mi, 0.5, from, to), "depthArc returns true for negative depth");
    check (std::fabs (from - 0.1) < 1.0e-6 && std::fabs (to - 0.5) < 1.0e-6, "Negative unidirectional arc [0.1, 0.5]");

    // Select source 3: bidirectional depth (+0.5) at v = 0.5 -> [0.0, 1.0]
    mi.selectedSource = 3;
    check (depthArc (mi, 0.5, from, to), "depthArc returns true for bidirectional source");
    check (std::fabs (from - 0.0) < 1.0e-6 && std::fabs (to - 1.0) < 1.0e-6, "Bidirectional arc [0.0, 1.0]");

    // Select source 4: bypassed source
    mi.selectedSource = 4;
    check (tabState (mi) == TabState::Hollow, "Bypassed source tabState is Hollow");
    check (! depthArc (mi, 0.5, from, to), "depthArc returns false for bypassed source");

    // tabRect bounds
    Rect ctl { 100, 100, 80, 80 };
    Rect tr = tabRect (ctl, "LFO1");
    check (ctl.contains (tr), "tabRect is inside control rect");
    check (tr.h <= 18, "tabRect height is capped at 18px");

    Rect hit = tabHitRect (ctl);
    check (ctl.contains (hit), "tabHitRect is inside control rect");
    check (hit.w == 56 && hit.h == 52, "tabHitRect dimensions 56x52");
}

static void test_pen_text_fitting()
{
    // Short string fits at maxSize
    float s = Pen::fitSize ("A", 100.0f, 50.0f, 20.0f, 8.0f);
    check (s == 20.0f, "Short string fits at maxSize");

    // Constrained width scales size down
    float sNarrow = Pen::fitSize ("VERY LONG PARAMETER NAME", 40.0f, 50.0f, 20.0f, 8.0f);
    check (sNarrow < 20.0f, "Narrow width scales size down");
    check (sNarrow >= 8.0f, "Size does not drop below minSize");

    // Constrained height drops below minSize -> returns 0.0f
    float sTooShort = Pen::fitSize ("A", 100.0f, 5.0f, 20.0f, 8.0f);
    check (sTooShort == 0.0f, "Insufficient height returns 0.0f");
}

int main()
{
    test_rect_math_and_operations();
    test_layout_row_math();
    test_set_text_safety_and_bounds();
    test_tap_gesture_slop_and_double_tap();
    test_mod_input_and_depth_arc();
    test_pen_text_fitting();

    std::printf ("ui_cov_layout_text_host: %d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
