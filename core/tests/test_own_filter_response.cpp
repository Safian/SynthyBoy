// SynthyBoy - tests of FilterResponse (core/src/filter_response.cpp): the curve of the real filter code, the readout of the modulated cutoff.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/filter_response.h"
#include "synthyboy/modulation.h"
#include "synthyboy/readout.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;
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
    fr.compute (v, req, db.data(), kPoints);
    return db;
}

float hzAt (int i) { return kMin * std::pow (kMax / kMin, static_cast<float> (i) / static_cast<float> (kPoints - 1)); }
int indexOf (float hz) { return static_cast<int> (std::lround (std::log (hz / kMin) / std::log (kMax / kMin) * (kPoints - 1))); }
float at (const std::vector<float>& c, float hz) { return c[static_cast<size_t> (indexOf (hz))]; }

/** The frequency (Hz) where the curve first falls to -12 dB or lower, searched from the passband upwards (linear interpolation on the log axis). */
float crossing12 (const std::vector<float>& c)
{
    for (int i = indexOf (30.0f); i < kPoints; ++i)
        if (c[static_cast<size_t> (i)] <= -12.0f)
            return hzAt (i);
    return 0.0f;
}

bool finiteAndSane (const std::vector<float>& c)
{
    for (float x : c)
        if (!std::isfinite (x) || x < -120.0f || x > 60.0f)
            return false;
    return true;
}

constexpr int kAnalog = 0, kNormal = 22, kComb = 6;
} // namespace

TEST (own_filter_response_analog_12_and_24_db_low_pass)
{
    FilterResponse fr (kSr);
    const auto c12 = curve (fr, patch (kAnalog, 0, 1000.0f));
    const auto c24 = curve (fr, patch (kAnalog, 1, 1000.0f));
    CHECK (finiteAndSane (c12));
    CHECK (finiteAndSane (c24));
    std::printf ("    analog 12 dB: 100 Hz %.2f dB, 1 kHz %.2f dB, 8 kHz %.2f dB; 24 dB: 100 Hz %.2f, 1 kHz %.2f, 8 kHz %.2f\n", at (c12, 100.0f), at (c12, 1000.0f), at (c12, 8000.0f),
                at (c24, 100.0f), at (c24, 1000.0f), at (c24, 8000.0f));
    CHECK (std::fabs (at (c12, 100.0f)) < 1.5f);
    CHECK (std::fabs (at (c24, 100.0f)) < 1.5f);
    CHECK (at (c12, 8000.0f) < -15.0f);
    CHECK (at (c24, 8000.0f) < -30.0f);
    CHECK (at (c24, 8000.0f) < at (c12, 8000.0f) - 6.0f); // steeper
}

TEST (own_filter_response_resonance_makes_a_peak_near_the_cutoff)
{
    FilterResponse fr (kSr);
    const auto flat = curve (fr, patch (kAnalog, 0, 1000.0f, 0.0f));
    const auto res = curve (fr, patch (kAnalog, 0, 1000.0f, 90.0f));
    const float pass = at (res, 100.0f);
    float peak = -200.0f, peakHz = 0.0f;
    for (int i = 0; i < kPoints; ++i)
        if (res[static_cast<size_t> (i)] > peak)
            peak = res[static_cast<size_t> (i)], peakHz = hzAt (i);
    std::printf ("    resonance 90 %%: passband %.2f dB, peak %.2f dB at %.0f Hz (without resonance %.2f dB at 1 kHz)\n", pass, peak, peakHz, at (flat, 1000.0f));
    CHECK (peak >= pass + 6.0f);
    CHECK (peakHz > 500.0f && peakHz < 2000.0f);
    CHECK (finiteAndSane (res));
}

TEST (own_filter_response_cutoff_octave_translates_the_curve)
{
    FilterResponse fr (kSr);
    for (int model : { kAnalog, kNormal })
        for (int style : { 0, 1 })
        {
            const int st = model == kNormal ? 2 + style : style; // Normal: Low 12 / Low 24
            const float a = crossing12 (curve (fr, patch (model, st, 1000.0f)));
            const float b = crossing12 (curve (fr, patch (model, st, 2000.0f)));
            const double octaves = std::log2 (static_cast<double> (b) / static_cast<double> (a));
            std::printf ("    model %d style %d: -12 dB at %.0f Hz and %.0f Hz (%.3f octaves)\n", model, st, a, b, octaves);
            CHECK (a > 0.0f && b > 0.0f);
            CHECK (std::fabs (octaves - 1.0) < 0.1);
        }
}

TEST (own_filter_response_high_pass_and_band_pass_styles)
{
    FilterResponse fr (kSr);
    // model 22 "Normal": styles 2 / 3 low pass 12 / 24, 4 / 5 high pass 12 / 24, 6 / 7 band pass 12 / 24
    for (int st : { 4, 5 })
    {
        const auto c = curve (fr, patch (kNormal, st, 1000.0f));
        CHECK (at (c, 100.0f) < -15.0f);
        CHECK (at (c, 8000.0f) > -3.0f);
    }
    for (int st : { 6, 7 })
    {
        const auto c = curve (fr, patch (kNormal, st, 1000.0f));
        CHECK (at (c, 100.0f) < at (c, 1000.0f) - 12.0f);
        CHECK (at (c, 10000.0f) < at (c, 1000.0f) - 12.0f);
    }
}

TEST (own_filter_response_comb_has_several_peaks)
{
    FilterResponse fr (kSr);
    ParamValues v = patch (kComb, 0, 400.0f, 70.0f);
    const auto c = curve (fr, v);
    int peaks = 0;
    for (int i = 2; i < kPoints - 2; ++i)
    {
        const float x = c[static_cast<size_t> (i)];
        if (x > c[static_cast<size_t> (i - 1)] && x >= c[static_cast<size_t> (i + 1)] && x > c[static_cast<size_t> (i - 2)] && x > c[static_cast<size_t> (i + 2)])
        {
            // a peak counts when the curve dropped by 3 dB to a trough on both sides before the next peak: look +-40 points
            float lo = x, lo2 = x;
            for (int k = std::max (0, i - 40); k < i; ++k)
                lo = std::min (lo, c[static_cast<size_t> (k)]);
            for (int k = i + 1; k < std::min (kPoints, i + 40); ++k)
                lo2 = std::min (lo2, c[static_cast<size_t> (k)]);
            if (x - lo >= 3.0f && x - lo2 >= 3.0f)
                ++peaks;
        }
    }
    std::printf ("    comb: %d separate peaks\n", peaks);
    CHECK (peaks >= 3);
}

TEST (own_filter_response_every_model_is_finite_and_fast)
{
    FilterResponse fr (kSr);
    const int models = static_cast<int> (findParamByKey ("filter1.model")->max) + 1; // the list kFilterModels of the table
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
                    std::printf ("    model %d style %d res %.0f: not finite / out of range\n", m, st, res);
                sumMs += ms;
                ++runs;
                if (ms > worstMs)
                    worstMs = ms, worstModel = m, worstStyle = st;
            }
    std::printf ("    %d computes (%d samples, %.2f Hz per bin): mean %.3f ms, worst %.3f ms (model %d style %d)\n", runs, fr.impulseLength(), fr.binWidthHz(), sumMs / runs, worstMs,
                worstModel, worstStyle);
    if (!sbtest::instrumented())
        CHECK (worstMs < 5.0);
}

TEST (own_filter_response_overrides_equal_a_changed_snapshot)
{
    FilterResponse fr (kSr);
    for (int model : { kAnalog, 3, 8, kComb })
    {
        ParamValues v = patch (model, 0, 1000.0f, 40.0f);
        const auto base = curve (fr, v);
        FilterResponse::Request same;
        same.cutoffHz = 1000.0f;
        same.resonancePercent = 40.0f;
        const auto withSame = curve (fr, v, same);
        CHECK (std::memcmp (base.data(), withSame.data(), base.size() * sizeof (float)) == 0);

        FilterResponse::Request over;
        over.cutoffHz = 3000.0f;
        over.resonancePercent = 75.0f;
        const auto overridden = curve (fr, v, over);
        const auto snapshot = curve (fr, patch (model, 0, 3000.0f, 75.0f));
        CHECK (std::memcmp (overridden.data(), snapshot.data(), snapshot.size() * sizeof (float)) == 0);
        CHECK (std::memcmp (base.data(), overridden.data(), base.size() * sizeof (float)) != 0);
    }
}

TEST (own_filter_response_key_tracking_uses_the_reference_note)
{
    FilterResponse fr (kSr);
    ParamValues v = patch (kAnalog, 0, 1000.0f);
    setv (v, "filter1.keytrack", 100.0f);
    FilterResponse::Request r72;
    r72.referenceNote = 72; // one octave up: cutoff 2 kHz
    const float a = crossing12 (curve (fr, v));
    const float b = crossing12 (curve (fr, v, r72));
    CHECK (std::fabs (std::log2 (static_cast<double> (b) / static_cast<double> (a)) - 1.0) < 0.1);
    FilterResponse::Request r60;
    CHECK (curve (fr, v, r60) == curve (fr, v));
}

TEST (own_filter_response_is_deterministic_and_filter_2_works)
{
    FilterResponse fr (kSr);
    ParamValues v = patch (3, 1, 2500.0f, 60.0f);
    const auto a = curve (fr, v);
    const auto b = curve (fr, v);
    CHECK (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
    FilterResponse other (kSr); // a fresh instance gives the same bits
    const auto c = curve (other, v);
    CHECK (std::memcmp (a.data(), c.data(), a.size() * sizeof (float)) == 0);

    // filter 2 (off by default) with the same settings gives the same curve as filter 1
    ParamValues w = ParamValues::defaults();
    setv (w, "filter2.on", 1);
    setv (w, "filter2.model", 3);
    setv (w, "filter2.style", 1);
    setv (w, "filter2.cutoff", 2500.0f);
    setv (w, "filter2.resonance", 60.0f);
    FilterResponse::Request r2;
    r2.filter = 2;
    const auto d = curve (fr, w, r2);
    CHECK (std::memcmp (a.data(), d.data(), a.size() * sizeof (float)) == 0);
}

TEST (own_filter_response_switched_off_returns_false)
{
    FilterResponse fr (kSr);
    ParamValues v = patch (kAnalog, 0, 1000.0f);
    setv (v, "filter1.on", 0);
    std::vector<float> db (64, 5.0f);
    CHECK (!fr.compute (v, {}, db.data(), 64));
    for (float x : db)
        CHECK (x == 0.0f);
    FilterResponse::Request r2;
    r2.filter = 2; // off by default
    std::fill (db.begin(), db.end(), 5.0f);
    CHECK (!fr.compute (ParamValues::defaults(), r2, db.data(), 64));
    for (float x : db)
        CHECK (x == 0.0f);
    r2.filter = 3;
    CHECK (!fr.compute (v, r2, db.data(), 64));
}

namespace
{
float srcChoice (const char* key)
{
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::string (modSourceTable()[i].key) == key)
            return static_cast<float> (i + 1);
    return 0.0f;
}

float dstChoice (const char* key)
{
    const ParamInfo* p = findParamByKey (key);
    return p != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (p->id)))) : 0.0f;
}

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    if (CHECK (p != nullptr))
        e.setParam (p->id, v);
}

std::unique_ptr<IEngine> lfoCutoffEngine()
{
    auto e = createOwnEngine();
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 1);
    set (*e, "filter1.model", 0);
    set (*e, "filter1.cutoff", 1000.0f);
    set (*e, "lfo1.shape", 0);
    set (*e, "lfo1.rate", 4.0f);
    set (*e, "lfo1.smooth", 0);
    set (*e, "mod01.source", srcChoice ("lfo1"));
    set (*e, "mod01.destination", dstChoice ("filter1.cutoff"));
    set (*e, "mod01.bipolar", 1);
    set (*e, "mod01.amount", 25.0f);
    return e;
}
} // namespace

TEST (own_filter_response_readout_reports_plain_value_without_voice_and_moves_with_lfo)
{
    auto e = lfoCutoffEngine();
    int idx[2];
    FilterResponse::readoutIndices (1, idx);
    CHECK (idx[0] == findParamIndex (findParamByKey ("filter1.cutoff")->id));
    CHECK (idx[1] == findParamIndex (findParamByKey ("filter1.resonance")->id));
    e->setReadoutTargets (idx, 2);

    std::vector<float> l (256), r (256);
    LiveValues live;
    // no sounding voice: the plain value of the knobs
    for (int b = 0; b < 4; ++b)
        e->process (nullptr, 0, l.data(), r.data(), 256);
    CHECK (e->copyReadout (live));
    CHECK (live.numTargets == 2);
    CHECK (live.activeVoices == 0);
    CHECK (std::fabs (live.target[0] - 1000.0f) < 0.01f);
    CHECK (std::fabs (live.target[1] - 20.0f) < 0.01f); // default resonance 20 %

    // a note is held: the LFO moves the cutoff, in Hz
    Event on = sbtest::noteOn (0, 60, 0.8f);
    e->process (&on, 1, l.data(), r.data(), 256);
    float lo = 1.0e9f, hi = -1.0e9f;
    for (int b = 0; b < 200; ++b) // 1.07 s: about four LFO cycles
    {
        e->process (nullptr, 0, l.data(), r.data(), 256);
        CHECK (e->copyReadout (live));
        CHECK (live.activeVoices >= 1);
        lo = std::min (lo, live.target[0]);
        hi = std::max (hi, live.target[0]);
        CHECK (std::fabs (live.target[1] - 20.0f) < 0.01f); // the resonance is not modulated
    }
    std::printf ("    cutoff readout under an LFO: %.1f .. %.1f Hz (knob 1000 Hz)\n", lo, hi);
    CHECK (lo < 600.0f);
    CHECK (hi > 1500.0f);
    CHECK (lo > 100.0f && hi < 10000.0f); // plain Hz: the modulated value of the knob, not clamped to the knob's range (the filter clamps what it runs on)

    // after the release and the tail the voice is gone: the plain value again
    Event off = sbtest::noteOff (0, 60);
    e->process (&off, 1, l.data(), r.data(), 256);
    for (int b = 0; b < 800; ++b)
        e->process (nullptr, 0, l.data(), r.data(), 256);
    CHECK (e->copyReadout (live));
    CHECK (live.activeVoices == 0);
    CHECK (std::fabs (live.target[0] - 1000.0f) < 0.01f);
}

TEST (own_filter_response_follows_the_modulated_cutoff_of_the_readout)
{
    auto e = lfoCutoffEngine();
    int idx[2];
    FilterResponse::readoutIndices (1, idx);
    e->setReadoutTargets (idx, 2);
    std::vector<float> l (256), r (256);
    Event on = sbtest::noteOn (0, 60, 0.8f);
    e->process (&on, 1, l.data(), r.data(), 256);
    FilterResponse fr (kSr);
    ParamValues v = patch (kAnalog, 0, 1000.0f);
    float lo = 1.0e9f, hi = -1.0e9f, loX = 0.0f, hiX = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        e->process (nullptr, 0, l.data(), r.data(), 256);
        LiveValues live;
        e->copyReadout (live);
        FilterResponse::Request req;
        req.cutoffHz = live.target[0];
        req.resonancePercent = live.target[1];
        const float x = crossing12 (curve (fr, v, req));
        if (live.target[0] < lo)
            lo = live.target[0], loX = x;
        if (live.target[0] > hi)
            hi = live.target[0], hiX = x;
    }
    std::printf("    cutoff %.0f Hz -> -12 dB at %.0f Hz; cutoff %.0f Hz -> %.0f Hz\n", lo, loX, hi, hiX);
    CHECK (hiX > loX * 1.5f);
}

TEST (own_filter_response_is_independent_of_a_running_engine)
{
    auto e = lfoCutoffEngine();
    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    std::thread audio ([&]
    {
        std::vector<float> l (256), r (256);
        Event on = sbtest::noteOn (0, 48, 0.9f);
        e->process (&on, 1, l.data(), r.data(), 256);
        while (!stop.load())
        {
            e->process (nullptr, 0, l.data(), r.data(), 256);
            blocks.fetch_add (1);
        }
    });
    FilterResponse fr (kSr);
    ParamValues v = patch (3, 1, 2000.0f, 50.0f);
    const auto ref = curve (fr, v);
    const int rounds = sbtest::scaled (60, 12);
    for (int i = 0; i < rounds; ++i)
    {
        const auto c = curve (fr, v);
        CHECK (std::memcmp (ref.data(), c.data(), ref.size() * sizeof (float)) == 0);
    }
    stop = true;
    audio.join();
    CHECK (blocks.load() > 0);
}
