// SynthyBoy - MPC overlay, GPU polyline stroke strip builder: turns an array of 2D points into a triangle strip.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace synthyboy_mpc { namespace overlay { namespace gpu {

/** Builds a 2D triangle strip for a polyline of points.
    For turns within the miter limit (cosHalf >= 0.4), miter vertices are emitted.
    For turns sharper than the limit (cosHalf < 0.4), a 4-vertex SVG bevel join (P ± n1*ext then P ± n2*ext)
    is emitted so that both incoming and outgoing arms maintain full width. */
template <typename Point, typename Emit>
inline void buildStrokeStrip (const Point* pts, size_t m, float half, float soft, Emit&& emit)
{
    if (pts == nullptr || m < 2 || ! std::isfinite (half) || half <= 0.0f)
        return;

    const float ext = half + soft * 0.5f + 1.0f;
    const int count = static_cast<int> (m);

    for (int i = 0; i < count; ++i)
    {
        float n1x = 0.0f, n1y = 0.0f, n2x = 0.0f, n2y = 0.0f;
        if (i > 0)
        {
            const float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
            const float l = std::sqrt (dx * dx + dy * dy);
            if (l > 1e-6f) { n1x = -dy / l; n1y = dx / l; }
        }
        if (i < count - 1)
        {
            const float dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
            const float l = std::sqrt (dx * dx + dy * dy);
            if (l > 1e-6f) { n2x = -dy / l; n2y = dx / l; }
        }
        if (i == 0) { n1x = n2x; n1y = n2y; }
        if (i == count - 1) { n2x = n1x; n2y = n1y; }
        float mx = n1x + n2x, my = n1y + n2y, ml = std::sqrt (mx * mx + my * my);
        if (ml < 1e-4f) { mx = n1x; my = n1y; ml = 1.0f; }
        mx /= ml;
        my /= ml;
        const float cosHalf = mx * n2x + my * n2y;
        if (cosHalf < 0.4f)
        {
            // Bevel join past miter limit 2.5: four vertices at the point, P ± n1*ext then P ± n2*ext
            emit (pts[i].x + n1x * ext, pts[i].y + n1y * ext, ext, 0.0f);
            emit (pts[i].x - n1x * ext, pts[i].y - n1y * ext, -ext, 0.0f);
            emit (pts[i].x + n2x * ext, pts[i].y + n2y * ext, ext, 0.0f);
            emit (pts[i].x - n2x * ext, pts[i].y - n2y * ext, -ext, 0.0f);
        }
        else
        {
            const float k2 = ext / cosHalf;
            emit (pts[i].x + mx * k2, pts[i].y + my * k2, ext, 0.0f);
            emit (pts[i].x - mx * k2, pts[i].y - my * k2, -ext, 0.0f);
        }
    }
}

template <typename Point, typename Emit>
inline void buildStrokeStrip (const std::vector<Point>& pts, float half, float soft, Emit&& emit)
{
    buildStrokeStrip (pts.data(), pts.size(), half, soft, std::forward<Emit> (emit));
}

}}} // namespace synthyboy_mpc::overlay::gpu
