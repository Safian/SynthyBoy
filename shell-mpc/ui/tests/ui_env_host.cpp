// SynthyBoy - MPC UI component library tests: the envelope element: the geometry and the touch of the EnvGraph (the handles, the hit test, the closed form of the curve drag, what a drag gives back), the two layouts
// (EnvCompact, EnvFullScreen: every part inside the rectangle, touch targets, the Q groups adjacent) and the targets page (ModTargetsPage).
// Build: c++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -o ui_env_host shell-mpc/ui/tests/ui_env_host.cpp
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui_test.h"
#include "../env_screen.h"
#include "../mod_targets.h"

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
    check (std::fabs (py[envgraph::Attack] - geo.yTop) < 1.0e-9 && std::fabs (py[envgraph::Hold] - geo.yTop) < 1.0e-9 && px[envgraph::Hold] > px[envgraph::Attack] + 10.0, "attack and hold end on the top line, with room between them even when the hold is 0");
    check (std::fabs (geo.x[6] - (g.plot().right())) < 1.0e-6, "the whole envelope fills the plot");
    // the handles of the default shape are far enough apart for the 56 pixel targets to be told apart by the nearest centre
    double minD = 1.0e9;
    for (int a = 0; a < 5; ++a)
        for (int b = a + 1; b < 5; ++b)
            minD = std::min (minD, std::hypot (px[a] - px[b], py[a] - py[b]));
    check (minD > 28.0, "the five end handles are at least half a target apart (" + std::to_string (minD) + ")");
    Rect t[8];
    check (g.touchTargets (t, 8) == 8 && t[0].w >= 56 && t[0].h >= 56, "eight touch targets of at least 56 x 56");
    check (g.handleAt (g.rect().x + 2, g.rect().y + 2) == envgraph::None, "a touch far from every handle takes none");
    // the same on a compact graph (400 wide): the handles are still separate
    EnvGraph c;
    c.place ({ 0, 0, 388, 336 });
    double a[2], b[2];
    c.handlePoint (envgraph::Attack, a[0], a[1]);
    c.handlePoint (envgraph::Hold, b[0], b[1]);
    check (b[0] - a[0] >= 30.0, "compact graph: attack and hold ends are " + std::to_string (b[0] - a[0]) + " px apart");
}

void testDragFollowsTheFinger()
{
    EnvGraph g;
    g.place ({ 20, 20, 800, 340 });
    g.state.defaults = g.state.shape;
    Touch tc;
    double hx, hy;
    g.handlePoint (envgraph::Attack, hx, hy);
    const int x0 = static_cast<int> (std::lround (hx)), y0 = static_cast<int> (std::lround (hy));
    check (! tc.down (g, x0, y0), "touch down alone gives no change");
    Change c;
    for (int i = 1; i <= 12; ++i)
    {
        c = tc.move (g, x0 + 5 * i, y0);
        if (c)
        {
            const EnvGraph::Edit& e = g.edit();
            g.state.shape.t[e.handle] = e.t;   // the owner writes the parameter and gives the shape back
        }
    }
    g.handlePoint (envgraph::Attack, hx, hy);
    check (std::fabs (hx - (x0 + 60)) < 1.5, "the attack handle is under the finger after a drag of 60 px (" + std::to_string (hx) + " vs " + std::to_string (x0 + 60) + ")");
    check (g.state.shape.t[1] > 0.3, "and the attack grew (" + std::to_string (g.state.shape.t[1]) + ")");
    tc.up (g, x0 + 60, y0);
    // far to the right: the time stops at its maximum
    tc.down (g, static_cast<int> (hx), static_cast<int> (hy));
    for (int i = 1; i <= 40; ++i)
    {
        c = tc.move (g, static_cast<int> (hx) + 20 * i, static_cast<int> (hy));
        if (c)
            g.state.shape.t[g.edit().handle] = g.edit().t;
    }
    tc.up (g, static_cast<int> (hx) + 800, static_cast<int> (hy));
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
    check (f.graph.rect().w > 1200 && f.targetsBlock().empty(), "full screen: no target, no block, the graph takes the whole width");
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
    testDragFollowsTheFinger();
    testDecayIsTwoDimensional();
    testCurveHandleFollowsTheFinger();
    testDoubleTapResets();
    testDrawing();
    testCompactLayout();
    testFullScreenLayout();
    testTargetsPage();
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
