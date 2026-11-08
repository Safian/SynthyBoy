// SynthyBoy - wavetable-edit: Drawing tools implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/wavetable_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace synthyboy::wtedit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Deterministic LCG pseudo-random generator
float pseudoRandom(uint32_t& seed) noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 9) / 8388608.0f; // in [0, 1)
}
} // namespace

void applyDrawingTool(Frame& frame, DrawingTool tool, int gridSize, int stepIndex, float param) noexcept
{
    if (tool == DrawingTool::Mirror)
    {
        for (size_t i = 0; i < kWaveformSize / 2; ++i)
        {
            frame[kWaveformSize - 1 - i] = frame[i];
        }
        return;
    }

    if (gridSize <= 0)
        gridSize = 1;
    if (gridSize > static_cast<int>(kWaveformSize))
        gridSize = static_cast<int>(kWaveformSize);

    const size_t stepSize = kWaveformSize / static_cast<size_t>(gridSize);
    if (stepSize == 0)
        return;

    if (stepIndex < 0)
        stepIndex = 0;
    if (stepIndex >= gridSize)
        stepIndex = gridSize - 1;

    const size_t start = static_cast<size_t>(stepIndex) * stepSize;
    const size_t end = (stepIndex == gridSize - 1) ? kWaveformSize : (start + stepSize);
    const size_t len = end - start;
    if (len == 0)
        return;

    const float startVal = (start > 0) ? frame[start - 1] : frame[0];
    const float endVal = (end < kWaveformSize) ? frame[end] : frame[kWaveformSize - 1];

    uint32_t seed = static_cast<uint32_t>(start * 7919u + 12345u);

    for (size_t i = 0; i < len; ++i)
    {
        const double t = (len > 1) ? (static_cast<double>(i) / static_cast<double>(len - 1)) : 0.0;
        float val = frame[start + i];

        switch (tool)
        {
        case DrawingTool::FlatLine:
            val = param;
            break;

        case DrawingTool::Sine:
            val = static_cast<float>(std::sin(2.0 * kPi * t));
            break;

        case DrawingTool::SlopeUp:
            val = static_cast<float>(-1.0 + 2.0 * t);
            break;

        case DrawingTool::SlopeDown:
            val = static_cast<float>(1.0 - 2.0 * t);
            break;

        case DrawingTool::CurveUp:
            // Quarter-sine curve from -1.0 to 1.0
            val = static_cast<float>(-1.0 + 2.0 * std::sin(0.5 * kPi * t));
            break;

        case DrawingTool::CurveDown:
            // Quarter-sine curve from 1.0 to -1.0
            val = static_cast<float>(1.0 - 2.0 * std::sin(0.5 * kPi * t));
            break;

        case DrawingTool::HalfSinePeak:
            // Half-sine arch from 0.0 to 1.0 to 0.0
            val = static_cast<float>(std::sin(kPi * t));
            break;

        case DrawingTool::HalfSineValley:
            // Inverted half-sine arch from 0.0 to -1.0 to 0.0
            val = static_cast<float>(-std::sin(kPi * t));
            break;

        case DrawingTool::InterpolateLinear:
            val = static_cast<float>((1.0 - t) * startVal + t * endVal);
            break;

        case DrawingTool::InterpolateCurved:
        {
            // S-curve smoothstep: 3t^2 - 2t^3
            const double s = t * t * (3.0 - 2.0 * t);
            val = static_cast<float>((1.0 - s) * startVal + s * endVal);
            break;
        }

        case DrawingTool::Nudge:
            val += param;
            break;

        case DrawingTool::Noise:
        {
            const float n = (pseudoRandom(seed) * 2.0f - 1.0f) * param;
            val += n;
            break;
        }

        case DrawingTool::Mirror:
            break;
        }

        // Clamp to [-1.0, 1.0]
        if (val > 1.0f)
            val = 1.0f;
        else if (val < -1.0f)
            val = -1.0f;

        frame[start + i] = val;
    }
}

} // namespace synthyboy::wtedit
