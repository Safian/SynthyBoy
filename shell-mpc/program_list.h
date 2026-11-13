// SynthyBoy - shell-mpc: factory presets as VST programs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYNTHYBOY_MPC_PROGRAM_LIST_H
#define SYNTHYBOY_MPC_PROGRAM_LIST_H

#include "synthyboy/preset.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace synthyboy_mpc
{

struct ProgramEntry
{
    std::string name;
    std::string category;
    std::string path;
};

/** Never throws: a file system error or an allocation failure gives an empty list (the plugin then has only "Init"). */
inline std::vector<ProgramEntry> scanPrograms (const std::string& presetsDir) noexcept
{
    std::vector<ProgramEntry> result;
    try
    {
    std::error_code ec;
    if (presetsDir.empty() || ! std::filesystem::is_directory (presetsDir, ec) || ec)
        return result;

    const auto raw = synthyboy::scanPresetDirectory (presetsDir);
    result.reserve (raw.size());
    for (const auto& entry : raw)
    {
        ProgramEntry p;
        p.name = entry.meta.name;
        p.category = entry.meta.category;
        p.path = entry.path;
        result.push_back (std::move (p));
    }

    // Sort by category (plain <), then by name case-insensitively (ASCII tolower),
    // then by path.
    std::sort (result.begin(), result.end(), [] (const ProgramEntry& a, const ProgramEntry& b)
    {
        if (a.category != b.category) return a.category < b.category;
        auto cmp = [] (char a, char b)
        {
            return static_cast<char> (std::tolower (static_cast<unsigned char> (a)))
                 < static_cast<char> (std::tolower (static_cast<unsigned char> (b)));
        };
        if (std::lexicographical_compare (a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), cmp))
            return true;
        if (std::lexicographical_compare (b.name.begin(), b.name.end(), a.name.begin(), a.name.end(), cmp))
            return false;
        return a.path < b.path;
    });
    }
    catch (...) { result.clear(); }

    return result;
}

inline std::string chunkWithProgram (const std::string& stateText, int program)
{
    return "{\"mpcProgram\":" + std::to_string (program) + "," + stateText.substr (1);
}

inline int programFromChunk (const void* data, size_t size)
{
    // {"mpcProgram": is 14 characters
    constexpr size_t prefixLen = 14;

    if (data == nullptr || size < prefixLen + 2) // at least 1 digit + comma
        return -1;

    const char* p = static_cast<const char*> (data);

    if (std::memcmp (p, "{\"mpcProgram\":", prefixLen) != 0)
        return -1;

    p += prefixLen;
    size_t remaining = size - prefixLen;

    // Read digits (1 to 9)
    int digits = 0;
    int value = 0;
    while (digits < 9 && remaining > 0 && *p >= '0' && *p <= '9')
    {
        value = value * 10 + (*p - '0');
        ++p;
        ++digits;
        --remaining;
    }

    if (digits == 0) return -1;

    // Must be followed by a comma
    if (remaining == 0 || *p != ',')
        return -1;

    return value;
}

} // namespace synthyboy_mpc

#endif // SYNTHYBOY_MPC_PROGRAM_LIST_H
