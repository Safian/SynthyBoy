// SynthyBoy - own engine: half-band 2x oversampler for distortion antialiasing (docs/plans/distortion-antialiasing.md task E7).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace synthyboy
{
namespace own
{

/** Kaiser windowed sinc low pass of N taps, `cutoff` in cycles per sample of the oversampled signal, normalised to unity gain at DC.
    Reused from core/engine-own/osc/wt_osc.h (lines 83-112). */
template <int N>
struct DecimatorTaps
{
    float c[N];
    DecimatorTaps (double cutoff, double beta)
    {
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        double total = 0.0;
        double tmp[N];
        for (int i = 0; i < N; ++i)
        {
            const double x = i - 0.5 * (N - 1);
            const double sinc = std::fabs (x) < 1.0e-9 ? 2.0 * cutoff : std::sin (2.0 * 3.14159265358979323846 * cutoff * x) / (3.14159265358979323846 * x);
            const double r = 2.0 * i / (N - 1) - 1.0;
            tmp[i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            total += tmp[i];
        }
        for (int i = 0; i < N; ++i)
            c[i] = static_cast<float> (tmp[i] / total);
    }
};

/** A linear-phase half-band FIR oversampler and decimator for 2x rate conversion (task E7).
    N = 31 taps, cutoff = 0.25 cycles/sample of the 2x stream, Kaiser window beta = 7.5 (stopband > 76 dB).
    All odd taps except tap 15 are zero. Polyphase decomposition uses 16 non-zero even taps and the center tap.
    Base latency of an up + down pair is 15 base (1x) samples (2 * 15 / 2).
    State per channel: histories of 15 floats (std::array, no dynamic memory allocation). */
class HalfBand2x
{
public:
    static constexpr int kTaps = 31;
    static constexpr int kPolyTaps = 16;
    static constexpr int kLatency = 15; // base samples of an up + down pair: 2 * 15 / 2

    struct Coeffs
    {
        std::array<float, kPolyTaps> poly {}; // even taps c[0], c[2], ..., c[30]
        float center = 0.5f;                  // center tap c[15]

        Coeffs()
        {
            const DecimatorTaps<kTaps> raw { 0.25, 7.5 };
            for (int i = 0; i < kPolyTaps; ++i)
                poly[static_cast<size_t> (i)] = raw.c[2 * i];
            center = raw.c[15];
        }
    };

    static inline const Coeffs kCoeffs {};

    HalfBand2x()
    {
        for (int i = 0; i < kPolyTaps; ++i)
            taps_[static_cast<size_t> (i)] = kCoeffs.poly[static_cast<size_t> (i)];
        center_ = kCoeffs.center;
        reset();
    }

    void reset() noexcept
    {
        histUp_.fill (0.0f);
        histDownA_.fill (0.0f);
        histDownB_.fill (0.0f);
    }

    /** 2x upsampler: takes 1 sample at 1x, outputs 2 samples at 2x. */
    std::array<float, 2> up (float x) noexcept
    {
        // Even polyphase branch: 16 even taps with current x and 15 past samples.
        float acc = taps_[0] * x;
        for (int i = 1; i < kPolyTaps; ++i)
            acc += taps_[static_cast<size_t> (i)] * histUp_[static_cast<size_t> (i - 1)];
        const float y0 = 2.0f * acc;

        // Odd polyphase branch: pure delay of 7 samples (index 6 in past history) scaled by center tap.
        const float y1 = 2.0f * center_ * histUp_[6]; // x[n - 7]

        // Shift history of 15 floats:
        for (int i = 14; i > 0; --i)
            histUp_[static_cast<size_t> (i)] = histUp_[static_cast<size_t> (i - 1)];
        histUp_[0] = x;

        return { y0, y1 };
    }

    void up (float x, float* out) noexcept
    {
        const auto res = up (x);
        out[0] = res[0];
        out[1] = res[1];
    }

    void up (float x, float out[2]) noexcept
    {
        const auto res = up (x);
        out[0] = res[0];
        out[1] = res[1];
    }

    /** 2x decimator: takes 2 samples at 2x (a = even, b = odd), outputs 1 sample at 1x. */
    float down (float a, float b) noexcept
    {
        // Even branch: 16 even taps with current a and 15 past samples of a.
        float acc = taps_[0] * a;
        for (int i = 1; i < kPolyTaps; ++i)
            acc += taps_[static_cast<size_t> (i)] * histDownA_[static_cast<size_t> (i - 1)];

        // Odd branch: center tap with b delayed by 8 samples (index 7 in past history).
        const float delayed = center_ * histDownB_[7]; // b[n - 8]

        // Shift histories of 15 floats:
        for (int i = 14; i > 0; --i)
            histDownA_[static_cast<size_t> (i)] = histDownA_[static_cast<size_t> (i - 1)];
        histDownA_[0] = a;

        for (int i = 14; i > 0; --i)
            histDownB_[static_cast<size_t> (i)] = histDownB_[static_cast<size_t> (i - 1)];
        histDownB_[0] = b;

        return acc + delayed;
    }

    const float* polyTaps() const noexcept { return taps_.data(); }
    float centerTap() const noexcept { return center_; }

private:
    std::array<float, kPolyTaps> taps_ {};
    float center_ = 0.5f;

    std::array<float, 15> histUp_ {};
    std::array<float, 15> histDownA_ {};
    std::array<float, 15> histDownB_ {};
};

} // namespace own
} // namespace synthyboy
