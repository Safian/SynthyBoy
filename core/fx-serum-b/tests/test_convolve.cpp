// SynthyBoy - core/fx-serum-b tests: Convolve partitioned FFT convolution reverb tests.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "convolve.h"

#include <cmath>
#include <limits>
#include <vector>

#include "test_runner.h"

using namespace synthyboy::fx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeTestSignal (int totalFrames, double freqHz, double sr)
{
    std::vector<float> sig (static_cast<size_t> (totalFrames));
    for (int i = 0; i < totalFrames; ++i)
        sig[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * kPi * freqHz * i / sr));
    return sig;
}
} // namespace

TEST (convolve_unit_impulse_equals_identity)
{
    const double sr = 48000.0;
    const int totalFrames = 1024;

    Convolve convolve;
    convolve.prepare (sr, 512);

    const float unitIr[1] = { 1.0f };
    CHECK (convolve.loadIr (unitIr, 1, 1, sr));

    convolve.params.mix = 1.0f;
    convolve.params.level = 0.0f;
    convolve.params.tone = 0.0f;
    convolve.params.preDelay = 0.0f;

    auto testL = makeTestSignal (totalFrames, 440.0, sr);
    auto testR = makeTestSignal (totalFrames, 880.0, sr);
    const auto origL = testL;
    const auto origR = testR;

    for (int start = 0; start < totalFrames; start += 64)
        convolve.process (testL.data() + start, testR.data() + start, 64);

    for (int i = 0; i < totalFrames; ++i)
    {
        CHECK_NEAR (testL[static_cast<size_t> (i)], origL[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (testR[static_cast<size_t> (i)], origR[static_cast<size_t> (i)], 1e-6);
    }
}

TEST (convolve_delayed_impulse_equals_delay)
{
    const double sr = 48000.0;
    const int delaySamples = 250; // Spans past partition size (128) into second partition
    const int irFrames = 300;
    std::vector<float> ir (static_cast<size_t> (irFrames), 0.0f);
    ir[static_cast<size_t> (delaySamples)] = 1.0f;

    Convolve convolve;
    convolve.prepare (sr, 512);
    CHECK (convolve.loadIr (ir.data(), irFrames, 1, sr));

    convolve.params.mix = 1.0f;
    convolve.params.level = 0.0f;
    convolve.params.tone = 0.0f;
    convolve.params.preDelay = 0.0f;

    const int totalFrames = 512;
    std::vector<float> inL (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<float> inR (static_cast<size_t> (totalFrames), 0.0f);
    inL[0] = 1.0f; // Unit impulse at sample 0
    inR[0] = 1.0f;

    for (int start = 0; start < totalFrames; start += 64)
        convolve.process (inL.data() + start, inR.data() + start, 64);

    // Output should be zero everywhere except sample 250 which is 1.0
    for (int i = 0; i < totalFrames; ++i)
    {
        const float expected = (i == delaySamples) ? 1.0f : 0.0f;
        CHECK_NEAR (inL[static_cast<size_t> (i)], expected, 1e-6);
        CHECK_NEAR (inR[static_cast<size_t> (i)], expected, 1e-6);
    }
}

TEST (convolve_ir_rate_conversion)
{
    // Engine runs at 48000 Hz; IR is sampled at 44100 Hz
    const double engineSr = 48000.0;
    const double irSr = 44100.0;

    // Place an impulse at sample 441 (exactly 10.0 ms at 44100 Hz)
    const int irFrames = 500;
    std::vector<float> ir (static_cast<size_t> (irFrames), 0.0f);
    ir[441] = 1.0f;

    Convolve convolve;
    convolve.prepare (engineSr, 512);
    CHECK (convolve.loadIr (ir.data(), irFrames, 1, irSr));

    convolve.params.mix = 1.0f;
    convolve.params.level = 0.0f;
    convolve.params.tone = 0.0f;
    convolve.params.preDelay = 0.0f;

    const int totalFrames = 640; // a whole number of 64-frame blocks (600 overran the buffers; found by ASan)
    std::vector<float> inL (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<float> inR (static_cast<size_t> (totalFrames), 0.0f);
    inL[0] = 1.0f; // Impulse at sample 0
    inR[0] = 1.0f;

    for (int start = 0; start < totalFrames; start += 64)
        convolve.process (inL.data() + start, inR.data() + start, 64);

    // Peak at 48 kHz should be at sample 480 (10.0 ms * 48000 = 480 samples)
    int peakIndexL = -1;
    float peakValL = 0.0f;
    for (int i = 0; i < totalFrames; ++i)
    {
        if (std::abs (inL[static_cast<size_t> (i)]) > peakValL)
        {
            peakValL = std::abs (inL[static_cast<size_t> (i)]);
            peakIndexL = i;
        }
    }
    CHECK_EQ (peakIndexL, 480);
    CHECK (peakValL > 0.8f);
}

TEST (convolve_block_size_independence)
{
    const double sr = 48000.0;
    const int totalFrames = 2048;

    // Create a multi-partition IR of 600 samples
    const int irFrames = 600;
    std::vector<float> ir (static_cast<size_t> (irFrames));
    for (int i = 0; i < irFrames; ++i)
        ir[static_cast<size_t> (i)] = 0.2f * static_cast<float> (std::sin (0.1 * i) * std::exp (-0.005 * i));

    auto makeEngine = [&] () {
        Convolve c;
        c.prepare (sr, 512);
        c.loadIr (ir.data(), irFrames, 1, sr);
        c.params.mix = 0.8f;
        c.params.level = -2.0f;
        return c;
    };

    const auto testL = makeTestSignal (totalFrames, 300.0, sr);
    const auto testR = makeTestSignal (totalFrames, 500.0, sr);

    // Block size 1
    auto c1 = makeEngine();
    auto out1L = testL;
    auto out1R = testR;
    for (int i = 0; i < totalFrames; ++i)
        c1.process (out1L.data() + i, out1R.data() + i, 1);

    // Block size 64
    auto c64 = makeEngine();
    auto out64L = testL;
    auto out64R = testR;
    for (int i = 0; i < totalFrames; i += 64)
        c64.process (out64L.data() + i, out64R.data() + i, 64);

    // Block size 512
    auto c512 = makeEngine();
    auto out512L = testL;
    auto out512R = testR;
    for (int i = 0; i < totalFrames; i += 512)
        c512.process (out512L.data() + i, out512R.data() + i, 512);

    for (int i = 0; i < totalFrames; ++i)
    {
        CHECK_NEAR (out1L[static_cast<size_t> (i)], out64L[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (out1R[static_cast<size_t> (i)], out64R[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (out1L[static_cast<size_t> (i)], out512L[static_cast<size_t> (i)], 1e-6);
        CHECK_NEAR (out1R[static_cast<size_t> (i)], out512R[static_cast<size_t> (i)], 1e-6);
    }
}

TEST (convolve_minimum_phase)
{
    const double sr = 48000.0;
    // Delayed impulse at sample 100
    const int irFrames = 256;
    std::vector<float> ir (static_cast<size_t> (irFrames), 0.0f);
    ir[100] = 1.0f;

    Convolve c;
    c.prepare (sr, 512);
    c.params.minimumPhase = true;
    c.loadIr (ir.data(), irFrames, 1, sr);

    const int totalFrames = 256;
    std::vector<float> inL (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<float> inR (static_cast<size_t> (totalFrames), 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    c.process (inL.data(), inR.data(), totalFrames);

    // Minimum phase concentrates energy at sample 0
    int peakIndex = -1;
    float peakVal = 0.0f;
    for (int i = 0; i < totalFrames; ++i)
    {
        if (std::abs (inL[static_cast<size_t> (i)]) > peakVal)
        {
            peakVal = std::abs (inL[static_cast<size_t> (i)]);
            peakIndex = i;
        }
    }
    CHECK_EQ (peakIndex, 0);
}

TEST (convolve_predelay)
{
    const double sr = 48000.0;
    const float unitIr[1] = { 1.0f };

    Convolve c;
    c.prepare (sr, 512);
    c.loadIr (unitIr, 1, 1, sr);
    c.params.preDelay = 0.01f; // 10 ms = 480 samples at 48 kHz
    c.params.mix = 1.0f;

    const int totalFrames = 600;
    std::vector<float> inL (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<float> inR (static_cast<size_t> (totalFrames), 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    c.process (inL.data(), inR.data(), totalFrames);

    int peakIdx = -1;
    float peakVal = 0.0f;
    for (int i = 0; i < totalFrames; ++i)
    {
        if (std::abs (inL[static_cast<size_t> (i)]) > peakVal)
        {
            peakVal = std::abs (inL[static_cast<size_t> (i)]);
            peakIdx = i;
        }
    }
    CHECK_EQ (peakIdx, 480);
    CHECK_NEAR (peakVal, 1.0f, 1e-5);
}

TEST (convolve_nan_inf_safety)
{
    Convolve c;
    c.prepare (48000.0, 512);

    float l[16] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0.0f };
    float r[16] = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 0.0f, -std::numeric_limits<float>::infinity() };

    c.process (l, r, 16);

    for (int i = 0; i < 16; ++i)
    {
        CHECK (std::isfinite (l[i]));
        CHECK (std::isfinite (r[i]));
        CHECK (! std::isnan (l[i]));
        CHECK (! std::isnan (r[i]));
    }
}
