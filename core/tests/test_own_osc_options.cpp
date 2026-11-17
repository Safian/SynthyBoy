// SynthyBoy - own engine tests: the oscillator options the pages of the type "OSCILLATOR OPTIONS" write: fixed pitch, transpose snap, key fold, key warp, phase memory, phase legato.
// Each has a test of what it does, and the default of each is proved to leave today's render bit identical (a checksum of the rendered floats taken before the options existed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "synthyboy/engine.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

struct Out
{
    std::vector<float> l, r;
};

struct Note
{
    int at, off, key;   // sample of the note on, of the note off (-1: never), key
};

struct Rig
{
    std::unique_ptr<IEngine> e;
    Rig()
    {
        e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (kSr, kBlock, qualityForTier (Tier::high));
        set ("filter1.on", 0.0f);
        set ("master.volume", 0.0f);
        set ("env1.attack", 0.0f);
        set ("env1.sustain", 100.0f);
        set ("env1.release", 10.0f);
        set ("osc1.random_phase", 0.0f);
        set ("osc1.level", 80.0f);
        set ("osc1.wavetable", 0.0f);   // Sine
        set ("osc1.unison.voices", 1.0f);
    }
    void set (const char* key, float v)
    {
        const ParamInfo* p = findParamByKey (key);
        CHECK (p != nullptr);
        if (p != nullptr)
            e->setParam (p->id, v);
    }
    /** Renders `total` samples (a multiple of the block) with the notes; the events sit at the start of the block they fall in. */
    Out render (const std::vector<Note>& notes, int total)
    {
        Out o;
        o.l.assign (static_cast<size_t> (total), 0.0f);
        o.r.assign (static_cast<size_t> (total), 0.0f);
        for (int pos = 0; pos < total; pos += kBlock)
        {
            Event ev[8];
            int ne = 0;
            for (const Note& n : notes)
            {
                if (n.at >= pos && n.at < pos + kBlock && ne < 8)
                    ev[ne++] = sbtest::noteOn (n.at - pos, n.key, 1.0f);
                if (n.off >= pos && n.off < pos + kBlock && ne < 8)
                    ev[ne++] = sbtest::noteOff (n.off - pos, n.key);
            }
            e->process (ev, ne, o.l.data() + pos, o.r.data() + pos, kBlock);
        }
        return o;
    }
};

uint64_t fnv (const Out& o)
{
    uint64_t h = 1469598103934665603ull;
    for (const std::vector<float>* v : { &o.l, &o.r })
        for (float f : *v)
        {
            uint32_t b;
            std::memcpy (&b, &f, 4);
            for (int i = 0; i < 4; ++i)
            {
                h ^= (b >> (8 * i)) & 255u;
                h *= 1099511628211ull;
            }
        }
    return h;
}

/** The frequency of a (nearly) sine signal from its rising zero crossings in [a, b). */
double frequencyOf (const std::vector<float>& x, size_t a, size_t b)
{
    double first = -1.0, last = -1.0;
    int n = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (-x[i - 1]) / static_cast<double> (x[i] - x[i - 1]);
            if (first < 0.0)
                first = t;
            last = t;
            ++n;
        }
    return n >= 3 ? (n - 1) * kSr / (last - first) : 0.0;
}
double hzOfKey (double key) { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }
double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (b - a));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// the scenarios of the default test: a saw with three unison voices, two overlapping notes, poly legato, pitch tracking off, two oscillators warping each other
Out scenarioSaw()
{
    Rig r;
    r.set ("osc1.wavetable", 1.0f);
    r.set ("osc1.unison.voices", 3.0f);
    r.set ("osc1.random_phase", 100.0f);
    return r.render ({ { 0, 20000, 57 }, { 4096, 24000, 64 } }, 32768);
}
Out scenarioPolyLegato()
{
    Rig r;
    r.set ("master.poly_legato", 1.0f);
    r.set ("osc1.wavetable", 1.0f);
    return r.render ({ { 0, 30000, 60 }, { 7168, 30000, 67 } }, 32768);
}
Out scenarioUntracked()
{
    Rig r;
    r.set ("osc1.pitch_track", 0.0f);
    r.set ("osc1.wavetable", 2.0f);
    return r.render ({ { 0, 12000, 48 }, { 4096, 16000, 72 } }, 24576);
}
Out scenarioTwoOscillators()
{
    Rig r;
    r.set ("osc2.on", 1.0f);
    r.set ("osc2.level", 70.0f);
    r.set ("osc2.pitch_coarse", 7.0f);
    r.set ("osc2.wavetable", 1.0f);
    r.set ("osc1.transpose", 5.0f);
    r.set ("osc1.warp.type", 1.0f);
    r.set ("osc1.warp.amount", 60.0f);
    return r.render ({ { 0, 16000, 55 }, { 3072, 20000, 62 } }, 24576);
}
Out scenarioRanges()
{
    Rig r;
    r.set ("osc1.map.key_low", 50.0f);
    r.set ("osc1.map.key_high", 70.0f);
    r.set ("osc1.wavetable", 1.0f);
    return r.render ({ { 0, 8000, 40 }, { 4096, 12000, 60 }, { 8192, 16000, 90 } }, 24576);
}

// the checksums of these renders taken with the engine as it was before the six options existed (the defaults must keep them)
constexpr uint64_t kGoldenSaw = 0xd2c89411914ab6a6ull, kGoldenPolyLegato = 0x327c6603c38fb5a3ull, kGoldenUntracked = 0xac25d77096e199a7ull, kGoldenTwo = 0x1a5c86e3433d0d8bull, kGoldenRanges = 0xbf3638ec3320d16bull;
} // namespace

TEST (own_osc_options_defaults_keep_the_render_bit_identical)
{
    if (std::getenv ("SB_PRINT_GOLDEN") != nullptr)
        std::printf ("golden saw %016llx polylegato %016llx untracked %016llx two %016llx ranges %016llx\n", static_cast<unsigned long long> (fnv (scenarioSaw())),
                 static_cast<unsigned long long> (fnv (scenarioPolyLegato())), static_cast<unsigned long long> (fnv (scenarioUntracked())),
                 static_cast<unsigned long long> (fnv (scenarioTwoOscillators())), static_cast<unsigned long long> (fnv (scenarioRanges())));
    CHECK (fnv (scenarioSaw()) == kGoldenSaw);
    CHECK (fnv (scenarioPolyLegato()) == kGoldenPolyLegato);
    CHECK (fnv (scenarioUntracked()) == kGoldenUntracked);
    CHECK (fnv (scenarioTwoOscillators()) == kGoldenTwo);
    CHECK (fnv (scenarioRanges()) == kGoldenRanges);
}

namespace
{
int choiceIndex (const char* key, const char* label)
{
    const ParamInfo* p = findParamByKey (key);
    if (p == nullptr || p->choices == nullptr)
        return -1;
    for (int i = 0; i < p->numChoices; ++i)
        if (std::strcmp (p->choices[i], label) == 0)
            return i;
    return -1;
}
double maxDiff (const Out& a, const Out& b, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = from; i < to; ++i)
        m = std::max ({ m, std::fabs (static_cast<double> (a.l[i] - b.l[i])), std::fabs (static_cast<double> (a.r[i] - b.r[i])) });
    return m;
}
double sqErr (const Out& a, const Out& b, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += std::pow (static_cast<double> (a.l[i] - b.l[i]), 2.0);
    return s;
}
}

TEST (own_osc_fixed_pitch_is_the_note_of_an_oscillator_without_pitch_tracking)
{
    auto hz = [] (const char* fixed, int key) {
        Rig r;
        r.set ("osc1.pitch_track", 0.0f);
        if (fixed != nullptr)
            r.set ("osc1.fixed_pitch", static_cast<float> (std::atof (fixed)));
        const Out o = r.render ({ { 0, -1, key } }, 16384);
        return frequencyOf (o.l, 4096, 16384);
    };
    CHECK (std::fabs (hz (nullptr, 40) / hzOfKey (60) - 1.0) < 0.01);   // the default: the oscillator's own C3, whatever the key
    CHECK (std::fabs (hz ("72", 40) / hzOfKey (72) - 1.0) < 0.01);
    CHECK (std::fabs (hz ("72", 90) / hzOfKey (72) - 1.0) < 0.01);       // and the key changes nothing
    CHECK (std::fabs (hz ("48", 40) / hzOfKey (48) - 1.0) < 0.01);
    // with the pitch tracking on the fixed pitch is not used
    Rig r;
    r.set ("osc1.fixed_pitch", 72.0f);
    CHECK (std::fabs (frequencyOf (r.render ({ { 0, -1, 55 } }, 16384).l, 4096, 16384) / hzOfKey (55) - 1.0) < 0.01);
}

TEST (own_osc_transpose_snap_moves_the_transpose_into_the_scale)
{
    auto hz = [] (int mask, int transpose, int key) {
        Rig r;
        r.set ("osc1.transpose_snap", static_cast<float> (mask));
        r.set ("osc1.transpose", static_cast<float> (transpose));
        const Out o = r.render ({ { 0, -1, key } }, 16384);
        return frequencyOf (o.l, 4096, 16384);
    };
    const int scale = 1 | (1 << 7);   // the notes 0 and 7 of the octave
    CHECK (std::fabs (hz (0, 5, 60) / hzOfKey (65) - 1.0) < 0.01);        // no snap: as it is
    CHECK (std::fabs (hz (scale, 5, 60) / hzOfKey (67) - 1.0) < 0.01);    // 5 is nearer to 7 than to 0
    CHECK (std::fabs (hz (scale, 2, 60) / hzOfKey (60) - 1.0) < 0.01);    // 2 is nearer to 0
    CHECK (std::fabs (hz (scale, 10, 60) / hzOfKey (67 + 0) - 1.0) > 0.005 && std::fabs (hz (scale, 10, 60) / hzOfKey (72) - 1.0) < 0.01);   // 10: the octave (12) is nearer than 7
    CHECK (std::fabs (hz (scale, -5, 60) / hzOfKey (55) - 1.0) < 0.01);   // -5 is the 7 of the octave below
    // global: the played note itself is snapped to the scale: key 61 + transpose 0 -> 60 (the scale has C and G)
    const int globalScale = scale | (1 << 12);
    CHECK (std::fabs (hz (globalScale, 0, 61) / hzOfKey (60) - 1.0) < 0.01);
    CHECK (std::fabs (hz (globalScale, 0, 66) / hzOfKey (67) - 1.0) < 0.01);
    CHECK (std::fabs (hz (scale, 0, 61) / hzOfKey (61) - 1.0) < 0.01);    // local: the transpose is 0 and the key stays
}

TEST (own_osc_key_fold_plays_the_notes_outside_the_range_inside_it)
{
    auto render = [] (int lo, int hi, bool fold, int key) {
        Rig r;
        r.set ("osc1.map.key_low", static_cast<float> (lo));
        r.set ("osc1.map.key_high", static_cast<float> (hi));
        r.set ("osc1.map.key_fold", fold ? 1.0f : 0.0f);
        return r.render ({ { 0, -1, key } }, 20480);
    };
    CHECK (rms (render (36, 47, false, 60).l, 4096, 20480) < 1.0e-6);     // outside the range and no fold: silent
    const Out folded = render (36, 47, true, 60);
    CHECK (rms (folded.l, 4096, 20480) > 0.1);
    CHECK (std::fabs (frequencyOf (folded.l, 4096, 20480) / hzOfKey (36) - 1.0) < 0.01);   // 60 folds down two octaves to 36
    CHECK (std::fabs (frequencyOf (render (36, 47, true, 20).l, 4096, 20480) / hzOfKey (32 + 12) - 1.0) < 0.02);   // 20 folds up to 44
    CHECK (maxDiff (render (36, 47, true, 40), render (36, 47, false, 40), 0, 20480) == 0.0);   // a note inside the range is not touched
    CHECK (rms (render (36, 41, true, 60).l, 4096, 20480) < 1.0e-6);       // a range narrower than an octave cannot take it: no fold
}

TEST (own_osc_key_warp_keeps_an_oscillator_out_of_its_range_running_as_a_warp_source)
{
    const int fm = choiceIndex ("osc1.warp.type", "FM from Osc 2");
    const int none = choiceIndex ("osc2.destination", "None");
    CHECK (fm > 0 && none >= 0);
    auto render = [&] (int osc2High, bool keyWarp, bool osc2Silent) {
        Rig r;
        r.set ("osc1.warp.type", static_cast<float> (fm));
        r.set ("osc1.warp.amount", 80.0f);
        r.set ("osc2.on", 1.0f);
        r.set ("osc2.level", 90.0f);
        r.set ("osc2.wavetable", 0.0f);
        r.set ("osc2.pitch_coarse", 7.0f);
        r.set ("osc2.map.key_high", static_cast<float> (osc2High));
        r.set ("osc2.map.key_warp", keyWarp ? 1.0f : 0.0f);
        if (osc2Silent)
            r.set ("osc2.destination", static_cast<float> (none));
        return r.render ({ { 0, -1, 60 } }, 12288);
    };
    const Out plain = render (50, false, false);          // osc 2 out of range, nothing warps
    const Out reference = render (127, false, true);      // osc 2 in range but routed nowhere: osc 1 warped by a signal nobody hears
    const Out warp = render (50, true, false);            // osc 2 out of range with Key Warp
    CHECK (maxDiff (warp, reference, 2048, 12288) < 1.0e-5);
    CHECK (maxDiff (warp, plain, 2048, 12288) > 0.05);
    CHECK (rms (plain.l, 2048, 12288) > 0.1);
}

TEST (own_osc_phase_memory_contiguous_continues_with_the_phase_of_the_previous_note)
{
    const int t2 = 256 * 22;
    auto pair = [&] (int memory) {
        Rig r;
        r.set ("osc1.phase_memory", static_cast<float> (memory));
        r.set ("osc1.phase", 0.0f);
        r.set ("osc1.random_phase", 0.0f);
        return r.render ({ { 0, -1, 60 }, { t2, -1, 72 } }, 8192);
    };
    auto firstAlone = [&] {
        Rig r;
        r.set ("osc1.random_phase", 0.0f);
        return r.render ({ { 0, -1, 60 } }, 8192);
    };
    auto minus = [] (Out a, const Out& b) { for (size_t i = 0; i < a.l.size(); ++i) { a.l[i] -= b.l[i]; a.r[i] -= b.r[i]; } return a; };
    const Out one = firstAlone();
    auto corr = [&] (const Out& a, const Out& b) {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = static_cast<size_t> (t2) + 100; i < static_cast<size_t> (t2) + 2000; ++i) { ab += static_cast<double> (a.l[i]) * b.l[i]; aa += static_cast<double> (a.l[i]) * a.l[i]; bb += static_cast<double> (b.l[i]) * b.l[i]; }
        return ab / std::sqrt (aa * bb);
    };
    // the first note is at 261.6 Hz and has PHASE 0: its phase when the second note starts
    const double expect = std::fmod (hzOfKey (60) / kSr * t2, 1.0) * 360.0;
    CHECK (expect > 40.0 && expect < 320.0);
    const Out contiguous = minus (pair (1), one);
    // a second note with PHASE = `deg` from All Voices needs the first note at the same PHASE: it is built so that both notes start at the PHASE `deg` and then the first note's own shift (deg) is
    // removed, so the continuation is compared at the point where the first note is at expect: PHASE of the first note = 0, PHASE of the second = expect
    auto allVoicesAt = [&] (double deg) {
        Rig r;
        r.set ("osc1.phase_memory", 0.0f);
        r.set ("osc1.random_phase", 0.0f);
        // two oscillators would be simpler; here the second note's PHASE is the oscillator's PHASE, so the first note is rendered with the same one and taken out with its own render
        r.set ("osc1.phase", static_cast<float> (deg));
        const Out both = r.render ({ { 0, -1, 60 }, { t2, -1, 72 } }, 8192);
        Rig r2;
        r2.set ("osc1.random_phase", 0.0f);
        r2.set ("osc1.phase", static_cast<float> (deg));
        return minus (both, r2.render ({ { 0, -1, 60 } }, 8192));
    };
    const double cBest = corr (contiguous, allVoicesAt (expect)), cLow = corr (contiguous, allVoicesAt (expect - 40.0)), cHigh = corr (contiguous, allVoicesAt (expect + 40.0)), cZero = corr (contiguous, allVoicesAt (0.0));
    CHECK (cBest > 0.97 && cBest > cLow + 0.05 && cBest > cHigh + 0.05 && cBest > cZero + 0.05);
    // All Voices (the default) starts the second note at PHASE 0
    CHECK (corr (minus (pair (0), one), allVoicesAt (0.0)) > 0.99);
}

TEST (own_osc_phase_memory_per_voice_starts_every_note_with_the_same_phase)
{
    auto starts = [] (int memory) {
        Rig r;
        r.set ("osc1.phase_memory", static_cast<float> (memory));
        r.set ("osc1.random_phase", 100.0f);
        const int gap = 256 * 32;
        const Out o = r.render ({ { 0, 2048, 60 }, { gap, gap + 2048, 60 }, { 2 * gap, 2 * gap + 2048, 60 } }, 3 * gap);
        return std::vector<Out> { Out { std::vector<float> (o.l.begin(), o.l.begin() + 400), {} }, Out { std::vector<float> (o.l.begin() + gap, o.l.begin() + gap + 400), {} },
                                  Out { std::vector<float> (o.l.begin() + 2 * gap, o.l.begin() + 2 * gap + 400), {} } };
    };
    // the very first note of an engine fades in a little; the phase is compared as the correlation of the segments (1 = the same phase)
    auto corr = [] (const Out& a, const Out& b) {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 100; i < a.l.size(); ++i) { ab += static_cast<double> (a.l[i]) * b.l[i]; aa += static_cast<double> (a.l[i]) * a.l[i]; bb += static_cast<double> (b.l[i]) * b.l[i]; }
        return ab / std::sqrt (aa * bb);
    };
    const auto per = starts (2);
    CHECK (corr (per[0], per[1]) > 0.98 && corr (per[0], per[2]) > 0.98 && corr (per[1], per[2]) > 0.9999);
    const auto all = starts (0);
    CHECK (corr (all[0], all[1]) < 0.9 && corr (all[1], all[2]) < 0.9);   // the random start phases differ from note to note
}

TEST (own_osc_phase_legato_makes_a_note_continue_the_phase_of_the_previous_note_while_its_key_is_held)
{
    const int t2 = 256 * 20;
    // oscillator switch, global switch (voice.phase_legato), previous key held or released
    auto pair = [&] (bool polyLegato, bool oscLegato, bool voiceLegato, bool firstReleased) {
        Rig r;
        r.set ("master.poly_legato", polyLegato ? 1.0f : 0.0f);
        r.set ("osc1.phase_legato", oscLegato ? 1.0f : 0.0f);
        r.set ("voice.phase_legato", voiceLegato ? 1.0f : 0.0f);
        r.set ("osc1.random_phase", 0.0f);
        r.set ("env1.release", 2000.0f);   // the released first note still sounds (a voice that is active but whose key is up)
        return r.render ({ { 0, firstReleased ? t2 - 256 : -1, 60 }, { t2, -1, 72 } }, 8192);
    };
    const Out off = pair (false, false, false, false);
    const Out osc = pair (false, true, false, false), global = pair (false, false, true, false);
    CHECK (maxDiff (off, osc, 0, static_cast<size_t> (t2)) == 0.0);                    // the first note is the same
    CHECK (maxDiff (off, osc, static_cast<size_t> (t2), 8192) > 0.1);                  // the second one starts at another phase
    CHECK (maxDiff (osc, global, 0, 8192) == 0.0);                                     // the global switch is the same as the switch of the oscillator
    // the key of the first note is up: nothing to continue
    CHECK (maxDiff (pair (false, false, false, true), pair (false, true, true, true), 0, 8192) == 0.0);
    // the continued phase is the one the first note has reached: the sum starts as twice the first voice's value (both voices in phase, the same level)
    const size_t i = static_cast<size_t> (t2);
    const double firstAlone = off.l[i - 1];
    CHECK (std::fabs (static_cast<double> (osc.l[i]) - 2.0 * firstAlone) < 0.15 * std::max (0.2, std::fabs (firstAlone)) + 0.05);
    // with poly legato (the envelopes continue) the same holds
    const Out polyOff = pair (true, false, false, false), polyOn = pair (true, true, false, false);
    CHECK (maxDiff (polyOff, polyOn, static_cast<size_t> (t2), 8192) > 0.1);
}
