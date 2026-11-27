// SynthyBoy - MPC overlay: the one row layout helper of the pages' native-cell geometry (a row of cells, some of a fixed width, the rest sharing the free width by weight). Pure arithmetic on rectangles: no control, no drawing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>

#include "../ui/model.h"

namespace synthyboy_mpc { namespace overlay { namespace pagegeo {

constexpr int kGap = 4;   // the gap between two cells of a row

/** Lays out `n` cells in the row `r` with `gap` between them: a cell with fixed[i] > 0 has exactly that width, the others share the rest in proportion to weight[i]. */
inline void layoutRow (const ui::Rect& r, const int* fixed, const int* weight, int n, int gap, ui::Rect* out) noexcept
{
    int fixedSum = 0, weightSum = 0;
    for (int i = 0; i < n; ++i)
    {
        fixedSum += fixed[i] > 0 ? fixed[i] : 0;
        weightSum += fixed[i] > 0 ? 0 : weight[i];
    }
    const int free = std::max (0, r.w - fixedSum - gap * (n - 1));
    int x = r.x, usedWeight = 0;
    for (int i = 0; i < n; ++i)
    {
        int w;
        if (fixed[i] > 0)
            w = fixed[i];
        else
        {
            const int before = weightSum > 0 ? free * usedWeight / weightSum : 0;
            usedWeight += weight[i];
            w = (weightSum > 0 ? free * usedWeight / weightSum : 0) - before;
        }
        out[i] = { x, r.y, std::max (0, std::min (w, r.right() - x)), r.h };
        x += w + gap;
    }
}

/** The cells of one titled block: `n` equal cells side by side inside the block (the title strip `titleH` on top, `pad` at the sides and the bottom); the last cell takes the remainder. */
inline void blockCells (const ui::Rect& block, int titleH, int pad, int n, int gap, ui::Rect* out) noexcept
{
    const ui::Rect in { block.x + pad, block.y + titleH, std::max (0, block.w - 2 * pad), std::max (0, block.h - titleH - pad) };
    const int cw = (in.w - gap * (n - 1)) / std::max (1, n);
    for (int i = 0; i < n; ++i)
        out[i] = { in.x + i * (cw + gap), in.y, i == n - 1 ? in.w - i * (cw + gap) : cw, in.h };
}

}}} // namespace synthyboy_mpc::overlay::pagegeo
