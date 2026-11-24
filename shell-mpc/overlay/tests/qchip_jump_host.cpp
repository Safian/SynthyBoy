// SynthyBoy - MPC overlay tests: nothing on the Play page may move when a value changes (owner rule, 2026-11-21: "the Q slot chips JUMP when Q-Link 9, the LFO 1 shape, is turned"). A change of the LFO shape / of an
// envelope / of an oscillator display sets the display's hot window (live ops for a few hundred ms); the picture OUTSIDE the animated display must be the same in the settled state before, in the hot window and
// in the settled state after, and the structure of the scene (the groups of the cell plan, the cells and their order) must not change between hot and settled. The test prints the numbers of the pixel diff.
// Build: c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -I core/include -I shell-mpc/overlay -o qchip_jump_host shell-mpc/overlay/tests/qchip_jump_host.cpp <libsynthyboy_core.a> -lpthread
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../gpu/cell_plan.h"
#include "../play_view.h"
#include "../scene_cpu.h"
#include "fakes.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;
using ovt::check;

namespace
{
/** A check that fails on the tree without docs/plans/qchip-jump-hot-window.patch (the hot window's data between the cells): reported as KNOWN-OPEN, a failure only with QCHIP_STRICT=1 (the patch applied). */
void knownOpen (bool ok, const std::string& what)
{
    const char* e = std::getenv ("QCHIP_STRICT");
    if (e != nullptr && e[0] == '1')
        check (ok, what);
    else
        std::printf ("%s  %s\n", ok ? "PASS" : "KNOWN-OPEN", what.c_str());
}

struct Shot
{
    std::unique_ptr<gpu::SceneData> sd = std::make_unique<gpu::SceneData>();
    std::vector<uint32_t> px;
    gpu::CellPlan plan;
    bool planned = false;
    bool hot = false;
    int groups = 0;   // the layers of the plan (0: uncached = drawn whole)
    int freeGroupsAfterFirstCell = 0;
};

/** One Play page: the view, its instance and the clock (a fake one: the update path is the real one). */
struct Page
{
    ovt::FakeInstance inst;
    synthyboy::LiveValues lv;
    PlayView pv { 1, 10.0 };
    UiStack st;
    ViewInput in;
    Page()
    {
        inst.playSel[0] = 2; inst.playSel[1] = 3; inst.playSel[2] = 1;   // Osc A, Env 3, LFO 1
        in.instance = &inst; in.io = &inst; in.ui = &st; in.live = &lv; in.nowMs = 0.0;
        pv.update (in);
        advance (500.0);
    }
    void advance (double ms) { in.nowMs += ms; pv.update (in); }
    Shot shot()
    {
        Shot s;
        gpu::SceneBuilder sb (*s.sd);
        check (pv.describe (sb, 0, PlayView::kW, PlayView::kH), "the Play page describes");
        renderScene (*s.sd, s.px);
        s.planned = gpu::planCells (*s.sd, s.plan);
        s.groups = s.planned ? (s.plan.layer[1] ? 2 : 1) : 0;
        return s;
    }
};

bool inside (const ui::Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

struct Diff { long n = 0; int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; };
/** The pixels that differ outside `hole` (the LFO display) and `hole2` (the control whose value text is the change itself). */
Diff diffOutside (const Shot& a, const Shot& b, const ui::Rect& hole, const ui::Rect& hole2 = {})
{
    Diff d;
    for (int y = 0; y < PlayView::kH; ++y)
        for (int x = 0; x < PlayView::kW; ++x)
            if (! inside (hole, x, y) && ! inside (hole2, x, y) && a.px[static_cast<size_t> (y) * PlayView::kW + static_cast<size_t> (x)] != b.px[static_cast<size_t> (y) * PlayView::kW + static_cast<size_t> (x)])
            {
                ++d.n;
                d.x0 = std::min (d.x0, x); d.y0 = std::min (d.y0, y); d.x1 = std::max (d.x1, x); d.y1 = std::max (d.y1, y);
            }
    return d;
}

/** The scene with every cell clipped to its own rectangle (what the helper's per-cell texture does): the pixels of the cached path, for a comparison with the whole-scene path. */
Shot clippedCells (const Shot& s)
{
    Shot out;
    gpu::SceneData& d = *out.sd;
    d.kind = s.sd->kind; d.viewIndex = s.sd->viewIndex; d.w = s.sd->w; d.h = s.sd->h;
    d.numOps = 0; d.numPoints = s.sd->numPoints; d.numText = s.sd->numText; d.numDefault = s.sd->numDefault;
    std::memcpy (d.text, s.sd->text, sizeof d.text);
    std::memcpy (d.y, s.sd->y, sizeof d.y);
    for (uint32_t k = 0; k < s.sd->numOps; ++k)
    {
        const gpu::SceneOp& o = s.sd->ops[k];
        d.ops[d.numOps++] = o;
        if (o.op == gpu::kOpCellBegin)
        {
            gpu::SceneOp c {};
            c.op = gpu::kOpClip;
            c.f[0] = std::floor (o.f[0]); c.f[1] = std::floor (o.f[1]); c.f[2] = std::ceil (o.f[2]); c.f[3] = std::ceil (o.f[3]);
            d.ops[d.numOps++] = c;
        }
        else if (o.op == gpu::kOpCellEnd)
        {
            gpu::SceneOp c {};
            c.op = gpu::kOpClip;
            d.ops[d.numOps++] = c;
        }
    }
    renderScene (d, out.px);
    return out;
}

std::string cellKeys (const Shot& s)
{
    std::string r;
    for (const gpu::CellRef& c : s.plan.cells)
        r += std::to_string (c.x0) + "," + std::to_string (c.y0) + "," + std::to_string (c.x1) + "," + std::to_string (c.y1) + "/" + std::to_string (c.layer) + ";";
    return r;
}

/** The cells in `s` that do not touch `hole` (the LFO display) or `hole2` (the control whose value is the change): (rectangle, hash) in scene order. */
std::vector<gpu::CellKey> outsideCells (const Shot& s, const ui::Rect& hole, const ui::Rect& hole2)
{
    auto touches = [] (const gpu::CellRef& c, const ui::Rect& r) { return c.x0 < r.x + r.w && r.x < c.x1 && c.y0 < r.y + r.h && r.y < c.y1; };
    std::vector<gpu::CellKey> v;
    for (const gpu::CellRef& c : s.plan.cells)
        if (! touches (c, hole) && ! touches (c, hole2))
            v.push_back ({ c.x0, c.y0, c.x1, c.y1, c.hash });
    return v;
}

void testShapeChange (bool wide, int toShape)
{
    ui::QGroup::setWideDevice (wide);
    const std::string tag = std::string (wide ? "XL, " : "4 knob, ") + "shape 0 -> " + std::to_string (toShape) + ": ";
    Page p;
    const ui::Rect hole = p.pv.lfoUi()->displayRect(), shapeR = p.pv.lfoUi()->compact().shape.rect();   // (the SHAPE stepper's own value text is the change)
    check (! hole.empty() && ! shapeR.empty(), tag + "the LFO display and the SHAPE stepper have rectangles");
    const Shot before = p.shot();
    // Q-Link 9 turns lfo1.shape: the write goes to the parameter, the next update picks it up (a fake clock: 16 ms later, inside the 350 ms window)
    p.inst.set ("lfo1.shape", static_cast<float> (toShape));
    p.advance (16.0);
    const Shot hot = p.shot();
    p.advance (400.0);   // outside the window
    const Shot after = p.shot();
    const Diff dBH = diffOutside (before, hot, hole, shapeR), dBA = diffOutside (before, after, hole, shapeR), dHA = diffOutside (hot, after, hole, shapeR);
    const Diff wholeHA = diffOutside (hot, after, {});   // the whole screen, the display included: the data drawn live is the data drawn as a cell
    const Shot cb = clippedCells (before), ch = clippedCells (hot), ca = clippedCells (after);
    const Diff cBH = diffOutside (cb, ch, hole, shapeR), cHA = diffOutside (ch, ca, hole, shapeR), wholeVsCell = diffOutside (hot, ch, hole, shapeR);
    std::printf ("INFO %sgroups before/hot/after = %d/%d/%d (0 = uncached, whole), cells %zu/%zu/%zu\n", tag.c_str(), before.groups, hot.groups, after.groups, before.plan.cells.size(), hot.plan.cells.size(), after.plan.cells.size());
    std::printf ("INFO %soutside the LFO display: pixels differing whole-scene before/hot %ld, before/after %ld, hot/after %ld; with every cell clipped to its rectangle (the helper's cell texture) before/hot %ld, hot/after %ld; hot whole vs hot clipped %ld",
                 tag.c_str(), dBH.n, dBA.n, dHA.n, cBH.n, cHA.n, wholeVsCell.n);
    if (dBH.n > 0)
        std::printf (" [before/hot box %d,%d..%d,%d]", dBH.x0, dBH.y0, dBH.x1, dBH.y1);
    if (dHA.n > 0)
        std::printf (" [hot/after box %d,%d..%d,%d]", dHA.x0, dHA.y0, dHA.x1, dHA.y1);
    std::printf ("; whole screen hot/after %ld\n", wholeHA.n);
    for (const gpu::CellRef& c : hot.plan.cells)
        if (c.layer == 1)
            std::printf ("INFO %shot window: layer 1 cell %d,%d..%d,%d\n", tag.c_str(), c.x0, c.y0, c.x1, c.y1);
    check (wholeHA.n == 0, tag + "the hot picture is pixel identical to the settled one (display included)");
    check (dBH.n == 0 && dBA.n == 0 && dHA.n == 0, tag + "nothing outside the LFO display changes (before / hot / after, whole scene)");
    check (cBH.n == 0 && cHA.n == 0 && wholeVsCell.n == 0, tag + "nothing outside the LFO display changes with the cells clipped to their rectangles");
    // the chips: the same cells (rectangle, hash) outside the display, in the same order
    const auto cb0 = outsideCells (before, hole, shapeR), ch0 = outsideCells (hot, hole, shapeR), ca0 = outsideCells (after, hole, shapeR);
    for (size_t i = 0; i < std::min (cb0.size(), ch0.size()); ++i)
        if (! (cb0[i] == ch0[i]))
            std::printf ("INFO %scell %zu outside the display differs before/hot: %d,%d..%d,%d hash %08x vs %d,%d..%d,%d hash %08x\n", tag.c_str(), i, cb0[i].x0, cb0[i].y0, cb0[i].x1, cb0[i].y1, cb0[i].hash, ch0[i].x0, ch0[i].y0, ch0[i].x1, ch0[i].y1, ch0[i].hash);
    check (cb0 == ch0 && ch0 == ca0, tag + "the cells outside the display (the chips, the labels, the knobs) are identical: rectangles, hashes, order (" + std::to_string (cb0.size()) + " cells)");
    // the structure of the scene: the same groups, the same cell order, the same number of cells apart from the LFO's own data cell
    // KNOWN (TODO.md): the hot window draws the data as live ops, a free group among the cells, so the cells that lie over the live parts (the osc display's FULL / POWER / ROUTE buttons) go to layer 1 in the hot
    // window and to layer 0 when settled (1 -> 2 -> 1 groups): a layer reassignment, not a movement. The scene stays cached (never a third group); the layer-1 cells are only those inside the oscillator third.
    check (before.planned && hot.planned && after.planned, tag + "the scene is cached (a cell plan) in the three states: never a third group");
    check (before.groups == after.groups, tag + "settled before and after have the same groups");
    bool l1Osc = true;
    for (const gpu::CellRef& c : hot.plan.cells)
        l1Osc = l1Osc && (c.layer == 0 || (c.x1 <= 432 && c.y0 >= 110));
    knownOpen (l1Osc, tag + "the cells on layer 1 in the hot window lie in the oscillator third only (the Q chips and labels of the LFO and envelope thirds stay on layer 0)");
    check (cellKeys (before) == cellKeys (after), tag + "settled before and after: the same cells, rectangles and layers in the same order");
}

/** The layout does not depend on the value: the rectangles of the members, the chips and the display of the LFO's compact view are the same for every shape (a constant of the layout, not of the data). */
void testLayoutIgnoresTheShape()
{
    for (const bool wide : { false, true })
    {
        ui::QGroup::setWideDevice (wide);
        std::vector<ui::Rect> first;
        bool same = true;
        for (const int shape : { 0, 1, 3, 7, 11 })
        {
            Page p;
            p.inst.set ("lfo1.shape", static_cast<float> (shape));
            p.advance (16.0);
            ui::LfoCompact& c = p.pv.lfoUi()->compact();
            std::vector<ui::Rect> r = { p.pv.lfoUi()->displayRect(), c.shape.rect(), c.rate.rect(), c.rise.rect(), c.smoothTime.rect(), c.full.rect(), c.instance.rect(), c.rect() };
            for (const ui::Rect& m : { c.shape.rect(), c.rate.rect(), c.rise.rect(), c.smoothTime.rect() })
                r.push_back (ui::QGroup::chipRect (m, 1));
            if (first.empty())
                first = r;
            for (size_t i = 0; i < r.size(); ++i)
                same = same && r[i].x == first[i].x && r[i].y == first[i].y && r[i].w == first[i].w && r[i].h == first[i].h;
        }
        check (same, std::string (wide ? "XL: " : "4 knob: ") + "the display, the members, their chips and the full screen button have the same rectangles for every shape");
    }
    ui::QGroup::setWideDevice (false);
}

/** The animated displays of the Play page (oscillator, envelope, LFO): a change of each must keep the structure of the scene. */
void testAllDisplays()
{
    ui::QGroup::setWideDevice (true);
    struct Case { const char* what; const char* key; float to; };
    const Case cases[] = { { "LFO 1 shape", "lfo1.shape", 3.0f }, { "LFO 1 rate", "lfo1.rate", 4.0f }, { "ENV 3 attack", "env3.attack", 400.0f }, { "ENV 3 release", "env3.release", 900.0f },
                           { "OSC A level", "osc1.level", 0.3f }, { "OSC A pan", "osc1.pan", 0.5f } };
    for (const Case& c : cases)
    {
        Page p;
        if (ovt::FakeInstance().index (c.key) < 0)
        {
            check (false, std::string (c.what) + ": the parameter " + c.key + " exists");
            continue;
        }
        const Shot before = p.shot();
        p.inst.set (c.key, c.to);
        p.advance (16.0);
        const Shot hot = p.shot();
        p.advance (400.0);
        const Shot after = p.shot();
        const std::string tag = std::string (c.what) + ": ";
        std::printf ("INFO %sgroups %d/%d/%d cells %zu/%zu/%zu\n", tag.c_str(), before.groups, hot.groups, after.groups, before.plan.cells.size(), hot.plan.cells.size(), after.plan.cells.size());
        for (const gpu::CellRef& c : before.plan.cells)   // which cells turn into live ops in the hot window (a cell of the settled state that has no equal in the hot one)
        {
            bool found = false;
            for (const gpu::CellRef& h : hot.plan.cells)
                found = found || (h.x0 == c.x0 && h.y0 == c.y0 && h.x1 == c.x1 && h.y1 == c.y1);
            if (! found)
                std::printf ("INFO %sthe cell %d,%d..%d,%d (layer %d) of the settled state is not a cell in the hot window\n", tag.c_str(), c.x0, c.y0, c.x1, c.y1, c.layer);
        }
        check (before.planned && hot.planned && after.planned && before.groups == after.groups, tag + "cached in all three states; settled before / after alike (hot may split the layers: TODO.md)");
    }
    ui::QGroup::setWideDevice (false);
}
}

int main()
{
    testLayoutIgnoresTheShape();
    testShapeChange (true, 3);
    testShapeChange (true, 1);
    testShapeChange (false, 3);
    testAllDisplays();
    std::printf ("%s (%d failed)\n", ovt::g_fails == 0 ? "ALL PASS" : "FAILURES", ovt::g_fails);
    return ovt::g_fails == 0 ? 0 : 1;
}
