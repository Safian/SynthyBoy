// SynthyBoy - core/fx-serum-b tests: the real input FFT (docs/plans/reverb-convolve.md task C1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "fft.h"
#include "fxopt_util.h"
#include "real_fft.h"
#include "test_runner.h"

namespace
{
std::vector<float> randomSignal (int n, uint32_t seed)
{
    std::vector<float> x (static_cast<size_t> (n));
    uint32_t s = seed * 2654435761u + 1u;
    for (auto& v : x)
    {
        s = s * 1664525u + 1013904223u;
        v = static_cast<float> (s) * (2.0f / 4294967296.0f) - 1.0f;
    }
    return x;
}
} // namespace

TEST (conv_realfft_matches_complex_fft)
{
    using namespace synthyboy::fx;
    for (const int n : { 256, 2048, 16384 })
    {
        const std::vector<float> x = randomSignal (n, static_cast<uint32_t> (n));
        Radix2FFT full (n);
        std::vector<Complex> ref (static_cast<size_t> (n));
        full.forwardReal (x.data(), n, ref.data());
        RealFft real;
        real.init (n);
        CHECK_EQ (real.bins(), n / 2 + 1);
        std::vector<float> re (static_cast<size_t> (real.bins())), im (static_cast<size_t> (real.bins()));
        real.forward (x.data(), n, re.data(), im.data());
        float maxX = 0.0f;
        for (int k = 0; k <= n / 2; ++k)
            maxX = std::max (maxX, ref[static_cast<size_t> (k)].abs());
        float worst = 0.0f;
        for (int k = 0; k <= n / 2; ++k)
            worst = std::max (worst, std::max (std::fabs (re[static_cast<size_t> (k)] - ref[static_cast<size_t> (k)].r), std::fabs (im[static_cast<size_t> (k)] - ref[static_cast<size_t> (k)].i)));
        std::printf ("    n=%d: worst bin difference %.3g of max |X| %.3g\n", n, static_cast<double> (worst), static_cast<double> (maxX));
        CHECK (worst <= 2e-5f * maxX);
    }
}

TEST (conv_realfft_roundtrip_and_zero_padding)
{
    using namespace synthyboy::fx;
    for (const int n : { 256, 2048 })
    {
        RealFft real;
        real.init (n);
        const std::vector<float> x = randomSignal (n, 5);
        std::vector<float> re (static_cast<size_t> (real.bins())), im (static_cast<size_t> (real.bins())), back (static_cast<size_t> (n));
        real.forward (x.data(), n, re.data(), im.data());
        real.inverse (re.data(), im.data(), back.data());
        float peak = 0.0f;
        for (const float v : x)
            peak = std::max (peak, std::fabs (v));
        CHECK (fxopt::maxAbsDiff (x.data(), back.data(), x.size()) <= 1e-5f * peak);

        // count < n: the rest of the input is zero
        const int count = n / 3;
        real.forward (x.data(), count, re.data(), im.data());
        real.inverse (re.data(), im.data(), back.data());
        std::vector<float> padded (x);
        std::fill (padded.begin() + count, padded.end(), 0.0f);
        CHECK (fxopt::maxAbsDiff (padded.data(), back.data(), padded.size()) <= 1e-5f * peak);
    }
}

TEST (conv_realfft_speed)
{
    using namespace synthyboy::fx;
    if (sbtest::instrumented())
        return;
    const int n = 2048;
    const std::vector<float> x = randomSignal (n, 9);
    RealFft real;
    real.init (n);
    Radix2FFT full (n);
    std::vector<float> re (static_cast<size_t> (real.bins())), im (static_cast<size_t> (real.bins())), back (static_cast<size_t> (n));
    std::vector<Complex> spec (static_cast<size_t> (n));
    float sink = 0.0f;
    const double ratio = fxopt::cpuRatio (
        [&] {
            for (int i = 0; i < 400; ++i)
            {
                real.forward (x.data(), n, re.data(), im.data());
                real.inverse (re.data(), im.data(), back.data());
                sink += back[1];
            }
        },
        [&] {
            for (int i = 0; i < 400; ++i)
            {
                full.forwardReal (x.data(), n, spec.data());
                full.inverseReal (spec.data(), back.data());
                sink += back[1];
            }
        },
        5);
    std::printf ("    real fft cpu ratio %.3f (model 0.5; limit 0.65) %g\n", ratio, static_cast<double> (sink));
    CHECK (ratio <= 0.65);
}
