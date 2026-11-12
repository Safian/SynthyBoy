// SynthyBoy - own engine: the modulation matrix.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mod_matrix.h"

#include <cstring>
#include <vector>

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

namespace
{
bool endsWith (const char* s, const char* suffix) noexcept
{
    const size_t n = std::strlen (s), m = std::strlen (suffix);
    return n >= m && std::strcmp (s + n - m, suffix) == 0;
}

DestLaw computeLaw (const ParamInfo& info) noexcept
{
    DestLaw law;
    const char* k = info.key;
    const float range = info.max - info.min;
    if (std::strcmp (k, "master.volume") == 0)
    {
        law.kind = DestKind::volume;
        law.width = 7399.44f;
        return law;
    }
    // envelope times: fourth root of seconds
    if (startsWith (k, "env") && info.unit == Unit::ms)
    {
        law.kind = DestKind::quart;
        law.width = (endsWith (k, ".delay") || endsWith (k, ".hold")) ? 1.41421f : 2.37842f;
        return law;
    }
    if (std::strcmp (k, "master.portamento.time") == 0 || endsWith (k, ".smooth_time"))
    {
        law.kind = DestKind::log2Seconds;
        law.width = std::strcmp (k, "master.portamento.time") == 0 ? 14.0f : 14.0f;
        return law;
    }
    if (info.unit == Unit::Hz && (endsWith (k, ".rate")))
    {
        law.kind = DestKind::log2Hz;
        law.width = std::log2 (info.max / info.min);
        return law;
    }
    if (info.unit == Unit::Hz && (endsWith (k, "cutoff") || endsWith (k, ".center")))
    {
        law.kind = DestKind::note;
        law.width = 128.0f;
        return law;
    }
    if ((endsWith (k, ".level") && info.unit == Unit::percent) || endsWith (k, "unison.detune") || (endsWith (k, ".resonance") && startsWith (k, "eq.")))
    {
        law.kind = DestKind::sqrtLevel;
        law.scale = 0.01f;
        law.width = endsWith (k, "unison.detune") ? 10.0f : 1.0f;
        return law;
    }
    law.kind = DestKind::lin;
    switch (info.unit)
    {
        case Unit::percent: law.scale = 0.01f; break;
        case Unit::degrees: law.scale = 1.0f / 360.0f; break;
        case Unit::cents: law.scale = 0.01f; break;
        default: law.scale = 1.0f; break;
    }
    law.width = range * law.scale;
    if (endsWith (k, ".frame") && startsWith (k, "osc"))
    {
        law.scale = 0.63f;
        law.width = 63.0f;
    }
    else if (endsWith (k, ".frame_spread"))
    {
        law.scale = 0.63f;
        law.width = 126.0f;
    }
    return law;
}
} // namespace

const DestLaw& destLawOf (size_t paramIndex) noexcept
{
    static const std::vector<DestLaw> laws = []
    {
        std::vector<DestLaw> v (paramCount());
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = computeLaw (paramTable()[i]);
        return v;
    }();
    return laws[paramIndex < laws.size() ? paramIndex : 0];
}

float modulatedValue (const ParamInfo& info, const DestLaw& law, float base, float delta) noexcept
{
    switch (law.kind)
    {
        case DestKind::volume:
        {
            const float db0 = std::min (base, 6.0f);
            const float c = (db0 <= -60.0f ? 0.0f : (db0 + 80.0f) * (db0 + 80.0f)) + delta;
            return c <= 0.0f ? -120.0f : std::sqrt (c) - 80.0f;
        }
        case DestKind::sqrtLevel:
        {
            const float c = std::sqrt (std::max (base * 0.01f * (law.width > 1.5f ? 100.0f : 1.0f), 0.0f)) + delta; // detune: sqrt of the percent
            const float v = c > 0.0f ? c * c : 0.0f;
            return law.width > 1.5f ? v : v * 100.0f;
        }
        case DestKind::quart:
        {
            const float c = std::sqrt (std::sqrt (std::max (base, 0.0f) * 0.001f)) + delta;
            const float c2 = c > 0.0f ? c * c : 0.0f;
            return c2 * c2 * 1000.0f;
        }
        case DestKind::log2Seconds:
        {
            const float c = std::log2 (std::max (base, 1.0e-3f) * 0.001f) + delta;
            return std::exp2 (std::clamp (c, -30.0f, 10.0f)) * 1000.0f;
        }
        case DestKind::log2Hz:
        {
            const float c = std::log2 (std::max (base, 1.0e-4f)) + delta;
            return std::exp2 (std::clamp (c, -14.0f, 14.0f));
        }
        case DestKind::note:
        {
            const float c = 12.0f * std::log2 (std::max (base, 1.0f) * (1.0f / 8.1757989156f)) + delta;
            return 8.1757989156f * std::exp2 (std::clamp (c, 0.0f, 160.0f) * (1.0f / 12.0f));
        }
        default:
            return base + delta / law.scale;
    }
    (void) info;
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
        c.amount = std::clamp (base[sp.amount] * 0.01f, -1.0f, 1.0f);
        c.power = sp.power >= 0 ? base[sp.power] : 0.0f;
        c.bipolar = sp.bipolar >= 0 && base[sp.bipolar] >= 0.5f;
        c.stereo = sp.stereo >= 0 && base[sp.stereo] >= 0.5f;
        c.aux = sp.auxSource >= 0 ? std::clamp (static_cast<int> (base[sp.auxSource] + 0.5f), 0, kNumSourceIds - 1) : 0;
        c.auxInv = sp.auxInv >= 0 && base[sp.auxInv] >= 0.5f;
        c.auxPower = sp.auxPower >= 0 ? base[sp.auxPower] : 0.0f;
        c.auxOut = sp.auxOutput >= 0 ? base[sp.auxOutput] * 0.01f : 1.0f;
        c.global = !isVoiceParam (table[dest]);
        c.width = destLawOf (static_cast<size_t> (dest)).width;
    }
}

void ModMatrix::evaluate (const float* src, bool global, float* delta, int* touched, int& numTouched, int maxTouched, float* deltaRight) const noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const CompiledSlot& c = slots[i];
        if (c.global != global || c.amount == 0.0f)
            continue;
        const float x = clamp01 (src[c.source]);
        float v;
        if (c.bipolar)
        {
            const float y = 2.0f * x - 1.0f;
            v = 0.5f * std::copysign (powerShape (std::fabs (y), -c.power), y); // swings +-0.5 of the full range
        }
        else
            v = powerShape (x, -c.power);
        float d = v * c.amount * c.width;
        if (c.aux != 0)
        {
            float a = clamp01 (src[c.aux]);
            if (c.auxPower != 0.0f)
                a = powerShape (a, -c.auxPower);
            if (c.auxInv)
                a = 1.0f - a;
            d *= a * c.auxOut;
        }
        if (d == 0.0f)
            continue;
        if (delta[c.dest] == 0.0f && numTouched < maxTouched)
            touched[numTouched++] = c.dest;
        delta[c.dest] += d;
        if (deltaRight != nullptr)
            deltaRight[c.dest] += c.stereo ? -d : d; // the stereo flag turns the right lane around (document 04, 3.2)
    }
}
} // namespace own
} // namespace synthyboy
