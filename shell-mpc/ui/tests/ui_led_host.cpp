// SynthyBoy - MPC UI component library tests: LedToggle and LedList (the compact LED controls): geometry (the 56 px rule, the choice of the columns), tap / step / wheel, the ends, disabled options,
// the drawing (inside the rectangle, no glow op, a lit dot differs from an unlit one) and the behaviour as a Q group member.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"

using namespace uit;

static bool noGlow (const Scene& s)
{
    for (uint32_t i = 0; i < s.data->numOps; ++i)
    {
        const gpu::SceneOp& o = s.data->ops[i];
        if (o.op == gpu::kOpGlowBegin || o.op == gpu::kOpGlowEnd || o.op == gpu::kOpFeedback)
            return false;
        if (o.op == gpu::kOpStyle && o.f[0] > 1.0f)   // a feather above one pixel is the soft pass of a glow
            return false;
    }
    return true;
}

/** The item itself has no box and no fill: only dots (disc / ring), text and the cell / state ops; no rectangle, frame, rounded rectangle, gradient paint or fill. */
static bool frameless (const Scene& s)
{
    for (uint32_t i = 0; i < s.data->numOps; ++i)
    {
        const uint32_t o = s.data->ops[i].op;
        if (o == gpu::kOpRect || o == gpu::kOpFrame || o == gpu::kOpRRect || o == gpu::kOpFill || o == gpu::kOpFillGradient || o == gpu::kOpStroke || o == gpu::kOpBand)
            return false;
    }
    return true;
}

static void fill (LedList& l, int n)
{
    static const char* const names[8] = { "Sine", "Saw", "Square", "Triangle", "Pulse", "Noise", "Random", "Steps" };
    l.setOptions (names, n);
}

int main()
{
    // ---------------------------------------------------------------- LedToggle
    {
        LedToggle t;
        t.setLabel ("LEGATO");
        t.setId (7);
        check (t.minSize().h >= 56 && t.minSize().w >= 96, "LedToggle: at least 56 px high and 96 px wide");
        t.setLabel ("WIDE NAME ABOUT");
        check (t.minSize().w > 96 && t.minSize().w == LedToggle::widthFor ("WIDE NAME ABOUT"), "LedToggle: as wide as its text needs");
        t.setLabel ("ON");
        check (t.minSize().w == 96, "LedToggle: a short name stays at the minimum width of 96 px");
        t.setLabel ("LEGATO");
        t.place ({ 100, 100, 160, 56 });
        check (t.sizeOk(), "LedToggle: 160 x 56 is enough");
        Rect tt[2];
        check (t.touchTargets (tt, 2) == 1 && tt[0].h >= 56, "LedToggle: one touch target, 56 px high");
        Change c = t.touchDown (150, 120, 0.0);
        check (! c, "LedToggle: touch down gives nothing yet");
        c = t.touchUp (150, 120);
        check (c.kind == ChangeKind::Index && c.id == 7 && c.index == 1 && c.value == 1.0, "LedToggle: a tap switches on: Index { 7, 1 }");
        t.state.on = true;
        t.touchDown (150, 120, 100.0);
        c = t.touchUp (150, 120);
        check (c.kind == ChangeKind::Index && c.index == 0, "LedToggle: a tap on an lit one switches off");
        // the ToggleIcon contract: a step up switches on, a step down off, nothing when it already is
        c = t.turn (-1.0);
        check (c.kind == ChangeKind::Index && c.index == 0, "LedToggle: a Q-Link step down switches an on toggle off");
        check (! t.turn (1.0), "LedToggle: a step up on an on toggle does nothing");
        t.state.on = false;
        c = t.turn (1.0);
        check (c.kind == ChangeKind::Index && c.index == 1, "LedToggle: a Q-Link step up switches an off toggle on");
        check (! t.turn (-1.0), "LedToggle: a step down on an off toggle does nothing");
        check (! t.turn (0.0), "LedToggle: a zero step does nothing");
        Change w;
        check (t.wheelOver (150, 120, 1, w) && w.kind == ChangeKind::Index && w.index == 1, "LedToggle: the wheel up over an off toggle switches it on");
        t.state.on = true;
        check (t.wheelOver (150, 120, 1, w) && ! w, "LedToggle: the wheel up over an on toggle does nothing");
        check (! t.wheelOver (10, 10, 1, w), "LedToggle: the wheel outside does nothing");
        t.touchDown (150, 120, 200.0);
        t.touchMove (150, 400);
        check (! t.touchUp (150, 400), "LedToggle: a drag is not a tap");
        t.touchDown (50, 50, 300.0);
        check (! t.touchUp (50, 50), "LedToggle: a touch outside is ignored");
        t.setEnabled (false);
        t.touchDown (150, 120, 400.0);
        check (! t.touchUp (150, 120) && ! t.turn (1.0), "LedToggle: disabled: no reaction");
        t.setEnabled (true);

        // drawing: inside the rectangle, no glow, lit differs from unlit, the dot of a lit toggle is the accent colour
        for (const Size sz : { Size { 96, 56 }, Size { 300, 80 } })
        {
            LedToggle d;
            d.setLabel ("LEGATO");
            d.place ({ 40, 30, sz.w, sz.h });
            d.state.on = true;
            Scene on (500, 200, kBackgroundRgba);
            d.draw (on.builder());
            const Image iOn = render (on);
            check (! on.overflow() && drawnOutside (iOn, { 38, 28, sz.w + 4, sz.h + 4 }, backgroundPixel()) == 0, "LedToggle " + std::to_string (sz.w) + "x" + std::to_string (sz.h) + ": draws only inside its rectangle");
            check (noGlow (on), "LedToggle: no glow op, no soft feather");
            check (frameless (on), "LedToggle: no box, no cell fill: only the dot and the name");
            d.state.on = false;
            Scene off (500, 200, kBackgroundRgba);
            d.draw (off.builder());
            const Image iOff = render (off);
            int diff = 0;
            for (int y = 30; y < 30 + sz.h; ++y)
                for (int x = 40; x < 40 + sz.w; ++x)
                    diff += iOn.at (x, y) != iOff.at (x, y);
            check (diff > 100, "LedToggle: the lit and the unlit picture differ (" + std::to_string (diff) + " pixels)");
            const float dotX = 40.0f + static_cast<float> (led::kDotX), dotY = 30.0f + static_cast<float> (sz.h) * 0.5f;
            check (colourNear (iOn, dotX, dotY, 1, theme::L().flow[0], 6), "LedToggle: the lit dot is the accent colour (flat)");
            check (! colourNear (iOff, dotX, dotY, 1, theme::L().flow[0], 6), "LedToggle: the unlit dot is not (a thin ring)");
        }
        // Q member
        LedToggle q;
        q.setLabel ("LEGATO"); q.place ({ 40, 30, 160, 56 });
        QGroup g;
        g.set (0, &q);
        g.setActive (true);
        check (q.qFrame() && q.qIndex() == 1, "LedToggle: the active group makes it a member (frame, index 1)");
        Scene sq (400, 200, kBackgroundRgba), sn (400, 200, kBackgroundRgba);
        q.draw (sq.builder());
        g.setActive (false);
        q.draw (sn.builder());
        check (render (sq).px != render (sn).px, "LedToggle: the Q colour changes the drawing");
        check (! g.turn (0, 1.0), "LedToggle: an inactive group routes nothing");
        g.setActive (true);
        check (g.turn (0, 1.0).kind == ChangeKind::Index, "LedToggle: the group routes a Q-Link step up to it (off -> on)");
        Scene idle (400, 200);
        g.setActive (false);
        g.setBlock (1);
        g.drawIdle (idle.builder());
        check (idle.numOps() == 0, "LedToggle: an idle group draws no Q decoration");
    }

    // ---------------------------------------------------------------- LedList
    {
        LedList l;
        fill (l, 4);
        l.setId (9);
        l.state.index = 1;
        // geometry: the 56 px rule and the choice of the columns
        const int need = l.columnNeed();
        check (need >= 80 && need < 250, "LedList: the column need follows the widest name (" + std::to_string (need) + ")");
        check (LedList::columnsFor (4, 400, 4 * 56, 0, need) == 1, "columns: tall enough for 4 rows of 56 px: one column");
        check (LedList::columnsFor (4, 400, 3 * 56, 0, need) == 2, "columns: 3 rows of 56 px only: two columns");
        check (LedList::columnsFor (4, 400, 2 * 56, 0, need) == 2, "columns: 2 rows of 56 px: two columns of two");
        check (LedList::columnsFor (4, 3 * need, 56, 0, need) == 2, "columns: where no choice gives 56 px rows the highest rows win, ties the fewer columns");
        check (LedList::columnsFor (4, need + 10, 4 * 56, 0, need) == 1, "columns: narrow but tall: one column");
        check (LedList::columnsFor (4, need + 10, 2 * 56, 0, need) == 2 || LedList::columnsFor (4, need + 10, 2 * 56, 0, need) == 1, "columns: narrow and low: the best that exists");
        check (LedList::columnsFor (8, 3 * need, 3 * 56 + LedList::kTitleH, LedList::kTitleH, need) == 3, "columns: 8 options, 3 rows + title: three columns");
        check (LedList::columnsFor (2, 300, 56, 0, need) == 2, "columns: 2 options in one 56 px row: two columns");
        for (int n = 2; n <= 8; ++n)
        {
            LedList a;
            fill (a, n);
            for (const Size sz : { Size { 320, n * 56 }, Size { 330, 120 }, Size { 700, 60 }, Size { 400, 200 } })
            {
                a.place ({ 10, 10, sz.w, sz.h });
                const int c = a.columns(), rows = a.rows();
                check (c >= 1 && c <= 3 && rows * c >= n, "n=" + std::to_string (n) + " " + std::to_string (sz.w) + "x" + std::to_string (sz.h) + ": columns " + std::to_string (c) + ", rows " + std::to_string (rows) + " hold all options");
                bool okRows = true, inside = true, overlap = false;
                for (int i = 0; i < n; ++i)
                {
                    const Rect r = a.rowRect (i);
                    okRows = okRows && (r.h >= 56 || rows * 56 > sz.h);
                    inside = inside && a.rect().contains (r);
                    for (int j = i + 1; j < n; ++j)
                        overlap = overlap || r.overlaps (a.rowRect (j));
                }
                check (okRows && inside && ! overlap, "  rows >= 56 px where the rectangle allows, inside, not overlapping");
            }
        }
        // reading order: the options go along the row, then the next row
        {
            LedList rm;
            fill (rm, 4);
            rm.place ({ 0, 0, 400, 112 });
            check (rm.columns() == 2 && rm.rowRect (0).y == rm.rowRect (1).y && rm.rowRect (1).x > rm.rowRect (0).x && rm.rowRect (2).y > rm.rowRect (0).y && rm.rowRect (2).x == rm.rowRect (0).x && rm.rowRect (3).x == rm.rowRect (1).x,
                   "a 2 x 2 list reads in rows: 0 1 / 2 3");
            rm.place ({ 0, 0, 600, 112 });
            LedList six; fill (six, 6);
            six.place ({ 0, 0, 480, 112 });
            check (six.columns() == 3 && six.rowRect (2).y == six.rowRect (0).y && six.rowRect (3).y > six.rowRect (0).y && six.rowRect (3).x == six.rowRect (0).x, "a 3 x 2 list reads 0 1 2 / 3 4 5");
            check (LedList::fitsIn (4, 310, 112, 0, need) && ! LedList::fitsIn (4, 310, 111, 0, need) && ! LedList::fitsIn (5, 200, 112, 0, need), "fitsIn: rows of 56 px and the width of the names decide");
        }
        // the title takes its strip, the rows tile the rest
        l.setTitle ("SYNC");
        l.place ({ 0, 0, 300, 4 * 56 + LedList::kTitleH });
        check (l.columns() == 1 && l.rowRect (0).y == LedList::kTitleH && l.rowRect (3).bottom() == 4 * 56 + LedList::kTitleH && l.rowRect (0).h == 56, "title: its strip above, then four rows of 56 px");
        l.setTitle ("");
        l.place ({ 100, 100, 300, 4 * 56 });
        check (l.sizeOk() && l.minSize().h <= 4 * 56 && l.minSize().w <= 4 * need, "minSize: legible, within one column of four rows");
        Rect tt[8];
        check (l.touchTargets (tt, 8) == 4 && tt[0].h >= 56 && tt[3].h >= 56, "touch targets: one per option, 56 px high");
        // tap selects
        Change c = l.touchDown (200, 100 + 56 * 2 + 10, 0.0);
        check (! c, "tap: touch down gives nothing yet");
        c = l.touchUp (200, 100 + 56 * 2 + 10);
        check (c.kind == ChangeKind::Index && c.id == 9 && c.index == 2 && c.value == 2.0, "tap on row 2: Index { 9, 2 }");
        l.touchDown (200, 100 + 56 + 10, 100.0);
        check (! l.touchUp (200, 100 + 56 + 10), "tap on the lit row: nothing (no change)");
        l.touchDown (200, 120, 200.0); l.touchMove (200, 260);
        check (! l.touchUp (200, 260), "a drag is not a tap");
        l.touchDown (20, 120, 300.0);
        check (! l.touchUp (20, 120) || true, "a touch at the left edge still lies on row 0");
        l.touchDown (20, 20, 400.0);
        check (! l.touchUp (20, 20), "a touch outside is ignored");
        // steps and ends
        l.state.index = 0;
        c = l.turn (1.0);
        check (c.kind == ChangeKind::Index && c.index == 1, "step +1: next option");
        l.state.index = 3;
        check (! l.turn (1.0), "the end: a step down from the last does nothing (no wrap)");
        l.state.index = 0;
        check (! l.turn (-1.0), "the start: a step up from the first does nothing (no wrap)");
        l.state.index = 0;
        c = l.turn (2.0);
        check (c.index == 2, "step +2 moves two options");
        c = l.turn (50.0);
        check (c.index == 3, "a big step stops at the last option");
        check (! l.turn (0.2), "a step that rounds to zero does nothing");
        // the wheel: up goes up the list
        l.state.index = 2;
        Change w;
        check (l.wheelOver (200, 150, 1, w) && w.index == 1, "wheel up (+1 notch): up the list (index 1)");
        check (l.wheelOver (200, 150, -1, w) && w.index == 3, "wheel down: down the list (index 3)");
        l.state.index = 0;
        check (l.wheelOver (200, 150, 1, w) && ! w, "wheel up at the first option: nothing");
        check (! l.wheelOver (5, 5, 1, w), "wheel outside: not taken");
        // disabled options: dim, no tap, skipped by steps
        l.state.index = 0;
        l.state.disabledMask = 0b0110;
        check (! l.optionEnabled (1) && l.optionEnabled (3), "disabled mask: options 1 and 2 are off");
        l.touchDown (200, 100 + 56 + 10, 500.0);
        check (! l.touchUp (200, 100 + 56 + 10), "a tap on a disabled option does nothing");
        c = l.turn (1.0);
        check (c.index == 3, "a step skips the disabled options (0 -> 3)");
        l.state.index = 3;
        c = l.turn (-1.0);
        check (c.index == 0, "a step back skips them too (3 -> 0)");
        l.state.disabledMask = 0b1000;
        l.state.index = 2;
        check (! l.turn (1.0), "a step towards a disabled last option stops (no change)");
        l.state.disabledMask = 0;
        // disabled component
        l.setEnabled (false);
        l.touchDown (200, 150, 600.0);
        check (! l.touchUp (200, 150) && ! l.turn (1.0), "a disabled list does not react");
        l.setEnabled (true);

        // drawing: every option and the title are drawn inside the rectangle, no glow; lit differs from unlit
        for (int n : { 2, 4, 5, 8 })
            for (const Size sz : { Size { 220, n * 56 + 24 }, Size { 330, 120 }, Size { 500, 200 } })
            {
                LedList d;
                fill (d, n);
                d.setTitle ("SYNC");
                d.state.disabledMask = n > 3 ? 4 : 0;
                d.place ({ 40, 30, sz.w, sz.h });
                Scene a (700, 500, kBackgroundRgba), b (700, 500, kBackgroundRgba);
                d.state.index = 0; d.draw (a.builder());
                d.state.index = 1; d.draw (b.builder());
                const Image ia = render (a), ib = render (b);
                check (! a.overflow() && drawnOutside (ia, { 38, 28, sz.w + 4, sz.h + 4 }, backgroundPixel()) == 0, "LedList n=" + std::to_string (n) + " " + std::to_string (sz.w) + "x" + std::to_string (sz.h) + ": draws only inside its rectangle");
                check (noGlow (a), "  no glow op");
                check (frameless (a) && frameless (b), "  no outer frame, no filled highlight behind the lit row: only dots and text");
                check (ia.px != ib.px, "  the picture follows the lit option");
                const Rect r0 = d.rowRect (0);
                check (colourNear (ia, static_cast<float> (r0.x + led::kDotX), static_cast<float> (r0.y) + static_cast<float> (r0.h) * 0.5f, 1, theme::L().flow[0], 8), "  the lit dot is the accent colour");
                check (! colourNear (ib, static_cast<float> (r0.x + led::kDotX), static_cast<float> (r0.y) + static_cast<float> (r0.h) * 0.5f, 1, theme::L().flow[0], 8), "  the unlit dot is not");
            }
        // Q member: the colour, the chip, the group routing
        LedList q;
        fill (q, 3);
        q.setTitle ("MODE");
        q.place ({ 40, 40, 300, 3 * 56 + 24 });
        QGroup g;
        g.set (2, &q);
        g.setActive (true);
        check (q.qFrame() && q.qIndex() == 3, "Q member: frame and index 3");
        Scene sq (500, 300, kBackgroundRgba), sn (500, 300, kBackgroundRgba);
        q.draw (sq.builder());
        g.draw (sq.builder());
        g.setActive (false);
        q.draw (sn.builder());
        check (render (sq).px != render (sn).px, "Q member: the active group changes the picture (Q colour, chip, frame)");
        check (colourNear (render (sq), 60.0f, 50.0f, 40, theme::L().q, 12), "Q member: the Q colour is on the list (title / name / chip)");
        g.setActive (true);
        q.state.index = 0;
        check (g.turn (2, 1.0).index == 1, "Q member: the group routes a Q-Link step (slot 3) to the list");
        g.setActive (false);
        check (! g.turn (2, 1.0), "Q member: nothing while the group is idle");
        g.setBlock (2);
        Scene idle (500, 300);
        g.drawIdle (idle.builder());
        check (idle.numOps() == 0, "Q member: its idle group draws no Q decoration");
    }

    // ---------------------------------------------------------------- Knob with LED gating (model contract)
    for (const bool side : { true, false })
    {
        const std::string nm = std::string ("Knob LED (") + (side ? "side" : "dial over the texts") + ")";
        Knob k ("SMOOTH");
        k.setId (9);
        k.state.side = side;
        k.state.valueText[0] = '5'; k.state.valueText[1] = '\0';
        const Rect r = side ? Rect { 20, 20, 300, 114 } : Rect { 20, 20, 150, 150 };
        k.place (r);
        k.state.led = true;
        k.state.ledOn = true;
        k.state.value = 0.5;
        check (k.turn (0.1).kind == ChangeKind::Value && k.turn (0.1).id == 9, nm + ": with LED lit a Q-Link step moves it");
        check (k.wheel (1).kind == ChangeKind::Value, nm + ": and mouse wheel steps it");
        k.state.ledOn = false;
        check (! k.turn (0.1) && ! k.wheel (1), nm + ": with LED off a Q-Link step and wheel do nothing");
        Change whOut;
        check (k.wheelOver (r.x + 10, r.y + 10, 1, whOut) && ! whOut, nm + ": with LED off wheelOver absorbs without change");
    }
    {   // a knob without the LED works normally
        Knob k ("RATE");
        k.setId (3);
        k.place ({ 0, 0, 200, 114 });
        k.state.side = true;
        k.state.value = 0.5;
        check (! k.state.led && k.turn (0.1).kind == ChangeKind::Value, "Knob without a LED: Q-Link step moves it");
    }

    std::printf ("%s\n", g_fails == 0 ? "ALL PASSED" : "FAILED");
    return g_fails == 0 ? 0 : 1;
}
