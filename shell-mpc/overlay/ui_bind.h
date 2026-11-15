// SynthyBoy - MPC overlay: what the plugin-drawn element UIs (env_ui.h, and the ones to come) share when they bind library components to the engine's parameters: the table lookups, the writes through
// IInstanceView (normalised, the plugin tells the host), the text of a value with its unit, and the modulation matrix seen from a parameter (which slots move it) and from a source (which slots it drives).
// Pure: no OS headers, no state. The same code is in osc_ui.h as private members (the oscillator UI was first); the element UIs use this file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "../ui/ui.h"
#include "curve_source.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay { namespace uibind {

inline int indexOfKey (const std::string& key) noexcept
{
    if (key.empty())
        return -1;
    const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key.c_str());
    return p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
}
inline const synthyboy::ParamInfo& info (int idx) noexcept { return synthyboy::paramTable()[idx]; }
inline double normOf (int idx, float plain) noexcept { return synthyboy::toNormalised (info (idx), plain); }
inline void writePlain (IInstanceView& v, int idx, double plain) noexcept
{
    const synthyboy::ParamInfo& i = info (idx);
    v.writeParam (idx, static_cast<float> (synthyboy::toNormalised (i, synthyboy::constrainValue (i, static_cast<float> (plain)))));
}
inline void writeNorm (IInstanceView& v, int idx, double norm) noexcept { v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0))); }

/** A value of a parameter as text with its unit: "35%", "+12 ct", "420 Hz", "1.20 s", "-6.0 dB" (at most 15 characters). */
inline void formatValue (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
{
    using synthyboy::Unit;
    const double v = static_cast<double> (plain);
    const int dec = std::min<int> (pi.decimals, 2);
    switch (pi.unit)
    {
        case Unit::percent: std::snprintf (out, cap, pi.min < 0.0f ? "%+.*f%%" : "%.*f%%", dec, v); break;
        case Unit::cents: std::snprintf (out, cap, "%+.0f ct", v); break;
        case Unit::semitone: std::snprintf (out, cap, "%+.*f st", dec, v); break;
        case Unit::Hz: if (v >= 1000.0) std::snprintf (out, cap, "%.2f kHz", v / 1000.0); else std::snprintf (out, cap, "%.0f Hz", v); break;
        case Unit::ms: if (v >= 1000.0) std::snprintf (out, cap, "%.2f s", v / 1000.0); else std::snprintf (out, cap, v < 10.0 ? "%.1f ms" : "%.0f ms", v); break;
        case Unit::dB: std::snprintf (out, cap, "%+.1f dB", v); break;
        case Unit::degrees: std::snprintf (out, cap, "%.0f\xc2\xb0", v); break;
        case Unit::beats: std::snprintf (out, cap, "%.2f beat", v); break;
        default: std::snprintf (out, cap, "%.*f", dec, v); break;
    }
}

// ---- the modulation matrix
inline const char* sourceName (int choice) noexcept
{
    const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
    return choice >= 1 && choice <= static_cast<int> (synthyboy::modSourceCount()) ? t[choice - 1].name : "";
}
/** The name of a modulation source in at most 6 characters that still says which one it is ("Macro 3" -> "MAC 3", "Random LFO 2" -> "RLFO 2", "Velocity" -> "VEL", the others their first six characters). */
inline void shortSourceName (const char* full, char out[7])
{
    const std::string n = full;
    auto number = [&] { const size_t sp = n.rfind (' '); return sp == std::string::npos ? std::string() : n.substr (sp); };
    std::string s;
    if (n.compare (0, 6, "Macro ") == 0) s = "MAC" + number();
    else if (n.compare (0, 11, "Random LFO ") == 0) s = "RLFO" + number();
    else if (n == "Velocity") s = "VEL";
    else if (n == "Aftertouch") s = "AT";
    else if (n == "Poly Aftertouch") s = "PAT";
    else if (n == "Mod Wheel") s = "MOD WH";
    else if (n == "Pitch Wheel") s = "PITCH";
    else if (n == "Random per Note") s = "RND";
    else s = n;
    std::snprintf (out, 7, "%.6s", s.c_str());
}
/** The modulation source ids (the choice value of modNN.source) of envelope n (1 .. 6) and LFO n (1 .. 10); ui::kNoSource for anything else. */
inline int envSource (int n) noexcept { return n >= 1 && n <= 6 ? n : ui::kNoSource; }
inline int lfoSource (int n) noexcept { return n >= 1 && n <= 8 ? 6 + n : (n == 9 ? 33 : (n == 10 ? 34 : ui::kNoSource)); }

constexpr int kMaxTargetSlots = synthyboy::kNumModSlots;   // the most slots a source can have

struct ModRow { int slot = -1; int source = 0; };

/** The modulations of the parameter `param`: a scan of the 64 slots (the slots whose destination is this parameter). `rows` (may be null) gets the slot of every entry. */
inline ui::ModInput modsOf (const IInstanceView& v, int param, int selected, ModRow* rows)
{
    ui::ModInput mi;
    mi.selectedSource = selected;
    const size_t dest = synthyboy::modDestinationChoiceOf (static_cast<size_t> (param));
    if (dest == 0)
        return mi;
    int n = 0;
    for (int s = 0; s < synthyboy::kNumModSlots && n < ui::ModInput::kMaxMods; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source < 0 || sp.destination < 0 || sp.amount < 0)
            continue;
        const int src = static_cast<int> (std::lround (v.param (sp.source)));
        if (src <= 0 || static_cast<size_t> (std::lround (v.param (sp.destination))) != dest)
            continue;
        const double depth = std::clamp (static_cast<double> (v.param (sp.amount)) / 100.0, -1.0, 1.0);
        const bool bi = sp.bipolar >= 0 && v.param (sp.bipolar) >= 0.5f;
        const bool by = sp.bypass >= 0 && v.param (sp.bypass) >= 0.5f;
        char name[7];
        shortSourceName (sourceName (src), name);
        mi.add (src, name, depth, bi, by);
        if (rows != nullptr)
            rows[n] = { s, src };
        ++n;
    }
    return mi;
}
/** The slot of the matrix that modulates `param` from `source` (-1: none). */
inline int slotOf (const IInstanceView& v, int param, int source) noexcept
{
    ModRow rows[ui::ModInput::kMaxMods];
    const ui::ModInput mi = modsOf (v, param, ui::kNoSource, rows);
    for (int i = 0; i < mi.numMods; ++i)
        if (mi.mods[i].source == source)
            return rows[i].slot;
    return -1;
}
/** The first slot without a source (-1: the matrix is full). */
inline int freeSlot (const IInstanceView& v) noexcept
{
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source >= 0 && std::lround (v.param (sp.source)) == 0)
            return s;
    }
    return -1;
}
/** Every slot whose source is `source`: slots[] gets them in table order; returns the count (at most `max`). */
inline int slotsOfSource (const IInstanceView& v, int source, int* slots, int max) noexcept
{
    int n = 0;
    for (int s = 0; s < synthyboy::kNumModSlots && n < max; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source < 0 || sp.destination < 0)
            continue;
        if (std::lround (v.param (sp.source)) == source && std::lround (v.param (sp.destination)) > 0)
            slots[n++] = s;
    }
    return n;
}
/** The first slot that has the source and the destination parameter `destChoice` (the choice value of modNN.destination), -1 none. */
inline int slotOfPair (const IInstanceView& v, int source, long destChoice) noexcept
{
    for (int s = 0; s < synthyboy::kNumModSlots; ++s)
    {
        const synthyboy::ModSlotParams sp = synthyboy::modSlotParams (s);
        if (sp.source >= 0 && sp.destination >= 0 && std::lround (v.param (sp.source)) == source && std::lround (v.param (sp.destination)) == destChoice)
            return s;
    }
    return -1;
}

}}} // namespace synthyboy_mpc::overlay::uibind
