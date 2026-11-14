// SynthyBoy - MPC overlay: the mapping of the landscape UI (1280 x 800) onto the portrait panel (800 x 1280). One function pair, used by the drawing (where the plane
// goes, how the canvas is rotated into the buffer) and by the page detector (where the marker pixels are read), so the two can never disagree.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rotation 90 (the default): the primary plane shows UI pixel (X, Y) at panel (Y, panelH - 1 - X): UI top = panel left, UI left = panel bottom (how the MPC app
// rotates its UI, and how mpc-tools/shot.sh un-rotates a screenshot). Rotation 270 is the mirror image: UI pixel (X, Y) at panel (panelW - 1 - Y, X).
#pragma once

#include <cstddef>
#include <cstdint>

namespace synthyboy_mpc { namespace overlay {

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool operator== (const Rect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

/** 90 or 270; anything else is read as 90. */
inline int normaliseRotation (int degrees) noexcept { return degrees == 270 ? 270 : 90; }

/** The panel pixel that shows the UI pixel (x, y). */
inline void uiPointToPanel (int rotation, int x, int y, int panelW, int panelH, int& px, int& py) noexcept
{
    if (normaliseRotation (rotation) == 90) { px = y; py = panelH - 1 - x; }
    else                                    { px = panelW - 1 - y; py = x; }
}

/** The UI pixel shown at the panel pixel (px, py) (the inverse of uiPointToPanel). */
inline void panelPointToUi (int rotation, int px, int py, int panelW, int panelH, int& x, int& y) noexcept
{
    if (normaliseRotation (rotation) == 90) { y = px; x = panelH - 1 - py; }
    else                                    { y = panelW - 1 - px; x = py; }
}

/** The panel rectangle (w and h swapped) that shows the UI rectangle. */
inline Rect uiRectToPanel (int rotation, const Rect& ui, int panelW, int panelH) noexcept
{
    Rect r;
    r.w = ui.h;
    r.h = ui.w;
    if (normaliseRotation (rotation) == 90) { r.x = ui.y; r.y = panelH - (ui.x + ui.w); }
    else                                    { r.x = panelW - (ui.y + ui.h); r.y = ui.x; }
    return r;
}

/** Copies the landscape canvas (w x h pixels, tightly packed) into a portrait buffer of h x w pixels (rows of `strideBytes`), writing the destination sequentially (it is
    write-combined memory on the device). The pixel (cx, cy) of the canvas lands where uiPointToPanel puts it, relative to the panel rectangle of the canvas. */
inline void rotateCanvasInto (int rotation, const uint32_t* canvas, int w, int h, uint32_t* dst, size_t strideBytes) noexcept
{
    const size_t stride = strideBytes / 4;
    const int pw = h, ph = w;                 // the buffer: pw columns, ph rows
    if (normaliseRotation (rotation) == 90)
        for (int by = 0; by < ph; ++by)
        {
            uint32_t* d = dst + static_cast<size_t> (by) * stride;
            const uint32_t* s = canvas + (w - 1 - by);
            for (int bx = 0; bx < pw; ++bx)
                d[bx] = s[static_cast<size_t> (bx) * static_cast<size_t> (w)];
        }
    else
        for (int by = 0; by < ph; ++by)
        {
            uint32_t* d = dst + static_cast<size_t> (by) * stride;
            const uint32_t* s = canvas + by + static_cast<size_t> (h - 1) * static_cast<size_t> (w);
            for (int bx = 0; bx < pw; ++bx)
                d[bx] = s[-static_cast<std::ptrdiff_t> (bx) * static_cast<std::ptrdiff_t> (w)];
        }
}

}} // namespace synthyboy_mpc::overlay
