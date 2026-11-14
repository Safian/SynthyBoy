// SynthyBoy - MPC overlay: drawing helpers on a landscape canvas of premultiplied ARGB8888 pixels (CPU only): blending, rectangles, an anti-aliased curve with a fill
// towards a baseline, a vertical line, a disc. Every view draws with these.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace synthyboy_mpc { namespace overlay { namespace draw {

inline uint32_t premultiplied (int r, int g, int b, int a) noexcept
{
    return (static_cast<uint32_t> (a) << 24) | (static_cast<uint32_t> ((r * a + 127) / 255) << 16) | (static_cast<uint32_t> ((g * a + 127) / 255) << 8) | static_cast<uint32_t> ((b * a + 127) / 255);
}
inline uint32_t opaque (uint32_t rgb) noexcept { return premultiplied (static_cast<int> ((rgb >> 16) & 255), static_cast<int> ((rgb >> 8) & 255), static_cast<int> (rgb & 255), 255); }

/** src over dst, both premultiplied; cov (0 .. 255) scales the source (coverage). */
inline uint32_t over (uint32_t dst, uint32_t src, int cov) noexcept
{
    const uint32_t sa = (src >> 24) * static_cast<uint32_t> (cov) / 255, sr = ((src >> 16) & 255) * static_cast<uint32_t> (cov) / 255,
                   sg = ((src >> 8) & 255) * static_cast<uint32_t> (cov) / 255, sb = (src & 255) * static_cast<uint32_t> (cov) / 255;
    const uint32_t k = 255 - sa;
    const uint32_t a = sa + (dst >> 24) * k / 255, r = sr + ((dst >> 16) & 255) * k / 255, g = sg + ((dst >> 8) & 255) * k / 255, b = sb + (dst & 255) * k / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline void fillRect (uint32_t* cv, int w, int h, int x0, int y0, int x1, int y1, uint32_t c) noexcept
{
    x0 = std::max (x0, 0); y0 = std::max (y0, 0); x1 = std::min (x1, w); y1 = std::min (y1, h);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            uint32_t& d = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
            d = over (d, c, 255);
        }
}

inline void setRect (uint32_t* cv, int w, int h, int x0, int y0, int x1, int y1, uint32_t c) noexcept
{
    x0 = std::max (x0, 0); y0 = std::max (y0, 0); x1 = std::min (x1, w); y1 = std::min (y1, h);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)] = c;
}

/** A filled disc (anti-aliased edge) of radius r around (cx, cy), source over the canvas. */
inline void disc (uint32_t* cv, int w, int h, double cx, double cy, double r, uint32_t c) noexcept
{
    const int x0 = std::max (0, static_cast<int> (std::floor (cx - r - 1.0))), x1 = std::min (w, static_cast<int> (std::ceil (cx + r + 1.0)));
    const int y0 = std::max (0, static_cast<int> (std::floor (cy - r - 1.0))), y1 = std::min (h, static_cast<int> (std::ceil (cy + r + 1.0)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const double d = std::hypot (x + 0.5 - cx, y + 0.5 - cy);
            const double cov = std::clamp (r + 0.5 - d, 0.0, 1.0);
            if (cov > 0.0)
            {
                uint32_t& p = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
                p = over (p, c, static_cast<int> (cov * 255.0));
            }
        }
}

/** A vertical line of the given width centred on x (fractional), from y0 to y1, anti-aliased on its sides. */
inline void vline (uint32_t* cv, int w, int h, double x, int y0, int y1, double width, uint32_t c) noexcept
{
    const int xa = static_cast<int> (std::floor (x - width * 0.5 - 1.0)), xb = static_cast<int> (std::ceil (x + width * 0.5 + 1.0));
    for (int px = std::max (xa, 0); px < std::min (xb, w); ++px)
    {
        const double lo = std::max (px, 0) * 1.0, hi = lo + 1.0;
        const double cov = std::clamp (std::min (hi, x + width * 0.5) - std::max (lo, x - width * 0.5), 0.0, 1.0);
        if (cov <= 0.0)
            continue;
        for (int y = std::max (y0, 0); y < std::min (y1, h); ++y)
        {
            uint32_t& p = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (px)];
            p = over (p, c, static_cast<int> (cov * 255.0));
        }
    }
}

/** The curve y positions (canvas rows) at N + 1 sub-samples of every pixel column: `v` has n values at x = i / (n - 1) over the canvas width; yOf (value) gives the row. */
template <class YOf>
void sampleColumns (std::vector<double>& ys, const float* v, int n, int w, int h, YOf yOf, int subSamples)
{
    ys.resize (static_cast<size_t> (w) * static_cast<size_t> (subSamples + 1) + 1);
    const double scale = static_cast<double> (n - 1) / static_cast<double> (w - 1);
    for (int x = 0; x < w; ++x)
        for (int k = 0; k <= subSamples; ++k)
        {
            const double xp = std::clamp (static_cast<double> (x) + (static_cast<double> (k) / subSamples - 0.5), 0.0, static_cast<double> (w - 1)) * scale;
            int i = static_cast<int> (xp);
            if (i >= n - 1)
                i = n - 2;
            const double f = xp - i;
            const double value = static_cast<double> (v[i]) * (1.0 - f) + static_cast<double> (v[i + 1]) * f;
            ys[static_cast<size_t> (x) * static_cast<size_t> (subSamples + 1) + static_cast<size_t> (k)] = std::clamp (yOf (value), -60.0, h + 60.0);
        }
}

/** The translucent fill between the curve and the row `baseY` (>= h: down to the bottom), anti-aliased on the curve's edge. `ys` from sampleColumns. */
inline void fillToBaseline (uint32_t* cv, int w, int h, const std::vector<double>& ys, int subSamples, double baseY, uint32_t fillC) noexcept
{
    for (int x = 0; x < w; ++x)
    {
        const double ymid = ys[static_cast<size_t> (x) * static_cast<size_t> (subSamples + 1) + static_cast<size_t> (subSamples / 2)];
        const double lo = std::min (ymid, baseY), hi = std::max (ymid, baseY);
        const int from = std::max (0, static_cast<int> (std::floor (lo)));
        const int to = std::min (h, static_cast<int> (std::ceil (hi)));
        uint32_t memoIn = 0, memoOut = 0;   // most pixels of a column are the plain background: the blend of the last distinct value is reused
        bool haveMemo = false;
        for (int y = from; y < to; ++y)
        {
            const double cov = std::clamp (std::min (y + 1.0, hi) - std::max (static_cast<double> (y), lo), 0.0, 1.0);
            if (cov <= 0.0)
                continue;
            uint32_t& d = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
            if (cov >= 1.0)
            {
                if (!haveMemo || d != memoIn)
                {
                    memoIn = d;
                    memoOut = over (d, fillC, 255);
                    haveMemo = true;
                }
                d = memoOut;
            }
            else
                d = over (d, fillC, static_cast<int> (cov * 255.0));
        }
    }
}

/** The anti-aliased line (half width `half` px): coverage from the distance to the curve's vertical extent in the column, corrected by the slope. */
inline void strokeCurve (uint32_t* cv, int w, int h, const std::vector<double>& ys, int subSamples, double half, uint32_t lineC) noexcept
{
    for (int x = 0; x < w; ++x)
    {
        const double* c = &ys[static_cast<size_t> (x) * static_cast<size_t> (subSamples + 1)];
        double ymin = c[0], ymax = c[0];
        for (int k = 1; k <= subSamples; ++k)
        {
            ymin = std::min (ymin, c[k]);
            ymax = std::max (ymax, c[k]);
        }
        const double slope = c[subSamples] - c[0], norm = std::sqrt (1.0 + slope * slope), reach = half * norm + 1.0;
        const int y0 = std::max (static_cast<int> (std::floor (ymin - reach)), 0), y1 = std::min (static_cast<int> (std::ceil (ymax + reach)), h);
        for (int y = y0; y < y1; ++y)
        {
            const double pc = y + 0.5;
            const double dv = pc < ymin ? ymin - pc : (pc > ymax ? pc - ymax : 0.0);
            double cov = half + 0.5 - dv / norm;
            if (cov <= 0.0)
                continue;
            if (cov > 1.0)
                cov = 1.0;
            uint32_t& d = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
            d = over (d, lineC, static_cast<int> (cov * 255.0));
        }
    }
}

}}} // namespace synthyboy_mpc::overlay::draw
