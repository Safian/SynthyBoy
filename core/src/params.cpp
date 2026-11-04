// SynthyBoy - core: parameter table, normalisation, display text.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/params.h"

#include <cmath>
#include <cstring>

#include "number.h"
#include "synthyboy/modulation.h"

namespace synthyboy
{
namespace
{
float clamp01 (float v) noexcept
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float clampRange (const ParamInfo& info, float v) noexcept
{
    return v < info.min ? info.min : (v > info.max ? info.max : v);
}

/** Snaps to min + k * interval. The grid step is the decimal value of the float interval (0.1f -> 0.1) and the sum is
    formed in double, so grid points such as -12 come out exact. Intervals below 1e-6 are not snapped. */
float snapToInterval (const ParamInfo& info, float v) noexcept
{
    const double step = std::round (static_cast<double> (info.interval) * 1.0e6) / 1.0e6;
    if (step > 0.0)
    {
        const double k = std::floor ((static_cast<double> (v) - static_cast<double> (info.min)) / step + 0.5);
        v = static_cast<float> (static_cast<double> (info.min) + k * step);
    }
    return clampRange (info, v);
}

char lowerAscii (char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char> (c - 'A' + 'a') : c;
}

bool isSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/** id -> table index, sorted by id; built on first use (a fixed array, no allocation). Engines and shells touch it
    during construction, so the audio thread never pays for the first call. */
struct IdIndex
{
    uint32_t id[kMaxParams];
    uint16_t index[kMaxParams];
    size_t count = 0;

    IdIndex()
    {
        const auto* table = paramTable();
        count = paramCount();
        for (size_t i = 0; i < count; ++i)
        {
            // insertion sort (the table is built once, n is a few hundred)
            size_t j = i;
            while (j > 0 && id[j - 1] > table[i].id)
            {
                id[j] = id[j - 1];
                index[j] = index[j - 1];
                --j;
            }
            id[j] = table[i].id;
            index[j] = static_cast<uint16_t> (i);
        }
    }
};

const IdIndex& idIndex() noexcept
{
    static const IdIndex lookup;
    return lookup;
}
} // namespace

const ParamInfo* findParam (uint32_t id) noexcept
{
    const int i = findParamIndex (id);
    return i < 0 ? nullptr : &paramTable()[i];
}

int findParamIndex (uint32_t id) noexcept
{
    const auto& lookup = idIndex();
    size_t lo = 0, hi = lookup.count;
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        if (lookup.id[mid] < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < lookup.count && lookup.id[lo] == id ? static_cast<int> (lookup.index[lo]) : -1;
}

const ParamInfo* findParamByKey (const char* key) noexcept
{
    if (key == nullptr)
        return nullptr;
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        if (std::strcmp (table[i].key, key) == 0)
            return &table[i];
    return nullptr;
}

float toNormalised (const ParamInfo& info, float plain) noexcept
{
    const float proportion = clamp01 ((plain - info.min) / (info.max - info.min));
    if (info.scale == Scale::exp_skew && info.skew != 1.0f)
        return std::pow (proportion, info.skew);
    return proportion;
}

float fromNormalised (const ParamInfo& info, float normalised) noexcept
{
    float proportion = clamp01 (normalised);
    if (info.scale == Scale::exp_skew && info.skew != 1.0f && proportion > 0.0f)
        proportion = std::exp (std::log (proportion) / info.skew);
    return snapToInterval (info, info.min + (info.max - info.min) * proportion);
}

float constrainValue (const ParamInfo& info, float plain) noexcept
{
    if (std::isnan (plain))
        return info.def;
    return snapToInterval (info, plain);
}

size_t choiceCount (const ParamInfo& info) noexcept
{
    switch (info.choiceSet)
    {
        case ChoiceSet::list:           return info.numChoices;
        case ChoiceSet::modSource:      return 1 + modSourceCount();
        case ChoiceSet::modDestination: return 1 + modDestinationCount();
        case ChoiceSet::none:           break;
    }
    return 0;
}

const char* choiceName (const ParamInfo& info, size_t index) noexcept
{
    switch (info.choiceSet)
    {
        case ChoiceSet::list:
            return index < info.numChoices ? info.choices[index] : nullptr;
        case ChoiceSet::modSource:
            if (index == 0)
                return "None";
            return index <= modSourceCount() ? modSourceTable()[index - 1].name : nullptr;
        case ChoiceSet::modDestination:
        {
            if (index == 0)
                return "None";
            const int p = modDestinationParamIndex (index);
            return p >= 0 ? paramTable()[p].name : nullptr;
        }
        case ChoiceSet::none:
            break;
    }
    return nullptr;
}

const char* unitLabel (Unit unit) noexcept
{
    switch (unit)
    {
        case Unit::none:     return "";
        case Unit::dB:       return "dB";
        case Unit::Hz:       return "Hz";
        case Unit::ms:       return "ms";
        case Unit::percent:  return "%";
        case Unit::semitone: return "st";
        case Unit::cents:    return "ct";
        case Unit::beats:    return "beats";
        case Unit::degrees:  return "deg";
        case Unit::voices:   return "v";
    }
    return "";
}

size_t valueToText (const ParamInfo& info, float plain, char* buf, size_t cap) noexcept
{
    if (buf == nullptr || cap == 0)
        return 0;

    if (info.scale == Scale::stepped)
    {
        const float snapped = constrainValue (info, plain);
        const char* name = choiceName (info, static_cast<size_t> (snapped));
        if (name != nullptr)
        {
            const size_t n = std::strlen (name);
            const size_t k = n < cap - 1 ? n : cap - 1;
            std::memcpy (buf, name, k);
            buf[k] = '\0';
            return k;
        }
    }

    if (! std::isfinite (plain))
        plain = info.def;
    return number::formatFixed (clampRange (info, plain), info.decimals, buf, cap);
}

bool textToValue (const ParamInfo& info, const char* text, float& plain) noexcept
{
    if (text == nullptr)
        return false;

    while (isSpace (*text))
        ++text;

    if (info.scale == Scale::stepped)
    {
        for (size_t i = 0; i < choiceCount (info); ++i)
        {
            const char* name = choiceName (info, i);
            const char* t = text;
            size_t k = 0;
            while (name[k] != '\0' && lowerAscii (t[k]) == lowerAscii (name[k]))
                ++k;
            if (name[k] == '\0')
            {
                t += k;
                while (isSpace (*t))
                    ++t;
                if (*t == '\0')
                {
                    plain = static_cast<float> (i);
                    return true;
                }
            }
        }
    }

    double v = 0.0;
    const char* next = nullptr;
    if (! number::parseDecimal (text, text + std::strlen (text), false, v, next) || std::isnan (v))
        return false;

    const double lo = static_cast<double> (info.min);
    const double hi = static_cast<double> (info.max);
    float f = static_cast<float> (v < lo ? lo : (v > hi ? hi : v));
    if (info.scale == Scale::stepped)
        f = constrainValue (info, std::floor (f + 0.5f));
    plain = f;
    return true;
}
} // namespace synthyboy
