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
