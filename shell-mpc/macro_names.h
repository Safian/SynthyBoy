// SynthyBoy - MPC shell: the names of the macros on the host's side (docs/ui-design/macro-names.md): the name a Q proxy reports to the host when its target is a macro, the queue of the renames the
// plugin-drawn Macros page asks for, and the cut of a UTF-8 text into a buffer of the host. Pure: no host headers, no threads of its own (the queue has a mutex).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "synthyboy/content_state.h"
#include "synthyboy/params.h"

namespace synthyboy_mpc
{
/** The macro (0 .. 7) a parameter of the engine's table is (macro.1 .. macro.8), -1 for any other parameter. */
inline int macroOfParam (int tableIndex) noexcept
{
    struct Map
    {
        int index[synthyboy::kNumMacros];
        Map()
        {
            for (int m = 0; m < synthyboy::kNumMacros; ++m)
            {
                char key[12];
                std::snprintf (key, sizeof key, "macro.%d", m + 1);
                const synthyboy::ParamInfo* p = synthyboy::findParamByKey (key);
                index[m] = p != nullptr ? static_cast<int> (p - synthyboy::paramTable()) : -1;
            }
        }
    };
    static const Map map;
    for (int m = 0; m < synthyboy::kNumMacros; ++m)
        if (tableIndex >= 0 && map.index[m] == tableIndex)
            return m;
    return -1;
}

/** Copies `s` into out (cap bytes, zero terminated), cut at the end of a character: never inside a UTF-8 sequence. */
inline void copyUtf8 (char* out, size_t cap, const std::string& s) noexcept
{
    if (cap == 0)
        return;
    size_t n = std::min (s.size(), cap - 1);
    while (n > 0 && n < s.size() && (static_cast<unsigned char> (s[n]) & 0xc0u) == 0x80u)
        --n;
    std::memcpy (out, s.data(), n);
    out[n] = '\0';
}

/** The name the host is told for the parameter `tableIndex` when it is a macro: the macro's name (the default "Macro n" when it has none). false for any other parameter; `content` null: the defaults. */
inline bool macroParamName (const std::shared_ptr<const synthyboy::StateContent>& content, int tableIndex, char* out, size_t cap) noexcept
{
    const int m = macroOfParam (tableIndex);
    if (m < 0)
        return false;
    try
    {
        copyUtf8 (out, cap, content != nullptr ? synthyboy::macroDisplayName (*content, m) : synthyboy::defaultMacroName (m));
    }
    catch (...)   // an allocation failure: the table's name
    {
        std::snprintf (out, cap, "Macro %d", m + 1);
    }
    return true;
}

/** The renames the plugin-drawn pages ask for: the overlay thread posts (the latest per macro wins), the display thread takes them and applies them to the content. */
class MacroNameQueue
{
public:
    void post (int macro, const std::string& name, uint64_t generation)
    {
        if (macro < 0 || macro >= synthyboy::kNumMacros)
            return;
        std::lock_guard<std::mutex> g (mutex_);
        name_[macro] = name;
        generation_[macro] = generation;
        due_[macro] = true;
    }
    /** The requests not taken yet: have[m] says whether macro m has one, names[m] is it. true when there is any. */
    bool take (std::string names[synthyboy::kNumMacros], bool have[synthyboy::kNumMacros], uint64_t generations[synthyboy::kNumMacros])
    {
        bool any = false;
        std::lock_guard<std::mutex> g (mutex_);
        for (int m = 0; m < synthyboy::kNumMacros; ++m)
        {
            have[m] = due_[m];
            if (due_[m])
            {
                names[m] = name_[m];
                generations[m] = generation_[m];
                due_[m] = false;
                any = true;
            }
        }
        return any;
    }

private:
    std::mutex mutex_;
    std::string name_[synthyboy::kNumMacros];
    uint64_t generation_[synthyboy::kNumMacros] = {};
    bool due_[synthyboy::kNumMacros] = {};
};
} // namespace synthyboy_mpc
