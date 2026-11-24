// SynthyBoy - transparent cells in an overlay-painted panel.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include "core.h"

namespace synthyboy_mpc::ui
{
/** Subdivide one rectangle into an uncovered set of horizontal strips, with
    gaps for each native cell. Rendering and input use those same cell bounds. */
template <size_t N, class Draw>
void outsideNativeCells (Rect base, const std::array<Rect, N>& cells, Draw&& draw)
{
    std::array<int, 2 + 2 * N> edges;
    unsigned n = 0;
    edges[n++] = base.y;
    edges[n++] = base.bottom();
    for (const Rect& cell : cells)
        if (base.overlaps (cell))
        {
            edges[n++] = std::max (base.y, cell.y);
            edges[n++] = std::min (base.bottom(), cell.bottom());
        }
    std::sort (edges.begin(), edges.begin() + n);
    for (unsigned row = 1; row < n; ++row)
    {
        const int y = edges[row - 1], h = edges[row] - y;
        if (h == 0)
            continue;
        std::array<Rect, N> rowCells;
        unsigned count = 0;
        for (const Rect& cell : cells)
            if (cell.x < base.right() && cell.right() > base.x && cell.y <= y && cell.bottom() >= y + h)
                rowCells[count++] = cell;
        std::sort (rowCells.begin(), rowCells.begin() + count, [] (Rect a, Rect b) { return a.x < b.x; });
        int x = base.x;
        for (unsigned i = 0; i < count; ++i)
        {
            const Rect& cell = rowCells[i];
            if (cell.x > x)
                draw (Rect { x, y, cell.x - x, h });
            x = std::max (x, cell.right());
        }
        if (x < base.right())
            draw (Rect { x, y, base.right() - x, h });
    }
}
/** Let the host paint this control above an already composed panel. */
inline void clearNativeCell (gpu::SceneBuilder& sb, Rect cell)
{
    if (cell.empty())
        return;
    gpu::ScopedClip clip (sb, cell.x, cell.y, cell.right(), cell.bottom());
    if (clip)
        sb.clear (0);
}

/** The same solid canvas as the host shows through a transparent native icon slot. */
inline void drawNativeGraphFrame (Pen& pen, Rect graph)
{
    const auto& look = theme::L();
    pen.rrect (graph, look.rBlock, theme::rgba (theme::kNativeCanvas));
    pen.rrect (graph, look.rBlock, pen.c (look.keyline, 255), look.frame);
}

/** Keep the same background gradient and border outside the cut-outs. */
template <size_t N, class Draw>
void paintAroundNativeCells (gpu::SceneBuilder& sb, Rect base, const std::array<Rect, N>& cells, Draw&& draw)
{
    outsideNativeCells (base, cells, [&] (Rect strip)
    {
        Cell cell (sb, strip, 0);
        gpu::ScopedClip clip (sb, strip.x, strip.y, strip.right(), strip.bottom());
        if (!clip) return;
        draw();
    });
}
} // namespace synthyboy_mpc::ui
