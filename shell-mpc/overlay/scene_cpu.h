// SynthyBoy - MPC overlay: a scene (gpu/scene.h) drawn by the CPU into a landscape canvas of premultiplied ARGB8888 pixels, with the drawing helpers of draw.h. It says what the display
// list MEANS, pixel by pixel: the CPU fallback of the GPU drawing for a scene-only picture (the UI component library, shell-mpc/ui/) and the reference the tests compare the views'
// pictures with (tests/scene_reference.h). What the helper's shaders make of the scene is checked on the device (read-back of the finished frame).
// Not drawn by the CPU: Feedback (it needs the previous picture) and the ops between GlowBegin and GlowEnd (the bloom layer); the GPU has them.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "draw.h"
#include "gpu/scene.h"

namespace synthyboy_mpc { namespace overlay {

inline uint32_t sceneColour (uint32_t rgba)
{
    return synthyboy_mpc::overlay::draw::premultiplied (static_cast<int> ((rgba >> 24) & 255u), static_cast<int> ((rgba >> 16) & 255u), static_cast<int> ((rgba >> 8) & 255u), static_cast<int> (rgba & 255u));
}

namespace scene_cpu
{
using namespace synthyboy_mpc::overlay::gpu;

/** Where an op takes its colour from: its own rgba, or the paint (at the position of the pixel, or the parameter along an arc) times the alpha of the op. */
struct Ink
{
    const PaintState* paint = nullptr;
    float base[4] = {};
    float alpha = 1.0f;
    Ink (const PaintState& p, uint32_t rgba)
    {
        alpha = static_cast<float> (rgba & 255u) / 255.0f;
        if (p.mode != 0)
            paint = &p;
        base[3] = alpha; base[0] = static_cast<float> ((rgba >> 24) & 255u) / 255.0f * alpha; base[1] = static_cast<float> ((rgba >> 16) & 255u) / 255.0f * alpha; base[2] = static_cast<float> ((rgba >> 8) & 255u) / 255.0f * alpha;
    }
    void at (float x, float y, float param, float out[4]) const
    {
        if (! paint)
        {
            for (int i = 0; i < 4; ++i) out[i] = base[i];
            return;
        }
        float t = param;
        if (paint->mode == 1) t = (x - paint->a) / (paint->b != paint->a ? paint->b - paint->a : 1.0f);
        else if (paint->mode == 2) t = (y - paint->a) / (paint->b != paint->a ? paint->b - paint->a : 1.0f);
        paintColour (*paint, t, alpha, out);
    }
};

inline void put (std::vector<uint32_t>& cv, int w, int x, int y, const float c[4], float cov)
{
    if (cov <= 0.0f)
        return;
    uint32_t& d = cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
    d = draw::over (d, draw::packPremul (c), static_cast<int> (cov * 255.0f));
}

/** The pixels of a box, calling fn (x, y, centreX, centreY) for each. */
template <class Fn>
void forBox (int w, int h, float x0, float y0, float x1, float y1, Fn fn)
{
    const int xa = std::max (0, static_cast<int> (std::floor (x0))), xb = std::min (w, static_cast<int> (std::ceil (x1)));
    const int ya = std::max (0, static_cast<int> (std::floor (y0))), yb = std::min (h, static_cast<int> (std::ceil (y1)));
    for (int y = ya; y < yb; ++y)
        for (int x = xa; x < xb; ++x)
            fn (x, y, static_cast<float> (x) + 0.5f, static_cast<float> (y) + 0.5f);
}
} // namespace scene_cpu

inline void renderScene (const gpu::SceneData& s, std::vector<uint32_t>& cv)
{
    using namespace synthyboy_mpc::overlay::gpu;
    using namespace scene_cpu;
    const int w = s.w, h = s.h;
    cv.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
    std::vector<double> ys;
    bool haveColumns = false;
    StyleState style;
    PaintState paint;
    int glowDepth = 0;   // inside GlowBegin .. GlowEnd: not drawn
    // the clip: the picture before the clip is kept, and what the clipped ops drew outside the rectangle is put back when the clip changes or the scene ends
    ClipGeom clipNow {};
    std::vector<uint32_t> beforeClip;
    auto endClip = [&]
    {
        if (! clipNow.on)
            return;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (x < clipNow.x0 || x >= clipNow.x1 || y < clipNow.y0 || y >= clipNow.y1)
                    cv[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)] = beforeClip[static_cast<size_t> (y) * static_cast<size_t> (w) + static_cast<size_t> (x)];
        clipNow.on = false;
    };
    // the current polyline: without a Curve op the whole scene polyline over the full width; a Curve op selects vertices and a span of columns
    int first = 0, count = s.numDefault >= 2 && s.numDefault <= static_cast<uint32_t> (kMaxCurve) ? static_cast<int> (s.numDefault) : 0, x0 = 0, cols = w;
    auto columns = [&]
    {
        if (! haveColumns)
        {
            draw::sampleColumns (ys, s.y + first, count, cols, h, [] (double v) { return v; }, 4);
            haveColumns = true;
        }
    };
    const bool plain = true;
    (void) plain;
    for (uint32_t k = 0; k < s.numOps && k < static_cast<uint32_t> (kMaxOps); ++k)
    {
        const SceneOp& o = s.ops[k];
        if (o.op == kOpClip)
        {
            endClip();
            ClipGeom g;
            if (clipGeom (o, g) && g.on)
            {
                clipNow = g;
                beforeClip = cv;
            }
            continue;
        }
        if (o.op == kOpGlowBegin) { int d; if (glowBeginGeom (o, d)) ++glowDepth; continue; }
        if (o.op == kOpGlowEnd) { if (glowDepth > 0) --glowDepth; continue; }
        if (glowDepth > 0)
            continue;
        const uint32_t c = sceneColour (o.rgba);
        const bool simple = paint.mode == 0 && style.soft <= 1.0f && ! style.round;   // the plain solid ops: the draw.h functions (the views draw the same pictures directly)
        const Ink ink (paint, o.rgba);
        switch (o.op)
        {
            case kOpStyle: applyStyle (o, style); break;
            case kOpPaint: applyPaint (o, paint); break;
            case kOpCellBegin: case kOpCellEnd: style = StyleState(); paint = PaintState(); break;   // a cell starts and ends with the default style and paint (the helper draws it on its own)
            case kOpClear: draw::setRect (cv.data(), w, h, 0, 0, w, h, c); break;
            case kOpRect: draw::fillRect (cv.data(), w, h, static_cast<int> (o.f[0]), static_cast<int> (o.f[1]), static_cast<int> (o.f[2]), static_cast<int> (o.f[3]), c); break;
            case kOpVLine:
                if (simple)
                    draw::vline (cv.data(), w, h, o.f[0], static_cast<int> (o.f[1]), static_cast<int> (o.f[2]), o.f[3], c);
                else if (coordOk (o.f[0]) && coordOk (o.f[1]) && coordOk (o.f[2]) && std::isfinite (o.f[3]) && o.f[3] > 0.0f)
                {
                    const float half = o.f[3] * 0.5f, e = half + style.soft * 0.5f + 1.0f;
                    forBox (w, h, o.f[0] - e, o.f[1], o.f[0] + e, o.f[2], [&] (int x, int y, float cx, float cy)
                    {
                        float col[4];
                        ink.at (cx, cy, 0.0f, col);
                        put (cv, w, x, y, col, edgeCoverage (half - std::fabs (cx - o.f[0]), style.soft));
                    });
                }
                break;
            case kOpDisc:
                if (simple)
                    draw::disc (cv.data(), w, h, o.f[0], o.f[1], o.f[2], c);
                else if (coordOk (o.f[0]) && coordOk (o.f[1]) && std::isfinite (o.f[2]) && o.f[2] > 0.0f && o.f[2] <= kMaxRadius)
                {
                    const float r = o.f[2], e = r + style.soft * 0.5f + 1.0f;
                    forBox (w, h, o.f[0] - e, o.f[1] - e, o.f[0] + e, o.f[1] + e, [&] (int x, int y, float cx, float cy)
                    {
                        float col[4];
                        ink.at (cx, cy, 0.0f, col);
                        put (cv, w, x, y, col, edgeCoverage (r - std::hypot (cx - o.f[0], cy - o.f[1]), style.soft));
                    });
                }
                break;
            case kOpFill:
                if (count >= 2)
                {
                    columns();
                    if (paint.mode == 0 || paint.mode == 3)
                        draw::fillToBaseline (cv.data(), w, h, ys, 4, o.f[0], c, x0, cols);
                    else
                        for (int x = 0; x < cols; ++x)
                        {
                            const double ymid = ys[static_cast<size_t> (x) * 5 + 2], base = o.f[0], lo = std::min (ymid, base), hi = std::max (ymid, base);
                            for (int y = std::max (0, static_cast<int> (std::floor (lo))); y < std::min (h, static_cast<int> (std::ceil (hi))); ++y)
                            {
                                float col[4];
                                ink.at (static_cast<float> (x0 + x) + 0.5f, static_cast<float> (y) + 0.5f, 0.0f, col);
                                put (cv, w, x0 + x, y, col, static_cast<float> (std::clamp (std::min (y + 1.0, hi) - std::max (static_cast<double> (y), lo), 0.0, 1.0)));
                            }
                        }
                }
                break;
            case kOpFillGradient:
                if (count >= 2)
                {
                    columns();
                    draw::fillToBaselineGradient (cv.data(), w, h, ys, 4, o.f[0], c, sceneColour (o.rgba2), static_cast<double> (o.f[1]), static_cast<double> (o.f[2]), x0, cols);
                }
                break;
            case kOpStroke:
                if (count >= 2 && std::isfinite (o.f[0]) && o.f[0] > 0.0f && o.f[0] <= kMaxStroke)
                {
                    columns();
                    if (simple)
                        draw::strokeCurve (cv.data(), w, h, ys, 4, o.f[0], c, x0, cols);
                    else
                    {
                        const double half = o.f[0];
                        for (int x = 0; x < cols; ++x)
                        {
                            const double* cc = &ys[static_cast<size_t> (x) * 5];
                            double ymin = cc[0], ymax = cc[0];
                            for (int q = 1; q <= 4; ++q) { ymin = std::min (ymin, cc[q]); ymax = std::max (ymax, cc[q]); }
                            const double slope = cc[4] - cc[0], norm = std::min (std::sqrt (1.0 + slope * slope), draw::kMaxSlopeNorm), reach = half * norm + style.soft * 0.5 + 1.0;
                            for (int y = std::max (static_cast<int> (std::floor (ymin - reach)), 0); y < std::min (static_cast<int> (std::ceil (ymax + reach)), h); ++y)
                            {
                                const double pc = y + 0.5, dv = pc < ymin ? ymin - pc : (pc > ymax ? pc - ymax : 0.0);
                                float col[4];
                                ink.at (static_cast<float> (x0 + x) + 0.5f, static_cast<float> (y) + 0.5f, 0.0f, col);
                                put (cv, w, x0 + x, y, col, lineCoverage (static_cast<float> (half), static_cast<float> (dv / norm), style.soft));
                            }
                        }
                    }
                }
                break;
            case kOpCurve:
            {
                int f2 = 0, n2 = 0;
                if (curveSpan (o.f[0], o.f[1], f2, n2) && coordOk (o.f[2]) && std::isfinite (o.f[3]) && o.f[3] >= 1.0f && o.f[3] <= kMaxCoord)
                {
                    first = f2; count = n2; x0 = static_cast<int> (o.f[2] + 0.5f); cols = static_cast<int> (o.f[3] + 0.5f);
                }
                else
                    count = 0;
                haveColumns = false;
                break;
            }
            case kOpText:
            {
                TextGeom g;
                if (! textGeom (o, g))
                    break;
                const int n = std::min (g.length, static_cast<int> (s.numText) - g.offset);
                if (n <= 0)
                    break;
                draw::text (cv.data(), w, h, g.x, g.baseline, g.size, s.text + g.offset, n, g.tracking, [&] (float px, float py, float* out) { ink.at (px, py, 0.0f, out); });
                break;
            }
            case kOpLine:
            {
                LineGeom g;
                if (! lineGeom (o, g))
                    break;
                if (simple)
                {
                    draw::line (cv.data(), w, h, g.x0, g.y0, g.x1, g.y1, g.half * 2.0f, c);
                    break;
                }
                const float dx = g.x1 - g.x0, dy = g.y1 - g.y0, len = std::sqrt (dx * dx + dy * dy), ux = dx / len, uy = dy / len, reach = g.half + style.soft * 0.5f + 1.0f;
                forBox (w, h, std::min (g.x0, g.x1) - reach, std::min (g.y0, g.y1) - reach, std::max (g.x0, g.x1) + reach, std::max (g.y0, g.y1) + reach, [&] (int x, int y, float cx, float cy)
                {
                    const float px = cx - g.x0, py = cy - g.y0, t = px * ux + py * uy, sd = std::fabs (-px * uy + py * ux);
                    float dist = sd;
                    if (t < 0.0f || t > len)
                    {
                        if (! style.round)
                            return;
                        const float e = t < 0.0f ? -t : t - len;
                        dist = std::sqrt (sd * sd + e * e);
                    }
                    float col[4];
                    ink.at (cx, cy, 0.0f, col);
                    put (cv, w, x, y, col, edgeCoverage (g.half - dist, style.soft));
                });
                break;
            }
            case kOpArc:
            {
                ArcGeom g;
                if (! arcGeom (o, g))
                    break;
                if (simple)
                {
                    draw::arc (cv.data(), w, h, g.cx, g.cy, g.r, g.half, [&g] (float dx, float dy) { return arcContains (g, dx, dy); }, c);
                    break;
                }
                const float reach = g.r + g.half + style.soft * 0.5f + 1.0f;
                const float e0x = g.cx + g.r * g.u0x, e0y = g.cy + g.r * g.u0y, e1x = g.cx + g.r * g.u1x, e1y = g.cy + g.r * g.u1y;
                forBox (w, h, g.cx - reach, g.cy - reach, g.cx + reach, g.cy + reach, [&] (int x, int y, float cx, float cy)
                {
                    const float dx = cx - g.cx, dy = cy - g.cy;
                    const float ring = lineCoverage (g.half, std::fabs (std::sqrt (dx * dx + dy * dy) - g.r), style.soft);
                    float cov = arcContains (g, dx, dy) ? ring : 0.0f;
                    if (style.round && g.mode != 2)
                    {
                        cov = std::max (cov, lineCoverage (g.half, std::hypot (cx - e0x, cy - e0y), style.soft));
                        cov = std::max (cov, lineCoverage (g.half, std::hypot (cx - e1x, cy - e1y), style.soft));
                    }
                    if (cov <= 0.0f)
                        return;
                    float col[4];
                    ink.at (cx, cy, arcParam (g, dx, dy), col);
                    put (cv, w, x, y, col, cov);
                });
                break;
            }
            case kOpFrame:
            {
                FrameGeom g;
                if (frameGeom (o, g))
                    frameRects (g, [&] (int fx0, int fy0, int fx1, int fy1) { draw::fillRect (cv.data(), w, h, fx0, fy0, fx1, fy1, c); });
                break;
            }
            case kOpRRect:
            {
                RRectGeom g;
                if (! rrectGeom (o, g))
                    break;
                const float e = style.soft * 0.5f + 1.0f;
                forBox (w, h, g.x0 - e, g.y0 - e, g.x1 + e, g.y1 + e, [&] (int x, int y, float cx, float cy)
                {
                    const float cov = rrectCoverage (g, cx, cy, style.soft);
                    if (cov <= 0.0f)
                        return;
                    float col[4];
                    ink.at (cx, cy, 0.0f, col);
                    put (cv, w, x, y, col, cov);
                });
                break;
            }
            case kOpBand:
            {
                BandGeom g;
                if (! bandGeom (o, g))
                    break;
                const int xa = std::max (0, static_cast<int> (std::floor (g.x0))), xb = std::min (w, static_cast<int> (std::ceil (g.x0 + g.width)));
                for (int x = xa; x < xb; ++x)
                {
                    const float pos = std::min (std::max ((static_cast<float> (x) + 0.5f - g.x0 - 0.5f) / std::max (g.width - 1.0f, 1.0f), 0.0f), 1.0f) * static_cast<float> (g.n - 1);
                    int i = static_cast<int> (pos);
                    if (i > g.n - 2) i = g.n - 2;
                    const float f = pos - static_cast<float> (i);
                    const float a = s.y[g.firstA + i] * (1.0f - f) + s.y[g.firstA + i + 1] * f, b = s.y[g.firstB + i] * (1.0f - f) + s.y[g.firstB + i + 1] * f;
                    const float lo = std::min (a, b), hi = std::max (a, b);
                    for (int y = std::max (0, static_cast<int> (std::floor (lo))); y < std::min (h, static_cast<int> (std::ceil (hi))); ++y)
                    {
                        float col[4];
                        ink.at (static_cast<float> (x) + 0.5f, static_cast<float> (y) + 0.5f, 0.0f, col);
                        put (cv, w, x, y, col, bandCoverage (a, b, y));
                    }
                }
                break;
            }
            default: break;
        }
    }
    endClip();
}

}} // namespace synthyboy_mpc::overlay
