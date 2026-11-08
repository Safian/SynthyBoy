// SynthyBoy - core/shapes: free-form shape evaluator and table baker.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/shape.h"

#include <algorithm>
#include <cmath>

namespace synthyboy::shapes
{
namespace
{
inline float sanitizeOutput (float y) noexcept
{
    return std::isfinite (y) ? flushDenormal (y) : 0.0f;
}
}

float powerScale (float t, float power) noexcept
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    if (std::fabs (power) < 0.001f)
        return t;

    float p = power;
    if (p > 25.0f) p = 25.0f;
    if (p < -25.0f) p = -25.0f;

    const float num = std::expm1 (p * t);
    const float den = std::expm1 (p);
    if (std::fabs (den) < 1.0e-7f)
        return t;

    const float val = num / den;
    return (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);
}

void ShapeData::initLinear() noexcept
{
    numPoints = 2;
    points[0] = { 0.0f, 0.0f, 0.0f, false, false };
    points[1] = { 1.0f, 1.0f, 0.0f, false, false };
}

void ShapeData::initTriangle() noexcept
{
    numPoints = 3;
    points[0] = { 0.0f, 0.0f, 0.0f, false, false };
    points[1] = { 0.5f, 1.0f, 0.0f, false, false };
    points[2] = { 1.0f, 0.0f, 0.0f, false, false };
}

void ShapeData::initSquare() noexcept
{
    numPoints = 4;
    points[0] = { 0.0f, 1.0f, 0.0f, false, false };
    points[1] = { 0.5f, 1.0f, 0.0f, false, false };
    points[2] = { 0.5f, 0.0f, 0.0f, false, false };
    points[3] = { 1.0f, 0.0f, 0.0f, false, false };
}

void ShapeData::initSine() noexcept
{
    numPoints = 3;
    points[0] = { 0.0f, 0.0f, 0.0f, false, true };
    points[1] = { 0.5f, 1.0f, 0.0f, false, true };
    points[2] = { 1.0f, 0.0f, 0.0f, false, true };
}

bool ShapeData::isValid() const noexcept
{
    if (numPoints < kMinPoints || numPoints > kMaxPoints)
        return false;

    for (size_t i = 0; i < numPoints; ++i)
    {
        if (! std::isfinite (points[i].x) || ! std::isfinite (points[i].y) || ! std::isfinite (points[i].curve))
            return false;
        if (points[i].x < 0.0f || points[i].x > 1.0f)
            return false;
    }

    for (size_t i = 0; i + 1 < numPoints; ++i)
    {
        if (points[i].x > points[i + 1].x)
            return false;
    }

    if (points[0].x != 0.0f || points[numPoints - 1].x != 1.0f)
        return false;

    return true;
}

void ShapeData::sanitize() noexcept
{
    if (numPoints < kMinPoints)
    {
        initLinear();
        return;
    }

    if (numPoints > kMaxPoints)
        numPoints = kMaxPoints;

    for (size_t i = 0; i < numPoints; ++i)
    {
        if (! std::isfinite (points[i].x))
            points[i].x = static_cast<float> (i) / static_cast<float> (numPoints - 1);
        if (! std::isfinite (points[i].y))
            points[i].y = 0.0f;
        if (! std::isfinite (points[i].curve))
            points[i].curve = 0.0f;

        points[i].x = std::clamp (points[i].x, 0.0f, 1.0f);
        points[i].curve = std::clamp (points[i].curve, -25.0f, 25.0f);
    }

    std::stable_sort (points.begin(), points.begin() + static_cast<ptrdiff_t> (numPoints),
                      [] (const Point& a, const Point& b) { return a.x < b.x; });

    points[0].x = 0.0f;
    points[numPoints - 1].x = 1.0f;

    for (size_t i = 0; i + 1 < numPoints; ++i)
    {
        if (points[i].x > points[i + 1].x)
            points[i + 1].x = points[i].x;
    }
}

float ShapeData::evaluate (float phase) const noexcept
{
    if (numPoints == 0)
        return 0.0f;

    if (! std::isfinite (phase))
        phase = 0.0f;

    phase = std::clamp (phase, 0.0f, 1.0f);

    if (numPoints == 1)
        return sanitizeOutput (points[0].y);

    if (phase <= points[0].x)
    {
        size_t idx = 0;
        while (idx + 1 < numPoints && points[idx + 1].x <= phase)
            ++idx;
        return sanitizeOutput (points[idx].y);
    }

    if (phase >= points[numPoints - 1].x)
        return sanitizeOutput (points[numPoints - 1].y);

    // Binary search for the right segment
    size_t low = 0;
    size_t high = numPoints - 1;
    while (low + 1 < high)
    {
        const size_t mid = low + (high - low) / 2;
        if (points[mid].x <= phase)
            low = mid;
        else
            high = mid;
    }

    // In case of duplicate x points (vertical step) at the exact boundary
    while (low + 1 < numPoints - 1 && points[low + 1].x <= phase && points[low].x == points[low + 1].x)
        ++low;

    const Point& p0 = points[low];
    const Point& p1 = points[low + 1];

    const float dx = p1.x - p0.x;
    if (dx <= 1.0e-7f)
        return sanitizeOutput (p1.y);

    float t = (phase - p0.x) / dx;
    t = std::clamp (t, 0.0f, 1.0f);

    if (p0.step)
        return sanitizeOutput (t < 1.0f ? p0.y : p1.y);

    if (p0.smooth)
        t = 0.5f * (1.0f - std::cos (3.14159265358979323846f * t));

    t = powerScale (t, p0.curve);
    const float y = p0.y + t * (p1.y - p0.y);
    return sanitizeOutput (y);
}

void ShapeData::bake (float* destinationTable, size_t size) const noexcept
{
    if (destinationTable == nullptr || size == 0)
        return;

    if (size == 1)
    {
        destinationTable[0] = evaluate (0.0f);
        return;
    }

    const float step = 1.0f / static_cast<float> (size - 1);
    for (size_t i = 0; i < size; ++i)
    {
        const float phase = static_cast<float> (i) * step;
        destinationTable[i] = evaluate (phase);
    }
}

std::array<float, ShapeData::kDefaultTableSize> ShapeData::bake() const noexcept
{
    std::array<float, kDefaultTableSize> table;
    bake (table.data(), kDefaultTableSize);
    return table;
}

float ShapeData::evaluateTable (const float* table, size_t size, float phase) noexcept
{
    if (table == nullptr || size == 0)
        return 0.0f;

    if (size == 1)
        return table[0];

    phase = std::clamp (phase, 0.0f, 1.0f);
    const float scaled = phase * static_cast<float> (size - 1);
    const size_t idx = static_cast<size_t> (scaled);
    if (idx >= size - 1)
        return table[size - 1];

    const float frac = scaled - static_cast<float> (idx);
    const float val = table[idx] + frac * (table[idx + 1] - table[idx]);
    return flushDenormal (val);
}

} // namespace synthyboy::shapes
