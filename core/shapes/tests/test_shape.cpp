// SynthyBoy - core/shapes tests: free-form shape evaluation, monotonicity, edge cases, and fuzzing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/shape.h"
#include "test_runner.h"

#include <cmath>
#include <random>

using namespace synthyboy::shapes;

TEST (shape_values_at_known_phases)
{
    // 1. Linear shape: (0, 0) -> (1, 1)
    ShapeData linear;
    linear.initLinear();
    CHECK (linear.isValid());

    CHECK_NEAR (linear.evaluate (0.0f), 0.0f, 1.0e-5f);
    CHECK_NEAR (linear.evaluate (0.25f), 0.25f, 1.0e-5f);
    CHECK_NEAR (linear.evaluate (0.5f), 0.5f, 1.0e-5f);
    CHECK_NEAR (linear.evaluate (0.75f), 0.75f, 1.0e-5f);
    CHECK_NEAR (linear.evaluate (1.0f), 1.0f, 1.0e-5f);

    // 2. Triangle shape: (0, 0) -> (0.5, 1) -> (1, 0)
    ShapeData triangle;
    triangle.initTriangle();
    CHECK (triangle.isValid());

    CHECK_NEAR (triangle.evaluate (0.0f), 0.0f, 1.0e-5f);
    CHECK_NEAR (triangle.evaluate (0.25f), 0.5f, 1.0e-5f);
    CHECK_NEAR (triangle.evaluate (0.5f), 1.0f, 1.0e-5f);
    CHECK_NEAR (triangle.evaluate (0.75f), 0.5f, 1.0e-5f);
    CHECK_NEAR (triangle.evaluate (1.0f), 0.0f, 1.0e-5f);

    // 3. Square shape with vertical step at 0.5: (0, 1) -> (0.5, 1) -> (0.5, 0) -> (1, 0)
    ShapeData square;
    square.initSquare();
    CHECK (square.isValid());

    CHECK_NEAR (square.evaluate (0.0f), 1.0f, 1.0e-5f);
    CHECK_NEAR (square.evaluate (0.25f), 1.0f, 1.0e-5f);
    CHECK_NEAR (square.evaluate (0.75f), 0.0f, 1.0e-5f);
    CHECK_NEAR (square.evaluate (1.0f), 0.0f, 1.0e-5f);

    // 4. Stepped segment
    ShapeData stepped;
    stepped.numPoints = 2;
    stepped.points[0] = { 0.0f, 0.2f, 0.0f, true, false };
    stepped.points[1] = { 1.0f, 0.8f, 0.0f, false, false };
    CHECK (stepped.isValid());

    CHECK_NEAR (stepped.evaluate (0.0f), 0.2f, 1.0e-5f);
    CHECK_NEAR (stepped.evaluate (0.5f), 0.2f, 1.0e-5f);
    CHECK_NEAR (stepped.evaluate (0.999f), 0.2f, 1.0e-5f);
    CHECK_NEAR (stepped.evaluate (1.0f), 0.8f, 1.0e-5f);

    // 5. Baked table matches direct evaluation
    const auto table = linear.bake();
    for (size_t i = 0; i < ShapeData::kDefaultTableSize; ++i)
    {
        const float phase = static_cast<float> (i) / static_cast<float> (ShapeData::kDefaultTableSize - 1);
        const float direct = linear.evaluate (phase);
        CHECK_NEAR (table[i], direct, 1.0e-5f);
        const float lookup = ShapeData::evaluateTable (table.data(), table.size(), phase);
        CHECK_NEAR (lookup, direct, 1.0e-5f);
    }
}

TEST (shape_monotone_curves_stay_monotone)
{
    const float powers[] = { -20.0f, -10.0f, -5.0f, -1.0f, 0.0f, 1.0f, 5.0f, 10.0f, 20.0f };

    for (float power : powers)
    {
        // Ascending segment
        ShapeData ascending;
        ascending.numPoints = 2;
        ascending.points[0] = { 0.0f, 0.0f, power, false, false };
        ascending.points[1] = { 1.0f, 1.0f, 0.0f, false, false };
        CHECK (ascending.isValid());

        float prevY = -1.0f;
        for (int step = 0; step <= 500; ++step)
        {
            const float phase = static_cast<float> (step) / 500.0f;
            const float y = ascending.evaluate (phase);
            if (step > 0)
            {
                CHECK (y >= prevY - 1.0e-6f);
            }
            prevY = y;
        }

        // Descending segment
        ShapeData descending;
        descending.numPoints = 2;
        descending.points[0] = { 0.0f, 1.0f, power, false, false };
        descending.points[1] = { 1.0f, 0.0f, 0.0f, false, false };
        CHECK (descending.isValid());

        prevY = 2.0f;
        for (int step = 0; step <= 500; ++step)
        {
            const float phase = static_cast<float> (step) / 500.0f;
            const float y = descending.evaluate (phase);
            if (step > 0)
            {
                CHECK (y <= prevY + 1.0e-6f);
            }
            prevY = y;
        }

        // S-curve smooth ascending segment
        ShapeData smoothed;
        smoothed.numPoints = 2;
        smoothed.points[0] = { 0.0f, 0.0f, power, false, true };
        smoothed.points[1] = { 1.0f, 1.0f, 0.0f, false, false };
        CHECK (smoothed.isValid());

        prevY = -1.0f;
        for (int step = 0; step <= 500; ++step)
        {
            const float phase = static_cast<float> (step) / 500.0f;
            const float y = smoothed.evaluate (phase);
            if (step > 0)
            {
                CHECK (y >= prevY - 1.0e-6f);
            }
            prevY = y;
        }
    }
}

TEST (shape_edge_points)
{
    ShapeData shape;
    shape.numPoints = 2;
    shape.points[0] = { 0.0f, 0.25f, 0.0f, false, false };
    shape.points[1] = { 1.0f, 0.75f, 0.0f, false, false };
    CHECK (shape.isValid());

    // Phase < 0.0 clamped to points[0].y
    CHECK_NEAR (shape.evaluate (-1.0f), 0.25f, 1.0e-5f);
    CHECK_NEAR (shape.evaluate (-0.0001f), 0.25f, 1.0e-5f);
    CHECK_NEAR (shape.evaluate (-1.0e6f), 0.25f, 1.0e-5f);

    // Phase > 1.0 clamped to points[last].y
    CHECK_NEAR (shape.evaluate (1.0001f), 0.75f, 1.0e-5f);
    CHECK_NEAR (shape.evaluate (2.0f), 0.75f, 1.0e-5f);
    CHECK_NEAR (shape.evaluate (1.0e6f), 0.75f, 1.0e-5f);

    // Exact boundaries
    CHECK_NEAR (shape.evaluate (0.0f), 0.25f, 1.0e-5f);
    CHECK_NEAR (shape.evaluate (1.0f), 0.75f, 1.0e-5f);

    // Zero-length segment (instantaneous jump)
    ShapeData stepAt0;
    stepAt0.numPoints = 3;
    stepAt0.points[0] = { 0.0f, 0.1f, 0.0f, false, false };
    stepAt0.points[1] = { 0.0f, 0.9f, 0.0f, false, false };
    stepAt0.points[2] = { 1.0f, 1.0f, 0.0f, false, false };
    CHECK (stepAt0.isValid());
    CHECK_NEAR (stepAt0.evaluate (0.0f), 0.9f, 1.0e-5f);

    // Maximum capacity (128 points)
    ShapeData maxPoints;
    maxPoints.numPoints = ShapeData::kMaxPoints;
    for (size_t i = 0; i < ShapeData::kMaxPoints; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (ShapeData::kMaxPoints - 1);
        maxPoints.points[i] = { x, x, 0.0f, false, false };
    }
    CHECK (maxPoints.isValid());
    CHECK_NEAR (maxPoints.evaluate (0.5f), 0.5f, 1.0e-4f);
}

TEST (shape_fuzz_point_lists)
{
    // Test validation constraints
    ShapeData empty;
    empty.numPoints = 0;
    CHECK (! empty.isValid());
    empty.sanitize();
    CHECK (empty.isValid());

    ShapeData singlePoint;
    singlePoint.numPoints = 1;
    singlePoint.points[0] = { 0.0f, 0.5f, 0.0f, false, false };
    CHECK (! singlePoint.isValid());
    singlePoint.sanitize();
    CHECK (singlePoint.isValid());

    ShapeData tooMany;
    tooMany.numPoints = 200;
    CHECK (! tooMany.isValid());
    tooMany.sanitize();
    CHECK (tooMany.numPoints == ShapeData::kMaxPoints);
    CHECK (tooMany.isValid());

    // Non-monotonic x
    ShapeData nonMonotone;
    nonMonotone.numPoints = 3;
    nonMonotone.points[0] = { 0.0f, 0.0f, 0.0f, false, false };
    nonMonotone.points[1] = { 0.8f, 0.5f, 0.0f, false, false };
    nonMonotone.points[2] = { 0.3f, 1.0f, 0.0f, false, false };
    CHECK (! nonMonotone.isValid());
    nonMonotone.sanitize();
    CHECK (nonMonotone.isValid());
    CHECK (nonMonotone.points[0].x <= nonMonotone.points[1].x);
    CHECK (nonMonotone.points[1].x <= nonMonotone.points[2].x);

    // Random fuzzing generator: verify evaluate never crashes and returns finite values
    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> distCoord (-10.0f, 10.0f);
    std::uniform_int_distribution<size_t> distCount (0, 150);

    for (int iter = 0; iter < 1000; ++iter)
    {
        ShapeData fuzzed;
        fuzzed.numPoints = distCount (rng);
        for (size_t i = 0; i < fuzzed.numPoints && i < ShapeData::kMaxPoints; ++i)
        {
            fuzzed.points[i].x = distCoord (rng);
            fuzzed.points[i].y = distCoord (rng);
            fuzzed.points[i].curve = distCoord (rng);
            fuzzed.points[i].step = (rng() & 1) != 0;
            fuzzed.points[i].smooth = (rng() & 1) != 0;
        }

        // Test with NaNs and Infs occasionally
        if (iter % 10 == 0 && fuzzed.numPoints > 0)
        {
            fuzzed.points[0].x = std::numeric_limits<float>::quiet_NaN();
            fuzzed.points[0].y = std::numeric_limits<float>::infinity();
        }

        // Evaluate directly without sanitization - must never crash or produce NaN
        const float valBefore = fuzzed.evaluate (distCoord (rng));
        CHECK (std::isfinite (valBefore));

        // Sanitize and check validity
        fuzzed.sanitize();
        CHECK (fuzzed.isValid());

        const float valAfter = fuzzed.evaluate (distCoord (rng));
        CHECK (std::isfinite (valAfter));
    }
}
