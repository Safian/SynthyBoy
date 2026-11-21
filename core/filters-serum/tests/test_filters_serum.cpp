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

// =============================================================================
// Tests T1 - T5: Ladder Cutoff Range & Stability (Audit Part 1, Rank 1)
// =============================================================================

namespace
{

static double findCornerFrequency (Filter& filter, double sampleRate, double targetRatio = 0.70710678)
{
    filter.reset();
    constexpr int kIrLen = 4096;
    std::vector<float> ir (kIrLen, 0.0f);
    for (int i = 0; i < kIrLen; ++i)
        ir[i] = filter.process (i == 0 ? 1.0e-3f : 0.0f) / 1.0e-3f;

    double dcGain = 0.0;
    for (int i = 0; i < kIrLen; ++i)
        dcGain += ir[i];
    const double targetMag = std::fabs (dcGain) * targetRatio;

    double low = 50.0, high = sampleRate * 0.499;
    for (int iter = 0; iter < 24; ++iter)
    {
        const double mid = 0.5 * (low + high);
        double re = 0.0, im = 0.0;
        const double omega = 2.0 * 3.14159265358979323846 * mid / sampleRate;
        for (int i = 0; i < kIrLen; ++i)
        {
            re += ir[i] * std::cos (omega * i);
            im -= ir[i] * std::sin (omega * i);
        }
        const double mag = std::sqrt (re * re + im * im);
        if (mag > targetMag)
            low = mid;
        else
            high = mid;
    }
    return 0.5 * (low + high);
}

struct T2Expected
{
    int model;
    float cutoff;
    float res;
    double expectedSumAbs;
    float expectedLast8[8];
};

static const T2Expected kT2Expected[84] = {
{ 0, 100.0f, 0.0f, 9158.78835208, { 0.18814294f, 0.19239958f, 0.19664507f, 0.20087908f, 0.20510131f, 0.20931146f, 0.21350922f, 0.21769430f } },
    { 0, 100.0f, 0.5f, 10884.56176468, { 0.21022606f, 0.21574859f, 0.22124603f, 0.22671774f, 0.23216309f, 0.23758146f, 0.24297227f, 0.24833494f } },
    { 0, 100.0f, 1.0f, 12604.38346551, { 0.21425933f, 0.22128616f, 0.22827347f, 0.23522022f, 0.24212547f, 0.24898827f, 0.25580773f, 0.26258296f } },
    { 0, 1000.0f, 0.0f, 16766.81013783, { 0.46106640f, 0.46479121f, 0.46848184f, 0.47213838f, 0.47576091f, 0.47934952f, 0.48290434f, 0.48642546f } },
    { 0, 1000.0f, 0.5f, 7440.74569683, { 0.20178168f, 0.20315765f, 0.20452152f, 0.20587458f, 0.20721804f, 0.20855305f, 0.20988071f, 0.21120206f } },
    { 0, 1000.0f, 1.0f, 8354.77617955, { 0.27259400f, 0.26536804f, 0.25560293f, 0.24350241f, 0.22931796f, 0.21334478f, 0.19591595f, 0.17739497f } },
    { 0, 5000.0f, 0.0f, 17476.61423114, { 0.48251826f, 0.48604879f, 0.48954552f, 0.49300861f, 0.49643815f, 0.49983424f, 0.50319713f, 0.50652689f } },
    { 0, 5000.0f, 0.5f, 6856.34713291, { 0.18695819f, 0.18849334f, 0.19001968f, 0.19153722f, 0.19304594f, 0.19454582f, 0.19603683f, 0.19751893f } },
    { 0, 5000.0f, 1.0f, 4662.08380003, { 0.11321379f, 0.11686634f, 0.12050706f, 0.12290825f, 0.12344849f, 0.12237168f, 0.12064462f, 0.11949164f } },
    { 0, 9000.0f, 0.0f, 17539.92315759, { 0.48492429f, 0.48843199f, 0.49190596f, 0.49534634f, 0.49875325f, 0.50212681f, 0.50546724f, 0.50877464f } },
    { 0, 9000.0f, 0.5f, 6796.16577721, { 0.18517742f, 0.18671919f, 0.18825215f, 0.18977633f, 0.19129176f, 0.19279830f, 0.19429588f, 0.19578455f } },
    { 0, 9000.0f, 1.0f, 4400.95823632, { 0.11431713f, 0.11547569f, 0.11686788f, 0.11788020f, 0.11843497f, 0.11906519f, 0.12016598f, 0.12147238f } },
    { 1, 100.0f, 0.0f, 5468.71533477, { -0.02426079f, -0.02122580f, -0.01817360f, -0.01510460f, -0.01201920f, -0.00891781f, -0.00580082f, -0.00266862f } },
    { 1, 100.0f, 0.5f, 6561.13449328, { -0.05357516f, -0.04980409f, -0.04600842f, -0.04218885f, -0.03834610f, -0.03448086f, -0.03059385f, -0.02668577f } },
    { 1, 100.0f, 1.0f, 7656.46217499, { -0.09699751f, -0.09254444f, -0.08805525f, -0.08353099f, -0.07897276f, -0.07438162f, -0.06975865f, -0.06510492f } },
    { 1, 1000.0f, 0.0f, 16303.98032255, { 0.43316078f, 0.43712622f, 0.44105732f, 0.44495413f, 0.44881666f, 0.45264494f, 0.45643905f, 0.46019906f } },
    { 1, 1000.0f, 0.5f, 7134.11610732, { 0.19122060f, 0.19271910f, 0.19420047f, 0.19566546f, 0.19711491f, 0.19854969f, 0.19997068f, 0.20137882f } },
    { 1, 1000.0f, 1.0f, 6632.55743924, { 0.21964747f, 0.22621927f, 0.23078392f, 0.23328413f, 0.23370056f, 0.23205285f, 0.22839986f, 0.22283903f } },
    { 1, 5000.0f, 0.0f, 17384.76280106, { 0.47768942f, 0.48126566f, 0.48480794f, 0.48831639f, 0.49179113f, 0.49523228f, 0.49864000f, 0.50201440f } },
    { 1, 5000.0f, 0.5f, 6809.41431959, { 0.18487354f, 0.18642040f, 0.18795852f, 0.18948787f, 0.19100843f, 0.19252017f, 0.19402307f, 0.19551711f } },
    { 1, 5000.0f, 1.0f, 4377.40017272, { 0.11171497f, 0.11351829f, 0.11632131f, 0.11924253f, 0.12137708f, 0.12220848f, 0.12182867f, 0.12087387f } },
    { 1, 9000.0f, 0.0f, 17508.27343845, { 0.48254776f, 0.48607811f, 0.48957470f, 0.49303761f, 0.49646696f, 0.49986288f, 0.50322556f, 0.50655508f } },
    { 1, 9000.0f, 0.5f, 6766.05906747, { 0.18413758f, 0.18568528f, 0.18722413f, 0.18875419f, 0.19027549f, 0.19178799f, 0.19329156f, 0.19478619f } },
    { 1, 9000.0f, 1.0f, 4234.35646661, { 0.11390563f, 0.11475248f, 0.11596560f, 0.11716944f, 0.11801404f, 0.11864316f, 0.11947433f, 0.12062382f } },
    { 2, 100.0f, 0.0f, 3349.07286285, { -0.09514034f, -0.09411614f, -0.09306337f, -0.09198219f, -0.09087278f, -0.08973530f, -0.08856992f, -0.08737683f } },
    { 2, 100.0f, 0.5f, 4021.01779614, { -0.12627889f, -0.12522368f, -0.12412987f, -0.12299766f, -0.12182727f, -0.12061892f, -0.11937283f, -0.11808922f } },
    { 2, 100.0f, 1.0f, 4693.64375114, { -0.16017190f, -0.15924667f, -0.15827127f, -0.15724592f, -0.15617083f, -0.15504622f, -0.15387236f, -0.15264948f } },
    { 2, 1000.0f, 0.0f, 15952.50967822, { 0.40358666f, 0.40779039f, 0.41196010f, 0.41609573f, 0.42019728f, 0.42426470f, 0.42829800f, 0.43229714f } },
    { 2, 1000.0f, 0.5f, 6907.34010443, { 0.17985950f, 0.18147275f, 0.18306957f, 0.18464991f, 0.18621385f, 0.18776156f, 0.18929331f, 0.19080946f } },
    { 2, 1000.0f, 1.0f, 5479.73393419, { 0.13888356f, 0.15007997f, 0.16052663f, 0.17005225f, 0.17850320f, 0.18574637f, 0.19167161f, 0.19619384f } },
    { 2, 5000.0f, 0.0f, 17320.26072722, { 0.47279879f, 0.47642091f, 0.48000896f, 0.48356301f, 0.48708317f, 0.49056959f, 0.49402240f, 0.49744171f } },
    { 2, 5000.0f, 0.5f, 6768.57483452, { 0.18277326f, 0.18433177f, 0.18588160f, 0.18742269f, 0.18895504f, 0.19047861f, 0.19199337f, 0.19349930f } },
    { 2, 5000.0f, 1.0f, 4230.38748329, { 0.11187701f, 0.11227813f, 0.11371081f, 0.11591873f, 0.11830017f, 0.12019435f, 0.12118369f, 0.12127458f } },
    { 2, 9000.0f, 0.0f, 17481.28534587, { 0.48015597f, 0.48370907f, 0.48722830f, 0.49071378f, 0.49416563f, 0.49758396f, 0.50096893f, 0.50432068f } },
    { 2, 9000.0f, 0.5f, 6748.93201781, { 0.18309385f, 0.18464741f, 0.18619217f, 0.18772811f, 0.18925528f, 0.19077368f, 0.19228321f, 0.19378383f } },
    { 2, 9000.0f, 1.0f, 4187.43932458, { 0.11346684f, 0.11420380f, 0.11519123f, 0.11636759f, 0.11741391f, 0.11819572f, 0.11893337f, 0.11988700f } },
    { 3, 100.0f, 0.0f, 2046.16603282, { -0.06766520f, -0.06804664f, -0.06840800f, -0.06874914f, -0.06906997f, -0.06937037f, -0.06965023f, -0.06990945f } },
    { 3, 100.0f, 0.5f, 2449.28067713, { -0.08123755f, -0.08186729f, -0.08247292f, -0.08305424f, -0.08361104f, -0.08414315f, -0.08465036f, -0.08513250f } },
    { 3, 100.0f, 1.0f, 2857.65472178, { -0.09128016f, -0.09224823f, -0.09318916f, -0.09410264f, -0.09498832f, -0.09584592f, -0.09667513f, -0.09747565f } },
    { 3, 1000.0f, 0.0f, 15655.99357697, { 0.37238657f, 0.37682280f, 0.38122576f, 0.38559538f, 0.38993156f, 0.39423421f, 0.39850327f, 0.40273866f } },
    { 3, 1000.0f, 0.5f, 6716.88108709, { 0.16796051f, 0.16965304f, 0.17133392f, 0.17300251f, 0.17465824f, 0.17630066f, 0.17792940f, 0.17954418f } },
    { 3, 1000.0f, 1.0f, 4721.13507913, { 0.07999444f, 0.08858489f, 0.09747256f, 0.10650655f, 0.11553445f, 0.12440505f, 0.13297108f, 0.14109187f } },
    { 3, 5000.0f, 0.0f, 17270.59501560, { 0.46784630f, 0.47151443f, 0.47514838f, 0.47874820f, 0.48231399f, 0.48584589f, 0.48934403f, 0.49280849f } },
    { 3, 5000.0f, 0.5f, 6733.23433883, { 0.18065742f, 0.18222754f, 0.18378900f, 0.18534179f, 0.18688586f, 0.18842119f, 0.18994775f, 0.19146553f } },
    { 3, 5000.0f, 1.0f, 4168.56332371, { 0.11218585f, 0.11212713f, 0.11259753f, 0.11380002f, 0.11559485f, 0.11757569f, 0.11926416f, 0.12032983f } },
    { 3, 9000.0f, 0.0f, 17445.91806668, { 0.47774890f, 0.48132476f, 0.48486671f, 0.48837483f, 0.49184921f, 0.49529001f, 0.49869737f, 0.50207138f } },
    { 3, 9000.0f, 0.5f, 6731.88507956, { 0.18204620f, 0.18360560f, 0.18515626f, 0.18669809f, 0.18823114f, 0.18975542f, 0.19127089f, 0.19277747f } },
    { 3, 9000.0f, 1.0f, 4153.77696920, { 0.11291017f, 0.11370093f, 0.11455274f, 0.11560122f, 0.11670343f, 0.11764482f, 0.11843094f, 0.11926793f } },
    { 72, 100.0f, 0.0f, 2046.16603282, { -0.06766520f, -0.06804664f, -0.06840800f, -0.06874914f, -0.06906997f, -0.06937037f, -0.06965023f, -0.06990945f } },
    { 72, 100.0f, 0.5f, 2449.28067713, { -0.08123755f, -0.08186729f, -0.08247292f, -0.08305424f, -0.08361104f, -0.08414315f, -0.08465036f, -0.08513250f } },
    { 72, 100.0f, 1.0f, 2857.65472178, { -0.09128016f, -0.09224823f, -0.09318916f, -0.09410264f, -0.09498832f, -0.09584592f, -0.09667513f, -0.09747565f } },
    { 72, 1000.0f, 0.0f, 15655.99357697, { 0.37238657f, 0.37682280f, 0.38122576f, 0.38559538f, 0.38993156f, 0.39423421f, 0.39850327f, 0.40273866f } },
    { 72, 1000.0f, 0.5f, 6716.88108709, { 0.16796051f, 0.16965304f, 0.17133392f, 0.17300251f, 0.17465824f, 0.17630066f, 0.17792940f, 0.17954418f } },
    { 72, 1000.0f, 1.0f, 4721.13507913, { 0.07999444f, 0.08858489f, 0.09747256f, 0.10650655f, 0.11553445f, 0.12440505f, 0.13297108f, 0.14109187f } },
    { 72, 5000.0f, 0.0f, 17270.59501560, { 0.46784630f, 0.47151443f, 0.47514838f, 0.47874820f, 0.48231399f, 0.48584589f, 0.48934403f, 0.49280849f } },
    { 72, 5000.0f, 0.5f, 6733.23433883, { 0.18065742f, 0.18222754f, 0.18378900f, 0.18534179f, 0.18688586f, 0.18842119f, 0.18994775f, 0.19146553f } },
    { 72, 5000.0f, 1.0f, 4168.56332371, { 0.11218585f, 0.11212713f, 0.11259753f, 0.11380002f, 0.11559485f, 0.11757569f, 0.11926416f, 0.12032983f } },
    { 72, 9000.0f, 0.0f, 17445.91806668, { 0.47774890f, 0.48132476f, 0.48486671f, 0.48837483f, 0.49184921f, 0.49529001f, 0.49869737f, 0.50207138f } },
    { 72, 9000.0f, 0.5f, 6731.88507956, { 0.18204620f, 0.18360560f, 0.18515626f, 0.18669809f, 0.18823114f, 0.18975542f, 0.19127089f, 0.19277747f } },
    { 72, 9000.0f, 1.0f, 4153.77696920, { 0.11291017f, 0.11370093f, 0.11455274f, 0.11560122f, 0.11670343f, 0.11764482f, 0.11843094f, 0.11926793f } },
    { 74, 100.0f, 0.0f, 2676.87848235, { -0.08877771f, -0.08927424f, -0.08974404f, -0.09018695f, -0.09060284f, -0.09099155f, -0.09135295f, -0.09168690f } },
    { 74, 100.0f, 0.5f, 3100.49346806, { -0.10720130f, -0.10794643f, -0.10866022f, -0.10934245f, -0.10999287f, -0.11061127f, -0.11119743f, -0.11175115f } },
    { 74, 100.0f, 1.0f, 3480.18510647, { -0.12062345f, -0.12165070f, -0.12264298f, -0.12359995f, -0.12452129f, -0.12540667f, -0.12625580f, -0.12706837f } },
    { 74, 1000.0f, 0.0f, 20393.50725264, { 0.49933007f, 0.50481421f, 0.51024115f, 0.51561093f, 0.52092361f, 0.52617931f, 0.53137815f, 0.53652024f } },
    { 74, 1000.0f, 0.5f, 9632.65480319, { 0.23814064f, 0.24057114f, 0.24299492f, 0.24541152f, 0.24782048f, 0.25022137f, 0.25261375f, 0.25499719f } },
    { 74, 1000.0f, 1.0f, 6272.00251503, { 0.10221993f, 0.10612141f, 0.11085104f, 0.11633624f, 0.12249160f, 0.12922065f, 0.13641781f, 0.14397041f } },
    { 74, 5000.0f, 0.0f, 22419.56856245, { 0.61492050f, 0.61911845f, 0.62326306f, 0.62735486f, 0.63139421f, 0.63538152f, 0.63931721f, 0.64320165f } },
    { 74, 5000.0f, 0.5f, 9748.18018908, { 0.25942484f, 0.26180255f, 0.26417050f, 0.26652858f, 0.26887670f, 0.27121481f, 0.27354285f, 0.27586076f } },
    { 74, 5000.0f, 1.0f, 5937.43064369, { 0.15946169f, 0.16091098f, 0.16223148f, 0.16346493f, 0.16468562f, 0.16596596f, 0.16734503f, 0.16881354f } },
    { 74, 9000.0f, 0.0f, 22636.60007501, { 0.62625915f, 0.63031328f, 0.63431519f, 0.63826543f, 0.64216429f, 0.64601219f, 0.64980960f, 0.65355706f } },
    { 74, 9000.0f, 0.5f, 9721.08585536, { 0.26105142f, 0.26339898f, 0.26573631f, 0.26806331f, 0.27037987f, 0.27268597f, 0.27498159f, 0.27726662f } },
    { 74, 9000.0f, 1.0f, 5915.16789877, { 0.15982370f, 0.16122790f, 0.16262329f, 0.16400246f, 0.16537058f, 0.16673757f, 0.16810487f, 0.16946432f } },
    { 75, 100.0f, 0.0f, 1913.53904538, { -0.06344049f, -0.06379453f, -0.06412950f, -0.06444532f, -0.06474189f, -0.06501909f, -0.06527684f, -0.06551503f } },
    { 75, 100.0f, 0.5f, 2303.42631676, { -0.07556473f, -0.07616404f, -0.07674050f, -0.07729394f, -0.07782419f, -0.07833109f, -0.07881443f, -0.07927407f } },
    { 75, 100.0f, 1.0f, 2707.64242329, { -0.08416481f, -0.08511125f, -0.08603215f, -0.08692716f, -0.08779598f, -0.08863834f, -0.08945394f, -0.09024251f } },
    { 75, 1000.0f, 0.0f, 13873.08251177, { 0.34372514f, 0.34723407f, 0.35069871f, 0.35411936f, 0.35749623f, 0.36082965f, 0.36411986f, 0.36736712f } },
    { 75, 1000.0f, 0.5f, 6075.69146890, { 0.15645196f, 0.15780638f, 0.15914485f, 0.16046688f, 0.16177215f, 0.16306040f, 0.16433144f, 0.16558518f } },
    { 75, 1000.0f, 1.0f, 4251.75270351, { 0.08254799f, 0.09079711f, 0.09915003f, 0.10746191f, 0.11559021f, 0.12339747f, 0.13075379f, 0.13753925f } },
    { 75, 5000.0f, 0.0f, 15126.97334059, { 0.41589764f, 0.41845492f, 0.42097524f, 0.42345896f, 0.42590666f, 0.42831868f, 0.43069538f, 0.43303734f } },
    { 75, 5000.0f, 0.5f, 6076.84753185, { 0.16604261f, 0.16725327f, 0.16845207f, 0.16963904f, 0.17081425f, 0.17197773f, 0.17312951f, 0.17426971f } },
    { 75, 5000.0f, 1.0f, 3783.71398863, { 0.10315087f, 0.10298029f, 0.10346075f, 0.10469000f, 0.10641769f, 0.10817212f, 0.10948321f, 0.11009639f } },
    { 75, 9000.0f, 0.0f, 15259.04592128, { 0.42279077f, 0.42524847f, 0.42767045f, 0.43005705f, 0.43240869f, 0.43472582f, 0.43700892f, 0.43925840f } },
    { 75, 9000.0f, 0.5f, 6072.85719241, { 0.16709945f, 0.16829711f, 0.16948296f, 0.17065696f, 0.17181921f, 0.17296982f, 0.17410882f, 0.17523621f } },
    { 75, 9000.0f, 1.0f, 3769.22287200, { 0.10411514f, 0.10466693f, 0.10534050f, 0.10622008f, 0.10709874f, 0.10777202f, 0.10831271f, 0.10895835f } },
};

} // namespace

// T1: Ladder Cutoff Defect and Correction
TEST (serum_ladder_cutoff_t1_defect)
{
    const double sr = 44100.0;
    const FilterType ladderModels[] = {
        FilterType::MGLow6, FilterType::MGLow12, FilterType::MGLow18, FilterType::MGLow24,
        FilterType::MGLadder, FilterType::EMSLadder, FilterType::MGDirty
    };

    for (FilterType m : ladderModels)
    {
        Filter filter;
        filter.prepare (sr);
        filter.setType (static_cast<int> (m));
        FilterParams p;
        p.resonance = 0.0f;
        p.drive = 0.0f;
        p.var = 0.0f;
        p.blend = 0.0f;
        p.mix = 1.0f;

        p.cutoff = 8000.0f;
        filter.setParams (p);
        const double f8 = findCornerFrequency (filter, sr);

        p.cutoff = 12000.0f;
        filter.setParams (p);
        const double f12 = findCornerFrequency (filter, sr);

        p.cutoff = 16000.0f;
        filter.setParams (p);
        const double f16 = findCornerFrequency (filter, sr);

        p.cutoff = 19000.0f;
        filter.setParams (p);
        const double f19 = findCornerFrequency (filter, sr);

        std::printf ("    T1 %s: 8k=%.1f Hz, 12k=%.1f Hz, 16k=%.1f Hz, 19k=%.1f Hz\n",
                     getFilterTypeName (static_cast<int> (m)), f8, f12, f16, f19);

        // At 8 kHz result must not change and match nominal
        if (m == FilterType::MGLow6)
            CHECK (std::fabs (f8 - 8000.0) <= 5.0);

        // Monotonic progression above 8 kHz (before fix, 12k, 16k, 19k are identical because gSafe <= 0.95f clamps)
        CHECK (f12 > f8 + 500.0);
        CHECK (f16 > f12 + 500.0);
        CHECK (f19 > f16 + 500.0);

        // After fix, within one third of an octave of the set cutoff
        if (m == FilterType::MGLow6)
        {
            CHECK (std::fabs (f12 - 12000.0) / 12000.0 < 0.25);
            CHECK (std::fabs (f16 - 16000.0) / 16000.0 < 0.25);
            CHECK (std::fabs (f19 - 19000.0) / 19000.0 < 0.25);
        }
    }
}

// T2: Bit-for-bit regression test below old limit (100 Hz, 1 kHz, 5 kHz, 9 kHz)
TEST (serum_ladder_cutoff_t2_below_limit_unchanged)
{
    const double sr = 44100.0;
    std::vector<float> inputSaw (44100);
    float phase = 0.0f;
    const float phaseInc = 130.8128f / 44100.0f;
    for (size_t i = 0; i < 44100; ++i)
    {
        inputSaw[i] = 2.0f * phase - 1.0f;
        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }

    for (int idx = 0; idx < 84; ++idx)
    {
        const auto& exp = kT2Expected[idx];
        Filter filter;
        filter.prepare (sr);
        filter.setType (exp.model);
        FilterParams p;
        p.cutoff = exp.cutoff;
        p.resonance = exp.res;
        p.drive = 0.0f;
        p.var = 0.0f;
        p.blend = 0.0f;
        p.mix = 1.0f;
        filter.setParams (p);
        filter.reset();

        double sumAbs = 0.0;
        float last8[8] = { 0 };
        for (size_t i = 0; i < 44100; ++i)
        {
            const float y = filter.process (inputSaw[i]);
            sumAbs += std::fabs (static_cast<double> (y));
            if (i >= 44100 - 8)
                last8[i - (44100 - 8)] = y;
        }

        const double tol = exp.expectedSumAbs * 1.0e-6;
        CHECK (std::fabs (sumAbs - exp.expectedSumAbs) <= tol);
        for (int s = 0; s < 8; ++s)
        {
            const double sTol = std::max (1.0e-5, std::fabs (static_cast<double> (exp.expectedLast8[s])) * 1.0e-6);
            CHECK (std::fabs (last8[s] - exp.expectedLast8[s]) <= sTol);
        }
    }
}

// T3: Stability where filter now opens (cutoff 12, 16, 19, 20 kHz, sr*0.49; max res, max drive)
TEST (serum_ladder_cutoff_t3_stability)
{
    const double sr = 44100.0;
    const FilterType models[] = {
        FilterType::MGLow6, FilterType::MGLow12, FilterType::MGLow18, FilterType::MGLow24,
        FilterType::MGLadder, FilterType::EMSLadder, FilterType::MGDirty
    };
    const float cutoffs[] = { 12000.0f, 16000.0f, 19000.0f, 20000.0f, static_cast<float> (sr * 0.49) };

    for (FilterType m : models)
    {
        for (float fc : cutoffs)
        {
            Filter filter;
            filter.prepare (sr);
            filter.setType (static_cast<int> (m));
            FilterParams p;
            p.cutoff = fc;
            p.resonance = 1.0f; // max resonance
            p.drive = 1.0f;     // max drive
            p.var = 0.5f;
            p.blend = 0.0f;
            p.mix = 1.0f;
            filter.setParams (p);
            filter.reset();

            SimpleRng rng;
            const int nNoise = static_cast<int> (sr * 2.0);
            const int nSilence = static_cast<int> (sr * 2.0);

            float peak = 0.0f;
            bool finite = true;

            // 2s noise
            for (int i = 0; i < nNoise; ++i)
            {
                const float in = rng.nextFloat (-1.0f, 1.0f);
                const float y = filter.process (in);
                if (!std::isfinite (y))
                    finite = false;
                peak = std::max (peak, std::fabs (y));
            }

            // 2s silence
            float tailPeak = 0.0f;
            for (int i = 0; i < nSilence; ++i)
            {
                const float y = filter.process (0.0f);
                if (!std::isfinite (y))
                    finite = false;
                peak = std::max (peak, std::fabs (y));
                if (i >= nSilence - 1000)
                    tailPeak = std::max (tailPeak, std::fabs (y));
            }

            CHECK (finite);
            CHECK (peak < 8.0f);
            if (m == FilterType::MGDirty)
            {
                // Bounded self-oscillation due to PAIN offset
                CHECK (tailPeak < 8.0f);
            }
            else
            {
                // Clean decay below 1e-3
                CHECK (tailPeak < 1.0e-3f);
            }
        }
    }
}

// T4: Fast Cutoff Modulation
TEST (serum_ladder_cutoff_t4_modulation)
{
    const double sr = 44100.0;
    const FilterType models[] = {
        FilterType::MGLow6, FilterType::MGLow12, FilterType::MGLow18, FilterType::MGLow24,
        FilterType::MGLadder, FilterType::EMSLadder, FilterType::MGDirty
    };

    for (FilterType m : models)
    {
        Filter filter;
        filter.prepare (sr);
        filter.setType (static_cast<int> (m));
        FilterParams p;
        p.cutoff = 200.0f;
        p.resonance = 1.0f; // max resonance
        p.drive = 0.5f;
        p.var = 0.5f;
        p.blend = 0.0f;
        p.mix = 1.0f;
        filter.setParams (p);
        filter.reset();

        SimpleRng rng;
        const int nTotal = static_cast<int> (sr * 1.0);
        const float sweepPeriodSec = 0.050f;
        const int sweepSamples = static_cast<int> (sr * sweepPeriodSec);
        const float maxCutoff = 20000.0f;
        const float minCutoff = 200.0f;

        float peak = 0.0f;
        bool finite = true;

        for (int i = 0; i < nTotal; ++i)
        {
            const int posInPeriod = i % sweepSamples;
            const float halfPeriod = static_cast<float> (sweepSamples) * 0.5f;
            float norm = 0.0f;
            if (static_cast<float> (posInPeriod) < halfPeriod)
                norm = static_cast<float> (posInPeriod) / halfPeriod;
            else
                norm = 1.0f - (static_cast<float> (posInPeriod) - halfPeriod) / halfPeriod;

            p.cutoff = minCutoff + norm * (maxCutoff - minCutoff);
            filter.setParams (p);

            const float in = rng.nextFloat (-1.0f, 1.0f);
            const float y = filter.process (in);
            if (!std::isfinite (y))
                finite = false;
            peak = std::max (peak, std::fabs (y));
        }

        CHECK (finite);
        CHECK (peak < 8.0f);
    }
}

// T5: Scaling with Sample Rate (96 kHz)
TEST (serum_ladder_cutoff_t5_96khz)
{
    const double sr = 96000.0;
    Filter filter;
    filter.prepare (sr);
    filter.setType (static_cast<int> (FilterType::MGLow6));
    FilterParams p;
    p.resonance = 0.0f;
    p.drive = 0.0f;
    p.var = 0.0f;
    p.blend = 0.0f;
    p.mix = 1.0f;

    // Corner frequencies at 96 kHz: before fix, clamped to atan(0.95)/pi*96000 = 23216.6 Hz
    p.cutoff = 16000.0f;
    filter.setParams (p);
    const double f16 = findCornerFrequency (filter, sr);

    p.cutoff = 24000.0f;
    filter.setParams (p);
    const double f24 = findCornerFrequency (filter, sr);

    p.cutoff = 36000.0f;
    filter.setParams (p);
    const double f36 = findCornerFrequency (filter, sr);

    p.cutoff = 42000.0f;
    filter.setParams (p);
    const double f42 = findCornerFrequency (filter, sr);

    std::printf ("    T5 96kHz MGLow6: 16k=%.1f Hz, 24k=%.1f Hz, 36k=%.1f Hz, 42k=%.1f Hz\n",
                 f16, f24, f36, f42);

    CHECK (std::fabs (f16 - 16000.0) <= 10.0);
    // At 96 kHz, before fix f36 and f42 are clamped to 23216.6 Hz!
    CHECK (f36 > f24 + 1000.0);
    CHECK (f42 > f36 + 1000.0);
    CHECK (std::fabs (f36 - 36000.0) / 36000.0 < 0.25);
    CHECK (std::fabs (f42 - 42000.0) / 42000.0 < 0.25);

    // Stability at 96 kHz
    p.cutoff = 42000.0f;
    p.resonance = 1.0f;
    p.drive = 1.0f;
    filter.setParams (p);
    filter.reset();
    SimpleRng rng;
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < 96000; ++i)
    {
        const float in = rng.nextFloat (-1.0f, 1.0f);
        const float y = filter.process (in);
        if (!std::isfinite (y)) finite = false;
        peak = std::max (peak, std::fabs (y));
    }
    CHECK (finite);
    CHECK (peak < 8.0f);
}

// =============================================================================
// Diode ladder probe (docs/plans/diode-ladder.md, task Z1): records what the
// serum DiodeLadder (AcidLadder) does BEFORE its change. Prints only; no
// numeric assertion, so it passes on both sides of the change.
// =============================================================================

namespace
{

/** Impulse response of 4096 samples (1e-3 impulse, divided by 1e-3), as findCornerFrequency measures it. */
static std::vector<float> diodeProbeImpulse (Filter& filter)
{
    filter.reset();
    constexpr int kIrLen = 4096;
    std::vector<float> ir (kIrLen, 0.0f);
    for (int i = 0; i < kIrLen; ++i)
        ir[i] = filter.process (i == 0 ? 1.0e-3f : 0.0f) / 1.0e-3f;
    return ir;
}

} // namespace

TEST (serum_diode_probe_before_change)
{
    const FilterType type = FilterType::AcidLadder;

    // (a) -3 dB corner at resonance 0.
    struct CornerRow
    {
        double sr;
        float fc;
    };
    const CornerRow cornerRows[] = {
        { 44100.0, 1000.0f }, { 44100.0, 4000.0f }, { 44100.0, 8000.0f }, { 44100.0, 10000.0f },
        { 44100.0, 12000.0f }, { 44100.0, 16000.0f }, { 44100.0, 19000.0f }, { 44100.0, 21000.0f },
        { 96000.0, 16000.0f }, { 96000.0, 24000.0f }, { 96000.0, 36000.0f }, { 96000.0, 42000.0f },
    };
    for (const auto& row : cornerRows)
    {
        Filter filter;
        filter.prepare (row.sr);
        filter.setType (static_cast<int> (type));
        FilterParams p;
        p.cutoff = row.fc;
        p.resonance = 0.0f;
        p.drive = 0.0f;
        p.var = 0.0f;
        p.blend = 0.0f;
        p.mix = 1.0f;
        filter.setParams (p);
        const double f3db = findCornerFrequency (filter, row.sr);
        std::printf ("DIODE-CORNER sr=%.0f fc=%.0f f3db=%.1f\n", row.sr, static_cast<double> (row.fc), f3db);
    }

    // (b) peak of the magnitude response, 50 Hz to 0.45 sr in 5 Hz steps, dB re the DC sum.
    {
        const double sr = 44100.0;
        const float peakCutoffs[] = { 1000.0f, 2000.0f, 5000.0f, 10000.0f, 15000.0f };
        const float peakRes[] = { 0.5f, 0.8f, 1.0f };
        for (float fc : peakCutoffs)
        {
            for (float r : peakRes)
            {
                Filter filter;
                filter.prepare (sr);
                filter.setType (static_cast<int> (type));
                FilterParams p;
                p.cutoff = fc;
                p.resonance = r;
                p.drive = 0.0f;
                p.var = 0.0f;
                p.blend = 0.0f;
                p.mix = 1.0f;
                filter.setParams (p);
                const std::vector<float> ir = diodeProbeImpulse (filter);
                const int n = static_cast<int> (ir.size());

                double dcGain = 0.0;
                for (int i = 0; i < n; ++i)
                    dcGain += ir[i];
                const double dcAbs = std::max (std::fabs (dcGain), 1.0e-30);

                double bestMag = 0.0, bestHz = 0.0;
                for (double hz = 50.0; hz <= 0.45 * sr; hz += 5.0)
                {
                    double re = 0.0, im = 0.0;
                    const double omega = 2.0 * 3.14159265358979323846 * hz / sr;
                    for (int i = 0; i < n; ++i)
                    {
                        re += ir[i] * std::cos (omega * i);
                        im -= ir[i] * std::sin (omega * i);
                    }
                    const double mag = std::sqrt (re * re + im * im);
                    if (mag > bestMag)
                    {
                        bestMag = mag;
                        bestHz = hz;
                    }
                }
                const double peakDb = 20.0 * std::log10 (std::max (bestMag, 1.0e-30) / dcAbs);
                std::printf ("DIODE-PEAK sr=44100 fc=%.0f res=%.1f peakHz=%.1f peakDb=%.3f\n",
                             static_cast<double> (fc), static_cast<double> (r), bestHz, peakDb);
            }
        }
    }

    // (c) checksums of 1 s of the 130.8128 Hz saw of T2.
    {
        const double sr = 44100.0;
        std::vector<float> inputSaw (44100);
        float phase = 0.0f;
        const float phaseInc = 130.8128f / 44100.0f;
        for (size_t i = 0; i < 44100; ++i)
        {
            inputSaw[i] = 2.0f * phase - 1.0f;
            phase += phaseInc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }

        const float sumCutoffs[] = { 500.0f, 2000.0f, 5000.0f, 8000.0f, 10000.0f };
        const float sumRes[] = { 0.0f, 0.5f, 1.0f };
        const float sumDrive[] = { 0.0f, 1.0f };
        for (float fc : sumCutoffs)
        {
            for (float r : sumRes)
            {
                for (float d : sumDrive)
                {
                    Filter filter;
                    filter.prepare (sr);
                    filter.setType (static_cast<int> (type));
                    FilterParams p;
                    p.cutoff = fc;
                    p.resonance = r;
                    p.drive = d;
                    p.var = 0.0f;
                    p.blend = 0.0f;
                    p.mix = 1.0f;
                    filter.setParams (p);
                    filter.reset();

                    double sumAbs = 0.0;
                    float last8[8] = { 0 };
                    for (size_t i = 0; i < 44100; ++i)
                    {
                        const float y = filter.process (inputSaw[i]);
                        sumAbs += std::fabs (static_cast<double> (y));
                        if (i >= 44100 - 8)
                            last8[i - (44100 - 8)] = y;
                    }
                    std::printf ("DIODE-CHECKSUM fc=%.0f res=%.1f drive=%.0f sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                                 static_cast<double> (fc), static_cast<double> (r), static_cast<double> (d), sumAbs,
                                 static_cast<double> (last8[0]), static_cast<double> (last8[1]),
                                 static_cast<double> (last8[2]), static_cast<double> (last8[3]),
                                 static_cast<double> (last8[4]), static_cast<double> (last8[5]),
                                 static_cast<double> (last8[6]), static_cast<double> (last8[7]));
                }
            }
        }
    }

    CHECK (true);
}

// =============================================================================
// Diode ladder tests (docs/plans/diode-ladder.md, task A1): the zero-delay
// feedback, the lifted 0.95 clamp and the cached coefficients of DiodeLadder.
// =============================================================================

namespace
{

/** A verbatim copy of DiodeLadder as it stood before task A1 (unit delay in the feedback, g clamped at 0.95, tan () every sample). */
struct LegacyDiodeLadder
{
    float s[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float hpState = 0.0f;

    void reset() noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = 0.0f;
        hpState = 0.0f;
    }

    float process (float in, float g, float res, float drive, float sampleRate) noexcept
    {
        for (int i = 0; i < 4; ++i)
            s[i] = sanitize (s[i]);
        hpState = sanitize (hpState);

        const float gSafe = std::max (1e-5f, std::min (g, 0.95f));
        const float G = gSafe / (1.0f + gSafe);
        const float k = 3.82f * res;

        const float gHp = std::tan (kPi * 30.0f / static_cast<float> (sampleRate));
        const float gHpSafe = std::max (1e-5f, std::min (gHp, 0.5f));
        const float GHp = gHpSafe / (1.0f + gHpSafe);

        const float fbLP = GHp * (s[3] - hpState) + hpState;
        hpState = sanitize (GHp * (s[3] - hpState) + fbLP);
        const float fbHP = s[3] - fbLP;

        const float driveGain = 1.0f + 4.0f * drive;
        const float u = softClip (driveGain * in - k * softClip (fbHP));

        float y = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = G * (y - s[i]);
            y = v + s[i];
            s[i] = sanitize (v + y);
        }
        return y;
    }
};

double refSoftClip (double x)
{
    if (x <= -3.0)
        return -1.0;
    if (x >= 3.0)
        return 1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

/** The converged solution of the delay-free diode ladder in double precision (the root of u - sc (D - k sc (p + c u)) by bisection). */
struct RefDiode
{
    double s[4] = { 0.0, 0.0, 0.0, 0.0 };
    double h = 0.0;

    double step (double in, double g, double res, double drive, double sr)
    {
        const double pi = 3.14159265358979323846;
        const double gS = std::max (1.0e-5, std::min (g, 50.0));
        const double G = gS / (1.0 + gS);
        const double G0 = 1.0 / (1.0 + gS);
        const double gh = std::max (1.0e-5, std::min (std::tan (pi * 30.0 / sr), 0.5));
        const double a = 1.0 / (1.0 + gh);
        const double GH = gh / (1.0 + gh);
        const double k = 3.82 * res;
        const double D = (1.0 + 4.0 * drive) * in;
        const double S = G0 * (G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3]);
        const double p = a * (S - h);
        const double c = a * G * G * G * G;

        double lo = -1.0, hi = 1.0;
        for (int i = 0; i < 100; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            const double fm = mid - refSoftClip (D - k * refSoftClip (p + c * mid));
            if (fm > 0.0)
                hi = mid;
            else
                lo = mid;
        }
        const double u = 0.5 * (lo + hi);

        double y = u;
        for (int i = 0; i < 4; ++i)
        {
            const double v = G * (y - s[i]);
            y = v + s[i];
            s[i] = v + y;
        }
        h += 2.0 * GH * (y - h);
        return y;
    }
};

/** The -3 dB corner of four equal one-poles at the cutoff knob fc: fs / pi atan (0.43498 tan (pi fc / fs)). */
double diodeExpectedCorner (double sr, double fc)
{
    const double pi = 3.14159265358979323846;
    return sr / pi * std::atan (0.43498 * std::tan (pi * fc / sr));
}

void diodeSetup (Filter& filter, double sr, float fc, float res, float drive, float var = 0.0f)
{
    filter.prepare (sr);
    filter.setType (static_cast<int> (FilterType::AcidLadder));
    FilterParams p;
    p.cutoff = fc;
    p.resonance = res;
    p.drive = drive;
    p.var = var;
    p.blend = 0.0f;
    p.mix = 1.0f;
    filter.setParams (p);
    filter.reset();
}

std::vector<float> diodeSaw (size_t n, double hz, double sr, float amplitude)
{
    std::vector<float> x (n);
    double phase = 0.0;
    for (auto& v : x)
    {
        v = amplitude * static_cast<float> (2.0 * phase - 1.0);
        phase += hz / sr;
        if (phase >= 1.0)
            phase -= 1.0;
    }
    return x;
}

double diodeRelRms (const std::vector<double>& ref, const std::vector<double>& got, size_t from)
{
    double e = 0.0, r = 0.0;
    for (size_t i = from; i < ref.size(); ++i)
    {
        const double d = got[i] - ref[i];
        e += d * d;
        r += ref[i] * ref[i];
    }
    return std::sqrt (e / std::max (r, 1.0e-30));
}

} // namespace

// D1: the cutoff knob keeps opening above 10.67 kHz.
TEST (serum_diode_d1_cutoff_reach)
{
    const double sr = 44100.0;
    const float cutoffs[] = { 8000.0f, 12000.0f, 16000.0f, 19000.0f };
    for (float fc : cutoffs)
    {
        Filter filter;
        diodeSetup (filter, sr, fc, 0.0f, 0.0f);
        const double f3db = findCornerFrequency (filter, sr);
        const double expected = diodeExpectedCorner (sr, static_cast<double> (fc));
        CHECK (std::fabs (f3db - expected) / expected < 0.03);
    }
}

// D2: no self-oscillation, bounded, at every cutoff up to 0.49 sr.
TEST (serum_diode_d2_stability)
{
    const double sr = 44100.0;
    const float cutoffs[] = { 2000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f, 19000.0f, 20000.0f, static_cast<float> (sr * 0.49) };
    const float resonances[] = { 1.0f, 0.5f };
    for (float res : resonances)
    {
        const float drive = res == 1.0f ? 1.0f : 0.5f;
        for (float fc : cutoffs)
        {
            Filter filter;
            diodeSetup (filter, sr, fc, res, drive, 0.5f);

            SimpleRng rng;
            const int nNoise = static_cast<int> (sr * 2.0);
            const int nSilence = static_cast<int> (sr * 2.0);
            float peak = 0.0f;
            bool finite = true;
            for (int i = 0; i < nNoise; ++i)
            {
                const float y = filter.process (rng.nextFloat (-1.0f, 1.0f));
                if (!std::isfinite (y))
                    finite = false;
                peak = std::max (peak, std::fabs (y));
            }
            float tailPeak = 0.0f;
            for (int i = 0; i < nSilence; ++i)
            {
                const float y = filter.process (0.0f);
                if (!std::isfinite (y))
                    finite = false;
                peak = std::max (peak, std::fabs (y));
                if (i >= nSilence - 1000)
                    tailPeak = std::max (tailPeak, std::fabs (y));
            }
            CHECK (finite);
            CHECK (peak < 8.0f);
            CHECK (tailPeak < 1.0e-3f);
        }
    }
}

// D3: the resonance peak sits at the knob frequency (calc: 4954 Hz at 5 kHz, 9924 Hz at 10 kHz, res 100 %).
TEST (serum_diode_d3_tuning)
{
    const double sr = 44100.0;
    struct Row
    {
        float fc;
        double expectedHz;
    };
    const Row rows[] = { { 5000.0f, 4954.0 }, { 10000.0f, 9924.0 } };
    for (const auto& row : rows)
    {
        Filter filter;
        diodeSetup (filter, sr, row.fc, 1.0f, 0.0f);
        const std::vector<float> ir = diodeProbeImpulse (filter);
        const int n = static_cast<int> (ir.size());
        double bestMag = 0.0, bestHz = 0.0;
        for (double hz = 200.0; hz <= 0.45 * sr; hz += 5.0)
        {
            double re = 0.0, im = 0.0;
            const double omega = 2.0 * 3.14159265358979323846 * hz / sr;
            for (int i = 0; i < n; ++i)
            {
                re += ir[i] * std::cos (omega * i);
                im -= ir[i] * std::sin (omega * i);
            }
            const double mag = std::sqrt (re * re + im * im);
            if (mag > bestMag)
            {
                bestMag = mag;
                bestHz = hz;
            }
        }
        CHECK (std::fabs (bestHz - row.expectedHz) / row.expectedHz < 0.03);
    }
}

// D4: a fast cutoff sweep at full resonance stays finite and bounded (guard).
TEST (serum_diode_d4_modulation)
{
    const double sr = 44100.0;
    Filter filter;
    diodeSetup (filter, sr, 200.0f, 1.0f, 0.5f, 0.5f);
    FilterParams p;
    p.cutoff = 200.0f;
    p.resonance = 1.0f;
    p.drive = 0.5f;
    p.var = 0.5f;
    p.blend = 0.0f;
    p.mix = 1.0f;

    SimpleRng rng;
    const int nTotal = static_cast<int> (sr * 1.0);
    const int sweepSamples = static_cast<int> (sr * 0.050f);
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < nTotal; ++i)
    {
        const int posInPeriod = i % sweepSamples;
        const float halfPeriod = static_cast<float> (sweepSamples) * 0.5f;
        float norm = 0.0f;
        if (static_cast<float> (posInPeriod) < halfPeriod)
            norm = static_cast<float> (posInPeriod) / halfPeriod;
        else
            norm = 1.0f - (static_cast<float> (posInPeriod) - halfPeriod) / halfPeriod;
        p.cutoff = 200.0f + norm * (20000.0f - 200.0f);
        filter.setParams (p);
        const float y = filter.process (rng.nextFloat (-1.0f, 1.0f));
        if (!std::isfinite (y))
            finite = false;
        peak = std::max (peak, std::fabs (y));
    }
    CHECK (finite);
    CHECK (peak < 8.0f);
}

// D5: 96 kHz: the corners follow the knob and the filter stays bounded at 42 kHz.
TEST (serum_diode_d5_96khz)
{
    const double sr = 96000.0;
    const float cutoffs[] = { 16000.0f, 24000.0f, 36000.0f, 42000.0f };
    for (float fc : cutoffs)
    {
        Filter filter;
        diodeSetup (filter, sr, fc, 0.0f, 0.0f);
        const double f3db = findCornerFrequency (filter, sr);
        const double expected = diodeExpectedCorner (sr, static_cast<double> (fc));
        CHECK (std::fabs (f3db - expected) / expected < 0.03);
    }

    Filter filter;
    diodeSetup (filter, sr, 42000.0f, 1.0f, 1.0f);
    SimpleRng rng;
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < 96000; ++i)
    {
        const float y = filter.process (rng.nextFloat (-1.0f, 1.0f));
        if (!std::isfinite (y))
            finite = false;
        peak = std::max (peak, std::fabs (y));
    }
    CHECK (finite);
    CHECK (peak < 8.0f);
}

// D6: at resonance 0 below the old limit (g <= 0.95) the new ladder is the old one, bit for bit.
TEST (serum_diode_d6_res0_bit_identical_below_the_old_limit)
{
    const std::vector<float> saw = diodeSaw (4410, 110.0, 44100.0, 0.8f);
    const float drives[] = { 0.0f, 0.5f, 1.0f };
    const float cutoffs[] = { 100.0f, 1000.0f, 5000.0f, 10000.0f };
    for (float drive : drives)
    {
        for (float fc : cutoffs)
        {
            const float g = std::tan (kPi * fc / 44100.0f);
            LegacyDiodeLadder legacy;
            DiodeLadder now;
            int mismatches = 0;
            for (float x : saw)
            {
                const float a = legacy.process (x, g, 0.0f, drive, 44100.0f);
                const float b = now.process (x, g, 0.0f, drive, 44100.0f);
                if (!(a == b))
                    ++mismatches;
            }
            CHECK_EQ (mismatches, 0);
        }
    }
}

// D7: the float ladder matches the converged double-precision solution (and follows a sample rate change).
TEST (serum_diode_d7_matches_the_converged_solution)
{
    struct Setting
    {
        float res, drive;
    };
    const Setting settings[] = { { 0.5f, 0.5f }, { 1.0f, 0.5f }, { 1.0f, 1.0f } };
    const float cutoffs[] = { 500.0f, 5000.0f, 15000.0f, 21609.0f };
    const std::vector<float> saw = diodeSaw (22050, 110.0, 44100.0, 0.8f);

    for (float fc : cutoffs)
    {
        for (const auto& st : settings)
        {
            const float g = std::tan (kPi * fc / 44100.0f);
            DiodeLadder dl;
            RefDiode ref;
            std::vector<double> r (saw.size()), y (saw.size());
            for (size_t i = 0; i < saw.size(); ++i)
            {
                y[i] = static_cast<double> (dl.process (saw[i], g, st.res, st.drive, 44100.0f));
                r[i] = ref.step (static_cast<double> (saw[i]), static_cast<double> (g), static_cast<double> (st.res), static_cast<double> (st.drive), 44100.0);
            }
            CHECK (diodeRelRms (r, y, 11025) < 1.0e-3);
        }
    }

    // second leg: the sample rate changes mid-stream (both the cutoff and the 30 Hz high-pass coefficient follow it)
    {
        const std::vector<float> saw2 = diodeSaw (2000, 110.0, 44100.0, 0.8f);
        const float g44 = std::tan (kPi * 5000.0f / 44100.0f);
        const float g96 = std::tan (kPi * 5000.0f / 96000.0f);
        DiodeLadder dl;
        RefDiode ref;
        std::vector<double> r (saw2.size()), y (saw2.size());
        for (size_t i = 0; i < saw2.size(); ++i)
        {
            const bool second = i >= 1000;
            const float g = second ? g96 : g44;
            const float sr = second ? 96000.0f : 44100.0f;
            y[i] = static_cast<double> (dl.process (saw2[i], g, 1.0f, 0.5f, sr));
            r[i] = ref.step (static_cast<double> (saw2[i]), static_cast<double> (g), 1.0, 0.5, static_cast<double> (sr));
        }
        CHECK (diodeRelRms (r, y, 1000) < 1.0e-3);
    }
}
