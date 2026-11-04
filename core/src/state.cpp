// SynthyBoyVST - core: versioned state (deterministic JSON text) with a bounded hand-written parser.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/state.h"

#include <array>
#include <cmath>
#include <cstring>

#include "number.h"

namespace synthyboy
{
namespace
{
constexpr const char* kFormatName = "synthyboy-state";
constexpr int kMaxDepth = 8;
constexpr size_t kMaxKeyChars = 63;

/** What the parser collected; indexed like paramTable(). Migrations rewrite it before it is applied. */
struct Parsed
{
    double value[kMaxParams] = {};
    bool has[kMaxParams] = {};
    int version = 0;
};

/** Hook for older document versions: a migration upgrades `Parsed` from version `from` to `from + 1`
    (rename keys, rescale values, ...). Empty for version 1; later versions append entries here. */
using MigrateFn = void (*) (Parsed&);
struct Migration
{
    int from;
    MigrateFn apply;
};
constexpr std::array<Migration, 0> kMigrations {};

void migrate (Parsed& parsed) noexcept
{
    for (int v = parsed.version; v < kStateVersion; ++v)
        for (const auto& m : kMigrations)
            if (m.from == v)
                m.apply (parsed);
}

class Parser
{
public:
    Parser (const char* b, const char* e) noexcept : p (b), end (e) {}

    bool parseDocument (Parsed& out, bool& formatOk, bool& versionSeen) noexcept
    {
        if (end - p >= 3 && static_cast<unsigned char> (p[0]) == 0xef && static_cast<unsigned char> (p[1]) == 0xbb
            && static_cast<unsigned char> (p[2]) == 0xbf)
            p += 3; // UTF-8 byte order mark

        skipWs();
        if (! consume ('{'))
            return false;

        skipWs();
        if (! consume ('}'))
        {
            for (;;)
            {
                char key[kMaxKeyChars + 1];
                bool truncated = false;
                skipWs();
                if (! parseString (key, sizeof key, truncated))
                    return false;
                skipWs();
                if (! consume (':'))
                    return false;
                skipWs();

                if (! truncated && std::strcmp (key, "format") == 0)
                {
                    char text[32];
                    bool tooLong = false;
                    if (p < end && *p == '"')
                    {
                        if (! parseString (text, sizeof text, tooLong))
                            return false;
                        formatOk = ! tooLong && std::strcmp (text, kFormatName) == 0;
                    }
                    else
                    {
                        formatOk = false;
                        if (! skipValue (1))
                            return false;
                    }
                }
                else if (! truncated && std::strcmp (key, "version") == 0)
                {
                    double v = 0.0;
                    if (! parseNumber (v) || v != std::floor (v) || v < 1.0 || v > 1.0e9)
                        return false;
                    out.version = static_cast<int> (v);
                    versionSeen = true;
                }
                else if (! truncated && std::strcmp (key, "params") == 0 && p < end && *p == '{')
                {
                    if (! parseParams (out))
                        return false;
                }
                else if (! skipValue (1))
                {
                    return false;
                }

                skipWs();
                if (consume (','))
                    continue;
                if (consume ('}'))
                    break;
                return false;
            }
        }

        skipWs();
        return p == end; // nothing but white space may follow
    }

private:
    const char* p;
    const char* end;

    bool consume (char c) noexcept
    {
        if (p < end && *p == c)
        {
            ++p;
            return true;
        }
        return false;
    }

    void skipWs() noexcept
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    static int hexValue (char c) noexcept
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    /** Parses a JSON string at p (which must be '"'). Copies at most cap - 1 decoded characters into out (out may be
        null to just validate); truncated is set if more were present. */
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
                return false; // raw control characters are not allowed in JSON strings
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
                    if (end - p < 4)
                        return false;
                    int code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        const int h = hexValue (p[i]);
                        if (h < 0)
                            return false;
                        code = code * 16 + h;
                    }
                    p += 4;
                    put (code > 0 && code < 0x80 ? static_cast<char> (code) : '\x01'); // keys are ASCII
                    break;
                }
                default: return false;
            }
        }
        return false; // unterminated
    }

    bool parseNumber (double& v) noexcept
    {
        const char* next = nullptr;
        if (! number::parseDecimal (p, end, true, v, next))
            return false;
        p = next;
        return true;
    }

    bool skipLiteral (const char* word) noexcept
    {
        const size_t n = std::strlen (word);
        if (static_cast<size_t> (end - p) < n || std::memcmp (p, word, n) != 0)
            return false;
        p += n;
        return true;
    }

    /** Validates and skips one JSON value; depth is the nesting level of this value. */
    bool skipValue (int depth) noexcept
    {
        if (p >= end)
            return false;

        switch (*p)
        {
            case '{':
            {
                if (depth > kMaxDepth)
                    return false;
                ++p;
                skipWs();
                if (consume ('}'))
                    return true;
                for (;;)
                {
                    bool truncated = false;
                    skipWs();
                    if (! parseString (nullptr, 0, truncated))
                        return false;
                    skipWs();
                    if (! consume (':'))
                        return false;
                    skipWs();
                    if (! skipValue (depth + 1))
                        return false;
                    skipWs();
                    if (consume (','))
                        continue;
                    return consume ('}');
                }
            }
            case '[':
            {
                if (depth > kMaxDepth)
                    return false;
                ++p;
                skipWs();
                if (consume (']'))
                    return true;
                for (;;)
                {
                    skipWs();
                    if (! skipValue (depth + 1))
                        return false;
                    skipWs();
                    if (consume (','))
                        continue;
                    return consume (']');
                }
            }
            case '"':
            {
                bool truncated = false;
                return parseString (nullptr, 0, truncated);
            }
            case 't': return skipLiteral ("true");
            case 'f': return skipLiteral ("false");
            case 'n': return skipLiteral ("null");
            default:
            {
                double ignored = 0.0;
                return parseNumber (ignored);
            }
        }
    }

    /** p is at the '{' of the "params" object. Numbers for known keys are collected, everything else is skipped. */
    bool parseParams (Parsed& out) noexcept
    {
        consume ('{');
        skipWs();
        if (consume ('}'))
            return true;

        for (;;)
        {
            char key[kMaxKeyChars + 1];
            bool truncated = false;
            skipWs();
            if (! parseString (key, sizeof key, truncated))
                return false;
            skipWs();
            if (! consume (':'))
                return false;
            skipWs();

            int index = -1;
            if (! truncated)
                if (const auto* info = findParamByKey (key))
                    index = findParamIndex (info->id);

            if (index >= 0 && p < end && (*p == '-' || (*p >= '0' && *p <= '9')))
            {
                double v = 0.0;
                if (! parseNumber (v))
                    return false;
                out.value[index] = v; // duplicate keys: the last one wins
                out.has[index] = true;
            }
            else if (! skipValue (2))
            {
                return false;
            }

            skipWs();
            if (consume (','))
                continue;
            return consume ('}');
        }
    }
};
} // namespace

ParamValues ParamValues::defaults() noexcept
{
    ParamValues v;
    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
        v.plain[i] = table[i].def;
    return v;
}

std::string saveState (const ParamValues& values)
{
    std::string s;
    s.reserve (160);
    s += "{\"format\":\"";
    s += kFormatName;
    s += "\",\"version\":";
    s += std::to_string (kStateVersion);
    s += ",\"params\":{";

    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        if (i > 0)
            s += ',';
        s += '"';
        s += table[i].key;
        s += "\":";

        char buf[32];
        number::formatG9 (constrainValue (table[i], values.plain[i]), buf, sizeof buf);
        s += buf;
    }
    s += "}}";
    return s;
}

LoadResult loadState (const void* data, size_t size, ParamValues& inOut) noexcept
{
    LoadResult result;
    if (size == 0)
    {
        result.status = LoadStatus::empty;
        return result;
    }
    if (data == nullptr || size > kMaxStateBytes)
        return result;

    const char* begin = static_cast<const char*> (data);
    Parsed parsed;
    bool formatOk = false;
    bool versionSeen = false;
    Parser parser (begin, begin + size);
    if (! parser.parseDocument (parsed, formatOk, versionSeen) || ! formatOk || ! versionSeen)
        return result;

    result.version = parsed.version;
    migrate (parsed);

    const auto* table = paramTable();
    for (size_t i = 0; i < paramCount(); ++i)
    {
        if (! parsed.has[i])
            continue;
        const auto& info = table[i];
        const double lo = static_cast<double> (info.min);
        const double hi = static_cast<double> (info.max);
        const double v = parsed.value[i];
        inOut.plain[i] = constrainValue (info, static_cast<float> (v < lo ? lo : (v > hi ? hi : v)));
        ++result.applied;
    }

    result.status = parsed.version > kStateVersion ? LoadStatus::newer_version : LoadStatus::ok;
    return result;
}
} // namespace synthyboy
