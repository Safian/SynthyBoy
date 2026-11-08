// SynthyBoy - core/shapes: per-slot remap curve evaluator (Q20).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/remap.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::shapes
{

void RemapCurve::initLinear() noexcept
{
    shape_.initLinear();
    hasCustomShape_ = false;
    power_ = 0.0f;
    bipolar_ = false;
}

void RemapCurve::setCustomShape (const ShapeData& shape) noexcept
{
    shape_ = shape;
    shape_.sanitize();
    hasCustomShape_ = true;
}

void RemapCurve::clearCustomShape() noexcept
{
    hasCustomShape_ = false;
}

float RemapCurve::evaluate (float input) const noexcept
{
    if (bipolar_)
    {
        input = std::clamp (input, -1.0f, 1.0f);
        if (hasCustomShape_)
        {
            const float norm = 0.5f * input + 0.5f;
            const float val = shape_.evaluate (norm);
            return flushDenormal (2.0f * val - 1.0f);
        }

        const float sign = input < 0.0f ? -1.0f : 1.0f;
        const float mag = std::fabs (input);
        const float out = sign * powerScale (mag, power_);
        return flushDenormal (out);
    }

    input = std::clamp (input, 0.0f, 1.0f);
    if (hasCustomShape_)
        return flushDenormal (shape_.evaluate (input));

    return flushDenormal (powerScale (input, power_));
}

void RemapCurve::bake (float* outTable, size_t size) const noexcept
{
    if (outTable == nullptr || size == 0)
        return;

    if (size == 1)
    {
        outTable[0] = evaluate (bipolar_ ? -1.0f : 0.0f);
        return;
    }

    const float step = 1.0f / static_cast<float> (size - 1);
    for (size_t i = 0; i < size; ++i)
    {
        const float t = static_cast<float> (i) * step;
        const float in = bipolar_ ? (-1.0f + 2.0f * t) : t;
        outTable[i] = evaluate (in);
    }
}

} // namespace synthyboy::shapes
