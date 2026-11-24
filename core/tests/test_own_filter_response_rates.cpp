// SynthyBoy - tests of FilterResponse at 44.1 kHz and 96 kHz (core/src/filter_response.cpp).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "synthyboy/filter_response.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"
#include "synthyboy/wavetable_edit.h"
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

bool inRange (const std::vector<float>& c)
{
    return std::all_of (c.begin(), c.end(), [] (float x) { return std::isfinite (x) && x >= -120.0f && x <= 60.0f; });
}

/** In range AND carrying a signal (compute () clamps to the range by construction: the range alone proves nothing; a filter that ran away shows -120 dB flat; a flat 0 dB is legal: a style a model does not have passes the signal). */
bool hasSignal (const std::vector<float>& c)
{
    float hi = c[0];
    for (float x : c)
    {
        if (!std::isfinite (x) || x < -120.0f || x > 60.0f)
            return false;
        hi = std::max (hi, x);
    }
    return hi > -100.0f;
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
    CHECK (hasSignal (c44));
    CHECK (hasSignal (c48));
    CHECK (hasSignal (c96));

    // The filters are digital (a bilinear transform at the native rate, filters/classic_filters.h svfSetup: g = tan (pi fc / sr)); the same filter therefore has a slightly
    // different response at different rates: a frequency f at rate sr acts like the warped frequency tan (pi f / sr) / tan (pi fc / sr). That is what the engine does and
    // what the display must show. Measured: 1.13 dB at 4.97 kHz between 44.1 and 96 kHz (24 dB, cutoff 1 kHz). So:
    //  1. the raw spread is bounded by what the warp predicts (1.3 dB, with margin) and is NOT zero (a zero would mean the display ignores the rate);
    //  2. after undoing the warp analytically the curves agree to 0.05 dB (this is the check that the measurement method is sound).
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
    CHECK (maxDiff <= 1.3f);
    CHECK (maxDiff >= 0.8f);

    const double fc = 1000.0;
    const double t44 = std::tan (3.14159265358979323846 * fc / kSr44), t96 = std::tan (3.14159265358979323846 * fc / kSr96);
    auto at96 = [&] (double hz) // c96 at a frequency, linear interpolation of dB on the log axis
    {
        const double pos = std::log (hz / kMin) / std::log (kMax / kMin) * (kPoints - 1);
        const int k = std::clamp (static_cast<int> (pos), 0, kPoints - 2);
        const double fr = pos - k;
        return (1.0 - fr) * static_cast<double> (c96[static_cast<size_t> (k)]) + fr * static_cast<double> (c96[static_cast<size_t> (k + 1)]);
    };
    double worstWarped = 0.0;
    for (int i = 0; i < kPoints; ++i)
    {
        const double hz = static_cast<double> (hzAt (i));
        if (hz > 5000.0)
            break;
        const double mapped = kSr96 / 3.14159265358979323846 * std::atan (std::tan (3.14159265358979323846 * hz / kSr44) / t44 * t96);
        worstWarped = std::max (worstWarped, std::fabs (static_cast<double> (c44[static_cast<size_t> (i)]) - at96 (mapped)));
    }
    std::printf ("    after the bilinear frequency warp of the 44.1 kHz curve onto 96 kHz: worst difference %.4f dB\n", worstWarped);
    CHECK (worstWarped < 0.05);
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
        CHECK (hasSignal (c44));
        CHECK (hasSignal (c48));
        CHECK (hasSignal (c96));

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
                CHECK (hasSignal (db));

                // A request whose entire range sits at or above Nyquist clamps to the Nyquist bin.
                FilterResponse::Request atNyq;
                atNyq.fMin = nyq;
                atNyq.fMax = fMax;
                std::vector<float> tail (32, -999.0f);
                CHECK (fr44.compute (v, atNyq, tail.data(), 32));
                CHECK (inRange (tail)); // the Nyquist end of a low pass is legitimately quiet
                for (float x : tail)
                    CHECK (std::fabs (x - db.back()) < 1.0e-5f);
            }
}

TEST (own_filter_response_every_model_at_96000_is_finite_and_fast)
{
    FilterResponse fr (kSr96);
    const int models = static_cast<int> (findParamByKey ("filter1.model")->max) + 1;
    CHECK (models == 27);
#ifdef NDEBUG
    const bool measureSpeed = !sbtest::instrumented();
#else
    constexpr bool measureSpeed = false;
#endif
    // Recalibrate against a same-size FFT on the current thread for each model.
    // This guards against extra work in the response path and individual models.
    // The absolute timing, including the cost of the FFT itself, is diagnostic.
    constexpr int kTimingRuns = 5;
    constexpr int kReferenceFfts = 32; // average a batch for coarser CPU clocks
    constexpr double kMaxFftRatio = 12.0; // measured below 10x on the Mac with four concurrent Release runs
    const size_t n = static_cast<size_t> (fr.impulseLength());
    std::vector<float> re (measureSpeed ? n : 0), im (re.size());
    double worstMs = 0.0, sumMs = 0.0;
    double worstRatio = 0.0;
    int worstModel = -1, worstStyle = -1, runs = 0;
    float worstRes = 0.0f;
    for (int m = 0; m < models; ++m)
    {
        double fftMs = 0.0;
        if (measureSpeed)
        {
            fftMs = sbtest::bestCpuMs (3, [&] {
                for (int i = 0; i < kReferenceFfts; ++i)
                {
                    std::fill (re.begin(), re.end(), 0.0f);
                    std::fill (im.begin(), im.end(), 0.0f);
                    re[0] = 1.0f;
                    wtedit::Radix2Fft::forward (re.data(), im.data(), n);
                }
            }) / kReferenceFfts;
            CHECK (fftMs > 0.0);
        }
        for (int st = 0; st < 12; ++st)
            for (float res : { 20.0f, 100.0f })
            {
                const ParamValues v = patch (m, st, 1500.0f, res);
                std::vector<float> db (256);
                bool ok = false;
                const auto compute = [&] { ok = fr.compute (v, {}, db.data(), 256); };
                double ms = 0.0;
                if (measureSpeed)
                    ms = sbtest::bestCpuMs (kTimingRuns, compute);
                else
                    compute();
                CHECK (ok);
                if (!CHECK (hasSignal (db)))
                    std::printf ("    96 kHz model %d style %d res %.0f: not finite / out of range\n", m, st, res);
                if (fftMs <= 0.0)
                    continue;

                const double ratio = ms / fftMs;
                sumMs += ms;
                ++runs;
                worstMs = std::max (worstMs, ms);
                if (ratio > worstRatio)
                {
                    worstRatio = ratio;
                    worstModel = m;
                    worstStyle = st;
                    worstRes = res;
                }
            }
    }
    if (runs > 0)
    {
        std::printf ("    96 kHz %d computes (%d samples, %.2f Hz per bin): mean %.3f ms, worst %.3f ms thread CPU, best of %d\n",
                     runs, fr.impulseLength(), fr.binWidthHz(), sumMs / runs, worstMs, kTimingRuns);
        std::printf ("    worst ratio %.2fx FFT CPU time (model %d style %d res %.0f), limit %.2fx\n",
                     worstRatio, worstModel, worstStyle, worstRes, kMaxFftRatio);
        CHECK (worstRatio < kMaxFftRatio);
    }
}
