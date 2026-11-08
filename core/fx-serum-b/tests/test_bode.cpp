// SynthyBoy - core/fx-serum-b tests: Bode frequency shifter tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bode.h"

#include <cmath>
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

    float l[16] = { 0.0f / 0.0f, 1.0f / 0.0f, -1.0f / 0.0f, 0.0f };
    float r[16] = { 1.0f / 0.0f, 0.0f / 0.0f, 0.0f, -1.0f / 0.0f };

    bode.process (l, r, 16);

    for (int i = 0; i < 16; ++i)
    {
        CHECK (std::isfinite (l[i]));
        CHECK (std::isfinite (r[i]));
        CHECK (! std::isnan (l[i]));
        CHECK (! std::isnan (r[i]));
    }
}
