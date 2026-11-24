// SynthyBoy - own engine: the value of an LFO shape at a cycle position, exactly as the voice reads it (shared by Voice::lfoValue and the LFO display).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../dsp/util.h"
#include "../matrix/mod_matrix.h"
#include "../voice/own_content.h"
#include "lfo.h"
#include "synthyboy/shapes/shape.h"

namespace synthyboy
{
namespace own
{
/** The custom shape `cs` at u (0 .. 1). `src` holds the source values by source id (the voice's array: 0 .. 1, the LFOs as 0.5 + 0.5 value); a shape without point
    modulation ignores it. `scratch` is the working copy of the points. The points that follow a source move by amount * source (y clamped to 0 .. 1, x between the
    neighbours, the end points keep x): 13-new-keys.md A45. */
inline float evaluateCustomShape (const CompiledShape& cs, const float* src, shapes::ShapeData& scratch, float u) noexcept
{
    if (!cs.anyMod)
        return cs.fixed.evaluate (u);
    auto srcValue = [&] (int choice)
    {
        const float v = src[std::clamp (choice, 0, kNumSourceIds - 1)];
        const bool bipolar = (choice >= kSrcLfo1 && choice < kSrcLfo1 + 8) || (choice >= kSrcRlfo1 && choice < kSrcRlfo1 + 4) || choice == kSrcLfo9 || choice == kSrcLfo10;
        return bipolar ? 2.0f * v - 1.0f : v;
    };
    scratch.numPoints = static_cast<size_t> (cs.num);
    for (int i = 0; i < cs.num; ++i)
    {
        const CompiledPoint& p = cs.points[static_cast<size_t> (i)];
        shapes::Point q = p.point;
        if (p.y.source != 0)
            q.y = clamp01 (q.y + p.y.amount * srcValue (p.y.source));
        if (p.x.source != 0 && i > 0 && i + 1 < cs.num)
            q.x = std::clamp (q.x + p.x.amount * srcValue (p.x.source), scratch.points[static_cast<size_t> (i - 1)].x, cs.points[static_cast<size_t> (i + 1)].point.x);
        if (p.curve.source != 0)
            q.curve = std::clamp (q.curve + 10.0f * p.curve.amount * srcValue (p.curve.source), -20.0f, 20.0f);
        scratch.points[static_cast<size_t> (i)] = q;
    }
    return scratch.evaluate (u);
}

/** Convert a transport position into the abscissa of the authored shape. */
inline float lfoShapePhase (float u, float swing, int dir) noexcept
{
    u = lfoSwing (u, swing);
    if (dir == 1)
        u = 1.0f - u;
    else if (dir == 2)
        u = 1.0f - std::fabs (2.0f * u - 1.0f);
    return u;
}

/** The shape of an LFO at the position u (0 .. 1 after the wrap) as lane value 0 .. 1: swing, direction (0 forward, 1 backward, 2 ping-pong), then the built-in shape or
    the custom one. `wrap` wraps u into the cycle (the free types), otherwise it is clamped to 0 .. 1 (the envelope types). */
inline float lfoShapeAt (int shape, float u, bool wrap, float swing, int dir, const CompiledShape* custom, const float* src, shapes::ShapeData& scratch, uint32_t seed) noexcept
{
    if (wrap)
        u -= ffloor (u);
    else
        u = clamp01 (u);
    u = lfoShapePhase (u, swing, dir);
    if (custom != nullptr)
        return clamp01 (evaluateCustomShape (*custom, src, scratch, u));
    return 0.5f + 0.5f * lfoShape (shape, u, seed);
}
} // namespace own
} // namespace synthyboy
