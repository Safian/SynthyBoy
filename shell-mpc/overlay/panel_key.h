// SynthyBoy - MPC overlay: the key of the host's Q-Link panel. The host draws everything it has into ONE opaque primary plane (no plane lies below its panel), so our picture cannot be stacked under the
// panel; it is keyed instead: it behaves as if it lay under whatever the host draws over the right part of the tab. The host picture under our page is static (the skin's hidden widgets), so it is
// taken once as a REFERENCE when the page is settled; every frame the strip is compared with it. The panel is a full height band whose left edge is a black line (the panel slides, so the line moves):
// the key is the left edge of that band, and the picture is cut there (a clip at that x), the picture right of it is the host's. Pure logic over IFrameSource, no state of the plugin.
// What it does NOT do: a host rectangle that is not a band over most of the strip height (a popup in the middle of the tab) is not keyed; the page is then taken as gone by its markers, as before.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "drm_interfaces.h"
#include "geometry.h"

namespace synthyboy_mpc { namespace overlay {

class PanelKey
{
public:
    static constexpr int kX0 = 840, kX1 = 1280;      // the strip that is read (upright UI pixels): the panel is 255 wide, the margin catches the slide and a shadow
    static constexpr int kStep = 2;                  // every second column: the panel's line is two pixels wide
    static constexpr int kRows = 16, kY0 = 150, kY1 = 726;   // sample rows inside the tab (above its function key bar)
    static constexpr int kCols = (kX1 - kX0) / kStep;
    static constexpr int kTolerance = 3;             // per channel: the host's own pictures differ by less (quantised gradients), the panel by much more
    static constexpr int kGrow = 2;                  // the key lies this many pixels left of the first band column (the anti-aliased border of the band)
    static constexpr int kNoKey = kX1;               // the value of "nothing of the host over the strip"
    static constexpr int kBandRows = (kRows * 7 + 9) / 10;   // a column is part of a band when this many of the rows differ

    struct Result
    {
        bool read = false;     // the framebuffer could be read
        int edge = kNoKey;     // the x the picture is cut at (kNoKey: not cut)
        bool stale = false;    // the band starts at the left end of the strip: the reference is not the picture under us any more (another host page, a new project): take it again
    };

    bool hasReference() const noexcept { return have_; }
    void forget() noexcept { have_ = false; }

    /** Takes the reference: the strip as it is now. The caller makes sure that nothing of the host lies over it (the page is whole, no knob is being turned). */
    bool capture (IFrameSource& src, int rotation)
    {
        int w = 0, h = 0;
        if (! src.begin (w, h))
            return false;
        bool ok = true;
        for (int r = 0; r < kRows && ok; ++r)
            for (int c = 0; c < kCols && ok; ++c)
                ok = sample (src, rotation, w, h, c, r, ref_[r][c]);
        src.end();
        have_ = ok;
        return ok;
    }

    /** Compares the strip with the reference: the left edge of the band the host drew over it. */
    Result measure (IFrameSource& src, int rotation) const
    {
        Result res;
        if (! have_)
            return res;
        int w = 0, h = 0;
        if (! src.begin (w, h))
            return res;
        res.read = true;
        int firstBand = -1;
        for (int c = 0; c < kCols && firstBand < 0; ++c)
        {
            int diff = 0, same = 0;
            for (int r = 0; r < kRows && same <= kRows - kBandRows; ++r)   // a column with more than kRows - kBandRows rows that match is no band: the rest of its rows is not read
            {
                uint32_t v = 0;
                if (! sample (src, rotation, w, h, c, r, v))
                {
                    res.read = false;
                    break;
                }
                const bool d = differs (v, ref_[r][c]);
                diff += d ? 1 : 0;
                same += d ? 0 : 1;
            }
            if (! res.read)
                break;
            if (diff >= kBandRows)
                firstBand = c;
        }
        src.end();
        if (res.read && firstBand >= 0)
        {
            res.edge = std::max (kX0, kX0 + firstBand * kStep - kGrow);
            res.stale = firstBand == 0;
        }
        return res;
    }

    static int rowY (int r) noexcept { return kY0 + (kY1 - kY0) * r / (kRows - 1); }

private:
    static bool sample (IFrameSource& src, int rotation, int w, int h, int c, int r, uint32_t& v)
    {
        int px, py;
        uiPointToPanel (rotation, kX0 + c * kStep, rowY (r), w, h, px, py);
        return src.pixel (px, py, v);
    }
    static bool differs (uint32_t a, uint32_t b) noexcept
    {
        for (int shift = 0; shift <= 16; shift += 8)
            if (std::abs (static_cast<int> ((a >> shift) & 255u) - static_cast<int> ((b >> shift) & 255u)) > kTolerance)
                return true;
        return false;
    }

    uint32_t ref_[kRows][kCols] = {};
    bool have_ = false;
};

}} // namespace synthyboy_mpc::overlay
