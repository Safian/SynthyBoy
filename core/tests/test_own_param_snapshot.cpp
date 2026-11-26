// SynthyBoy - the block snapshot of the own engine follows only the parameters that changed: the sound is bit for bit the one of the full rebuild.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Two engines get the same events and the same parameter changes; one of them rebuilds the whole snapshot (matrix, tuning table, voice caches) on every change
// (kOwnSavingParamDiff: what the engine did before the snapshot followed the changed parameters), the other rebuilds only what the changed parameters can reach.
// Every block of both must have the same bits. The counters of the engine show that the second one really skips the work.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "own_engine.h"
#include "osc/lib_osc.h"
#include "voice/param_index.h"
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
    // quick run: every 3rd parameter (the random values are drawn for all of them, so a parameter gets the value it gets in the full run); SB_TEST_FULL=1: every parameter
    const size_t stride = static_cast<size_t> (sbtest::quickScaled (tier == Tier::low ? "own_param_snapshot every parameter alone, low tier, every Nth parameter" : "own_param_snapshot every parameter alone, high tier, every Nth parameter", 1, 3, 0));
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const ParamInfo& info = paramTable()[i];
        const float value = nextValue (info, rng);
        if (i % stride != 0)
            continue;
        setBothAt (p, info, value);
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

TEST (own_param_snapshot_a_write_of_the_held_value_derives_nothing_again)
{
    Pair p = makePair (Tier::high);
    patch (p);
    chord (p, true);
    for (int i = 0; i < 6; ++i)
        renderBlock (p, {});
    auto deltaOf = [&] (bool ref, const own::OwnEngine::SnapshotStats& before)
    {
        const auto after = statsOf (p, ref);
        own::OwnEngine::SnapshotStats d {};
        d.snapshots = after.snapshots - before.snapshots;
        d.matrixCompiles = after.matrixCompiles - before.matrixCompiles;
        d.tuningComputes = after.tuningComputes - before.tuningComputes;
        d.voiceEpochs = after.voiceEpochs - before.voiceEpochs;
        return d;
    };
    // the value a parameter holds written again, for one key of every consumer of the snapshot: the engine derives nothing again, the reference (kOwnSavingParamDiff) rebuilds as it did
    for (const char* key : { "filter1.cutoff", "reverb.mix", "mod01.amount", "master.tuning.a4", "macro.1", "master.volume", "osc1.level" })
    {
        const ParamInfo* info = findParamByKey (key);
        CHECK (info != nullptr);
        if (info == nullptr)
            continue;
        const float held = p.inc->getParam (info->id);
        setBothAt (p, *info, held);
        const auto bi = statsOf (p, false), br = statsOf (p, true);
        renderBlock (p, {});
        const auto di = deltaOf (false, bi), dr = deltaOf (true, br);
        if (di.snapshots != 0u || di.matrixCompiles != 0u || di.tuningComputes != 0u || di.voiceEpochs != 0u)
            std::printf ("a write of the held value of %s rebuilt: %u snapshots\n", key, di.snapshots);
        CHECK_EQ (di.snapshots, 0u);
        CHECK_EQ (di.matrixCompiles, 0u);
        CHECK_EQ (di.tuningComputes, 0u);
        CHECK_EQ (di.voiceEpochs, 0u);
        CHECK_EQ (dr.snapshots, 1u);
        CHECK_EQ (dr.matrixCompiles, 1u);
        renderBlock (p, {});
    }
    // the same values as a whole preset: a new generation, no changed value
    {
        ParamValues values = ParamValues::defaults();
        for (size_t i = 0; i < paramCount(); ++i)
            values.plain[i] = p.inc->getParam (paramTable()[i].id);
        const auto bi = statsOf (p, false), br = statsOf (p, true);
        p.inc->setPresetParams (values);
        p.ref->setPresetParams (values);
        renderBlock (p, {});
        const auto di = deltaOf (false, bi), dr = deltaOf (true, br);
        CHECK_EQ (di.snapshots, 0u);
        CHECK_EQ (dr.snapshots, 1u);
    }
    // a changed value still rebuilds, and the +0 / -0 pair of one parameter is a change (the bit pattern is compared)
    {
        const ParamInfo* info = findParamByKey ("osc1.tune");
        CHECK (info != nullptr);
        setBothAt (p, *info, 0.0f);
        renderBlock (p, {});
        const float before = p.inc->getParam (info->id);
        uint32_t b0 = 0;
        std::memcpy (&b0, &before, 4);
        const auto bi = statsOf (p, false);
        setBothAt (p, *info, -0.0f);
        renderBlock (p, {});
        const float after = p.inc->getParam (info->id);
        uint32_t b1 = 0;
        std::memcpy (&b1, &after, 4);
        const auto di = deltaOf (false, bi);
        CHECK_EQ (di.snapshots, b0 != b1 ? 1u : 0u);
        setBothAt (p, *findParamByKey ("filter1.cutoff"), 777.0f);
        const auto bj = statsOf (p, false);
        renderBlock (p, {});
        CHECK_EQ (deltaOf (false, bj).snapshots, 1u);
    }
    // writes that change nothing, mixed with blocks that follow real changes, sound as the reference does
    uint64_t rng = 7;
    for (int round = 0; round < 60; ++round)
    {
        const ParamInfo& info = paramTable()[static_cast<size_t> (round * 37) % paramCount()];
        if (round % 3 == 0)
            setBothAt (p, info, nextValue (info, rng));
        else
            setBothAt (p, info, p.inc->getParam (info.id));
        renderBlock (p, {});
    }
    CHECK_EQ (p.mismatches, 0);
}

// ---- the effect only classification (isEffectOnlyIndex in own_engine.cpp): a key that a voice reads must never be classed effect only ---------------------------------------------------------------

namespace
{
/** `key` set to the fraction `f` (0 .. 1) of its range, on both engines; a key that does not exist is recorded (a patch that silently misses a key proves nothing). */
void setFrac (Pair& p, const char* key, float f, std::vector<std::string>& missing)
{
    const ParamInfo* info = findParamByKey (key);
    if (info == nullptr)
    {
        missing.push_back (key);
        return;
    }
    setBothAt (p, *info, info->min + f * (info->max - info->min));
}

void setChoice (Pair& p, const char* key, int v, std::vector<std::string>& missing)
{
    const ParamInfo* info = findParamByKey (key);
    if (info == nullptr)
    {
        missing.push_back (key);
        return;
    }
    setBothAt (p, *info, static_cast<float> (v));
}

void slot (Pair& p, int n, const char* src, const char* dst, float amount, std::vector<std::string>& missing)
{
    char key[40];
    std::snprintf (key, sizeof key, "mod%02d.source", n);
    setChoice (p, key, static_cast<int> (srcChoice (src)), missing);
    std::snprintf (key, sizeof key, "mod%02d.destination", n);
    setChoice (p, key, static_cast<int> (dstChoice (dst)), missing);
    std::snprintf (key, sizeof key, "mod%02d.amount", n);
    setFrac (p, key, 0.5f + 0.5f * amount, missing);
}

/** Patch 2: every voice feature that a sweep of one patch can leave unused: three oscillators with unison and warps, the sub and the noise, two filters in series, free running, keyed and mono LFOs,
    eight slots on voice and global destinations, sends to both buses, effects in all racks. */
void patchHeavyPoly (Pair& p, std::vector<std::string>& missing)
{
    for (int o = 1; o <= 3; ++o)
    {
        char key[48];
        std::snprintf (key, sizeof key, "osc%d.on", o);
        setChoice (p, key, 1, missing);
        std::snprintf (key, sizeof key, "osc%d.unison.voices", o);
        setChoice (p, key, 2 + o, missing);
        std::snprintf (key, sizeof key, "osc%d.unison.detune", o);
        setFrac (p, key, 0.3f, missing);
        std::snprintf (key, sizeof key, "osc%d.warp.type", o);
        setChoice (p, key, o, missing);
        std::snprintf (key, sizeof key, "osc%d.warp.amount", o);
        setFrac (p, key, 0.55f, missing);
        std::snprintf (key, sizeof key, "osc%d.bus%d_send", o, o == 3 ? 2 : 1);
        setFrac (p, key, 0.4f, missing);
        std::snprintf (key, sizeof key, "osc%d.pan", o);
        setFrac (p, key, 0.3f + 0.2f * static_cast<float> (o), missing);
    }
    setChoice (p, "sub.on", 1, missing);
    setChoice (p, "noise.on", 1, missing);
    setFrac (p, "noise.level", 0.3f, missing);
    setChoice (p, "filter1.on", 1, missing);
    setChoice (p, "filter2.on", 1, missing);
    setChoice (p, "filter.routing", 1, missing);
    setFrac (p, "filter1.cutoff", 0.55f, missing);
    setFrac (p, "filter2.cutoff", 0.7f, missing);
    setFrac (p, "filter1.resonance", 0.3f, missing);
    for (int l = 1; l <= 4; ++l)
    {
        char key[48];
        std::snprintf (key, sizeof key, "lfo%d.rate", l);
        setFrac (p, key, 0.2f + 0.1f * static_cast<float> (l), missing);
        std::snprintf (key, sizeof key, "lfo%d.sync_type", l);
        setChoice (p, key, l % 3 == 0 ? 4 : 1, missing);
        std::snprintf (key, sizeof key, "lfo%d.sync", l);
        setChoice (p, key, l == 2 ? 4 : 0, missing);
        std::snprintf (key, sizeof key, "lfo%d.mono", l);
        setChoice (p, key, l == 4 ? 1 : 0, missing);
    }
    slot (p, 1, "lfo1", "filter1.cutoff", 0.5f, missing);
    slot (p, 2, "env2", "osc1.level", -0.4f, missing);
    slot (p, 3, "lfo2", "osc2.warp.amount", 0.6f, missing);
    slot (p, 4, "velocity", "filter2.cutoff", 0.5f, missing);
    slot (p, 5, "lfo3", "osc3.pan", 0.5f, missing);
    slot (p, 6, "macro1", "mod01.amount", 0.6f, missing);
    slot (p, 7, "lfo4", "master.volume", 0.2f, missing);
    slot (p, 8, "env3", "osc1.unison.detune", 0.5f, missing);
    setFrac (p, "macro.1", 0.5f, missing);
    for (const char* on : { "reverb.on", "delay.on", "eq.on", "compressor.on", "chorus.on", "distortion.on", "phaser.on", "flanger.on" })
        setChoice (p, on, 1, missing);
    setChoice (p, "reverb.rack", 1, missing);
    setChoice (p, "delay.rack", 2, missing);
    setFrac (p, "voicectl.length", 0.5f, missing);
}

/** Patch 3: monophonic, legato, portamento, key follow LFO, sub, a tuning scale. */
void patchMonoLegato (Pair& p, std::vector<std::string>& missing)
{
    setChoice (p, "master.polyphony", 1, missing);
    setChoice (p, "master.legato", 1, missing);
    setFrac (p, "master.portamento.time", 0.35f, missing);
    setChoice (p, "osc1.on", 1, missing);
    setChoice (p, "osc2.on", 1, missing);
    setFrac (p, "osc2.tune", 0.55f, missing);
    setChoice (p, "sub.on", 1, missing);
    setChoice (p, "filter1.on", 1, missing);
    setFrac (p, "filter1.cutoff", 0.4f, missing);
    setFrac (p, "filter1.keytrack", 0.5f, missing);
    setChoice (p, "lfo1.sync", 4, missing);
    setChoice (p, "lfo1.sync_type", 1, missing);
    setChoice (p, "master.tuning.scale", 3, missing);
    slot (p, 1, "lfo1", "osc1.tune", 0.3f, missing);
    slot (p, 2, "note", "filter1.cutoff", 0.5f, missing);
    slot (p, 3, "env2", "osc2.level", 0.5f, missing);
    for (const char* on : { "reverb.on", "delay.on", "eq.on" })
        setChoice (p, on, 1, missing);
}

void sweepEffectOnlyKeys (Tier tier, int which)
{
    Pair p = makePair (tier);
    std::vector<std::string> missing;
    if (which == 0)
        patchHeavyPoly (p, missing);
    else
        patchMonoLegato (p, missing);
    for (const std::string& k : missing)
        std::printf ("the patch misses the key %s\n", k.c_str());
    CHECK (missing.empty());
    chord (p, true);
    for (int i = 0; i < 12; ++i)
        renderBlock (p, {});
    float level = 0.0f;
    for (float v : p.li)
        level = std::max (level, std::fabs (v));
    CHECK (level > 1.0e-4f);   // the patch makes sound
    uint64_t rng = 4242 + static_cast<uint64_t> (which);
    const size_t stride = static_cast<size_t> (sbtest::quickScaled ("own_param_snapshot effect only keys under the voice heavy patches, every Nth key", 1, 8, 0));
    size_t moved = 0, effectOnly = 0;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        const ParamInfo& info = paramTable()[i];
        const float random = nextValue (info, rng);
        if (!own::paramIsEffectOnly (i))
            continue;
        ++effectOnly;
        if ((effectOnly - 1) % stride != 0)
            continue;
        ++moved;
        for (const float v : { random, (effectOnly % 2 == 0) ? info.max : info.min })
        {
            setBothAt (p, info, v);
            renderBlock (p, {});
            renderBlock (p, {});
        }
        if (p.mismatches != 0)
        {
            std::printf ("first mismatch after the change of the effect only key %s\n", info.key);
            break;
        }
        if (moved % 40 == 0)
            chord (p, (moved / 40) % 2 == 0);   // notes start and stop in the middle of the sweep
    }
    std::printf ("    %zu effect only keys, %zu moved\n", effectOnly, moved);
    CHECK (effectOnly > 100);
    CHECK_EQ (p.mismatches, 0);
}
} // namespace

TEST (own_param_snapshot_effect_only_keys_under_the_heavy_poly_patch)
{
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
        sweepEffectOnlyKeys (tier, 0);
}

TEST (own_param_snapshot_effect_only_keys_under_the_mono_legato_patch)
{
    for (const Tier tier : { Tier::low, Tier::medium, Tier::high })
        sweepEffectOnlyKeys (tier, 1);
}

TEST (own_param_snapshot_a_key_a_voice_reads_is_never_effect_only)
{
    // (1) the tables the voices read their keys through: every position of ParamIndex and the library oscillator keys are voice visible, whatever their names look like
    size_t withPrefix = 0, effectOnly = 0;
    for (size_t i = 0; i < paramCount(); ++i)
    {
        withPrefix += own::paramHasEffectPrefix (i) ? 1 : 0;
        effectOnly += own::paramIsEffectOnly (i) ? 1 : 0;
    }
    std::printf ("    %zu keys with an effect prefix, %zu of them effect only\n", withPrefix, effectOnly);
    CHECK (effectOnly > 0 && effectOnly <= withPrefix);
    {
        std::vector<int> positions (sizeof (own::ParamIndex) / sizeof (int));
        std::memcpy (positions.data(), &own::paramIndex(), positions.size() * sizeof (int));
        for (int o = 0; o < 3; ++o)
            for (const int idx : own::libKeys (o).idx)
                positions.push_back (idx);
        positions.push_back (findParamIndex (hashKey ("noise.filter")));
        size_t checked = 0;
        for (const int idx : positions)
        {
            if (idx < 0 || static_cast<size_t> (idx) >= paramCount())
                continue;
            ++checked;
            if (own::paramIsEffectOnly (static_cast<size_t> (idx)))
                std::printf ("voice read key %s is classed effect only\n", paramTable()[idx].key);
            CHECK (! own::paramIsEffectOnly (static_cast<size_t> (idx)));
        }
        CHECK (checked > 500);
    }
#ifdef SYNTHYBOY_TEST_SOURCE_DIR
    // (2) a key written out as a string in the voice side sources (a lookup by name that the tables above do not see) is voice visible
    namespace fs = std::filesystem;
    std::set<std::string> literals;
    size_t files = 0;
    for (const char* dir : { "voice", "osc", "matrix", "env_lfo", "dsp" })
    {
        const fs::path d = fs::path (SYNTHYBOY_TEST_SOURCE_DIR) / "core" / "engine-own" / dir;
        if (!fs::exists (d))
            continue;
        for (const auto& entry : fs::directory_iterator (d))
        {
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h")
                continue;
            ++files;
            std::ifstream in (entry.path());
            std::stringstream buffer;
            buffer << in.rdbuf();
            const std::string text = buffer.str();
            for (size_t at = text.find ('"'); at != std::string::npos; at = text.find ('"', at + 1))
            {
                const size_t end = text.find ('"', at + 1);
                if (end == std::string::npos)
                    break;
                const std::string lit = text.substr (at + 1, end - at - 1);
                at = end;
                if (!lit.empty() && lit.size() < 64 && lit.find (' ') == std::string::npos && lit.find ('.') != std::string::npos)
                    literals.insert (lit);
            }
        }
    }
    CHECK (files > 10);
    size_t keys = 0;
    for (const std::string& lit : literals)
    {
        const ParamInfo* info = findParamByKey (lit.c_str());
        if (info == nullptr)
            continue;
        ++keys;
        const int idx = findParamIndex (info->id);
        if (idx >= 0 && own::paramIsEffectOnly (static_cast<size_t> (idx)))
            std::printf ("the voice side sources name the key %s, which is classed effect only\n", lit.c_str());
        CHECK (idx < 0 || ! own::paramIsEffectOnly (static_cast<size_t> (idx)));
    }
    std::printf ("    %zu voice side sources, %zu key literals\n", files, keys);
    CHECK (keys > 0);
#endif
}
