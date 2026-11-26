// SynthyBoy - core tests: helpers of the effect optimisation tests (docs/plans/reverb-convolve.md section 4.0): a deterministic stereo program, the checksum of a render, differences and CPU ratios.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "test_runner.h"

namespace fxopt
{
struct Program
{
    std::vector<float> l, r;
};

/** A stereo program: band-limited noise (one-pole low pass at 4 kHz, 0.1 rms, independent in the channels) plus a 220 Hz saw (0.2) gated 0.1 s on / 0.4 s off plus a click at frame 0
    (0.4, so the peak of the program stays below 1). Deterministic: the same arguments give the same samples on every platform. */
inline Program makeProgram (size_t frames, double sr, uint32_t seed)
{
    Program p;
    p.l.assign (frames, 0.0f);
    p.r.assign (frames, 0.0f);
    const double a = std::exp (-6.283185307179586 * 4000.0 / sr); // the pole of the low pass
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<float>& out = ch == 0 ? p.l : p.r;
        uint32_t state = seed * 2654435761u + static_cast<uint32_t> (ch) * 40503u + 12345u;
        double lp = 0.0, sumSq = 0.0;
        std::vector<double> noise (frames);
        for (size_t i = 0; i < frames; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const double white = static_cast<double> (state) * (2.0 / 4294967296.0) - 1.0;
            lp = (1.0 - a) * white + a * lp;
            noise[i] = lp;
            sumSq += lp * lp;
        }
        const double rms = std::sqrt (sumSq / static_cast<double> (std::max<size_t> (frames, 1)));
        const double scale = rms > 0.0 ? 0.1 / rms : 0.0;
        for (size_t i = 0; i < frames; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double gate = std::fmod (t, 0.5) < 0.1 ? 1.0 : 0.0;
            const double ph = std::fmod (220.0 * t, 1.0);
            out[i] = static_cast<float> (noise[i] * scale + 0.2 * gate * (2.0 * ph - 1.0));
        }
    }
    if (frames > 0)
    {
        p.l[0] += 0.4f;
        p.r[0] += 0.4f;
    }
    return p;
}

/** The 64 bit FNV-1a of the bits of v[i] + 0.0f (so -0 and +0 hash alike): the <NAME>-CHECKSUM of the tests. */
inline uint64_t fnv1a (const float* v, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = v[i] + 0.0f;
        uint32_t bits;
        std::memcpy (&bits, &x, sizeof bits);
        for (int b = 0; b < 4; ++b)
        {
            h ^= static_cast<uint64_t> ((bits >> (8 * b)) & 0xffu);
            h *= 1099511628211ull;
        }
    }
    return h;
}

inline float maxAbs (const float* v, size_t n)
{
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i)
        m = std::max (m, std::fabs (v[i]));
    return m;
}

inline float maxAbsDiff (const float* a, const float* b, size_t n)
{
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i)
        m = std::max (m, std::fabs (a[i] - b[i]));
    return m;
}

/** True when the two renders hold the same bits after zero normalisation (-0 == +0). */
inline bool sameBits (const float* a, const float* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const float x = a[i] + 0.0f, y = b[i] + 0.0f;
        if (std::memcmp (&x, &y, sizeof x) != 0)
            return false;
    }
    return true;
}

/** CPU time of `newer` over the CPU time of `older`: the best (smallest) thread CPU time of each, measured in interleaved pairs (new, old, new, old, ...) so that a slow phase of the machine
    (a busy sibling hyper-thread, frequency steps, the rest of the suite running in parallel) hits both sides alike instead of one whole series; at least 12 pairs, because with the best of 5
    run one after the other the same build measured 0.69 to 1.14 for a true 0.72 on a loaded 4 vCPU x86 host. The limits of the callers are unchanged. */
template <class FnNew, class FnOld>
double cpuRatio (FnNew&& newer, FnOld&& older, int reps)
{
    double a = 1.0e300, b = 1.0e300;
    for (int i = 0; i < (reps < 12 ? 12 : reps); ++i)
    {
        a = std::min (a, sbtest::bestCpuMs (1, newer));
        b = std::min (b, sbtest::bestCpuMs (1, older));
    }
    return b > 0.0 ? a / b : 1.0;
}
} // namespace fxopt
