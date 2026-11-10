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
    { "poly_aftertouch", "Poly Aftertouch", false },
    // appended in block B20 (choice value 39, G4)
    { "stereo", "Stereo", true },
    // appended in block B26 (GROUPA2, WP-07; choice values 40 .. 42): the per voice values of the voice control
    { "voice_index", "Voice Index", false }, { "voice_mod1", "Voice Mod 1", true }, { "voice_mod2", "Voice Mod 2", true },
    // appended by GROUPB WP-06 (13-new-keys.md 4.34; choice values 43 .. 57): the output of the sound sources, and the sources that are not a control
    { "osc_a", "Oscillator A", true }, { "osc_b", "Oscillator B", true }, { "osc_c", "Oscillator C", true }, { "noise", "Noise", true }, { "sub", "Sub", true },
    { "filter1", "Filter 1", true }, { "filter2", "Filter 2", true },
    { "active_voices", "Active Voices", false }, { "note_on_alt1", "Note-On Alt 1", false }, { "note_on_alt2", "Note-On Alt 2", false },
    { "note_on_rand2", "Note-On Rand 2", false }, { "note_on_rand_discrete", "Note-On Rand (Discrete)", false }, { "release_velocity", "Release Velocity", false },
    { "expression_x", "Expression X", true }, { "fixed", "Fixed", false }
};

int findIndexByKey (const char* key) noexcept
{
    const ParamInfo* info = findParamByKey (key);
    return info != nullptr ? findParamIndex (info->id) : -1;
}

constexpr size_t kLastIndexOfB20 = 1595; // master.voice_override, the last key of block B20

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
        const auto add = [&] (int p) noexcept
        {
            if (p >= 0 && choiceOfParam[p] == 0)
            {
                ++count;
                paramOfChoice[count] = p;
                choiceOfParam[p] = count;
            }
        };
        for (size_t i = 0; i < paramCount(); ++i)
        {
            if ((table[i].flags & kParamModulatable) != 0)
                add (static_cast<int> (i));
            // block B20 (G1, G3): the appended destinations follow the last modulatable key of the table as it was at B20, so the choice
            // numbers (stored in presets) of every later modulatable key are stable: 454 .. 517 = mod01.amount .. mod64.amount,
            // 518 = master.portamento.time, 519 = master.polyphony; keys of later blocks continue at 520.
            if (i == kLastIndexOfB20)
            {
                for (int slot = 0; slot < kNumModSlots; ++slot)
                {
                    char key[32];
                    std::snprintf (key, sizeof key, "mod%02d.amount", slot + 1);
                    add (findIndexByKey (key));
                }
                add (findIndexByKey ("master.portamento.time"));
                add (findIndexByKey ("master.polyphony"));
            }
        }
    }
};

const DestinationLists& destinations() noexcept
{
    static const DestinationLists lists;
    return lists;
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
    ModSlotParams p { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
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
    p.rise = find ("rise");
    p.fall = find ("fall");
    p.delay = find ("delay");
    p.delayBeats = find ("delay_beats");
    p.delaySync = find ("delay_sync");
    return p;
}
} // namespace synthyboy
