// SynthyBoy - own engine: the modulation matrix.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mod_matrix.h"

#include <cstring>

#include "../dsp/util.h"

namespace synthyboy
{
namespace own
{
namespace
{
bool startsWith (const char* s, const char* prefix) noexcept
{
    return std::strncmp (s, prefix, std::strlen (prefix)) == 0;
}

/** Per voice parameters: everything the voice reads; the rest (master, effects) is modulated by the global sources only. */
bool isVoiceParam (const ParamInfo& info) noexcept
{
    static const char* const prefixes[] = { "osc", "sub.", "noise.", "filter1.", "filter2.", "env", "lfo", "rlfo", "mod" };
    for (const char* p : prefixes)
        if (startsWith (info.key, p))
            return true;
    return false;
}
} // namespace

ModDomain modDomainOf (const ParamInfo& info) noexcept
{
    const size_t len = std::strlen (info.key);
    if (len > 6 && std::strcmp (info.key + len - 6, ".level") == 0 && info.unit == Unit::percent)
        return ModDomain::quadratic;
    if (info.unit == Unit::Hz && info.min > 0.0f && info.max > 4.0f * info.min)
        return ModDomain::logarithmic;
    return ModDomain::normalised;
}

float modulatedValue (const ParamInfo& info, ModDomain domain, float base, float delta) noexcept
{
    const float range = info.max - info.min;
    if (!(range > 0.0f))
        return base;
    switch (domain)
    {
        case ModDomain::quadratic:
        {
            const float n = std::sqrt (std::max (0.0f, (base - info.min) / range)) + delta;
            return info.min + range * std::min (std::max (n, 0.0f), 1.5f) * std::min (std::max (n, 0.0f), 1.5f);
        }
        case ModDomain::logarithmic:
        {
            const float l = std::log (info.max / info.min);
            const float n = std::clamp (std::log (std::max (base, info.min) / info.min) / l + delta, 0.0f, 1.0f);
            return info.min * std::exp (n * l);
        }
        default:
        {
            float prop = std::clamp ((base - info.min) / range, 0.0f, 1.0f);
            if (info.scale == Scale::exp_skew && info.skew > 0.0f)
                prop = std::pow (prop, info.skew);
            prop = std::clamp (prop + delta, 0.0f, 1.0f);
            if (info.scale == Scale::exp_skew && info.skew > 0.0f)
                prop = std::pow (prop, 1.0f / info.skew);
            return info.min + range * prop;
        }
    }
}

void ModMatrix::compile (const float* base) noexcept
{
    const ParamIndex& pi = paramIndex();
    const ParamInfo* table = paramTable();
    count = 0;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const ModSlotParams& sp = pi.slot[s];
        if (sp.source < 0 || sp.destination < 0 || sp.amount < 0)
            continue;
        const int src = static_cast<int> (base[sp.source] + 0.5f);
        const int dstChoice = static_cast<int> (base[sp.destination] + 0.5f);
        if (src <= 0 || src >= kNumSourceIds || dstChoice <= 0 || (sp.bypass >= 0 && base[sp.bypass] >= 0.5f))
            continue;
        const int dest = modDestinationParamIndex (static_cast<size_t> (dstChoice));
        if (dest < 0)
            continue;
        CompiledSlot& c = slots[count++];
        c.source = src;
        c.dest = dest;
        c.amount = base[sp.amount] * 0.01f;
        c.power = sp.power >= 0 ? base[sp.power] : 0.0f;
        c.bipolar = sp.bipolar >= 0 && base[sp.bipolar] >= 0.5f;
        c.aux = sp.auxSource >= 0 ? std::clamp (static_cast<int> (base[sp.auxSource] + 0.5f), 0, kNumSourceIds - 1) : 0;
        c.auxInv = sp.auxInv >= 0 && base[sp.auxInv] >= 0.5f;
        c.auxPower = sp.auxPower >= 0 ? base[sp.auxPower] : 0.0f;
        c.auxOut = sp.auxOutput >= 0 ? base[sp.auxOutput] * 0.01f : 1.0f;
        c.global = !isVoiceParam (table[dest]);
    }
}

void ModMatrix::evaluate (const float* src, bool global, float* delta, int* touched, int& numTouched, int maxTouched) const noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const CompiledSlot& c = slots[i];
        if (c.global != global || c.amount == 0.0f)
            continue;
        float v = src[c.source];
        if (sourceIsBipolar (c.source))
        {
            v = std::copysign (powerShape (std::fabs (v), c.power), v);
        }
        else
        {
            v = powerShape (clamp01 (v), c.power);
            if (c.bipolar)
                v -= 0.5f;
        }
        float d = v * c.amount;
        if (c.aux != 0)
        {
            float a = src[c.aux];
            a = sourceIsBipolar (c.aux) ? 0.5f + 0.5f * a : clamp01 (a);
            a = powerShape (a, c.auxPower);
            if (c.auxInv)
                a = 1.0f - a;
            d *= a * c.auxOut;
        }
        if (d == 0.0f)
            continue;
        if (delta[c.dest] == 0.0f && numTouched < maxTouched)
            touched[numTouched++] = c.dest;
        delta[c.dest] += d;
    }
}
} // namespace own
} // namespace synthyboy
