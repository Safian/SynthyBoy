// SynthyBoy - core/shapes tests: per-slot remap curve evaluator (Q20).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/remap.h"
#include "test_runner.h"

#include <cmath>

using namespace synthyboy::shapes;

TEST (remap_curve_unipolar_and_bipolar)
{
    RemapCurve remap;
    remap.initLinear();

    // 1. Unipolar linear
    CHECK (! remap.isBipolar());
    CHECK (! remap.hasCustomShape());
    CHECK_NEAR (remap.evaluate (0.0f), 0.0f, 1.0e-5f);
    CHECK_NEAR (remap.evaluate (0.5f), 0.5f, 1.0e-5f);
    CHECK_NEAR (remap.evaluate (1.0f), 1.0f, 1.0e-5f);

    // 2. Unipolar with power curvature
    remap.setPower (5.0f);
    CHECK (remap.evaluate (0.5f) < 0.5f); // concave downward
    remap.setPower (-5.0f);
    CHECK (remap.evaluate (0.5f) > 0.5f); // convex upward

    // 3. Bipolar linear
    remap.setPower (0.0f);
    remap.setBipolar (true);
    CHECK (remap.isBipolar());
    CHECK_NEAR (remap.evaluate (-1.0f), -1.0f, 1.0e-5f);
    CHECK_NEAR (remap.evaluate (0.0f), 0.0f, 1.0e-5f);
    CHECK_NEAR (remap.evaluate (1.0f), 1.0f, 1.0e-5f);

    // 4. Bipolar with power curvature (sign preserved)
    remap.setPower (4.0f);
    const float posVal = remap.evaluate (0.5f);
    const float negVal = remap.evaluate (-0.5f);
    CHECK_NEAR (posVal, -negVal, 1.0e-5f);

    // 5. Custom shape remap
    ShapeData custom;
    custom.numPoints = 3;
    custom.points[0] = { 0.0f, 0.0f, 0.0f, false, false };
    custom.points[1] = { 0.5f, 0.9f, 0.0f, false, false };
    custom.points[2] = { 1.0f, 1.0f, 0.0f, false, false };

    remap.setCustomShape (custom);
    CHECK (remap.hasCustomShape());

    // In bipolar mode: 0.0 maps to normalized 0.5 -> custom(0.5) = 0.9 -> bipolar 2*0.9 - 1 = 0.8
    CHECK_NEAR (remap.evaluate (0.0f), 0.8f, 1.0e-5f);

    // In unipolar mode:
    remap.setBipolar (false);
    CHECK_NEAR (remap.evaluate (0.5f), 0.9f, 1.0e-5f);

    // 6. Baked remap table
    float table[256];
    remap.bake (table, 256);
    CHECK_NEAR (table[0], 0.0f, 1.0e-5f);
    CHECK_NEAR (table[127], remap.evaluate (127.0f / 255.0f), 1.0e-4f);
    CHECK_NEAR (table[255], 1.0f, 1.0e-5f);
}
