// SynthyBoy - core tests: the keys of block B22 (the pool of effect instances) and the type descriptor table
// (docs/ui-design/13-new-keys.md 4.23, docs/ui-design/18-FX-INSTANCES.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "synthyboy/engine_caps.h"
#include "synthyboy/fx_types.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"
#include "synthyboy/state.h"
#include "test_runner.h"

using namespace synthyboy;

namespace
{
int positionOf (const char* key)
{
    const ParamInfo* info = findParamByKey (key);
    return info != nullptr ? findParamIndex (info->id) : -1;
}
} // namespace

TEST (fxi_block_b22_is_28_keys_per_slot_behind_b21)
{
    CHECK_EQ (positionOf ("fxi01.type"), 1603);
    for (int s = 0; s < kNumFxInstances; ++s)
    {
        const FxInstanceKeys& k = fxInstanceKeys (s);
        const int first = 1603 + 28 * s;
        CHECK_EQ (k.type, first);
        CHECK_EQ (k.on, first + 1);
        CHECK_EQ (k.rack, first + 2);
        CHECK_EQ (k.order, first + 3);
        for (int p = 0; p < kFxInstanceParams; ++p)
            CHECK_EQ (k.p[p], first + 4 + p);
    }
    CHECK_EQ (fxInstanceKeys (-1).type, -1);
    CHECK_EQ (fxInstanceKeys (kNumFxInstances).p[0], -1);

    for (size_t i = 0; i < paramCount(); ++i)
    {
        int slot = -1, number = -2;
        const bool isSlotKey = fxInstanceOfParam (i, slot, number);
        CHECK_EQ (isSlotKey, i >= 1603 && i < 1603 + 16 * 28); // (the keys behind block B22 belong to later blocks)
        if (isSlotKey)
        {
            CHECK_EQ (slot, static_cast<int> (i - 1603) / 28);
            CHECK_EQ (number, static_cast<int> (i - 1603) % 28 < 4 ? -1 : static_cast<int> (i - 1603) % 28 - 4);
        }
    }
}

TEST (fxi_defaults_are_neutral_and_the_names_generic)
{
    for (int s = 0; s < kNumFxInstances; ++s)
    {
        const FxInstanceKeys& k = fxInstanceKeys (s);
        const auto& type = paramTable()[k.type];
        CHECK_EQ (type.def, 0.0f);                               // no module
        CHECK_EQ (choiceCount (type), static_cast<size_t> (kNumFxInstanceTypes));
        CHECK_EQ (paramTable()[k.on].def, 0.0f);
        CHECK_EQ (paramTable()[k.rack].def, 0.0f);               // Main
        CHECK_EQ (choiceCount (paramTable()[k.rack]), static_cast<size_t> (3)); // Main, Bus 1, Bus 2: the pool has no band racks
        CHECK_EQ (paramTable()[k.order].def, static_cast<float> (kFxOrderMax));
        CHECK_EQ (paramTable()[k.order].max, static_cast<float> (kFxOrderMax));
        for (int p = 0; p < kFxInstanceParams; ++p)
        {
            const auto& info = paramTable()[k.p[p]];
            CHECK_EQ (info.min, 0.0f);
            CHECK_EQ (info.max, 1.0f);
            CHECK_EQ (info.def, 0.0f);
            CHECK ((info.flags & kParamModulatable) != 0);       // modulatable when the source is (a source that is not leaves the destination unwired)
            char name[40];
            std::snprintf (name, sizeof name, "FX Slot %02d P%02d", s + 1, p + 1);
            CHECK (std::strcmp (info.name, name) == 0);
        }
    }
    // the type names are the ones of the modules
    CHECK (std::strcmp (choiceName (paramTable()[fxInstanceKeys (0).type], 0), "None") == 0);
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
        CHECK (std::strcmp (choiceName (paramTable()[fxInstanceKeys (5).type], static_cast<size_t> (t)), fxTypeName (t)) == 0);
}

TEST (fxi_descriptor_maps_every_type_to_its_own_keys)
{
    // parameters per type: all of them fit into the 24 of a slot (decision P = 24: with 16 the compressor (24), the reverb (19) and the FX filter (18) did not)
    const int expected[kNumFxInstanceTypes] = { 0, 12, 24, 14, 9, 18, 18, 9, 11, 20, 8, 10, 14, 13 }; // (EQ 18 with eq.mid2.* and eq.law: B34; delay 14 with delay.offset_left, reverb 20 with reverb.feedback: B25)
    int total = 0;
    for (int t = 0; t < kNumFxInstanceTypes; ++t)
    {
        const FxTypeDescriptor& d = fxTypeDescriptor (t);
        CHECK_EQ (d.count, expected[t]);
        CHECK_EQ (d.unfit, 0);
        total += d.count;
        if (t == 0)
            continue;
        CHECK_EQ (d.module, t - 1);
        CHECK (d.onIndex >= 0 && d.rackIndex >= 0);
        CHECK (std::strcmp (paramTable()[d.onIndex].key, (std::string (d.prefix) + ".on").c_str()) == 0);
        CHECK (std::strcmp (paramTable()[d.rackIndex].key, (std::string (d.prefix) + ".rack").c_str()) == 0);

        std::set<int> seen;
        int previous = -1;
        for (int k = 0; k < d.count; ++k)
        {
            const int source = d.source[k];
            CHECK (source > previous);                           // table order
            previous = source;
            CHECK (seen.insert (source).second);
            const std::string key = paramTable()[source].key;
            CHECK (key.compare (0, std::strlen (d.prefix) + 1, std::string (d.prefix) + ".") == 0);
            CHECK (key != std::string (d.prefix) + ".on" && key != std::string (d.prefix) + ".rack");
            CHECK (key != "reverb.force_enable");                // a switch of the whole engine
            CHECK (fxInstanceSource (t, k) == &paramTable()[source]);
        }
        for (int k = d.count; k < kFxInstanceParams; ++k)
        {
            CHECK_EQ (d.source[k], -1);
            CHECK (fxInstanceSource (t, k) == nullptr);
        }
    }
    CHECK_EQ (total, 12 + 24 + 14 + 9 + 18 + 18 + 9 + 11 + 20 + 8 + 10 + 14 + 13);

    // every key of every type of instance 0 is covered (nothing but .on, .rack and the reverb's force switch is left out)
    std::set<int> covered;
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
        for (int k = 0; k < fxTypeDescriptor (t).count; ++k)
            covered.insert (fxTypeDescriptor (t).source[k]);
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
    {
        const std::string prefix = std::string (fxTypeDescriptor (t).prefix) + ".";
        for (size_t i = 0; i < paramCount(); ++i)
        {
            const std::string key = paramTable()[i].key;
            if (key.compare (0, prefix.size(), prefix) != 0 || key == prefix + "on" || key == prefix + "rack" || key == "reverb.force_enable")
                continue;
            if (! CHECK (covered.count (static_cast<int> (i)) == 1))
                std::printf ("    %s is not mapped\n", key.c_str());
        }
    }

    // the splitters are not in the pool, an unknown type is "none"
    CHECK_EQ (fxTypeDescriptor (kNumFxInstanceTypes).count, 0);
    CHECK_EQ (fxTypeDescriptor (-3).count, 0);
}

TEST (fxi_normalisation_uses_the_parameter_info_of_the_source)
{
    for (int t = 1; t < kNumFxInstanceTypes; ++t)
    {
        float defaults[kFxInstanceParams];
        fxInstanceDefaults (t, defaults);
        const FxTypeDescriptor& d = fxTypeDescriptor (t);
        for (int k = 0; k < kFxInstanceParams; ++k)
        {
            if (k >= d.count)
            {
                CHECK_EQ (defaults[k], 0.0f);
                continue;
            }
            const ParamInfo& src = paramTable()[d.source[k]];
            CHECK (defaults[k] >= 0.0f && defaults[k] <= 1.0f);
            // the default of the source comes back (a stepped parameter exactly, a continuous one to float precision)
            const float back = fxInstancePlain (t, k, defaults[k]);
            if (src.scale == Scale::stepped)
                CHECK_EQ (back, src.def);
            else
                CHECK (std::fabs (back - src.def) <= 1.0e-4f * std::max (1.0f, std::fabs (src.def)));
            // the whole range is reachable and monotonic
            CHECK_EQ (fxInstancePlain (t, k, 0.0f), src.min);
            CHECK_EQ (fxInstancePlain (t, k, 1.0f), src.max);
            CHECK (fxInstanceNormalise (t, k, src.max) == 1.0f && fxInstanceNormalise (t, k, src.min) == 0.0f);
        }
    }
    CHECK_EQ (fxInstancePlain (0, 0, 0.5f), 0.0f);
    CHECK_EQ (fxInstanceNormalise (3, kFxInstanceParams, 1.0f), 0.0f);
}

TEST (fxi_keys_need_the_capability_bit_and_the_type_the_modules_bit)
{
    for (int s = 0; s < kNumFxInstances; ++s)
    {
        const FxInstanceKeys& k = fxInstanceKeys (s);
        for (const int index : { k.type, k.on, k.rack, k.order, k.p[0], k.p[23] })
            CHECK_EQ (requiredCaps (static_cast<size_t> (index)), kCapFxInstances);
    }
    const size_t type = static_cast<size_t> (fxInstanceKeys (2).type);
    CHECK_EQ (requiredCapsForValue (type, 0.0f), kCapFxInstances);
    CHECK_EQ (requiredCapsForValue (type, static_cast<float> (kFxTypeChorus)), kCapFxInstances);
    CHECK_EQ (requiredCapsForValue (type, static_cast<float> (kFxTypeBode)), kCapFxInstances | kCapFxBode);
    CHECK_EQ (requiredCapsForValue (type, static_cast<float> (kFxTypeHyperDimension)), kCapFxInstances | kCapFxHyperDimension);
    // a modulation destination that is a slot parameter needs the bit as well
    const size_t dest = static_cast<size_t> (findParamIndex (findParamByKey ("mod01.destination")->id));
    const size_t choice = modDestinationChoiceOf (static_cast<size_t> (fxInstanceKeys (0).p[0]));
    CHECK_EQ (choice, static_cast<size_t> (520));
    CHECK ((requiredCapsForValue (dest, static_cast<float> (choice)) & kCapFxInstances) != 0);
    CHECK_EQ (modDestinationChoiceOf (static_cast<size_t> (fxInstanceKeys (15).p[23])), static_cast<size_t> (903));
}

TEST (fxi_slots_survive_a_state_round_trip)
{
    ParamValues values = ParamValues::defaults();
    const FxInstanceKeys& k = fxInstanceKeys (3);
    values.plain[k.type] = static_cast<float> (kFxTypeReverb);
    values.plain[k.on] = 1.0f;
    values.plain[k.rack] = 2.0f;
    values.plain[k.order] = 321.0f;
    for (int p = 0; p < kFxInstanceParams; ++p)
        values.plain[k.p[p]] = 0.0123f * static_cast<float> (p + 1);
    values.plain[fxInstanceKeys (15).type] = static_cast<float> (kFxTypeConvolve);

    const std::string text = saveState (values);
    ParamValues loaded = ParamValues::defaults();
    const LoadResult result = loadState (text.data(), text.size(), loaded);
    CHECK (result.status == LoadStatus::ok);
    CHECK_EQ (result.applied, static_cast<int> (paramCount()));
    for (size_t i = 0; i < paramCount(); ++i)
        CHECK (std::fabs (loaded.plain[i] - values.plain[i]) <= 1.0e-6f);

    // a state without the keys (every older document) leaves the pool empty
    ParamValues older = ParamValues::defaults();
    const std::string small = "{\"format\":\"synthyboy-state\",\"version\":3,\"params\":{\"master.volume\":-3}}";
    CHECK (loadState (small.data(), small.size(), older).status == LoadStatus::ok);
    for (int s = 0; s < kNumFxInstances; ++s)
        CHECK_EQ (older.plain[fxInstanceKeys (s).type], 0.0f);
}
