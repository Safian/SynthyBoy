// SynthyBoy - MPC overlay, GPU helper: the plan of the cached cells of a scene (scene.h, CellBegin) and the damage between two pictures. Pure logic (no GL): the helper uses it, the Mac tests check it.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "scene.h"

namespace synthyboy_mpc { namespace overlay { namespace gpu {

/** A cell of a scene: the ops (begin .. end, both included), its rectangle (whole pixels, inside the canvas), its hash and the layer it is drawn into. */
struct CellRef { int begin = 0, end = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0; uint32_t hash = 0; int layer = 0; };
/** What identifies a cell from one picture to the next: the rectangle and the hash of the content. */
struct CellKey
{
    int x0, y0, x1, y1;
    uint32_t hash;
    bool operator== (const CellKey& o) const noexcept { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1 && hash == o.hash; }
};
struct DamageRect { int x0, y0, x1, y1; };

struct CellPlan
{
    std::vector<CellRef> cells;            // the cached cells in scene order
    std::vector<uint8_t> inCell;           // per op: 1 = a begin .. end op of a cached cell (not drawn with the picture)
    int blitAt[2] = { -1, -1 };            // the op (the begin of the first cell of the layer) at which the layer goes onto the picture
    int box[2][4] = {};                    // the union rectangle of the layer's cells
    bool layer[2] = { false, false };
};

/** Finds the cells. A drawing op that is not in a cell, after the first cell of a layer, starts the next layer (at most two: a cell after a second such group makes the whole scene uncached). false (and an
    empty plan): no cells, or a cell that is not closed, nested, empty or with a bad rectangle - the scene is then drawn whole, as it always was. */
namespace detail
{
inline bool planCellsImpl (const SceneData& s, CellPlan& plan)
{
    const uint32_t n = std::min<uint32_t> (s.numOps, static_cast<uint32_t> (kMaxOps));
    plan.inCell.assign (n, 0);
    int layer = 0;
    bool seen[2] = { false, false };
    for (uint32_t k = 0; k < n; ++k)
    {
        const SceneOp& o = s.ops[k];
        switch (o.op)
        {
            case kOpCellBegin:
            {
                uint32_t e = k + 1;
                while (e < n && s.ops[e].op != kOpCellEnd && s.ops[e].op != kOpCellBegin)
                    ++e;
                if (e >= n || s.ops[e].op != kOpCellEnd || layer >= 2)
                    return false;
                for (int i = 0; i < 4; ++i)
                    if (! coordOk (o.f[i]))
                        return false;
                CellRef c;
                c.begin = static_cast<int> (k);
                c.end = static_cast<int> (e);
                c.x0 = std::max (0, static_cast<int> (std::floor (o.f[0])));
                c.y0 = std::max (0, static_cast<int> (std::floor (o.f[1])));
                c.x1 = std::min (s.w, static_cast<int> (std::ceil (o.f[2])));
                c.y1 = std::min (s.h, static_cast<int> (std::ceil (o.f[3])));
                if (c.x1 <= c.x0 || c.y1 <= c.y0)
                    return false;
                c.hash = o.rgba;
                c.layer = layer;
                for (uint32_t i = k; i <= e; ++i)
                    plan.inCell[i] = 1;
                int* b = plan.box[layer];
                if (plan.blitAt[layer] < 0)
                {
                    plan.blitAt[layer] = static_cast<int> (k);
                    b[0] = c.x0; b[1] = c.y0; b[2] = c.x1; b[3] = c.y1;
                }
                else
                {
                    b[0] = std::min (b[0], c.x0); b[1] = std::min (b[1], c.y0); b[2] = std::max (b[2], c.x1); b[3] = std::max (b[3], c.y1);
                }
                plan.layer[layer] = true;
                seen[layer] = true;
                plan.cells.push_back (c);
                k = e;
                break;
            }
            case kOpNone: case kOpClear: case kOpClip: case kOpStyle: case kOpPaint: case kOpCurve: case kOpCellEnd:
                break;
            default:
                if (layer < 2 && seen[layer])
                    ++layer;
                break;
        }
    }
    return ! plan.cells.empty();
}
}

inline bool planCells (const SceneData& s, CellPlan& plan)
{
    plan = CellPlan();
    if (detail::planCellsImpl (s, plan))
        return true;
    plan = CellPlan();
    return false;
}

/** Merges the overlapping boxes; when more than `maxBoxes` are left they become one (the union). */
inline void mergeBoxes (std::vector<DamageRect>& out, size_t maxBoxes)
{
    bool merged = true;
    while (merged)
    {
        merged = false;
        for (size_t i = 0; i < out.size() && ! merged; ++i)
            for (size_t j = i + 1; j < out.size() && ! merged; ++j)
                if (out[i].x0 < out[j].x1 && out[j].x0 < out[i].x1 && out[i].y0 < out[j].y1 && out[j].y0 < out[i].y1)
                {
                    out[i] = { std::min (out[i].x0, out[j].x0), std::min (out[i].y0, out[j].y0), std::max (out[i].x1, out[j].x1), std::max (out[i].y1, out[j].y1) };
                    out.erase (out.begin() + static_cast<long> (j));
                    merged = true;
                }
    }
    if (out.size() > maxBoxes && ! out.empty())
    {
        DamageRect u = out[0];
        for (const DamageRect& r : out)
            u = { std::min (u.x0, r.x0), std::min (u.y0, r.y0), std::max (u.x1, r.x1), std::max (u.y1, r.y1) };
        out.assign (1, u);
    }
}

/** The part of the picture to paint when the back buffer holds the picture `age` pictures ago (EGL_EXT_buffer_age): what changed in this picture (`now`) and in the `age - 1` pictures before it (`history`, the
    newest first; `historyCount` of them are valid). Empty result + `full` true: the whole picture (the age is unknown, 0, or older than the history). */
inline void framePaint (const std::vector<DamageRect>& now, const std::vector<DamageRect>* history, int historyCount, int age, std::vector<DamageRect>& out, bool& full, size_t maxBoxes = 4)
{
    out.clear();
    full = age <= 0 || age - 1 > historyCount;
    if (full)
        return;
    out = now;
    for (int i = 0; i < age - 1; ++i)
        out.insert (out.end(), history[i].begin(), history[i].end());
    mergeBoxes (out, maxBoxes);
}

/** The rectangles to draw again in a layer: the cells at the same place in the list whose key differs (both the old and the new rectangle), the cells that came or went; the overlapping boxes are merged,
    and when more than `maxBoxes` are left they become one. Empty: nothing changed. */
inline void layerDamage (const std::vector<CellKey>& before, const std::vector<CellKey>& now, std::vector<DamageRect>& out, size_t maxBoxes = 4)
{
    out.clear();
    for (size_t i = 0; i < std::max (before.size(), now.size()); ++i)
    {
        const bool haveOld = i < before.size(), haveNew = i < now.size();
        if (haveOld && haveNew && before[i] == now[i])
            continue;
        if (haveOld)
            out.push_back ({ before[i].x0, before[i].y0, before[i].x1, before[i].y1 });
        if (haveNew)
            out.push_back ({ now[i].x0, now[i].y0, now[i].x1, now[i].y1 });
    }
    mergeBoxes (out, maxBoxes);
}

}}} // namespace synthyboy_mpc::overlay::gpu
