// SynthyBoy - own engine tests: the CPU optimisations of the library oscillators (docs/release/CPU-REVIEW-CONTENT.md) change no output sample. Each one is compared with the old computation, which stays
// selectable for the test.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "osc/lib_osc.h"
#include "synthyboy/content_state.h"
#include "synthyboy/content_store.h"
#include "synthyboy/engine.h"
#include "synthyboy/state.h"
#include "synthyboy/user_content.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr double kSr = 48000.0;

void set (IEngine& e, const char* key, float v)
{
    const ParamInfo* p = findParamByKey (key);
    CHECK (p != nullptr);
    if (p != nullptr)
        e.setParam (p->id, v);
}

std::vector<uint8_t> toneWav()
{
    const size_t n = static_cast<size_t> (44100 * 1.5);
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * 220.0 * static_cast<double> (i) / 44100.0) + 0.2 * std::sin (6.283185307179586 * 331.0 * static_cast<double> (i) / 44100.0)
                                   + 0.05 * std::sin (0.37 * static_cast<double> (i * i % 4001)));
    return encodeWav (x.data(), nullptr, n, 44100, 16);
}

/** A rig with one sample on oscillator 1 and the oscillator's type; renders in segments with the keys moved in between. */
struct Rig
{
    MemoryContentStore store;
    StateContent content;
    std::unique_ptr<IEngine> e;
    sbtest::Rendered all;

    explicit Rig (int type, Tier tier = Tier::low)
    {
        e = createOwnEngine();
        e->setParam (kParamMasterPolyphony, 8.0f);
        Quality q = qualityForTier (tier);
        q.realtime = false;
        e->prepare (kSr, 256, q);
        set (*e, "filter1.on", 0.0f);
        set (*e, "master.volume", 0.0f);
        set (*e, "env1.attack", 0.0f);
        set (*e, "osc1.level", 100.0f);
        set (*e, "osc1.random_phase", 0.0f);
        set (*e, "osc1.type", static_cast<float> (type));
        store.add ("test", "tone.wav", toneWav());
        ContentFileRef ref;
        ref.pack = "test";
        ref.file = "tone.wav";
        ref.name = "tone";
        attachSample (content, 1, ref, 44100, 66150, 1);
        e->setContentStore (&store);
        e->setContent (content);
        set (*e, "osc1.sample.root", 57.0f);
        set (*e, "osc1.sample.loop_mode", 1.0f);
        set (*e, "osc1.unison.voices", 3.0f);
    }
    void render (const std::vector<Event>& events, int frames)
    {
        const auto r = sbtest::render (*e, events, frames, 256);
        all.left.insert (all.left.end(), r.left.begin(), r.left.end());
        all.right.insert (all.right.end(), r.right.begin(), r.right.end());
    }
};

bool same (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    return a.left.size() == b.left.size() && std::memcmp (a.left.data(), b.left.data(), a.left.size() * sizeof (float)) == 0
        && std::memcmp (a.right.data(), b.right.data(), a.right.size() * sizeof (float)) == 0;
}

/** The same patch and gestures for a type: a chord, the unison, pan, spread, detune curve and tuning moved between segments (held for some, so that the caches hit). */
sbtest::Rendered play (int type, Tier tier)
{
    Rig r (type, tier);
    r.render ({ sbtest::noteOn (0, 57), sbtest::noteOn (0, 64) }, 4096);
    for (int step = 0; step < 10; ++step)
    {
        const float f = static_cast<float> (step / 2);   // each setting is held for two segments
        set (*r.e, "osc1.pan", -40.0f + 20.0f * f);
        set (*r.e, "osc1.unison.spread", 20.0f + 15.0f * f);
        set (*r.e, "osc1.unison.detune", 10.0f + 12.0f * f);
        set (*r.e, "osc1.unison.detune_curve", -50.0f + 25.0f * f);
        set (*r.e, "osc1.unison.blend", 30.0f + 10.0f * f);
        set (*r.e, "osc1.transpose", step < 6 ? 0.0f : 7.0f);
        r.render (step == 5 ? std::vector<Event> { sbtest::noteOn (0, 69) } : std::vector<Event> {}, 3000);
    }
    return r.all;
}
} // namespace

// LU1: the library oscillator's update (pan law, detune curve, spread pan, pitch ratios cached by their inputs) against the update that computes them every control period. Sample, Granular and Spectral.
TEST (own_library_oscillator_update_caches_equal_the_per_period_computation)
{
    for (int type = 1; type <= 3; ++type)
    {
        sbtest::Rendered r[2];
        for (int legacy = 0; legacy < 2; ++legacy)
        {
            own::LibOsc::setLegacyCachesForTest (legacy != 0);
            r[legacy] = play (type, Tier::low);
        }
        own::LibOsc::setLegacyCachesForTest (false);
        CHECK (same (r[0], r[1]));
        double energy = 0.0;
        for (float v : r[0].left)
            energy += static_cast<double> (v) * v;
        CHECK (energy > 1.0e-3);   // it sounds
        std::printf ("    type %d: %zu frames identical with the per period computation\n", type, r[0].left.size());
    }
}
