// SynthyBoy - MPC overlay tests: a reference interpreter of the GPU scene (gpu/scene.h) with the CPU drawing helpers (draw.h): what the display list MEANS, pixel by pixel. The view
// tests compare it with the picture the CPU renderer makes of the same data, which proves that the scene (the geometry, the order, the colours, the data) describes the same
// picture. What the helper's shaders make of the scene is checked on the device (read-back of the finished frame).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <vector>

#include "../draw.h"
#include "../gpu/scene.h"

namespace ovt {

inline uint32_t sceneColour (uint32_t rgba)
{
    return synthyboy_mpc::overlay::draw::premultiplied (static_cast<int> ((rgba >> 24) & 255u), static_cast<int> ((rgba >> 16) & 255u), static_cast<int> ((rgba >> 8) & 255u), static_cast<int> (rgba & 255u));
}

inline void renderSceneReference (const synthyboy_mpc::overlay::gpu::SceneData& s, std::vector<uint32_t>& cv)
{
    using namespace synthyboy_mpc::overlay;
    using namespace synthyboy_mpc::overlay::gpu;
    const int w = s.w, h = s.h;
    cv.assign (static_cast<size_t> (w) * static_cast<size_t> (h), 0u);
    std::vector<double> ys;
    bool haveColumns = false;
    auto columns = [&]
    {
        if (! haveColumns)
        {
            draw::sampleColumns (ys, s.y, static_cast<int> (s.numPoints), w, h, [] (double v) { return v; }, 4);
            haveColumns = true;
        }
    };
    for (uint32_t k = 0; k < s.numOps; ++k)
    {
        const SceneOp& o = s.ops[k];
        const uint32_t c = sceneColour (o.rgba);
        switch (o.op)
        {
            case kOpClear: draw::setRect (cv.data(), w, h, 0, 0, w, h, c); break;
            case kOpRect: draw::fillRect (cv.data(), w, h, static_cast<int> (o.f[0]), static_cast<int> (o.f[1]), static_cast<int> (o.f[2]), static_cast<int> (o.f[3]), c); break;
            case kOpVLine: draw::vline (cv.data(), w, h, o.f[0], static_cast<int> (o.f[1]), static_cast<int> (o.f[2]), o.f[3], c); break;
            case kOpDisc: draw::disc (cv.data(), w, h, o.f[0], o.f[1], o.f[2], c); break;
            case kOpFill: if (s.numPoints >= 2) { columns(); draw::fillToBaseline (cv.data(), w, h, ys, 4, o.f[0], c); } break;
            case kOpFillGradient:
                if (s.numPoints >= 2)
                {
                    columns();
                    draw::fillToBaselineGradient (cv.data(), w, h, ys, 4, o.f[0], c, sceneColour (o.rgba2), static_cast<double> (o.f[1]), static_cast<double> (o.f[2]));
                }
                break;
            case kOpStroke: if (s.numPoints >= 2) { columns(); draw::strokeCurve (cv.data(), w, h, ys, 4, o.f[0], c); } break;
            default: break;
        }
    }
}

} // namespace ovt
