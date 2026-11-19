// SynthyBoy - core tests: output stage host tests (make-up gain + look-ahead limiter).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "synthyboy/output_stage.h"
#include "output_stage.h"

#ifndef SYNTHYBOY_MPC_OUTPUT_GAIN_DB
#define SYNTHYBOY_MPC_OUTPUT_GAIN_DB synthyboy::kMakeupGainDb
#endif

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> generateSine (size_t n, float amp, double hz, double sr)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double> (i) / sr;
        v[i] = amp * static_cast<float> (std::sin (phase));
    }
    return v;
}

} // namespace

// 1. Parity: the MPC path and the desktop path produce bit-identical output for the same input
// (find where each shell calls OutputStage; test through the shared class with the two parameter sets the shells use).
TEST (output_stage_parity_mpc_desktop)
{
    // Desktop path:
    // JUCE (shell-juce/PluginProcessor.cpp:315, 451, 485) and CLAP (shell-clap/synthyboy_clap.cpp:187, 216, 359, 383):
    // synthyboy::OutputStage outputStage (default constructor sets kMakeupGainDb = -2.5 dB, limiter ON).
    // prepare(sampleRate), reset().
    //
    // MPC path:
    // MPC (shell-mpc/synthyboy_mpc.cpp:394, 404, 1660-1663, 2354, 3102):
    // synthyboy_mpc::SwitchableOutputStage outputStage;
    // prepare(sampleRate), setGainDb(SYNTHYBOY_MPC_OUTPUT_GAIN_DB), reset(), setLimiter(...) based on LIMIT setting.

    const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
    const int blockSizes[] = { 64, 128, 256 };

    for (const double sr : sampleRates)
    {
        for (const bool limiterState : { true, false })
        {
            for (const int bs : blockSizes)
            {
                synthyboy::OutputStage desktop;
                desktop.prepare (sr);
                desktop.setLimiter (limiterState);

                synthyboy_mpc::SwitchableOutputStage mpc;
                mpc.prepare (sr);
                mpc.setGainDb (static_cast<float> (SYNTHYBOY_MPC_OUTPUT_GAIN_DB));
                mpc.reset();
                mpc.setLimiter (limiterState);

                CHECK_EQ (desktop.latencySamples(), mpc.latencySamples());
                CHECK_EQ (desktop.lookaheadSamples(), mpc.lookaheadSamples());

                const size_t totalFrames = static_cast<size_t> (sr * 0.25); // 250 ms of test audio
                auto inL = generateSine (totalFrames, 1.8f, 440.0, sr);
                auto inR = generateSine (totalFrames, 1.2f, 880.0, sr);

                std::vector<float> dL = inL, dR = inR;
                std::vector<float> mL = inL, mR = inR;

                size_t diffSamplesL = 0;
                size_t diffSamplesR = 0;

                for (size_t pos = 0; pos < totalFrames; pos += static_cast<size_t> (bs))
                {
                    const int frames = static_cast<int> (std::min (static_cast<size_t> (bs), totalFrames - pos));
                    desktop.process (dL.data() + pos, dR.data() + pos, frames);
                    mpc.process (mL.data() + pos, mR.data() + pos, frames);

                    for (size_t i = pos; i < pos + static_cast<size_t> (frames); ++i)
                    {
                        uint32_t bDL = 0, bML = 0, bDR = 0, bMR = 0;
                        std::memcpy (&bDL, &dL[i], sizeof (float));
                        std::memcpy (&bML, &mL[i], sizeof (float));
                        std::memcpy (&bDR, &dR[i], sizeof (float));
                        std::memcpy (&bMR, &mR[i], sizeof (float));

                        if (bDL != bML)
                            ++diffSamplesL;
                        if (bDR != bMR)
                            ++diffSamplesR;
                    }
                }

                CHECK_EQ (diffSamplesL, 0u);
                CHECK_EQ (diffSamplesR, 0u);
                CHECK_EQ (desktop.lastGain(), mpc.lastGain());
                CHECK_EQ (desktop.safetyClips(), mpc.safetyClips());
                CHECK_NEAR (desktop.gainReductionDb(), mpc.gainReductionDb(), 1e-5);
            }
        }
    }
}

// 2. Aliased / null channel pointers: in-place processing (in == out), and a null right channel,
// do not crash and give the same left result.
TEST (output_stage_aliased_and_null_pointers)
{
    const double sampleRates[] = { 44100.0, 48000.0 };

    for (const double sr : sampleRates)
    {
        for (const bool limiterOn : { true, false })
        {
            const size_t totalFrames = 4800; // 100 ms
            auto sourceL = generateSine (totalFrames, 1.5f, 500.0, sr);

            // Null pointers must not crash
            synthyboy::OutputStage stageCrashTest;
            stageCrashTest.prepare (sr);
            stageCrashTest.setLimiter (limiterOn);
            stageCrashTest.process (nullptr, nullptr, 128);
            stageCrashTest.process (nullptr, nullptr, 0);

            // Left null, right valid
            std::vector<float> rOnly = sourceL;
            stageCrashTest.process (nullptr, rOnly.data(), static_cast<int> (rOnly.size()));
            CHECK (std::isfinite (rOnly[0]));

            // Compare:
            // Case 1: Null right channel: process(mono.data(), nullptr, n)
            // Case 2: Aliased channel: process(aliased.data(), aliased.data(), n)
            // Case 3: Right channel filled with explicit zeros: process(refL.data(), refZeros.data(), n)
            synthyboy::OutputStage stageMono;
            stageMono.prepare (sr);
            stageMono.setLimiter (limiterOn);

            synthyboy::OutputStage stageAliased;
            stageAliased.prepare (sr);
            stageAliased.setLimiter (limiterOn);

            synthyboy::OutputStage stageRef;
            stageRef.prepare (sr);
            stageRef.setLimiter (limiterOn);

            std::vector<float> bufMono = sourceL;
            std::vector<float> bufAliased = sourceL;
            std::vector<float> bufRefL = sourceL;
            std::vector<float> bufRefR (totalFrames, 0.0f);

            const int bs = 128;
            size_t diffAliasedVsMono = 0;
            size_t diffRefVsMono = 0;

            for (size_t pos = 0; pos < totalFrames; pos += static_cast<size_t> (bs))
            {
                const int frames = static_cast<int> (std::min (static_cast<size_t> (bs), totalFrames - pos));
                stageMono.process (bufMono.data() + pos, nullptr, frames);
                stageAliased.process (bufAliased.data() + pos, bufAliased.data() + pos, frames);
                stageRef.process (bufRefL.data() + pos, bufRefR.data() + pos, frames);

                for (size_t i = pos; i < pos + static_cast<size_t> (frames); ++i)
                {
                    uint32_t bMono = 0, bAliased = 0, bRef = 0;
                    std::memcpy (&bMono, &bufMono[i], sizeof (float));
                    std::memcpy (&bAliased, &bufAliased[i], sizeof (float));
                    std::memcpy (&bRef, &bufRefL[i], sizeof (float));

                    if (bAliased != bMono)
                        ++diffAliasedVsMono;
                    if (bRef != bMono)
                        ++diffRefVsMono;
                }
            }

            // Exactly identical results across all frames
            CHECK_EQ (diffAliasedVsMono, 0u);
            CHECK_EQ (diffRefVsMono, 0u);
            CHECK_EQ (stageAliased.lastGain(), stageMono.lastGain());
            CHECK_EQ (stageRef.lastGain(), stageMono.lastGain());

            // In-place processing consistency: block-by-block vs sample-by-sample
            synthyboy::OutputStage stageBlock, stageSample;
            stageBlock.prepare (sr);
            stageBlock.setLimiter (limiterOn);
            stageSample.prepare (sr);
            stageSample.setLimiter (limiterOn);

            auto inBlockL = sourceL, inBlockR = sourceL;
            auto inSampleL = sourceL, inSampleR = sourceL;

            for (size_t pos = 0; pos < totalFrames; pos += static_cast<size_t> (bs))
            {
                const int frames = static_cast<int> (std::min (static_cast<size_t> (bs), totalFrames - pos));
                stageBlock.process (inBlockL.data() + pos, inBlockR.data() + pos, frames);
            }

            for (size_t i = 0; i < totalFrames; ++i)
            {
                stageSample.process (&inSampleL[i], &inSampleR[i], 1);
            }

            size_t diffBlockVsSample = 0;
            for (size_t i = 0; i < totalFrames; ++i)
            {
                uint32_t bBlockL = 0, bSampleL = 0;
                std::memcpy (&bBlockL, &inBlockL[i], sizeof (float));
                std::memcpy (&bSampleL, &inSampleL[i], sizeof (float));
                if (bBlockL != bSampleL)
                    ++diffBlockVsSample;
            }
            CHECK_EQ (diffBlockVsSample, 0u);
        }
    }
}

// 3. NaN / Inf in the input: output finite, bounded by the ceiling, and the stage recovers
// (next clean block normal).
TEST (output_stage_nan_and_inf_recovery)
{
    const double sampleRates[] = { 44100.0, 48000.0 };
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    const float negNanVal = -std::numeric_limits<float>::quiet_NaN();
    const float infVal = std::numeric_limits<float>::infinity();
    const float negInfVal = -std::numeric_limits<float>::infinity();

    for (const double sr : sampleRates)
    {
        for (const bool limiterOn : { true, false })
        {
            synthyboy::OutputStage stageTest;
            stageTest.prepare (sr);
            stageTest.setLimiter (limiterOn);

            synthyboy::OutputStage stageRef;
            stageRef.prepare (sr);
            stageRef.setLimiter (limiterOn);

            const int bs = 128;

            // Block 1: Clean prelude
            auto clean1L = generateSine (static_cast<size_t> (bs), 0.5f, 440.0, sr);
            auto clean1R = generateSine (static_cast<size_t> (bs), 0.3f, 880.0, sr);

            auto test1L = clean1L, test1R = clean1R;
            auto ref1L = clean1L, ref1R = clean1R;

            stageTest.process (test1L.data(), test1R.data(), bs);
            stageRef.process (ref1L.data(), ref1R.data(), bs);

            // Block 2: Corrupted block containing NaNs, -NaNs, +Infs, -Infs
            std::vector<float> badL (static_cast<size_t> (bs));
            std::vector<float> badR (static_cast<size_t> (bs));
            std::vector<float> zeroL (static_cast<size_t> (bs), 0.0f);
            std::vector<float> zeroR (static_cast<size_t> (bs), 0.0f);

            for (size_t i = 0; i < static_cast<size_t> (bs); ++i)
            {
                switch (i % 4)
                {
                    case 0: badL[i] = nanVal;    badR[i] = negNanVal; break;
                    case 1: badL[i] = infVal;    badR[i] = negInfVal; break;
                    case 2: badL[i] = negInfVal; badR[i] = infVal; break;
                    default: badL[i] = negNanVal; badR[i] = nanVal; break;
                }
            }

            stageTest.process (badL.data(), badR.data(), bs);
            stageRef.process (zeroL.data(), zeroR.data(), bs);

            // Check that during the corrupted block, all output samples are finite and <= ceiling
            bool allBadFinite = true;
            bool allBadBounded = true;
            for (size_t i = 0; i < static_cast<size_t> (bs); ++i)
            {
                if (! std::isfinite (badL[i]) || ! std::isfinite (badR[i]))
                    allBadFinite = false;
                if (std::fabs (badL[i]) > synthyboy::OutputStage::kCeiling + 1e-6f ||
                    std::fabs (badR[i]) > synthyboy::OutputStage::kCeiling + 1e-6f)
                    allBadBounded = false;
            }
            CHECK (allBadFinite);
            CHECK (allBadBounded);

            // Block 3: Next clean block normal
            auto clean3L = generateSine (static_cast<size_t> (bs * 4), 0.7f, 330.0, sr);
            auto clean3R = generateSine (static_cast<size_t> (bs * 4), 0.7f, 660.0, sr);

            auto test3L = clean3L, test3R = clean3R;
            auto ref3L = clean3L, ref3R = clean3R;

            size_t diffPostRecovery = 0;
            bool allCleanFinite = true;
            bool allCleanBounded = true;

            for (size_t pos = 0; pos < clean3L.size(); pos += static_cast<size_t> (bs))
            {
                stageTest.process (test3L.data() + pos, test3R.data() + pos, bs);
                stageRef.process (ref3L.data() + pos, ref3R.data() + pos, bs);

                for (size_t i = pos; i < pos + static_cast<size_t> (bs); ++i)
                {
                    if (! std::isfinite (test3L[i]) || ! std::isfinite (test3R[i]))
                        allCleanFinite = false;
                    if (std::fabs (test3L[i]) > synthyboy::OutputStage::kCeiling + 1e-6f ||
                        std::fabs (test3R[i]) > synthyboy::OutputStage::kCeiling + 1e-6f)
                        allCleanBounded = false;

                    uint32_t bTL = 0, bRL = 0, bTR = 0, bRR = 0;
                    std::memcpy (&bTL, &test3L[i], sizeof (float));
                    std::memcpy (&bRL, &ref3L[i], sizeof (float));
                    std::memcpy (&bTR, &test3R[i], sizeof (float));
                    std::memcpy (&bRR, &ref3R[i], sizeof (float));

                    if (bTL != bRL || bTR != bRR)
                        ++diffPostRecovery;
                }
            }

            CHECK (allCleanFinite);
            CHECK (allCleanBounded);
            // Must have recovered and be bit-identical to the reference run that saw zeros
            CHECK_EQ (diffPostRecovery, 0u);
            CHECK_EQ (stageTest.lastGain(), stageRef.lastGain());
            CHECK_NEAR (stageTest.gainReductionDb(), stageRef.gainReductionDb(), 1e-5);

            // With limiter on, verify that NaNs, infinities and extreme values up to 3e38 remain finite and within ceiling
            if (limiterOn)
            {
                synthyboy::OutputStage stageHuge;
                stageHuge.prepare (sr);
                stageHuge.setLimiter (true);
                std::vector<float> lHuge (256, nanVal), rHuge (256, negInfVal);
                lHuge[5] = 3.0e38f;
                rHuge[7] = 1.0e30f;
                lHuge[9] = 1.0e-40f;
                stageHuge.process (lHuge.data(), rHuge.data(), 256);

                bool hugeOk = true;
                for (size_t i = 0; i < 256; ++i)
                {
                    if (! std::isfinite (lHuge[i]) || ! std::isfinite (rHuge[i]))
                        hugeOk = false;
                    if (std::fabs (lHuge[i]) > synthyboy::OutputStage::kCeiling ||
                        std::fabs (rHuge[i]) > synthyboy::OutputStage::kCeiling)
                        hugeOk = false;
                }
                CHECK (hugeOk);
                CHECK (std::isfinite (stageHuge.gainReductionDb()));
            }
        }
    }
}

// 4. sampleFrames <= 0 (0 and negative): no write, no state change
// (a following block identical to a run that never saw it).
TEST (output_stage_nonpositive_sample_frames)
{
    const double sampleRates[] = { 44100.0, 48000.0 };

    for (const double sr : sampleRates)
    {
        for (const bool limiterOn : { true, false })
        {
            synthyboy::OutputStage stageA, stageB;
            stageA.prepare (sr);
            stageA.setLimiter (limiterOn);
            stageB.prepare (sr);
            stageB.setLimiter (limiterOn);

            // 1. No write check: buffers must remain exactly byte-identical
            constexpr size_t kSentinelCount = 64;
            const uint32_t kSentinelPattern = 0xdeadbeefu;
            float sentinelL[kSentinelCount];
            float sentinelR[kSentinelCount];

            auto resetSentinels = [&]() {
                for (size_t i = 0; i < kSentinelCount; ++i)
                {
                    std::memcpy (&sentinelL[i], &kSentinelPattern, sizeof (float));
                    std::memcpy (&sentinelR[i], &kSentinelPattern, sizeof (float));
                }
            };

            auto checkSentinels = [&]() -> bool {
                for (size_t i = 0; i < kSentinelCount; ++i)
                {
                    uint32_t bL = 0, bR = 0;
                    std::memcpy (&bL, &sentinelL[i], sizeof (float));
                    std::memcpy (&bR, &sentinelR[i], sizeof (float));
                    if (bL != kSentinelPattern || bR != kSentinelPattern)
                        return false;
                }
                return true;
            };

            resetSentinels();
            stageA.process (sentinelL, sentinelR, 0);
            CHECK (checkSentinels());

            stageA.process (sentinelL, sentinelR, -1);
            CHECK (checkSentinels());

            stageA.process (sentinelL, sentinelR, -100);
            CHECK (checkSentinels());

            // 2. No state change from idle:
            // stageA saw n=0 and n<0 calls; stageB did not.
            // A following block must be 100% bit-identical.
            const int bs = 128;
            auto testBlockL = generateSine (static_cast<size_t> (bs), 0.8f, 440.0, sr);
            auto testBlockR = generateSine (static_cast<size_t> (bs), 0.6f, 880.0, sr);

            auto outA_L = testBlockL, outA_R = testBlockR;
            auto outB_L = testBlockL, outB_R = testBlockR;

            stageA.process (outA_L.data(), outA_R.data(), bs);
            stageB.process (outB_L.data(), outB_R.data(), bs);

            size_t diffIdle = 0;
            for (size_t i = 0; i < static_cast<size_t> (bs); ++i)
            {
                uint32_t bAL = 0, bBL = 0, bAR = 0, bBR = 0;
                std::memcpy (&bAL, &outA_L[i], sizeof (float));
                std::memcpy (&bBL, &outB_L[i], sizeof (float));
                std::memcpy (&bAR, &outA_R[i], sizeof (float));
                std::memcpy (&bBR, &outB_R[i], sizeof (float));
                if (bAL != bBL || bAR != bBR)
                    ++diffIdle;
            }
            CHECK_EQ (diffIdle, 0u);
            CHECK_EQ (stageA.lastGain(), stageB.lastGain());
            CHECK_NEAR (stageA.gainReductionDb(), stageB.gainReductionDb(), 1e-6);

            // 3. No state change while actively limiting:
            // Both process a hot signal block first
            auto hotL = generateSine (static_cast<size_t> (bs), 2.0f, 1000.0, sr);
            auto hotR = generateSine (static_cast<size_t> (bs), 2.0f, 1000.0, sr);
            auto hotA_L = hotL, hotA_R = hotR;
            auto hotB_L = hotL, hotB_R = hotR;

            stageA.process (hotA_L.data(), hotA_R.data(), bs);
            stageB.process (hotB_L.data(), hotB_R.data(), bs);

            // Interleave n<=0 calls into stageA only
            resetSentinels();
            stageA.process (sentinelL, sentinelR, 0);
            stageA.process (sentinelL, sentinelR, -5);
            CHECK (checkSentinels());

            // Process next block through both
            auto nextL = generateSine (static_cast<size_t> (bs), 1.5f, 330.0, sr);
            auto nextR = generateSine (static_cast<size_t> (bs), 1.5f, 330.0, sr);
            auto nextA_L = nextL, nextA_R = nextR;
            auto nextB_L = nextL, nextB_R = nextR;

            stageA.process (nextA_L.data(), nextA_R.data(), bs);
            stageB.process (nextB_L.data(), nextB_R.data(), bs);

            size_t diffActive = 0;
            for (size_t i = 0; i < static_cast<size_t> (bs); ++i)
            {
                uint32_t bAL = 0, bBL = 0, bAR = 0, bBR = 0;
                std::memcpy (&bAL, &nextA_L[i], sizeof (float));
                std::memcpy (&bBL, &nextB_L[i], sizeof (float));
                std::memcpy (&bAR, &nextA_R[i], sizeof (float));
                std::memcpy (&bBR, &nextB_R[i], sizeof (float));
                if (bAL != bBL || bAR != bBR)
                    ++diffActive;
            }
            CHECK_EQ (diffActive, 0u);
            CHECK_EQ (stageA.lastGain(), stageB.lastGain());
            CHECK_NEAR (stageA.gainReductionDb(), stageB.gainReductionDb(), 1e-6);
        }
    }
}

// 5. Limiting switch (LIMIT off/on) crossfade: no sample jump larger than a derived bound at the switch.
TEST (output_stage_switch_crossfade_bound)
{
    const double sampleRates[] = { 44100.0, 48000.0 };
    const double testFrequencies[] = { 150.0, 330.0, 1000.0 };

    for (const double sr : sampleRates)
    {
        const int D = synthyboy::OutputStage::lookaheadFor (sr);
        const float A = 0.5f;

        for (const double f : testFrequencies)
        {
            // Derived bound:
            // Delta y[n] <= sineStep + fadeStep
            // where sineStep = 2 * pi * f / sr * A
            // and fadeStep = 2 * A / (kSwitchFadeSeconds * sr)
            const double sineStep = 2.0 * kPi * f / sr * static_cast<double> (A);
            const double fadeStep = 2.0 * static_cast<double> (A) / std::max (1.0, std::round (synthyboy::OutputStage::kSwitchFadeSeconds * sr));
            const double maxAllowedStep = sineStep + fadeStep + 1e-5;

            for (const bool toOn : { true, false })
            {
                synthyboy::OutputStage stage;
                stage.prepare (sr);
                stage.setGainDb (0.0f);
                stage.setLimiter (! toOn);
                stage.reset();

                const size_t sec = static_cast<size_t> (sr);
                auto in = generateSine (sec, A, f, sr);
                auto l = in, r = in;

                const size_t switchPoint = sec / 2;

                // Process first half before switch
                stage.process (l.data(), r.data(), static_cast<int> (switchPoint));

                // Flip switch
                stage.setLimiter (toOn);

                // Process second half across switch and fade
                stage.process (l.data() + switchPoint, r.data() + switchPoint, static_cast<int> (sec - switchPoint));

                // Measure maximum sample jump in the region around and throughout the switch
                double observedMaxStep = 0.0;
                const size_t checkStart = switchPoint > 10 ? switchPoint - 10 : 0;
                const size_t checkEnd = std::min (sec - 1, switchPoint + static_cast<size_t> (synthyboy::OutputStage::kSwitchFadeSeconds * sr * 1.5));

                for (size_t i = checkStart; i < checkEnd; ++i)
                {
                    const double stepL = static_cast<double> (std::fabs (l[i + 1] - l[i]));
                    const double stepR = static_cast<double> (std::fabs (r[i + 1] - r[i]));
                    observedMaxStep = std::max (observedMaxStep, std::max (stepL, stepR));
                }

                // Numeric assertion: jump bounded by derived bound
                CHECK (observedMaxStep <= maxAllowedStep);
                CHECK (observedMaxStep > 0.0);

                // Verify that after crossfade completes, output matches the target path bit-identically
                const size_t settledStart = switchPoint + static_cast<size_t> (synthyboy::OutputStage::kSwitchFadeSeconds * sr * 1.5) + static_cast<size_t> (D);
                size_t settledDiffs = 0;
                for (size_t i = settledStart; i < sec; ++i)
                {
                    const float expected = toOn ? in[i - static_cast<size_t> (D)] : in[i];
                    uint32_t bObsL = 0, bExp = 0;
                    std::memcpy (&bObsL, &l[i], sizeof (float));
                    std::memcpy (&bExp, &expected, sizeof (float));
                    if (bObsL != bExp)
                        ++settledDiffs;
                }
                CHECK_EQ (settledDiffs, 0u);
            }
        }

        // Test switching ON with signal far above ceiling:
        // Output must never exceed ceiling from the switch moment onward
        {
            synthyboy::OutputStage stageLoud;
            stageLoud.prepare (sr);
            stageLoud.setGainDb (18.0f);
            stageLoud.setLimiter (false);
            stageLoud.reset();

            const size_t sec = static_cast<size_t> (sr * 2);
            auto inLoud = generateSine (sec, 0.5f, 220.0, sr);
            auto lLoud = inLoud, rLoud = inLoud;

            const size_t switchPoint = sec / 2;
            stageLoud.process (lLoud.data(), rLoud.data(), static_cast<int> (switchPoint));

            // Switch to ON
            stageLoud.setLimiter (true);
            stageLoud.process (lLoud.data() + switchPoint, rLoud.data() + switchPoint, static_cast<int> (sec - switchPoint));

            float maxPostSwitchL = 0.0f, maxPostSwitchR = 0.0f;
            for (size_t i = switchPoint; i < sec; ++i)
            {
                maxPostSwitchL = std::max (maxPostSwitchL, std::fabs (lLoud[i]));
                maxPostSwitchR = std::max (maxPostSwitchR, std::fabs (rLoud[i]));
            }
            CHECK (maxPostSwitchL <= synthyboy::OutputStage::kCeiling);
            CHECK (maxPostSwitchR <= synthyboy::OutputStage::kCeiling);
        }
    }
}

// 6. Look-ahead delay reporting: initialDelay equals the actual latency measured with an impulse
// (idle state, limiter on) and equals 0 with limiter off.
TEST (output_stage_lookahead_delay_reporting)
{
    const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

    for (const double sr : sampleRates)
    {
        const int expectedLookahead = synthyboy::OutputStage::lookaheadFor (sr);

        // Case A: Limiter ON
        {
            synthyboy::OutputStage stage;
            stage.prepare (sr);
            stage.setLimiter (true);
            stage.setGainDb (0.0f);
            stage.reset();

            // Shell reporting (e.g. synthyboy_mpc.cpp:2307, 3199)
            const int initialDelay = stage.limiter() ? synthyboy::OutputStage::lookaheadFor (sr) : 0;
            CHECK_EQ (initialDelay, expectedLookahead);
            CHECK_EQ (stage.latencySamples(), expectedLookahead);

            // Measure actual latency with an impulse in idle state
            const size_t bufSize = static_cast<size_t> (expectedLookahead + 128);
            std::vector<float> impulseL (bufSize, 0.0f);
            std::vector<float> impulseR (bufSize, 0.0f);
            const float impulseAmp = 0.5f;
            impulseL[0] = impulseAmp;
            impulseR[0] = impulseAmp;

            stage.process (impulseL.data(), impulseR.data(), static_cast<int> (bufSize));

            // Find peak position
            size_t measuredLatencyL = 0;
            float maxValL = 0.0f;
            for (size_t i = 0; i < bufSize; ++i)
            {
                if (std::fabs (impulseL[i]) > maxValL)
                {
                    maxValL = std::fabs (impulseL[i]);
                    measuredLatencyL = i;
                }
            }

            // Exactly equals lookahead and initialDelay
            CHECK_EQ (static_cast<int> (measuredLatencyL), expectedLookahead);
            CHECK_EQ (static_cast<int> (measuredLatencyL), initialDelay);
            CHECK_EQ (static_cast<int> (measuredLatencyL), stage.latencySamples());

            // Check bit-exact value at latency and silence everywhere else
            CHECK_EQ (impulseL[static_cast<size_t> (expectedLookahead)], impulseAmp);
            CHECK_EQ (impulseR[static_cast<size_t> (expectedLookahead)], impulseAmp);

            size_t nonZeroSilences = 0;
            for (size_t i = 0; i < bufSize; ++i)
            {
                if (i != static_cast<size_t> (expectedLookahead))
                {
                    if (impulseL[i] != 0.0f || impulseR[i] != 0.0f)
                        ++nonZeroSilences;
                }
            }
            CHECK_EQ (nonZeroSilences, 0u);
        }

        // Case B: Limiter OFF
        {
            synthyboy::OutputStage stage;
            stage.prepare (sr);
            stage.setLimiter (false);
            stage.setGainDb (0.0f);
            stage.reset();

            const int initialDelay = stage.limiter() ? synthyboy::OutputStage::lookaheadFor (sr) : 0;
            CHECK_EQ (initialDelay, 0);
            CHECK_EQ (stage.latencySamples(), 0);

            const size_t bufSize = static_cast<size_t> (expectedLookahead + 128);
            std::vector<float> impulseL (bufSize, 0.0f);
            std::vector<float> impulseR (bufSize, 0.0f);
            const float impulseAmp = 0.5f;
            impulseL[0] = impulseAmp;
            impulseR[0] = impulseAmp;

            stage.process (impulseL.data(), impulseR.data(), static_cast<int> (bufSize));

            size_t measuredLatencyL = 0;
            float maxValL = 0.0f;
            for (size_t i = 0; i < bufSize; ++i)
            {
                if (std::fabs (impulseL[i]) > maxValL)
                {
                    maxValL = std::fabs (impulseL[i]);
                    measuredLatencyL = i;
                }
            }

            CHECK_EQ (static_cast<int> (measuredLatencyL), 0);
            CHECK_EQ (static_cast<int> (measuredLatencyL), initialDelay);
            CHECK_EQ (static_cast<int> (measuredLatencyL), stage.latencySamples());

            CHECK_EQ (impulseL[0], impulseAmp);
            CHECK_EQ (impulseR[0], impulseAmp);

            size_t nonZeroSilencesOff = 0;
            for (size_t i = 1; i < bufSize; ++i)
            {
                if (impulseL[i] != 0.0f || impulseR[i] != 0.0f)
                    ++nonZeroSilencesOff;
            }
            CHECK_EQ (nonZeroSilencesOff, 0u);
        }

        // Case C: Small block processing across blocks
        {
            synthyboy::OutputStage stage;
            stage.prepare (sr);
            stage.setLimiter (true);
            stage.setGainDb (0.0f);
            stage.reset();

            const int blockSize = 64;
            const size_t totalFrames = static_cast<size_t> (expectedLookahead + 256);
            std::vector<float> impulseL (totalFrames, 0.0f);
            std::vector<float> impulseR (totalFrames, 0.0f);
            impulseL[0] = 0.4f;
            impulseR[0] = 0.4f;

            for (size_t pos = 0; pos < totalFrames; pos += static_cast<size_t> (blockSize))
            {
                const int n = static_cast<int> (std::min (static_cast<size_t> (blockSize), totalFrames - pos));
                stage.process (impulseL.data() + pos, impulseR.data() + pos, n);
            }

            CHECK_EQ (impulseL[static_cast<size_t> (expectedLookahead)], 0.4f);
            CHECK_EQ (impulseR[static_cast<size_t> (expectedLookahead)], 0.4f);
            CHECK_EQ (impulseL[0], 0.0f);
        }
    }
}
