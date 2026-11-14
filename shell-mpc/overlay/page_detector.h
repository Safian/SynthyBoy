// SynthyBoy - MPC overlay: the page detector. "A view of OUR skin is on screen and not covered" = both marker pictures of that view (two small solid squares of colours
// that no other view uses, inside its region, shell-mpc/overlay/overlay_layout.json) are in the application's primary framebuffer. The overlay lives on another plane, so the markers
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
    static constexpr int kMaxViews = 16;
    bool readable = false;           // the framebuffer could be read
    int view = -1;                   // the first view whose two markers are both on screen, -1 when none
    uint32_t seen[kMaxViews][2] = {};   // the pixel at the centre of every marker (0xRRGGBB; 0xffffffff when it could not be read), for the status file
    int count = 0;                   // views checked
};

/** Looks for the marker pairs of all views (two markers each) in the primary framebuffer. The colours of the pairs differ from one another by more than the tolerance, so at most one
    view matches: the one whose tab is on screen. */
class PageDetector
{
public:
    static constexpr int kTolerance = 2;   // per channel

    PageDetector (IFrameSource& source, const MarkerSpec* markers, int numViews)
        : source_ (source), markers_ (markers), count_ (numViews > Detection::kMaxViews ? Detection::kMaxViews : numViews) {}

    /** Reads the sample pixels of every marker (the four corners of an inset square and the centre) through the shared UI -> panel mapping. */
    Detection check (int rotation)
    {
        Detection d;
        d.count = count_;
        int w = 0, h = 0;
        for (int i = 0; i < count_; ++i)
            d.seen[i][0] = d.seen[i][1] = 0xffffffffu;
        if (count_ <= 0 || ! source_.begin (w, h))
            return d;
        d.readable = true;
        for (int i = 0; i < count_; ++i)
        {
            bool all = true;
            for (int m = 0; m < 2; ++m)
            {
                const MarkerSpec& spec = markers_[i * 2 + m];
                const Rect& r = spec.rect;
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
                        d.seen[i][m] = ok ? v : 0xffffffffu;
                    if (! ok || ! close (v, spec.rgb))
                        all = false;
                }
            }
            if (all && d.view < 0)
                d.view = i;
        }
        source_.end();
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
