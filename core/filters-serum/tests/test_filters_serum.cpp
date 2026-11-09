// SynthyBoy - core/filters-serum: Comprehensive filter test suite.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/filters/filter.h"
#include "synthyboy/filters/filter_types.h"
#include "test_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace synthyboy::filters;

namespace
{

#ifndef M_PI
constexpr float kPi = 3.14159265358979323846f;
#else
constexpr float kPi = static_cast<float> (M_PI);
#endif

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
    // the names of the new models' styles
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (22, 4))) == "High 12");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (23, 3))) == "BPN");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (24, 8))) == "Phs HL");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (25, 11))) == "Dist.Comb 2 BP");
    CHECK (std::string (getFilterTypeName (serumFilterTypeOf (26, 5))) == "Exp BPF");
}
