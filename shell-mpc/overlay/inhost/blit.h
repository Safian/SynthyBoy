// SynthyBoy - MPC in-host drawing (experimental): the copy of our picture into a buffer of the host. Pure functions over memory, no system calls, no allocation: the hook that calls them runs on the
// host's main thread inside its frame presentation (inhost/runtime.h). The picture is what the overlay's CPU path rendered: premultiplied ARGB8888 in the orientation of the panel (rotateCanvasInto), the
// size of the panel rectangle of the view; the target is a dumb buffer of the host (XR24, write-combined memory: written row by row, read only where the picture is translucent).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../geometry.h"

namespace synthyboy_mpc { namespace overlay { namespace inhost {

enum RowKind : uint8_t { kRowTransparent = 0, kRowOpaque = 1, kRowMixed = 2 };

/** Which rows of a w x h picture (tightly packed) are empty (every alpha 0), solid (every alpha 255) or mixed. Done by the thread that makes the picture, so the hook copies without looking at pixels. */
inline void classifyRows (const uint32_t* pix, int w, int h, uint8_t* kinds) noexcept
{
    for (int y = 0; y < h; ++y)
    {
        const uint32_t* row = pix + static_cast<size_t> (y) * static_cast<size_t> (w);
        uint32_t andA = 0xffu, orA = 0u;
        for (int x = 0; x < w; ++x)
        {
            const uint32_t a = row[x] >> 24;
            andA &= a;
            orA |= a;
        }
        kinds[y] = orA == 0u ? kRowTransparent : (andA == 0xffu ? kRowOpaque : kRowMixed);
    }
}

/** Colour scaled by dim256 / 256 (256 = unchanged), alpha kept. */
inline uint32_t dimPixel (uint32_t px, unsigned dim256) noexcept
{
    if (dim256 >= 256u)
        return px;
    const uint32_t rb = (((px & 0x00ff00ffu) * dim256) >> 8) & 0x00ff00ffu;
    const uint32_t g = (((px & 0x0000ff00u) * dim256) >> 8) & 0x0000ff00u;
    return (px & 0xff000000u) | rb | g;
}

/** `src` (premultiplied) over `dst`, channel by channel: src + dst * (255 - alpha) / 255, saturated; the result is opaque. */
inline uint32_t blendOver (uint32_t src, uint32_t dst) noexcept
{
    const uint32_t inv = 255u - (src >> 24);
    uint32_t out = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        const uint32_t s = (src >> shift) & 255u, d = (dst >> shift) & 255u;
        uint32_t v = s + (d * inv + 127u) / 255u;
        if (v > 255u)
            v = 255u;
        out |= v << shift;
    }
    return out;
}

/** The part of the panel rectangle `panel` that shows the UI to the left of `edgeX` (upright UI pixels; edgeX >= the UI width: all of it). Empty (w or h 0) when nothing is left. */
inline Rect clipToEdge (const Rect& panel, int rotation, int edgeX, int panelW, int panelH) noexcept
{
    const bool sw = rotationSwapsSides (rotation);
    const int uiW = sw ? panelH : panelW, uiH = sw ? panelW : panelH;
    if (edgeX >= uiW)
        return panel;
    const Rect ui { 0, 0, edgeX < 0 ? 0 : edgeX, uiH };
    const Rect keep = uiRectToPanel (rotation, ui, panelW, panelH);
    const int x0 = std::max (panel.x, keep.x), y0 = std::max (panel.y, keep.y);
    const int x1 = std::min (panel.x + panel.w, keep.x + keep.w), y1 = std::min (panel.y + panel.h, keep.y + keep.h);
    Rect r;
    if (x1 > x0 && y1 > y0)
        r = { x0, y0, x1 - x0, y1 - y0 };
    return r;
}

/** A picture to copy: `pix` w x h tightly packed, `kinds` one entry per row, `panel` where it goes on the panel (panel.w == w, panel.h == h). */
struct PictureRef
{
    const uint32_t* pix = nullptr;
    const uint8_t* kinds = nullptr;
    int w = 0, h = 0;
    Rect panel;
};

/** Copies the rows [rowFrom, rowTo) of the picture into the buffer `dst` (rows of `pitchBytes`, `dstW` x `dstH` pixels), only inside `clip` (panel coordinates), darkened by dim256 / 256. Transparent rows are
    skipped, solid rows are written without reading, mixed rows are blended per pixel (a destination read, only where the alpha is neither 0 nor 255). Returns the pixels written. Never reads or writes outside the
    buffer (the rectangles are intersected with it first). */
inline size_t blitRows (uint8_t* dst, size_t pitchBytes, int dstW, int dstH, const PictureRef& p, const Rect& clip, unsigned dim256, int rowFrom, int rowTo) noexcept
{
    if (dst == nullptr || p.pix == nullptr || p.kinds == nullptr || p.w <= 0 || p.h <= 0 || pitchBytes < static_cast<size_t> (dstW) * 4u)
        return 0;
    const int cx0 = std::max ({ clip.x, p.panel.x, 0 }), cx1 = std::min ({ clip.x + clip.w, p.panel.x + p.w, dstW });
    const int cy0 = std::max ({ clip.y, p.panel.y, 0 }), cy1 = std::min ({ clip.y + clip.h, p.panel.y + p.h, dstH });
    if (cx1 <= cx0 || cy1 <= cy0)
        return 0;
    const int r0 = std::max (rowFrom, cy0 - p.panel.y), r1 = std::min (rowTo, cy1 - p.panel.y);
    size_t written = 0;
    const size_t n = static_cast<size_t> (cx1 - cx0);
    for (int r = r0; r < r1; ++r)
    {
        const uint8_t kind = p.kinds[r];
        if (kind == kRowTransparent)
            continue;
        const uint32_t* s = p.pix + static_cast<size_t> (r) * static_cast<size_t> (p.w) + static_cast<size_t> (cx0 - p.panel.x);
        uint32_t* d = reinterpret_cast<uint32_t*> (dst + static_cast<size_t> (p.panel.y + r) * pitchBytes) + cx0;
        if (kind == kRowOpaque)
        {
            if (dim256 >= 256u)
                std::memcpy (d, s, n * 4u);
            else
                for (size_t i = 0; i < n; ++i)
                    d[i] = dimPixel (s[i], dim256);
        }
        else
        {
            for (size_t i = 0; i < n; ++i)
            {
                const uint32_t px = dimPixel (s[i], dim256);   // premultiplied: scaling the colour keeps it a valid premultiplied pixel
                const uint32_t a = px >> 24;
                if (a == 0u)
                    continue;
                d[i] = a == 255u ? px : blendOver (px, d[i]);
            }
        }
        written += n;
    }
    return written;
}

}}} // namespace synthyboy_mpc::overlay::inhost
