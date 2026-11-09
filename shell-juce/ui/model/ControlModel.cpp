// SynthyBoy - UI view-model implementation.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ControlModel.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "synthyboy/modulation.h"

namespace synthyboy::ui
{
KeyRef resolveKey (std::string_view key) noexcept
{
    char buf[96];
    if (key.empty() || key.size() >= sizeof buf)
        return {};
    std::copy (key.begin(), key.end(), buf);
    buf[key.size()] = '\0';

    const ParamInfo* info = findParamByKey (buf);
    if (info == nullptr)
        return {};
    return { info, findParamIndex (info->id) };
}

ControlKind defaultKind (const ParamInfo& info) noexcept
{
    if (info.scale != Scale::stepped)
        return ControlKind::knob;
    return (info.min == 0.0f && info.max == 1.0f) ? ControlKind::toggle : ControlKind::choice;
}

std::string shortName (const ParamInfo& info)
{
    const std::string name = info.name;
    const std::string group = info.group;
    const size_t cut = group.rfind (" / ");
    const std::string last = cut == std::string::npos ? group : group.substr (cut + 3);
    if (! last.empty() && name.size() > last.size() + 1 && name.compare (0, last.size(), last) == 0 && name[last.size()] == ' ')
        return name.substr (last.size() + 1);
    return name;
}

std::string unitText (const ParamInfo& info)
{
    const char* u = unitLabel (info.unit);
    return u != nullptr ? u : "";
}

std::string displayText (const ParamInfo& info, float plain)
{
    char buf[64];
    if (info.scale == Scale::stepped)
    {
        valueToText (info, plain, buf, sizeof buf);
        return buf;
    }

    if (info.unit == Unit::Hz && std::fabs (plain) >= 1000.0f)
    {
        std::snprintf (buf, sizeof buf, "%.2f", static_cast<double> (plain) / 1000.0);
        return std::string (buf) + " kHz";
    }
    valueToText (info, plain, buf, sizeof buf);
    const std::string unit = unitText (info);
    return unit.empty() ? std::string (buf) : std::string (buf) + " " + unit;
}

bool isCutoffReadout (const ParamInfo& info) noexcept
{
    if (info.unit != Unit::Hz || info.scale == Scale::stepped)
        return false;
    const std::string key = info.key;
    for (const char* word : { "cutoff", "center", "_cut", "lpf", "hpf", "freq", "crossover" })
        if (key.find (word) != std::string::npos)
            return true;
    return false;
}

std::string semitoneText (float hz)
{
    const double semis = hz > 0.0f ? 12.0 * std::log2 (static_cast<double> (hz) / 261.6256) : -999.0;
    char buf[48];
    std::snprintf (buf, sizeof buf, "%+.1f st", semis);
    return buf;
}

bool semitoneTextToHz (const std::string& text, float& hz)
{
    std::string t = text;
    while (! t.empty() && std::isspace (static_cast<unsigned char> (t.back()))) t.pop_back();
    for (const char* suffix : { "semitones", "semitone", "semis", "semi", "st" })
    {
        const size_t n = std::strlen (suffix);
        if (t.size() > n && t.compare (t.size() - n, n, suffix) == 0)
        {
            t.erase (t.size() - n);
            break;
        }
    }
    while (! t.empty() && std::isspace (static_cast<unsigned char> (t.back()))) t.pop_back();
    if (t.empty())
        return false;
    char* end = nullptr;
    const double semis = std::strtod (t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || ! std::isfinite (semis) || std::fabs (semis) > 200.0)
        return false;
    hz = static_cast<float> (261.6256 * std::pow (2.0, semis / 12.0));
    return true;
}

std::string displayText (const ParamInfo& info, float plain, bool semitones)
{
    return semitones && isCutoffReadout (info) ? semitoneText (plain) : displayText (info, plain);
}

std::string sourceChipLabel (int sourceChoice)
{
    if (sourceChoice < 1 || sourceChoice > kNumModSources)
        return {};
    std::string key = modSourceTable()[sourceChoice - 1].key;
    if (key.rfind ("macro", 0) == 0)
        return "M" + key.substr (5);
    for (auto& ch : key)
        ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
    return key;
}

int sourceChoiceForKey (std::string_view key) noexcept
{
    for (int i = 0; i < kNumModSources; ++i)
        if (key == modSourceTable()[i].key)
            return i + 1;
    return 0;
}

std::string macroKey (int n)
{
    return "macro." + std::to_string (n);
}

CapState capStateForKey (int paramIndex, EngineCapsMask engineCaps)
{
    CapState s;
    if (paramIndex < 0)
        return s;
    const EngineCapsMask needed = requiredCaps (static_cast<size_t> (paramIndex));
    if (! capsCover (engineCaps, needed))
    {
        s.available = false;
        s.missing = needed & ~engineCaps;
        s.tooltip = kNotAvailableText;
    }
    return s;
}

CapState capStateForValue (int paramIndex, float plain, EngineCapsMask engineCaps)
{
    CapState s;
    if (paramIndex < 0)
        return s;
    const EngineCapsMask needed = requiredCapsForValue (static_cast<size_t> (paramIndex), plain);
    if (! capsCover (engineCaps, needed))
    {
        s.available = false;
        s.missing = needed & ~engineCaps;
        s.tooltip = kNotAvailableText;
    }
    return s;
}

FineZone zoneForOffset (float sidewaysPx, float zoneWidthPx) noexcept
{
    const float d = std::fabs (sidewaysPx);
    if (d < zoneWidthPx)
        return FineZone::normal;
    return d < 2.0f * zoneWidthPx ? FineZone::fine : FineZone::ultra;
}

float zoneFactor (FineZone zone) noexcept
{
    switch (zone)
    {
        case FineZone::fine:   return 0.25f;
        case FineZone::ultra:  return 0.05f;
        case FineZone::normal: break;
    }
    return 1.0f;
}

float applyDragDelta (float normalised, float dyUpPx, FineZone zone, float pixelsForFullRange) noexcept
{
    return std::clamp (normalised + dyUpPx / pixelsForFullRange * zoneFactor (zone), 0.0f, 1.0f);
}

bool isDoubleTap (int msSinceLastTap, float distancePx) noexcept
{
    return msSinceLastTap >= 0 && msSinceLastTap <= kDoubleTapMs && distancePx <= kDoubleTapSlopPx;
}

bool isLongPress (int heldMs, float movedPx) noexcept
{
    return heldMs >= kLongPressMs && movedPx <= kLongPressSlopPx;
}

LayoutMode layoutModeForWidth (int widthPx) noexcept
{
    if (widthPx < 600)
        return LayoutMode::phone;
    return widthPx < 1000 ? LayoutMode::tablet : LayoutMode::wide;
}

int columnsFor (LayoutMode mode) noexcept
{
    switch (mode)
    {
        case LayoutMode::phone:  return 1;
        case LayoutMode::tablet: return 2;
        case LayoutMode::wide:   break;
    }
    return 3;
}

const char* pageLabel (Page page) noexcept
{
    static const char* const labels[kNumPages] = { "OSC", "MIX", "FLT", "ENV", "LFO", "MOD", "FX", "GLB" };
    const int i = static_cast<int> (page);
    return i >= 0 && i < kNumPages ? labels[i] : "";
}
} // namespace synthyboy::ui
