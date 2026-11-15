// SynthyBoy - MPC overlay tests: the cache of the cached layers and the plan of the partial picture (gpu/cell_cache.h): what a layer holds, what changed against it, and for every age of the back
// buffer (0 = unknown, 1, 2, 3, older than the history) which boxes are painted; a changed clip, the diagnostics switch, the 70 percent rule, the reset after a picture without cells.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "gpu/cell_cache.h"

using namespace synthyboy_mpc::overlay::gpu;

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    g_fails += ! ok;
}
static bool same (const std::vector<DamageRect>& a, const std::vector<DamageRect>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].x0 != b[i].x0 || a[i].y0 != b[i].y0 || a[i].x1 != b[i].x1 || a[i].y1 != b[i].y1)
            return false;
    return true;
}

int main()
{
    const int W = 1000, H = 800;
    const int clip[4] = { 0, 0, W, H };
    // ---- the layer cache
    {
        CellPlan plan;
        plan.cells.push_back ({ 0, 0, 10, 10, 50, 50, 111u, 0 });
        plan.cells.push_back ({ 0, 0, 100, 10, 150, 50, 222u, 0 });
        plan.cells.push_back ({ 0, 0, 10, 100, 50, 150, 333u, 1 });
        LayerCache c;
        const std::vector<CellKey> k0 = LayerCache::keysOf (plan, 0), k1 = LayerCache::keysOf (plan, 1);
        check (k0.size() == 2 && k1.size() == 1 && k1[0].hash == 333u, "the keys of a layer are its cells (rectangle and hash)");
        std::vector<DamageRect> d;
        c.damage (0, k0, d);
        check (d.size() == 2 && c.size (0) == 0, "an empty layer: every cell is to be drawn");
        c.commit (0, k0);
        c.damage (0, k0, d);
        check (d.empty() && c.size (0) == 2, "the same cells again: nothing to draw");
        std::vector<CellKey> changed = k0;
        changed[1].hash = 999u;
        c.damage (0, changed, d);
        check (d.size() == 1 && d[0].x0 == 100 && d[0].x1 == 150, "one cell with another hash: only its place");
        c.forget (0);
        c.damage (0, k0, d);
        check (d.size() == 2, "a forgotten layer (not used by a scene, or made again): everything is drawn");
        c.commit (1, k1);
        c.forgetAll();
        check (c.size (0) == 0 && c.size (1) == 0, "forgetAll");
    }
    // ---- the partial picture, one small change in every picture
    {
        PartialPaint p;
        const std::vector<DamageRect> none, small1 = { { 10, 10, 60, 60 } }, small2 = { { 200, 200, 260, 240 } };
        std::vector<DamageRect> out;
        // age 0 / unknown: all of it
        check (! p.plan (small1, none, clip, 0, true, W, H, out), "buffer age unknown (0): the whole picture");
        // age 1: this picture's damage only (the buffer holds the picture before)
        check (p.plan (small2, none, clip, 1, true, W, H, out) && same (out, small2), "age 1: only what changed in this picture");
        // age 2: this picture's and the one before's
        check (p.plan (small1, none, clip, 2, true, W, H, out) && out.size() == 2, "age 2: this picture's damage and the last one's (" + std::to_string (out.size()) + " boxes)");
        // age 3 with two pictures of history before: fine; age 5 (more than the history we have): all
        check (p.plan (small2, none, clip, 3, true, W, H, out) && out.size() >= 2, "age 3: three pictures' damage");
        check (! p.plan (small1, none, clip, 9, true, W, H, out), "age older than the history: the whole picture");
        // the live ops: now and the picture before
        PartialPaint q;
        const std::vector<DamageRect> live1 = { { 300, 300, 340, 340 } }, live2 = { { 310, 300, 350, 340 } };
        q.plan (none, live1, clip, 1, true, W, H, out);
        check (q.plan (none, live2, clip, 1, true, W, H, out) && out.size() == 1 && out[0].x0 == 300 && out[0].x1 == 350, "live ops: the place now and the place the picture before (one merged box)");
        const int clip2[4] = { 0, 0, 500, 400 };
        // a changed clip: all of it; switched off: all of it
        check (! q.plan (none, live2, clip2, 1, true, W, H, out), "a changed clip: the whole picture");
        check (! q.plan (none, live2, clip2, 1, false, W, H, out), "switched off by the diagnostics file: the whole picture");
        // more than 70 percent: all of it; boxes are cut to the picture
        PartialPaint r;
        const std::vector<DamageRect> big = { { 0, 0, 900, 700 } }, over = { { -20, -20, 40, 40 } };
        check (! r.plan (big, none, clip, 1, true, W, H, out), "boxes that cover more than 70 percent: the whole picture");
        check (r.plan (over, none, clip, 1, true, W, H, out) && out.size() == 1 && out[0].x0 == 0 && out[0].y0 == 0 && out[0].x1 == 40, "a box outside the picture is cut to it");
        // reset: the history is gone, age 2 then needs one more picture of history
        r.reset();
        check (r.historyCount() == 0 && ! r.plan (small1, none, clip, 2, true, W, H, out), "after a picture without cells (reset): the history is gone, age 2 paints it all");
        check (r.historyCount() == 1, "the history starts again");
        // nothing changed at all, age 1: an empty box list is a valid partial picture (nothing to paint)
        PartialPaint z;
        z.plan (small1, none, clip, 1, true, W, H, out);
        check (z.plan (none, none, clip, 1, true, W, H, out) && out.empty(), "nothing changed, age 1: nothing to paint");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
