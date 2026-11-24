// SynthyBoy - MPC overlay tests: the cached cells of a scene (gpu/scene.h CellBegin / CellEnd, gpu/cell_plan.h): the hash of a cell does not depend on where its text and its curves are in the pools, a changed
// value changes it; the CPU reference draws a scene with cells exactly like the same scene without them; the plan (layers, the place of the blits, the broken cases) and the damage between two pictures.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "gpu/cell_plan.h"
#include "scene_cpu.h"

using namespace synthyboy_mpc::overlay;
using namespace synthyboy_mpc::overlay::gpu;

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    g_fails += ! ok;
}

namespace
{
/** A part of a picture: a text, a curve with a gradient fill, a rounded rectangle; `value` moves the rounded rectangle. */
void part (SceneBuilder& sb, float x, float value, bool inCell, const char* label = "KNOB")
{
    if (inCell)
        sb.cellBegin (x - 2.0f, 10.0f, x + 82.0f, 90.0f);
    sb.style (1.0f, false);
    sb.paintOff();
    sb.rrect (x, 20.0f, x + 80.0f, 20.0f + 40.0f + value, 6.0f, rgba8 (60, 80, 200, 255));
    sb.text (x + 4.0f, 80.0f, 14.0f, label, rgba8 (255, 255, 255, 255));
    const float rows[2] = { 15.0f, 15.0f };
    sb.curve (rows, 2, static_cast<int> (x), 80);
    sb.fillGradient (60.0f, 15.0f, 60.0f, rgba8 (0, 255, 0, 120), rgba8 (0, 0, 255, 0));
    if (inCell)
        sb.cellEnd();
}
uint32_t firstHash (const SceneData& s)
{
    for (uint32_t k = 0; k < s.numOps; ++k)
        if (s.ops[k].op == kOpCellBegin)
            return s.ops[k].rgba;
    return 0;
}
}

int main()
{
    auto a = std::make_unique<SceneData>(), b = std::make_unique<SceneData>(), c = std::make_unique<SceneData>();
    // ---- the hash: the same content at another place in the pools gives the same hash; another value, text, colour or rectangle gives another
    {
        SceneBuilder sa (*a), sb2 (*b), sc (*c);
        sa.begin (kKindPlay, 0, 400, 200); sb2.begin (kKindPlay, 0, 400, 200); sc.begin (kKindPlay, 0, 400, 200);
        sa.clear (0u);
        part (sa, 10.0f, 0.0f, true);
        sb2.clear (0u);
        sb2.text (300.0f, 150.0f, 12.0f, "something before in the text pool", rgba8 (1, 2, 3, 255));
        const float other[5] = { 1, 2, 3, 4, 5 };
        sb2.curve (other, 5, 0, 50);
        part (sb2, 10.0f, 0.0f, true);                // the same cell after other text and other rows
        sc.clear (0u);
        part (sc, 10.0f, 5.0f, true);                 // the value moved
        const uint32_t ha = firstHash (*a), hb = [&] { uint32_t h = 0; for (uint32_t k = 0; k < b->numOps; ++k) if (b->ops[k].op == kOpCellBegin) h = b->ops[k].rgba; return h; }(), hc = firstHash (*c);
        check (ha != 0 && ha == hb, "cell hash: the same content gives the same hash wherever its text and its curve are in the pools");
        check (hc != ha, "cell hash: a changed value gives another hash");
        auto d = std::make_unique<SceneData>(), e = std::make_unique<SceneData>(), f = std::make_unique<SceneData>();
        SceneBuilder s1 (*d), s2 (*e), s3 (*f);
        s1.begin (kKindPlay, 0, 400, 200); s2.begin (kKindPlay, 0, 400, 200); s3.begin (kKindPlay, 0, 400, 200);
        part (s1, 10.0f, 0.0f, true, "KNOB");
        part (s2, 10.0f, 0.0f, true, "KNOC");
        part (s3, 12.0f, 0.0f, true, "KNOB");
        check (firstHash (*d) != firstHash (*e), "cell hash: another text gives another hash");
        check (firstHash (*d) != firstHash (*f), "cell hash: another rectangle gives another hash");
    }
    // ---- nesting and empty cells
    {
        auto s = std::make_unique<SceneData>();
        SceneBuilder sb (*s);
        sb.begin (kKindPlay, 0, 100, 100);
        sb.cellBegin (0, 0, 50, 50);
        sb.cellBegin (0, 0, 20, 20);
        sb.rect (1, 1, 10, 10, rgba8 (255, 0, 0, 255));
        sb.cellEnd();
        sb.rect (20, 20, 30, 30, rgba8 (0, 255, 0, 255));
        sb.cellEnd();
        sb.cellBegin (0, 0, 5, 5);
        sb.cellEnd();
        int begins = 0, ends = 0;
        for (uint32_t k = 0; k < s->numOps; ++k)
        {
            begins += s->ops[k].op == kOpCellBegin;
            ends += s->ops[k].op == kOpCellEnd;
        }
        check (begins == 1 && ends == 1 && s->numOps == 4, "builder: a nested pair is ignored, a cell without ops is dropped");
    }
    // ---- the CPU reference draws a scene with cells like the same scene without
    {
        auto with = std::make_unique<SceneData>(), without = std::make_unique<SceneData>();
        SceneBuilder s1 (*with), s2 (*without);
        s1.begin (kKindPlay, 0, 400, 200); s2.begin (kKindPlay, 0, 400, 200);
        for (SceneBuilder* sb : { &s1, &s2 })
        {
            const bool cells = sb == &s1;
            sb->clear (rgba8 (10, 10, 20, 255));
            part (*sb, 10.0f, 0.0f, cells);
            part (*sb, 110.0f, 8.0f, cells, "TWO");
            part (*sb, 210.0f, -6.0f, cells, "THREE");
        }
        std::vector<uint32_t> p1, p2;
        renderScene (*with, p1);
        renderScene (*without, p2);
        check (p1 == p2 && ! p1.empty(), "CPU reference: a scene with cells is the same picture as the scene without them");
    }
    // ---- the plan: two layers, the place of the blits, the broken cases
    {
        auto s = std::make_unique<SceneData>();
        SceneBuilder sb (*s);
        sb.begin (kKindPlay, 0, 400, 200);
        sb.clear (0u);
        part (sb, 10.0f, 0.0f, true);      // layer 0
        part (sb, 110.0f, 0.0f, true);     // layer 0
        sb.rect (0, 0, 400, 5, rgba8 (255, 255, 255, 255));   // a drawing op outside a cell: what follows is over it
        part (sb, 210.0f, 0.0f, true);     // layer 1
        CellPlan plan;
        check (planCells (*s, plan) && plan.cells.size() == 3 && plan.cells[0].layer == 0 && plan.cells[1].layer == 0 && plan.cells[2].layer == 1 && plan.layer[0] && plan.layer[1], "plan: two cells on layer 0, one over the free op on layer 1");
        check (plan.blitAt[0] == plan.cells[0].begin && plan.blitAt[1] == plan.cells[2].begin && plan.box[0][0] == 8 && plan.box[0][2] == 192 && plan.box[1][0] == 208, "plan: the layer goes onto the picture where its first cell began, over the union of its cells");
        bool covered = true;
        for (const CellRef& r : plan.cells)
            for (int k = r.begin; k <= r.end; ++k)
                covered = covered && plan.inCell[static_cast<size_t> (k)] == 1;
        check (covered && plan.inCell[0] == 0, "plan: every op of a cell is marked, the others not");
        sb.rect (0, 100, 400, 105, rgba8 (255, 255, 255, 255));
        part (sb, 310.0f, 0.0f, true);     // a third group: not cached, the scene is drawn whole
        check (! planCells (*s, plan) && plan.cells.empty(), "plan: a cell after a second group of free ops: no caching for the scene");
        auto t = std::make_unique<SceneData>();
        SceneBuilder st (*t);
        st.begin (kKindPlay, 0, 400, 200);
        st.rect (0, 0, 10, 10, rgba8 (1, 1, 1, 255));
        check (! planCells (*t, plan), "plan: a scene without cells is drawn whole");
        // an unclosed cell and a bad rectangle
        auto u = std::make_unique<SceneData>();
        u->numOps = 2; u->w = 100; u->h = 100;
        u->ops[0].op = kOpCellBegin; u->ops[0].f[2] = 50; u->ops[0].f[3] = 50;
        u->ops[1].op = kOpRect;
        check (! planCells (*u, plan), "plan: a cell that is not closed: no caching");
        u->numOps = 3;
        u->ops[2].op = kOpCellEnd;
        check (planCells (*u, plan), "plan: the same cell closed is accepted");
        u->ops[0].f[2] = -5;
        check (! planCells (*u, plan), "plan: an empty rectangle: no caching");
        u->ops[0].f[2] = std::numeric_limits<float>::quiet_NaN();
        check (! planCells (*u, plan), "plan: a NaN rectangle: no caching");
    }
    // ---- the damage between two pictures
    {
        std::vector<CellKey> before = { { 0, 0, 100, 100, 1 }, { 200, 0, 300, 100, 2 }, { 0, 200, 100, 300, 3 } }, now = before;
        std::vector<DamageRect> d;
        layerDamage (before, now, d);
        check (d.empty(), "damage: nothing changed, nothing to draw");
        now[1].hash = 9;
        layerDamage (before, now, d);
        check (d.size() == 1 && d[0].x0 == 200 && d[0].x1 == 300 && d[0].y1 == 100, "damage: one cell changed: its rectangle (old and new are the same)");
        now[1] = { 220, 0, 320, 100, 2 };
        layerDamage (before, now, d);
        check (d.size() == 1 && d[0].x0 == 200 && d[0].x1 == 320, "damage: a cell that moved: the union of the old and the new place");
        now = before;
        now.push_back ({ 500, 500, 600, 600, 7 });
        layerDamage (before, now, d);
        check (d.size() == 1 && d[0].x0 == 500, "damage: a cell that came");
        now = before;
        now.pop_back();
        layerDamage (before, now, d);
        check (d.size() == 1 && d[0].y0 == 200, "damage: a cell that went");
        now = before;
        now[0].hash = 10; now[2].hash = 30;
        layerDamage (before, now, d);
        check (d.size() == 4 || d.size() == 2, "damage: two far apart changes: their boxes (" + std::to_string (d.size()) + ")");
        now = before;
        now.insert (now.begin(), { 600, 600, 700, 700, 99 });
        layerDamage (before, now, d);
        check (d.size() == 1 && d[0].x0 == 600, "damage: a cell inserted at the front does not make the cells after it look changed");
        now = before;
        now.erase (now.begin());
        layerDamage (before, now, d);
        check (d.size() == 1 && d[0].x1 == 100 && d[0].y1 == 100, "damage: a cell taken away from the front: only its place");
        std::vector<CellKey> many, many2;
        for (int i = 0; i < 8; ++i) { many.push_back ({ i * 100, 0, i * 100 + 50, 50, 1u }); many2.push_back ({ i * 100, 0, i * 100 + 50, 50, 2u }); }
        layerDamage (many, many2, d);
        check (d.size() == 1 && d[0].x0 == 0 && d[0].x1 == 750, "damage: more than four boxes become one");
        now = before;
        now[0].hash = 11; now[1] = { 50, 50, 250, 150, 5 };
        layerDamage (before, now, d);
        check (d.size() == 1, "damage: overlapping boxes are merged");
    }
    // ---- the part to paint for the age of the back buffer
    {
        std::vector<DamageRect> now = { { 0, 0, 100, 100 } }, hist[4], out;
        hist[0] = { { 500, 500, 600, 600 } };
        hist[1] = { { 900, 0, 1000, 50 } };
        bool full = false;
        framePaint (now, hist, 2, 2, out, full);
        check (! full && out.size() == 2, "paint: age 2: this picture's damage and the one before it");
        framePaint (now, hist, 2, 3, out, full);
        check (! full && out.size() == 3, "paint: age 3: and the one before that");
        framePaint (now, hist, 2, 4, out, full);
        check (full, "paint: the age is older than the history: the whole picture");
        framePaint (now, hist, 2, 1, out, full);
        check (! full && out.size() == 1, "paint: age 1: only this picture's damage");
        framePaint (now, hist, 2, 0, out, full);
        check (full, "paint: age 0 (undefined buffer): the whole picture");
        framePaint (now, hist, 0, 1, out, full);
        check (! full && out.size() == 1, "paint: age 1 without history: this picture's damage");
        framePaint (now, hist, 0, 2, out, full);
        check (full, "paint: no history yet: the whole picture");
        std::vector<DamageRect> many;
        for (int i = 0; i < 10; ++i) many.push_back ({ i * 100, 0, i * 100 + 10, 10 });
        mergeBoxes (many, 4);
        check (many.size() == 1 && many[0].x0 == 0 && many[0].x1 == 910, "paint: more than four boxes become one");
    }
    std::printf ("%s: %d failed\n", g_fails ? "FAIL" : "PASS", g_fails);
    return g_fails ? 1 : 0;
}
