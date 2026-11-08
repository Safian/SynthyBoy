// SynthyBoy - core: preset container (.sbpreset) and metadata discovery (docs/ARCHITECTURE.md 5.2).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/preset.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace synthyboy
{
namespace
{
constexpr size_t kMaxPresetFileBytes = 1u << 20; // 1 MiB

void escapeJsonString (const std::string& in, std::string& out)
{
    out += '"';
    for (char c : in)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char> (c) < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof buf, "\\u%04x", static_cast<unsigned char> (c));
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

class FastMetaParser
{
public:
    FastMetaParser (const char* b, const char* e) noexcept : p (b), end (e) {}

    bool parseMetaOnly (PresetMeta& meta) noexcept
    {
        skipBOM();
        skipWs();
        if (! consume ('{'))
            return false;

        bool inFormatSeen = false;
        bool isPresetFormat = false;
        bool hasMeta = false;

        while (p < end)
        {
            skipWs();
            if (consume ('}'))
                return isPresetFormat && hasMeta;

            char key[64];
            bool truncated = false;
            if (! parseString (key, sizeof key, truncated))
                return false;

            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            if (! truncated && std::strcmp (key, "format") == 0)
            {
                char fmt[32];
                if (parseString (fmt, sizeof fmt, truncated))
                {
                    inFormatSeen = true;
                    isPresetFormat = (std::strcmp (fmt, kPresetFormatName) == 0);
                    if (isPresetFormat && hasMeta)
                        return true;
                }
                else
                {
                    return false;
                }
            }
            else if (! truncated && std::strcmp (key, "meta") == 0 && p < end && *p == '{')
            {
                if (! parseMetaBody (meta))
                    return false;
                hasMeta = true;
                if (isPresetFormat)
                    return true;
            }
            else
            {
                if (! skipValue (1))
                    return false;
            }

            skipWs();
            if (consume (','))
                continue;
            if (consume ('}'))
                break;
            return false;
        }

        return inFormatSeen && isPresetFormat && hasMeta;
    }

    bool locateStateObject (const char*& stateStart, size_t& stateLen) noexcept
    {
        p = origBegin;
        skipBOM();
        skipWs();
        if (! consume ('{'))
            return false;

        while (p < end)
        {
            skipWs();
            if (consume ('}'))
                return false;

            char key[64];
            bool truncated = false;
            if (! parseString (key, sizeof key, truncated))
                return false;

            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            if (! truncated && std::strcmp (key, "state") == 0 && p < end && *p == '{')
            {
                const char* s = p;
                if (! skipValue (1))
                    return false;
                stateStart = s;
                stateLen = static_cast<size_t> (p - s);
                return true;
            }
            else
            {
                if (! skipValue (1))
                    return false;
            }

            skipWs();
            if (consume (','))
                continue;
            if (consume ('}'))
                break;
            return false;
        }
        return false;
    }

private:
    const char* p;
    const char* end;
    const char* origBegin = p;

    void skipBOM() noexcept
    {
        if (end - p >= 3 && static_cast<unsigned char> (p[0]) == 0xef && static_cast<unsigned char> (p[1]) == 0xbb
            && static_cast<unsigned char> (p[2]) == 0xbf)
            p += 3;
    }

    void skipWs() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    bool consume (char c) noexcept
    {
        if (p < end && *p == c)
        {
            ++p;
            return true;
        }
        return false;
    }

    static int hexValue (char c) noexcept
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool parseString (char* out, size_t cap, bool& truncated) noexcept
    {
        truncated = false;
        if (! consume ('"'))
            return false;

        size_t n = 0;
        auto put = [&] (char c) noexcept
        {
            if (out != nullptr && n + 1 < cap)
                out[n++] = c;
            else
                truncated = true;
        };

        while (p < end)
        {
            const unsigned char c = static_cast<unsigned char> (*p++);
            if (c == '"')
            {
                if (out != nullptr && cap > 0)
                    out[n] = '\0';
                return true;
            }
            if (c < 0x20)
                return false;
            if (c != '\\')
            {
                put (static_cast<char> (c));
                continue;
            }

            if (p >= end)
                return false;
            const char e = *p++;
            switch (e)
            {
                case '"': put ('"'); break;
                case '\\': put ('\\'); break;
                case '/': put ('/'); break;
                case 'b': put ('\b'); break;
                case 'f': put ('\f'); break;
                case 'n': put ('\n'); break;
                case 'r': put ('\r'); break;
                case 't': put ('\t'); break;
                case 'u':
                {
                    if (end - p < 4) return false;
                    int code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        const int h = hexValue (p[i]);
                        if (h < 0) return false;
                        code = (code << 4) | h;
                    }
                    p += 4;
                    if (code <= 0x7f)
                        put (static_cast<char> (code));
                    else
                        put ('?');
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool skipValue (int depth) noexcept
    {
        if (depth > 16 || p >= end)
            return false;

        skipWs();
        if (p >= end) return false;

        const char c = *p;
        if (c == '"')
        {
            bool tr = false;
            return parseString (nullptr, 0, tr);
        }
        if (c == '{' || c == '[')
        {
            const char close = (c == '{') ? '}' : ']';
            ++p;
            skipWs();
            if (consume (close))
                return true;
            for (;;)
            {
                if (c == '{')
                {
                    bool tr = false;
                    if (! parseString (nullptr, 0, tr))
                        return false;
                    skipWs();
                    if (! consume (':'))
                        return false;
                }
                if (! skipValue (depth + 1))
                    return false;
                skipWs();
                if (consume (','))
                    continue;
                if (consume (close))
                    return true;
                return false;
            }
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9')))
                ++p;
            return true;
        }
        if (end - p >= 4 && std::strncmp (p, "true", 4) == 0) { p += 4; return true; }
        if (end - p >= 5 && std::strncmp (p, "false", 5) == 0) { p += 5; return true; }
        if (end - p >= 4 && std::strncmp (p, "null", 4) == 0) { p += 4; return true; }
        return false;
    }

    bool parseMetaBody (PresetMeta& meta) noexcept
    {
        if (! consume ('{'))
            return false;

        skipWs();
        if (consume ('}'))
            return true;

        for (;;)
        {
            char key[64];
            bool truncated = false;
            skipWs();
            if (! parseString (key, sizeof key, truncated))
                return false;

            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            char val[256];
            if (p < end && *p == '"')
            {
                if (! parseString (val, sizeof val, truncated))
                    return false;
                if (! truncated)
                {
                    if (std::strcmp (key, "name") == 0) meta.name = val;
                    else if (std::strcmp (key, "category") == 0) meta.category = val;
                    else if (std::strcmp (key, "author") == 0) meta.author = val;
                    else if (std::strcmp (key, "license") == 0) meta.license = val;
                    else if (std::strcmp (key, "tags") == 0) meta.tags = val;
                    else if (std::strcmp (key, "description") == 0) meta.description = val;
                }
            }
            else
            {
                if (! skipValue (1))
                    return false;
            }

            skipWs();
            if (consume (','))
                continue;
            if (consume ('}'))
                return true;
            return false;
        }
    }
};
} // namespace

PresetMeta PresetMeta::initDefault() noexcept
{
    PresetMeta m;
    m.name = "Init";
    m.category = "Init";
    m.author = "Safian Szabolcs";
    m.license = "CC0-1.0";
    m.tags = "init,basic";
    m.description = "Clean default patch";
    return m;
}

std::string savePreset (const PresetMeta& meta, const ParamValues& values)
{
    return savePreset (meta, values, StateContent {});
}

std::string savePreset (const PresetMeta& meta, const ParamValues& values, const StateContent& content)
{
    std::string s;
    s.reserve (256 + 1024 + paramCount() * 28);
    s += "{\"format\":\"";
    s += kPresetFormatName;
    s += "\",\"version\":";
    s += std::to_string (kPresetVersion);
    s += ",\"meta\":{";

    s += "\"name\":"; escapeJsonString (meta.name.empty() ? "Init" : meta.name, s);
    s += ",\"category\":"; escapeJsonString (meta.category.empty() ? "Lead" : meta.category, s);
    s += ",\"author\":"; escapeJsonString (meta.author.empty() ? "Safian Szabolcs" : meta.author, s);
    s += ",\"license\":"; escapeJsonString (meta.license.empty() ? "CC0-1.0" : meta.license, s);
    s += ",\"tags\":"; escapeJsonString (meta.tags, s);
    s += ",\"description\":"; escapeJsonString (meta.description, s);

    s += "},\"state\":";
    s += saveState (values, content);
    s += "}";
    return s;
}

bool readPresetMeta (const void* data, size_t size, PresetMeta& metaOut) noexcept
{
    if (data == nullptr || size == 0 || size > kMaxPresetFileBytes)
        return false;

    const char* b = static_cast<const char*> (data);
    FastMetaParser parser (b, b + size);
    return parser.parseMetaOnly (metaOut);
}

namespace
{
LoadResult loadPresetImpl (const void* data, size_t size, PresetMeta& metaOut, ParamValues& valuesOut, StateContent* contentOut) noexcept
{
    LoadResult res;
    if (data == nullptr || size == 0)
    {
        res.status = LoadStatus::empty;
        return res;
    }
    if (size > kMaxPresetFileBytes)
        return res;

    const char* b = static_cast<const char*> (data);
    FastMetaParser parser (b, b + size);

    PresetMeta meta;
    if (parser.parseMetaOnly (meta))
    {
        metaOut = meta;
        const char* stateStart = nullptr;
        size_t stateLen = 0;
        if (parser.locateStateObject (stateStart, stateLen) && stateStart != nullptr && stateLen > 0)
            return contentOut != nullptr ? loadStateWithContent (stateStart, stateLen, valuesOut, *contentOut) : loadState (stateStart, stateLen, valuesOut);
        return res;
    }

    // Fallback: check if the document is a raw synthyboy-state document
    res = contentOut != nullptr ? loadStateWithContent (data, size, valuesOut, *contentOut) : loadState (data, size, valuesOut);
    if (res.status == LoadStatus::ok || res.status == LoadStatus::newer_version)
        metaOut = PresetMeta::initDefault();

    return res;
}
} // namespace

LoadResult loadPreset (const void* data, size_t size, PresetMeta& metaOut, ParamValues& valuesOut) noexcept
{
    return loadPresetImpl (data, size, metaOut, valuesOut, nullptr);
}

LoadResult loadPreset (const void* data, size_t size, PresetMeta& metaOut, ParamValues& valuesOut, StateContent& contentInOut) noexcept
{
    return loadPresetImpl (data, size, metaOut, valuesOut, &contentInOut);
}

std::vector<PresetEntry> scanPresetDirectory (const std::string& directoryPath)
{
    std::vector<PresetEntry> entries;
    std::error_code ec;
    std::filesystem::path root (directoryPath);

    if (! std::filesystem::exists (root, ec) || ! std::filesystem::is_directory (root, ec))
        return entries;

    for (const auto& item : std::filesystem::recursive_directory_iterator (root, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (ec)
            break;

        if (! item.is_regular_file (ec))
            continue;

        if (item.path().extension() == ".sbpreset")
        {
            const std::string filePath = item.path().string();
            FILE* f = std::fopen (filePath.c_str(), "rb");
            if (f != nullptr)
            {
                char buf[65536];
                const size_t n = std::fread (buf, 1, sizeof buf, f);
                std::fclose (f);
                if (n > 0)
                {
                    PresetMeta meta;
                    if (readPresetMeta (buf, n, meta))
                        entries.push_back ({ filePath, std::move (meta) });
                }
            }
        }
    }

    std::sort (entries.begin(), entries.end(), [] (const PresetEntry& a, const PresetEntry& b) {
        if (a.meta.category != b.meta.category)
            return a.meta.category < b.meta.category;
        return a.meta.name < b.meta.name;
    });

    return entries;
}
} // namespace synthyboy
