// SynthyBoy - shell-mpc: factory presets as VST programs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYNTHYBOY_MPC_PROGRAM_LIST_H
#define SYNTHYBOY_MPC_PROGRAM_LIST_H

#include "synthyboy/preset.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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

// ---------------------------------------------------------------------------
// Chunk helpers: store program number + name in the chunk, resolve by name.
// ---------------------------------------------------------------------------

/** Escape a program name for JSON embedding in a chunk.
    `"` -> `\"`, `\` -> `\\`, every byte < 0x20 -> `\u00XX` (4 hex, lower a-f), other bytes unchanged. */
inline std::string escapeProgramName (const std::string& name)
{
    std::string out;
    out.reserve (name.size() + 8);
    for (unsigned char c : name)
    {
        if (c == '"')       { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c < 0x20)
        {
            char buf[8];
            std::snprintf (buf, sizeof (buf), "\\u%04x", c);
            out += buf;
        }
        else
        {
            out += static_cast<char> (c);
        }
    }
    return out;
}

/** Build a chunk that carries the program number and name.
    Returns  {"mpcProgram":N,"mpcProgramName":"<escaped name>",  followed by stateText.substr(1). */
inline std::string chunkWithProgram (const std::string& stateText, int program, const std::string& name)
{
    return "{\"mpcProgram\":" + std::to_string (program)
         + ",\"mpcProgramName\":\"" + escapeProgramName (name) + "\","
         + stateText.substr (1);
}

/** Read the program number from a chunk.  Returns -1 when no program tag is present. */
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

/** Read the program name from a chunk (if present).
    Returns true only when the chunk has the program number prefix directly followed by
    "mpcProgramName":"<escaped name>",".
    Unescapes `\"`, `\\`, `\/`, `\n`, `\t`, `\r`, `\b`, `\f` and `\u00XX`
    (only code points < 0x80; anything else -> false).
    Returns false (nameOut untouched or cleared) when: null data, format mismatch,
    unterminated name, or unescaped name > 256 bytes.  Never reads beyond `size`. */
inline bool programNameFromChunk (const void* data, size_t size, std::string& nameOut) noexcept
{
    try
    {
        constexpr size_t prefixLen = 14; // {"mpcProgram":
        constexpr size_t nameFieldLen = 18; // "mpcProgramName":"

        if (data == nullptr || size < prefixLen + 2)
            return false;

        const char* p = static_cast<const char*> (data);

        // Verify the program number prefix
        if (std::memcmp (p, "{\"mpcProgram\":", prefixLen) != 0)
            return false;

        p += prefixLen;
        size_t remaining = size - prefixLen;

        // Read and skip the digits
        int digits = 0;
        while (digits < 9 && remaining > 0 && *p >= '0' && *p <= '9')
        {
            ++p;
            ++digits;
            --remaining;
        }
        if (digits == 0) return false;

        // Must be followed by a comma
        if (remaining == 0 || *p != ',')
            return false;
        ++p;
        --remaining;

        // Must have "mpcProgramName":"
        if (remaining < nameFieldLen)
            return false;
        if (std::memcmp (p, "\"mpcProgramName\":\"", nameFieldLen) != 0)
            return false;

        p += nameFieldLen;
        remaining -= nameFieldLen;

        // Unescape the name
        std::string name;
        name.reserve (32);

        while (remaining > 0)
        {
            if (*p != '\\')
            {
                if (*p == '"')
                    break;  // closing quote
                name += *p;
                ++p;
                --remaining;
                continue;
            }

            // Must have at least one more byte for the escape char
            if (remaining < 2)
                return false;
            ++p;
            --remaining;
            char esc = *p;
            ++p;
            --remaining;

            switch (esc)
            {
            case '"':  name += '"';  break;
            case '\\': name += '\\'; break;
            case '/':  name += '/';  break;
            case 'n':  name += '\n'; break;
            case 't':  name += '\t'; break;
            case 'r':  name += '\r'; break;
            case 'b':  name += '\b'; break;
            case 'f':  name += '\f'; break;
            case 'u':
            {
                // \u00XX: need exactly 4 hex digits
                if (remaining < 4)
                    return false;
                unsigned int code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char h = p[i];
                    unsigned int nibble;
                    if (h >= '0' && h <= '9')       nibble = h - '0';
                    else if (h >= 'a' && h <= 'f')   nibble = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F')   nibble = h - 'A' + 10;
                    else
                        return false;
                    code = (code << 4) | nibble;
                }
                if (code >= 0x80)
                    return false;
                name += static_cast<char> (code);
                p += 4;
                remaining -= 4;
                break;
            }
            default:
                return false;
            }
        }

        // After the name we expect a closing quote and a comma
        if (remaining == 0)
            return false;
        if (*p != '"')
            return false;
        ++p;
        --remaining;
        if (remaining == 0 || *p != ',')
            return false;

        // Unescaped name must not exceed 256 bytes
        if (name.size() > 256)
            return false;

        nameOut = std::move (name);
        return true;
    }
    catch (...)
    {
        nameOut.clear();
        return false;
    }
}

/** Resolve a stored program index + name to a program number.
    Program numbers: 0 = "Init", i >= 1 = list[i-1].
    Returns a program number, or -1 for "no program". */
inline int resolveProgram (const std::vector<ProgramEntry>& list, int storedIndex, bool hasName, const std::string& name) noexcept
{
    if (! hasName)
    {
        // Old chunk: just use the stored index if in range
        if (storedIndex >= 0 && storedIndex <= static_cast<int> (list.size()))
            return storedIndex;
        return -1;
    }

    // hasName == true
    if (storedIndex == 0 && name == "Init")
        return 0;

    // storedIndex >= 1: check if it points to a program with the given name
    if (storedIndex >= 1 && storedIndex <= static_cast<int> (list.size())
        && list[static_cast<size_t> (storedIndex - 1)].name == name)
        return storedIndex;

    // Search for the first program with that name
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].name == name)
            return static_cast<int> (i) + 1;
    }

    return -1;
}

} // namespace synthyboy_mpc

#endif // SYNTHYBOY_MPC_PROGRAM_LIST_H
