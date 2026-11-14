// SynthyBoy - MPC overlay: the renderer. Draws the filter curve into a landscape canvas of premultiplied ARGB8888 pixels (an opaque dark background): faint vertical grid at
// 100 Hz / 1 kHz / 10 kHz, the 0 dB line, a translucent fill from the curve down to the bottom, an anti-aliased 4 px line. CPU only; a GPU renderer would implement the same
// interface later (Presenter and the controller do not care how the canvas is made).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The frequency axis is logarithmic, 20 Hz at the left edge to 20 kHz at the right edge, point i of the n values at x = i / (n - 1) * (w - 1) (between two points the dB value is
// interpolated linearly). The dB axis: kDbTop at the top margin, kDbBottom at the bottom margin, linear; the 0 dB line is at zeroLineY (). Colours: line #ff2e63, fill #d31145 at
// 25 % alpha (the accent of the skin).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "draw.h"
#include "geometry.h"

namespace synthyboy_mpc { namespace overlay {

class ICurveRenderer
{
public:
    virtual ~ICurveRenderer() = default;
    /** Draws `n` dB values (n >= 2) into canvas (w x h, tightly packed) on an opaque background of the colour `backgroundRgb`, so what the skin shows under the region
        stays hidden (and the skin's marker squares with it). `covers` (rectangles of the canvas) are painted in the background colour again, on top of everything. */
    virtual void render (const float* db, int n, uint32_t* canvas, int w, int h, uint32_t backgroundRgb, const Rect* covers, int numCovers) = 0;
};

class CpuCurveRenderer final : public ICurveRenderer
{
public:
    static constexpr double kDbTop = 30.0;      // dB at the top margin
    static constexpr double kDbBottom = -36.0;  // dB at the bottom margin
    static constexpr double kMargin = 12.0;     // px above kDbTop and below kDbBottom
    static constexpr double kHalfWidth = 2.0;   // line width 4 px
    static constexpr int kCoverGrow = 1;
    static constexpr int kSub = 4;              // curve samples per pixel column

    static uint32_t premultiplied (int r, int g, int b, int a) noexcept { return draw::premultiplied (r, g, b, a); }

    /** y (canvas row, fractional, centre of the pixel row = y + 0.5) of a dB value for a canvas of height h. */
    static double dbToY (double db, int h) noexcept
    {
        const double pxPerDb = (h - 2.0 * kMargin) / (kDbTop - kDbBottom);
        return kMargin + (kDbTop - db) * pxPerDb;
    }
    static double zeroLineY (int h) noexcept { return dbToY (0.0, h); }
    /** x of the grid line of a frequency (Hz) on a canvas of width w. */
    static int gridX (double hz, int w) noexcept { return static_cast<int> (std::lround ((w - 1) * std::log10 (hz / 20.0) / 3.0)); }

    void render (const float* db, int n, uint32_t* cv, int w, int h, uint32_t backgroundRgb, const Rect* covers, int numCovers) override
    {
        const uint32_t bg = draw::opaque (backgroundRgb);
        draw::setRect (cv, w, h, 0, 0, w, h, bg);
        if (n < 2 || w < 2 || h < 8)
            return;
        const uint32_t lineC = premultiplied (0xff, 0x2e, 0x63, 255), fillC = premultiplied (0xd3, 0x11, 0x45, 64);
        const uint32_t gridV = premultiplied (255, 255, 255, 40), grid0 = premultiplied (255, 255, 255, 90);
        // the three decade lines and the 0 dB line
        const double fs[3] = { 100.0, 1000.0, 10000.0 };
        for (double f : fs)
            draw::fillRect (cv, w, h, gridX (f, w), 0, gridX (f, w) + 1, h, gridV);
        const int zy = static_cast<int> (zeroLineY (h));
        draw::fillRect (cv, w, h, 0, zy, w, zy + 1, grid0);
        draw::sampleColumns (ys_, db, n, w, h, [h] (double v) { return dbToY (v, h); }, kSub);
        draw::fillToBaseline (cv, w, h, ys_, kSub, h, fillC);   // down to the bottom
        for (int i = 0; i < numCovers; ++i)
            draw::setRect (cv, w, h, covers[i].x - kCoverGrow, covers[i].y - kCoverGrow, covers[i].x + covers[i].w + kCoverGrow, covers[i].y + covers[i].h + kCoverGrow, bg);
        draw::strokeCurve (cv, w, h, ys_, kSub, kHalfWidth, lineC);
    }

private:
    std::vector<double> ys_;
};

}} // namespace synthyboy_mpc::overlay
