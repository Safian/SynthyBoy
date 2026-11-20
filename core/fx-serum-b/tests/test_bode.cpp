// SynthyBoy - core/fx-serum-b tests: Bode frequency shifter tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bode.h"

#include <cmath>
#include <limits>
#include <vector>

#include "test_dsp.h"
#include "test_runner.h"

using namespace synthyboy::fx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeSine (int totalFrames, double freqHz, double sr)
{
    std::vector<float> sig (static_cast<size_t> (totalFrames));
    for (int i = 0; i < totalFrames; ++i)
        sig[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * freqHz * i / sr));
    return sig;
}
} // namespace

TEST (bode_shift_sine_up_hits_fft_peak)
{
    const double sr = 48000.0;
    const int totalFrames = 16384;
    const double inFreq = 1000.0;
    const double shiftAmount = 200.0;
    const double expectedFreq = inFreq + shiftAmount; // 1200 Hz

    Bode bode;
    bode.prepare (sr, 512);
    bode.params.shift = 0.2f;
    bode.params.range = 1000.0f; // 0.2 * 1000 = +200 Hz
    bode.params.dir = 1.0f;      // Both up
    bode.params.width = 1.0f;
    bode.params.mix = 1.0f;
    bode.params.level = 0.0f;

    auto inSigL = makeSine (totalFrames, inFreq, sr);
    auto inSigR = inSigL;

    // Process in blocks of 256
    const int blockSize = 256;
    for (int start = 0; start < totalFrames; start += blockSize)
        bode.process (inSigL.data() + start, inSigR.data() + start, blockSize);

    // Analyze steady state spectrum using Hann window
    const size_t fftSize = 8192;
    const size_t analyzeFrom = 4096;
    const auto powerL = sbtest::powerSpectrum (inSigL, analyzeFrom, 4096, fftSize, sbtest::Window::hann);
    const double detectedPeakL = sbtest::peakFrequency (powerL, fftSize, sr, expectedFreq);

    CHECK_NEAR (detectedPeakL, expectedFreq, 2.0); // Within 2 Hz of 1200 Hz
}

TEST (bode_shift_sine_down_hits_fft_peak)
{
    const double sr = 48000.0;
    const int totalFrames = 16384;
    const double inFreq = 1000.0;
    const double shiftAmount = 300.0;
    const double expectedFreq = inFreq - shiftAmount; // 700 Hz

    Bode bode;
    bode.prepare (sr, 512);
    bode.params.shift = 0.3f;
    bode.params.range = 1000.0f;
    bode.params.dir = -1.0f; // Both down
    bode.params.width = 1.0f;
    bode.params.mix = 1.0f;
    bode.params.level = 0.0f;

    auto inSigL = makeSine (totalFrames, inFreq, sr);
    auto inSigR = inSigL;

    const int blockSize = 256;
    for (int start = 0; start < totalFrames; start += blockSize)
        bode.process (inSigL.data() + start, inSigR.data() + start, blockSize);

    const size_t fftSize = 8192;
    const size_t analyzeFrom = 4096;
    const auto powerL = sbtest::powerSpectrum (inSigL, analyzeFrom, 4096, fftSize, sbtest::Window::hann);
    const double detectedPeakL = sbtest::peakFrequency (powerL, fftSize, sr, expectedFreq);

    CHECK_NEAR (detectedPeakL, expectedFreq, 2.0); // Within 2 Hz of 700 Hz
}

TEST (bode_stereo_dir_center_opposite_shifts)
{
    const double sr = 48000.0;
    const int totalFrames = 16384;
    const double inFreq = 1000.0;
    const double shiftAmount = 200.0;

    Bode bode;
    bode.prepare (sr, 512);
    bode.params.shift = 0.2f;
    bode.params.range = 1000.0f; // 200 Hz
    bode.params.dir = 0.0f;      // Center: opposite directions for L and R
    bode.params.width = 1.0f;    // Full stereo width
    bode.params.mix = 1.0f;

    auto inSigL = makeSine (totalFrames, inFreq, sr);
    auto inSigR = inSigL;

    for (int start = 0; start < totalFrames; start += 256)
        bode.process (inSigL.data() + start, inSigR.data() + start, 256);

    const size_t fftSize = 8192;
    const size_t analyzeFrom = 4096;

    // Left channel shifted UP to 1200 Hz
    const auto powerL = sbtest::powerSpectrum (inSigL, analyzeFrom, 4096, fftSize, sbtest::Window::hann);
    const double peakL = sbtest::peakFrequency (powerL, fftSize, sr, inFreq + shiftAmount);
    CHECK_NEAR (peakL, inFreq + shiftAmount, 2.0);

    // Right channel shifted DOWN to 800 Hz
    const auto powerR = sbtest::powerSpectrum (inSigR, analyzeFrom, 4096, fftSize, sbtest::Window::hann);
    const double peakR = sbtest::peakFrequency (powerR, fftSize, sr, inFreq - shiftAmount);
    CHECK_NEAR (peakR, inFreq - shiftAmount, 2.0);
}

TEST (bode_mono_input)
{
    Bode bode;
    bode.prepare (48000.0, 512);
    bode.params.monoInput = true;
    bode.params.mix = 1.0f;

    float l[64];
    float r[64];
    for (int i = 0; i < 64; ++i)
    {
        l[i] = 1.0f;
        r[i] = 0.0f;
    }

    bode.process (l, r, 64);

    for (int i = 0; i < 64; ++i)
        CHECK_NEAR (l[i], r[i], 1e-6);
}

TEST (bode_block_size_independence)
{
    const double sr = 48000.0;
    const int totalFrames = 2048;

    auto makeEngine = [&] () {
        Bode b;
        b.prepare (sr, 512);
        b.params.shift = 0.15f;
        b.params.range = 800.0f;
        b.params.dir = 0.5f;
        b.params.width = 0.8f;
        b.params.feedback = 0.2f;
        b.params.delay = 0.05f;
        b.params.mix = 0.7f;
        return b;
    };

    const auto sourceL = makeSine (totalFrames, 440.0, sr);
    const auto sourceR = makeSine (totalFrames, 660.0, sr);

    // Block size 1
    auto b1 = makeEngine();
    auto out1L = sourceL;
    auto out1R = sourceR;
    for (int i = 0; i < totalFrames; ++i)
        b1.process (out1L.data() + i, out1R.data() + i, 1);

    // Block size 64
    auto b64 = makeEngine();
    auto out64L = sourceL;
    auto out64R = sourceR;
    for (int i = 0; i < totalFrames; i += 64)
        b64.process (out64L.data() + i, out64R.data() + i, 64);

    // Block size 512
    auto b512 = makeEngine();
    auto out512L = sourceL;
    auto out512R = sourceR;
    for (int i = 0; i < totalFrames; i += 512)
        b512.process (out512L.data() + i, out512R.data() + i, 512);

    for (int i = 0; i < totalFrames; ++i)
    {
        CHECK_NEAR (out1L[static_cast<size_t> (i)], out64L[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (out1R[static_cast<size_t> (i)], out64R[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (out1L[static_cast<size_t> (i)], out512L[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (out1R[static_cast<size_t> (i)], out512R[static_cast<size_t> (i)], 1e-6);
    }
}

TEST (bode_nan_inf_safety)
{
    Bode bode;
    bode.prepare (48000.0, 512);
    bode.params.feedback = 0.5f;
    bode.params.delay = 0.01f;

    float l[16] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0.0f };
    float r[16] = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 0.0f, -std::numeric_limits<float>::infinity() };

    bode.process (l, r, 16);

    for (int i = 0; i < 16; ++i)
    {
        CHECK (std::isfinite (l[i]));
        CHECK (std::isfinite (r[i]));
        CHECK (! std::isnan (l[i]));
        CHECK (! std::isnan (r[i]));
    }
}

// Existing test in this file setting up Bode: bode_shift_sine_up_hits_fft_peak (line 29)
TEST (bode_feedback_loop_limit_and_clean_headroom_linearity)
{
    // Part 1: Sine at 1.5 peak amplitude (within engine headroom of +-1.8) at 1 kHz,
    // shift +100 Hz, feedback 0, mix 100%. Desired SSB output is a single sine at 1100 Hz.
    // In unfixed code, softLimit (x - 4/27 x^3 clipping at +-1.0) compresses the 1.5 amplitude fundamental
    // to 1.125 and generates a 3rd harmonic of amplitude 0.125 at 3000 Hz, shifted to 3100 Hz.
    // In fixed code using loopLimit (+-8.0), the 1.5 amplitude signal passes unclipped (mag1100 ~= 1.5),
    // and every component other than 1100 Hz (3100 Hz, 3300 Hz, and 900 Hz image) is at least 40 dB below 1100 Hz.
    {
        const double sr = 48000.0;
        const int totalFrames = 4800; // 100 ms: exact integer cycles for 900, 1000, 1100, 3100, 3300 Hz
        const double inFreq = 1000.0;

        Bode bode;
        bode.prepare (sr, 512);
        bode.params.shift = 0.1f;
        bode.params.range = 1000.0f; // 0.1 * 1000 = +100 Hz shift
        bode.params.dir = 1.0f;      // Both channels up
        bode.params.width = 1.0f;
        bode.params.feedback = 0.0f;
        bode.params.mix = 1.0f;
        bode.params.level = 0.0f;

        std::vector<float> inSigL (static_cast<size_t> (totalFrames));
        std::vector<float> inSigR (static_cast<size_t> (totalFrames));
        for (int i = 0; i < totalFrames; ++i)
        {
            const float s = 1.5f * static_cast<float> (std::sin (2.0 * kPi * inFreq * static_cast<double> (i) / sr));
            inSigL[static_cast<size_t> (i)] = s;
            inSigR[static_cast<size_t> (i)] = s;
        }

        const int blockSize = 240; // 4800 divides evenly into 20 blocks
        for (int start = 0; start < totalFrames; start += blockSize)
            bode.process (inSigL.data() + start, inSigR.data() + start, blockSize);

        // Direct DFT over the last 2400 samples (50 ms, 55 cycles of 1100 Hz, 45 of 900 Hz, 155 of 3100 Hz, 165 of 3300 Hz).
        // Zero spectral leakage due to exact integer periods.
        auto dftMag = [&] (const std::vector<float>& x, double freq) -> double
        {
            double re = 0.0;
            double im = 0.0;
            const size_t from = 2400;
            const size_t len = 2400;
            for (size_t i = 0; i < len; ++i)
            {
                const double ph = 2.0 * kPi * freq * static_cast<double> (i) / sr;
                re += static_cast<double> (x[from + i]) * std::cos (ph);
                im -= static_cast<double> (x[from + i]) * std::sin (ph);
            }
            return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (len);
        };

        const double mag1100 = dftMag (inSigL, 1100.0);
        const double mag900  = dftMag (inSigL, 900.0);
        const double mag3100 = dftMag (inSigL, 3100.0);
        const double mag3300 = dftMag (inSigL, 3300.0);

        // Required: 1100 Hz fundamental unclipped at 1.5 (fails on unfixed code: ~1.125)
        CHECK_NEAR (mag1100, 1.5, 0.05);

        // Required: all other components at least 40 dB below 1100 Hz (ratio < 0.01).
        // Fails on unfixed code at 3100 Hz (shifted 3rd harmonic of input): ratio is ~0.111 (-19.1 dB).
        CHECK (mag3100 / mag1100 < 0.01);
        CHECK (mag3300 / mag1100 < 0.01);

        // Hilbert opposite-sideband image at 900 Hz: verification notes ~69 dB image rejection at 1 kHz,
        // which easily meets the -40 dB (< 0.01) threshold.
        CHECK (mag900 / mag1100 < 0.01);
    }

    // Part 2: Feedback at maximum (0.95) with full-scale (1.0) input for 2 seconds.
    // The loop limiter bounds the feedback loop so every output sample remains finite and below 8.5 in magnitude.
    {
        const double sr = 48000.0;
        const int nTotal = static_cast<int> (2.0 * sr); // 96000 frames

        Bode bodeFb;
        bodeFb.prepare (sr, 512);
        bodeFb.params.shift = 0.1f;
        bodeFb.params.range = 1000.0f;
        bodeFb.params.dir = 1.0f;
        bodeFb.params.width = 1.0f;
        bodeFb.params.feedback = 0.95f; // Maximum feedback
        bodeFb.params.delay = 0.05f;    // 50 ms delay
        bodeFb.params.mix = 1.0f;
        bodeFb.params.level = 0.0f;

        auto sigL = makeSine (nTotal, 1000.0, sr);
        auto sigR = sigL;

        const int blockSize = 240; // 96000 divides evenly into 400 blocks
        for (int start = 0; start < nTotal; start += blockSize)
            bodeFb.process (sigL.data() + start, sigR.data() + start, blockSize);

        bool allFiniteAndBounded = true;
        for (int i = 0; i < nTotal; ++i)
        {
            const float l = sigL[static_cast<size_t> (i)];
            const float r = sigR[static_cast<size_t> (i)];
            if (! std::isfinite (l) || std::fabs (l) >= 8.5f ||
                ! std::isfinite (r) || std::fabs (r) >= 8.5f)
            {
                allFiniteAndBounded = false;
                break;
            }
        }
        CHECK (allFiniteAndBounded);
    }
}

