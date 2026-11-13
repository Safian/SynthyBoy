// SynthyBoy - benchmark helper: the heavy patch of the own engine (only parameters that engine-own implements, see
// core/engine-own/own_support.cpp): 3 oscillators with unison and a morph and a warp, both filters, sub and noise, 3 envelopes' worth of
// modulation, several LFOs and mod-matrix routes, and the effects the own engine runs at tier low (chorus, compressor, delay, distortion,
// EQ, FX filter, phaser; reverb and flanger are masked by the tier and are switched on too, so higher tiers include them).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <cstring>
#include <string>

#include "synthyboy/engine.h"
#include "synthyboy/modulation.h"

namespace synthyboy
{
namespace bench
{
inline void ownSet (IEngine& engine, const char* key, float value)
{
    const auto* info = findParamByKey (key);
    if (info == nullptr)
    {
        std::fprintf (stderr, "bench: unknown parameter %s\n", key);
        return;
    }
    if (! engine.supportsParam (info->id))
        std::fprintf (stderr, "bench: the own engine does not implement %s\n", key);
    engine.setParam (info->id, value);
}

inline void ownSlot (IEngine& engine, int index, const char* sourceKey, const char* destinationKey, float amountPercent)
{
    const auto s = modSlotParams (index);
    float source = 0.0f;
    for (size_t k = 0; k < modSourceCount(); ++k)
        if (std::strcmp (modSourceTable()[k].key, sourceKey) == 0)
            source = static_cast<float> (k + 1);
    const auto* destination = findParamByKey (destinationKey);
    engine.setParam (paramTable()[s.source].id, source);
    engine.setParam (paramTable()[s.destination].id, static_cast<float> (modDestinationChoiceOf (static_cast<size_t> (findParamIndex (destination->id)))));
    engine.setParam (paramTable()[s.amount].id, amountPercent);
}

inline void ownHeavyPatch (IEngine& engine)
{
    ownSet (engine, "master.polyphony", 32.0f);
    const float tables[] = { 1.0f, 2.0f, 5.0f };
    for (int i = 1; i <= 3; ++i)
    {
        const std::string k = "osc" + std::to_string (i) + ".";
        ownSet (engine, (k + "on").c_str(), 1.0f);
        ownSet (engine, (k + "wavetable").c_str(), tables[i - 1]);
        ownSet (engine, (k + "level").c_str(), 35.0f);
        ownSet (engine, (k + "unison.voices").c_str(), 7.0f);
        ownSet (engine, (k + "unison.detune").c_str(), 30.0f);
        ownSet (engine, (k + "transpose").c_str(), static_cast<float> ((i - 2) * 12));
        ownSet (engine, (k + "random_phase").c_str(), 100.0f);
    }
    ownSet (engine, "osc1.morph.type", 1.0f);
    ownSet (engine, "osc1.morph.amount", 60.0f);
    ownSet (engine, "osc2.warp.type", 1.0f);
    ownSet (engine, "osc2.warp.amount", 40.0f);
    ownSet (engine, "sub.on", 1.0f);
    ownSet (engine, "noise.on", 1.0f);
    ownSet (engine, "noise.level", 10.0f);
    ownSet (engine, "filter1.model", 2.0f);
    ownSet (engine, "filter1.cutoff", 3000.0f);
    ownSet (engine, "filter1.resonance", 40.0f);
    ownSet (engine, "filter2.on", 1.0f);
    ownSet (engine, "filter2.model", 0.0f);
    ownSet (engine, "filter2.cutoff", 6000.0f);
    ownSet (engine, "filter.routing", 1.0f);
    for (const char* l : { "lfo1", "lfo2", "lfo3", "lfo4" })
        ownSet (engine, (std::string (l) + ".sync").c_str(), 0.0f);
    ownSet (engine, "lfo1.rate", 3.0f);
    ownSet (engine, "lfo2.rate", 0.7f);
    ownSet (engine, "lfo3.rate", 5.0f);
    ownSet (engine, "lfo4.rate", 1.3f);
    ownSet (engine, "env2.attack", 300.0f);
    ownSlot (engine, 0, "lfo1", "filter1.cutoff", 30.0f);
    ownSlot (engine, 1, "lfo2", "osc1.frame", 50.0f);
    ownSlot (engine, 2, "lfo3", "osc2.pan", 40.0f);
    ownSlot (engine, 3, "env2", "filter2.cutoff", 40.0f);
    ownSlot (engine, 4, "velocity", "osc3.level", 30.0f);
    ownSlot (engine, 5, "lfo4", "osc3.frame", 40.0f);
    ownSlot (engine, 6, "note", "filter1.cutoff", 20.0f);
    for (const char* fx : { "chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb" })
        ownSet (engine, (std::string (fx) + ".on").c_str(), 1.0f);
    ownSet (engine, "distortion.drive", 6.0f);
    ownSet (engine, "eq.mid.gain", 4.0f);
}
} // namespace bench
} // namespace synthyboy
