// SynthyBoy - own engine tests: the oscillator options the pages of the type "OSCILLATOR OPTIONS" write: fixed pitch, transpose snap, key fold, key warp, phase memory, phase legato.
// Each has a test of what it does, and the default of each is proved to leave today's render bit identical (a checksum of the rendered floats taken before the options existed).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
    std::printf ("golden saw %016llx polylegato %016llx untracked %016llx two %016llx ranges %016llx\n", static_cast<unsigned long long> (fnv (scenarioSaw())),
                 static_cast<unsigned long long> (fnv (scenarioPolyLegato())), static_cast<unsigned long long> (fnv (scenarioUntracked())),
                 static_cast<unsigned long long> (fnv (scenarioTwoOscillators())), static_cast<unsigned long long> (fnv (scenarioRanges())));
    CHECK (fnv (scenarioSaw()) == kGoldenSaw);
    CHECK (fnv (scenarioPolyLegato()) == kGoldenPolyLegato);
    CHECK (fnv (scenarioUntracked()) == kGoldenUntracked);
    CHECK (fnv (scenarioTwoOscillators()) == kGoldenTwo);
    CHECK (fnv (scenarioRanges()) == kGoldenRanges);
}
