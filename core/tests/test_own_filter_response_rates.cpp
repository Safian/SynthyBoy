// SynthyBoy - tests of FilterResponse at 44.1 kHz and 96 kHz (core/src/filter_response.cpp).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr44 = 44100.0;
constexpr double kSr48 = 48000.0;
constexpr double kSr96 = 96000.0;
constexpr int kPoints = 512;
constexpr float kMin = 20.0f, kMax = 20000.0f;

void setv (ParamValues& v, const char* key, float x)
{
    const ParamInfo* p = findParamByKey (key);
    if (!CHECK (p != nullptr))
        return;
    v.plain[findParamIndex (p->id)] = constrainValue (*p, x);
}

ParamValues patch (int model, int style, float cutoff, float resonance = 0.0f)
{
    ParamValues v = ParamValues::defaults();
    setv (v, "filter1.on", 1);
    setv (v, "filter1.model", static_cast<float> (model));
    setv (v, "filter1.style", static_cast<float> (style));
    setv (v, "filter1.cutoff", cutoff);
    setv (v, "filter1.resonance", resonance);
    return v;
}

std::vector<float> curve (FilterResponse& fr, const ParamValues& v, FilterResponse::Request req = {})
{
    std::vector<float> db (kPoints);
    CHECK (fr.compute (v, req, db.data(), kPoints));
    return db;
}

float hzAt (int i) { return kMin * std::pow (kMax / kMin, static_cast<float> (i) / static_cast<float> (kPoints - 1)); }
int indexOf (float hz) { return static_cast<int> (std::lround (std::log (hz / kMin) / std::log (kMax / kMin) * (kPoints - 1))); }
float at (const std::vector<float>& c, float hz) { return c[static_cast<size_t> (indexOf (hz))]; }

bool finiteAndSane (const std::vector<float>& c)
{
    for (float x : c)
        if (!std::isfinite (x) || x < -120.0f || x > 60.0f)
            return false;
    return true;
}

constexpr int kAnalog = 0, kNormal = 22, kComb = 6;
} // namespace

TEST (own_filter_response_analog_24_db_curves_agree_across_sample_rates)
{
    FilterResponse fr44 (kSr44);
    FilterResponse fr48 (kSr48);
    FilterResponse fr96 (kSr96);
    CHECK (fr44.sampleRate() == kSr44);
    CHECK (fr48.sampleRate() == kSr48);
    CHECK (fr96.sampleRate() == kSr96);
    CHECK (fr44.impulseLength() == 16384);
    CHECK (fr48.impulseLength() == 16384);
    CHECK (fr96.impulseLength() == 32768);

    const ParamValues v = patch (kAnalog, 1, 1000.0f, 0.0f);
    const auto c44 = curve (fr44, v);
    const auto c48 = curve (fr48, v);
    const auto c96 = curve (fr96, v);
    CHECK (finiteAndSane (c44));
    CHECK (finiteAndSane (c48));
    CHECK (finiteAndSane (c96));

    float maxDiff = 0.0f, worstHz = 0.0f;
    for (int i = 0; i < kPoints; ++i)
    {
        const float hz = hzAt (i);
        if (hz > 5000.0f)
            break;
        const float a = c44[static_cast<size_t> (i)];
        const float b = c48[static_cast<size_t> (i)];
        const float c = c96[static_cast<size_t> (i)];
        const float d = std::max ({ std::fabs (a - b), std::fabs (b - c), std::fabs (a - c) });
        if (d > maxDiff)
            maxDiff = d, worstHz = hz;
    }
    std::printf ("    analog 24 dB 1 kHz (20 Hz .. 5 kHz): worst spread across 44.1 / 48 / 96 kHz is %.3f dB at %.1f Hz\n", maxDiff, worstHz);
    CHECK (maxDiff <= 1.0f);
}

TEST (own_filter_response_high_resonance_peak_agrees_across_sample_rates)
{
    FilterResponse fr44 (kSr44);
    FilterResponse fr48 (kSr48);
    FilterResponse fr96 (kSr96);

    for (int style : { 0, 1 })
    {
        const ParamValues v = patch (kAnalog, style, 1000.0f, 90.0f);
        const auto c44 = curve (fr44, v);
        const auto c48 = curve (fr48, v);
        const auto c96 = curve (fr96, v);
        CHECK (finiteAndSane (c44));
        CHECK (finiteAndSane (c48));
        CHECK (finiteAndSane (c96));

        auto findPeak = [] (const std::vector<float>& c, float& peakDb, float& peakHz)
        {
            peakDb = -200.0f;
            peakHz = 0.0f;
            for (int i = 0; i < kPoints; ++i)
                if (c[static_cast<size_t> (i)] > peakDb)
                    peakDb = c[static_cast<size_t> (i)], peakHz = hzAt (i);
        };

        float peak44 = 0.0f, hz44 = 0.0f;
        float peak48 = 0.0f, hz48 = 0.0f;
        float peak96 = 0.0f, hz96 = 0.0f;
        findPeak (c44, peak44, hz44);
        findPeak (c48, peak48, hz48);
        findPeak (c96, peak96, hz96);

        CHECK (peak44 >= at (c44, 100.0f) + 6.0f);
        CHECK (peak48 >= at (c48, 100.0f) + 6.0f);
        CHECK (peak96 >= at (c96, 100.0f) + 6.0f);
        CHECK (hz44 > 500.0f && hz44 < 2000.0f);
        CHECK (hz48 > 500.0f && hz48 < 2000.0f);
        CHECK (hz96 > 500.0f && hz96 < 2000.0f);

        const double oct44_48 = std::fabs (std::log2 (static_cast<double> (hz44) / static_cast<double> (hz48)));
        const double oct96_48 = std::fabs (std::log2 (static_cast<double> (hz96) / static_cast<double> (hz48)));
        const double oct44_96 = std::fabs (std::log2 (static_cast<double> (hz44) / static_cast<double> (hz96)));
        const double maxOct = std::max ({ oct44_48, oct96_48, oct44_96 });
        std::printf ("    analog style %d res 90 %%: peak %.1f Hz (44.1k), %.1f Hz (48k), %.1f Hz (96k), max spread %.4f oct\n", style, hz44, hz48, hz96, maxOct);
        CHECK (maxOct <= 1.0 / 20.0);
    }
}

TEST (own_filter_response_fmax_above_nyquist_at_44100_is_finite)
{
    FilterResponse fr44 (kSr44);
    const float nyq = static_cast<float> (0.5 * kSr44); // 22050 Hz

    for (int model : { kAnalog, kComb, kNormal })
        for (int style : { 0, 1 })
            for (float fMax : { 24000.0f, 30000.0f, 48000.0f })
            {
                const ParamValues v = patch (model, style, 2000.0f, 70.0f);
                FilterResponse::Request req;
                req.fMin = 20.0f;
                req.fMax = fMax;
                std::vector<float> db (kPoints, -999.0f);
                CHECK (fr44.compute (v, req, db.data(), kPoints));
                CHECK (finiteAndSane (db));

                // A request whose entire range sits at or above Nyquist clamps to the Nyquist bin.
                FilterResponse::Request atNyq;
                atNyq.fMin = nyq;
                atNyq.fMax = fMax;
                std::vector<float> tail (32, -999.0f);
                CHECK (fr44.compute (v, atNyq, tail.data(), 32));
                CHECK (finiteAndSane (tail));
                for (float x : tail)
                    CHECK (std::fabs (x - db.back()) < 1.0e-5f);
            }
}

TEST (own_filter_response_every_model_at_96000_is_finite_and_fast)
{
    FilterResponse fr (kSr96);
    const int models = static_cast<int> (findParamByKey ("filter1.model")->max) + 1;
    CHECK (models == 27);
    double worstMs = 0.0, sumMs = 0.0;
    int worstModel = -1, worstStyle = -1, runs = 0;
    for (int m = 0; m < models; ++m)
        for (int st = 0; st < 12; ++st)
            for (float res : { 20.0f, 100.0f })
            {
                ParamValues v = patch (m, st, 1500.0f, res);
                std::vector<float> db (256);
                const auto t0 = std::chrono::steady_clock::now();
                const bool ok = fr.compute (v, {}, db.data(), 256);
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                CHECK (ok);
                if (!CHECK (finiteAndSane (db)))
                    std::printf ("    96 kHz model %d style %d res %.0f: not finite / out of range\n", m, st, res);
                sumMs += ms;
                ++runs;
                if (ms > worstMs)
                    worstMs = ms, worstModel = m, worstStyle = st;
            }
    std::printf ("    96 kHz %d computes (%d samples, %.2f Hz per bin): mean %.3f ms, worst %.3f ms (model %d style %d)\n", runs, fr.impulseLength(), fr.binWidthHz(), sumMs / runs,
                 worstMs, worstModel, worstStyle);
    if (!sbtest::instrumented())
        CHECK (worstMs < 5.0);
}
