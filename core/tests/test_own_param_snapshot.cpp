// SynthyBoy - the block snapshot of the own engine follows only the parameters that changed: the sound is bit for bit the one of the full rebuild.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Two engines get the same events and the same parameter changes; one of them rebuilds the whole snapshot (matrix, tuning table, voice caches) on every change
// (kOwnSavingParamDiff: what the engine did before the snapshot followed the changed parameters), the other rebuilds only what the changed parameters can reach.
// Every block of both must have the same bits. The counters of the engine show that the second one really skips the work.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "own_engine.h"
#include "synthyboy/content_state.h"
#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"
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
    std::unique_ptr<IEngine> inc, ref;
    std::vector<float> li, ri, lr, rr;
    int mismatches = 0;
};

Pair makePair (Tier tier)
{
    Pair p;
    p.inc = createOwnEngine();
    p.ref = createOwnEngine();
    setOwnSavingsOff (*p.ref, kOwnSavingParamDiff);
    for (auto* e : { p.inc.get(), p.ref.get() })
    {
        e->setParam (kParamMasterPolyphony, 16.0f);
        e->prepare (44100.0, kBlock, qualityForTier (tier));
    }
    p.li.assign (kBlock, 0.0f);
    p.ri = p.lr = p.rr = p.li;
    return p;
}

void setBoth (Pair& p, const char* key, float v)
{
    const ParamInfo* info = findParamByKey (key);
    if (info == nullptr)
        return;
    p.inc->setParam (info->id, v);
    p.ref->setParam (info->id, v);
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
    const ParamInfo* info = findParamByKey (key);
    return info != nullptr ? static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (info->id)))) : 0.0f;
}

/** A patch that makes every consumer of the snapshot do something: voice routes, a slot that modulates another slot's amount, a slot with a slew, two effects, a tuning scale. */
void patch (Pair& p)
{
    setBoth (p, "osc1.on", 1.0f);
    setBoth (p, "osc2.on", 1.0f);
    setBoth (p, "osc1.unison.voices", 3.0f);
    setBoth (p, "filter1.on", 1.0f);
    setBoth (p, "filter1.cutoff", 1500.0f);
    setBoth (p, "mod01.source", srcChoice ("lfo1"));
    setBoth (p, "mod01.destination", dstChoice ("filter1.cutoff"));
    setBoth (p, "mod01.amount", 45.0f);
    setBoth (p, "mod02.source", srcChoice ("env2"));
    setBoth (p, "mod02.destination", dstChoice ("osc1.level"));
    setBoth (p, "mod02.amount", -30.0f);
    setBoth (p, "mod03.source", srcChoice ("macro1"));
    setBoth (p, "mod03.destination", dstChoice ("mod01.amount"));
    setBoth (p, "mod03.amount", 60.0f);
    setBoth (p, "mod04.source", srcChoice ("velocity"));
    setBoth (p, "mod04.destination", dstChoice ("master.volume"));
    setBoth (p, "mod04.amount", 20.0f);
    setBoth (p, "mod05.source", srcChoice ("lfo2"));
    setBoth (p, "mod05.destination", dstChoice ("lfo1.rate"));
    setBoth (p, "mod05.amount", 35.0f);
    setBoth (p, "mod05.rise", 40.0f);
    setBoth (p, "macro.1", 50.0f);
    setBoth (p, "reverb.on", 1.0f);
    setBoth (p, "reverb.mix", 30.0f);
    setBoth (p, "chorus.on", 1.0f);
    setBoth (p, "chorus.mix", 40.0f);
    setBoth (p, "master.tuning.scale", 2.0f);
}

void renderBlock (Pair& p, const std::vector<Event>& events)
{
    p.inc->process (events.empty() ? nullptr : events.data(), static_cast<int> (events.size()), p.li.data(), p.ri.data(), kBlock);
    p.ref->process (events.empty() ? nullptr : events.data(), static_cast<int> (events.size()), p.lr.data(), p.rr.data(), kBlock);
    if (std::memcmp (p.li.data(), p.lr.data(), sizeof (float) * kBlock) != 0 || std::memcmp (p.ri.data(), p.rr.data(), sizeof (float) * kBlock) != 0)
        ++p.mismatches;
}

void chord (Pair& p, bool on)
{
    std::vector<Event> ev;
    for (int k : { 48, 55, 60, 64 })
        ev.push_back (on ? sbtest::noteOn (0, k, 0.8f, 0, k) : sbtest::noteOff (0, k));
    renderBlock (p, ev);
}

own::OwnEngine::SnapshotStats statsOf (Pair& p, bool ref)
{
    return dynamic_cast<own::OwnEngine*> (ref ? p.ref.get() : p.inc.get())->snapshotStats();
}

float nextValue (const ParamInfo& info, uint64_t& rng)
{
    rng = rng * 6364136223846793005ull + 1442695040888963407ull;
    const float fr = static_cast<float> ((rng >> 33) % 10001) / 10000.0f;
    return info.min + fr * (info.max - info.min);
}

void setBothAt (Pair& p, const ParamInfo& info, float v)
{
    p.inc->setParam (info.id, v);
    p.ref->setParam (info.id, v);
}

void sweepEveryParameter (Tier tier)
{
    Pair p = makePair (tier);
    patch (p);
    chord (p, true);
    for (int i = 0; i < 8; ++i)
        renderBlock (p, {});
    uint64_t rng = 99;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const ParamInfo& info = paramTable()[i];
        setBothAt (p, info, nextValue (info, rng));
        renderBlock (p, {}); // the block that follows the change
        renderBlock (p, {}); // a steady block after it
        if (p.mismatches != 0)
        {
            std::printf ("first mismatch after the change of %s\n", info.key);
            break;
        }
    }
    CHECK_EQ (p.mismatches, 0);
}
} // namespace

TEST (own_param_snapshot_every_parameter_alone_low)
{
    sweepEveryParameter (Tier::low);
}

TEST (own_param_snapshot_every_parameter_alone_high)
{
    sweepEveryParameter (Tier::high);
}

TEST (own_param_snapshot_moving_parameters_content_and_notes)
{
    for (const Tier tier : { Tier::low, Tier::high })
    {
        Pair p = makePair (tier);
        patch (p);
        uint64_t rng = 7;
        chord (p, true);
        const int blocks = sbtest::instrumented() ? 400 : 1500;
        for (int b = 0; b < blocks; ++b)
        {
            rng = rng * 6364136223846793005ull + 1442695040888963407ull;
            const int n = 1 + static_cast<int> ((rng >> 33) % 3);
            for (int j = 0; j < n; ++j)
            {
                rng = rng * 6364136223846793005ull + 1442695040888963407ull;
                const ParamInfo& info = paramTable()[(rng >> 33) % paramCount()];
                setBothAt (p, info, nextValue (info, rng));
            }
            if (b % 97 == 96)
            {
                const StateContent content;
                p.inc->setContent (content);
                p.ref->setContent (content);
            }
            if (b % 211 == 210)
                chord (p, (b / 211) % 2 == 0);
            else
                renderBlock (p, {});
        }
        CHECK_EQ (p.mismatches, 0);
    }
}

TEST (own_param_snapshot_whole_preset_and_unchanged_values)
{
    Pair p = makePair (Tier::high);
    patch (p);
    chord (p, true);
    for (int i = 0; i < 4; ++i)
        renderBlock (p, {});
    ParamValues values = ParamValues::defaults();
    for (size_t i = 0; i < paramCount(); ++i)
        if (i % 3 != 0)
            values.plain[i] = paramTable()[i].min + 0.37f * (paramTable()[i].max - paramTable()[i].min);
    p.inc->setPresetParams (values);
    p.ref->setPresetParams (values);
    renderBlock (p, {});
    renderBlock (p, {});
    // the same values again: a new generation without a changed parameter
    p.inc->setPresetParams (values);
    p.ref->setPresetParams (values);
    renderBlock (p, {});
    renderBlock (p, {});
    CHECK_EQ (p.mismatches, 0);
}

TEST (own_param_snapshot_rebuilds_only_what_a_changed_parameter_reaches)
{
    Pair p = makePair (Tier::high);
    patch (p);
    chord (p, true);
    for (int i = 0; i < 4; ++i)
        renderBlock (p, {});
    auto change = [&] (const char* key, float v)
    {
        own::OwnEngine::SnapshotStats d {};
        const ParamInfo* info = findParamByKey (key);
        CHECK (info != nullptr);
        if (info == nullptr)
            return d;
        setBothAt (p, *info, v);
        const auto before = statsOf (p, false);
        renderBlock (p, {});
        const auto after = statsOf (p, false);
        d.snapshots = after.snapshots - before.snapshots;
        d.matrixCompiles = after.matrixCompiles - before.matrixCompiles;
        d.tuningComputes = after.tuningComputes - before.tuningComputes;
        d.voiceEpochs = after.voiceEpochs - before.voiceEpochs;
        return d;
    };
    // a voice parameter: no matrix, no tuning table, the voices see a new epoch
    auto d = change ("filter1.cutoff", 900.0f);
    CHECK_EQ (d.snapshots, 1u);
    CHECK_EQ (d.matrixCompiles, 0u);
    CHECK_EQ (d.tuningComputes, 0u);
    CHECK_EQ (d.voiceEpochs, 1u);
    // an effect parameter: the voices keep what they derived
    d = change ("reverb.mix", 55.0f);
    CHECK_EQ (d.snapshots, 1u);
    CHECK_EQ (d.matrixCompiles, 0u);
    CHECK_EQ (d.tuningComputes, 0u);
    CHECK_EQ (d.voiceEpochs, 0u);
    // a matrix slot parameter: the matrix is compiled again
    d = change ("mod01.amount", 10.0f);
    CHECK_EQ (d.matrixCompiles, 1u);
    CHECK_EQ (d.tuningComputes, 0u);
    // a tuning parameter: the table is computed again
    d = change ("master.tuning.a4", 432.0f);
    CHECK_EQ (d.matrixCompiles, 0u);
    CHECK_EQ (d.tuningComputes, 1u);
    // the reference engine rebuilds everything every time
    const auto r0 = statsOf (p, true);
    change ("reverb.mix", 20.0f);
    const auto r1 = statsOf (p, true);
    CHECK_EQ (r1.matrixCompiles - r0.matrixCompiles, 1u);
    CHECK_EQ (r1.tuningComputes - r0.tuningComputes, 1u);
    CHECK_EQ (r1.voiceEpochs - r0.voiceEpochs, 1u);
    CHECK_EQ (p.mismatches, 0);
}
