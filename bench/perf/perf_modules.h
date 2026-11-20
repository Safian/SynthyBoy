// SynthyBoy - measurement helper: the catalogue of the modules whose cost tier_bench --modules measures (every effect type, filter model, warp type, the morph,
// unison counts, the extra oscillators, sub and noise). The cases are built from the parameter table only, so a new filter model or warp type enters the catalogue
// without an edit here. Links synthyboy_core (the tables).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"

namespace synthyboy::perf
{
/** One case: the parameters (key, plain value) set on top of the base load; group is fx, filter, warp, morph, unison or osc. */
struct ModuleCase
{
    std::string group, name;
    std::vector<std::pair<std::string, float>> params;
};

/** The catalogue. The base (the default load, nothing switched on) is added by the caller. */
inline std::vector<ModuleCase> moduleCases()
{
    std::vector<ModuleCase> cases;

    // an effect per pool type; its mix at the middle of the range because the engine skips an effect whose mix is exactly 0 (engine.h, kOwnSavingFxMute)
    for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
    {
        const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
        ModuleCase c;
        c.group = "fx";
        c.name = d.name;
        c.params.emplace_back (paramTable()[d.onIndex].key, 1.0f);
        if (const ParamInfo* mix = findParamByKey ((std::string (d.prefix) + ".mix").c_str()))
            c.params.emplace_back (mix->key, mix->min + 0.5f * (mix->max - mix->min));
        cases.push_back (std::move (c));
    }

    // a filter model (the values of bench/own_heavy_patch.h)
    if (const ParamInfo* model = findParamByKey ("filter1.model"))
        for (int m = 0; m < static_cast<int> (model->numChoices); ++m)
        {
            ModuleCase c;
            c.group = "filter";
            c.name = model->choices[m];
            c.params = { { "filter1.on", 1.0f }, { "filter1.model", static_cast<float> (m) }, { "filter1.cutoff", 3000.0f }, { "filter1.resonance", 40.0f } };
            cases.push_back (std::move (c));
        }

    // a warp type (entry 0 is "none")
    if (const ParamInfo* warp = findParamByKey ("osc1.warp.type"))
        for (int w = 1; w < static_cast<int> (warp->numChoices); ++w)
        {
            ModuleCase c;
            c.group = "warp";
            c.name = warp->choices[w];
            c.params = { { "osc1.warp.type", static_cast<float> (w) }, { "osc1.warp.amount", 40.0f } };
            cases.push_back (std::move (c));
        }

    {
        ModuleCase c;
        c.group = "morph";
        c.name = "Spectral morph";
        c.params = { { "osc1.morph.type", 1.0f }, { "osc1.morph.amount", 60.0f } };
        cases.push_back (std::move (c));
    }

    for (int n : { 3, 7, 16 })
    {
        ModuleCase c;
        c.group = "unison";
        c.name = "Unison " + std::to_string (n);
        c.params = { { "osc1.unison.voices", static_cast<float> (n) }, { "osc1.unison.detune", 30.0f } };
        cases.push_back (std::move (c));
    }

    const std::pair<const char*, const char*> extras[] = { { "Osc 2", "osc2.on" }, { "Osc 3", "osc3.on" }, { "Sub", "sub.on" }, { "Noise", "noise.on" } };
    for (const auto& e : extras)
    {
        ModuleCase c;
        c.group = "osc";
        c.name = e.first;
        c.params = { { e.second, 1.0f } };
        cases.push_back (std::move (c));
    }
    return cases;
}
} // namespace synthyboy::perf
