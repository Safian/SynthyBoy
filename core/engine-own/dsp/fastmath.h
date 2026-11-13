// SynthyBoy - own engine: cheaper forms of the library functions that run per sample, used only at tier low (the MPC's Cortex-A17 has no fast libm). Each states its error.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

namespace synthyboy
{
namespace own
{
namespace fastmath
{
/** tanh: [7/6] Pade approximant, absolute error below 1e-4 on the whole line (it is clamped to +-1 beyond |x| = 4.972, where it reaches 1). */
inline float tanh (float x) noexcept
{
    if (x >= 4.972f)
        return 1.0f;
    if (x <= -4.972f)
        return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
}

/** 2^x: the integer part by the exponent bits, 2^f on [-0.5, 0.5] by a degree 6 polynomial; relative error below 3e-9 (under the float rounding) for |x| < 120, 0 / huge outside. */
inline float exp2 (float x) noexcept
{
    if (x < -120.0f)
        return 0.0f;
    if (x > 120.0f)
        x = 120.0f;
    const float r = x < 0.0f ? x - 0.5f : x + 0.5f;
    const int n = static_cast<int> (r); // round to nearest (ties away from zero)
    const float f = x - static_cast<float> (n);
    const float p = 1.0f + f * (0.693147207f + f * (0.240226512f + f * (0.0555032721f + f * (0.0096180256f + f * (0.00134004322f + f * 0.00015469732f)))));
    union { uint32_t u; float v; } s;
    s.u = static_cast<uint32_t> (n + 127) << 23;
    return p * s.v;
}
/** e^x, relative error below 1e-6. */
inline float exp (float x) noexcept { return exp2 (x * 1.44269504f); }
/** 10^x, relative error below 1e-6. */
inline float pow10 (float x) noexcept { return exp2 (x * 3.32192809f); }

/** The sine table of one cycle, 1024 steps (+1 to close it). */
struct SineTable
{
    float v[1025];
    SineTable()
    {
        for (int i = 0; i <= 1024; ++i)
            v[i] = static_cast<float> (std::sin (6.283185307179586 * i / 1024.0));
    }
};
inline const SineTable kSine;
/** sin (2 pi c) for a phase c in cycles (any value): table with linear interpolation, absolute error below 5e-6. */
inline float sinCycles (float c) noexcept
{
    float w = c - static_cast<float> (static_cast<int> (c)); // (-1, 1)
    if (w < 0.0f)
        w += 1.0f;
    const float pos = w * 1024.0f;
    int i = static_cast<int> (pos);
    if (i > 1023)
        i = 1023;
    const float fr = pos - static_cast<float> (i);
    return kSine.v[i] + (kSine.v[i + 1] - kSine.v[i]) * fr;
}
} // namespace fastmath
} // namespace own
} // namespace synthyboy
