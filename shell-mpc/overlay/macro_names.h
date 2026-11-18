// SynthyBoy - MPC overlay: the names of the eight macros as the plugin-drawn pages show them (docs/ui-design/macro-names.md): the full name (the card of the Macros page, the pickers and the rows of the
// Mod page, the title of a targets page) and the short form of at most six bytes (the chip on a knob, the row of the modulations page, the matrix). Pure: no OS headers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The names are state (StateContent::macroNames, content.macro_names of the preset and of the chunk). A page object keeps a MacroNames of its own that it loads from the instance's content every update
// and makes the ACTIVE one (NamesScope) while it updates, draws and handles input, so the helpers every page uses (uibind::sourceName, uibind::shortName) answer with the names of the instance on
// screen without each of them being handed the names. Outside a scope the default names are the answer ("Macro 3", "MAC 3").
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "synthyboy/content_state.h"
#include "synthyboy/modulation.h"

namespace synthyboy_mpc { namespace overlay {

/** The macro (0 .. 7) a modulation source choice value (1 ..) stands for; -1 for any other source. */
inline int macroOfSource (int choice) noexcept
{
    struct Map
    {
        int macro[128] = {};
        Map()
        {
            for (int& m : macro)
                m = -1;
            const synthyboy::ModSourceInfo* t = synthyboy::modSourceTable();
            for (size_t i = 0; i < synthyboy::modSourceCount() && i < 128; ++i)
                for (int k = 0; k < synthyboy::kNumMacros; ++k)
                {
                    char key[12];
                    std::snprintf (key, sizeof key, "macro%d", k + 1);
                    if (std::strcmp (t[i].key, key) == 0)
                        macro[i] = k;
                }
        }
    };
    static const Map map;
    return choice >= 1 && choice <= 128 ? map.macro[choice - 1] : -1;
}

class MacroNames
{
public:
    static constexpr int kCount = synthyboy::kNumMacros;
    static constexpr size_t kFullBytes = synthyboy::kMaxMacroNameBytes + 1;
    static constexpr size_t kShortBytes = 7;   // six bytes and the end (the chips' ModEntry::name)

    MacroNames() { load (nullptr); }

    /** Takes the names of a content (null: the defaults). Returns true when something changed. */
    bool load (const synthyboy::StateContent* c)
    {
        bool changed = false;
        for (int m = 0; m < kCount; ++m)
        {
            const std::string n = c != nullptr ? synthyboy::macroDisplayName (*c, m) : synthyboy::defaultMacroName (m);
            const bool custom = c != nullptr && ! c->macroNames[m].empty();
            if (n != full_[m] || custom != custom_[m])
            {
                std::snprintf (full_[m], sizeof full_[m], "%s", n.c_str());
                custom_[m] = custom;
                shortForm (custom ? n : std::string(), m, short_[m]);
                changed = true;
            }
        }
        return changed;
    }

    /** The full name of macro m (0 .. 7): its name, or "Macro n". The pointer stays valid for the life of the object (the text may change with load ()). */
    const char* full (int m) const noexcept { return m >= 0 && m < kCount ? full_[m] : ""; }
    /** The short form of at most six bytes: "MAC n" for a macro without a name. */
    const char* shortName (int m) const noexcept { return m >= 0 && m < kCount ? short_[m] : ""; }
    bool custom (int m) const noexcept { return m >= 0 && m < kCount && custom_[m]; }

    /** The short form of a name for the chip of macro m: "MAC n" when there is no name; else the first letters that fit six bytes: the name cut at a character boundary, at the end of a word when
        the cut would fall inside one and at least three letters come before it, without trailing blanks and signs ("Filter Sweep" -> "Filter", "Big Wobble" -> "Big", "Cutoff" -> "Cutoff",
        "Resonance" -> "Resona", "Drive + Warp" -> "Drive"). */
    static void shortForm (const std::string& name, int m, char out[kShortBytes])
    {
        if (name.empty())
        {
            std::snprintf (out, kShortBytes, "MAC %d", m + 1);
            return;
        }
        size_t n = 0;   // the bytes taken: whole characters only
        while (n < name.size() && n < 6)
        {
            const unsigned char c = static_cast<unsigned char> (name[n]);
            const size_t len = c < 0x80 ? 1 : (c >= 0xf0 ? 4 : (c >= 0xe0 ? 3 : 2));
            if (n + len > 6)
                break;
            n += len;
        }
        if (n < name.size() && name[n] != ' ')   // cut inside a word
        {
            const size_t sp = name.rfind (' ', n);
            if (sp != std::string::npos && sp >= 3)
                n = sp;
        }
        while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '+' || name[n - 1] == '-' || name[n - 1] == '/' || name[n - 1] == '&'))
            --n;
        if (n == 0)
            n = std::min<size_t> (name.size(), 1);
        std::snprintf (out, kShortBytes, "%.*s", static_cast<int> (n), name.c_str());
    }

private:
    char full_[kCount][kFullBytes] = {};
    char short_[kCount][kShortBytes] = {};
    bool custom_[kCount] = {};
};

/** The names the helpers of the pages answer with (the page on the thread that is updating, drawing or handling input). null: the defaults. */
inline const MacroNames*& activeMacroNames() noexcept
{
    static thread_local const MacroNames* active = nullptr;
    return active;
}

/** Makes `names` the active ones for the life of the object (the previous ones come back after). */
class NamesScope
{
public:
    explicit NamesScope (const MacroNames& names) noexcept : previous_ (activeMacroNames()) { activeMacroNames() = &names; }
    ~NamesScope() { activeMacroNames() = previous_; }
    NamesScope (const NamesScope&) = delete;
    NamesScope& operator= (const NamesScope&) = delete;

private:
    const MacroNames* previous_;
};

}} // namespace synthyboy_mpc::overlay
