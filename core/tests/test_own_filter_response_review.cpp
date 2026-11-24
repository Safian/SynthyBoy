// SynthyBoy - tests of FilterResponse for the findings of the review (docs: core/docs/filter-response-review-answers.md): explicit overrides, the readout of an idle
// engine, stereo, argument validation, the response below the first bin, the level floor, the output offset of MG Dirty, state that must not leak between calls.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
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
constexpr int kPoints = 128;
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

void setv (ParamValues& v, const char* key, float x)
{
    const ParamInfo* p = findParamByKey (key);
    if (!CHECK (p != nullptr))
        return;
    v.plain[findParamIndex (p->id)] = constrainValue (*p, x);
}

void setRaw (ParamValues& v, const char* key, float x) // no constraining: for the out of range and NaN cases
{
    const ParamInfo* p = findParamByKey (key);
    if (CHECK (p != nullptr))
        v.plain[findParamIndex (p->id)] = x;
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

std::vector<float> curve (FilterResponse& fr, const ParamValues& v, const FilterResponse::Request& req = {}, int points = kPoints)
{
    std::vector<float> db (static_cast<size_t> (points));
    CHECK (fr.compute (v, req, db.data(), points));
    return db;
}

bool same (const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0; }

bool allZero (const std::vector<float>& a)
{
    return std::all_of (a.begin(), a.end(), [] (float x) { return x == 0.0f; });
}

float maxOf (const std::vector<float>& c) { return *std::max_element (c.begin(), c.end()); }

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
} // namespace

// ---- (a) the override is an explicit optional, not a magic value ---------------------------------------------------------------------------------------------

TEST (own_filter_response_negative_modulated_resonance_does_not_switch_the_override_off)
{
    FilterResponse fr (kSr);
    const ParamValues v = patch (0, 0, 1000.0f, 90.0f); // knob: 90 %
    FilterResponse::Request neg;
    neg.resonancePercent = -10.0f; // what the readout reports when a modulation pulls the knob below zero: the filter runs at 0 %
    const auto atNeg = curve (fr, v, neg);
    FilterResponse::Request zero;
    zero.resonancePercent = 0.0f;
    const auto atZero = curve (fr, v, zero);
    const auto knob = curve (fr, v);
    CHECK (same (atNeg, atZero));   // clamped by the filter like in the voice
    CHECK (!same (atNeg, knob));    // and NOT the 90 % of the knob
    CHECK (maxOf (knob) > maxOf (atZero) + 1.0f);
    FilterResponse::Request over;
    over.resonancePercent = 150.0f; // above the knob range: passed on, the filter clamps (same as 100 %)
    FilterResponse::Request hundred;
    hundred.resonancePercent = 100.0f;
    CHECK (same (curve (fr, v, over), curve (fr, v, hundred)));
}

TEST (own_filter_response_each_override_alone_replaces_only_its_knob)
{
    FilterResponse fr (kSr);
    const ParamValues v = patch (0, 0, 1000.0f, 40.0f);
    FilterResponse::Request c;
    c.cutoffHz = 3000.0f;
    CHECK (same (curve (fr, v, c), curve (fr, patch (0, 0, 3000.0f, 40.0f))));
    FilterResponse::Request r;
    r.resonancePercent = 80.0f;
    CHECK (same (curve (fr, v, r), curve (fr, patch (0, 0, 1000.0f, 80.0f))));
    FilterResponse::Request none; // unset: the knobs
    CHECK (same (curve (fr, v, none), curve (fr, v)));
}

// ---- (b) the readout of an idle engine follows the global sources -------------------------------------------------------------------------------------------

namespace
{
std::unique_ptr<IEngine> macroCutoffEngine (const char* source)
{
    auto e = createOwnEngine();
    e->prepare (kSr, 256, qualityForTier (Tier::high));
    set (*e, "filter1.on", 1);
    set (*e, "filter1.cutoff", 1000.0f);
    set (*e, "filter1.resonance", 30.0f);
    set (*e, "mod01.source", srcChoice (source));
    set (*e, "mod01.destination", dstChoice ("filter1.cutoff"));
    set (*e, "mod01.bipolar", 0);
    set (*e, "mod01.amount", 50.0f);
    int idx[2];
    FilterResponse::readoutIndices (1, idx);
    e->setReadoutTargets (idx, 2);
    return e;
}

LiveValues idle (IEngine& e, int blocks = 6, const Event* ev = nullptr)
{
    std::vector<float> l (256), r (256);
    for (int b = 0; b < blocks; ++b)
        e.process (b == 0 ? ev : nullptr, b == 0 && ev != nullptr ? 1 : 0, l.data(), r.data(), 256);
    LiveValues live;
    CHECK (e.copyReadout (live));
    return live;
}
} // namespace

TEST (own_filter_response_readout_without_a_voice_follows_macro_and_mod_wheel)
{
    {
        auto e = macroCutoffEngine ("macro1");
        set (*e, "macro.1", 0.0f);
        const LiveValues lo = idle (*e);
        CHECK (lo.activeVoices == 0);
        set (*e, "macro.1", 100.0f);
        const LiveValues hi = idle (*e);
        CHECK (hi.activeVoices == 0);
        std::printf ("    idle macro 1: cutoff readout %.1f Hz (macro 0) .. %.1f Hz (macro 100), knob 1000 Hz\n", lo.target[0], hi.target[0]);
        CHECK (std::fabs (lo.target[0] - 1000.0f) < 0.5f); // unipolar: macro 0 adds nothing
        CHECK (hi.target[0] > 1500.0f);
        CHECK (std::fabs (hi.target[1] - 30.0f) < 0.01f);  // resonance is not modulated
        // the display's curve moves with it
        FilterResponse fr (kSr);
        FilterResponse::Request a, b;
        a.cutoffHz = lo.target[0];
        b.cutoffHz = hi.target[0];
        CHECK (!same (curve (fr, patch (0, 0, 1000.0f, 30.0f), a), curve (fr, patch (0, 0, 1000.0f, 30.0f), b)));
    }
    {
        auto e = macroCutoffEngine ("mod_wheel");
        Event mw;
        mw.type = Event::ModWheel;
        mw.channel = 0;
        mw.value = 1.0f;
        mw.frame = 0;
        const LiveValues up = idle (*e, 6, &mw);
        CHECK (up.activeVoices == 0);
        CHECK (up.target[0] > 1500.0f);
    }
    {
        // a slot that reads a per voice source has no value without a voice: the plain knob stays (an LFO or envelope must not look like 0)
        auto e = macroCutoffEngine ("lfo1");
        set (*e, "mod01.bipolar", 1);
        const LiveValues live = idle (*e);
        CHECK (std::fabs (live.target[0] - 1000.0f) < 0.01f);
    }
}

TEST (own_filter_response_idle_readout_leaves_the_sound_unchanged)
{
    // the same patch, rendered once with a readout subscription (preview evaluated) and once without: bit identical
    auto render = [] (bool subscribe)
    {
        auto e = macroCutoffEngine ("macro1");
        if (!subscribe)
            e->setReadoutTargets (nullptr, 0);
        set (*e, "macro.1", 70.0f);
        std::vector<float> l (256 * 8), r (256 * 8);
        Event on = sbtest::noteOn (256, 60, 0.8f);
        e->process (nullptr, 0, l.data(), r.data(), 256);
        e->process (&on, 1, l.data() + 256, r.data() + 256, 256);
        for (int b = 2; b < 8; ++b)
            e->process (nullptr, 0, l.data() + 256 * b, r.data() + 256 * b, 256);
        return l;
    };
    CHECK (same (render (true), render (false)));
}

// ---- (c) stereo ---------------------------------------------------------------------------------------------------------------------------------------------

TEST (own_filter_response_filter_pan_shows_the_power_mean_of_the_two_channels)
{
    FilterResponse fr (kSr);
    ParamValues v = patch (0, 0, 1000.0f, 0.0f);
    setv (v, "filter1.pan", 50.0f); // +-6 semitones, opposite in the two channels
    const auto shown = curve (fr, v, {}, 256);
    FilterResponse::Request lo, hi;
    lo.cutoffHz = 1000.0f * std::exp2 (-0.5f);
    hi.cutoffHz = 1000.0f * std::exp2 (0.5f);
    const auto a = curve (fr, patch (0, 0, 1000.0f, 0.0f), lo, 256);
    const auto b = curve (fr, patch (0, 0, 1000.0f, 0.0f), hi, 256);
    float worst = 0.0f;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const double pa = std::pow (10.0, a[i] / 10.0), pb = std::pow (10.0, b[i] / 10.0);
        const double mean = 10.0 * std::log10 (0.5 * (pa + pb));
        worst = std::max (worst, static_cast<float> (std::fabs (static_cast<double> (shown[i]) - mean)));
    }
    std::printf ("    pan 50 %%: shown curve vs the power mean of the two channel curves: worst %.4f dB\n", worst);
    CHECK (worst < 0.01f);
    CHECK (!same (shown, curve (fr, patch (0, 0, 1000.0f, 0.0f), {}, 256))); // differs from no pan
}

TEST (own_filter_response_a_stereo_modulation_shows_the_left_lane)
{
    // the readout carries the left lane of a slot with the stereo flag; the display's curve for that value is the plain curve of that cutoff
    auto e = macroCutoffEngine ("macro1");
    set (*e, "mod01.stereo", 1);
    set (*e, "mod01.amount", 15.0f);
    set (*e, "macro.1", 100.0f);
    const LiveValues live = idle (*e);
    FilterResponse fr (kSr);
    FilterResponse::Request req;
    req.cutoffHz = live.target[0];
    CHECK (live.target[0] > 1100.0f && live.target[0] < 10000.0f);
    CHECK (same (curve (fr, patch (0, 0, 1000.0f, 30.0f), req), curve (fr, patch (0, 0, live.target[0], 30.0f))));
}

// ---- (d) validation -----------------------------------------------------------------------------------------------------------------------------------------

TEST (own_filter_response_unusable_arguments_have_defined_behaviour)
{
    FilterResponse fr (kSr);
    const ParamValues v = patch (0, 0, 1000.0f, 20.0f);
    std::vector<float> db (32, 7.0f);
    auto bad = [&] (const FilterResponse::Request& r)
    {
        std::fill (db.begin(), db.end(), 7.0f);
        const bool ok = fr.compute (v, r, db.data(), 32);
        return !ok && allZero (db);
    };
    FilterResponse::Request r;
    r.fMin = kNan;                              CHECK (bad (r));
    r = {}; r.fMax = kNan;                      CHECK (bad (r));
    r = {}; r.fMin = kInf; r.fMax = kInf;       CHECK (bad (r)); // the case that was a float -> size_t cast of NaN
    r = {}; r.fMax = kInf;                      CHECK (bad (r));
    r = {}; r.fMin = 0.0f;                      CHECK (bad (r));
    r = {}; r.fMin = -5.0f;                     CHECK (bad (r));
    r = {}; r.fMin = 5000.0f; r.fMax = 100.0f;  CHECK (bad (r));
    r = {}; r.cutoffHz = kNan;                  CHECK (bad (r));
    r = {}; r.cutoffHz = 0.0f;                  CHECK (bad (r));
    r = {}; r.cutoffHz = -3.0f;                 CHECK (bad (r));
    r = {}; r.resonancePercent = kInf;          CHECK (bad (r));
    r = {}; r.filter = 0;                       CHECK (bad (r));
    r = {}; r.filter = 3;                       CHECK (bad (r));
    // equal fMin / fMax is a flat axis: allowed
    r = {}; r.fMin = 1000.0f; r.fMax = 1000.0f;
    CHECK (fr.compute (v, r, db.data(), 32));
    for (float x : db)
        CHECK (x == db[0]);
    // null output, numPoints < 1: false, nothing written
    CHECK (!fr.compute (v, {}, nullptr, 32));
    float guard = 7.0f;
    CHECK (!fr.compute (v, {}, &guard, 0));
    CHECK (!fr.compute (v, {}, &guard, -4));
    CHECK (guard == 7.0f);
    // numPoints == 1: the value at fMin
    float one = 0.0f;
    FilterResponse::Request at1k;
    at1k.fMin = 1000.0f;
    at1k.fMax = 8000.0f;
    CHECK (fr.compute (v, at1k, &one, 1));
    std::vector<float> two (2);
    CHECK (fr.compute (v, at1k, two.data(), 2));
    CHECK (one == two[0]);
    CHECK (one < -1.0f && one > -15.0f); // the 12 dB analog filter at its own cutoff
    // readoutIndices with a null pointer and a bad filter number
    FilterResponse::readoutIndices (1, nullptr);
    int idx[2] = { 5, 5 };
    FilterResponse::readoutIndices (3, idx);
    CHECK (idx[0] == -1 && idx[1] == -1);
    FilterResponse::readoutIndices (2, idx);
    CHECK (idx[0] == findParamIndex (findParamByKey ("filter2.cutoff")->id));
    CHECK (idx[1] == findParamIndex (findParamByKey ("filter2.resonance")->id));
}

TEST (own_filter_response_unusable_snapshot_values_have_defined_behaviour)
{
    FilterResponse fr (kSr);
    std::vector<float> db (16, 7.0f);
    for (const char* key : { "filter1.on", "filter1.model", "filter1.style", "filter1.cutoff", "filter1.resonance", "filter1.drive", "filter1.mix", "filter1.blend", "filter1.keytrack",
                             "filter1.pan", "filter1.var" })
        for (float bad : { kNan, kInf, -kInf })
        {
            ParamValues v = patch (0, 0, 1000.0f);
            setRaw (v, key, bad);
            std::fill (db.begin(), db.end(), 7.0f);
            if (!CHECK (!fr.compute (v, {}, db.data(), 16)))
                std::printf ("    %s = %g was accepted\n", key, static_cast<double> (bad));
            CHECK (allZero (db));
        }
    // finite but out of range values are clamped to the knob range: no undefined integer cast
    ParamValues w = patch (0, 0, 1000.0f);
    setRaw (w, "filter1.model", 1.0e30f);
    setRaw (w, "filter1.style", -1.0e30f);
    setRaw (w, "filter1.cutoff", 1.0e30f);
    CHECK (fr.compute (w, {}, db.data(), 16));
    // the level is only looked at when it is asked for
    ParamValues x = patch (0, 0, 1000.0f);
    setRaw (x, "filter1.level", kNan);
    CHECK (fr.compute (x, {}, db.data(), 16));
    FilterResponse::Request lv;
    lv.includeLevel = true;
    CHECK (!fr.compute (x, lv, db.data(), 16));
    // the reference note is limited to the MIDI range
    FilterResponse::Request far;
    far.referenceNote = 2000000000;
    ParamValues k = patch (0, 0, 1000.0f);
    setv (k, "filter1.keytrack", 100.0f);
    CHECK (fr.compute (k, far, db.data(), 16));
    for (float y : db)
        CHECK (std::isfinite (y));
}

TEST (own_filter_response_sample_rate_is_made_usable)
{
    for (double bad : { 0.0, -48000.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
    {
        FilterResponse fr (bad);
        CHECK (fr.sampleRate() == 48000.0);
        CHECK (curve (fr, patch (0, 0, 1000.0f)).size() == kPoints);
    }
    FilterResponse low (8000.0), high (384000.0);
    CHECK (low.sampleRate() == 22050.0); // the range the filters run in (filters::Filter::prepare clamps the same way)
    CHECK (high.sampleRate() == 192000.0);
    CHECK (low.impulseLength() == 8192);
    CHECK (high.impulseLength() == 65536);
    CHECK (high.binWidthHz() > 2.9 && high.binWidthHz() < 3.0);
    // a 24 dB low pass at 1 kHz is where the cutoff says at the clamped rate: one octave above the cutoff it is far down
    const auto c = curve (high, patch (22, 3, 1000.0f), {}, 256);
    CHECK (c[0] > -1.5f);
    CHECK (c.back() < -60.0f);
}

// ---- (e) below the first bin --------------------------------------------------------------------------------------------------------------------------------

TEST (own_filter_response_below_the_first_bin_keeps_the_slope_of_a_high_pass)
{
    FilterResponse fr (kSr);
    // Normal 12 dB high pass (style 4) at 30 Hz: 12 dB per octave below the cutoff, bin width 2.93 Hz
    const ParamValues v = patch (22, 4, 30.0f);
    FilterResponse::Request req;
    req.fMin = 0.5f;
    req.fMax = 2.0f; // two octaves, all below the first bin (2.93 Hz)
    std::vector<float> c (3);
    CHECK (fr.compute (v, req, c.data(), 3));
    const float slope = (c[2] - c[0]) / 2.0f; // dB per octave
    std::printf ("    12 dB high pass 30 Hz: %.2f dB at 0.5 Hz, %.2f dB at 1 Hz, %.2f dB at 2 Hz: %.2f dB per octave (interpolating towards the DC bin gave about 6)\n", c[0], c[1], c[2], slope);
    CHECK (slope > 10.5f && slope < 13.5f);
    // continuous across the first bin
    req.fMin = 2.5f;
    req.fMax = 3.5f;
    std::vector<float> d (64);
    CHECK (fr.compute (v, req, d.data(), 64));
    float jump = 0.0f;
    for (size_t i = 1; i < d.size(); ++i)
        jump = std::max (jump, std::fabs (d[i] - d[i - 1]));
    CHECK (jump < 0.5f);
    // a low pass is flat there
    req.fMin = 0.5f;
    req.fMax = 2.0f;
    CHECK (fr.compute (patch (22, 2, 1000.0f), req, c.data(), 3));
    CHECK (std::fabs (c[0]) < 0.5f && std::fabs (c[2]) < 0.5f);
}

// ---- level floor and the output offset of MG Dirty ----------------------------------------------------------------------------------------------------------

TEST (own_filter_response_level_is_added_after_the_floor)
{
    FilterResponse fr (kSr);
    ParamValues v = patch (22, 3, 500.0f); // 24 dB low pass: the top of the axis is far below -120 dB
    setv (v, "filter1.level", 12.0f);
    FilterResponse::Request with;
    with.includeLevel = true;
    const auto a = curve (fr, v, {}, 256);
    const auto b = curve (fr, v, with, 256);
    CHECK (a.back() == -120.0f);
    CHECK (b.back() == -120.0f + 12.0f); // the silent end is the floor plus the level, like every other quiet bin: no jump
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] > -100.0f)
            CHECK (std::fabs ((b[i] - a[i]) - 12.0f) < 1.0e-3f);
    // monotone down the stopband of a plain low pass (no 60 dB step back up)
    for (size_t i = 150; i + 1 < b.size(); ++i)
        CHECK (b[i + 1] <= b[i] + 0.01f);
}

TEST (own_filter_response_output_offset_of_a_model_does_not_swamp_the_curve)
{
    FilterResponse fr (kSr);
    // MG Dirty (model 26, style 2) adds 0.15 * var to its input: at the default var the output has a DC offset 75 times the measuring impulse
    const ParamValues v = patch (26, 2, 1000.0f, 20.0f);
    const auto c = curve (fr, v, {}, 256);
    std::printf ("    MG Dirty var 50 %%: %.2f dB at 20 Hz, max %.2f dB, %.2f dB at 20 kHz\n", c.front(), maxOf (c), c.back());
    CHECK (maxOf (c) < 30.0f);     // the small signal gain of the 'pain' stage (about +17 dB), not the +60 dB of the step
    CHECK (c.front() > 5.0f);
    CHECK (c.back() < c.front() - 60.0f); // still a low pass
    // and a model without an offset is untouched by the baseline: bit identical to a second call
    const ParamValues w = patch (0, 0, 1000.0f);
    CHECK (same (curve (fr, w), curve (fr, w)));
}

// ---- state must not leak between calls ----------------------------------------------------------------------------------------------------------------------

TEST (own_filter_response_no_state_leaks_between_different_patches)
{
    FilterResponse fresh (kSr);
    const ParamValues a = patch (0, 1, 800.0f, 70.0f);
    const auto ref = curve (fresh, a);
    FilterResponse used (kSr);
    ParamValues pan = patch (3, 0, 3000.0f, 90.0f);
    setv (pan, "filter1.pan", 40.0f);
    ParamValues clean = patch (6, 0, 400.0f, 70.0f);
    setv (clean, "filter1.clean_mode", 1.0f);
    for (const ParamValues* other : { &pan, &clean })
    {
        (void) curve (used, *other);
        CHECK (same (curve (used, a), ref));
    }
    (void) curve (used, patch (26, 2, 1000.0f)); // the one with an offset
    CHECK (same (curve (used, a), ref));
}

TEST (own_filter_response_clean_mode_does_not_change_the_small_signal_curve)
{
    // clean mode has no effect on the small signal response (-24 dB before, +24 dB after the filter): stated in the header
    FilterResponse fr (kSr);
    ParamValues v = patch (0, 0, 1000.0f, 30.0f);
    ParamValues c = v;
    setv (c, "filter1.clean_mode", 1.0f);
    const auto a = curve (fr, v), b = curve (fr, c);
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::fabs (a[i] - b[i]));
    std::printf ("    clean mode: largest change of the curve %.3f dB\n", worst);
    CHECK (worst < 0.2f);
}
