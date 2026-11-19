// SynthyBoy - MPC overlay: what the plugin-drawn element UIs (env_ui.h, and the ones to come) share when they bind library components to the engine's parameters: the table lookups, the writes through
// IInstanceView (normalised, the plugin tells the host), the text of a value with its unit, and the modulation matrix seen from a parameter (which slots move it) and from a source (which slots it drives).
// Pure: no OS headers, no state. The same code is in osc_ui.h as private members (the oscillator UI was first); the element UIs use this file.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../kit/param_source.h"
#include "../kit/synthyboy_params.h"
#include "../ui/ui.h"
#include "instance_view.h"
#include "macro_names.h"
#include "synthyboy/modulation.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc { namespace overlay { namespace uibind {

/** The parameters of SynthyBoy through the kit's parameter source (kit/param_source.h IParamSource): the lookups, the conversions and the text of a value are the kit's, so a page written on them runs on any plugin
    (migration step 3 of docs/ui-components/TEMPLATE.md; `kit_parity_host` compares them with the old direct calls for every parameter). */
inline const mpckit::IParamSource& params() noexcept { return mpckit::synthyboy_adapter::Params::get(); }

inline int indexOfKey (const std::string& key) noexcept { return params().indexOfKey (key.c_str()); }   // (an empty key: -1)
inline const synthyboy::ParamInfo& info (int idx) noexcept { return synthyboy::paramTable()[idx]; }   // the pages read ParamInfo (choices, ranges)
inline double normOf (int idx, float plain) noexcept { return params().toNormalised (idx, plain); }
inline void writePlain (IInstanceView& v, int idx, double plain) noexcept
{
    const mpckit::IParamSource& p = params();
    v.writeParam (idx, p.toNormalised (idx, p.constrain (idx, static_cast<float> (plain))));
}
inline void writeNorm (IInstanceView& v, int idx, double norm) noexcept { v.writeParam (idx, static_cast<float> (std::clamp (norm, 0.0, 1.0))); }

/** A value of a parameter as text with its unit: "35%", "+12 ct", "420 Hz", "1.20 s", "-6.0 dB" (at most 15 characters). Rates under 100 Hz keep their decimals (two under 10 Hz, one under 100 Hz: "0.25 Hz", "33.5 Hz",
    "420 Hz"); a value that rounds to zero has no sign; a pan (a percentage from -100 to 100 whose key ends in ".pan") reads like the pans of the mixer: C, L 50, R 50. The kit's function (mpckit::formatValue, the one algorithm). */
inline void formatValue (const synthyboy::ParamInfo& pi, float plain, char* out, size_t cap)
{
    mpckit::formatValue (mpckit::synthyboy_adapter::descOf (pi), plain, out, cap);
}

// ---- the modulation matrix
inline const char* sourceName (int choice) noexcept
{
    if (const MacroNames* names = activeMacroNames())   // a macro answers with its own name (macro_names.h)
    {
        const int m = macroOfSource (choice);
        if (m >= 0)
            return names->full (m);
    }
    const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
    return choice >= 1 && choice <= static_cast<int> (synthyboy::modSourceCount()) ? t[choice - 1].name : "";
}
/** True for the sources that are macros (their chips take the colour of the macros whatever the name says). */
inline bool isMacroSource (int choice) noexcept { return macroOfSource (choice) >= 0; }
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
/** The short name (at most six bytes) of the source `choice`: a macro answers with the short form of its name ("MAC 3", "Filter"), the others as shortSourceName. */
inline void shortName (int choice, char out[7])
{
    const MacroNames* names = activeMacroNames();
    const int m = macroOfSource (choice);
    if (m >= 0)
    {
        if (names != nullptr)
            std::snprintf (out, 7, "%s", names->shortName (m));
        else
            std::snprintf (out, 7, "MAC %d", m + 1);   // outside a scope: the default names
        return;
    }
    shortSourceName (sourceName (choice), out);
}
/** The modulation source choice value (1 ..) of the macro n (1 .. 8); 0 when the table has none. */
inline int macroSource (int n) noexcept
{
    char key[12];
    std::snprintf (key, sizeof key, "macro%d", n);
    const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
    for (size_t i = 0; i < synthyboy::modSourceCount(); ++i)
        if (std::strcmp (t[i].key, key) == 0)
            return static_cast<int> (i) + 1;
    return 0;
}
/** The groups of the sources in a list: 1 envelopes, 2 LFOs (random ones too), 3 macros, 4 the player (velocity, note, wheels, pressure ...), 5 the sound (the outputs of the sources and filters, the voice values). 0: none. */
constexpr int kSourceGroups = 5;
inline const char* sourceGroupName (int g) noexcept
{
    static const char* const n[kSourceGroups + 1] = { "NONE", "ENVELOPES", "LFOS", "MACROS", "PLAYER", "SOUND" };
    return g >= 0 && g <= kSourceGroups ? n[g] : "";
}
inline int sourceGroup (int choice) noexcept
{
    if (choice < 1 || choice > static_cast<int> (synthyboy::modSourceCount()))
        return 0;
    const char* k = synthyboy::modSourceTable()[choice - 1].key;
    auto starts = [&] (const char* p) { return std::strncmp (k, p, std::strlen (p)) == 0; };
    if (starts ("env")) return 1;
    if (starts ("lfo") || starts ("rlfo")) return 2;
    if (starts ("macro")) return 3;
    if (starts ("osc_") || starts ("noise") || starts ("sub") || starts ("filter") || starts ("stereo") || starts ("voice_") || starts ("active_voices")) return 5;
    return 4;
}
// ---- the board of the sources (ui/mod_board.h): every source the engine has as a chip in its group; the matrix page and the page of the modulations of one control use the same
/** The text of a chip: the engine's name of the source in capitals ("Poly Aftertouch" -> "POLY AFTERTOUCH"; the random LFOs by their short name, "RLFO 2": the whole LFO family fits one row of the board). */
inline void boardLabel (int choice, char* out, size_t cap)
{
    const char* key = synthyboy::modSourceTable()[choice - 1].key;
    if (std::strncmp (key, "rlfo", 4) == 0)
        shortSourceName (sourceName (choice), out);
    else
        std::snprintf (out, cap, "%s", sourceName (choice));
    for (char* c = out; *c != '\0'; ++c)
        *c = static_cast<char> (std::toupper (static_cast<unsigned char> (*c)));
}
/** The name that picks the colour of a chip (theme::sourceColour): the short name of the source, the random sources as RND (the colour of the random LFO of the LFO page), the macros as MAC whatever they are called. */
inline void boardColourName (int choice, char* out, size_t cap)
{
    const char* key = synthyboy::modSourceTable()[choice - 1].key;
    if (std::strncmp (key, "rlfo", 4) == 0 || std::strcmp (key, "random") == 0)
        std::snprintf (out, cap, "RND");
    else if (isMacroSource (choice))
        std::snprintf (out, cap, "MAC");
    else
    {
        char sn[7];
        shortSourceName (sourceName (choice), sn);
        std::snprintf (out, cap, "%s", sn);
    }
}
/** Fills a board with every source of the engine, in the groups of sourceGroup (the order of a group is the engine's, except that the random LFOs come after the ten LFOs); the choice numbers (the engine's indices) are what the chips carry. */
inline void fillSourceBoard (ui::ModSourceBoard& b)
{
    b.clear();
    for (int g = 1; g <= kSourceGroups; ++g)
        b.setGroupName (g - 1, sourceGroupName (g));
    for (int g = 1; g <= kSourceGroups; ++g)
        for (int pass = 0; pass < 2; ++pass)
            for (int c = 1; c <= static_cast<int> (synthyboy::modSourceCount()); ++c)
                if (sourceGroup (c) == g && (std::strncmp (synthyboy::modSourceTable()[c - 1].key, "rlfo", 4) == 0) == (pass == 1))
                {
                    char label[28], colour[12];
                    boardLabel (c, label, sizeof label);
                    boardColourName (c, colour, sizeof colour);
                    b.add (c, label, colour, g - 1);
                }
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
        shortName (src, name);
        mi.add (src, name, depth, bi, by, isMacroSource (src));
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

/** The destinations of the modulation matrix grouped for a list (the group of the parameter before " / "): the groups, then per group the destination choice values (1 ..) in table order. ONE list for every
    page that adds a target (the envelope's and the LFO's ADD TARGET, the destination of a slot of the matrix). */
struct DestIndex
{
    std::vector<std::string> groups;
    std::vector<std::vector<int>> choices;
    static const DestIndex& get()
    {
        static const DestIndex d = build();
        return d;
    }
    static DestIndex build()
    {
        DestIndex d;
        const size_t n = synthyboy::modDestinationCount();
        for (size_t c = 1; c <= n; ++c)
        {
            const int p = synthyboy::modDestinationParamIndex (c);
            if (p < 0)
                continue;
            std::string g = info (p).group != nullptr ? info (p).group : "";
            const size_t cut = g.find (" / ");
            if (cut != std::string::npos)
                g.resize (cut);
            size_t i = 0;
            while (i < d.groups.size() && d.groups[i] != g)
                ++i;
            if (i == d.groups.size())
            {
                d.groups.push_back (g);
                d.choices.emplace_back();
            }
            d.choices[i].push_back (static_cast<int> (c));
        }
        return d;
    }
    /** The group and the position inside it of a destination choice value; false when the choice is in no group. */
    bool locate (int choice, int& group, int& index) const
    {
        for (size_t g = 0; g < choices.size(); ++g)
            for (size_t i = 0; i < choices[g].size(); ++i)
                if (choices[g][i] == choice)
                {
                    group = static_cast<int> (g);
                    index = static_cast<int> (i);
                    return true;
                }
        return false;
    }
};

}}} // namespace synthyboy_mpc::overlay::uibind
