// SynthyBoy - core/shapes tests: LFO engine, directions, tempo sync, chaos generators, and smoothing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/shapes/lfo.h"
#include "test_runner.h"

#include <cmath>

using namespace synthyboy::shapes;

TEST (lfo_phase_directions_and_ping_pong_continuity)
{
    const double sr = 48000.0;
    PhaseAccumulator acc;
    acc.prepare (sr);
    acc.setFrequency (10.0f);

    // 1. Forward direction
    acc.setDirection (Direction::forward);
    acc.reset (0.0f);
    float p0 = acc.next();
    float p1 = acc.next();
    CHECK (p1 > p0);

    // 2. Backward direction
    acc.setDirection (Direction::backward);
    acc.reset (0.0f);
    p0 = acc.next();
    p1 = acc.next();
    CHECK (p1 < p0);

    // 3. Ping-Pong continuity: run through 20 full cycles (96,000 samples)
    acc.setDirection (Direction::pingPong);
    acc.reset (0.0f);

    float prevPhase = acc.next();
    const float dPhase = static_cast<float> (2.0 * 10.0 / sr);
    const float maxStep = dPhase * 1.5f;

    for (int i = 0; i < 96000; ++i)
    {
        const float p = acc.next();
        CHECK (p >= 0.0f && p <= 1.0f);

        // Ping-pong continuity check: step difference must be bounded by dPhase without jumps
        const float diff = std::fabs (p - prevPhase);
        CHECK (diff <= maxStep);

        prevPhase = p;
    }
}

TEST (lfo_modes_retrigger_and_free)
{
    const double sr = 44100.0;
    PhaseAccumulator acc;
    acc.prepare (sr);
    acc.setFrequency (5.0f);

    // Retrigger mode
    acc.setSyncType (SyncType::retrigger);
    acc.reset (0.0f);
    for (int i = 0; i < 1000; ++i)
        acc.next();

    CHECK (acc.phase() > 0.05f);
    acc.noteOn (0.0f);
    CHECK_NEAR (acc.phase(), 0.0f, 1.0e-5f);

    // FreeRun mode
    acc.setSyncType (SyncType::freeRun);
    for (int i = 0; i < 500; ++i)
        acc.next();

    const float phaseBeforeNoteOn = acc.phase();
    acc.noteOn (0.0f); // Should NOT reset
    CHECK_NEAR (acc.phase(), phaseBeforeNoteOn, 1.0e-5f);

    // Envelope mode (one-shot)
    acc.setSyncType (SyncType::envelope);
    acc.reset (0.0f);
    const int samplesForOneCycle = static_cast<int> (sr / 5.0f) + 10;
    for (int i = 0; i < samplesForOneCycle; ++i)
        acc.next();

    CHECK (acc.isFinished());
    const float stoppedPhase = acc.phase();
    CHECK_NEAR (stoppedPhase, 1.0f, 1.0e-4f);
    CHECK_NEAR (acc.next(), 1.0f, 1.0e-4f);
}

TEST (lfo_tempo_sync_period_equals_expected_samples)
{
    const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
    const float bpms[] = { 60.0f, 120.0f, 144.0f };
    const size_t divisions[] = { 5, 7, 8 }; // 1/1, 1/4, 1/8
    const BeatModifier modifiers[] = { BeatModifier::straight, BeatModifier::triplet, BeatModifier::dotted };

    for (double sr : sampleRates)
    {
        for (float bpm : bpms)
        {
            for (size_t div : divisions)
            {
                for (BeatModifier mod : modifiers)
                {
                    const float expectedSamples = beatDivisionToSamples (div, mod, bpm, sr);
                    const float freq = beatDivisionToFrequency (div, mod, bpm);

                    PhaseAccumulator acc;
                    acc.prepare (sr);
                    acc.setFrequency (freq);
                    acc.setDirection (Direction::forward);
                    acc.setSyncType (SyncType::retrigger);
                    acc.reset (0.0f);

                    // Advancing exactly expectedSamples samples
                    const int samplesToRun = static_cast<int> (std::round (expectedSamples));
                    for (int i = 0; i < samplesToRun; ++i)
                        acc.next();

                    // After one full period, the phase should have wrapped around to near 0.0
                    const float sampleStep = freq / static_cast<float> (sr);
                    const float currentPhase = acc.phase();
                    CHECK (currentPhase <= sampleStep * 2.0f || currentPhase >= 1.0f - sampleStep * 2.0f);
                }
            }
        }
    }
}

TEST (lfo_host_sync_hook)
{
    PhaseAccumulator acc;
    acc.prepare (44100.0);
    acc.setFrequency (2.0f);

    // Sync to PPQ = 0.0, beats = 4.0 (1 bar)
    acc.syncToHost (0.0, 120.0, 4.0f);
    CHECK_NEAR (acc.phase(), 0.0f, 1.0e-5f);

    // Sync to PPQ = 2.0 (halfway through 4-beat bar)
    acc.syncToHost (2.0, 120.0, 4.0f);
    CHECK_NEAR (acc.phase(), 0.5f, 1.0e-5f);

    // Sync to PPQ = 15.0 with beats = 4.0 -> frac(15/4) = 0.75
    acc.syncToHost (15.0, 120.0, 4.0f);
    CHECK_NEAR (acc.phase(), 0.75f, 1.0e-5f);
}

TEST (lfo_fade_in_and_delay)
{
    const double sr = 44100.0;
    LfoFadeDelay fd;
    fd.prepare (sr);
    fd.setDelay (0.01f);  // 10 ms delay = 441 samples
    fd.setFadeIn (0.02f); // 20 ms fade-in = 882 samples
    fd.reset();
    fd.noteOn();

    // During delay: gain == 0
    for (int i = 0; i < 400; ++i)
    {
        const float gain = fd.nextGain();
        CHECK_NEAR (gain, 0.0f, 1.0e-5f);
    }

    // Step through transition to fade-in
    for (int i = 400; i < 441; ++i)
        fd.nextGain();

    // During fade-in: monotonically increasing
    float prevGain = 0.0f;
    for (int i = 0; i < 882; ++i)
    {
        const float gain = fd.nextGain();
        CHECK (gain >= prevGain);
        CHECK (gain <= 1.0f);
        prevGain = gain;
    }

    // After fade-in: steady 1.0
    for (int i = 0; i < 100; ++i)
    {
        CHECK_NEAR (fd.nextGain(), 1.0f, 1.0e-5f);
    }
}

TEST (lfo_smoother)
{
    const double sr = 48000.0;
    LfoSmoother smoother;
    smoother.prepare (sr);
    smoother.setTime (0.005f); // 5 ms
    smoother.reset (0.0f);

    // Step input from 0.0 to 1.0
    float prev = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        const float out = smoother.process (1.0f);
        CHECK (out >= prev);
        CHECK (out <= 1.0f);
        prev = out;
    }

    // Reached nearly 1.0
    CHECK_NEAR (prev, 1.0f, 0.002f);
}

TEST (lfo_rossler_bounded)
{
    const double sampleRates[] = { 22050.0, 44100.0, 96000.0, 192000.0 };

    for (double sr : sampleRates)
    {
        RosslerAttractor rossler;
        rossler.prepare (sr);
        rossler.reset (42);
        rossler.setFrequency (2.0f);

        float minVal = 1.0f;
        float maxVal = -1.0f;

        // Run 100,000 samples and check strictly bounded in [-1.0, 1.0]
        for (int i = 0; i < 100000; ++i)
        {
            const float val = rossler.next();
            CHECK (std::isfinite (val));
            CHECK (val >= -1.0f);
            CHECK (val <= 1.0f);

            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }

        // Verify non-trivial oscillation
        CHECK (maxVal > 0.1f);
        CHECK (minVal < -0.1f);
    }
}

TEST (lfo_lorenz_bounded)
{
    const double sampleRates[] = { 22050.0, 44100.0, 48000.0 };

    for (double sr : sampleRates)
    {
        LorenzAttractor lorenz;
        lorenz.prepare (sr);
        lorenz.reset (100);
        lorenz.setFrequency (1.0f);

        float minVal = 1.0f;
        float maxVal = -1.0f;

        for (int i = 0; i < 50000; ++i)
        {
            const float val = lorenz.next();
            CHECK (std::isfinite (val));
            CHECK (val >= -1.0f);
            CHECK (val <= 1.0f);

            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }

        CHECK (maxVal > 0.05f);
        CHECK (minVal < -0.05f);
    }
}

TEST (lfo_random_styles)
{
    const double sr = 44100.0;
    RandomLfoEngine rnd;
    rnd.prepare (sr);
    rnd.setFrequency (5.0f);

    const RandomLfoStyle styles[] = {
        RandomLfoStyle::sampleAndHold,
        RandomLfoStyle::sineInterpolate,
        RandomLfoStyle::perlin,
        RandomLfoStyle::lorenz,
        RandomLfoStyle::rossler
    };

    for (auto st : styles)
    {
        rnd.setStyle (st);
        rnd.reset (777);

        for (int i = 0; i < 5000; ++i)
        {
            const float val = rnd.next();
            CHECK (std::isfinite (val));
            CHECK (val >= -1.0f && val <= 1.0f);
        }
    }
}

TEST (lfo_sample_rates_and_block_sizes)
{
    const double sampleRates[] = { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const int blockSizes[] = { 1, 2, 7, 32, 64, 128, 256, 512, 1024, 2048, 4096 };

    for (double sr : sampleRates)
    {
        LfoEngine engine;
        engine.prepare (sr);
        LfoParameters params;
        params.frequency = 4.0f;
        params.fadeIn = 0.05f;
        params.delay = 0.01f;
        params.smoothTime = 0.005f;
        engine.setParameters (params);

        ShapeData sine;
        sine.initSine();
        engine.setShape (sine);
        engine.reset (123);
        engine.noteOn();

        for (int bs : blockSizes)
        {
            float blockBuffer[4096];
            for (int i = 0; i < bs; ++i)
            {
                blockBuffer[i] = engine.next();
                CHECK (std::isfinite (blockBuffer[i]));
                // Denormal check: must not be subnormal
                if (std::fabs (blockBuffer[i]) > 0.0f)
                {
                    CHECK (std::fabs (blockBuffer[i]) >= 1.0e-15f);
                }
            }
        }
    }
}

