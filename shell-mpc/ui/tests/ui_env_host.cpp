// SynthyBoy - MPC UI component library tests: the envelope element: the geometry and the touch of the EnvGraph (the handles, the hit test, the closed form of the curve drag, what a drag gives back), the two layouts
// (EnvCompact, EnvFullScreen: every part inside the rectangle, touch targets, the Q groups adjacent) and the targets page (ModTargetsPage).
// Build: c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -o ui_env_host shell-mpc/ui/tests/ui_env_host.cpp
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../env_screen.h"
#include "../mod_targets.h"
#include "../play_rows.h"

using namespace uit;

namespace
{
struct Touch
{
    double t = 0.0;
    Change down (Component& c, int x, int y) { t += 50.0; return c.touchDown (x, y, t); }
    Change move (Component& c, int x, int y) { t += 16.0; return c.touchMove (x, y); }
    Change up (Component& c, int x, int y) { t += 16.0; return c.touchUp (x, y); }
};

void testShapeMath()
{
    using namespace envgraph;
    check (std::fabs (powerShape (0.5, 0.0) - 0.5) < 1.0e-12 && std::fabs (powerShape (1.0, 7.0) - 1.0) < 1.0e-12 && std::fabs (powerShape (0.0, -9.0)) < 1.0e-12, "the curve function: straight at 0, 0 -> 0 and 1 -> 1 for every power");
    bool ok = true;
    for (double p = -20.0; p <= 20.0; p += 0.5)
        if (std::fabs (p) > 0.2 && std::fabs (p) < 19.0)
            ok = ok && std::fabs (curveForMiddle (powerShape (0.5, p)) - p) < 1.0e-6;
    check (ok, "curveForMiddle is the inverse of the middle of powerShape (closed form, -19 .. 19)");
    Shape s;
    s.sustain = 0.4;
    check (levelAt (s, 0, 0.5) == 0.0 && levelAt (s, 2, 0.3) == 1.0 && levelAt (s, 4, 0.3) == 0.4, "delay 0, hold 1, sustain at the sustain level");
    check (std::fabs (levelAt (s, 3, 1.0) - 0.4) < 1.0e-12 && std::fabs (levelAt (s, 1, 1.0) - 1.0) < 1.0e-12 && std::fabs (levelAt (s, 5, 1.0)) < 1.0e-12, "attack ends at 1, decay at the sustain level, release at 0");
    s.curve[0] = 8.0;   // a positive attack curve: the engine runs powerShape (x, -8): fast start, the middle is above the straight line
    check (levelAt (s, 1, 0.5) > 0.5, "a positive attack curve starts fast (the middle above the line)");
}

void testHandlesAndHitTest()
{
    EnvGraph g;
    g.place ({ 20, 20, 800, 340 });
    g.state.shape.t[envgraph::Hold] = 0.2;
    g.state.shape.t[envgraph::Attack] = 0.5;   // an attack with room for its curve handle (a stage under kCurveRoom pixels has none)
    double px[envgraph::kNumHandles], py[envgraph::kNumHandles];
    const EnvGraph::Geom geo = g.geom();
    bool inside = true;
    for (int h = 0; h < envgraph::kNumHandles; ++h)
    {
        g.handlePoint (h, px[h], py[h]);
        inside = inside && g.rect().contains (static_cast<int> (px[h]), static_cast<int> (py[h]));
        check (g.handleAt (static_cast<int> (std::lround (px[h])), static_cast<int> (std::lround (py[h]))) == h, "a touch on the centre of handle " + std::to_string (h) + " takes that handle");
    }
    check (inside, "every handle is inside the graph's rectangle");
    check (std::fabs (px[envgraph::Delay] - geo.x[1]) < 1.0e-9 && std::fabs (py[envgraph::Delay] - geo.yBot) < 1.0e-9, "the delay handle is the end of the delay on the bottom line");
    check (std::fabs (py[envgraph::Attack] - geo.yTop) < 1.0e-9 && std::fabs (py[envgraph::Hold] - geo.yTop) < 1.0e-9 && px[envgraph::Hold] > px[envgraph::Attack] + 10.0, "attack and hold end on the top line, with room between them");
    // with hold = 0, hold collapses to 0 px width
    g.state.shape.t[envgraph::Hold] = 0.0;
    const EnvGraph::Geom geo0 = g.geom();
    check (geo0.x[3] == geo0.x[2], "hold 0 ms collapses to 0 px width");
    Rect t[8];
    check (g.touchTargets (t, 8) == 8 && t[0].w >= 56 && t[0].h >= 56, "eight touch targets of at least 56 x 56");
    check (g.handleAt (g.rect().x + 2, g.rect().y + 2) == envgraph::None, "a touch far from every handle takes none");
}

void testDragFollowsTheFinger()
{
    EnvGraph g;
    g.place ({ 20, 20, 800, 340 });
    g.state.shape.t[1] = 0.35;
    g.state.defaults = g.state.shape;
    Touch tc;
    double hx, hy;
    g.handlePoint (envgraph::Attack, hx, hy);
    const int x0 = static_cast<int> (hx), y0 = static_cast<int> (std::lround (hy));   // left of the joint of attack and the zero-length hold: the attack (a touch right of it takes the hold)
    check (! tc.down (g, x0, y0), "touch down alone gives no change");
    Change c;
    for (int i = 1; i <= 12; ++i)
    {
        c = tc.move (g, x0 + 5 * i, y0);
        if (c)
        {
            const EnvGraph::Edit& e = g.edit();
            g.state.shape.t[e.handle] = e.t;
        }
    }
    g.handlePoint (envgraph::Attack, hx, hy);
    check (std::fabs (hx - (x0 + 60)) < 2.5, "the attack handle is under the finger after a drag of 60 px (" + std::to_string (hx) + " vs " + std::to_string (x0 + 60) + ")");
    check (g.state.shape.t[1] > 0.35, "and the attack grew (" + std::to_string (g.state.shape.t[1]) + ")");
    tc.up (g, x0 + 60, y0);
    // far to the right: the time stops at its maximum
    tc.down (g, static_cast<int> (hx), static_cast<int> (hy));
    for (int i = 1; i <= 40; ++i)
    {
        c = tc.move (g, static_cast<int> (hx) + 200 * i, static_cast<int> (hy));
        if (c)
            g.state.shape.t[g.edit().handle] = g.edit().t;
    }
    tc.up (g, static_cast<int> (hx) + 8000, static_cast<int> (hy));
    check (g.state.shape.t[1] <= 1.0 && g.state.shape.t[1] > 0.95, "dragged to the far right the time stops at 1 (" + std::to_string (g.state.shape.t[1]) + ")");
    // to the left of its start the time is 0
    g.handlePoint (envgraph::Attack, hx, hy);
    tc.down (g, static_cast<int> (hx), static_cast<int> (hy));
    for (int i = 1; i <= 40; ++i)
    {
        c = tc.move (g, static_cast<int> (hx) - 20 * i, static_cast<int> (hy));
        if (c)
            g.state.shape.t[g.edit().handle] = g.edit().t;
    }
    tc.up (g, static_cast<int> (hx) - 800, static_cast<int> (hy));
    check (g.state.shape.t[1] == 0.0, "dragged far to the left the time is 0");
}

void testSerumEnvelopeGraph()
{
    // B2 requirements:
    // 1. All-zero times: zero-length stages collapsed to 0 px, auto-zoom stepped to 100 ms
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        for (int i = 0; i < 5; ++i)
            g.state.shape.t[i] = 0.0;
        const EnvGraph::Geom geo = g.geom();
        check (geo.window == 1000.0 && geo.locked, "all-zero times: locked, the picture stands for the 1 s an empty envelope shows");
        check (geo.x[1] == geo.x[0] && geo.x[2] == geo.x[1] && geo.x[3] == geo.x[2] && geo.x[4] == geo.x[3],
               "all-zero times: delay, attack, hold, decay all collapsed to 0 px width");
        check (geo.x[6] == geo.x[5], "all-zero times: release collapsed to 0 px width");
        check (geo.x[5] - geo.x[4] == geo.sustainGap, "all-zero times: sustain marker gap is present");

        // Dragging right gives the stage time
        Touch tc;
        tc.down (g, static_cast<int> (geo.x[1]), static_cast<int> (geo.yBot));
        Change c = tc.move (g, static_cast<int> (geo.x[1]) + 50, static_cast<int> (geo.yBot));
        check (c && g.edit().handle == envgraph::Delay && g.edit().t > 0.0,
               "all-zero times: dragging right on delay joint gives delay time");
        tc.up (g, static_cast<int> (geo.x[1]) + 50, static_cast<int> (geo.yBot));
    }

    // 2. Attack 5 ms vs 300 ms
    {
        EnvGraph g5;
        g5.place ({ 20, 20, 800, 340 });
        for (int i = 0; i < 5; ++i) g5.state.shape.t[i] = 0.0;
        g5.state.shape.t[1] = envgraph::normFromTimeMs (1, 5.0);
        const EnvGraph::Geom geo5 = g5.geom();
        const double w5 = geo5.x[2] - geo5.x[1];

        EnvGraph g300;
        g300.place ({ 20, 20, 800, 340 });
        for (int i = 0; i < 5; ++i) g300.state.shape.t[i] = 0.0;
        g300.state.shape.t[1] = envgraph::normFromTimeMs (1, 300.0);
        const EnvGraph::Geom geo300 = g300.geom();
        const double w300 = geo300.x[2] - geo300.x[1];

        check (std::fabs (geo5.window - 5.0) < 1.0e-6, "attack 5 ms: locked, the plot stands for exactly the 5 ms the envelope needs");
        check (std::fabs (geo300.window - 300.0) < 1.0e-6 && std::fabs (geo300.x[6] - (g300.plot().right())) < 1.0e-6, "attack 300 ms: the window is 300 ms and the envelope ends at the right edge of the plot");
        check (w300 >= w5 - 1.0e-6 && std::fabs (w300 - (g300.plot().w - geo300.sustainGap)) < 1.0e-6, "attack 300 ms (" + std::to_string (w300) + " px) takes the whole width, 5 ms (" + std::to_string (w5) + " px) too: locked fit, the ruler tells the difference");
    }

    // 3. Delay 500 ms
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        for (int i = 0; i < 5; ++i) g.state.shape.t[i] = 0.0;
        g.state.shape.t[0] = envgraph::normFromTimeMs (0, 500.0);
        const EnvGraph::Geom geo = g.geom();
        const double wDelay = geo.x[1] - geo.x[0];
        check (std::fabs (geo.window - 500.0) < 1.0e-6, "delay 500 ms: the window is 500 ms");
        check (wDelay > 50.0, "delay 500 ms takes width (" + std::to_string (wDelay) + " px)");
    }

    // 4. Hold 200 ms
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        for (int i = 0; i < 5; ++i) g.state.shape.t[i] = 0.0;
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 100.0);
        g.state.shape.t[2] = envgraph::normFromTimeMs (2, 200.0);
        const EnvGraph::Geom geo = g.geom();
        const double wHold = geo.x[3] - geo.x[2];
        check (std::fabs (geo.window - 300.0) < 1.0e-6, "hold 200 ms + attack 100 ms: the window is 300 ms");
        check (std::fabs (wHold / (geo.x[3] - geo.x[1]) - 2.0 / 3.0) < 1.0e-6, "hold 200 ms next to attack 100 ms takes two thirds of their width (" + std::to_string (wHold) + " px)");
    }

    // 5. Sustain 0 %, 50 %, 100 %
    {
        for (double sLevel : { 0.0, 0.5, 1.0 })
        {
            EnvGraph g;
            g.place ({ 20, 20, 800, 340 });
            g.state.shape.sustain = sLevel;
            const EnvGraph::Geom geo = g.geom();
            double px, py;
            g.handlePoint (envgraph::Decay, px, py);
            const double expectedY = EnvGraph::levelRow (geo, sLevel);
            check (std::fabs (py - expectedY) < 1.0e-9, "sustain " + std::to_string (static_cast<int> (sLevel * 100.0)) + "%: decay ends at level row (" + std::to_string (py) + ")");
            check (geo.x[5] - geo.x[4] == geo.sustainGap, "sustain is a level, not a time (short fixed marker gap of " + std::to_string (geo.sustainGap) + " px)");
        }
    }

    // 6. Release 10 s next to attack 1 ms: short stages stay visible (minimum 6 px for stage > 0)
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        for (int i = 0; i < 5; ++i) g.state.shape.t[i] = 0.0;
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 1.0);
        g.state.shape.t[4] = envgraph::normFromTimeMs (4, 10000.0);
        const EnvGraph::Geom geo = g.geom();
        const double wAttack = geo.x[2] - geo.x[1];
        const double wRelease = geo.x[6] - geo.x[5];
        check (std::fabs (geo.window - 10001.0) < 1.0e-3, "release 10 s next to attack 1 ms: the window is 10.001 s");
        check (wAttack >= envgraph::kMinStageW - 1.0e-9, "attack 1 ms stays visible at the minimum width (" + std::to_string (wAttack) + " px)");
        check (wRelease > 200.0, "release 10 s takes large proportional width (" + std::to_string (wRelease) + " px)");
    }
}


void testLockedFitRulerAndZoom()
{
    // LOCKED fit: the envelope fills the plot whatever its times (the ruler changes), every stage with time keeps its minimum width, the sum is exact
    for (double ms : { 3.0, 80.0, 1500.0, 9000.0 })
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.shape.t[0] = 0.0; g.state.shape.t[2] = 0.0;
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 2.0);   // a very short attack next to ...
        g.state.shape.t[3] = envgraph::normFromTimeMs (3, ms);
        g.state.shape.t[4] = envgraph::normFromTimeMs (4, ms / 3.0);
        const EnvGraph::Geom geo = g.geom();
        check (std::fabs (geo.x[6] - g.plot().right()) < 1.0e-6, "locked, decay " + std::to_string (static_cast<int> (ms)) + " ms: the envelope ends at the right edge of the plot");
        check (geo.x[2] - geo.x[1] >= envgraph::kMinStageW - 1.0e-9, "locked: the 2 ms attack keeps its minimum width");
        check (geo.cu[4] > 0.0 && std::fabs (geo.window - geo.cu[6]) < 1.0e-6, "locked: the ruler's window is the whole time");
        // the handles of the short attack are apart and each takes a touch at its own centre
        double ax, ay, ox, oy;
        g.handlePoint (envgraph::Attack, ax, ay);
        g.handlePoint (envgraph::Delay, ox, oy);
        check (std::hypot (ax - ox, ay - oy) >= envgraph::kMinStageW - 1.0e-9 && g.handleAt (static_cast<int> (std::lround (ax)), static_cast<int> (std::lround (ay))) == envgraph::Attack, "locked: the 2 ms attack's handle is apart from the origin and touchable");
    }
    // the ruler: round marks, time_ruler.h, drawn under the plot (a picture with the labels differs from one without)
    {
        const TimeRuler r = makeTimeRuler (1500.0, 0.0f, 800.0f, 84.0f);
        check (r.count >= 3 && r.stepMs > 0.0 && std::strcmp (r.ticks[0].label, "0") == 0, "time ruler: marks at round times from 0");
    }
    // UNLOCKED: the zoom decides, an envelope longer than the window runs past the right edge, its handles beyond the edge are not offered
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.shape.t[0] = 0.0; g.state.shape.t[2] = 0.0;
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 100.0);
        g.state.shape.t[3] = envgraph::normFromTimeMs (3, 400.0);
        g.state.shape.t[4] = envgraph::normFromTimeMs (4, 2000.0);
        g.state.locked = false;
        g.state.zoom = 1000.0;
        const EnvGraph::Geom geo = g.geom();
        check (std::fabs (geo.pxPerUnit - (g.plot().w - geo.sustainGap) / 1000.0) < 1.0e-9 && geo.x[6] > g.plot().right(), "unlocked: the scale is the zoom (1000 ms over the plot), the release runs past the edge");
        Rect t[8];
        const int n = g.touchTargets (t, 8);
        check (n < 8, "unlocked: the handles past the right edge are not offered (" + std::to_string (n) + " targets)");
        g.state.zoom = 4000.0;
        check (g.touchTargets (t, 8) >= 6, "unlocked and zoomed out: the handles are back");
    }
    // the LOCK button: a tap toggles, unlocking keeps the picture as it is (no jump)
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 300.0);
        g.state.shape.t[3] = envgraph::normFromTimeMs (3, 500.0);
        const EnvGraph::Geom before = g.geom();
        const Rect lk = g.lockRect();
        check (lk.w >= 56 && lk.h >= 34, "the LOCK button is at least 56 wide");
        Touch tc;
        tc.down (g, lk.x + lk.w / 2, lk.y + lk.h / 2);
        const Change c = tc.up (g, lk.x + lk.w / 2, lk.y + lk.h / 2);
        check (c && g.edit().handle == EnvGraph::kLock && ! g.state.locked, "a tap on LOCK unlocks");
        const EnvGraph::Geom after = g.geom();
        bool same = true;
        for (int k = 0; k < 7; ++k)
            same = same && std::fabs (before.x[k] - after.x[k]) < 1.0e-6;
        check (same, "unlocking does not move the picture");
        // zoom by a horizontal drag along the ruler band: right = in, left = out
        const Rect zr = g.zoomRect();
        check (! zr.empty() && zr.h >= 24, "unlocked: there is a zoom band under the plot");
        const double z0 = g.state.zoom;
        Touch tz;
        tz.down (g, zr.x + zr.w / 2, zr.y + zr.h / 2);
        tz.move (g, zr.x + zr.w / 2 + 80, zr.y + zr.h / 2);
        check (g.state.zoom < z0 * 0.7 && g.edit().handle == EnvGraph::kZoom, "a drag right along the ruler zooms in (" + std::to_string (z0) + " -> " + std::to_string (g.state.zoom) + ")");
        tz.up (g, zr.x + zr.w / 2 + 80, zr.y + zr.h / 2);
        Touch ty;
        ty.down (g, zr.x + zr.w / 2, zr.y + zr.h / 2);
        ty.move (g, zr.x + zr.w / 2 - 300, zr.y + zr.h / 2);
        check (g.state.zoom > z0, "a drag left zooms out");
        ty.up (g, zr.x + zr.w / 2 - 300, zr.y + zr.h / 2);
        const Rect lk2 = g.lockRect();
        Touch tl;
        tl.down (g, lk2.x + 10, lk2.y + 10);
        tl.up (g, lk2.x + 10, lk2.y + 10);
        check (g.state.locked, "a second tap on LOCK locks again");
    }
    // locked drag: the handle's end stays under the finger although the picture refits at every step
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.shape.t[0] = 0.0; g.state.shape.t[2] = 0.0;
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 200.0);
        g.state.shape.t[3] = envgraph::normFromTimeMs (3, 600.0);
        g.state.shape.t[4] = envgraph::normFromTimeMs (4, 800.0);
        double hx, hy;
        g.handlePoint (envgraph::Attack, hx, hy);
        const int x0 = static_cast<int> (std::lround (hx)), y0 = static_cast<int> (std::lround (hy));
        Touch tc;
        tc.down (g, x0, y0);
        for (int i = 1; i <= 10; ++i)
            if (tc.move (g, x0 + 8 * i, y0))
                g.state.shape.t[1] = g.edit().t;
        tc.up (g, x0 + 80, y0);
        g.handlePoint (envgraph::Attack, hx, hy);
        check (std::fabs (hx - (x0 + 80)) < 2.0 && g.state.shape.t[1] > envgraph::normFromTimeMs (1, 200.0), "locked drag of the attack handle: it is under the finger after 80 px (" + std::to_string (hx) + " vs " + std::to_string (x0 + 80) + ")");
    }
    // the ghost lies on the axis of the envelope in front: the same time is at the same x
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.shape.t[1] = envgraph::normFromTimeMs (1, 400.0);
        g.state.ghost = true;
        g.state.ghostShape.t[1] = envgraph::normFromTimeMs (1, 200.0);
        Scene sc (900, 400, kBackgroundRgba);
        g.draw (sc.builder());
        check (sc.numOps() > 10, "a graph with a ghost draws");
    }
    // BEATS: the axis is in beats (the ruler says beats)
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.beats = true;
        g.state.shape.t[1] = envgraph::normFromTime (1, 1.0, true);
        g.state.shape.t[3] = envgraph::normFromTime (3, 2.0, true);
        const EnvGraph::Geom geo = g.geom();
        check (std::fabs (envgraph::stageTime (g.state.shape, 1, true) - 1.0) < 1.0e-9 && std::fabs (geo.window - (1.0 + 2.0 + envgraph::stageTime (g.state.shape, 4, true))) < 1.0e-6, "beats: the stages are in beats and the window is their sum");
    }
}

void testDecayIsTwoDimensional()
{
    EnvGraph g;
    g.place ({ 20, 20, 800, 340 });
    g.state.shape.sustain = 0.8;
    Touch tc;
    double hx, hy;
    g.handlePoint (envgraph::Decay, hx, hy);
    const int x0 = static_cast<int> (std::lround (hx)), y0 = static_cast<int> (std::lround (hy));
    tc.down (g, x0, y0);
    const Change c = tc.move (g, x0 + 30, y0 + 80);
    check (c && g.edit().handle == envgraph::Decay && g.edit().t > g.state.shape.t[3] && g.edit().level < 0.8 - 0.1, "a drag of the decay handle right and down: a longer decay and a lower sustain in one Edit");
    tc.up (g, x0 + 30, y0 + 80);
}

void testCurveHandleFollowsTheFinger()
{
    for (int h : { envgraph::CurveA, envgraph::CurveD, envgraph::CurveR })
    {
        EnvGraph g;
        g.place ({ 20, 20, 800, 340 });
        g.state.shape.sustain = 0.5;
        g.state.shape.t[1] = 0.5;
        g.state.shape.curve[0] = 0.0; g.state.shape.curve[1] = 0.0; g.state.shape.curve[2] = 0.0;
        Touch tc;
        double hx, hy;
        g.handlePoint (h, hx, hy);
        const int x0 = static_cast<int> (std::lround (hx)), y0 = static_cast<int> (std::lround (hy));
        tc.down (g, x0, y0);
        for (int i = 1; i <= 8; ++i)
            if (tc.move (g, x0, y0 + 6 * i))
                g.state.shape.curve[h - envgraph::CurveA] = g.edit().curve;
        tc.up (g, x0, y0 + 48);
        double nx, ny;
        g.handlePoint (h, nx, ny);
        check (std::fabs (ny - (y0 + 48)) < 1.5 && std::fabs (g.state.shape.curve[h - envgraph::CurveA]) > 0.5, "curve handle " + std::to_string (h) + ": the handle is at the finger's row (" + std::to_string (ny) + " vs " + std::to_string (y0 + 48) + "), the curve changed");
    }
    // a sustain of 100 % leaves no height for the decay curve: a relative move still bends it
    EnvGraph g;
    g.place ({ 20, 20, 800, 340 });
    g.state.shape.sustain = 1.0;
    Touch tc;
    double hx, hy;
    g.handlePoint (envgraph::CurveD, hx, hy);
    tc.down (g, static_cast<int> (hx), static_cast<int> (hy));
    const Change c = tc.move (g, static_cast<int> (hx), static_cast<int> (hy) - 30);
    check (c && g.edit().curve != g.state.shape.curve[1], "no sustain range: the decay curve still moves (relative drag)");
}

void testDoubleTapResets()
{
    EnvGraph g;
    g.place ({ 20, 20, 800, 340 });
    g.state.defaults.t[4] = 0.25;
    g.state.shape.t[4] = 0.8;
    Touch tc;
    double hx, hy;
    g.handlePoint (envgraph::Release, hx, hy);
    const int x = static_cast<int> (std::lround (hx)), y = static_cast<int> (std::lround (hy));
    tc.down (g, x, y); tc.up (g, x, y);
    tc.down (g, x, y);
    const Change c = tc.up (g, x, y);
    check (c && g.edit().reset && g.edit().handle == envgraph::Release && std::fabs (g.edit().t - 0.25) < 1.0e-12, "a double tap on a handle is the default");
}

void testDrawing()
{
    for (int lvl : { 1, 2 })
    {
        theme::setDetail (lvl);
        EnvGraph g;
        g.place ({ 100, 60, 388, 336 });
        g.state.ghost = true;
        g.state.numDots = 3; g.state.dots[0] = { 4, 0.3f }; g.state.dots[1] = { 6, 0.8f }; g.state.dots[2] = { 5, 0.0f };
        g.state.selected = envgraph::Attack;
        std::snprintf (g.state.text[envgraph::Attack], 16, "250 ms");
        Scene s (600, 460, kBackgroundRgba);
        g.draw (s.builder());
        g.drawLive (s.builder());
        const Image im = render (s);
        check (! s.overflow() && drawnInside (im, g.rect(), backgroundPixel()) > 3000, std::string ("graph, detail ") + std::to_string (lvl) + ": drawn");
        check (drawnOutside (im, { g.rect().x - 3, g.rect().y - 3, g.rect().w + 6, g.rect().h + 6 }, backgroundPixel()) == 0, std::string ("graph, detail ") + std::to_string (lvl) + ": nothing outside the rectangle");
    }
    theme::setDetail (1);
}

bool allInside (const Composite& c)
{
    for (int i = 0; i < c.numChildren(); ++i)
        if (c.childShown (i) && ! c.rect().contains (c.child (i)->rect()))
            return false;
    return true;
}

void testCompactLayout()
{
    EnvCompact c;
    c.place ({ 440, 114, 400, 620 });
    check (allInside (c), "compact: every part is inside the third");
    bool sizes = true;
    for (int i = 0; i < c.numChildren(); ++i)
        sizes = sizes && c.child (i)->sizeOk();
    check (sizes, "compact: every part has at least its minimum size (touch targets of 56 px and more)");
    check (c.graph.rect().h > 280 && c.graph.rect().w >= 380, "compact: the graph is big (" + std::to_string (c.graph.rect().w) + " x " + std::to_string (c.graph.rect().h) + ")");
    check (c.graph.rect().y == playrows::displayRect (c.rect()).y && c.graph.rect().h == playrows::displayRect (c.rect()).h && c.attack.rect().y == playrows::controlsLine (c.rect()), "compact: the graph is the display rectangle shared by the three thirds and the knobs start at the common controls line");
    check (c.instances.rect().h == 56 && c.instances.listable && c.instances.state.count == kNumEnvs && c.instances.rect().y == c.rect().y + 6, "compact: the envelope selector is the one selector of the thirds (a stepper with a list, 56 px, the top row), not six tabs");
    check (c.graph.rect().contains (c.full.rect()) && c.full.rect().w >= 56 && c.full.rect().h >= 56, "compact: the full screen button (56 x 56 or more) lies in the corner of the graph");
    Component* leaves[64];
    const int n = c.collectLeaves (leaves, 0, 64);
    check (c.group.check (leaves, n) == 0 && c.group.numMembers() == 4, "compact: the ADSR group has four members and nothing else lies inside its frame");
    Touch tc;
    const Rect fb = c.full.rect();
    const Change ch = (tc.down (c, fb.x + fb.w / 2, fb.y + fb.h / 2), tc.up (c, fb.x + fb.w / 2, fb.y + fb.h / 2));
    check (ch.kind == ChangeKind::Request && ch.id == c.full.id (), "a tap on the full screen button over the graph is that button's request, not the graph's");
    check (c.instances.targetMask() == 0 && ! c.instances.hasTargets (0), "compact: selector targetMask starts at 0");
    c.instances.setTargetMask (0x05);
    check (c.instances.hasTargets (0) && ! c.instances.hasTargets (1) && c.instances.hasTargets (2), "compact: selector hasTargets reflects mask (ENV 1 and 3)");
    Scene s (1280, 800, kBackgroundRgba);
    c.draw (s.builder());
    const Image im = render (s);
    check (! s.overflow() && drawnOutside (im, { 440 - 4, 114 - 4, 408, 628 }, backgroundPixel()) == 0, "compact: drawn, and nothing outside its rectangle");
}

/** The compact graph (the Play page's third): the header band at the top of the display holds the full screen button; the plot area is the common one (playrows::plotRect), the grid, the curve, the fill and the handles lie in it, in every state (short and
    long times, no times at all, a selected handle with its value bubble, the running dots), so nothing of the curve is drawn under the button. The full screen page's graph has no plot area and keeps its margins. */
void testCompactPlot()
{
    EnvCompact c;
    c.place ({ 440, 114, 400, 620 });
    const Rect disp = playrows::displayRect (c.rect()), area = playrows::plotRect (disp), band = playrows::bandRect (disp);
    check (c.graph.rect() == disp && c.graph.plot() == area && c.graph.plotArea == area, "compact graph: the plot is the common plot area of the three thirds (playrows::plotRect of the display)");
    check (area.y >= band.bottom() && area.bottom() <= disp.bottom() && area.x > disp.x && area.right() < disp.right(), "compact graph: the plot area lies below the header band, inside the display");
    check (c.full.rect() == playrows::fullRect (disp) && c.full.rect().w >= 56 && c.full.rect().h >= 56 && band.contains (c.full.rect()), "compact graph: the full screen button stands in the band (rightmost, 56 px at least)");
    EnvGraph full;
    full.place ({ 0, 110, 1280, 628 });
    check (full.plotArea.empty() && full.plot().y == 110 + 38 && full.plot().x == 24, "full screen graph: no plot area, the margins of the page are unchanged");
    struct Case { const char* name; double ms[5]; double sustain; bool select; bool beats; };
    const Case cases[] = { { "default times", { 0.0, 100.0, 0.0, 400.0, 200.0 }, 1.0, false, false },
                           { "long times", { 2000.0, 6000.0, 3000.0, 10000.0, 10000.0 }, 0.5, true, false },
                           { "no times", { 0.0, 0.0, 0.0, 0.0, 0.0 }, 0.0, false, false },
                           { "peak held to the right edge (hold, no decay, no release)", { 0.0, 5.0, 4000.0, 0.0, 0.0 }, 1.0, true, false },
                           { "beats", { 1.0, 2.0, 1.0, 4.0, 4.0 }, 0.7, true, true } };
    for (const Case& cs : cases)
    {
        c.graph.state.beats = cs.beats;
        for (int i = 0; i < 5; ++i)
        {
            c.graph.state.shape.timeMs[i] = cs.ms[i];
            c.graph.state.shape.t[i] = cs.beats ? envgraph::normFromTime (i, cs.ms[i], true) : envgraph::normFromTimeMs (i, cs.ms[i]);
        }
        c.graph.state.shape.sustain = cs.sustain;
        c.graph.state.ghost = true;
        c.graph.state.ghostShape = EnvGraph::Shape();
        c.graph.state.numDots = 2;
        c.graph.state.dots[0] = { 2, 0.5f };
        c.graph.state.dots[1] = { 4, 0.2f };
        for (int sel : { static_cast<int> (envgraph::None), static_cast<int> (envgraph::Attack), static_cast<int> (envgraph::Hold), static_cast<int> (envgraph::Decay), static_cast<int> (envgraph::CurveA) })
        {
            c.graph.state.selected = cs.select ? sel : static_cast<int> (envgraph::None);
            std::snprintf (c.graph.state.text[envgraph::Attack], sizeof c.graph.state.text[0], "%.0f ms", cs.ms[1]);
            std::snprintf (c.graph.state.text[envgraph::Hold], sizeof c.graph.state.text[0], "%.0f ms", cs.ms[2]);
            std::snprintf (c.graph.state.text[envgraph::Decay], sizeof c.graph.state.text[0], "%.0f ms 50%%", cs.ms[3]);
            std::snprintf (c.graph.state.text[envgraph::CurveA], sizeof c.graph.state.text[0], "-3.0");
            Scene s (1280, 800, kBackgroundRgba);
            c.graph.draw (s.builder());
            c.graph.drawLive (s.builder());
            const std::string nm = std::string ("compact graph, ") + cs.name + (cs.select ? ", handle " + std::to_string (c.graph.state.selected) + " selected" : "") + ": ";
            check (! s.overflow(), nm + "fits one scene");
            check (dataOpsInto (s, c.full.rect()) == 0, nm + "no curve, fill, grid line or handle lies under the full screen button");
            check (dataOpsInto (s, band) == 0, nm + "nothing of the graph is drawn in the header band");
        }
    }
    check (c.graph.plot().y - c.graph.rect().y >= playrows::kBandH + playrows::kPlotPadTop, "compact graph: the plot starts at least a band and its padding under the top of the display");
}

void testFullScreenLayout()
{
    EnvFullScreen f;
    f.place ({ 0, 110, 1280, 628 });
    check (allInside (f), "full screen: every part is inside the page");
    bool sizes = true, overlap = false;
    for (int i = 0; i < f.numChildren(); ++i)
    {
        sizes = sizes && f.child (i)->sizeOk();
        for (int j = i + 1; j < f.numChildren(); ++j)
            overlap = overlap || f.child (i)->rect().overlaps (f.child (j)->rect());
    }
    check (sizes, "full screen: every part has at least its minimum size");
    check (! overlap, "full screen: no two parts overlap");
    Component* leaves[96];
    const int n = f.collectLeaves (leaves, 0, 96);
    bool adjacent = true;
    for (int g = 0; g < EnvFullScreen::kMaxGroups; ++g)
        adjacent = adjacent && f.groups[g].check (leaves, n) == 0;
    check (adjacent, "full screen: the three Q groups hold only their members");
    check (f.groups[0].numMembers() == 4 && f.groups[1].numMembers() == 3 && f.groups[2].numMembers() == 4, "full screen: ADSR 4, SHAPE 3, TIME 4 members");
    bool oneGroup = true;   // a value is in one group only
    for (int a = 0; a < EnvFullScreen::kMaxGroups; ++a)
        for (int b = a + 1; b < EnvFullScreen::kMaxGroups; ++b)
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 4; ++k)
                    oneGroup = oneGroup && (f.groups[a].member (i) == nullptr || f.groups[a].member (i) != f.groups[b].member (k));
    check (oneGroup, "full screen: no value is in two Q groups");
    for (int g = 0; g < EnvFullScreen::kMaxGroups; ++g)   // the members of a group lie side by side, with room above them for the index chips
    {
        const Rect b = f.block (g);
        const Rect u = f.groups[g].rect();
        check (b.contains (u) && u.y - b.y >= 14, "full screen: group " + std::to_string (g) + " is inside its block, with the lane of the index chips above it");
    }
    Touch tc;
    const Rect k = f.hold.rect();
    tc.down (f, k.x + k.w / 2, k.y + k.h / 2);
    tc.up (f, k.x + k.w / 2, k.y + k.h / 2);
    check (f.activeGroup() == 2, "a touch on a TIME knob makes the TIME group the active one");
    f.setActiveGroup (0);
    check (f.instances.targetMask() == 0 && ! f.instances.hasTargets (0), "full screen: selector targetMask starts at 0");
    f.instances.setTargetMask (0x05);
    check (f.instances.hasTargets (0) && ! f.instances.hasTargets (1) && f.instances.hasTargets (2), "full screen: selector hasTargets reflects mask (ENV 1 and 3)");
    Scene s (1280, 800, kBackgroundRgba);
    f.draw (s.builder());
    f.drawLive (s.builder());
    const Image im = render (s);
    check (! s.overflow() && drawnOutside (im, { 0, 110 - 4, 1280, 628 + 8 }, backgroundPixel()) == 0, "full screen: drawn, and nothing outside the page");
    // without a target the graph has the whole width: the curve has a row for EVERY pixel of it (a curve with fewer rows ends before the right edge)
    check (f.graph.rect().w > 1200 && f.tagsRect().w >= 200 && f.tagsRect().right() <= f.targets.rect().x, "full screen: the graph takes the whole width with or without target; the tags have their room in the top bar left of TARGETS");
    uint32_t most = 0;
    for (uint32_t k = 0; k < s.data->numOps; ++k)
        if (s.data->ops[k].op == gpu::kOpCurve)
            most = std::max (most, static_cast<uint32_t> (s.data->ops[k].f[1]));
    check (most >= static_cast<uint32_t> (f.graph.rect().w - 60), "full screen: the curve of the wide graph has a row per pixel (" + std::to_string (most) + " rows, graph " + std::to_string (f.graph.rect().w) + " px)");
}

void testTargetsPage()
{
    ModTargetsPage p;
    p.place ({ 0, 110, 1280, 628 });
    ModTargetsPage::Entry e[6];
    for (int i = 0; i < 6; ++i)
    {
        std::snprintf (e[i].name, sizeof e[i].name, "Filter 1 Cutoff %d", i);
        e[i].depth = -0.5 + 0.2 * i;
    }
    p.load (e, 6, "ENV 2", 0xff9a3d, 13, 1);
    check (allInside (p) && p.numRows() == 6, "targets page: six rows inside the page");
    check (p.rowY (5) + ModTargetsPage::kRowH <= 110 + 628, "targets page: the sixth row ends inside the page");
    bool sizes = true;
    for (int i = 0; i < p.numChildren(); ++i)
        sizes = sizes && (! p.childShown (i) || p.child (i)->sizeOk());
    check (sizes, "targets page: every shown part has its minimum size");
    check (p.page.state.count == 3 && p.page.state.index == 1 && p.childShown (1), "13 targets are three pages, the page stepper is shown (page 2)");
    p.load (e, 2, "ENV 2", 0xff9a3d, 2, 0);
    check (p.numRows() == 2 && ! p.childShown (1), "two targets: two rows, no page stepper");
    Touch tc;
    const Rect r1 = p.rows[1].depth.rect();
    tc.down (p, r1.x + 20, r1.y + 20);
    tc.up (p, r1.x + 20, r1.y + 20);
    check (p.activeRow() == 1 && p.group.member (0) == &p.rows[1].depth && p.group.member (2) == &p.rows[1].bypass, "a touch on a row makes its depth, polarity and on / off the Q group");
    Scene s (1280, 800, kBackgroundRgba);
    p.draw (s.builder());
    check (! s.overflow(), "targets page: one scene");
}
} // namespace

int main()
{
    testShapeMath();
    testHandlesAndHitTest();
    testSerumEnvelopeGraph();
    testLockedFitRulerAndZoom();
    testDragFollowsTheFinger();
    testDecayIsTwoDimensional();
    testCurveHandleFollowsTheFinger();
    testDoubleTapResets();
    testDrawing();
    testCompactLayout();
    testCompactPlot();
    testFullScreenLayout();
    testTargetsPage();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
