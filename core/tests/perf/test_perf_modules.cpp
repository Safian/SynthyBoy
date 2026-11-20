// SynthyBoy - tests of the module catalogue of the benchmarks (bench/perf/perf_modules.h): every case is built from the parameter table, every key resolves.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "perf/perf_modules.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/params.h"
#include "test_runner.h"

using namespace synthyboy::perf;

namespace
{
size_t countGroup (const std::vector<ModuleCase>& cases, const char* group)
{
    size_t n = 0;
    for (const ModuleCase& c : cases)
        if (c.group == group)
            ++n;
    return n;
}
} // namespace

TEST (perf_modules)
{
    const std::vector<ModuleCase> cases = moduleCases();

    int unresolved = 0;
    for (const ModuleCase& c : cases)
        for (const auto& p : c.params)
            if (findParamByKey (p.first.c_str()) == nullptr)
                ++unresolved;
    CHECK_EQ (unresolved, 0);

    CHECK_EQ (countGroup (cases, "fx"), static_cast<size_t> (13));
    CHECK_EQ (countGroup (cases, "fx"), static_cast<size_t> (synthyboy::kNumFxInstanceTypes - 1));
    const ParamInfo* model = findParamByKey ("filter1.model");
    const ParamInfo* warp = findParamByKey ("osc1.warp.type");
    CHECK (model != nullptr);
    CHECK (warp != nullptr);
    if (model != nullptr)
        CHECK_EQ (countGroup (cases, "filter"), static_cast<size_t> (model->numChoices));
    if (warp != nullptr)
        CHECK_EQ (countGroup (cases, "warp"), static_cast<size_t> (warp->numChoices - 1));
    CHECK_EQ (countGroup (cases, "morph"), static_cast<size_t> (1));
    CHECK_EQ (countGroup (cases, "unison"), static_cast<size_t> (3));
    CHECK_EQ (countGroup (cases, "osc"), static_cast<size_t> (4));

    // an effect case switches the module on and puts its mix strictly inside the range (the engine skips an effect whose mix is exactly 0)
    int badMix = 0;
    for (const ModuleCase& c : cases)
    {
        if (c.group != "fx")
            continue;
        for (int t = 1; t < synthyboy::kNumFxInstanceTypes; ++t)
        {
            const synthyboy::FxTypeDescriptor& d = synthyboy::fxTypeDescriptor (t);
            if (c.name != d.name)
                continue;
            const ParamInfo* mix = findParamByKey ((std::string (d.prefix) + ".mix").c_str());
            if (mix == nullptr)
                continue;
            int found = 0;
            for (const auto& p : c.params)
                if (p.first == std::string (d.prefix) + ".mix")
                {
                    ++found;
                    if (! (p.second > mix->min && p.second < mix->max))
                        ++badMix;
                }
            if (found != 1)
                ++badMix;
        }
    }
    CHECK_EQ (badMix, 0);

    std::set<std::string> seen;
    int duplicates = 0;
    for (const ModuleCase& c : cases)
        if (! seen.insert (c.group + "|" + c.name).second)
            ++duplicates;
    CHECK_EQ (duplicates, 0);
}
