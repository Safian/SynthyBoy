// SynthyBoy - tests of the display data of the own engine: the LFO shape (LfoDisplay) and one cycle of the wavetable oscillator (OscCycle), checked against the engine.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/env_display.h"
#include "synthyboy/engine.h"
#include "synthyboy/lfo_display.h"
#include "synthyboy/modulation.h"
#include "synthyboy/osc_cycle.h"
#include "synthyboy/readout.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"
#include "../engine-own/env_lfo/envelope.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    if (CHECK (p != nullptr))
        e.setParam (p->id, v);
}

void setv (ParamValues& v, const char* key, float x)
{
    const ParamInfo* p = findParamByKey (key);
    if (CHECK (p != nullptr))
        v.plain[findParamIndex (p->id)] = constrainValue (*p, x);
}

int indexOfKey (const char* key) { return findParamIndex (findParamByKey (key)->id); }

float srcChoice (const char* key)
{
    for (size_t i = 0; i < modSourceCount(); ++i)
        if (std::string (modSourceTable()[i].key) == key)
            return static_cast<float> (i + 1);
    return 0.0f;
}

/** The display curve at x (linear interpolation of the n points). */
float at (const std::vector<float>& c, float x)
{
    const float pos = std::clamp (x, 0.0f, 1.0f) * static_cast<float> (c.size() - 1);
    const size_t i = std::min (static_cast<size_t> (pos), c.size() - 2);
    const float f = pos - static_cast<float> (i);
    return c[i] * (1.0f - f) + c[i + 1] * f;
}

/** An engine with LFO 1 as the only thing that matters: a held note, the live sources read out. `setup` sets the LFO parameters. */
struct LfoRig
{
    std::unique_ptr<IEngine> e;
    std::vector<float> l = std::vector<float> (256), r = std::vector<float> (256);
    ParamValues values = ParamValues::defaults();
    bool meters = false;   // the display shows the source strips: requestMeters () before every block

    template <class F>
    explicit LfoRig (F&& setup, const StateContent* content = nullptr)
    {
        e = createOwnEngine();
        e->prepare (kSr, 256, qualityForTier (Tier::high));
        setup (*e, values);
        if (content != nullptr)
            e->setContent (*content);
    }
    void note() { Event on = sbtest::noteOn (0, 60, 0.8f); if (meters) e->requestMeters(); e->process (&on, 1, l.data(), r.data(), 256); }
    LiveValues block()
    {
        if (meters)
            e->requestMeters();
        e->process (nullptr, 0, l.data(), r.data(), 256);
        LiveValues live;
        CHECK (e->copyReadout (live));
        return live;
    }
};

void both (IEngine& e, ParamValues& v, const char* key, float x)
{
    set (e, key, x);
    setv (v, key, x);
}

/** The largest difference between the LFO value the engine reports for a held note (source lfo1, left lane) and the display curve at the engine's own cursor position. */
float worstAgainstEngine (LfoRig& rig, int blocks, const StateContent* content, bool skipFirstCycle, float* firstReadout = nullptr)
{
    const float lfoSrc = static_cast<float> (srcChoice ("lfo1"));
    std::vector<float> y (2049);
    LfoDisplay::Request req;
    req.lfo = 1;
    CHECK (LfoDisplay::compute (rig.values, content, req, y.data(), 2049));
    rig.note();
    float worst = 0.0f;
    float lastX = -1.0f;
    int cycles = 0;
    for (int b = 0; b < blocks; ++b)
    {
        const LiveValues live = rig.block();
        const float x = LfoDisplay::cursorX (live.lfoPhase[0]);
        if (firstReadout != nullptr && b == 0)
            *firstReadout = live.lfoPhase[0];
        if (x < lastX)
            ++cycles;
        lastX = x;
        if (skipFirstCycle && cycles < 1)
            continue;
        const float engine = live.source[static_cast<size_t> (lfoSrc) - 1];
        worst = std::max (worst, std::fabs (engine - at (y, x)));
    }
    return worst;
}
} // namespace

TEST (own_lfo_display_every_builtin_shape_matches_the_engine)
{
    for (int shape : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 })
    {
        LfoRig rig ([&] (IEngine& e, ParamValues& v)
        {
            both (e, v, "lfo1.shape", static_cast<float> (shape));
            both (e, v, "lfo1.rate", 3.0f);
            both (e, v, "lfo1.smooth", 0.0f);
        });
        const float w = worstAgainstEngine (rig, 300, nullptr, false);
        std::printf ("    shape %d: worst difference to the engine's LFO value %.4f\n", shape, w);
        // the engine reads the shape once per control period (32 samples): at 3 Hz a step of the cursor is 0.002 cycles; a step shape may differ in the one sample at its edge
        const bool stepped = shape != 0 && shape != 1 && shape != 11; // the others jump (the saws at the wrap, the steps at their edges)
        CHECK (w < (stepped ? 2.0f : 0.02f));
        if (stepped)
        {
            // a stepped shape: the picture matches except within one control period of an edge: count the readings that are far off
            LfoRig again ([&] (IEngine& e, ParamValues& v)
            {
                both (e, v, "lfo1.shape", static_cast<float> (shape));
                both (e, v, "lfo1.rate", 3.0f);
                both (e, v, "lfo1.smooth", 0.0f);
            });
            std::vector<float> y (2049);
            LfoDisplay::Request req;
            CHECK (LfoDisplay::compute (again.values, nullptr, req, y.data(), 2049));
            again.note();
            int off = 0, total = 0;
            for (int b = 0; b < 300; ++b)
            {
                const LiveValues live = again.block();
                const float engine = live.source[static_cast<size_t> (srcChoice ("lfo1")) - 1];
                off += std::fabs (engine - at (y, live.lfoPhase[0])) > 0.05f;
                ++total;
            }
            CHECK (off * 20 <= total); // at most 5 % of the readings sit on an edge
        }
    }
}

TEST (own_lfo_display_phase_direction_swing_and_stereo_are_in_the_picture)
{
    struct Case { const char* key; float value; };
    for (const Case c : { Case { "lfo1.phase", 90.0f }, Case { "lfo1.direction", 1.0f }, Case { "lfo1.direction", 2.0f }, Case { "lfo1.swing", 60.0f }, Case { "lfo1.stereo", 180.0f } })
    {
        LfoRig rig ([&] (IEngine& e, ParamValues& v)
        {
            both (e, v, "lfo1.shape", 1.0f); // triangle: asymmetric under every one of the above
            both (e, v, "lfo1.rate", 2.0f);
            both (e, v, "lfo1.smooth", 0.0f);
            both (e, v, c.key, c.value);
        });
        const float w = worstAgainstEngine (rig, 300, nullptr, false);
        std::printf ("    %s = %g: worst difference %.4f\n", c.key, static_cast<double> (c.value), w);
        CHECK (w < 0.03f);
        // and the setting changed the picture
        ParamValues plain = ParamValues::defaults();
        setv (plain, "lfo1.shape", 1.0f);
        std::vector<float> a (256), b (256);
        LfoDisplay::Request req;
        CHECK (LfoDisplay::compute (plain, nullptr, req, a.data(), 256));
        CHECK (LfoDisplay::compute (rig.values, nullptr, req, b.data(), 256));
        CHECK (a != b);
    }
}

TEST (own_lfo_display_smoothing_matches_the_engine_after_the_first_cycle)
{
    LfoRig rig ([&] (IEngine& e, ParamValues& v)
    {
        both (e, v, "lfo1.shape", 4.0f); // square: smoothing makes the exponential edges
        both (e, v, "lfo1.rate", 2.0f);
        both (e, v, "lfo1.smooth", 1.0f);
        both (e, v, "lfo1.smooth_time", 80.0f);
    });
    const float w = worstAgainstEngine (rig, 800, nullptr, true);
    std::printf("    smoothed square, 2 Hz, 80 ms: worst difference to the engine %.4f\n", w);
    CHECK (w < 0.06f);
    ParamValues raw = rig.values;
    setv (raw, "lfo1.smooth", 0.0f);
    std::vector<float> a (256), b (256);
    LfoDisplay::Request req;
    CHECK (LfoDisplay::compute (raw, nullptr, req, a.data(), 256));
    CHECK (LfoDisplay::compute (rig.values, nullptr, req, b.data(), 256));
    CHECK (a != b);
    CHECK (b[1] > -0.99f && b[1] < 0.99f); // not the square's value: the smoothing rounds the edge
}

TEST (own_lfo_display_custom_shape_is_the_engines)
{
    StateContent content;
    ContentLfoShape s;
    s.lfo = 1;
    s.smooth = true;
    s.points = { ContentPoint (0.0f, 0.1f), ContentPoint (0.3f, 0.95f, 2.0f), ContentPoint (0.7f, 0.2f, -3.0f), ContentPoint (1.0f, 0.6f) };
    content.lfoShapes.push_back (s);
    LfoRig rig ([&] (IEngine& e, ParamValues& v)
    {
        both (e, v, "lfo1.shape", 12.0f);
        both (e, v, "lfo1.rate", 2.0f);
        both (e, v, "lfo1.smooth", 0.0f);
    }, &content);
    const float w = worstAgainstEngine (rig, 300, &content, false);
    std::printf ("    custom shape (4 points, curved): worst difference %.4f\n", w);
    CHECK (w < 0.03f);
    // without the content the shape does not exist: the same as the engine without it (a flat line at the centre)
    std::vector<float> flat (64);
    LfoDisplay::Request req;
    CHECK (LfoDisplay::compute (rig.values, nullptr, req, flat.data(), 64));
    for (float x : flat)
        CHECK (std::fabs (x) < 1.0e-6f);
}

TEST (own_lfo_display_free_running_cursor_moves_and_arguments_are_checked)
{
    LfoRig rig ([&] (IEngine& e, ParamValues& v)
    {
        both (e, v, "lfo1.rate", 2.0f);
        both (e, v, "lfo1.sync_type", 1.0f); // free running
    });
    rig.note();
    float lo = 2.0f, hi = -1.0f;
    for (int b = 0; b < 100; ++b)
    {
        const float x = rig.block().lfoPhase[0];
        lo = std::min (lo, x);
        hi = std::max (hi, x);
    }
    CHECK (lo < 0.2f && hi > 0.8f); // the cursor of a free running LFO runs round (it stayed at 0 before the readout read the clock)
    CHECK (LfoDisplay::cursorX (-1.0f) == -1.0f);
    CHECK (LfoDisplay::cursorX (NAN) == -1.0f);
    CHECK (LfoDisplay::cursorX (0.4f) == 0.4f);
    float y[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    LfoDisplay::Request bad;
    bad.lfo = 11;
    CHECK (!LfoDisplay::compute (ParamValues::defaults(), nullptr, bad, y, 8));
    CHECK (y[0] == 0.0f);
    bad.lfo = 1;
    CHECK (!LfoDisplay::compute (ParamValues::defaults(), nullptr, bad, nullptr, 8));
    CHECK (!LfoDisplay::compute (ParamValues::defaults(), nullptr, bad, y, 1));
    ParamValues nanv = ParamValues::defaults();
    nanv.plain[indexOfKey ("lfo1.rate")] = NAN;
    nanv.plain[indexOfKey ("lfo1.shape")] = INFINITY;
    CHECK (LfoDisplay::compute (nanv, nullptr, bad, y, 8));
    for (float x : y)
        CHECK (std::isfinite (x));
}

// the live read-out has one LFO position per sounding voice (newest first): the cursors of the LFO display, one for every voice
TEST (own_lfo_read_out_has_a_cursor_per_voice)
{
    LfoRig rig ([&] (IEngine& e, ParamValues& v)
    {
        both (e, v, "lfo1.rate", 1.0f);
        both (e, v, "lfo1.sync_type", 0.0f); // retrigger: every voice starts its own cycle
    });
    LiveValues live;
    CHECK (rig.e->copyReadout (live));
    CHECK (live.numLfoVoices == 0);
    rig.note();
    for (int b = 0; b < 60; ++b)
        rig.block();
    Event on2 = sbtest::noteOn (0, 64, 0.8f);
    rig.e->process (&on2, 1, rig.l.data(), rig.r.data(), 256);
    live = rig.block();
    CHECK (live.numLfoVoices == 2);
    CHECK (live.lfoVoicePhase[0][0] >= 0.0f && live.lfoVoicePhase[0][1] > live.lfoVoicePhase[0][0] + 0.05f); // the older voice is further into its cycle
    CHECK (live.lfoVoicePhase[0][2] < 0.0f && live.lfoVoicePhase[0][7] < 0.0f);                              // no third voice
    CHECK (live.lfoVoicePhase[0][0] == live.lfoPhase[0]);                                                     // the newest voice is the one that is heard
}

// the live read-out has the fine stage of every envelope in every sounding voice (newest first): the running dots of the envelope display, one for every voice
TEST (own_env_read_out_has_a_stage_per_voice)
{
    LfoRig rig ([&] (IEngine& e, ParamValues& v)
    {
        both (e, v, "env1.attack", 5.0f);
        both (e, v, "env1.decay", 2000.0f);
        both (e, v, "env1.sustain", 50.0f);
    });
    LiveValues live;
    CHECK (rig.e->copyReadout (live));
    CHECK (live.envVoiceStage[0][0] == 0.0f && live.envVoiceStage[0][1] == 0.0f);   // no voice: idle everywhere
    rig.note();
    for (int b = 0; b < 40; ++b)
        rig.block();
    Event on2 = sbtest::noteOn (0, 64, 0.8f);
    rig.e->process (&on2, 1, rig.l.data(), rig.r.data(), 256);
    live = rig.block();
    CHECK (live.envVoiceStage[0][0] >= 2.0f && live.envVoiceStage[0][0] <= 5.0f);   // the newest voice is in attack .. sustain
    CHECK (live.envVoiceStage[0][1] == 4.0f && live.envVoiceFraction[0][1] > 0.0f && live.envVoiceFraction[0][1] < 1.0f);   // the older voice is somewhere in its long decay
    CHECK (live.envVoiceStage[0][2] == 0.0f && live.envVoiceStage[0][7] == 0.0f);   // no third voice
    CHECK (live.envVoiceStage[0][0] == live.envFineStage[0] && live.envVoiceFraction[0][0] == live.envFineFraction[0]);   // the newest voice is the one that is heard
    CHECK (live.envVoiceStage[5][1] >= 1.0f);                                          // every envelope is read out, not only the first
}

// ---- the oscillator cycle ---------------------------------------------------------------------------------------------------------------------------------

namespace
{
std::vector<float> cycleOf (OscCycle& c, const ParamValues& v, const OscCycle::Request& req = {}, OscCycle::Kind* kind = nullptr)
{
    std::vector<float> y (OscCycle::kSamples);
    const OscCycle::Kind k = c.compute (v, req, y.data());
    if (kind != nullptr)
        *kind = k;
    return y;
}

ParamValues oscPatch (int table, float frame, int warpType = 0, float warpAmount = 0.0f)
{
    ParamValues v = ParamValues::defaults();
    setv (v, "osc1.on", 1.0f);
    setv (v, "osc1.type", 0.0f);
    setv (v, "osc1.wavetable", static_cast<float> (table));
    setv (v, "osc1.frame", frame);
    setv (v, "osc1.warp.type", static_cast<float> (warpType));
    setv (v, "osc1.warp.amount", warpAmount);
    return v;
}

float peak (const std::vector<float>& y) { float p = 0.0f; for (float x : y) p = std::max (p, std::fabs (x)); return p; }

/** Normalised correlation of two cycles at the best circular shift within +-maxLag; the shift is returned. */
float bestCorrelation (const std::vector<float>& a, const std::vector<float>& b, int maxLag, int& lagOut)
{
    const int n = static_cast<int> (a.size());
    double ea = 0.0, eb = 0.0;
    for (int i = 0; i < n; ++i)
        ea += static_cast<double> (a[static_cast<size_t> (i)]) * a[static_cast<size_t> (i)], eb += static_cast<double> (b[static_cast<size_t> (i)]) * b[static_cast<size_t> (i)];
    double best = -2.0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i)
            s += static_cast<double> (a[static_cast<size_t> (i)]) * b[static_cast<size_t> ((i + lag + n) % n)];
        const double c = s / std::sqrt (std::max (ea * eb, 1.0e-30));
        if (c > best)
            best = c, lagOut = lag;
    }
    return static_cast<float> (best);
}
} // namespace

TEST (own_osc_cycle_is_the_wavetable_cycle_and_follows_frame_and_warp)
{
    OscCycle oc;
    OscCycle::Kind kind;
    // a built-in single frame table: the saw. The cycle is the saw (a rising ramp that falls at the end), phase 0 at the start.
    const auto saw = cycleOf (oc, oscPatch (kWavetableSaw, 0.0f), {}, &kind);
    CHECK (kind == OscCycle::Kind::wavetable);
    CHECK (peak (saw) > 0.7f && peak (saw) < 1.3f);
    int lag = 0;
    std::vector<float> ideal (OscCycle::kSamples);
    for (int i = 0; i < OscCycle::kSamples; ++i)
        ideal[static_cast<size_t> (i)] = 2.0f * static_cast<float> (i) / OscCycle::kSamples - 1.0f; // the saw of the table is rising, up to its sign
    float c = bestCorrelation (ideal, saw, 8, lag);
    std::printf ("    saw: correlation with an ideal ramp %.4f at shift %d\n", static_cast<double> (c), lag);
    CHECK (std::fabs (c) > 0.97f);
    CHECK (std::abs (lag) <= 1);
    // determinism and no state between calls
    CHECK (cycleOf (oc, oscPatch (kWavetableSaw, 0.0f)) == saw);
    OscCycle other;
    CHECK (cycleOf (other, oscPatch (kWavetableSaw, 0.0f)) == saw);
    // a multi frame table: the frame position moves the picture; the live (modulated) value is the same as the knob at that value
    int basic = -1;   // the first built-in table whose first and last frame differ
    for (int t = 0; t < kNumBuiltinWavetables && basic < 0; ++t)
        if (t != kWavetablePresetTable && cycleOf (oc, oscPatch (t, 0.0f)) != cycleOf (oc, oscPatch (t, 100.0f)))
            basic = t;
    CHECK (basic >= 0);
    const auto f0 = cycleOf (oc, oscPatch (basic, 0.0f)), f1 = cycleOf (oc, oscPatch (basic, 100.0f)), f5 = cycleOf (oc, oscPatch (basic, 50.0f));
    CHECK (f0 != f1);
    CHECK (f5 != f0 && f5 != f1);
    OscCycle::Request live;
    live.framePercent = 100.0f;
    CHECK (cycleOf (oc, oscPatch (basic, 0.0f), live) == f1);
    // warp: a warp with amount 0 is the cycle itself (the oversampled path's delay is taken out); a warp with an amount changes it
    for (int warp : { 4 /*Bend*/, 1 /*Sync*/ })
    {
        const auto w0 = cycleOf (oc, oscPatch (basic, 30.0f, warp, 0.0f));
        const auto plain = cycleOf (oc, oscPatch (basic, 30.0f));
        int l = 0;
        const float cc = bestCorrelation (plain, w0, 8, l);
        const auto w1 = cycleOf (oc, oscPatch (basic, 30.0f, warp, 70.0f));
        std::printf ("    warp %d: amount 0 vs none: correlation %.5f at shift %d; amount 70 differs: %d\n", warp, static_cast<double> (cc), l, w1 != plain);
        CHECK (cc > 0.98f); // the oversampled path band-limits a little differently (4x, 128 taps)
        CHECK (l == 0);
        CHECK (w1 != plain);
        OscCycle::Request liveAmount;
        liveAmount.warpAmountPercent[0] = 70.0f;
        CHECK (cycleOf (oc, oscPatch (basic, 30.0f, warp, 0.0f), liveAmount) == w1);
    }
    // other kinds and bad input
    ParamValues off = oscPatch (basic, 0.0f);
    setv (off, "osc1.on", 0.0f);
    cycleOf (oc, off, {}, &kind);
    CHECK (kind == OscCycle::Kind::off);
    ParamValues sample = oscPatch (basic, 0.0f);
    setv (sample, "osc1.type", 1.0f);
    const auto sy = cycleOf (oc, sample, {}, &kind);
    CHECK (kind == OscCycle::Kind::other);
    CHECK (peak (sy) == 0.0f);
    OscCycle::Request badOsc;
    badOsc.osc = 7;
    cycleOf (oc, oscPatch (basic, 0.0f), badOsc, &kind);
    CHECK (kind == OscCycle::Kind::other);
    ParamValues nanv = oscPatch (basic, 0.0f);
    nanv.plain[indexOfKey ("osc1.frame")] = NAN;
    const auto ny = cycleOf (oc, nanv, {}, &kind);
    for (float x : ny)
        CHECK (std::isfinite (x));
    int idx[4];
    OscCycle::readoutIndices (2, idx);
    CHECK (idx[0] == indexOfKey ("osc2.frame") && idx[1] == indexOfKey ("osc2.warp.amount") && idx[2] == indexOfKey ("osc2.warp2.amount") && idx[3] >= 0);
    OscCycle::readoutIndices (9, idx);
    CHECK (idx[0] == -1);
}

TEST (own_osc_cycle_agrees_with_what_the_engine_plays)
{
    // The engine, oscillator 1 only, a held note whose period is one display cycle (the engine at 56320 Hz, note 45 = 110 Hz = 512 samples): its output is the cycle
    // up to the gain of the output stage and a delay of the voice; the cycle of OscCycle must be the same shape.
    for (int warp : { 0, 4 })
    {
        const double rate = 512.0 * 110.0;
        auto e = createOwnEngine();
        e->prepare (rate, 256, qualityForTier (Tier::high));
        ParamValues v = oscPatch (2, 35.0f, warp, warp != 0 ? 60.0f : 0.0f);
        set (*e, "osc1.on", 1.0f);
        set (*e, "osc1.wavetable", 2.0f);
        set (*e, "osc1.frame", 35.0f);
        set (*e, "osc1.warp.type", static_cast<float> (warp));
        set (*e, "osc1.warp.amount", warp != 0 ? 60.0f : 0.0f);
        set (*e, "osc2.on", 0.0f);
        set (*e, "osc3.on", 0.0f);
        set (*e, "filter1.on", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "env1.sustain", 100.0f);
        std::vector<float> l (256), r (256), all;
        Event on = sbtest::noteOn (0, 45, 1.0f);
        e->process (&on, 1, l.data(), r.data(), 256);
        all.insert (all.end(), l.begin(), l.end());
        for (int b = 0; b < 40; ++b)
        {
            e->process (nullptr, 0, l.data(), r.data(), 256);
            all.insert (all.end(), l.begin(), l.end());
        }
        std::vector<float> engineCycle (all.end() - 512 - 1024, all.end() - 1024);
        OscCycle oc;
        const auto cyc = cycleOf (oc, v);
        int lag = 0;
        const float c = bestCorrelation (cyc, engineCycle, 256, lag);
        std::printf ("    warp %d: OscCycle against the engine's output: correlation %.4f (shift %d)\n", warp, static_cast<double> (c), lag);
        CHECK (c > 0.98f);
    }
}

// ---- EnvDisplay: the curve of the own engine's envelope (the Play page of the MPC overlay) -------------------------------------------------------------------------------------

/** The engine's envelope run in steps of 32 samples (its control period) for `seconds`; the value at the end. */
float runEnv (own::Envelope& e, const own::EnvSettings& s, double seconds)
{
    const double dt = 32.0 / 48000.0;
    float v = 0.0f;
    for (double t = 0.0; t + dt * 0.5 < seconds; t += dt)
        v = e.advance (s, dt);
    return v;
}

TEST (own_env_display_follows_the_engine_envelope_with_curvature)
{
    struct Case { float delay, attack, hold, decay, sustain, release, ac, dc, rc; };
    const Case cases[] = { { 0, 50, 0, 400, 60, 300, 0, 0, 0 }, { 120, 800, 200, 1500, 25, 2500, 8, -6, 12 }, { 0, 5, 0, 4000, 0, 100, -10, 14, -3 }, { 300, 2000, 1000, 500, 100, 800, 20, -20, 5 } };
    for (int env = 1; env <= 6; ++env)
        for (const Case& c : cases)
        {
            ParamValues v = ParamValues::defaults();
            const std::string k = "env" + std::to_string (env) + ".";
            setv (v, (k + "delay").c_str(), c.delay); setv (v, (k + "attack").c_str(), c.attack); setv (v, (k + "hold").c_str(), c.hold);
            setv (v, (k + "decay").c_str(), c.decay); setv (v, (k + "sustain").c_str(), c.sustain); setv (v, (k + "release").c_str(), c.release);
            setv (v, (k + "attack_curve").c_str(), c.ac); setv (v, (k + "decay_curve").c_str(), c.dc); setv (v, (k + "release_curve").c_str(), c.rc);
            setv (v, (k + "units").c_str(), 0.0f);
            EnvDisplay::Request req;
            req.env = env;
            EnvDisplay::Geometry g;
            std::vector<float> y (400);
            CHECK (EnvDisplay::compute (v, req, g, y.data(), 400));
            CHECK (g.end[5] == 1.0);
            for (int i = 0; i < 5; ++i)
                CHECK (g.end[i] <= g.end[i + 1] + 1e-12);
            // the engine's own envelope, sampled inside every stage
            own::EnvSettings s;
            s.delay = c.delay * 0.001f; s.attack = c.attack * 0.001f; s.hold = c.hold * 0.001f; s.decay = c.decay * 0.001f; s.sustain = c.sustain * 0.01f; s.release = c.release * 0.001f;
            s.attackCurve = -c.ac; s.decayCurve = c.dc; s.releaseCurve = c.rc;
            for (double f : { 0.2, 0.5, 0.8 })
            {
                if (c.attack >= 100)   // (a shorter attack is not resolved by the engine's 32 sample control period)
                {
                    own::Envelope e; e.noteOn();
                    const float eng = runEnv (e, s, s.delay + f * s.attack);
                    CHECK (std::fabs (eng - at (y, static_cast<float> (g.end[0] + f * (g.end[1] - g.end[0])))) < 0.03f);
                }
                if (c.decay > 0)
                {
                    own::Envelope e; e.noteOn();
                    const float eng = runEnv (e, s, s.delay + s.attack + s.hold + f * s.decay);
                    CHECK (std::fabs (eng - at (y, static_cast<float> (g.end[2] + f * (g.end[3] - g.end[2])))) < 0.03f);
                }
                if (c.release > 0)
                {
                    own::Envelope e; e.noteOn();
                    runEnv (e, s, s.delay + s.attack + s.hold + s.decay + 0.01);
                    e.noteOff();
                    const float eng = runEnv (e, s, f * s.release);
                    CHECK (std::fabs (eng - at (y, static_cast<float> (g.end[4] + f * (g.end[5] - g.end[4])))) < 0.03f);
                }
            }
            CHECK (at (y, 0.0f) == 0.0f);
            double x = 0, lvl = 0;
            CHECK (EnvDisplay::dotFor (g, y.data(), 400, 4.0f, 0.5f, x, lvl));
            CHECK (x > g.end[2] && x < g.end[3]);
            CHECK (! EnvDisplay::dotFor (g, y.data(), 400, 0.0f, 0.5f, x, lvl));
            CHECK (! EnvDisplay::dotFor (g, y.data(), 400, 7.0f, 0.5f, x, lvl));
        }
}

TEST (own_env_display_beats_units_follow_the_tempo_and_arguments_are_checked)
{
    ParamValues v = ParamValues::defaults();
    setv (v, "env2.units", 1.0f); setv (v, "env2.attack_beats", 1.0f); setv (v, "env2.decay_beats", 1.0f); setv (v, "env2.release_beats", 1.0f);
    setv (v, "env2.sustain", 50.0f); setv (v, "env2.hold_beats", 0.0f); setv (v, "env2.delay_beats", 0.0f);
    EnvDisplay::Request r;
    r.env = 2;
    EnvDisplay::Geometry g60, g120;
    std::vector<float> y (64);
    r.bpm = 60.0;
    CHECK (EnvDisplay::compute (v, r, g60, y.data(), 64));
    r.bpm = 120.0;
    CHECK (EnvDisplay::compute (v, r, g120, y.data(), 64));
    // the proportions between the stages do not depend on the tempo (all three stages are beats); the sustain segment is the only thing that has a minimum in seconds
    CHECK (std::fabs (g60.end[1] - g120.end[1]) < 0.1);
    EnvDisplay::Geometry g;
    CHECK (! EnvDisplay::compute (v, EnvDisplay::Request { 0, 120.0 }, g, y.data(), 64));
    CHECK (! EnvDisplay::compute (v, EnvDisplay::Request { 7, 120.0 }, g, y.data(), 64));
    CHECK (! EnvDisplay::compute (v, r, g, nullptr, 64));
    CHECK (! EnvDisplay::compute (v, r, g, y.data(), 1));
}

// ---- the level meters of the mixer (LiveValues::stripPeak) ------------------------------------------------------------------------------------------------

// the strips measure what their source plays: the oscillator A of a note shows on strip A and on the master, a strip of a source that is off stays at zero, the sub appears when it is switched on, the meters fall
// back after the note, and the read-out is published without a lock (the audio thread writes atomics, the display thread reads them while it keeps running)
TEST (own_mixer_strip_peaks_follow_the_sources_and_decay)
{
    LfoRig rig ([&] (IEngine& e, ParamValues& v)
    {
        both (e, v, "osc1.on", 1.0f);
        both (e, v, "osc1.level", 60.0f);
        both (e, v, "osc2.on", 0.0f);
        both (e, v, "osc3.on", 0.0f);
        both (e, v, "sub.on", 0.0f);
        both (e, v, "noise.on", 0.0f);
        both (e, v, "env1.attack", 2.0f);
        both (e, v, "env1.sustain", 100.0f);
    });
    rig.meters = true;
    LiveValues live;
    CHECK (rig.e->copyReadout (live));
    for (int s = 0; s < LiveValues::kMaxStrips; ++s)
        CHECK (live.stripPeak[s] == 0.0f);   // nothing sounds: every meter is at the floor
    rig.note();
    for (int b = 0; b < 40; ++b)
        live = rig.block();
    CHECK (live.stripPeak[1] > 0.3f && live.stripPeak[1] <= 1.0f);   // strip A
    CHECK (live.stripPeak[2] == 0.0f && live.stripPeak[3] == 0.0f);  // B and C are off
    CHECK (live.stripPeak[0] == 0.0f && live.stripPeak[4] == 0.0f);  // Sub and Noise are off
    CHECK (live.stripPeak[5] > 0.3f);                                 // the oscillator goes through filter 1 (the default destination)
    CHECK (live.stripPeak[9] > 0.3f && live.stripPeak[9] <= 1.0f);   // the master
    CHECK (live.stripPeak[7] == 0.0f && live.stripPeak[8] == 0.0f);  // nothing is sent to the buses
    const float held = live.stripPeak[1];
    // a louder oscillator reads higher
    {
        LfoRig loud ([&] (IEngine& e, ParamValues& v)
        {
            both (e, v, "osc1.on", 1.0f);
            both (e, v, "osc1.level", 100.0f);
            both (e, v, "osc2.on", 0.0f);
            both (e, v, "osc3.on", 0.0f);
            both (e, v, "env1.attack", 2.0f);
            both (e, v, "env1.sustain", 100.0f);
        });
        loud.meters = true;
        loud.note();
        LiveValues l2;
        for (int b = 0; b < 40; ++b)
            l2 = loud.block();
        CHECK (l2.stripPeak[1] > held);
    }
    // the sub is switched on: its strip lights up
    set (*rig.e, "sub.on", 1.0f);
    set (*rig.e, "sub.level", 80.0f);
    rig.note();
    for (int b = 0; b < 40; ++b)
        live = rig.block();
    CHECK (live.stripPeak[0] > 0.2f);
    // the sends of the oscillator reach the buses
    set (*rig.e, "osc1.bus1_send", 100.0f);
    for (int b = 0; b < 40; ++b)
        live = rig.block();
    CHECK (live.stripPeak[7] > 0.2f && live.stripPeak[8] == 0.0f);
    // all notes off: the meters fall back to the floor (release 300 ms)
    Event off = sbtest::noteOff (0, 60);
    rig.e->process (&off, 1, rig.l.data(), rig.r.data(), 256);
    Event off2 = sbtest::noteOff (0, 64);
    rig.e->process (&off2, 1, rig.l.data(), rig.r.data(), 256);
    for (int b = 0; b < 400; ++b)
        live = rig.block();
    CHECK (live.stripPeak[9] < 0.05f && live.stripPeak[1] < 0.05f);
}

// The source strips are measured only while the display asks (IEngine::requestMeters). Against an engine that is asked every block (the behaviour before the request existed): the audio of both is
// identical to the bit, the bus and master strips are the same floats always, the source strips are the same floats from the moment the display asks (a steady chord, so no louder past is
// forgotten), the first block after the request already carries real values, and they fall back to 0 once the requests stop.
TEST (own_source_meters_are_measured_only_while_asked_and_change_no_output)
{
    const auto setup = [] (IEngine& e, ParamValues& v)
    {
        both (e, v, "osc1.on", 1.0f);
        both (e, v, "osc1.level", 70.0f);
        both (e, v, "osc2.on", 1.0f);
        both (e, v, "osc2.level", 50.0f);
        both (e, v, "osc3.on", 0.0f);
        both (e, v, "sub.on", 1.0f);
        both (e, v, "sub.level", 60.0f);
        both (e, v, "noise.on", 1.0f);
        both (e, v, "noise.level", 20.0f);
        both (e, v, "osc1.bus1_send", 60.0f);
        both (e, v, "osc2.bus2_send", 40.0f);
        both (e, v, "env1.attack", 2.0f);
        both (e, v, "env1.sustain", 100.0f);
    };
    LfoRig always (setup), onDemand (setup);
    always.meters = true;
    const int notes[3] = { 60, 64, 67 };
    for (int n = 0; n < 3; ++n)
    {
        Event on = sbtest::noteOn (0, notes[n], 0.8f);
        always.e->requestMeters();
        always.e->process (&on, 1, always.l.data(), always.r.data(), 256);
        onDemand.e->process (&on, 1, onDemand.l.data(), onDemand.r.data(), 256);
    }
    auto bits = [] (float x) { uint32_t u; std::memcpy (&u, &x, 4); return u; };
    long long audioBad = 0, stripBad = 0;
    LiveValues a, d;
    const auto step = [&] (bool ask)
    {
        onDemand.meters = ask;
        a = always.block();
        d = onDemand.block();
        for (size_t i = 0; i < always.l.size(); ++i)
            audioBad += bits (always.l[i]) != bits (onDemand.l[i]) || bits (always.r[i]) != bits (onDemand.r[i]);
    };
    // 60 blocks nobody asks: the source strips read 0, the bus and master strips equal the reference
    for (int b = 0; b < 60; ++b)
    {
        step (false);
        for (int s = 0; s < 7; ++s)
            CHECK (d.stripPeak[s] == 0.0f);
        for (int s = 7; s < 10; ++s)
            stripBad += bits (a.stripPeak[s]) != bits (d.stripPeak[s]);
    }
    CHECK (a.stripPeak[1] > 0.2f && a.stripPeak[0] > 0.1f && a.stripPeak[4] > 0.05f);   // (the reference does measure the sources)
    // the display asks: the first block already has real values (not the stale zeros), and the strips follow the reference
    step (true);
    for (int s = 0; s < 7; ++s)
        if (a.stripPeak[s] > 0.0f)
            CHECK (d.stripPeak[s] > 0.9f * a.stripPeak[s]);
    for (int b = 0; b < 300; ++b)
    {
        step (true);
        if (b >= 150)   // (the memory of the release, 300 ms, is gone: the floats are the reference's)
            for (int s = 0; s < 10; ++s)
                stripBad += bits (a.stripPeak[s]) != bits (d.stripPeak[s]);
    }
    CHECK_EQ (audioBad, 0);
    CHECK_EQ (stripBad, 0);
    // the requests stop: the hold runs out and the source strips go to 0 while the output keeps being identical
    for (int b = 0; b < 400; ++b)
        step (false);
    for (int s = 0; s < 7; ++s)
        CHECK (d.stripPeak[s] == 0.0f);
    CHECK_EQ (audioBad, 0);
    std::printf ("    %lld output samples and %lld meter values differ from the engine that is always asked\n", audioBad, stripBad);
}

// the voice control: each of the newest voices reports the step of the voice control it plays (note number - 1, modulo the length): the highlighted column of the Voice page
TEST (own_voice_control_read_out_has_the_step_of_every_voice)
{
    LfoRig rig ([&] (IEngine& e, ParamValues& v) { both (e, v, "voicectl.length", 3.0f); });
    LiveValues live;
    CHECK (rig.e->copyReadout (live));
    CHECK (live.voiceStep[0] < 0.0f);   // no voice: nothing is lit
    const int keys[4] = { 60, 62, 64, 65 };
    for (int k = 0; k < 4; ++k)
    {
        Event on = sbtest::noteOn (0, keys[k], 0.8f);
        rig.e->process (&on, 1, rig.l.data(), rig.r.data(), 256);
        live = rig.block();
    }
    // four notes with a length of 3: the steps 0, 1, 2, 0; newest first
    CHECK (live.voiceStep[0] == 0.0f && live.voiceStep[1] == 2.0f && live.voiceStep[2] == 1.0f && live.voiceStep[3] == 0.0f);
    CHECK (live.voiceStep[4] < 0.0f && live.voiceStep[7] < 0.0f);
}
