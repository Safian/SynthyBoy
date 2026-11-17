// SynthyBoy - shell-mpc: factory presets as VST programs.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYNTHYBOY_MPC_PROGRAM_LIST_H
#define SYNTHYBOY_MPC_PROGRAM_LIST_H

#include "synthyboy/preset.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace synthyboy_mpc
{

struct ProgramEntry
{
    std::string name;
    std::string category;
    std::string path;
};

/** The order of the programs: by category (plain <), then by name case-insensitively (ASCII tolower), then by path. The scan and the index use this one rule, so the program numbers the host
    stores in projects and the Sounds browser's files do not depend on which of them made the list. */
inline void sortPrograms (std::vector<ProgramEntry>& result)
{
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

    sortPrograms (result);
    }
    catch (...) { result.clear(); }

    return result;
}


// ---------------------------------------------------------------------------
// The preset index (scripts/build-preset-index.py writes <presets>/index.json at package time; docs/MPC.md "The preset index")
// ---------------------------------------------------------------------------

/** FNV-1a 64: the hash of the index (an integrity check, not a security measure). */
inline uint64_t fnv1a64 (const void* data, size_t n, uint64_t h = 0xcbf29ce484222325ull) noexcept
{
    const unsigned char* p = static_cast<const unsigned char*> (data);
    for (size_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

/** The listing hash of a set of (relative path, size) pairs: FNV-1a 64 over "<path>\0<size>\n" in the order of the paths' bytes (the script computes the same). */
inline uint64_t listingHash (std::vector<std::pair<std::string, unsigned long long>> files)
{
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });   // std::string compares as unsigned bytes: the script sorts UTF-8 bytes
    uint64_t h = 0xcbf29ce484222325ull;
    for (const auto& f : files)
    {
        const std::string line = f.first + std::string (1, '\0') + std::to_string (f.second) + "\n";
        h = fnv1a64 (line.data(), line.size(), h);
    }
    return h;
}

namespace index_detail
{
/** A JSON string at `p` (after the opening quote is checked here): unescapes \" \\ \/ \b \f \n \r \t and \u00XX below 0x80; any other escape fails. Advances `p` past the closing quote. */
inline bool str (const char*& p, const char* end, std::string& out)
{
    if (p >= end || *p != '"')
        return false;
    ++p;
    out.clear();
    while (p < end)
    {
        const char c = *p++;
        if (c == '"')
            return true;
        if (static_cast<unsigned char> (c) < 0x20)
            return false;
        if (c != '\\')
        {
            out += c;
            continue;
        }
        if (p >= end)
            return false;
        const char e = *p++;
        switch (e)
        {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                if (end - p < 4)
                    return false;
                unsigned code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char h = p[i];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= static_cast<unsigned> (h - '0');
                    else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned> (h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned> (h - 'A' + 10);
                    else return false;
                }
                if (code >= 0x80)
                    return false;   // the script writes UTF-8 as is; anything else is not an index of ours: the scan decides
                out += static_cast<char> (code);
                p += 4;
                break;
            }
            default: return false;
        }
    }
    return false;
}
inline bool lit (const char*& p, const char* end, const char* text)
{
    const size_t n = std::strlen (text);
    if (static_cast<size_t> (end - p) < n || std::memcmp (p, text, n) != 0)
        return false;
    p += n;
    return true;
}
inline bool num (const char*& p, const char* end, unsigned long long& out)
{
    if (p >= end || *p < '0' || *p > '9')
        return false;
    out = 0;
    int digits = 0;
    while (p < end && *p >= '0' && *p <= '9' && digits < 18)
    {
        out = out * 10 + static_cast<unsigned long long> (*p++ - '0');
        ++digits;
    }
    return true;
}
} // namespace index_detail

/** Reads `<presetsDir>/index.json` and checks it against the directory (names and sizes only, no preset file is opened). Returns true and the programs (sorted by sortPrograms) when the index is
    valid and current; false (and `why` says in a few words) when it is missing, unparsable, of another version, or stale: the caller then scans. Never throws. */
inline bool loadPresetIndex (const std::string& presetsDir, std::vector<ProgramEntry>& out, std::string& why) noexcept
{
    try
    {
        out.clear();
        const std::string indexPath = presetsDir + "/index.json";
        std::error_code ec;
        const auto indexSize = std::filesystem::file_size (indexPath, ec);
        if (ec || indexSize == 0 || indexSize > (4u << 20))
        {
            why = "no index";
            return false;
        }
        std::string text (static_cast<size_t> (indexSize), '\0');
        FILE* f = std::fopen (indexPath.c_str(), "rb");
        if (f == nullptr)
        {
            why = "index not readable";
            return false;
        }
        const size_t got = std::fread (&text[0], 1, text.size(), f);
        std::fclose (f);
        text.resize (got);
        const char* p = text.data();
        const char* end = p + text.size();
        using namespace index_detail;
        unsigned long long version = 0, count = 0;
        std::string listing;
        if (! (lit (p, end, "{\"index_version\":") && num (p, end, version) && lit (p, end, ",\"count\":") && num (p, end, count) && lit (p, end, ",\"listing_hash\":") && str (p, end, listing)
               && lit (p, end, ",\"presets\":[\n")))
        {
            why = "index header unparsable";
            return false;
        }
        if (version != 1 || count > 100000)
        {
            why = "index version not known";
            return false;
        }
        std::vector<ProgramEntry> programs;
        std::vector<std::pair<std::string, unsigned long long>> files;
        programs.reserve (static_cast<size_t> (count));
        for (unsigned long long i = 0; i < count; ++i)
        {
            std::string path, name, category, group, hash;
            unsigned long long size = 0, fv = 0, sv = 0;
            if (! (lit (p, end, "{\"path\":") && str (p, end, path) && lit (p, end, ",\"name\":") && str (p, end, name) && lit (p, end, ",\"category\":") && str (p, end, category)
                   && lit (p, end, ",\"group\":") && str (p, end, group) && lit (p, end, ",\"size\":") && num (p, end, size) && lit (p, end, ",\"hash\":") && str (p, end, hash)
                   && lit (p, end, ",\"format_version\":") && num (p, end, fv) && lit (p, end, ",\"state_version\":") && num (p, end, sv) && lit (p, end, "}")))
            {
                why = "index entry unparsable";
                return false;
            }
            if (i + 1 < count && ! lit (p, end, ",\n"))
            {
                why = "index entry unparsable";
                return false;
            }
            if (path.empty() || path.find ("..") != std::string::npos || path[0] == '/')
            {
                why = "index path not acceptable";
                return false;
            }
            ProgramEntry e;
            e.name = std::move (name);
            e.category = std::move (category);
            e.path = presetsDir + "/" + path;
            programs.push_back (std::move (e));
            files.emplace_back (std::move (path), size);
        }
        char want[32];
        std::snprintf (want, sizeof want, "%016llx", static_cast<unsigned long long> (listingHash (files)));
        if (listing != want)
        {
            why = "index header inconsistent";
            return false;
        }
        // the cheap directory check: the same names and sizes as the index, from the directory listing alone
        std::vector<std::pair<std::string, unsigned long long>> present;
        const std::filesystem::path root (presetsDir);
        for (std::filesystem::recursive_directory_iterator it (root, std::filesystem::directory_options::skip_permission_denied, ec), last; ! ec && it != last; it.increment (ec))
        {
            std::error_code e2;
            if (! it->is_regular_file (e2) || it->path().extension() != ".sbpreset")
                continue;
            const auto size = it->file_size (e2);
            if (e2)
            {
                why = "directory listing failed";
                return false;
            }
            present.emplace_back (std::filesystem::relative (it->path(), root, e2).generic_string(), static_cast<unsigned long long> (size));
        }
        if (ec || present.size() != files.size() || listingHash (present) != listingHash (files))
        {
            why = "index stale (names or sizes differ from the directory)";
            return false;
        }
        sortPrograms (programs);
        out = std::move (programs);
        why = "index";
        return true;
    }
    catch (...)
    {
        out.clear();
        why = "index failed";
        return false;
    }
}

/** The programs of the factory preset folder: from the index when it is valid and current (one small file and a directory listing), else by the scan (the first 4 KB of every file). `source`
    says which ("index" or the reason for the scan). The order is the same either way. */
inline std::vector<ProgramEntry> loadPrograms (const std::string& presetsDir, std::string* source = nullptr) noexcept
{
    std::vector<ProgramEntry> result;
    std::string why;
    if (loadPresetIndex (presetsDir, result, why))
    {
        if (source != nullptr)
            *source = why;
        return result;
    }
    if (source != nullptr)
        *source = "scan (" + why + ")";
    return scanPrograms (presetsDir);
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
