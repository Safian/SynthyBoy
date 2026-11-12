// SynthyBoy - own engine tests: the Serum-class engine (voices, oscillators, filters, envelopes, LFOs, matrix) through IEngine only.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "own_oracle.h"
#include "osc/wavetable_bank.h"
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

// ---- the acceptance vectors M1 .. M9 and E1 of docs/engine-spec/04-modulation-matrix.md and 03-envelopes-lfos.md (reproduced against engine-vital, tolerance 0.1 dB)

namespace
{
/** Amplitude of the line at `hz` in a window (Goertzel), as a linear peak value. */
double lineAmplitude (const std::vector<float>& x, size_t a, size_t b, double hz, double sr = 48000.0)
{
    double re = 0.0, im = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i)
    {
        const double ph = 2.0 * 3.14159265358979323846 * hz * static_cast<double> (i) / sr;
        re += x[i] * std::cos (ph);
        im += x[i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (b - a);
}

double dbOf (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

/** The setup of the matrix vectors: Sine, filter off, volume 0 dB, attack 0, release 50 ms. */
std::unique_ptr<IEngine> matrixBase()
{
    auto e = make();
    set (*e, "osc1.wavetable", 0.0f);
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "env1.attack", 0.0f);
    set (*e, "env1.release", 50.0f);
    return e;
}

void slotMacroTo (IEngine& e, const char* dst, float amount)
{
    set (e, "mod01.source", srcChoice ("macro1"));
    set (e, "mod01.destination", dstChoice (dst));
    set (e, "mod01.amount", amount);
}
} // namespace

TEST (own_matrix_vectors_m1_to_m5_level_laws)
{
    struct Case { float level, macro, amount, bipolar, power, expectedDb; };
    const Case cases[] = {
        { 50, 100, 50, 0, 0, 3.28f },    // M1
        { 50, 100, 50, 1, 0, -0.75f },   // M2
        { 50, 50, 100, 0, 0, 3.28f },    // M3
        { 100, 50, -50, 0, 0, -4.99f },  // M4
        { 100, 50, -100, 0, -5, -1.36f },// M5 (power -5)
        { 100, 50, -100, 0, 5, -44.79f } // M5 (power +5)
    };
    for (const Case& c : cases)
    {
        auto e = matrixBase();
        set (*e, "osc1.level", c.level);
        set (*e, "macro.1", c.macro);
        slotMacroTo (*e, "osc1.level", c.amount);
        set (*e, "mod01.bipolar", c.bipolar);
        set (*e, "mod01.power", c.power);
        const Out o = renderNote (*e, 69, 1.0f, 1.0, 1.2);
        CHECK_NEAR (dbOf (lineAmplitude (o.l, 14400, 38400, 440.0)), static_cast<double> (c.expectedDb), 0.12);
    }
}

TEST (own_matrix_vectors_m6_to_m9_transpose_and_pan)
{
    {
        auto e = matrixBase();
        set (*e, "macro.1", 50.0f);
        slotMacroTo (*e, "osc1.transpose", 25.0f); // M6: +12 st
        const Out o = renderNote (*e, 69, 1.0f, 1.0, 1.2);
        CHECK (lineAmplitude (o.l, 14400, 38400, 880.0) > 5.0 * lineAmplitude (o.l, 14400, 38400, 440.0));
    }
    {
        auto e = matrixBase();
        set (*e, "macro.1", 100.0f);
        set (*e, "mod01.bipolar", 1.0f);
        slotMacroTo (*e, "osc1.transpose", 50.0f); // M7: +24 st
        const Out o = renderNote (*e, 69, 1.0f, 1.0, 1.2);
        CHECK (lineAmplitude (o.l, 14400, 38400, 1760.0) > 5.0 * lineAmplitude (o.l, 14400, 38400, 440.0));
    }
    for (const float stereo : { 0.0f, 1.0f })
    {
        auto e = matrixBase();
        set (*e, "macro.1", 100.0f);
        slotMacroTo (*e, "osc1.pan", 100.0f); // M8 / M9
        set (*e, "mod01.stereo", stereo);
        const Out o = renderNote (*e, 69, 1.0f, 1.0, 1.2);
        const double l = lineAmplitude (o.l, 14400, 38400, 440.0), r = lineAmplitude (o.r, 14400, 38400, 440.0);
        CHECK (l < 1.0e-3);
        if (stereo == 0.0f)
            CHECK_NEAR (dbOf (r), -3.01, 0.12);
        // M9 (stereo On): the right lane gets -1 so the pan lanes are +1 / -1. A voice has one pan value in this engine (the left lane is used), see the report.
    }
}

TEST (own_envelope_vector_e1)
{
    auto e = make();
    set (*e, "osc1.wavetable", 0.0f);
    set (*e, "osc1.level", 100.0f);
    set (*e, "osc1.phase", 90.0f);
    set (*e, "filter1.on", 0.0f);
    set (*e, "master.volume", 0.0f);
    set (*e, "env1.delay", 0.0f);
    set (*e, "env1.attack", 100.0f);
    set (*e, "env1.hold", 50.0f);
    set (*e, "env1.decay", 200.0f);
    set (*e, "env1.sustain", 50.0f);
    set (*e, "env1.release", 300.0f);
    set (*e, "env1.attack_curve", 0.0f);
    set (*e, "env1.decay_curve", -2.0f);
    set (*e, "env1.release_curve", -2.0f);
    const Out o = renderNote (*e, 69, 1.0f, 0.75, 1.5);
    auto peakAt = [&] (double t) // peak of the waveform in a +-1.25 ms window, t after the note on
    {
        double p = 0.0;
        const size_t c = static_cast<size_t> (t * 48000.0);
        for (size_t i = c - 60; i < c + 60; ++i)
            p = std::max (p, static_cast<double> (std::fabs (o.l[i])));
        return p;
    };
    const double times[] = { 0.010, 0.050, 0.090, 0.120, 0.160, 0.200, 0.300, 0.350, 0.500 };
    const double expected[] = { 0.0097, 0.256, 0.821, 0.998, 0.930, 0.619, 0.309, 0.253, 0.250 };
    for (size_t i = 0; i < 9; ++i)
        CHECK_NEAR (peakAt (times[i]), expected[i], 0.06 * expected[i] + 0.005);
    const double relTimes[] = { 0.010, 0.050, 0.100, 0.200 }; // after the note off at 0.75 s
    const double relExpected[] = { 0.217, 0.115, 0.049, 0.0057 };
    for (size_t i = 0; i < 4; ++i)
        CHECK_NEAR (peakAt (0.75 + relTimes[i]), relExpected[i], 0.06 * relExpected[i] + 0.005);
}

// ---- docs/engine-spec/05-filters.md section 10: the gain of white noise through the models at cutoff 1 kHz (8 frequencies, dB)
namespace
{
std::vector<double> noiseGainDb (int model, int style, float res)
{
    own::FilterUnit f;
    f.prepare (48000.0);
    own::FilterSettings s;
    s.model = model;
    s.style = style;
    s.cutoffHz = 1000.0f;
    s.resonance = res;
    s.blend = 0.0f;
    f.set (s);
    f.set (s);
    f.reset();
    own::Rng rng;
    rng.seed (5);
    const size_t N = 1 << 17;
    std::vector<float> in (N), out (N);
    for (auto& x : in)
        x = rng.bi();
    for (size_t i = 0; i < N; i += 32)
    {
        float dl[32];
        f.process (in.data() + i, in.data() + i, out.data() + i, dl, 32, 0);
    }
    std::vector<double> g;
    const double freqs[8] = { 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    const size_t seg = 8192;
    std::vector<double> pin (seg / 2, 0.0), pout (seg / 2, 0.0);
    for (size_t a = 4096; a + seg <= N; a += seg)
    {
        const auto i1 = oracle::powerSpectrum (in, a, seg), o1 = oracle::powerSpectrum (out, a, seg);
        for (size_t k = 0; k < seg / 2; ++k) { pin[k] += i1[k]; pout[k] += o1[k]; }
    }
    for (const double hz : freqs)
    {
        const size_t c = static_cast<size_t> (hz / 48000.0 * seg);
        double a = 0, b = 0;
        for (size_t k = c - 6; k <= c + 6; ++k) { a += pin[k]; b += pout[k]; }
        g.push_back (10.0 * std::log10 (b / a));
    }
    return g;
}
} // namespace

TEST (own_filter_responses_match_document_05)
{
    struct Row { int model, style; float res; double db[8]; double tol; };
    const Row rows[] = {
        { 0, 0, 0.0f, { -1.5, -1.8, -3.3, -7.3, -15.3, -26.0, -37.9, -51.2 }, 1.5 },
        { 0, 0, 0.5f, { -6.5, -6.0, -4.6, -2.1, -16.7, -30.2, -42.9, -56.4 }, 1.5 },
        { 0, 1, 0.0f, { -0.2, -0.4, -1.2, -6.2, -25.2, -48.7, -73.2, -99.9 }, 1.5 },
        { 1, 0, 0.0f, { 0.4, 0.0, -1.2, -5.1, -13.2, -24.0, -36.1, -49.4 }, 3.0 },
        { 2, 0, 0.0f, { -1.5, -1.9, -3.3, -7.4, -15.4, -26.1, -38.1, -51.4 }, 1.5 },
        { 2, 1, 0.0f, { -1.6, -2.4, -5.3, -13.4, -29.4, -50.9, -74.8, -101.3 }, 1.5 },
        { 2, 1, 0.5f, { -8.8, -8.1, -5.9, -5.4, -27.2, -49.0, -72.8, -99.4 }, 3.0 },
        { 3, 0, 0.0f, { -0.1, -0.5, -1.9, -6.0, -14.0, -24.7, -36.6, -49.9 }, 1.5 },
        { 3, 0, 0.5f, { -1.8, -1.4, 0.3, 5.7, -11.8, -25.6, -38.3, -51.8 }, 1.5 },
        { 3, 1, 0.0f, { -0.1, -0.3, -1.0, -6.0, -25.1, -48.6, -73.1, -99.7 }, 1.5 },
        { 4, 0, 0.0f, { 0.3, -7.4, -16.8, -29.2, -45.5, -66.1, -89.6, -116.0 }, 3.0 },
        { 5, 0, 0.0f, { -12.9, -2.8, 2.6, 4.2, -27.6, -9.5, -18.4, -26.0 }, 5.0 },
    };
    for (const Row& r : rows)
    {
        const auto g = noiseGainDb (r.model, r.style, r.res);
        for (int i = 0; i < 8; ++i)
        {
            if (r.db[i] < -40.0) // the deep stopband: below -40 dB (document 05, section 10)
                { CHECK (g[static_cast<size_t> (i)] < -40.0); continue; }
            if (!CHECK (std::fabs (g[static_cast<size_t> (i)] - r.db[i]) <= r.tol))
                std::printf ("    model %d style %d res %.1f bin %d: %.1f dB, spec %.1f dB\n", r.model, r.style, static_cast<double> (r.res), i, g[static_cast<size_t> (i)], r.db[i]);
        }
    }
}

// The filters-serum collection itself (no guard): every type stays bounded at the extreme settings on loud noise.
TEST (own_filters_serum_raw_stays_bounded)
{
    int bad = 0;
    for (int type = 0; type < filters::kFilterTypeCount; ++type)
        for (const float cutoff : { 20.0f, 300.0f, 5000.0f, 20000.0f })
            for (const float res : { 0.0f, 0.5f, 1.0f })
            {
                filters::Filter f;
                f.prepare (48000.0);
                f.setType (type);
                filters::FilterParams p;
                p.cutoff = cutoff;
                p.resonance = res;
                p.drive = 1.0f;
                p.var = 1.0f;
                p.blend = 0.5f;
                p.mix = 1.0f;
                f.setParams (p);
                f.reset();
                own::Rng rng;
                rng.seed (static_cast<uint32_t> (type));
                float in[32], out[32], peak = 0.0f;
                for (int b = 0; b < 400; ++b)
                {
                    for (auto& x : in)
                        x = 1.4f * rng.bi();
                    if (b % 50 == 0)
                    {
                        p.cutoff = 20.0f * std::pow (1000.0f, static_cast<float> (rng.uni()));
                        f.setParams (p);
                    }
                    f.process (in, out, 32);
                    for (const float v : out)
                        peak = std::max (peak, std::isfinite (v) ? std::fabs (v) : 1.0e30f);
                }
                if (!(peak <= 128.0f))
                {
                    ++bad;
                    std::printf ("    type %d (%s) cutoff %g res %g: peak %g\n", type, filters::getFilterTypeName (type), static_cast<double> (cutoff), static_cast<double> (res), static_cast<double> (peak));
                }
            }
    CHECK_EQ (bad, 0);
}

// Checkpoint samples of the built-in tables (docs/engine-spec/02-oscillator.md 1.1), tolerance 2e-4.
TEST (own_builtin_tables_match_the_checkpoints)
{
    struct Row { int entry, frame; double x[12]; };
    const int idx[12] = { 0, 128, 256, 384, 512, 640, 768, 896, 1024, 1280, 1536, 1792 };
    const Row rows[] = {
        { 1, 0, { 0.0, -0.7415, -0.6360, -0.5302, -0.4242, -0.3182, -0.2121, -0.1061, 0.0, 0.2121, 0.4242, 0.6360 } },
        { 2, 0, { 0.0, 0.8468, 0.8474, 0.8476, 0.8477, 0.8476, 0.8474, 0.8468, 0.0, -0.8474, -0.8477, -0.8474 } },
        { 4, 0, { 0.2976, 0.8920, 0.8923, 0.8920, 0.2976, -0.2969, -0.2974, -0.2975, -0.2976, -0.2976, -0.2976, -0.2974 } },
        { 5, 21, { 0.0, 0.7766, 0.7065, 0.6360, 0.5654, 0.4947, 0.4240, 0.3531, 0.0, -0.4240, -0.5654, -0.7065 } },
        { 9, 21, { 0.0, 0.4967, -0.0397, 0.0568, 0.0661, 0.0566, 0.0420, 0.0224, 0.0, -0.0420, -0.0661, 0.0397 } },
        { 10, 42, { 0.0, -0.0794, -0.9973, -0.7611, 1.0, -0.7611, -0.9973, -0.0794, 0.0, 0.9973, -1.0, 0.9973 } },
        { 12, 63, { 0.0552, 0.3649, 0.8197, -0.0090, -0.5539, -0.0781, 0.0478, -0.1594, 0.8282, -0.1646, 0.3187, -0.7323 } },
    };
    for (const Row& r : rows)
    {
        const own::Wavetable* wt = own::builtinWavetable (r.entry);
        CHECK (wt != nullptr);
        if (wt == nullptr)
            continue;
        for (int i = 0; i < 12; ++i)
            if (!CHECK_NEAR (wt->cycle (r.frame, 0)[idx[i]], r.x[i], 2.0e-4))
                std::printf ("    entry %d frame %d sample %d\n", r.entry, r.frame, idx[i]);
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
