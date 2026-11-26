// SynthyBoy - the own engine derives the latency plan of the racks (the delays that line the paths up) only when a rack's reported latency moved: the sound and the reported latency are bit for
// bit those of the engine that derives it every period.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// FxChain::update reported "configured" in its settled path every period, because a module can change its latency with the content or the quality without a parameter changing, so the engine
// computed the plan, the five delays and the reported latency every control period. The chain now compares the reported latencies of its rack (reported and not) at the end of every update
// and the engine derives the plan only for a chain that says its latency moved (or when the mixer settings or a chain derivation changed). kOwnSavingLatencyKey off is the old behaviour.
// Two engines get the same events and changes; every block (bits and latencySamples ()) must be the same. A latency that moves without a parameter moving is simulated by
// setRackLatencyOffsetForTest (the modules that exist today change theirs only together with a parameter or the quality, which the full derivation sees).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "own_engine.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/params.h"
#include "synthyboy/quality.h"
#include "synthyboy/state.h"
#include "test_runner.h"
#include "test_util.h"

using namespace synthyboy;

namespace
{
constexpr int kBlock = 128;

struct Pair
{
    std::unique_ptr<IEngine> keyed, plain;   // keyed: the new behaviour, plain: kOwnSavingLatencyKey off (the plan is derived every period)
    std::vector<float> lk, rk, lp, rp;
    int mismatches = 0, latencyMismatches = 0;
    int latencyNow = 0;
};

own::OwnEngine& ownOf (IEngine& e) { return *dynamic_cast<own::OwnEngine*> (&e); }

void setBoth (Pair& p, const char* key, float v)
{
    const ParamInfo* info = findParamByKey (key);
    if (info == nullptr)
        return;
    p.keyed->setParam (info->id, v);
    p.plain->setParam (info->id, v);
}

Pair makePair (Tier tier)
{
    Pair p;
    p.keyed = createOwnEngine();
    p.plain = createOwnEngine();
    setOwnSavingsOff (*p.plain, own::kOwnSavingLatencyKey);
    for (auto* e : { p.keyed.get(), p.plain.get() })
    {
        e->setParam (kParamMasterPolyphony, 8.0f);
        e->prepare (44100.0, kBlock, qualityForTier (tier));
    }
    p.lk.assign (kBlock, 0.0f);
    p.rk = p.lp = p.rp = p.lk;
    // oscillator 1 plays in Main and sends to Bus 1 and Bus 2; Main holds a limiting compressor and a reverb, Bus 1 a distortion (oversampled at tier high: it has a latency), Bus 2 a delay
    setBoth (p, "osc1.on", 1.0f);
    setBoth (p, "osc1.unison.voices", 3.0f);
    setBoth (p, "osc1.bus1_send", 60.0f);
    setBoth (p, "osc1.bus2_send", 40.0f);
    setBoth (p, "compressor.on", 1.0f);
    setBoth (p, "compressor.limit", 1.0f);
    setBoth (p, "compressor.rack", 0.0f);
    setBoth (p, "reverb.on", 1.0f);
    setBoth (p, "reverb.mix", 30.0f);
    setBoth (p, "reverb.rack", 0.0f);
    setBoth (p, "distortion.on", 1.0f);
    setBoth (p, "distortion.drive", 12.0f);
    setBoth (p, "distortion.rack", 1.0f);
    setBoth (p, "delay.on", 1.0f);
    setBoth (p, "delay.mix", 40.0f);
    setBoth (p, "delay.rack", 2.0f);
    return p;
}

void renderBlock (Pair& p, const std::vector<Event>& events)
{
    const Event* ev = events.empty() ? nullptr : events.data();
    const int n = static_cast<int> (events.size());
    p.keyed->process (ev, n, p.lk.data(), p.rk.data(), kBlock);
    p.plain->process (ev, n, p.lp.data(), p.rp.data(), kBlock);
    if (std::memcmp (p.lk.data(), p.lp.data(), sizeof (float) * kBlock) != 0 || std::memcmp (p.rk.data(), p.rp.data(), sizeof (float) * kBlock) != 0)
        ++p.mismatches;
    if (p.keyed->latencySamples() != p.plain->latencySamples())
        ++p.latencyMismatches;
    p.latencyNow = p.keyed->latencySamples();
}

void render (Pair& p, int blocks)
{
    for (int i = 0; i < blocks; ++i)
        renderBlock (p, {});
}

void chord (Pair& p, bool on)
{
    std::vector<Event> ev;
    for (int k : { 48, 55, 60, 64 })
        ev.push_back (on ? sbtest::noteOn (0, k, 0.8f, 0, k) : sbtest::noteOff (0, k));
    renderBlock (p, ev);
}

uint32_t plansOf (Pair& p, bool keyed) { return ownOf (keyed ? *p.keyed : *p.plain).latencyPlanComputes(); }
} // namespace

TEST (own_latency_plan_steady_state_derives_nothing_and_sounds_as_before)
{
    Pair p = makePair (Tier::high);
    chord (p, true);
    render (p, 40);   // the chains settle
    CHECK (p.latencyNow > 0);   // the limiter and the oversampled distortion have a latency: the plan is not trivial
    const uint32_t k0 = plansOf (p, true), p0 = plansOf (p, false);
    render (p, 200);
    const uint32_t keyedPlans = plansOf (p, true) - k0, plainPlans = plansOf (p, false) - p0;
    std::printf ("    steady 200 blocks of %d frames: %u plan derivations (keyed), %u (every period)\n", kBlock, keyedPlans, plainPlans);
    CHECK_EQ (keyedPlans, 0u);
    CHECK (plainPlans >= 200u * (kBlock / own::kControlPeriod));
    CHECK_EQ (p.mismatches, 0);
    CHECK_EQ (p.latencyMismatches, 0);
}

TEST (own_latency_plan_a_latency_that_moves_without_a_parameter_is_picked_up)
{
    Pair p = makePair (Tier::high);
    chord (p, true);
    render (p, 40);
    const int base = p.latencyNow;
    for (int rack = 0; rack < 3; ++rack)
        for (const int offset : { 9, 0, 33, 0 })
        {
            const uint32_t k0 = plansOf (p, true);
            ownOf (*p.keyed).setRackLatencyOffsetForTest (rack, offset);
            ownOf (*p.plain).setRackLatencyOffsetForTest (rack, offset);
            render (p, 12);
            const uint32_t derived = plansOf (p, true) - k0;
            std::printf ("    rack %d, offset %d: reported latency %d (was %d), %u derivation(s)\n", rack, offset, p.latencyNow, base, derived);
            CHECK (derived >= 1u);   // the change was seen
            CHECK (derived <= 3u);   // ... and not derived again every period
            CHECK_EQ (p.mismatches, 0);
            CHECK_EQ (p.latencyMismatches, 0);
        }
    // an offset on a rack that holds an effect that feeds the reported total moves what the host is told
    ownOf (*p.keyed).setRackLatencyOffsetForTest (0, 21);
    ownOf (*p.plain).setRackLatencyOffsetForTest (0, 21);
    render (p, 12);
    CHECK_EQ (p.latencyNow, base + 21);
    CHECK_EQ (p.plain->latencySamples(), base + 21);
    ownOf (*p.keyed).setRackLatencyOffsetForTest (0, 0);
    ownOf (*p.plain).setRackLatencyOffsetForTest (0, 0);
    render (p, 12);
    CHECK_EQ (p.latencyNow, base);
    CHECK_EQ (p.mismatches, 0);
}

TEST (own_latency_plan_parameter_quality_content_and_mixer_changes_sound_as_before)
{
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
    {
        Pair p = makePair (tier);
        chord (p, true);
        render (p, 24);
        // a latency that follows a parameter
        setBoth (p, "compressor.limit", 0.0f);
        render (p, 12);
        setBoth (p, "compressor.limit", 1.0f);
        render (p, 12);
        setBoth (p, "compressor.limit_latency_comp", 0.0f);
        render (p, 12);
        setBoth (p, "compressor.limit_latency_comp", 1.0f);
        render (p, 12);
        setBoth (p, "distortion.on", 0.0f);
        render (p, 12);
        setBoth (p, "distortion.on", 1.0f);
        render (p, 12);
        // the quality (the oversampling of the distortion follows the economy groups), at the same moment on both
        for (const Tier t : { Tier::high, Tier::low, Tier::medium, tier })
        {
            p.keyed->setQuality (qualityForTier (t));
            p.plain->setQuality (qualityForTier (t));
            render (p, 12);
        }
        // the content
        const StateContent content;
        p.keyed->setContent (content);
        p.plain->setContent (content);
        render (p, 12);
        // the mixer: bypasses and destinations change the plan through the mix, not the chains
        setBoth (p, "bus1.fx_bypass", 1.0f);
        render (p, 12);
        setBoth (p, "bus1.fx_bypass", 0.0f);
        setBoth (p, "main.fx_bypass", 1.0f);
        render (p, 12);
        setBoth (p, "main.fx_bypass", 0.0f);
        setBoth (p, "bus1.destination", 2.0f);
        render (p, 12);
        setBoth (p, "bus1.destination", 1.0f);
        render (p, 12);
        setBoth (p, "bus1.destination", 0.0f);
        render (p, 12);
        // the racks of the effects move
        setBoth (p, "reverb.rack", 2.0f);
        render (p, 12);
        setBoth (p, "distortion.rack", 0.0f);
        render (p, 12);
        // a whole preset
        ParamValues values = ParamValues::defaults();
        for (size_t i = 0; i < paramCount(); ++i)
            if (i % 5 != 0)
                values.plain[i] = paramTable()[i].min + 0.41f * (paramTable()[i].max - paramTable()[i].min);
        p.keyed->setPresetParams (values);
        p.plain->setPresetParams (values);
        render (p, 24);
        chord (p, false);
        render (p, 24);
        CHECK_EQ (p.mismatches, 0);
        CHECK_EQ (p.latencyMismatches, 0);
    }
}
