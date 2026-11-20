// SynthyBoy - the voice-count gain of the own engine (docs/release/VOICE-COUNT-GAIN-SPEC.md): acceptance tests, written before the implementation. Each test FAILs with
// "voice-count gain not implemented" until core/include/synthyboy/engine.h defines SYNTHYBOY_HAS_VOICE_COUNT_GAIN with setOwnVoiceCountAlpha and ownVoiceCountGain.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test_runner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/preset.h"
#include "device_profile.h"
#include "dsp/simd.h"

using namespace synthyboy;

#if ! defined (SYNTHYBOY_HAS_VOICE_COUNT_GAIN)

TEST (voice_count_gain_alpha_zero_and_single_voices_are_bit_identical) { CHECK (! "voice-count gain not implemented (docs/release/VOICE-COUNT-GAIN-SPEC.md)"); }
TEST (voice_count_gain_steady_state_follows_n_to_the_minus_alpha) { CHECK (! "voice-count gain not implemented (docs/release/VOICE-COUNT-GAIN-SPEC.md)"); }
TEST (voice_count_gain_slopes_are_bounded) { CHECK (! "voice-count gain not implemented (docs/release/VOICE-COUNT-GAIN-SPEC.md)"); }
TEST (voice_count_gain_closes_the_chord_note_gap) { CHECK (! "voice-count gain not implemented (docs/release/VOICE-COUNT-GAIN-SPEC.md)"); }
TEST (voice_count_gain_applies_immediately_on_mid_period_chord_strike) { CHECK (! "voice-count gain not implemented (docs/release/VOICE-COUNT-GAIN-SPEC.md)"); }

#else

namespace
{
constexpr double kSr = 48000.0;

struct Preset
{
    std::string name, category;
    ParamValues values;
    StateContent content;
};

std::vector<Preset> loadPresets()
{
    std::vector<Preset> out;
    for (const PresetEntry& e : scanPresetDirectory (std::string (SYNTHYBOY_TEST_SOURCE_DIR) + "/content/presets"))
    {
        std::ifstream in (e.path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        Preset p;
        PresetMeta meta;
        p.values = ParamValues::defaults();
        const LoadResult r = loadPreset (text.data(), text.size(), meta, p.values, p.content);
        if (r.status != LoadStatus::ok && r.status != LoadStatus::newer_version)
            continue;
        p.name = meta.name;
        p.category = meta.category;
        out.push_back (std::move (p));
    }
    return out;
}

Quality profileQuality (const char* text)
{
    synthyboy_mpc::Profile profile;
    std::string why;
    synthyboy_mpc::parseProfile (text, profile, &why);
    Quality q = synthyboy_mpc::profileQuality (profile);
    q.realtime = false;
    return q;
}

/** The test patch of the spec: a sine, no filter, instant attack, full sustain, 50 ms release, no velocity tracking, 16 voices, no effects. */
ParamValues testPatch()
{
    ParamValues v = ParamValues::defaults();
    auto set = [&] (const char* key, float value) {
        const ParamInfo* p = findParamByKey (key);
        CHECK (p != nullptr);
        if (p != nullptr)
            v.plain[static_cast<size_t> (p - paramTable())] = value;
    };
    set ("osc1.wavetable", 0.0f);
    set ("filter1.on", 0.0f);
    set ("env1.attack", 0.0f);
    set ("env1.sustain", 100.0f);
    set ("env1.release", 50.0f);
    set ("master.velocity_track", 0.0f);
    set ("master.polyphony", 16.0f);
    return v;
}

std::unique_ptr<IEngine> makeEngine (const ParamValues& v, const StateContent& c, const Quality& q, const IContentStore* store, float alpha, int block)
{
    auto e = createEngine (EngineKind::own);
    CHECK (setOwnVoiceCountAlpha (*e, alpha));
    e->prepare (kSr, block, q);
    e->setContentStore (store);
    for (size_t i = 0; i < paramCount(); ++i)
        e->setParam (paramTable()[i].id, v.plain[i]);
    e->setContent (c);
    std::vector<float> zl (static_cast<size_t> (block)), zr (zl.size());
    for (int b = 0; b < static_cast<int> (0.3 * kSr) / block; ++b)   // the smoothing of master.volume settles
        e->process (nullptr, 0, zl.data(), zr.data(), block);
    return e;
}

Event noteOn (int key, float vel)
{
    Event e;
    e.frame = 0;
    e.type = Event::NoteOn;
    e.channel = 0;
    e.key = static_cast<int16_t> (key);
    e.value = vel;
    e.noteId = key;
    return e;
}

Event noteOff (int key)
{
    Event e;
    e.frame = 0;
    e.type = Event::NoteOff;
    e.channel = 0;
    e.key = static_cast<int16_t> (key);
    e.value = 0.0f;
    e.noteId = -1;
    return e;
}

/** keys struck at 0, released at `offAt` (seconds), rendered for `seconds`, 128 frame blocks. */
void render (IEngine& e, const std::vector<int>& keys, double offAt, double seconds, std::vector<float>& L, std::vector<float>& R)
{
    const int block = 128, blocks = static_cast<int> (seconds * kSr / block), offBlock = static_cast<int> (offAt * kSr / block);
    L.assign (static_cast<size_t> (blocks * block), 0.0f);
    R.assign (L.size(), 0.0f);
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<Event> ev;
        if (b == 0)
            for (int k : keys)
                ev.push_back (noteOn (k, 1.0f));
        if (b == offBlock)
            for (int k : keys)
                ev.push_back (noteOff (k));
        e.process (ev.empty() ? nullptr : ev.data(), static_cast<int> (ev.size()), L.data() + b * block, R.data() + b * block, block);
    }
}

uint32_t bitsOf (float f)
{
    uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}

double rmsDb (const std::vector<float>& L, const std::vector<float>& R, double t0, double t1)
{
    double s = 0;
    const size_t a = static_cast<size_t> (t0 * kSr), b = static_cast<size_t> (t1 * kSr);
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (L[i]) * L[i] + static_cast<double> (R[i]) * R[i];
    return 10.0 * std::log10 (std::max (s / (2.0 * static_cast<double> (b - a)), 1e-30));
}

double peakDb (const std::vector<float>& L, const std::vector<float>& R)
{
    double p = 0;
    for (size_t i = 0; i < L.size(); ++i)
        p = std::max ({ p, static_cast<double> (std::fabs (L[i])), static_cast<double> (std::fabs (R[i])) });
    return 20.0 * std::log10 (std::max (p, 1e-10));
}

double median (std::vector<double> v)
{
    std::sort (v.begin(), v.end());
    return v.empty() ? 0.0 : (v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]));
}
} // namespace

TEST (voice_count_gain_alpha_zero_and_single_voices_are_bit_identical)
{
    DirectoryContentStore store (SYNTHYBOY_TEST_CONTENT_STORE);
    long renders = 0, differ = 0;
    std::vector<float> L0, R0, L1, R1;
    for (const char* prof : { "low", "high,voices=16,control=economy" })
    {
        const Quality q = profileQuality (prof);
        for (const Preset& p : loadPresets())
        {
            auto a = makeEngine (p.values, p.content, q, &store, 0.0f, 128);
            auto b = makeEngine (p.values, p.content, q, &store, 0.5f, 128);
            render (*a, { 48 }, 1.0, 2.0, L0, R0);
            render (*b, { 48 }, 1.0, 2.0, L1, R1);
            ++renders;
            bool same = true;
            for (size_t i = 0; i < L0.size() && same; ++i)
                same = bitsOf (L0[i]) == bitsOf (L1[i]) && bitsOf (R0[i]) == bitsOf (R1[i]);
            if (! same)
            {
                // one note can sound with more than one voice by design (an arpeggio, a strum, overlapping retriggers): asserted only for the monophonic presets
                const ParamInfo* poly = findParamByKey ("master.polyphony");
                const bool mono = poly != nullptr && p.values.plain[static_cast<size_t> (poly - paramTable())] <= 1.0f;
                differ += mono ? 1 : 0;
                std::printf ("  a single note differs between alpha 0 and 0.5: %s (%s)%s\n", p.name.c_str(), prof, mono ? " MONOPHONIC: a fault" : " (several voices for one note)");
            }
        }
    }
    std::printf ("  %ld single note renders, %ld monophonic ones differ\n", renders, differ);
    CHECK_EQ (differ, 0L);
    // the test patch: one note at alpha 0.5 equals alpha 0
    {
        const Quality q1 = profileQuality ("low");
        auto a1 = makeEngine (testPatch(), {}, q1, nullptr, 0.0f, 128), b1 = makeEngine (testPatch(), {}, q1, nullptr, 0.5f, 128);
        render (*a1, { 48 }, 0.5, 1.0, L0, R0);
        render (*b1, { 48 }, 0.5, 1.0, L1, R1);
        bool same1 = true;
        for (size_t i = 0; i < L0.size(); ++i)
            same1 = same1 && bitsOf (L0[i]) == bitsOf (L1[i]) && bitsOf (R0[i]) == bitsOf (R1[i]);
        CHECK (same1);
    }
    // the switch itself: two engines at alpha 0 give the same chord
    const Quality q = profileQuality ("low");
    auto a = makeEngine (testPatch(), {}, q, nullptr, 0.0f, 128), b = makeEngine (testPatch(), {}, q, nullptr, 0.0f, 128);
    render (*a, { 48, 55, 60, 64 }, 0.5, 1.0, L0, R0);
    render (*b, { 48, 55, 60, 64 }, 0.5, 1.0, L1, R1);
    bool same = true;
    for (size_t i = 0; i < L0.size(); ++i)
        same = same && bitsOf (L0[i]) == bitsOf (L1[i]);
    CHECK (same);
    CHECK_EQ (ownVoiceCountGain (*a), 1.0f);
}

TEST (voice_count_gain_steady_state_follows_n_to_the_minus_alpha)
{
    const Quality q = profileQuality ("low");
    const std::vector<int> keys8 { 48, 52, 55, 59, 62, 65, 69, 72 };
    for (int n : { 2, 4, 8 })
    {
        const std::vector<int> keys (keys8.begin(), keys8.begin() + n);
        auto a = makeEngine (testPatch(), {}, q, nullptr, 0.0f, 128), b = makeEngine (testPatch(), {}, q, nullptr, 0.5f, 128);
        std::vector<float> L0, R0, L1, R1;
        // held to the end: the gain read at the end of the render (0.3 s after the strike) is the steady one
        render (*a, keys, 10.0, 0.3, L0, R0);
        render (*b, keys, 10.0, 0.3, L1, R1);
        const double expect = -10.0 * std::log10 (static_cast<double> (n));
        const double got = rmsDb (L1, R1, 0.2, 0.3) - rmsDb (L0, R0, 0.2, 0.3);
        std::printf ("  %d voices: %.3f dB (expected %.3f), engine gain %.6f (expected %.6f)\n", n, got, expect, static_cast<double> (ownVoiceCountGain (*b)), std::pow (static_cast<double> (n), -0.5));
        CHECK_NEAR (got, expect, 0.05);
        CHECK_NEAR (ownVoiceCountGain (*b), std::pow (static_cast<double> (n), -0.5), 1e-4);
    }
}

TEST (voice_count_gain_slopes_are_bounded)
{
    const Quality q = profileQuality ("low");
    const int block = 32;   // one control period per call: the gain is read once per period
    auto e = makeEngine (testPatch(), {}, q, nullptr, 0.5f, block);
    std::vector<float> l (block), r (block);
    double prevDb = 0.0, worstDown = 0.0, worstUp = 0.0;
    const double endOfTails = 0.8 + 0.25;   // the 50 ms release tails of the three notes are far under -60 dB 250 ms after their note-off
    double backAt = -1.0;
    const int periods = static_cast<int> (1.6 * kSr / block);
    for (int k = 0; k < periods; ++k)
    {
        const double t = k * block / kSr;
        std::vector<Event> ev;
        if (k == 0) ev.push_back (noteOn (48, 1.0f));
        if (k == static_cast<int> (0.3 * kSr / block)) for (int key : { 55, 60, 64 }) ev.push_back (noteOn (key, 1.0f));
        if (k == static_cast<int> (0.8 * kSr / block)) for (int key : { 55, 60, 64 }) ev.push_back (noteOff (key));
        e->process (ev.empty() ? nullptr : ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), block);
        const double g = ownVoiceCountGain (*e);
        const double gDb = 20.0 * std::log10 (std::max (g, 1e-6));
        if (k > 0)
        {
            worstDown = std::min (worstDown, gDb - prevDb);
            worstUp = std::max (worstUp, gDb - prevDb);
        }
        prevDb = gDb;
        if (t >= 0.8 && backAt < 0.0 && g == 1.0)
            backAt = t;
    }
    std::printf ("  largest step down %.3f dB, up %.3f dB per control period; gain back to 1 at %.3f s (tails ended about %.3f s)\n", worstDown, worstUp, backAt, endOfTails);
    CHECK (worstDown >= -2.0 - 1e-6);
    CHECK (worstUp <= 0.1 + 1e-6);
    CHECK (backAt > 0.0 && backAt - endOfTails <= 0.2);
    // a chord struck from silence is scaled from its first period (the instant rule)
    auto c = makeEngine (testPatch(), {}, q, nullptr, 0.5f, block);
    std::vector<Event> ev;
    for (int key : { 48, 55, 60, 64 })
        ev.push_back (noteOn (key, 1.0f));
    c->process (ev.data(), static_cast<int> (ev.size()), l.data(), r.data(), block);
    const double first = 20.0 * std::log10 (std::max (static_cast<double> (ownVoiceCountGain (*c)), 1e-6));
    std::printf ("  four notes from silence: the gain of the first period %.3f dB\n", first);
    CHECK_NEAR (first, -6.0206, 0.05);
}

// Mid-period chord strike acceptance test: extends voice_count_gain_slopes_are_bounded
// for unaligned note-ons when eventGrid is disabled (desktop medium/high quality tiers).
TEST (voice_count_gain_applies_immediately_on_mid_period_chord_strike)
{
    Quality q = qualityForTier (Tier::high);
    q.realtime = false;
    const int block = 32;
    auto e0 = makeEngine (testPatch(), {}, q, nullptr, 0.0f, block);
    auto e1 = makeEngine (testPatch(), {}, q, nullptr, 0.5f, block);

    std::vector<float> l0 (static_cast<size_t> (block)), r0 (static_cast<size_t> (block));
    std::vector<float> l1 (static_cast<size_t> (block)), r1 (static_cast<size_t> (block));

    std::vector<Event> chord;
    for (int key : { 48, 55, 60, 64 })
    {
        Event ev = noteOn (key, 1.0f);
        ev.frame = 5;
        chord.push_back (ev);
    }

    // 1. Strike a 4-note chord from silence at frame = 5 inside block 0 (32 samples)
    e0->process (chord.data(), static_cast<int> (chord.size()), l0.data(), r0.data(), block);
    e1->process (chord.data(), static_cast<int> (chord.size()), l1.data(), r1.data(), block);

    double sumSq0 = 0.0, sumSq1 = 0.0;
    for (int i = 5; i < 32; ++i)
    {
        sumSq0 += static_cast<double> (l0[static_cast<size_t> (i)]) * l0[static_cast<size_t> (i)] + static_cast<double> (r0[static_cast<size_t> (i)]) * r0[static_cast<size_t> (i)];
        sumSq1 += static_cast<double> (l1[static_cast<size_t> (i)]) * l1[static_cast<size_t> (i)] + static_cast<double> (r1[static_cast<size_t> (i)]) * r1[static_cast<size_t> (i)];
    }
    const double rms0 = std::sqrt (std::max (sumSq0 / (2.0 * 27.0), 1e-30));
    const double rms1 = std::sqrt (std::max (sumSq1 / (2.0 * 27.0), 1e-30));
    const double diffDb = 20.0 * std::log10 (std::max (rms1 / rms0, 1e-15));
    const double firstGainDb = 20.0 * std::log10 (std::max (static_cast<double> (ownVoiceCountGain (*e1)), 1e-6));

    std::printf ("  mid-period chord strike (frame = 5): diffDb = %.4f dB, firstGainDb = %.4f dB\n", diffDb, firstGainDb);
    CHECK_NEAR (diffDb, -6.0206, 0.05);
    CHECK_NEAR (firstGainDb, -6.0206, 0.05);

    // Render second block (samples 32..63): gain stays at -6.0206 dB
    e1->process (nullptr, 0, l1.data(), r1.data(), block);
    const double secondGainDb = 20.0 * std::log10 (std::max (static_cast<double> (ownVoiceCountGain (*e1)), 1e-6));
    std::printf ("  second block gain: %.4f dB\n", secondGainDb);
    CHECK_NEAR (secondGainDb, -6.0206, 0.05);

    // 2. Mid-period addition to a held note
    auto eHeld = makeEngine (testPatch(), {}, q, nullptr, 0.5f, block);
    std::vector<float> lHeld (static_cast<size_t> (block)), rHeld (static_cast<size_t> (block));

    std::vector<Event> evHeld = { noteOn (48, 1.0f) };
    eHeld->process (evHeld.data(), 1, lHeld.data(), rHeld.data(), block);
    for (int b = 1; b < 10; ++b)
        eHeld->process (nullptr, 0, lHeld.data(), rHeld.data(), block);

    std::vector<Event> evAdd;
    for (int key : { 55, 60, 64 })
    {
        Event ev = noteOn (key, 1.0f);
        ev.frame = 7;
        evAdd.push_back (ev);
    }
    eHeld->process (evAdd.data(), static_cast<int> (evAdd.size()), lHeld.data(), rHeld.data(), block);

    const double afterAddDb = 20.0 * std::log10 (std::max (static_cast<double> (ownVoiceCountGain (*eHeld)), 1e-6));
    std::printf ("  mid-period note addition (frame = 7 of block 10): afterAddDb = %.4f dB (expected -1.5625 dB)\n", afterAddDb);
    CHECK_NEAR (afterAddDb, -1.5625, 0.05);

    // 3. Bit-identity checksum: render a 4-note chord at frame = 0 on profileQuality ("low") and Tier::high, 256 samples each
    auto printVcgChecksum = [] (const char* caseName, const std::vector<float>& left)
    {
        CHECK (left.size() >= 8);
        double sumAbs = 0.0;
        for (float s : left)
            sumAbs += std::fabs (static_cast<double> (s));
        const size_t sz = left.size();
        std::printf ("VCG-CHECKSUM %s sum=%.9g last8=%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                     caseName, sumAbs,
                     static_cast<double> (left[sz - 8]),
                     static_cast<double> (left[sz - 7]),
                     static_cast<double> (left[sz - 6]),
                     static_cast<double> (left[sz - 5]),
                     static_cast<double> (left[sz - 4]),
                     static_cast<double> (left[sz - 3]),
                     static_cast<double> (left[sz - 2]),
                     static_cast<double> (left[sz - 1]));
    };

    {
        const Quality qLow = profileQuality ("low");
        auto eLow = makeEngine (testPatch(), {}, qLow, nullptr, 0.5f, block);
        std::vector<float> leftLow (256, 0.0f), rightLow (256, 0.0f);
        std::vector<Event> chord0;
        for (int key : { 48, 55, 60, 64 })
            chord0.push_back (noteOn (key, 1.0f));
        for (int b = 0; b < 8; ++b)
        {
            const Event* evPtr = (b == 0) ? chord0.data() : nullptr;
            const int evNum = (b == 0) ? static_cast<int> (chord0.size()) : 0;
            eLow->process (evPtr, evNum, leftLow.data() + static_cast<size_t> (b * block), rightLow.data() + static_cast<size_t> (b * block), block);
        }
        printVcgChecksum ("low_grid_frame0", leftLow);
    }

    {
        Quality qHigh = qualityForTier (Tier::high);
        qHigh.realtime = false;
        auto eHigh = makeEngine (testPatch(), {}, qHigh, nullptr, 0.5f, block);
        std::vector<float> leftHigh (256, 0.0f), rightHigh (256, 0.0f);
        std::vector<Event> chord0;
        for (int key : { 48, 55, 60, 64 })
            chord0.push_back (noteOn (key, 1.0f));
        for (int b = 0; b < 8; ++b)
        {
            const Event* evPtr = (b == 0) ? chord0.data() : nullptr;
            const int evNum = (b == 0) ? static_cast<int> (chord0.size()) : 0;
            eHigh->process (evPtr, evNum, leftHigh.data() + static_cast<size_t> (b * block), rightHigh.data() + static_cast<size_t> (b * block), block);
        }
        printVcgChecksum ("high_nogrid_frame0", leftHigh);
    }
}

TEST (voice_count_gain_closes_the_chord_note_gap)
{
    DirectoryContentStore store (SYNTHYBOY_TEST_CONTENT_STORE);
    const Quality q = profileQuality ("low");
    std::map<std::string, std::vector<double>> gap0, gap5;
    std::vector<double> all0, all5;
    std::vector<float> L, R;
    for (const Preset& p : loadPresets())
    {
        double g[2];
        int k = 0;
        for (float alpha : { 0.0f, 0.5f })
        {
            auto en = makeEngine (p.values, p.content, q, &store, alpha, 128);
            render (*en, { 48 }, 10.0, 1.0, L, R);
            const double note = peakDb (L, R);
            auto ec = makeEngine (p.values, p.content, q, &store, alpha, 128);
            render (*ec, { 48, 55, 60, 64 }, 10.0, 1.0, L, R);
            g[k++] = peakDb (L, R) - note;
        }
        gap0[p.category].push_back (g[0]);
        gap5[p.category].push_back (g[1]);
        all0.push_back (g[0]);
        all5.push_back (g[1]);
    }
    std::printf ("  chord (48 55 60 64) minus note (48) peak, Key 61, medians: category  alpha 0  alpha 0.5\n");
    for (const auto& kv : gap0)
        std::printf ("    %-6s %6.2f  %6.2f dB\n", kv.first.c_str(), median (kv.second), median (gap5[kv.first]));
    const double fall = median (all0) - median (all5);
    std::printf ("    all    %6.2f  %6.2f dB (falls by %.2f dB)\n", median (all0), median (all5), fall);
    CHECK (fall >= 4.5 && fall <= 7.0);
}

TEST (voice_count_gain_neon_and_scalar_ramp_multiply_are_bit_identical)
{
#if defined(__ARM_NEON)
    uint32_t s = 12345u;
    const auto randFloat = [&] {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float> (static_cast<int32_t> (s)) * (1.0f / 2147483648.0f);
    };
    int totalCases = 0;
    for (int rep = 0; rep < 500; ++rep)
    {
        for (int n : { 1, 4, 7, 16, 23, 31, 32 })
        {
            std::vector<float> dataNeon (static_cast<size_t> (n)), dataScalar (static_cast<size_t> (n)), g (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                dataNeon[static_cast<size_t> (i)] = dataScalar[static_cast<size_t> (i)] = randFloat();
                g[static_cast<size_t> (i)] = std::fabs (randFloat());
            }
            own::simd::mulRamp (dataNeon.data(), g.data(), n);
            own::simd::mulRampScalar (dataScalar.data(), g.data(), n);
            for (int i = 0; i < n; ++i)
                CHECK_EQ (bitsOf (dataNeon[static_cast<size_t> (i)]), bitsOf (dataScalar[static_cast<size_t> (i)]));
            ++totalCases;
        }
    }
    std::printf ("  NEON vs scalar ramp multiply verified bit-identical on %d cases\n", totalCases);
#else
    std::printf ("  no NEON in this build\n");
#endif
}

#endif
