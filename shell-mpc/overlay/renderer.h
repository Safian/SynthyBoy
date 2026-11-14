// SynthyBoy - MPC overlay: the renderer. Draws the filter curve into a landscape canvas of premultiplied ARGB8888 pixels (transparent background): faint vertical grid at
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

#include "geometry.h"

namespace synthyboy_mpc { namespace overlay {

class ICurveRenderer
{
public:
    virtual ~ICurveRenderer() = default;
    /** Draws `n` dB values (n >= 2) into canvas (w x h, tightly packed). `covers`: rectangles of the canvas (opaque background colour) that hide the skin's marker pictures. */
    virtual void render (const float* db, int n, uint32_t* canvas, int w, int h, const Rect* covers, int numCovers) = 0;
};

class CpuCurveRenderer final : public ICurveRenderer
{
public:
    static constexpr double kDbTop = 30.0;      // dB at the top margin
    static constexpr double kDbBottom = -36.0;  // dB at the bottom margin
    static constexpr double kMargin = 12.0;     // px above kDbTop and below kDbBottom
    static constexpr double kHalfWidth = 2.0;   // line width 4 px
    static constexpr uint32_t kCoverRgb = 0x1e1d1e;
    static constexpr int kCoverGrow = 1;

    static uint32_t premultiplied (int r, int g, int b, int a) noexcept
    {
        return (static_cast<uint32_t> (a) << 24) | (static_cast<uint32_t> ((r * a + 127) / 255) << 16) | (static_cast<uint32_t> ((g * a + 127) / 255) << 8)
               | static_cast<uint32_t> ((b * a + 127) / 255);
    }

    /** y (canvas row, fractional, centre of the pixel row = y + 0.5) of a dB value for a canvas of height h. */
    static double dbToY (double db, int h) noexcept
    {
        const double pxPerDb = (h - 2.0 * kMargin) / (kDbTop - kDbBottom);
        return kMargin + (kDbTop - db) * pxPerDb;
    }
    static double zeroLineY (int h) noexcept { return dbToY (0.0, h); }
    /** x of the grid line of a frequency (Hz) on a canvas of width w. */
    static int gridX (double hz, int w) noexcept { return static_cast<int> (std::lround ((w - 1) * std::log10 (hz / 20.0) / 3.0)); }

    void render (const float* db, int n, uint32_t* cv, int w, int h, const Rect* covers, int numCovers) override
    {
        std::memset (cv, 0, static_cast<size_t> (w) * static_cast<size_t> (h) * 4);
        if (n < 2 || w < 2 || h < 8)
            return;
        const uint32_t lineC = premultiplied (0xff, 0x2e, 0x63, 255), fillC = premultiplied (0xd3, 0x11, 0x45, 64);
        const uint32_t gridV = premultiplied (255, 255, 255, 40), grid0 = premultiplied (255, 255, 255, 90);
        // the three decade lines and the 0 dB line
        const double fs[3] = { 100.0, 1000.0, 10000.0 };
        for (double f : fs)
            fillRect (cv, w, h, gridX (f, w), 0, gridX (f, w) + 1, h, gridV);
        const int zy = static_cast<int> (zeroLineY (h));
        fillRect (cv, w, h, 0, zy, w, zy + 1, grid0);

        constexpr int N = 4;                       // curve samples per pixel column
        std::vector<double>& ys = ys_;
        ys.resize (static_cast<size_t> (w) * (N + 1) + 1);
        const double scale = static_cast<double> (n - 1) / static_cast<double> (w - 1);
        // y of the curve at the sub-sample positions of every column (x - 0.5 .. x + 0.5), clamped to a band around the canvas
        for (int x = 0; x < w; ++x)
            for (int k = 0; k <= N; ++k)
            {
                const double xp = std::clamp (static_cast<double> (x) + (static_cast<double> (k) / N - 0.5), 0.0, static_cast<double> (w - 1));
                ys[static_cast<size_t> (x) * (N + 1) + static_cast<size_t> (k)] = std::clamp (dbToY (interpolate (db, n, xp * scale), h), -60.0, h + 60.0);
            }
        // the translucent fill below the curve, anti-aliased on the top edge
        for (int x = 0; x < w; ++x)
        {
            const double ymid = ys[static_cast<size_t> (x) * (N + 1) + N / 2];
            int from = static_cast<int> (std::floor (ymid));
            if (from < 0)
                from = 0;
            for (int y = from; y < h; ++y)
            {
                double c = y + 1.0 - ymid;
                if (c <= 0.0)
                    continue;
                uint32_t* d = &cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
                if (c >= 1.0)
                {
                    if (*d == 0)
                    {   // the rest of the column down to the next non-empty pixel: plain stores
                        for (; y < h; ++y)
                        {
                            uint32_t* e = &cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
                            *e = *e == 0 ? fillC : over (*e, fillC, 255);
                        }
                        break;
                    }
                    *d = over (*d, fillC, 255);
                }
                else
                    *d = over (*d, fillC, static_cast<int> (c * 255.0));
            }
        }
        // opaque background over the markers of the skin (the player must not see them)
        const uint32_t coverC = premultiplied ((kCoverRgb >> 16) & 255, (kCoverRgb >> 8) & 255, kCoverRgb & 255, 255);
        for (int i = 0; i < numCovers; ++i)
            fillRectSet (cv, w, h, covers[i].x - kCoverGrow, covers[i].y - kCoverGrow, covers[i].x + covers[i].w + kCoverGrow, covers[i].y + covers[i].h + kCoverGrow, coverC);
        // the 4 px line: coverage from the distance to the curve's vertical extent in the column, corrected by the slope
        for (int x = 0; x < w; ++x)
        {
            const double* c = &ys[static_cast<size_t> (x) * (N + 1)];
            double ymin = c[0], ymax = c[0];
            for (int k = 1; k <= N; ++k)
            {
                ymin = std::min (ymin, c[k]);
                ymax = std::max (ymax, c[k]);
            }
            const double slope = c[N] - c[0], norm = std::sqrt (1.0 + slope * slope), reach = kHalfWidth * norm + 1.0;
            int y0 = static_cast<int> (std::floor (ymin - reach)), y1 = static_cast<int> (std::ceil (ymax + reach));
            y0 = std::max (y0, 0);
            y1 = std::min (y1, h);
            for (int y = y0; y < y1; ++y)
            {
                const double pc = y + 0.5;
                const double dv = pc < ymin ? ymin - pc : (pc > ymax ? pc - ymax : 0.0);
                double cov = kHalfWidth + 0.5 - dv / norm;
                if (cov <= 0.0)
                    continue;
                if (cov > 1.0)
                    cov = 1.0;
                uint32_t& d = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
                d = over (d, lineC, static_cast<int> (cov * 255.0));
            }
        }
    }

private:
    static double interpolate (const float* db, int n, double pos) noexcept
    {
        int i = static_cast<int> (pos);
        if (i >= n - 1)
            i = n - 2;
        const double f = pos - i;
        return static_cast<double> (db[i]) * (1.0 - f) + static_cast<double> (db[i + 1]) * f;
    }

    /** src over dst, both premultiplied; cov (0 .. 255) scales the source (coverage). */
    static uint32_t over (uint32_t dst, uint32_t src, int cov) noexcept
    {
        const uint32_t sa = (src >> 24) * static_cast<uint32_t> (cov) / 255, sr = ((src >> 16) & 255) * static_cast<uint32_t> (cov) / 255,
                       sg = ((src >> 8) & 255) * static_cast<uint32_t> (cov) / 255, sb = (src & 255) * static_cast<uint32_t> (cov) / 255;
        const uint32_t k = 255 - sa;
        const uint32_t a = sa + (dst >> 24) * k / 255, r = sr + ((dst >> 16) & 255) * k / 255, g = sg + ((dst >> 8) & 255) * k / 255, b = sb + (dst & 255) * k / 255;
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    static void fillRect (uint32_t* cv, int w, int h, int x0, int y0, int x1, int y1, uint32_t c) noexcept
    {
        x0 = std::max (x0, 0); y0 = std::max (y0, 0); x1 = std::min (x1, w); y1 = std::min (y1, h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)] = over (cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)], c, 255);
    }

    static void fillRectSet (uint32_t* cv, int w, int h, int x0, int y0, int x1, int y1, uint32_t c) noexcept
    {
        x0 = std::max (x0, 0); y0 = std::max (y0, 0); x1 = std::min (x1, w); y1 = std::min (y1, h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)] = c;
    }

    std::vector<double> ys_;
};

}} // namespace synthyboy_mpc::overlay
