// SynthyBoy - core: modulation matrix vocabulary (sources, destination list, slot parameters).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/modulation.h"

#include <cstdio>
#include <cstring>

namespace synthyboy
{
namespace
{
constexpr ModSourceInfo kSources[kNumModSources] = {
    { "env1", "Env 1", false },       { "env2", "Env 2", false },       { "env3", "Env 3", false },
    { "env4", "Env 4", false },       { "env5", "Env 5", false },       { "env6", "Env 6", false },
    { "lfo1", "LFO 1", true },        { "lfo2", "LFO 2", true },        { "lfo3", "LFO 3", true },
    { "lfo4", "LFO 4", true },        { "lfo5", "LFO 5", true },        { "lfo6", "LFO 6", true },
    { "lfo7", "LFO 7", true },        { "lfo8", "LFO 8", true },
    { "rlfo1", "Random LFO 1", true }, { "rlfo2", "Random LFO 2", true }, { "rlfo3", "Random LFO 3", true },
    { "rlfo4", "Random LFO 4", true },
    { "macro1", "Macro 1", false },   { "macro2", "Macro 2", false },   { "macro3", "Macro 3", false },
    { "macro4", "Macro 4", false },
    { "velocity", "Velocity", false }, { "note", "Note", false },        { "aftertouch", "Aftertouch", false },
    { "pitch_wheel", "Pitch Wheel", true }, { "mod_wheel", "Mod Wheel", false }, { "random", "Random per Note", false },
    // appended in state version 3 (choice values 29 .. 38), docs/ui-design/13-new-keys.md section 2
    { "macro5", "Macro 5", false },   { "macro6", "Macro 6", false },   { "macro7", "Macro 7", false },   { "macro8", "Macro 8", false },
    { "lfo9", "LFO 9", true },        { "lfo10", "LFO 10", true },
    { "lift", "Lift", false },        { "slide", "Slide", false },      { "oct_note", "Oct Note", false },
    { "poly_aftertouch", "Poly Aftertouch", false }
};

/** Table index of the k-th modulatable parameter (k = 0 ..), computed once on first use (static local: thread safe). */
struct DestinationLists
{
    int paramOfChoice[kMaxParams + 1];   // choice value -> table index (-1 for 0)
    size_t choiceOfParam[kMaxParams];    // table index -> choice value (0 = not a destination)
    size_t count = 0;

    DestinationLists()
    {
        for (auto& v : paramOfChoice)
            v = -1;
        for (auto& v : choiceOfParam)
            v = 0;
        const auto* table = paramTable();
        for (size_t i = 0; i < paramCount(); ++i)
            if ((table[i].flags & kParamModulatable) != 0)
            {
                ++count;
                paramOfChoice[count] = static_cast<int> (i);
                choiceOfParam[i] = count;
            }
    }
};

const DestinationLists& destinations() noexcept
{
    static const DestinationLists lists;
    return lists;
}

int findIndexByKey (const char* key) noexcept
{
    const ParamInfo* info = findParamByKey (key);
    return info != nullptr ? findParamIndex (info->id) : -1;
}
} // namespace

const ModSourceInfo* modSourceTable() noexcept { return kSources; }
size_t modSourceCount() noexcept { return kNumModSources; }

size_t modDestinationCount() noexcept { return destinations().count; }

int modDestinationParamIndex (size_t choiceValue) noexcept
{
    return choiceValue >= 1 && choiceValue <= destinations().count ? destinations().paramOfChoice[choiceValue] : -1;
}

size_t modDestinationChoiceOf (size_t paramIndex) noexcept
{
    return paramIndex < paramCount() ? destinations().choiceOfParam[paramIndex] : 0;
}

ModSlotParams modSlotParams (int slot) noexcept
{
    ModSlotParams p { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    if (slot < 0 || slot >= kNumModSlots)
        return p;

    char key[32];
    const auto find = [&] (const char* field) noexcept
    {
        std::snprintf (key, sizeof key, "mod%02d.%s", slot + 1, field);
        return findIndexByKey (key);
    };
    p.source = find ("source");
    p.destination = find ("destination");
    p.amount = find ("amount");
    p.power = find ("power");
    p.bipolar = find ("bipolar");
    p.stereo = find ("stereo");
    p.bypass = find ("bypass");
    p.auxSource = find ("aux_source");
    p.auxInv = find ("aux_inv");
    p.auxPower = find ("aux_power");
    p.auxOutput = find ("aux_output");
    return p;
}
} // namespace synthyboy
