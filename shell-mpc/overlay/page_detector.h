// SynthyBoy - MPC overlay: the page detector. "The curve area of OUR skin is on screen and not covered" = both marker pictures of the skin (two small solid squares of
// unusual colours inside the curve area, shell-mpc/overlay/overlay_layout.json) are in the application's primary framebuffer. The overlay lives on another plane, so the markers
// stay readable while it covers them. A popup, a menu or another tab covers or replaces them, and the overlay goes.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdlib>

#include "drm_interfaces.h"
#include "overlay_layout.h"

namespace synthyboy_mpc { namespace overlay {

struct MarkerSpec
{
    Rect rect;      // landscape UI pixels
    uint32_t rgb;   // 0xRRGGBB
};

struct Detection
{
    bool readable = false;    // the framebuffer could be read
    bool visible = false;     // every sampled pixel of every marker has the colour
    uint32_t seen[8] = {};    // the pixel at the centre of every marker (0xRRGGBB; 0xffffffff when it could not be read), for the status file
    int count = 0;
};

class PageDetector
{
public:
    static constexpr int kMaxMarkers = 8;
    static constexpr int kTolerance = 2;   // per channel

    PageDetector (IFrameSource& source, const MarkerSpec* markers, int count)
        : source_ (source), markers_ (markers), count_ (count > kMaxMarkers ? kMaxMarkers : count) {}

    /** Reads the sample pixels of every marker (the four corners of an inset square and the centre) through the shared UI -> panel mapping. */
    Detection check (int rotation)
    {
        Detection d;
        d.count = count_;
        int w = 0, h = 0;
        for (int i = 0; i < count_; ++i)
            d.seen[i] = 0xffffffffu;
        if (count_ <= 0 || ! source_.begin (w, h))
            return d;
        d.readable = true;
        bool all = true;
        for (int i = 0; i < count_; ++i)
        {
            const Rect& r = markers_[i].rect;
            const int ix = r.w > 3 ? 1 : 0, iy = r.h > 3 ? 1 : 0;
            const int xs[5] = { r.x + ix, r.x + r.w - 1 - ix, r.x + ix, r.x + r.w - 1 - ix, r.x + r.w / 2 };
            const int ys[5] = { r.y + iy, r.y + iy, r.y + r.h - 1 - iy, r.y + r.h - 1 - iy, r.y + r.h / 2 };
            for (int k = 0; k < 5; ++k)
            {
                int px, py;
                uint32_t v = 0;
                uiPointToPanel (rotation, xs[k], ys[k], w, h, px, py);
                const bool ok = source_.pixel (px, py, v);
                if (k == 4)
                    d.seen[i] = ok ? v : 0xffffffffu;
                if (! ok || ! close (v, markers_[i].rgb))
                    all = false;
            }
        }
        source_.end();
        d.visible = all;
        return d;
    }

private:
    static bool close (uint32_t a, uint32_t b) noexcept
    {
        for (int shift = 0; shift <= 16; shift += 8)
            if (std::abs (static_cast<int> ((a >> shift) & 255u) - static_cast<int> ((b >> shift) & 255u)) > kTolerance)
                return false;
        return true;
    }

    IFrameSource& source_;
    const MarkerSpec* markers_;
    int count_;
};

}} // namespace synthyboy_mpc::overlay
