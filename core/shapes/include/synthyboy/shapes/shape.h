// SynthyBoy - core/shapes: free-form shape evaluator and table baker.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace synthyboy::shapes
{

/** Flushes subnormal (denormal) float values to zero. */
inline float flushDenormal (float v) noexcept
{
    return (v > -1.0e-15f && v < 1.0e-15f) ? 0.0f : v;
}

/** Computes power-scaled curve mapping t in [0, 1] to [0, 1].
    Mathematically guaranteed to be strictly monotonic for any finite power. */
float powerScale (float t, float power) noexcept;

/** A single control point on a 2D shape graph. */
struct Point
{
    float x = 0.0f;       // Horizontal phase in [0.0, 1.0]
    float y = 0.0f;       // Vertical value (e.g. [0.0, 1.0] or [-1.0, 1.0])
    float curve = 0.0f;   // Curvature of segment to the next point (-20..20, 0 = linear)
    bool step = false;    // Hold y value constant until next point (step mode)
    bool smooth = false;  // Apply S-curve (cosine) smoothing to this segment
};

/** Free-form shape container with point list, validation, evaluation, and baking.
    Point count is capped at 128 (min 2). */
struct ShapeData
{
    static constexpr size_t kMinPoints = 2;
    static constexpr size_t kMaxPoints = 128;
    static constexpr size_t kDefaultTableSize = 2048;

    std::array<Point, kMaxPoints> points = {};
    size_t numPoints = 0;

    /** Initializes to a default linear ramp from (0,0) to (1,1). */
    void initLinear() noexcept;

    /** Initializes to a triangle wave: (0, 0) -> (0.5, 1) -> (1, 0). */
    void initTriangle() noexcept;

    /** Initializes to a square wave: (0, 1) -> (0.5, 1) -> (0.5, 0) -> (1, 0). */
    void initSquare() noexcept;

    /** Initializes to a half-sine wave: (0, 0) -> (0.5, 1) -> (1, 0) with smooth curves. */
    void initSine() noexcept;

    /** Validates point list:
        - numPoints in [2, 128]
        - all values finite
        - x coordinates in [0.0, 1.0] and non-decreasing
        - first point x == 0.0, last point x == 1.0. */
    bool isValid() const noexcept;

    /** Sanitizes point list: clamps count, removes NaNs/Infs, clamps x, sorts by x, sets endpoints. */
    void sanitize() noexcept;

    /** Evaluates shape at given phase [0.0, 1.0].
        Real-time safe (zero allocation, no locks, O(log N) lookup).
        Never crashes or produces NaN/Inf even if called on invalid or fuzzed data. */
    float evaluate (float phase) const noexcept;

    /** Bakes shape into a pre-allocated table of given size (default 2048). */
    void bake (float* destinationTable, size_t size = kDefaultTableSize) const noexcept;

    /** Bakes shape into a fixed 2048-sample array. */
    std::array<float, kDefaultTableSize> bake() const noexcept;

    /** Fast table lookup with linear interpolation. */
    static float evaluateTable (const float* table, size_t size, float phase) noexcept;
};

} // namespace synthyboy::shapes
