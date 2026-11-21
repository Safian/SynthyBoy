// SynthyBoy - own engine tests: half-band 2x oversampler for distortion antialiasing (docs/plans/distortion-antialiasing.md task E7).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "fx/fx_oversampler.h"
#include "test_runner.h"

using namespace synthyboy;
using namespace synthyboy::own;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Evaluates the magnitude |H(w)| of an N-tap FIR filter at normalized frequency f (cycles/sample).
template <int N>
double firMagnitude (const float* taps, double f)
{
    const double w = 2.0 * kPi * f;
    double re = 0.0, im = 0.0;
    for (int n = 0; n < N; ++n)
    {
        re += static_cast<double> (taps[n]) * std::cos (w * n);
        im -= static_cast<double> (taps[n]) * std::sin (w * n);
    }
    return std::sqrt (re * re + im * im);
}
} // namespace

TEST (own_alias_oversampler_halfband_taps)
{
    // Design parameters from wt_osc.h Kaiser sinc: N=31, cutoff=0.25 (cycles/sample of 2x stream), beta=7.5.
    const DecimatorTaps<31> raw { 0.25, 7.5 };

    CHECK_EQ (HalfBand2x::kTaps, 31);
    CHECK_EQ (HalfBand2x::kPolyTaps, 16);
    CHECK_EQ (HalfBand2x::kLatency, 15);

    // Sum of all taps must equal 1.0 (unity DC gain).
    double sum = 0.0;
    for (int i = 0; i < 31; ++i)
        sum += raw.c[i];
    CHECK_NEAR (sum, 1.0, 1e-6);

    // Center tap at index 15 should be approximately 0.5.
    CHECK_NEAR (raw.c[15], 0.5f, 1e-4);

    // In a half-band filter, every odd tap except the center tap is zero.
    for (int i = 1; i < 31; i += 2)
    {
        if (i != 15)
            CHECK_NEAR (raw.c[i], 0.0f, 1e-6);
    }

    // Filter coefficients must be symmetric: c[i] == c[30 - i].
    for (int i = 0; i < 15; ++i)
        CHECK_NEAR (raw.c[i], raw.c[30 - i], 1e-6);

    // Check that HalfBand2x exposes the 16 non-zero polyphase even taps and center tap.
    HalfBand2x hb;
    CHECK_NEAR (hb.centerTap(), raw.c[15], 1e-6);
    const float* poly = hb.polyTaps();
    for (int i = 0; i < 16; ++i)
        CHECK_NEAR (poly[i], raw.c[2 * i], 1e-6);
}

TEST (own_alias_oversampler_sine_gain_and_latency)
{
    HalfBand2x hb;
    const double fs = 44100.0;
    const double f = 1000.0;
    const int nSamples = 2048;

    std::vector<float> in (static_cast<size_t> (nSamples));
    std::vector<float> out (static_cast<size_t> (nSamples));

    for (int n = 0; n < nSamples; ++n)
        in[static_cast<size_t> (n)] = static_cast<float> (std::sin (2.0 * kPi * f * n / fs));

    for (int n = 0; n < nSamples; ++n)
    {
        const auto up2 = hb.up (in[static_cast<size_t> (n)]);
        out[static_cast<size_t> (n)] = hb.down (up2[0], up2[1]);
    }

    // Latency measurement via cross-correlation peak.
    double maxCorr = -1.0e9;
    int bestLag = -1;
    for (int lag = 0; lag <= 30; ++lag)
    {
        double corr = 0.0;
        for (int n = 100; n < nSamples - 100; ++n)
            corr += static_cast<double> (in[static_cast<size_t> (n)]) * static_cast<double> (out[static_cast<size_t> (n + lag)]);
        if (corr > maxCorr)
        {
            maxCorr = corr;
            bestLag = lag;
        }
    }
    CHECK_EQ (bestLag, HalfBand2x::kLatency); // Exactly 15 samples latency

    // Gain at 1 kHz within 0.05 dB after latency alignment.
    double rmsIn = 0.0, rmsOut = 0.0;
    const int count = nSamples - 300;
    for (int n = 100; n < 100 + count; ++n)
    {
        const double xi = static_cast<double> (in[static_cast<size_t> (n)]);
        const double yo = static_cast<double> (out[static_cast<size_t> (n + HalfBand2x::kLatency)]);
        rmsIn += xi * xi;
        rmsOut += yo * yo;
    }
    rmsIn = std::sqrt (rmsIn / count);
    rmsOut = std::sqrt (rmsOut / count);
    const double gainDb = 20.0 * std::log10 (rmsOut / rmsIn);
    CHECK (std::fabs (gainDb) <= 0.05);
}

TEST (own_alias_oversampler_impulse_symmetry_and_dc)
{
    HalfBand2x hb;
    std::vector<float> impulse (45, 0.0f);
    impulse[0] = 1.0f;

    std::vector<float> response (45, 0.0f);
    for (size_t n = 0; n < impulse.size(); ++n)
    {
        const auto up2 = hb.up (impulse[n]);
        response[n] = hb.down (up2[0], up2[1]);
    }

    // The peak of the impulse response is at index 15 (latency 15).
    CHECK (response[15] > 0.9f);

    // The impulse response must be symmetric around index 15.
    for (int d = 1; d <= 14; ++d)
    {
        CHECK_NEAR (response[static_cast<size_t> (15 - d)], response[static_cast<size_t> (15 + d)], 1e-6);
    }

    // Sum of impulse response must equal DC gain 1.000000.
    double sum = 0.0;
    for (float v : response)
        sum += v;
    CHECK_NEAR (sum, 1.0, 1e-5);

    // DC steady-state response.
    hb.reset();
    float dcOut = 0.0f;
    for (int n = 0; n < 50; ++n)
    {
        const auto up2 = hb.up (1.0f);
        dcOut = hb.down (up2[0], up2[1]);
    }
    CHECK_NEAR (dcOut, 1.0f, 1e-5);
}

TEST (own_alias_oversampler_passband_ripple)
{
    const DecimatorTaps<31> raw { 0.25, 7.5 };
    // Passband from 0 to 0.15 cycles/sample of the 2x stream (up to ~13.2 kHz at 88.2 kHz rate).
    double maxRipple = 0.0;
    for (double f = 0.005; f <= 0.15; f += 0.005)
    {
        const double mag = firMagnitude<31> (raw.c, f);
        const double ripple = std::fabs (mag - 1.0);
        if (ripple > maxRipple)
            maxRipple = ripple;
    }
    // Ripple must be well under 0.01 dB (< 0.001 linear).
    CHECK (maxRipple < 0.001);
}

TEST (own_alias_oversampler_stopband_attenuation)
{
    const DecimatorTaps<31> raw { 0.25, 7.5 };

    // Stopband of the 2x half-band filter spans [0.35, 0.50] cycles/sample.
    // Theoretical Kaiser stopband attenuation A = 7.5 / 0.1102 + 8.7 = 76.8 dB.
    // Margin threshold from plan: at least 60 dB.
    double maxStopbandMag = 0.0;
    for (double f = 0.35; f <= 0.50; f += 0.005)
    {
        const double mag = firMagnitude<31> (raw.c, f);
        if (mag > maxStopbandMag)
            maxStopbandMag = mag;
    }
    const double minAttDb = -20.0 * std::log10 (std::max (maxStopbandMag, 1e-12));
    CHECK (minAttDb >= 60.0);

    // Test decimation filter stopband directly:
    // Feed a stopband tone (normalized frequency 0.40 cyc/sample of the 2x stream) into down(a, b).
    // (At 2fs = 44.1 kHz, 20 kHz is f_norm = 20000 / 44100 = 0.4535 cyc/sample).
    HalfBand2x hb;
    const int nBlocks = 512;
    std::vector<float> in2x (static_cast<size_t> (nBlocks * 2));
    for (size_t m = 0; m < in2x.size(); ++m)
        in2x[m] = static_cast<float> (std::sin (2.0 * kPi * 0.40 * static_cast<double> (m)));

    std::vector<float> downOut (static_cast<size_t> (nBlocks));
    for (int k = 0; k < nBlocks; ++k)
        downOut[static_cast<size_t> (k)] = hb.down (in2x[static_cast<size_t> (2 * k)], in2x[static_cast<size_t> (2 * k + 1)]);

    double rmsIn = 0.0, rmsDown = 0.0;
    for (size_t m = 200; m < in2x.size(); ++m)
        rmsIn += static_cast<double> (in2x[m]) * static_cast<double> (in2x[m]);
    for (size_t k = 100; k < downOut.size(); ++k)
        rmsDown += static_cast<double> (downOut[k]) * static_cast<double> (downOut[k]);

    rmsIn = std::sqrt (rmsIn / static_cast<double> (in2x.size() - 200));
    rmsDown = std::sqrt (rmsDown / static_cast<double> (downOut.size() - 100));
    const double downAttDb = 20.0 * std::log10 (rmsDown / rmsIn);
    CHECK (downAttDb <= -60.0); // Attenuated by at least 60 dB (measured ~ -86.8 dB)
}

TEST (own_alias_oversampler_round_trip_and_reset)
{
    HalfBand2x hb;
    const double fs = 44100.0;
    const int nSamples = 500;

    // Multi-tone bandlimited input.
    std::vector<float> in (static_cast<size_t> (nSamples));
    for (int n = 0; n < nSamples; ++n)
    {
        const double t = static_cast<double> (n) / fs;
        in[static_cast<size_t> (n)] = static_cast<float> (
            0.33 * std::sin (2.0 * kPi * 440.0 * t) +
            0.33 * std::sin (2.0 * kPi * 1200.0 * t) +
            0.33 * std::sin (2.0 * kPi * 2800.0 * t));
    }

    std::vector<float> out (static_cast<size_t> (nSamples));
    for (int n = 0; n < nSamples; ++n)
    {
        const auto up2 = hb.up (in[static_cast<size_t> (n)]);
        out[static_cast<size_t> (n)] = hb.down (up2[0], up2[1]);
    }

    // After warmup (delay 15), round-trip matches delayed input within 1e-4.
    double maxDiff = 0.0;
    for (int n = 50; n < nSamples - 50; ++n)
    {
        const double diff = std::fabs (static_cast<double> (out[static_cast<size_t> (n)]) -
                                       static_cast<double> (in[static_cast<size_t> (n - HalfBand2x::kLatency)]));
        if (diff > maxDiff)
            maxDiff = diff;
    }
    CHECK (maxDiff < 0.001);

    // reset() clears all history; feeding 0.0f produces exact 0.0f.
    hb.reset();
    for (int n = 0; n < 10; ++n)
    {
        const auto up2 = hb.up (0.0f);
        const float y = hb.down (up2[0], up2[1]);
        CHECK_EQ (y, 0.0f);
    }
}
