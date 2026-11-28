// MpcKit - the parameter table a plugin gives the kit (IParamSource), the plain description of one parameter (ParamDesc) and, for a plugin that has no table of its own,
// ParamTable: a ready implementation over a constant array of ParamDesc with the three mappings (linear, skewed, stepped), the value texts and the Q-Link stepping rule.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// WHAT THIS IS FOR. The pages of the kit (page_base.h, binder.h) and the Q proxies (q_targets.h) never know what a parameter IS: they ask this interface. A parameter is addressed by its
// INDEX (0 .. count () - 1: the position in the table, which is also the host's parameter number), described by a ParamDesc, and its current value lives in the instance (instance.h:
// IInstance::param (index) returns the plain value, IInstance::writeParam (index, normalised) writes). This header holds only the STATIC facts about the parameters, so one object serves
// every instance of the plugin and every thread: all methods are const, lock free and allocate nothing.
//
// NO DEPENDENCY on the synthesizer engine of SynthyBoy: this header includes only the C++ standard library. SynthyBoy's own adapter (synthyboy_adapter.h) implements the same interface over
// its table.
//
// The conversions (the same as the table of SynthyBoy, docs/ARCHITECTURE.md 4.1, so that a plugin made with the kit behaves like it):
//   linear    plain = min + (max - min) * normalised
//   exp_skew  normalised = pow ((plain - min) / (max - min), skew)   (skew < 1 gives more room to the low end: a time or a frequency)
//   stepped   a choice or an integer: plain 0 .. max, normalised = plain / max; `choices` names them (numChoices names, the range is 0 .. numChoices - 1)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mpckit {

enum class Scale : uint8_t { linear, exp_skew, stepped };
enum class Unit : uint8_t { none, dB, Hz, ms, percent, semitone, cents, beats, degrees, voices };

/** What the kit needs to know about one parameter. Plain data, the strings are constants of the plugin (never freed). */
struct ParamDesc
{
    const char* key = "";            // stable machine name ("level", "filter.cutoff"): never renamed, used by the pages to find the parameter and by the chunk
    const char* name = "";           // the name the host shows ("Level"): at most 31 bytes (the host's buffer, shell-mpc/host_strings.h); shorter reads better on the Q-Link display
    const char* group = "";          // the section, levels separated by " / " ("Filter / Mode")
    float min = 0.0f, max = 1.0f, def = 0.0f;   // plain values
    float skew = 1.0f;               // Scale::exp_skew only
    float interval = 0.0f;           // the snap grid of the plain value, 0 = continuous
    Scale scale = Scale::linear;
    Unit unit = Unit::none;
    const char* const* choices = nullptr;   // Scale::stepped: the names of the values 0 .. numChoices - 1 (null: a plain integer)
    int numChoices = 0;
    int decimals = 1;                // decimals of the text of a continuous parameter (the pages print at most two)
};

/** The label of a unit for the host's unit column ("dB", "Hz", "ms", "%", "st", "ct", "beat", "deg", "voices", "" for none). */
inline const char* unitLabel (Unit u) noexcept
{
    switch (u)
    {
        case Unit::dB: return "dB";
        case Unit::Hz: return "Hz";
        case Unit::ms: return "ms";
        case Unit::percent: return "%";
        case Unit::semitone: return "st";
        case Unit::cents: return "ct";
        case Unit::beats: return "beat";
        case Unit::degrees: return "deg";
        case Unit::voices: return "voices";
        default: return "";
    }
}

/** The rule of the relative Q proxies (shell-mpc/q_step_rule.h): how many values a STEPPED control has (a toggle 2, a list its entries, an integer stepper its range), 0 for a parameter that
    stays an absolute knob. An integer stepper (interval 1, whole numbers) counts as stepped up to kMaxIntegerSteps values; a quantised amount with more values (a percent in steps of 1) is a knob. */
constexpr int kMaxIntegerSteps = 32;
inline int stepCountOf (const ParamDesc& p) noexcept
{
    if (p.scale == Scale::stepped)
        return static_cast<int> (std::lround (p.max - p.min)) + 1;
    if (p.interval == 1.0f && p.max > p.min && std::floor (p.min) == p.min && std::floor (p.max) == p.max)
    {
        const int n = static_cast<int> (std::lround (p.max - p.min)) + 1;
        return n <= kMaxIntegerSteps ? n : 0;
    }
    return 0;
}

/** A value that rounds to zero at `decimals` places prints as 0, never as "-0.0". */
inline double noNegativeZero (double v, int decimals) noexcept
{
    double half = 0.5;
    for (int i = 0; i < decimals; ++i)
        half *= 0.1;
    return std::fabs (v) < half ? 0.0 : v;
}

/** A value as text with its unit for the pages: "35%", "+12 ct", "420 Hz", "1.20 s", "-6.0 dB" (at most 15 characters). Rates under 100 Hz keep their decimals; a value that rounds to zero has no sign;
    a pan (a percentage from -100 to 100 whose key is "pan" or ends in ".pan") reads C, L 50, R 50. It is the same function as ui_bind.h formatValue of SynthyBoy (the choice NAME of a stepped parameter
    is the caller's business: the page prints it, hostText () for the host). */
inline void formatValue (const ParamDesc& pi, float plain, char* out, size_t cap) noexcept
{
    if (out == nullptr || cap == 0)
        return;
    double v = static_cast<double> (plain);
    const int dec = std::min<int> (pi.decimals, 2);
    if (pi.unit == Unit::percent && pi.min < 0.0f && pi.max > 0.0f)
    {
        const size_t n = std::strlen (pi.key);
        if (std::strcmp (pi.key, "pan") == 0 || (n >= 4 && std::strcmp (pi.key + n - 4, ".pan") == 0))
        {
            const double a = std::fabs (v);
            if (a < 0.5)
                std::snprintf (out, cap, "C");
            else
                std::snprintf (out, cap, v < 0.0 ? "L %.0f" : "R %.0f", a);
            return;
        }
    }
    switch (pi.unit)
    {
        case Unit::percent: v = noNegativeZero (v, dec); std::snprintf (out, cap, pi.min < 0.0f ? "%+.*f%%" : "%.*f%%", dec, v); break;
        case Unit::cents: v = noNegativeZero (v, 0); std::snprintf (out, cap, "%+.0f ct", v); break;
        case Unit::semitone: v = noNegativeZero (v, dec); std::snprintf (out, cap, "%+.*f st", dec, v); break;
        case Unit::Hz:
            if (v >= 1000.0) std::snprintf (out, cap, "%.2f kHz", v / 1000.0);
            else if (v < 10.0) std::snprintf (out, cap, "%.2f Hz", v);
            else if (v < 100.0) std::snprintf (out, cap, "%.1f Hz", v);
            else std::snprintf (out, cap, "%.0f Hz", v);
            break;
        case Unit::ms: if (v >= 1000.0) std::snprintf (out, cap, "%.2f s", v / 1000.0); else std::snprintf (out, cap, v < 10.0 ? "%.1f ms" : "%.0f ms", v); break;
        case Unit::dB: v = noNegativeZero (v, 1); std::snprintf (out, cap, "%+.1f dB", v); break;
        case Unit::degrees: v = noNegativeZero (v, 0); std::snprintf (out, cap, "%.0f\xc2\xb0", v); break;
        case Unit::beats: std::snprintf (out, cap, "%.2f beat", v); break;
        case Unit::voices: std::snprintf (out, cap, v == 1.0 ? "%.0f voice" : "%.0f voices", v); break;
        default: v = noNegativeZero (v, dec); std::snprintf (out, cap, "%.*f", dec, v); break;
    }
}

/** The parameters of a plugin, as the kit sees them. Implemented once per plugin (ParamTable below does it for a constant array); all methods are const, lock free, allocation free. */
class IParamSource
{
public:
    virtual ~IParamSource() = default;
    /** The number of parameters the pages and the Q proxies can address: indices 0 .. count () - 1. (The host's own parameter count is larger: it adds "Overlay Instance" and the Q proxies, see q_targets.h.) */
    virtual int count() const noexcept = 0;
    virtual const ParamDesc& desc (int index) const noexcept = 0;
    /** The index of the parameter with this key, -1 when there is none. */
    virtual int indexOfKey (const char* key) const noexcept = 0;
    virtual float toNormalised (int index, float plain) const noexcept = 0;
    virtual float fromNormalised (int index, float normalised) const noexcept = 0;
    /** The plain value clamped to the range and snapped to the interval. */
    virtual float constrain (int index, float plain) const noexcept = 0;
    /** The text a PAGE prints for a value, with its unit (formatValue above: "420 Hz"). */
    virtual void valueText (int index, float plain, char* out, size_t cap) const noexcept = 0;
    /** The text the HOST shows for a value (effGetParamDisplay): the choice name or the number, without the unit (the host has a column for it). Returns the length written. */
    virtual size_t hostText (int index, float plain, char* out, size_t cap) const noexcept = 0;
    /** The number of values of a stepped control, 0 for an absolute knob (stepCountOf: the Q proxies drive a stepped target in steps). */
    virtual int stepCount (int index) const noexcept = 0;

    /** Convenience: the normalised value of a plain one, and the other way, for the pages. */
    double normOf (int index, float plain) const noexcept { return static_cast<double> (toNormalised (index, plain)); }
};

/** A ready IParamSource over a constant array of ParamDesc (the array must outlive the table: a constexpr / static array of the plugin). The mapping code is the one of SynthyBoy's table. */
class ParamTable final : public IParamSource
{
public:
    ParamTable (const ParamDesc* table, int count) noexcept : table_ (table), count_ (count) {}

    int count() const noexcept override { return count_; }
    const ParamDesc& desc (int index) const noexcept override { return table_[std::clamp (index, 0, std::max (0, count_ - 1))]; }
    int indexOfKey (const char* key) const noexcept override
    {
        if (key == nullptr || key[0] == '\0')
            return -1;
        for (int i = 0; i < count_; ++i)
            if (std::strcmp (table_[i].key, key) == 0)
                return i;
        return -1;
    }
    float toNormalised (int index, float plain) const noexcept override
    {
        const ParamDesc& p = desc (index);
        if (p.max <= p.min)
            return 0.0f;
        const double prop = std::clamp ((static_cast<double> (plain) - p.min) / (static_cast<double> (p.max) - p.min), 0.0, 1.0);
        if (p.scale == Scale::exp_skew)
            return static_cast<float> (std::pow (prop, static_cast<double> (p.skew)));
        return static_cast<float> (prop);
    }
    float fromNormalised (int index, float normalised) const noexcept override
    {
        const ParamDesc& p = desc (index);
        const double n = std::isfinite (normalised) ? std::clamp (static_cast<double> (normalised), 0.0, 1.0) : 0.0;
        double prop = n;
        if (p.scale == Scale::exp_skew && p.skew > 0.0f)
            prop = std::pow (n, 1.0 / static_cast<double> (p.skew));
        return static_cast<float> (p.min + (static_cast<double> (p.max) - p.min) * prop);
    }
    float constrain (int index, float plain) const noexcept override
    {
        const ParamDesc& p = desc (index);
        double v = std::isfinite (plain) ? static_cast<double> (plain) : static_cast<double> (p.def);
        v = std::clamp (v, static_cast<double> (p.min), static_cast<double> (p.max));
        if (p.scale == Scale::stepped)
            v = std::round (v);
        else if (p.interval > 0.0f)
            v = p.min + std::round ((v - p.min) / p.interval) * p.interval;
        return static_cast<float> (std::clamp (v, static_cast<double> (p.min), static_cast<double> (p.max)));
    }
    void valueText (int index, float plain, char* out, size_t cap) const noexcept override { formatValue (desc (index), plain, out, cap); }
    size_t hostText (int index, float plain, char* out, size_t cap) const noexcept override
    {
        if (out == nullptr || cap == 0)
            return 0;
        const ParamDesc& p = desc (index);
        int n;
        if (p.scale == Scale::stepped && p.choices != nullptr && p.numChoices > 0)
            n = std::snprintf (out, cap, "%s", p.choices[std::clamp (static_cast<int> (std::lround (plain)), 0, p.numChoices - 1)]);
        else
            n = std::snprintf (out, cap, "%.*f", std::min<int> (p.decimals, 2), static_cast<double> (plain));
        return n < 0 ? 0 : std::min (static_cast<size_t> (n), cap - 1);
    }
    int stepCount (int index) const noexcept override { return stepCountOf (desc (index)); }

private:
    const ParamDesc* table_;
    int count_;
};

} // namespace mpckit
