// SynthyBoy - core/filters-serum: Comprehensive filter test suite.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/filters/filter.h"
#include "synthyboy/filters/filter_types.h"
#include "../src/filter_dsp.h"
#include "test_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace synthyboy::filters;

namespace
{

// Fast linear-congruential generator for deterministic pseudo-random test vectors
struct SimpleRng
{
    uint32_t state = 123456789;

    uint32_t next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    float nextFloat (float minVal, float maxVal) noexcept
    {
        const float unit = static_cast<float> (next()) / 4294967295.0f;
        return minVal + unit * (maxVal - minVal);
    }
};

/** Measures steady-state RMS amplitude of filter output when driven by a sine of testFreq. */
float measureSineResponse (Filter& filter, float testFreq, double sampleRate, int settleSamples = 512, int measureSamples = 256)
{
    const auto srFloat = static_cast<float> (sampleRate);
    float phase = 0.0f;
    const float phaseInc = 2.0f * kPi * testFreq / srFloat;

    // Settle transients
    for (int i = 0; i < settleSamples; ++i)
    {
        const float in = std::sin (phase);
        phase += phaseInc;
        if (phase >= 2.0f * kPi)
            phase -= 2.0f * kPi;
        filter.process (in);
    }

    // Measure RMS amplitude
    float sumSq = 0.0f;
    for (int i = 0; i < measureSamples; ++i)
    {
        const float in = std::sin (phase);
        phase += phaseInc;
        if (phase >= 2.0f * kPi)
            phase -= 2.0f * kPi;
        const float out = filter.process (in);
        sumSq += out * out;
    }

    return std::sqrt (sumSq / static_cast<float> (measureSamples));
}

} // namespace

// =============================================================================
// Test 1: Metadata and Catalogue Verification
// =============================================================================
TEST (serum_filters_metadata)
{
    CHECK_EQ (kFilterTypeCount, 80);

    for (int t = 0; t < kFilterTypeCount; ++t)
    {
        const char* name = getFilterTypeName (t);
        const char* family = getFilterFamilyName (t);
        const char* varName = getFilterVarName (t);

        CHECK (name != nullptr && name[0] != '\0');
        CHECK (family != nullptr && family[0] != '\0');
        CHECK (varName != nullptr && varName[0] != '\0');
    }
}

// =============================================================================
// Test 2: Magnitude Response at 3 Cutoffs vs Expected Shape Class
// =============================================================================
TEST (serum_filters_magnitude_response)
{
    static constexpr double kSampleRate = 44100.0;
    static constexpr float kTestCutoffs[3] = { 200.0f, 1000.0f, 4000.0f };

    for (int t = 0; t < kFilterTypeCount; ++t)
    {
        const auto type = static_cast<FilterType> (t);
        sbtest::Context ctx (getFilterTypeName (t));

        for (float fc : kTestCutoffs)
        {
            Filter filter;
            filter.prepare (kSampleRate);
            filter.setType (t);

            FilterParams p;
            p.cutoff = fc;
            p.resonance = 0.2f;
            p.drive = 0.0f;
            p.var = 0.0f;
            p.blend = 0.0f;
            p.mix = 1.0f;

            // --- LowPass filters ---
            if (type == FilterType::MGLow6 || type == FilterType::MGLow12 ||
                type == FilterType::MGLow18 || type == FilterType::MGLow24 ||
                type == FilterType::Low6 || type == FilterType::Low12 ||
                type == FilterType::Low18 || type == FilterType::Low24 ||
                type == FilterType::FrenchLP || type == FilterType::GermanLP ||
                type == FilterType::MGLadder || type == FilterType::AcidLadder ||
                type == FilterType::EMSLadder || type == FilterType::MGDirty)
            {
                filter.setParams (p);
                filter.reset();
                const float magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.25f), kSampleRate);
                const float magHigh = measureSineResponse (filter, std::min (18000.0f, fc * 3.5f), kSampleRate);
                CHECK (magLow > magHigh);
            }
            // --- HighPass filters ---
            else if (type == FilterType::High6 || type == FilterType::High12 ||
                     type == FilterType::High18 || type == FilterType::High24)
            {
                filter.setParams (p);
                filter.reset();
                const float magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.25f), kSampleRate);
                const float magHigh = measureSineResponse (filter, std::min (18000.0f, fc * 3.0f), kSampleRate);
                CHECK (magHigh > magLow);
            }
            // --- BandPass filters ---
            else if (type == FilterType::Band12 || type == FilterType::Band24 ||
                     type == FilterType::ExpBPF || type == FilterType::ScreamBP)
            {
                p.resonance = 0.6f;
                filter.setParams (p);
                filter.reset();
                const float magCenter = measureSineResponse (filter, fc, kSampleRate);
                const float magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.2f), kSampleRate);
                const float magHigh = measureSineResponse (filter, std::min (18000.0f, fc * 4.0f), kSampleRate);
                CHECK (magCenter > magLow);
                CHECK (magCenter > magHigh);
            }
            // --- Notch / Bandreject filters ---
            else if (type == FilterType::Notch12 || type == FilterType::Notch24 ||
                     type == FilterType::Bandreject)
            {
                p.resonance = 0.5f;
                filter.setParams (p);
                filter.reset();
                const float magCenter = measureSineResponse (filter, fc, kSampleRate);
                const float magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.15f), kSampleRate);
                const float magHigh = measureSineResponse (filter, std::min (18000.0f, fc * 5.0f), kSampleRate);
                CHECK (magCenter < magLow);
                CHECK (magCenter < magHigh);
            }
            // --- Peak filters ---
            else if (type == FilterType::Peak12 || type == FilterType::Peak24)
            {
                p.resonance = 0.7f;
                filter.setParams (p);
                filter.reset();
                const float magCenter = measureSineResponse (filter, fc, kSampleRate);
                const float magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.15f), kSampleRate);
                CHECK (magCenter > magLow);
            }
            // --- Morphing SVF filters (LBH) ---
            else if (type == FilterType::LBH)
            {
                // morph = 0.0 -> LowPass
                p.var = 0.0f;
                filter.setParams (p);
                filter.reset();
                float magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.25f), kSampleRate);
                float magHigh = measureSineResponse (filter, std::min (18000.0f, fc * 3.5f), kSampleRate);
                CHECK (magLow > magHigh);

                // morph = 1.0 -> HighPass
                p.var = 1.0f;
                filter.setParams (p);
                filter.reset();
                magLow = measureSineResponse (filter, std::max (30.0f, fc * 0.25f), kSampleRate);
                magHigh = measureSineResponse (filter, std::min (18000.0f, fc * 3.0f), kSampleRate);
                CHECK (magHigh > magLow);
            }
            // --- EQ filters ---
            else if (type == FilterType::LowEQ12)
            {
                // Var = 1.0 (+24 dB boost)
                p.var = 1.0f;
                filter.setParams (p);
                filter.reset();
                const float magBoost = measureSineResponse (filter, std::max (30.0f, fc * 0.3f), kSampleRate);
                // Var = 0.0 (-24 dB cut)
                p.var = 0.0f;
                filter.setParams (p);
                filter.reset();
                const float magCut = measureSineResponse (filter, std::max (30.0f, fc * 0.3f), kSampleRate);
                CHECK (magBoost > magCut);
            }
            // --- DJ Mixer filter ---
            else if (type == FilterType::DJMixerXfer)
            {
                if (fc < 500.0f)
                {
                    // Lowpass behavior
                    filter.setParams (p);
                    filter.reset();
                    const float magLow = measureSineResponse (filter, 60.0f, kSampleRate);
                    const float magHigh = measureSineResponse (filter, 2000.0f, kSampleRate);
                    CHECK (magLow > magHigh);
                }
                else if (fc > 2000.0f)
                {
                    // Highpass behavior
                    filter.setParams (p);
                    filter.reset();
                    const float magLow = measureSineResponse (filter, 200.0f, kSampleRate);
                    const float magHigh = measureSineResponse (filter, 8000.0f, kSampleRate);
                    CHECK (magHigh > magLow);
                }
            }
            else
            {
                // General sanity check for other filters: filter processes signal without silence or explosion
                filter.setParams (p);
                filter.reset();
                const float mag = measureSineResponse (filter, fc, kSampleRate);
                CHECK (!std::isnan (mag));
                CHECK (!std::isinf (mag));
                CHECK (mag >= 0.0f);
            }
        }
    }
}

// =============================================================================
// Test 3: Stability Test (Impulse Response Decays, No Runaway Even at Max Res)
// =============================================================================
TEST (serum_filters_stability)
{
    static constexpr double kSampleRates[2] = { 44100.0, 96000.0 };

    for (double sr : kSampleRates)
    {
        for (int t = 0; t < kFilterTypeCount; ++t)
        {
            sbtest::Context ctx (getFilterTypeName (t));

            Filter filter;
            filter.prepare (sr);
            filter.setType (t);

            // Part A: Impulse response decays at high resonance
            FilterParams pDecay;
            pDecay.cutoff = 1000.0f;
            pDecay.resonance = 0.85f;
            pDecay.drive = 0.0f;
            pDecay.var = 0.5f;
            pDecay.blend = 0.5f;
            pDecay.mix = 1.0f;
            filter.setParams (pDecay);
            filter.reset();

            const float firstOut = filter.process (1.0f);
            CHECK (!std::isnan (firstOut));
            CHECK (!std::isinf (firstOut));

            float initialEnergy = firstOut * firstOut;
            float tailEnergy = 0.0f;

            for (int i = 1; i < 2048; ++i)
            {
                const float out = filter.process (0.0f);
                CHECK (!std::isnan (out));
                CHECK (!std::isinf (out));

                if (i < 256)
                    initialEnergy += out * out;
                else if (i >= 1024)
                    tailEnergy += out * out;
            }

            // Impulse response decays over time (tail RMS < initial RMS)
            const float initialRms = std::sqrt (initialEnergy / 256.0f);
            const float tailRms = std::sqrt (tailEnergy / 1024.0f);
            CHECK (tailRms < initialRms);

            // Part B: Stability at extreme resonance (1.0) and high drive (1.0) - must remain bounded
            FilterParams pMax;
            pMax.cutoff = 1000.0f;
            pMax.resonance = 1.0f;
            pMax.drive = 1.0f;
            pMax.var = 0.5f;
            pMax.blend = 0.5f;
            pMax.mix = 1.0f;
            filter.setParams (pMax);
            filter.reset();

            float maxAmp = 0.0f;
            for (int i = 0; i < 1024; ++i)
            {
                const float in = (i == 0) ? 1.0f : 0.0f;
                const float out = filter.process (in);
                CHECK (!std::isnan (out));
                CHECK (!std::isinf (out));
                maxAmp = std::max (maxAmp, std::abs (out));
            }
            CHECK (maxAmp < 10.0f);
        }
    }
}

// =============================================================================
// Test 4: NaN and Inf Fuzzing (10,000 Random Trials Across Full Range)
// =============================================================================
TEST (serum_filters_fuzz_nan_inf)
{
    Filter filter;
    filter.prepare (44100.0);

    SimpleRng rng;
    static constexpr double kTestSR[4] = { 22050.0, 44100.0, 88200.0, 192000.0 };

    for (int trial = 0; trial < 10000; ++trial)
    {
        // Randomly re-prepare sample rate occasionally
        if ((trial % 1000) == 0)
        {
            const double sr = kTestSR[(trial / 1000) % 4];
            filter.prepare (sr);
        }

        const int type = static_cast<int> (rng.next() % static_cast<uint32_t> (kFilterTypeCount));
        filter.setType (type);

        FilterParams p;
        p.cutoff = rng.nextFloat (10.0f, 24000.0f);
        p.resonance = rng.nextFloat (0.0f, 1.0f);
        p.drive = rng.nextFloat (0.0f, 1.0f);
        p.var = rng.nextFloat (0.0f, 1.0f);
        p.blend = rng.nextFloat (0.0f, 1.0f);
        p.mix = rng.nextFloat (0.0f, 1.0f);
        filter.setParams (p);

        // Process a block of 16 samples with extreme input spikes and noise
        float inBlock[16];
        float outBlock[16];
        for (int i = 0; i < 16; ++i)
            inBlock[i] = rng.nextFloat (-4.0f, 4.0f);

        filter.process (inBlock, outBlock, 16);

        for (int i = 0; i < 16; ++i)
        {
            const float s = outBlock[i];
            CHECK (!std::isnan (s));
            CHECK (!std::isinf (s));
        }
    }
}

// =============================================================================
// Test 5: Determinism (Identical Inputs & Params -> Bit-Identical Outputs)
// =============================================================================
TEST (serum_filters_determinism)
{
    static constexpr int kBlockSize = 256;
    std::vector<float> inBuffer (kBlockSize);
    std::vector<float> out1 (kBlockSize);
    std::vector<float> out2 (kBlockSize);

    SimpleRng rng;
    for (int i = 0; i < kBlockSize; ++i)
        inBuffer[i] = rng.nextFloat (-1.0f, 1.0f);

    for (int t = 0; t < kFilterTypeCount; ++t)
    {
        sbtest::Context ctx (getFilterTypeName (t));

        Filter f1;
        Filter f2;
        f1.prepare (48000.0);
        f2.prepare (48000.0);

        f1.setType (t);
        f2.setType (t);

        FilterParams p;
        p.cutoff = 1500.0f;
        p.resonance = 0.6f;
        p.drive = 0.3f;
        p.var = 0.4f;
        p.blend = 0.2f;
        p.mix = 0.8f;

        f1.setParams (p);
        f2.setParams (p);
        f1.reset();
        f2.reset();

        f1.process (inBuffer.data(), out1.data(), kBlockSize);
        f2.process (inBuffer.data(), out2.data(), kBlockSize);

        for (int i = 0; i < kBlockSize; ++i)
            CHECK_EQ (out1[i], out2[i]);
    }
}

// =============================================================================
// Model / style table of filterN.model (8 .. 26) -> FilterType, shared by the engine and the UI
// =============================================================================
TEST (serum_filters_model_style_table_reaches_every_type_exactly_once)
{
    std::vector<int> hits (static_cast<size_t> (kFilterTypeCount), 0);
    int total = 0;
    for (int model = kFirstSerumModel; model <= kLastSerumModel; ++model)
    {
        const int styles = serumModelStyleCount (model);
        CHECK (styles >= 1 && styles <= 12);
        for (int style = 0; style < styles; ++style)
        {
            const int type = serumFilterTypeOf (model, style);
            CHECK (type >= 0 && type < kFilterTypeCount);
            if (type >= 0 && type < kFilterTypeCount)
                ++hits[static_cast<size_t> (type)];
            ++total;
        }
        // a style beyond the last clamps to the last, a negative one to the first
        CHECK_EQ (serumFilterTypeOf (model, styles + 5), serumFilterTypeOf (model, styles - 1));
        CHECK_EQ (serumFilterTypeOf (model, -3), serumFilterTypeOf (model, 0));
    }
    CHECK_EQ (total, kFilterTypeCount);
    for (int t = 0; t < kFilterTypeCount; ++t)
        CHECK_EQ (hits[static_cast<size_t> (t)], 1);
    CHECK_EQ (serumModelStyleCount (7), 0);
    CHECK_EQ (serumModelStyleCount (27), 0);
    // the query of the UI: the styles a model really has (classic models 0 .. 7, then the Serum table)
    const int kExpected[8] = { 6, 6, 6, 6, 2, 3, 6, 2 };
    for (int model = 0; model < kFirstSerumModel; ++model)
        CHECK_EQ (filterModelStyleCount (model), kExpected[model]);
    for (int model = kFirstSerumModel; model <= kLastSerumModel; ++model)
        CHECK_EQ (filterModelStyleCount (model), serumModelStyleCount (model));
    CHECK_EQ (filterModelStyleCount (-1), 0);
    CHECK_EQ (filterModelStyleCount (27), 0);
    // the names of the new models' styles
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (22, 4))) == "High 12");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (23, 3))) == "BPN");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (24, 8))) == "Phs HL");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (25, 11))) == "Dist.Comb 2 BP");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (26, 5))) == "Exp BPF");

    // filterModelStyleNames and filterModelStyleName queries
    CHECK (filterModelStyleNames (-1) == nullptr);
    CHECK (filterModelStyleNames (27) == nullptr);
    CHECK (std::string (filterModelStyleName (-1, 0)).empty());
    CHECK (std::string (filterModelStyleName (27, 0)).empty());

    for (int model = 0; model <= kLastSerumModel; ++model)
    {
        const int cnt = filterModelStyleCount (model);
        CHECK (cnt > 0);
        const char* const* names = filterModelStyleNames (model);
        CHECK (names != nullptr);
        for (int s = 0; s < cnt; ++s)
        {
            CHECK (names[s] != nullptr);
            const char* name = filterModelStyleName (model, s);
            CHECK (name != nullptr && name[0] != '\0');
            CHECK_EQ (std::string (name), std::string (names[s]));
            if (model >= kFirstSerumModel)
            {
                const char* expected = getFilterTypeName (serumFilterTypeOf (model, s));
                CHECK_EQ (std::string (name), std::string (expected));
            }
        }
        CHECK (std::string (filterModelStyleName (model, -1)).empty());
        CHECK (std::string (filterModelStyleName (model, cnt)).empty());
    }

    // Classic model style names verification
    CHECK_EQ (std::string (filterModelStyleName (0, 0)), "Style A (12 dB)");
    CHECK_EQ (std::string (filterModelStyleName (0, 5)), "Style F (shelf)");
    CHECK_EQ (std::string (filterModelStyleName (4, 0)), "Style A (12 dB)");
    CHECK_EQ (std::string (filterModelStyleName (4, 1)), "Style B (24 dB)");
    CHECK_EQ (std::string (filterModelStyleName (5, 0)), "A-O-I-E");
    CHECK_EQ (std::string (filterModelStyleName (5, 1)), "A-I-U-O");
    CHECK_EQ (std::string (filterModelStyleName (5, 2)), "Vocal Tract");
    CHECK_EQ (std::string (filterModelStyleName (6, 0)), "Comb");
    CHECK_EQ (std::string (filterModelStyleName (7, 0)), "Positive Feedback");
    CHECK_EQ (std::string (filterModelStyleName (7, 1)), "Negative Feedback");
}

// FilterParams::jump (quality.no_smoothing of the own engine): a step of the cutoff applies at once instead of through the one pole smoother
TEST (serum_filters_jump_skips_the_cutoff_smoother)
{
    auto tail = [] (bool jump)
    {
        Filter f;
        f.prepare (48000.0);
        f.setType (static_cast<int> (FilterType::Low12));
        FilterParams p;
        p.cutoff = 18000.0f;
        p.resonance = 0.1f;
        f.setParams (p);
        std::vector<float> in (64), out (64);
        const float w = 2.0f * kPi * 5000.0f / 48000.0f;
        for (int k = 0; k < 40; ++k) // settle on the open filter
            for (int i = 0; i < 64; ++i)
                f.process (std::sin (w * static_cast<float> (k * 64 + i)));
        p.cutoff = 200.0f;
        p.jump = jump;
        f.setParams (p);
        float peak = 0.0f;
        // the states hold the 5 kHz tone when the cutoff drops, and a state variable filter rings them out at its new cutoff (about 40 samples to 1/e at 200 Hz): the window starts at
        // sample 192, when that is over; the one pole smoother (270 samples to 1/e) still has the cutoff at several kHz there
        for (int i = 0; i < 256; ++i)
        {
            const float y = f.process (std::sin (w * static_cast<float> (2560 + i)));
            if (i >= 192)
                peak = std::max (peak, std::fabs (y));
        }
        return peak;
    };
    const float smooth = tail (false), jumped = tail (true);
    std::printf ("    5 kHz after a step of the cutoff to 200 Hz: peak %.3f with the smoother, %.3f with jump\n", static_cast<double> (smooth), static_cast<double> (jumped));
    CHECK (smooth > 0.4f);
    CHECK (jumped < 0.15f);
}

// Rank 14: Combs and Reverb must use 4 independent comb feedback low-pass filters instead of sharing one
TEST (serum_filters_combs_and_reverb_independent_comb_damping)
{
    // Test 1: Combs filter type
    {
        constexpr double sampleRate = 44100.0;
        constexpr float srFloat = static_cast<float> (sampleRate);
        Filter filter;
        filter.prepare (sampleRate);
        filter.setType (static_cast<int> (FilterType::Combs));

        FilterParams p;
        p.cutoff = 800.0f;
        p.resonance = 0.7f;
        p.drive = 0.0f;
        p.var = 0.5f; // Damping clearly on: damping LP cutoff ~ 11025 Hz
        p.mix = 1.0f; // 100% wet
        filter.setParams (p);
        filter.reset();

        // Independent 4-comb reference as specified by the filter architecture:
        // 4 parallel comb lines each with its own independent OnePoleTPT feedback low-pass filter
        DelayLine refCombs[4];
        static constexpr float kReverbCombMs[4] = { 29.7f, 37.1f, 41.3f, 43.7f };
        for (int i = 0; i < 4; ++i)
        {
            const size_t combLen = static_cast<size_t> (kReverbCombMs[i] * 0.001f * srFloat) + 16;
            refCombs[i].prepare (combLen);
            refCombs[i].reset();
        }
        OnePoleTPT refLP[4];
        for (int i = 0; i < 4; ++i)
            refLP[i].reset();

        static constexpr float kCombRatios[4] = { 1.0f, 1.33f, 1.67f, 2.11f };
        const float baseDelay = std::max (2.0f, srFloat / std::max (20.0f, p.cutoff));
        const float dampG = std::tan (kPi * std::max (100.0f, srFloat * (0.45f - 0.4f * p.var)) / srFloat);

        constexpr int numSamples = 4096;
        float maxDiff = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float in = (n == 0) ? 1.0f : 0.0f; // Impulse
            const float out = filter.process (in);

            // Compute reference output
            const float drivenIn = softClip ((1.0f + 2.5f * p.drive) * in);
            float sum = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float d = std::max (2.0f, std::min (baseDelay * kCombRatios[i], static_cast<float> (sampleRate * 0.05)));
                const float delayed = refCombs[i].read (d);
                const float damped = refLP[i].processLPOnly (delayed, dampG);
                refCombs[i].write (drivenIn + softClip (0.85f * p.resonance * damped));
                sum += delayed;
            }
            const float rawOut = 0.25f * sum;
            const float refOut = sanitize ((1.0f - p.mix) * in + p.mix * rawOut);

            const float diff = std::fabs (out - refOut);
            if (diff > maxDiff)
                maxDiff = diff;
        }

        std::printf ("    Combs filter impulse response max diff vs independent 4-comb reference: %e\n", static_cast<double> (maxDiff));
        CHECK (maxDiff < 1e-5f);
    }

    // Test 2: Reverb filter type
    {
        constexpr double sampleRate = 44100.0;
        constexpr float srFloat = static_cast<float> (sampleRate);
        Filter filter;
        filter.prepare (sampleRate);
        filter.setType (static_cast<int> (FilterType::Reverb));

        FilterParams p;
        p.cutoff = 1000.0f;
        p.resonance = 0.7f;
        p.drive = 0.0f;
        p.var = 0.5f; // Damping clearly on: dampG = tan(pi * max(200, srFloat * (0.4 - 0.35 * 0.5)) / srFloat)
        p.mix = 1.0f; // 100% wet
        filter.setParams (p);
        filter.reset();

        // Independent 4-comb + 2-allpass reference
        DelayLine refCombs[4];
        static constexpr float kReverbCombMs[4] = { 29.7f, 37.1f, 41.3f, 43.7f };
        for (int i = 0; i < 4; ++i)
        {
            const size_t combLen = static_cast<size_t> (kReverbCombMs[i] * 0.001f * srFloat) + 16;
            refCombs[i].prepare (combLen);
            refCombs[i].reset();
        }
        OnePoleTPT refLP[4];
        for (int i = 0; i < 4; ++i)
            refLP[i].reset();

        AllpassDiffuser refAP[2];
        refAP[0].prepare (static_cast<size_t> (5.1f * 0.001f * srFloat) + 16, 0.7f);
        refAP[1].prepare (static_cast<size_t> (12.7f * 0.001f * srFloat) + 16, 0.7f);
        refAP[0].reset();
        refAP[1].reset();

        const float dampG = std::tan (kPi * std::max (200.0f, srFloat * (0.4f - 0.35f * p.var)) / srFloat);
        static constexpr float kRevDelayMs[4] = { 25.0f, 32.0f, 37.0f, 41.0f };

        constexpr int numSamples = 4096;
        float maxDiff = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float in = (n == 0) ? 1.0f : 0.0f; // Impulse
            const float out = filter.process (in);

            // Compute reference output
            const float drivenIn = softClip ((1.0f + 2.5f * p.drive) * in);
            float sum = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float d = kRevDelayMs[i] * 0.001f * srFloat;
                const float delayed = refCombs[i].read (d);
                const float damped = refLP[i].processLPOnly (delayed, dampG);
                refCombs[i].write (drivenIn + softClip (0.8f * p.resonance * damped));
                sum += delayed;
            }
            float rev = refAP[0].process (sum * 0.25f);
            rev = refAP[1].process (rev);
            const float rawOut = 0.5f * drivenIn + 0.5f * rev;
            const float refOut = sanitize ((1.0f - p.mix) * in + p.mix * rawOut);

            const float diff = std::fabs (out - refOut);
            if (diff > maxDiff)
                maxDiff = diff;
        }

        std::printf ("    Reverb filter impulse response max diff vs independent 4-comb reference: %e\n", static_cast<double> (maxDiff));
        CHECK (maxDiff < 1e-5f);
    }
}
