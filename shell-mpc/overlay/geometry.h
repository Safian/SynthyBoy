// SynthyBoy - MPC overlay: the mapping of the landscape UI (1280 x 800) onto the portrait panel (800 x 1280). One function pair, used by the drawing (where the plane
// goes, how the canvas is rotated into the buffer) and by the page detector (where the marker pixels are read), so the two can never disagree.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The panel of both devices is portrait (800 x 1280) and the UI landscape (1280 x 800), so the UI is turned by 90 or 270 degrees; 0 and 180 (UI and panel the same way up) are
// kept for a panel that is landscape. Rotation 90: the primary plane shows UI pixel (X, Y) at panel (Y, panelH - 1 - X): UI top = panel left, UI left = panel bottom (how the MPC
// Key 61 app rotates its UI, and how mpc-tools/shot.sh un-rotates a screenshot). Rotation 270 is the mirror image: UI pixel (X, Y) at panel (panelW - 1 - Y, X) (MPC XL).
// 0: (X, Y) -> (X, Y); 180: (X, Y) -> (panelW - 1 - X, panelH - 1 - Y). The rotation is not a constant of a model: the overlay finds it from the markers of its own skin
// (rotation_finder in page_detector.h) and the override file /tmp/synthyboy-overlay-rot names it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace synthyboy_mpc { namespace overlay {

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool operator== (const Rect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

/** 0, 90, 180 or 270; anything else is read as 90. */
inline int normaliseRotation (int degrees) noexcept { return (degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270) ? degrees : 90; }
/** The landscape canvas and its buffer have swapped sides (the buffer is portrait): 90 and 270. */
inline bool rotationSwapsSides (int rotation) noexcept { const int r = normaliseRotation (rotation); return r == 90 || r == 270; }

/** The panel pixel that shows the UI pixel (x, y). */
inline void uiPointToPanel (int rotation, int x, int y, int panelW, int panelH, int& px, int& py) noexcept
{
    switch (normaliseRotation (rotation))
    {
        case 0:   px = x; py = y; break;
        case 180: px = panelW - 1 - x; py = panelH - 1 - y; break;
        case 270: px = panelW - 1 - y; py = x; break;
        default:  px = y; py = panelH - 1 - x; break;
    }
}

/** The UI pixel shown at the panel pixel (px, py) (the inverse of uiPointToPanel). */
inline void panelPointToUi (int rotation, int px, int py, int panelW, int panelH, int& x, int& y) noexcept
{
    switch (normaliseRotation (rotation))
    {
        case 0:   x = px; y = py; break;
        case 180: x = panelW - 1 - px; y = panelH - 1 - py; break;
        case 270: y = panelW - 1 - px; x = py; break;
        default:  y = px; x = panelH - 1 - py; break;
    }
}

/** The panel rectangle (w and h swapped for 90 / 270) that shows the UI rectangle. */
inline Rect uiRectToPanel (int rotation, const Rect& ui, int panelW, int panelH) noexcept
{
    Rect r;
    switch (normaliseRotation (rotation))
    {
        case 0:   r = ui; break;
        case 180: r.w = ui.w; r.h = ui.h; r.x = panelW - (ui.x + ui.w); r.y = panelH - (ui.y + ui.h); break;
        case 270: r.w = ui.h; r.h = ui.w; r.x = panelW - (ui.y + ui.h); r.y = ui.x; break;
        default:  r.w = ui.h; r.h = ui.w; r.x = ui.y; r.y = panelH - (ui.x + ui.w); break;
    }
    return r;
}

/** Copies the landscape canvas (w x h pixels, tightly packed) into the buffer of the plane (rows of `strideBytes`; h x w pixels for 90 / 270, w x h for 0 / 180), writing the
    destination sequentially (it is write-combined memory on the device). The pixel (cx, cy) of the canvas lands where uiPointToPanel puts it, relative to the panel rectangle of the canvas. */
inline void rotateCanvasInto (int rotation, const uint32_t* canvas, int w, int h, uint32_t* dst, size_t strideBytes) noexcept
{
    const size_t stride = strideBytes / 4;
    const int r = normaliseRotation (rotation);
    if (r == 0 || r == 180)
    {
        for (int by = 0; by < h; ++by)
        {
            uint32_t* d = dst + static_cast<size_t> (by) * stride;
            if (r == 0)
            {
                const uint32_t* s = canvas + static_cast<size_t> (by) * static_cast<size_t> (w);
                for (int bx = 0; bx < w; ++bx)
                    d[bx] = s[bx];
            }
            else
            {
                const uint32_t* s = canvas + static_cast<size_t> (h - 1 - by) * static_cast<size_t> (w);
                for (int bx = 0; bx < w; ++bx)
                    d[bx] = s[w - 1 - bx];
            }
        }
        return;
    }
    const int pw = h, ph = w;                 // the buffer: pw columns, ph rows
    if (r == 90)
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
