// SynthyBoy - core: the effect types of the instance pool and the descriptor table (docs/ui-design/18-FX-INSTANCES.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/fx_types.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace synthyboy
{
namespace
{
struct TypeInfo
{
    const char* name;
    const char* shortName;
    const char* prefix;
};

// index = FxInstanceType
constexpr TypeInfo kTypes[kNumFxInstanceTypes] = {
    { "None", "None", "" },
    { "Chorus", "Chor", "chorus" },
    { "Compressor", "Comp", "compressor" },
    { "Delay", "Delay", "delay" },
    { "Distortion", "Dist", "distortion" },
    { "EQ", "EQ", "eq" },
    { "FX Filter", "Filt", "fxfilter" },
    { "Flanger", "Flg", "flanger" },
    { "Phaser", "Phs", "phaser" },
    { "Reverb", "Rev", "reverb" },
    { "Hyper/Dimension", "HypD", "hyperdim" },
    { "Utility", "Util", "utility" },
    { "Bode", "Bode", "bode" },
    { "Convolve", "Conv", "convolve" },
};

bool startsWith (const char* s, const char* prefix, size_t n) noexcept { return std::strncmp (s, prefix, n) == 0; }

struct Tables
{
    FxTypeDescriptor types[kNumFxInstanceTypes];
    FxInstanceKeys slots[kNumFxInstances];
    FxInstanceKeys none;
    int firstSlotIndex = -1;

    Tables()
    {
        const ParamInfo* table = paramTable();
        const size_t n = paramCount();

        for (int t = 0; t < kNumFxInstanceTypes; ++t)
        {
            FxTypeDescriptor& d = types[t];
            d = FxTypeDescriptor {};
            d.name = kTypes[t].name;
            d.shortName = kTypes[t].shortName;
            d.prefix = kTypes[t].prefix;
            d.module = t > 0 ? t - 1 : -1;
            d.count = 0;
            d.unfit = 0;
            d.onIndex = d.rackIndex = -1;
            for (int& s : d.source)
                s = -1;
            if (t == 0)
                continue;

            const size_t prefixLen = std::strlen (d.prefix);
            for (size_t i = 0; i < n; ++i)
            {
                const char* key = table[i].key;
                if (! startsWith (key, d.prefix, prefixLen) || key[prefixLen] != '.')
                    continue;
                if (std::strcmp (key + prefixLen, ".on") == 0)
                    d.onIndex = static_cast<int> (i);
                else if (std::strcmp (key + prefixLen, ".rack") == 0)
                    d.rackIndex = static_cast<int> (i);
                else if (std::strcmp (key, "reverb.force_enable") == 0)
                    continue; // a switch of the whole engine (the tier mask), not a parameter of one instance
                else if (d.count < kFxInstanceParams)
                    d.source[d.count++] = static_cast<int> (i);
                else
                    ++d.unfit;
            }
        }

        for (int s = 0; s < kNumFxInstances; ++s)
        {
            FxInstanceKeys& k = slots[s];
            char key[32];
            const auto find = [&] (const char* field)
            {
                std::snprintf (key, sizeof key, "fxi%02d.%s", s + 1, field);
                const ParamInfo* info = findParamByKey (key);
                return info != nullptr ? findParamIndex (info->id) : -1;
            };
            k.type = find ("type");
            k.on = find ("on");
            k.rack = find ("rack");
            k.order = find ("order");
            for (int p = 0; p < kFxInstanceParams; ++p)
            {
                char field[8];
                std::snprintf (field, sizeof field, "p%02d", p + 1);
                k.p[p] = find (field);
            }
            if (s == 0)
                firstSlotIndex = k.type;
        }
        none.type = none.on = none.rack = none.order = -1;
        for (int& p : none.p)
            p = -1;
    }
};

const Tables& tables() noexcept
{
    static const Tables t;
    return t;
}
} // namespace

const FxTypeDescriptor& fxTypeDescriptor (int type) noexcept
{
    const Tables& t = tables();
    return t.types[type >= 0 && type < kNumFxInstanceTypes ? type : 0];
}

const FxInstanceKeys& fxInstanceKeys (int slot) noexcept
{
    const Tables& t = tables();
    return slot >= 0 && slot < kNumFxInstances ? t.slots[slot] : t.none;
}

bool fxInstanceOfParam (size_t paramIndex, int& slot, int& number) noexcept
{
    const Tables& t = tables();
    const int first = t.firstSlotIndex;
    if (first < 0)
        return false;
    constexpr int perSlot = 4 + kFxInstanceParams;
    const int rel = static_cast<int> (paramIndex) - first;
    if (rel < 0 || rel >= perSlot * kNumFxInstances)
        return false;
    slot = rel / perSlot;
    const int field = rel % perSlot;
    number = field < 4 ? -1 : field - 4;
    return true;
}

const ParamInfo* fxInstanceSource (int type, int number) noexcept
{
    const FxTypeDescriptor& d = fxTypeDescriptor (type);
    if (number < 0 || number >= d.count)
        return nullptr;
    return &paramTable()[d.source[number]];
}

float fxInstanceNormalise (int type, int number, float plain) noexcept
{
    const ParamInfo* src = fxInstanceSource (type, number);
    return src != nullptr ? toNormalised (*src, plain) : 0.0f;
}

float fxInstancePlain (int type, int number, float normalised) noexcept
{
    const ParamInfo* src = fxInstanceSource (type, number);
    return src != nullptr ? fromNormalised (*src, normalised) : 0.0f;
}

void fxInstanceDefaults (int type, float* out) noexcept
{
    for (int k = 0; k < kFxInstanceParams; ++k)
    {
        const ParamInfo* src = fxInstanceSource (type, k);
        out[k] = src != nullptr ? toNormalised (*src, src->def) : 0.0f;
    }
}

const char* fxTypeName (int type) noexcept
{
    return fxTypeDescriptor (type).name;
}
} // namespace synthyboy
