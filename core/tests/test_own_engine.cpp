// SynthyBoy - own engine tests: the Serum-class engine (voices, oscillators, filters, envelopes, LFOs, matrix) through IEngine only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "voice/filter_unit.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
std::unique_ptr<IEngine> make (double sr = 48000.0)
{
    auto e = createOwnEngine();
    e->setParam (kParamMasterPolyphony, 32.0f);
    e->prepare (sr, 256, qualityForTier (Tier::high));
    return e;
}

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

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

struct Out
{
    std::vector<float> l, r;
};

Out renderNote (IEngine& e, int key, float vel, double noteSeconds, double totalSeconds, double sr = 48000.0)
{
    const size_t total = static_cast<size_t> (totalSeconds * sr), offAt = static_cast<size_t> (noteSeconds * sr);
    Out o;
    o.l.assign (total, 0.0f);
    o.r.assign (total, 0.0f);
    size_t pos = 0;
    while (pos < total)
    {
        const size_t n = std::min<size_t> (256, total - pos);
        Event ev[2];
        int ne = 0;
        if (pos == 0)
            ev[ne++] = sbtest::noteOn (0, key, vel);
        if (offAt >= pos && offAt < pos + n)
            ev[ne++] = sbtest::noteOff (static_cast<int> (offAt - pos), key);
        e.process (ev, ne, o.l.data() + pos, o.r.data() + pos, static_cast<int> (n));
        pos += n;
    }
    return o;
}

double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    b = std::min (b, x.size());
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return b > a ? std::sqrt (s / static_cast<double> (b - a)) : 0.0;
}
} // namespace

// The source ids of the engine are the choice values of modNN.source: the order of the table must not change under it.
TEST (own_source_ids_match_the_table)
{
    const char* expected[] = { "env1", "env2", "env3", "env4", "env5", "env6", "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "lfo7", "lfo8", "rlfo1", "rlfo2",
                               "rlfo3", "rlfo4", "macro1", "macro2", "macro3", "macro4", "velocity", "note", "aftertouch", "pitch_wheel", "mod_wheel", "random",
                               "macro5", "macro6", "macro7", "macro8", "lfo9", "lfo10", "lift", "slide", "oct_note", "poly_aftertouch", "stereo", "voice_index",
                               "voice_mod1", "voice_mod2", "osc_a", "osc_b", "osc_c", "noise", "sub", "filter1", "filter2", "active_voices", "note_on_alt1",
                               "note_on_alt2", "note_on_rand2", "note_on_rand_discrete", "release_velocity", "expression_x", "fixed" };
    CHECK_EQ (modSourceCount(), sizeof expected / sizeof expected[0]);
    for (size_t i = 0; i < modSourceCount() && i < sizeof expected / sizeof expected[0]; ++i)
        CHECK (std::string (modSourceTable()[i].key) == expected[i]);
}

// Random patches, matrix wiring and notes must never produce NaN, Inf or a runaway level.
TEST (own_engine_is_finite_and_bounded_under_random_patches)
{
    for (int seed = 1; seed <= 5; ++seed)
    {
        auto e = make();
        sbtest::Rng rng (seed <= 3 ? static_cast<uint64_t> (seed) * 7919 : static_cast<uint64_t> (seed + 6));
        static float l[128], r[128];
        std::vector<std::string> history;
        bool ok = true;
        for (int block = 0; block < 1500 && ok; ++block)
        {
            if (seed > 3 && block % 100 == 50)
            {
                Quality q = qualityForTier (rng.u32() % 2 == 0 ? Tier::low : Tier::high);
                q.maxVoices = 1 + static_cast<int> (rng.u32() % 32);
                e->setQuality (q);
            }
            for (int k = 0; k < 8; ++k)
            {
                const auto& info = paramTable()[rng.u32() % paramCount()];
                const float v = info.min + static_cast<float> (rng.uniform()) * (info.max - info.min);
                e->setParam (info.id, v);
                history.push_back (std::string (info.key) + "=" + std::to_string (v));
                if (history.size() > 64)
                    history.erase (history.begin());
            }
            for (int k = 0; k < 3; ++k)
            {
                const auto slot = modSlotParams (static_cast<int> (rng.u32() % kNumModSlots));
                e->setParam (paramTable()[slot.source].id, static_cast<float> (rng.u32() % (1 + modSourceCount())));
                e->setParam (paramTable()[slot.destination].id, static_cast<float> (rng.u32() % (1 + modDestinationCount())));
                e->setParam (paramTable()[slot.amount].id, -100.0f + 200.0f * static_cast<float> (rng.uniform()));
            }
            Event ev[3];
            int n = 0;
            if (block % 3 == 0)
                ev[n++] = sbtest::noteOn (static_cast<int> (rng.u32() % 100), 24 + static_cast<int> (rng.u32() % 80), 0.2f + 0.8f * static_cast<float> (rng.uniform()));
            if (block % 5 == 0)
                ev[n++] = sbtest::noteOff (120, 24 + static_cast<int> (rng.u32() % 80));
            std::stable_sort (ev, ev + n, [] (const Event& a, const Event& b) { return a.frame < b.frame; });
            e->process (ev, n, l, r, 128);
            for (int i = 0; i < 128; ++i)
                if (!std::isfinite (l[i]) || !std::isfinite (r[i]) || std::fabs (l[i]) > 100.0f || std::fabs (r[i]) > 100.0f)
                {
                    ok = false;
                    std::printf ("    seed %d block %d sample %d value L %g R %g; last parameter writes:\n", seed, block, i, static_cast<double> (l[i]), static_cast<double> (r[i]));
                    for (const auto& h : history)
                        std::printf ("      %s\n", h.c_str());
                    break;
                }
        }
        CHECK (ok);
    }
}


// Every filter model and style of the table, at the extreme settings, on loud noise: bounded and finite (the unit's guard; the collection itself runs
// away for some types at resonance and drive 100 %, see the report).
TEST (own_filters_stay_bounded)
{
    for (int model = 0; model <= 26; ++model)
        for (int style = 0; style < 12; ++style)
            for (const float cutoff : { 20.0f, 300.0f, 5000.0f, 20000.0f })
                for (const float res : { 0.0f, 1.0f })
                {
                    own::FilterUnit f;
                    f.prepare (48000.0);
                    own::FilterSettings s;
                    s.model = model;
                    s.style = style;
                    s.cutoffHz = cutoff;
                    s.resonance = res;
                    s.drive = 1.0f;
                    s.blend = 0.5f;
                    s.var = 1.0f;
                    f.set (s);
                    f.reset();
                    own::Rng rng;
                    rng.seed (static_cast<uint32_t> (model * 100 + style));
                    float in[32], out[32], dummy[32];
                    float peak = 0.0f;
                    for (int block = 0; block < 400; ++block)
                    {
                        for (auto& x : in)
                            x = 1.4f * rng.bi();
                        if (block % 50 == 0)
                        {
                            s.cutoffHz = 20.0f * std::pow (1000.0f, static_cast<float> (rng.uni()));
                            f.set (s);
                        }
                        f.process (in, in, out, dummy, 32);
                        for (const float v : out)
                            peak = std::max (peak, std::isfinite (v) ? std::fabs (v) : 1.0e30f);
                    }
                    if (!(peak <= 16.01f))
                    {
                        std::printf ("    model %d style %d cutoff %g res %g: peak %g\n", model, style, static_cast<double> (cutoff), static_cast<double> (res), static_cast<double> (peak));
                        CHECK (peak <= 16.01f);
                    }
                }
}

// The sequence of test_rt_alloc.cpp (random parameters, matrix wiring, quality changes), checked for NaN with the cause printed.
TEST (own_engine_rt_sequence_is_finite)
{
    for (const Tier tier : { Tier::low, Tier::high })
    {
        auto e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 32.0f);
        e->prepare (48000.0, 128, qualityForTier (tier));
        static float left[128], right[128];
        sbtest::Rng rng (tier == Tier::low ? 11 : 12);
        std::vector<std::string> history;
        Event events[8];
        bool finite = true;
        for (int block = 0; block < 1500 && finite; ++block)
        {
            for (int k = 0; k < 6; ++k)
            {
                const auto& info = paramTable()[rng.u32() % paramCount()];
                const float v = info.min + static_cast<float> (rng.uniform()) * (info.max - info.min);
                e->setParam (info.id, v);
                history.push_back (std::string (info.key) + "=" + std::to_string (v));
            }
            for (int k = 0; k < 3; ++k)
            {
                const auto slot = modSlotParams (static_cast<int> (rng.u32() % kNumModSlots));
                e->setParam (paramTable()[slot.source].id, static_cast<float> (rng.u32() % (1 + modSourceCount())));
                e->setParam (paramTable()[slot.destination].id, static_cast<float> (rng.u32() % (1 + modDestinationCount())));
                e->setParam (paramTable()[slot.amount].id, -100.0f + 200.0f * static_cast<float> (rng.uniform()));
                history.push_back ("slot " + std::to_string (slot.source) + "/" + std::to_string (slot.destination));
            }
            if (block % 100 == 50)
            {
                Quality q = qualityForTier (rng.u32() % 2 == 0 ? Tier::low : Tier::high);
                q.maxVoices = 1 + static_cast<int> (rng.u32() % 32);
                e->setQuality (q);
            }
            int n = 0;
            if (block % 2 == 0)
                events[n++] = sbtest::noteOn (static_cast<int> (rng.u32() % 100), 30 + static_cast<int> (rng.u32() % 60), 0.3f + 0.7f * static_cast<float> (rng.uniform()));
            if (block % 3 == 0)
                events[n++] = sbtest::noteOff (120, 30 + static_cast<int> (rng.u32() % 60));
            std::stable_sort (events, events + n, [] (const Event& a, const Event& b) { return a.frame < b.frame; });
            e->process (events, n, left, right, 128);
            for (int i = 0; i < 128; ++i)
                if (!std::isfinite (left[i]) || !std::isfinite (right[i]))
                {
                    finite = false;
                    std::printf ("    tier %d block %d sample %d: %g %g\n", static_cast<int> (tier), block, i, static_cast<double> (left[i]), static_cast<double> (right[i]));
                    for (size_t h = history.size() > 200 ? history.size() - 200 : 0; h < history.size(); ++h)
                        std::printf ("      %s\n", history[h].c_str());
                    break;
                }
        }
        CHECK (finite);
    }
}

// The factory behaviour of the default patch: a saw at the pitch of the key, level within 1 dB of the measured Vital default (-24.5 dB rms at A4).
TEST (own_default_patch_pitch_and_level)
{
    auto e = make();
    const Out o = renderNote (*e, 69, 1.0f, 1.0, 1.2);
    const double db = 20.0 * std::log10 (rmsOf (o.l, 14400, 48000));
    CHECK_NEAR (db, -24.5, 1.0);
    // zero crossings of the low passed output give the pitch
    int crossings = 0;
    for (size_t i = 14401; i < 48000; ++i)
        if (o.l[i - 1] < 0.0f && o.l[i] >= 0.0f)
            ++crossings;
    CHECK_NEAR (crossings / (static_cast<double> (48000 - 14400) / 48000.0), 440.0, 1.5);
}

// The envelope 1 times: attack, release ends the voice exactly.
TEST (own_amp_envelope_times)
{
    auto e = make();
    set (*e, "env1.attack", 100.0f);
    set (*e, "env1.release", 300.0f);
    set (*e, "filter1.on", 0.0f);
    set (*e, "osc1.wavetable", 0.0f);
    const Out o = renderNote (*e, 69, 1.0f, 1.0, 2.0);
    // quadratic amplitude: the 100 ms attack reaches 25 % at 50 ms
    const double early = rmsOf (o.l, 0, 480), mid = rmsOf (o.l, 2200, 2600), full = rmsOf (o.l, 9600, 10000);
    CHECK (early < 0.15 * full);
    CHECK_NEAR (mid / full, 0.25, 0.06);
    CHECK_EQ (rmsOf (o.l, static_cast<size_t> (1.31 * 48000), static_cast<size_t> (2.0 * 48000)), 0.0);
    CHECK (rmsOf (o.l, static_cast<size_t> (1.1 * 48000), static_cast<size_t> (1.15 * 48000)) > 0.0);
    CHECK_EQ (e->activeVoices(), 0);
}

// Unison: two voices with detune spread to the two channels, level stays about equal.
TEST (own_unison_detunes_and_keeps_level)
{
    auto e = make();
    set (*e, "filter1.on", 0.0f);
    set (*e, "osc1.wavetable", 0.0f);
    const Out one = renderNote (*e, 69, 1.0f, 1.0, 1.2);
    auto u = make();
    set (*u, "filter1.on", 0.0f);
    set (*u, "osc1.wavetable", 0.0f);
    set (*u, "osc1.unison.voices", 4.0f);
    set (*u, "osc1.unison.detune", 40.0f);
    const Out four = renderNote (*u, 69, 1.0f, 1.0, 1.2);
    const double a = rmsOf (one.l, 14400, 48000), b = rmsOf (four.l, 14400, 48000);
    CHECK_NEAR (20.0 * std::log10 (b / a), 0.0, 1.5);
    CHECK (std::fabs (rmsOf (four.l, 14400, 48000) - rmsOf (four.r, 14400, 48000)) < 0.2 * a);
}

// A matrix slot: LFO 1 (square) onto the osc 1 level changes the level; amount 0 does not.
TEST (own_matrix_lfo_to_level)
{
    for (const float amount : { 0.0f, 100.0f })
    {
        auto e = make();
        set (*e, "filter1.on", 0.0f);
        set (*e, "osc1.wavetable", 0.0f);
        set (*e, "lfo1.shape", 4.0f);
        set (*e, "lfo1.rate", 2.0f);
        set (*e, "lfo1.smooth", 0.0f);
        set (*e, "mod01.source", srcChoice ("lfo1"));
        set (*e, "mod01.destination", dstChoice ("osc1.level"));
        set (*e, "mod01.amount", amount);
        const Out o = renderNote (*e, 69, 1.0f, 1.5, 1.6);
        const double q1 = rmsOf (o.l, 4800, 9600), q2 = rmsOf (o.l, 33600, 38400); // first half cycle / second half cycle of the 2 Hz LFO
        if (amount == 0.0f)
            CHECK_NEAR (q1 / q2, 1.0, 0.05);
        else
            CHECK (std::fabs (q1 / q2 - 1.0) > 0.3);
    }
}

// Block splits must not change the result (the control grid is on the absolute sample count).
TEST (own_output_does_not_depend_on_the_block_size)
{
    std::vector<float> ref;
    for (const int block : { 1, 7, 64, 256, 1000 })
    {
        auto e = make();
        set (*e, "osc1.unison.voices", 3.0f);
        set (*e, "lfo1.shape", 0.0f);
        set (*e, "mod01.source", srcChoice ("lfo1"));
        set (*e, "mod01.destination", dstChoice ("filter1.cutoff"));
        set (*e, "mod01.amount", 40.0f);
        const int total = 9600;
        std::vector<float> l (static_cast<size_t> (total)), r (static_cast<size_t> (total));
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (block, total - pos);
            Event ev[2];
            int ne = 0;
            if (pos <= 100 && pos + n > 100)
                ev[ne++] = sbtest::noteOn (100 - pos, 60);
            if (pos <= 5000 && pos + n > 5000)
                ev[ne++] = sbtest::noteOff (5000 - pos, 60);
            e->process (ev, ne, l.data() + pos, r.data() + pos, n);
            pos += n;
        }
        if (ref.empty())
            ref = l;
        else
            CHECK (ref == l);
    }
}
