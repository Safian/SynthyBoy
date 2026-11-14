// SynthyBoy - MPC overlay: a scene (gpu/scene.h) drawn by the CPU into a landscape canvas of premultiplied ARGB8888 pixels, with the drawing helpers of draw.h. It says what the display
// list MEANS, pixel by pixel: the CPU fallback of the GPU drawing for a scene-only picture (the UI component library, shell-mpc/ui/) and the reference the tests compare the views'
// pictures with (tests/scene_reference.h). What the helper's shaders make of the scene is checked on the device (read-back of the finished frame).
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

inline void renderScene (const gpu::SceneData& s, std::vector<uint32_t>& cv)
{
    using namespace synthyboy_mpc::overlay::gpu;
    const int w = s.w, h = s.h;
    cv.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
    std::vector<double> ys;
    bool haveColumns = false;
    // the current polyline: without a Curve op the whole scene polyline over the full width; a Curve op selects vertices and a span of columns
    int first = 0, count = static_cast<int> (s.numPoints), x0 = 0, cols = w;
    auto columns = [&]
    {
        if (! haveColumns)
        {
            draw::sampleColumns (ys, s.y + first, count, cols, h, [] (double v) { return v; }, 4);
            haveColumns = true;
        }
    };
    for (uint32_t k = 0; k < s.numOps && k < static_cast<uint32_t> (kMaxOps); ++k)
    {
        const SceneOp& o = s.ops[k];
        const uint32_t c = sceneColour (o.rgba);
        switch (o.op)
        {
            case kOpClear: draw::setRect (cv.data(), w, h, 0, 0, w, h, c); break;
            case kOpRect: draw::fillRect (cv.data(), w, h, static_cast<int> (o.f[0]), static_cast<int> (o.f[1]), static_cast<int> (o.f[2]), static_cast<int> (o.f[3]), c); break;
            case kOpVLine: draw::vline (cv.data(), w, h, o.f[0], static_cast<int> (o.f[1]), static_cast<int> (o.f[2]), o.f[3], c); break;
            case kOpDisc: draw::disc (cv.data(), w, h, o.f[0], o.f[1], o.f[2], c); break;
            case kOpFill: if (count >= 2) { columns(); draw::fillToBaseline (cv.data(), w, h, ys, 4, o.f[0], c, x0, cols); } break;
            case kOpFillGradient:
                if (count >= 2)
                {
                    columns();
                    draw::fillToBaselineGradient (cv.data(), w, h, ys, 4, o.f[0], c, sceneColour (o.rgba2), static_cast<double> (o.f[1]), static_cast<double> (o.f[2]), x0, cols);
                }
                break;
            case kOpStroke: if (count >= 2) { columns(); draw::strokeCurve (cv.data(), w, h, ys, 4, o.f[0], c, x0, cols); } break;
            case kOpCurve:
                first = static_cast<int> (o.f[0] + 0.5f); count = static_cast<int> (o.f[1] + 0.5f); x0 = static_cast<int> (o.f[2] + 0.5f); cols = static_cast<int> (o.f[3] + 0.5f);
                haveColumns = false;
                break;
            case kOpText:
            {
                char chars[13];
                textOf (o, chars);
                draw::text (cv.data(), w, h, static_cast<int> (o.f[0]), static_cast<int> (o.f[1]), static_cast<int> (o.f[2] + 0.5f), chars, 12, c);
                break;
            }
            case kOpLine:
            {
                LineGeom g;
                if (lineGeom (o, g))
                    draw::line (cv.data(), w, h, g.x0, g.y0, g.x1, g.y1, g.half * 2.0f, c);
                break;
            }
            case kOpArc:
            {
                ArcGeom g;
                if (arcGeom (o, g))
                    draw::arc (cv.data(), w, h, g.cx, g.cy, g.r, g.half, [&g] (float dx, float dy) { return arcContains (g, dx, dy); }, c);
                break;
            }
            case kOpFrame:
            {
                FrameGeom g;
                if (frameGeom (o, g))
                    frameRects (g, [&] (int x0, int y0, int x1, int y1) { draw::fillRect (cv.data(), w, h, x0, y0, x1, y1, c); });
                break;
            }
            default: break;
        }
    }
}

}} // namespace synthyboy_mpc::overlay
