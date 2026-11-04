// SynthyBoyVST - core: parameter table, normalisation, display text.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/params.h"

#include <cmath>
#include <cstring>

#include "number.h"

namespace synthyboy
{
namespace
{
constexpr ParamInfo kParams[] = {
    { hashKey ("gain"),     "gain",     "Gain",     "Master",     -60.0f,    6.0f,   -12.0f, 2.5f, 0.1f, Scale::exp_skew, Unit::dB, kParamAutomatable },
    { hashKey ("waveform"), "waveform", "Waveform", "Oscillator",   0.0f,    1.0f,     1.0f, 1.0f, 1.0f, Scale::stepped,  Unit::none, kParamAutomatable | kParamStepped },
    { hashKey ("cutoff"),   "cutoff",   "Cutoff",   "Filter",      20.0f, 20000.0f, 8000.0f, 0.3f, 0.0f, Scale::exp_skew, Unit::Hz, kParamAutomatable }
};

constexpr size_t kParamCount = sizeof (kParams) / sizeof (kParams[0]);
static_assert (kParamCount <= kMaxParams, "parameter table larger than kMaxParams");

constexpr const char* kWaveformChoices[] = { "Sine", "Saw" };

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
} // namespace

const ParamInfo* paramTable() noexcept { return kParams; }
size_t paramCount() noexcept { return kParamCount; }

const ParamInfo* findParam (uint32_t id) noexcept
{
    const int i = findParamIndex (id);
    return i < 0 ? nullptr : &kParams[i];
}

int findParamIndex (uint32_t id) noexcept
{
    for (size_t i = 0; i < kParamCount; ++i)
        if (kParams[i].id == id)
            return static_cast<int> (i);
    return -1;
}

const ParamInfo* findParamByKey (const char* key) noexcept
{
    if (key == nullptr)
        return nullptr;
    for (const auto& p : kParams)
        if (std::strcmp (p.key, key) == 0)
            return &p;
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
    return info.id == kParamWaveform ? sizeof (kWaveformChoices) / sizeof (kWaveformChoices[0]) : 0;
}

const char* choiceName (const ParamInfo& info, size_t index) noexcept
{
    return index < choiceCount (info) ? kWaveformChoices[index] : nullptr;
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
    return number::formatFixed1 (clampRange (info, plain), buf, cap);
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
