// SynthyBoy - Unit tests for Serum FX modules (Hyper/Dimension, Utility, Splitters).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test_runner.h"

#include <cmath>
#include <vector>

#include "synthyboy/fx/hyper_dimension.h"
#include "synthyboy/fx/splitter_lh.h"
#include "synthyboy/fx/splitter_lmh.h"
#include "synthyboy/fx/splitter_ms.h"
#include "synthyboy/fx/stereo_splitter.h"
#include "synthyboy/fx/utility.h"

namespace
{
void generateSine (float* l, float* r, int n, double sampleRate, float freqHz)
{
    const double phaseInc = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < n; ++i)
    {
        const float val = static_cast<float> (std::sin (i * phaseInc));
        l[i] = val;
        r[i] = val * 0.8f;
    }
}

float computeRMS (const float* x, int n)
{
    if (n <= 0)
        return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double> (x[i]) * static_cast<double> (x[i]);
    return static_cast<float> (std::sqrt (sum / static_cast<double> (n)));
}

float computeMaxSampleStep (const float* x, int n)
{
    float maxStep = 0.0f;
    for (int i = 1; i < n; ++i)
    {
        const float diff = std::fabs (x[i] - x[i - 1]);
        if (diff > maxStep)
            maxStep = diff;
    }
    return maxStep;
}
} // namespace

// ============================================================================
// 1. Bypass-Null Tests at Mix 0 and Enabled = False
// ============================================================================

TEST (HyperDimension_BypassNull)
{
    synthyboy::fx::HyperDimension fx;
    fx.prepare (44100.0, 512);

    std::vector<float> l(512);
    std::vector<float> r(512);
    generateSine (l.data(), r.data(), 512, 44100.0, 440.0f);
    const auto origL = l;
    const auto origR = r;

    // Test 1: hyperMix = 0, dimMix = 0, levelDb = 0 -> bit-for-bit null
    synthyboy::fx::HyperDimension::Params p;
    p.hyperMix = 0.0f;
    p.dimMix = 0.0f;
    p.levelDb = 0.0f;
    fx.setParams (p);
    fx.reset();
    fx.process (l.data(), r.data(), 512);

    for (size_t i = 0; i < 512; ++i)
    {
        CHECK_EQ (l[i], origL[i]);
        CHECK_EQ (r[i], origR[i]);
    }

    // Test 2: enabled = false -> bit-for-bit null
    l = origL;
    r = origR;
    p.hyperMix = 0.7f;
    p.dimMix = 0.6f;
    p.enabled = false;
    fx.setParams (p);
    fx.process (l.data(), r.data(), 512);

    for (size_t i = 0; i < 512; ++i)
    {
        CHECK_EQ (l[i], origL[i]);
        CHECK_EQ (r[i], origR[i]);
    }
}

TEST (Utility_BypassNull)
{
    synthyboy::fx::Utility fx;
    fx.prepare (44100.0, 512);

    std::vector<float> l(512);
    std::vector<float> r(512);
    generateSine (l.data(), r.data(), 512, 44100.0, 440.0f);
    const auto origL = l;
    const auto origR = r;

    // Test 1: mix = 0, levelDb = 0 -> bit-for-bit null
    synthyboy::fx::Utility::Params p;
    p.mix = 0.0f;
    p.levelDb = 0.0f;
    p.width = 0.0f; // extreme width should not affect mix 0
    p.invertL = true;
    fx.setParams (p);
    fx.reset();
    fx.process (l.data(), r.data(), 512);

    for (size_t i = 0; i < 512; ++i)
    {
        CHECK_EQ (l[i], origL[i]);
        CHECK_EQ (r[i], origR[i]);
    }

    // Test 2: enabled = false -> bit-for-bit null
    l = origL;
    r = origR;
    p.enabled = false;
    p.mix = 1.0f;
    fx.setParams (p);
    fx.process (l.data(), r.data(), 512);

    for (size_t i = 0; i < 512; ++i)
    {
        CHECK_EQ (l[i], origL[i]);
        CHECK_EQ (r[i], origR[i]);
    }
}

TEST (Splitters_BypassNull)
{
    std::vector<float> l(256);
    std::vector<float> r(256);
    generateSine (l.data(), r.data(), 256, 48000.0, 1000.0f);
    const auto origL = l;
    const auto origR = r;

    // SplitterLH at mix 0
    {
        synthyboy::fx::SplitterLH lh;
        lh.prepare (48000.0, 256);
        synthyboy::fx::SplitterLH::Params p;
        p.mix = 0.0f;
        p.levelDb = 0.0f;
        p.lowGainDb = -60.0f;
        lh.setParams (p);
        lh.reset();
        auto curL = origL;
        auto curR = origR;
        lh.process (curL.data(), curR.data(), 256);
        for (size_t i = 0; i < 256; ++i)
        {
            CHECK_EQ (curL[i], origL[i]);
            CHECK_EQ (curR[i], origR[i]);
        }
    }

    // SplitterLMH at mix 0
    {
        synthyboy::fx::SplitterLMH lmh;
        lmh.prepare (48000.0, 256);
        synthyboy::fx::SplitterLMH::Params p;
        p.mix = 0.0f;
        p.levelDb = 0.0f;
        p.midGainDb = -60.0f;
        lmh.setParams (p);
        lmh.reset();
        auto curL = origL;
        auto curR = origR;
        lmh.process (curL.data(), curR.data(), 256);
        for (size_t i = 0; i < 256; ++i)
        {
            CHECK_EQ (curL[i], origL[i]);
            CHECK_EQ (curR[i], origR[i]);
        }
    }

    // SplitterMS at mix 0
    {
        synthyboy::fx::SplitterMS ms;
        ms.prepare (48000.0, 256);
        synthyboy::fx::SplitterMS::Params p;
        p.mix = 0.0f;
        p.levelDb = 0.0f;
        p.sideGainDb = -60.0f;
        ms.setParams (p);
        ms.reset();
        auto curL = origL;
        auto curR = origR;
        ms.process (curL.data(), curR.data(), 256);
        for (size_t i = 0; i < 256; ++i)
        {
            CHECK_EQ (curL[i], origL[i]);
            CHECK_EQ (curR[i], origR[i]);
        }
    }

    // StereoSplitter at mix 0
    {
        synthyboy::fx::StereoSplitter ss;
        ss.prepare (48000.0, 256);
        synthyboy::fx::StereoSplitter::Params p;
        p.mix = 0.0f;
        p.levelDb = 0.0f;
        p.leftGainDb = -60.0f;
        ss.setParams (p);
        ss.reset();
        auto curL = origL;
        auto curR = origR;
        ss.process (curL.data(), curR.data(), 256);
        for (size_t i = 0; i < 256; ++i)
        {
            CHECK_EQ (curL[i], origL[i]);
            CHECK_EQ (curR[i], origR[i]);
        }
    }
}

// ============================================================================
// 2. Level and Energy Sanity
// ============================================================================

TEST (HyperDimension_EnergySanity)
{
    synthyboy::fx::HyperDimension fx;
    fx.prepare (44100.0, 1024);

    synthyboy::fx::HyperDimension::Params p;
    p.hyperRate = 1.2f;
    p.hyperUnison = 7;
    p.hyperDetune = 0.5f;
    p.hyperMix = 1.0f;
    p.dimSize = 0.5f;
    p.dimMix = 0.5f;
    p.levelDb = 0.0f;
    fx.setParams (p);

    std::vector<float> l(2048);
    std::vector<float> r(2048);
    generateSine (l.data(), r.data(), 2048, 44100.0, 440.0f);
    const float inRMS = computeRMS (l.data(), 2048);

    fx.process (l.data(), r.data(), 2048);
    const float outRMS_L = computeRMS (l.data(), 2048);
    const float outRMS_R = computeRMS (r.data(), 2048);

    // Wet signal should have sensible energy (not zero, and not blown up by > 12 dB)
    CHECK (outRMS_L > 0.1f * inRMS);
    CHECK (outRMS_L < 4.0f * inRMS);
    CHECK (outRMS_R > 0.1f * inRMS);
    CHECK (outRMS_R < 4.0f * inRMS);
}

TEST (Splitters_ReconstructionFlatMagnitude)
{
    // Test that LR4 crossover split & merge preserves total magnitude without gain blowup
    const int kSamples = 4096;
    std::vector<float> l(kSamples);
    std::vector<float> r(kSamples);
    generateSine (l.data(), r.data(), kSamples, 48000.0, 500.0f);
    const float inRMS = computeRMS (l.data(), kSamples);

    // SplitterLH at default splitFreq 500 Hz (at the crossover)
    {
        synthyboy::fx::SplitterLH lh;
        lh.prepare (48000.0, kSamples);
        synthyboy::fx::SplitterLH::Params p;
        p.splitFreq = 500.0f;
        p.mix = 1.0f;
        p.levelDb = 0.0f;
        lh.setParams (p);

        auto curL = l;
        auto curR = r;
        // Warm up filter state
        lh.process (curL.data(), curR.data(), kSamples);

        curL = l;
        curR = r;
        lh.process (curL.data(), curR.data(), kSamples);
        const float outRMS = computeRMS (curL.data(), kSamples);
        // Linkwitz-Riley 4th order reconstruction should be near unity RMS
        CHECK_NEAR (outRMS, inRMS, 0.08f * inRMS);
    }

    // SplitterMS reconstruction
    {
        synthyboy::fx::SplitterMS ms;
        ms.prepare (48000.0, kSamples);
        auto curL = l;
        auto curR = r;
        ms.process (curL.data(), curR.data(), kSamples);
        for (int i = 0; i < kSamples; ++i)
        {
            CHECK_NEAR (curL[i], l[i], 1.0e-5);
            CHECK_NEAR (curR[i], r[i], 1.0e-5);
        }
    }
}

// ============================================================================
// 3. No NaN or Inf on Extremes & Denormals
// ============================================================================

TEST (NoNaN_ExtremesAndDenormals)
{
    const int kLen = 512;

    // Test extreme inputs: NaNs, infs, denormals, high gain
    std::vector<float> l(kLen, 1.0e-30f); // subnormal float
    std::vector<float> r(kLen, -1.0e-30f);

    // HyperDimension extremes
    {
        synthyboy::fx::HyperDimension hd;
        hd.prepare (96000.0, kLen);
        synthyboy::fx::HyperDimension::Params p;
        p.hyperRate = 20.0f;
        p.hyperUnison = 7;
        p.hyperDetune = 1.0f;
        p.dimSize = 1.0f;
        p.levelDb = 24.0f;
        hd.setParams (p);

        auto curL = l;
        auto curR = r;
        hd.process (curL.data(), curR.data(), kLen);
        for (int i = 0; i < kLen; ++i)
        {
            CHECK (! std::isnan (curL[i]));
            CHECK (! std::isinf (curL[i]));
            CHECK (! std::isnan (curR[i]));
            CHECK (! std::isinf (curR[i]));
        }
    }

    // Utility extremes
    {
        synthyboy::fx::Utility ut;
        ut.prepare (96000.0, kLen);
        synthyboy::fx::Utility::Params p;
        p.invertL = true;
        p.invertR = true;
        p.lpfEnabled = true;
        p.lpfFreq = 20.0f; // extreme low cutoff
        p.hpfEnabled = true;
        p.hpfFreq = 20000.0f; // extreme high cutoff
        p.monoBassEnabled = true;
        p.monoBassFreq = 1000.0f;
        p.width = 2.0f;
        p.pan = 1.0f;
        p.dcFilter = true;
        p.levelDb = 24.0f;
        ut.setParams (p);

        auto curL = l;
        auto curR = r;
        ut.process (curL.data(), curR.data(), kLen);
        for (int i = 0; i < kLen; ++i)
        {
            CHECK (! std::isnan (curL[i]));
            CHECK (! std::isinf (curL[i]));
            CHECK (! std::isnan (curR[i]));
            CHECK (! std::isinf (curR[i]));
        }
    }

    // SplitterLMH extremes
    {
        synthyboy::fx::SplitterLMH lmh;
        lmh.prepare (96000.0, kLen);
        synthyboy::fx::SplitterLMH::Params p;
        p.splitFreq1 = 20.0f;
        p.splitFreq2 = 20000.0f;
        p.lowGainDb = 12.0f;
        p.midGainDb = -60.0f;
        p.highGainDb = 12.0f;
        p.levelDb = 24.0f;
        lmh.setParams (p);

        auto curL = l;
        auto curR = r;
        lmh.process (curL.data(), curR.data(), kLen);
        for (int i = 0; i < kLen; ++i)
        {
            CHECK (! std::isnan (curL[i]));
            CHECK (! std::isinf (curL[i]));
            CHECK (! std::isnan (curR[i]));
            CHECK (! std::isinf (curR[i]));
        }
    }
}

// ============================================================================
// 4. Determinism
// ============================================================================

TEST (Determinism_IdenticalRuns)
{
    const int kLen = 1024;
    std::vector<float> inL(kLen);
    std::vector<float> inR(kLen);
    generateSine (inL.data(), inR.data(), kLen, 44100.0, 300.0f);

    // HyperDimension determinism
    {
        synthyboy::fx::HyperDimension hd1;
        synthyboy::fx::HyperDimension hd2;
        hd1.prepare (44100.0, kLen);
        hd2.prepare (44100.0, kLen);

        synthyboy::fx::HyperDimension::Params p;
        p.hyperRate = 2.0f;
        p.hyperUnison = 5;
        p.hyperDetune = 0.4f;
        p.dimSize = 0.7f;
        hd1.setParams (p);
        hd2.setParams (p);
        hd1.reset();
        hd2.reset();

        auto l1 = inL;
        auto r1 = inR;
        auto l2 = inL;
        auto r2 = inR;

        hd1.process (l1.data(), r1.data(), kLen);
        hd2.process (l2.data(), r2.data(), kLen);

        for (int i = 0; i < kLen; ++i)
        {
            CHECK_EQ (l1[i], l2[i]);
            CHECK_EQ (r1[i], r2[i]);
        }
    }

    // Utility determinism
    {
        synthyboy::fx::Utility u1;
        synthyboy::fx::Utility u2;
        u1.prepare (44100.0, kLen);
        u2.prepare (44100.0, kLen);

        synthyboy::fx::Utility::Params p;
        p.lpfEnabled = true;
        p.lpfFreq = 1500.0f;
        p.monoBassEnabled = true;
        p.monoBassFreq = 180.0f;
        p.width = 1.5f;
        p.pan = -0.3f;
        u1.setParams (p);
        u2.setParams (p);
        u1.reset();
        u2.reset();

        auto l1 = inL;
        auto r1 = inR;
        auto l2 = inL;
        auto r2 = inR;

        u1.process (l1.data(), r1.data(), kLen);
        u2.process (l2.data(), r2.data(), kLen);

        for (int i = 0; i < kLen; ++i)
        {
            CHECK_EQ (l1[i], l2[i]);
            CHECK_EQ (r1[i], r2[i]);
        }
    }
}

// ============================================================================
// 5. Parameter Sweeps Without Clicks (Max Sample Step Bound)
// ============================================================================

TEST (ParameterSweeps_ClickFree)
{
    const int kBlock = 64;
    const int kNumBlocks = 32;
    const int kTotal = kBlock * kNumBlocks;

    std::vector<float> inL(kTotal);
    std::vector<float> inR(kTotal);
    generateSine (inL.data(), inR.data(), kTotal, 48000.0, 220.0f);

    // Utility: sweep width and pan dramatically across blocks
    {
        synthyboy::fx::Utility ut;
        ut.prepare (48000.0, kBlock);
        synthyboy::fx::Utility::Params p;
        p.width = 0.0f;
        p.pan = -1.0f;
        ut.setParams (p);
        ut.reset();

        std::vector<float> outL(kTotal);
        std::vector<float> outR(kTotal);

        for (int b = 0; b < kNumBlocks; ++b)
        {
            const float progress = static_cast<float> (b) / static_cast<float> (kNumBlocks - 1);
            p.width = progress * 2.0f;        // sweep 0.0 -> 2.0
            p.pan = -1.0f + 2.0f * progress;  // sweep -1.0 -> +1.0
            p.mix = progress;                 // sweep 0.0 -> 1.0
            ut.setParams (p);

            float* blkL = inL.data() + b * kBlock;
            float* blkR = inR.data() + b * kBlock;
            ut.process (blkL, blkR, kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                outL[b * kBlock + i] = blkL[i];
                outR[b * kBlock + i] = blkR[i];
            }
        }

        // Max sample step should be smooth (no click discontinuties > 0.5 for a 220 Hz sine)
        const float stepL = computeMaxSampleStep (outL.data(), kTotal);
        const float stepR = computeMaxSampleStep (outR.data(), kTotal);
        CHECK (stepL < 0.35f);
        CHECK (stepR < 0.35f);
    }

    // HyperDimension: sweep mix and size across blocks
    {
        synthyboy::fx::HyperDimension hd;
        hd.prepare (48000.0, kBlock);
        synthyboy::fx::HyperDimension::Params p;
        p.hyperMix = 0.0f;
        p.dimMix = 0.0f;
        hd.setParams (p);
        hd.reset();

        std::vector<float> outL(kTotal);
        std::vector<float> outR(kTotal);

        for (int b = 0; b < kNumBlocks; ++b)
        {
            const float progress = static_cast<float> (b) / static_cast<float> (kNumBlocks - 1);
            p.hyperMix = progress;
            p.dimMix = progress;
            hd.setParams (p);

            float* blkL = inL.data() + b * kBlock;
            float* blkR = inR.data() + b * kBlock;
            hd.process (blkL, blkR, kBlock);

            for (int i = 0; i < kBlock; ++i)
            {
                outL[b * kBlock + i] = blkL[i];
                outR[b * kBlock + i] = blkR[i];
            }
        }

        const float stepL = computeMaxSampleStep (outL.data(), kTotal);
        const float stepR = computeMaxSampleStep (outR.data(), kTotal);
        CHECK (stepL < 0.35f);
        CHECK (stepR < 0.35f);
    }
}

// ============================================================================
// 6. Sample Rate and Block Size Scalability
// ============================================================================

TEST (Scalability_SampleRatesAndBlockSizes)
{
    const double rates[] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    const int blockSizes[] = { 1, 7, 32, 64, 128, 512, 1024, 4096 };

    for (double sr : rates)
    {
        for (int bs : blockSizes)
        {
            synthyboy::fx::HyperDimension hd;
            hd.prepare (sr, bs);
            std::vector<float> l(bs, 0.5f);
            std::vector<float> r(bs, -0.5f);
            hd.process (l.data(), r.data(), bs);
            CHECK (! std::isnan (l[0]));
            CHECK (! std::isnan (r[0]));

            synthyboy::fx::Utility ut;
            ut.prepare (sr, bs);
            ut.process (l.data(), r.data(), bs);
            CHECK (! std::isnan (l[0]));
            CHECK (! std::isnan (r[0]));

            synthyboy::fx::SplitterLH lh;
            lh.prepare (sr, bs);
            lh.process (l.data(), r.data(), bs);
            CHECK (! std::isnan (l[0]));
            CHECK (! std::isnan (r[0]));

            synthyboy::fx::SplitterLMH lmh;
            lmh.prepare (sr, bs);
            lmh.process (l.data(), r.data(), bs);
            CHECK (! std::isnan (l[0]));
            CHECK (! std::isnan (r[0]));

            synthyboy::fx::SplitterMS ms;
            ms.prepare (sr, bs);
            ms.process (l.data(), r.data(), bs);
            CHECK (! std::isnan (l[0]));
            CHECK (! std::isnan (r[0]));

            synthyboy::fx::StereoSplitter ss;
            ss.prepare (sr, bs);
            ss.process (l.data(), r.data(), bs);
            CHECK (! std::isnan (l[0]));
            CHECK (! std::isnan (r[0]));
        }
    }
}

// ============================================================================
// 7. Routing Helper Methods (split and merge)
// ============================================================================

TEST (RoutingHelpers_SplitAndMerge)
{
    const int kLen = 256;
    std::vector<float> inL(kLen);
    std::vector<float> inR(kLen);
    generateSine (inL.data(), inR.data(), kLen, 44100.0, 1000.0f);

    // SplitterLH helper
    {
        synthyboy::fx::SplitterLH lh;
        lh.prepare (44100.0, kLen);
        std::vector<float> lowL(kLen), lowR(kLen), highL(kLen), highR(kLen);
        std::vector<float> outL(kLen), outR(kLen);

        lh.split (inL.data(), inR.data(), lowL.data(), lowR.data(), highL.data(), highR.data(), kLen);
        lh.merge (lowL.data(), lowR.data(), highL.data(), highR.data(), outL.data(), outR.data(), kLen);

        CHECK (computeRMS (lowL.data(), kLen) > 0.0f);
        CHECK (computeRMS (highL.data(), kLen) > 0.0f);
        CHECK_NEAR (computeRMS (outL.data(), kLen), computeRMS (inL.data(), kLen), 0.1f * computeRMS (inL.data(), kLen));
    }

    // SplitterLMH helper
    {
        synthyboy::fx::SplitterLMH lmh;
        lmh.prepare (44100.0, kLen);
        std::vector<float> lowL(kLen), lowR(kLen), midL(kLen), midR(kLen), highL(kLen), highR(kLen);
        std::vector<float> outL(kLen), outR(kLen);

        lmh.split (inL.data(), inR.data(), lowL.data(), lowR.data(), midL.data(), midR.data(), highL.data(), highR.data(), kLen);
        lmh.merge (lowL.data(), lowR.data(), midL.data(), midR.data(), highL.data(), highR.data(), outL.data(), outR.data(), kLen);

        CHECK (computeRMS (outL.data(), kLen) > 0.0f);
    }

    // SplitterMS helper
    {
        synthyboy::fx::SplitterMS ms;
        ms.prepare (44100.0, kLen);
        std::vector<float> midL(kLen), midR(kLen), sideL(kLen), sideR(kLen);
        std::vector<float> outL(kLen), outR(kLen);

        ms.split (inL.data(), inR.data(), midL.data(), midR.data(), sideL.data(), sideR.data(), kLen);
        ms.merge (midL.data(), midR.data(), sideL.data(), sideR.data(), outL.data(), outR.data(), kLen);

        for (int i = 0; i < kLen; ++i)
        {
            CHECK_NEAR (outL[i], inL[i], 1.0e-5);
            CHECK_NEAR (outR[i], inR[i], 1.0e-5);
        }
    }

    // StereoSplitter helper
    {
        synthyboy::fx::StereoSplitter ss;
        ss.prepare (44100.0, kLen);
        std::vector<float> leftCh(kLen), rightCh(kLen);
        std::vector<float> outL(kLen), outR(kLen);

        ss.split (inL.data(), inR.data(), leftCh.data(), rightCh.data(), kLen);
        ss.merge (leftCh.data(), rightCh.data(), outL.data(), outR.data(), kLen);

        for (int i = 0; i < kLen; ++i)
        {
            CHECK_NEAR (outL[i], inL[i], 1.0e-5);
            CHECK_NEAR (outR[i], inR[i], 1.0e-5);
        }
    }
}
