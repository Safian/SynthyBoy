// SynthyBoy - MPC in-host drawing (experimental): the copy of our picture into a buffer of the host. Pure functions over memory, no system calls, no allocation: the hook that calls them runs on the
// host's main thread inside its frame presentation (inhost/runtime.h). The picture is what the overlay's CPU path rendered: premultiplied ARGB8888 in the orientation of the panel (rotateCanvasInto), the
// size of the panel rectangle of the view; the target is a dumb buffer of the host (XR24, write-combined memory: written row by row, read only where the picture is translucent).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../geometry.h"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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

/** The ways to copy a row span into write-combined memory (bytes: a multiple of 4). Chosen at run time (a file, see runtime.h) so the fastest one measured on the device can be selected without a build; the default is
    plain memcpy, which is what measured 4.5 ms for the page region on the Key 61 (tools/ihbench measures the others). */
enum class CopyKind : int { Memcpy = 0, Neon64 = 1, Neon64NoPrefetch = 2, Neon128 = 3, U32 = 4, U32Prefetch = 5 };
inline void copyMemcpy (void* d, const void* s, size_t n) noexcept { std::memcpy (d, s, n); }
inline void copyU32 (void* dst, const void* src, size_t n, bool prefetch) noexcept
{
    uint32_t* d = static_cast<uint32_t*> (dst);
    const uint32_t* s = static_cast<const uint32_t*> (src);
    for (size_t i = 0; i < n / 4; ++i)
    {
        if (prefetch && (i & 15u) == 0)
            __builtin_prefetch (s + i + 64);
        d[i] = s[i];
    }
}
#if defined(__ARM_NEON)
template <int kBlock, bool kPrefetch>
inline void copyNeon (void* dst, const void* src, size_t bytes) noexcept
{
    uint8_t* d = static_cast<uint8_t*> (dst);
    const uint8_t* s = static_cast<const uint8_t*> (src);
    while (bytes >= 4 && (reinterpret_cast<uintptr_t> (d) & 15u) != 0)
    {
        std::memcpy (d, s, 4);
        d += 4; s += 4; bytes -= 4;
    }
    while (bytes >= static_cast<size_t> (kBlock))
    {
        if (kPrefetch)
            __builtin_prefetch (s + 256);
        for (int o = 0; o < kBlock; o += 64)
        {
            const uint32x4_t a = vld1q_u32 (reinterpret_cast<const uint32_t*> (s + o)), b = vld1q_u32 (reinterpret_cast<const uint32_t*> (s + o + 16));
            const uint32x4_t c = vld1q_u32 (reinterpret_cast<const uint32_t*> (s + o + 32)), e = vld1q_u32 (reinterpret_cast<const uint32_t*> (s + o + 48));
            vst1q_u32 (reinterpret_cast<uint32_t*> (d + o), a);
            vst1q_u32 (reinterpret_cast<uint32_t*> (d + o + 16), b);
            vst1q_u32 (reinterpret_cast<uint32_t*> (d + o + 32), c);
            vst1q_u32 (reinterpret_cast<uint32_t*> (d + o + 48), e);
        }
        d += kBlock; s += kBlock; bytes -= static_cast<size_t> (kBlock);
    }
    if (bytes != 0)
        std::memcpy (d, s, bytes);
}
#endif
inline void copyKind (CopyKind k, void* d, const void* s, size_t n) noexcept
{
    switch (k)
    {
#if defined(__ARM_NEON)
        case CopyKind::Neon64: copyNeon<64, true> (d, s, n); return;
        case CopyKind::Neon64NoPrefetch: copyNeon<64, false> (d, s, n); return;
        case CopyKind::Neon128: copyNeon<128, true> (d, s, n); return;
#endif
        case CopyKind::U32: copyU32 (d, s, n, false); return;
        case CopyKind::U32Prefetch: copyU32 (d, s, n, true); return;
        default: copyMemcpy (d, s, n); return;
    }
}
/** The variant in use (set once at start by the runtime from the file; read in the hook). */
inline std::atomic<int>& copyChoice() noexcept { static std::atomic<int> c { 0 }; return c; }
inline void copyToWc (void* dst, const void* src, size_t bytes) noexcept { copyKind (static_cast<CopyKind> (copyChoice().load (std::memory_order_relaxed)), dst, src, bytes); }

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
                copyToWc (d, s, n * 4u);
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

/** Fills the rows [rowFrom, rowTo) of the panel rectangle `r` (clipped to `clip` and the buffer) with one colour (a page's background, darkened by dim256 / 256). */
inline void fillRect (uint8_t* dst, size_t pitchBytes, int dstW, int dstH, const Rect& r, const Rect& clip, uint32_t rgb, unsigned dim256) noexcept
{
    if (dst == nullptr)
        return;
    const int x0 = std::max ({ r.x, clip.x, 0 }), x1 = std::min ({ r.x + r.w, clip.x + clip.w, dstW });
    const int y0 = std::max ({ r.y, clip.y, 0 }), y1 = std::min ({ r.y + r.h, clip.y + clip.h, dstH });
    if (x1 <= x0 || y1 <= y0)
        return;
    const uint32_t px = dimPixel (0xff000000u | (rgb & 0xffffffu), dim256);
    for (int y = y0; y < y1; ++y)
    {
        uint32_t* d = reinterpret_cast<uint32_t*> (dst + static_cast<size_t> (y) * pitchBytes) + x0;
        for (int x = 0; x < x1 - x0; ++x)
            d[x] = px;
    }
}

}}} // namespace synthyboy_mpc::overlay::inhost
