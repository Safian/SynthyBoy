// SynthyBoy - own engine tests: the Quantize warp of the library oscillators computes its level count (an exp2 of the warp amount) once per call instead of once per sample
// (core/engine-own/osc/lib_osc.cpp, postWarp); the output equals the per sample computation bit for bit (LibOsc::setLegacyCachesForTest selects the old computation).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "osc/lib_osc.h"
#include "osc/warp.h"
#include "synthyboy/aa_switch.h"
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

/** Sample (1) or Granular (2) oscillator with Quantize on a warp slot at the given amount; two notes, a held amount and then a moved one; returns the output. */
sbtest::Rendered play (int type, int slot, float amount, float amount2, Tier tier)
{
    MemoryContentStore store;
    StateContent content;
    auto e = createOwnEngine();
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
    set (*e, "osc1.unison.voices", 2.0f);
    set (*e, slot == 0 ? "osc1.warp.type" : "osc1.warp2.type", static_cast<float> (own::kWarpQuantize));
    set (*e, slot == 0 ? "osc1.warp.amount" : "osc1.warp2.amount", amount);
    sbtest::Rendered all;
    auto add = [&] (const std::vector<Event>& ev, int frames)
    {
        const auto r = sbtest::render (*e, ev, frames, 256);
        all.left.insert (all.left.end(), r.left.begin(), r.left.end());
        all.right.insert (all.right.end(), r.right.begin(), r.right.end());
    };
    add ({ sbtest::noteOn (0, 57), sbtest::noteOn (0, 64) }, 6000);
    set (*e, slot == 0 ? "osc1.warp.amount" : "osc1.warp2.amount", amount2);
    add ({}, 6000);
    return all;
}

bool same (const sbtest::Rendered& a, const sbtest::Rendered& b)
{
    return a.left.size() == b.left.size() && std::memcmp (a.left.data(), b.left.data(), a.left.size() * sizeof (float)) == 0
        && std::memcmp (a.right.data(), b.right.data(), a.right.size() * sizeof (float)) == 0;
}
} // namespace

TEST (the_quantize_level_count_made_once_per_call_equals_the_one_made_per_sample)
{
    int runs = 0, differ = 0, silent = 0;
    const float amounts[] = { 0.0f, 12.0f, 35.0f, 50.0f, 77.0f, 100.0f };
    for (int type = 1; type <= 2; ++type)
        for (int aaOff = 0; aaOff < 2; ++aaOff)
            for (int tier = 0; tier < 2; ++tier)
                for (int slot = 0; slot < 2; ++slot)
                    for (int ai = 0; ai < 6; ++ai)
                    {
                        aa::setOff (aaOff != 0 ? aa::kLibWarp : 0u);
                        sbtest::Rendered r[2];
                        for (int legacy = 0; legacy < 2; ++legacy)
                        {
                            own::LibOsc::setLegacyCachesForTest (legacy != 0);
                            r[legacy] = play (type, slot, amounts[ai], amounts[(ai + 2) % 6], tier == 0 ? Tier::low : Tier::high);
                        }
                        own::LibOsc::setLegacyCachesForTest (false);
                        aa::setOff (0);
                        ++runs;
                        differ += same (r[0], r[1]) ? 0 : 1;
                        double energy = 0.0;
                        for (float v : r[1].left)
                            energy += static_cast<double> (v) * v;
                        silent += energy > 1.0e-3 ? 0 : 1;
                    }
    std::printf ("    %d renders (Sample and Granular, both warp slots, ADAA on and off, tier low and high, six amounts moved once): %d differ from the per sample level count, %d silent\n", runs, differ, silent);
    CHECK_EQ (differ, 0);
    CHECK_EQ (silent, 0);
}
