// SynthyBoy - core: bounded, tolerant, iterative JSON reader of the Vital importer (docs/ui-design/11-vital-preset-import.md 1.1).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// No recursion (an explicit stack of at most maxDepth containers), never reads outside [data, data + size), no exceptions except
// std::bad_alloc which the caller maps to budgetExceeded. Duplicate keys are kept (find() returns the last one, as nlohmann does).

#include <cstring>
#include <new>

#include "number.h"
#include "vital_import_internal.h"

namespace synthyboy
{
namespace vitalimport
{
namespace json
{
int Doc::find (int object, const char* key) const noexcept
{
    if (object < 0 || nodes[static_cast<size_t> (object)].type != Type::object)
        return -1;
    int found = -1;
    for (int c = nodes[static_cast<size_t> (object)].first; c >= 0; c = nodes[static_cast<size_t> (c)].next)
        if (nodes[static_cast<size_t> (c)].key >= 0 && strings[static_cast<size_t> (nodes[static_cast<size_t> (c)].key)] == key)
            found = c;
    return found;
}

namespace
{
struct Parser
{
    const char* p;
    const char* end;
    const char* begin;
    Doc& doc;
    size_t budget;     // bytes still available
    bool overBudget = false;

    bool atEnd() const { return p >= end; }
    void skipWs()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    bool charge (size_t bytes)
    {
        if (bytes > budget)
        {
            overBudget = true;
            return false;
        }
        budget -= bytes;
        return true;
    }

    static void appendUtf8 (std::string& s, uint32_t cp)
    {
        if (cp < 0x80)
            s += static_cast<char> (cp);
        else if (cp < 0x800)
        {
            s += static_cast<char> (0xC0 | (cp >> 6));
            s += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            s += static_cast<char> (0xE0 | (cp >> 12));
            s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else
        {
            s += static_cast<char> (0xF0 | (cp >> 18));
            s += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char> (0x80 | (cp & 0x3F));
        }
    }

    bool hex4 (uint32_t& out)
    {
        if (end - p < 4)
            return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t> (c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t> (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t> (c - 'A' + 10);
            else return false;
        }
        p += 4;
        out = v;
        return true;
    }

    /** At the opening quote. Returns the index of the new string, -1 on a syntax error or over budget. */
    int32_t parseString()
    {
        ++p; // opening quote
        std::string s;
        for (;;)
        {
            if (p >= end)
                return -1;
            const char c = *p++;
            if (c == '"')
                break;
            if (c != '\\')
            {
                s += c; // raw control characters and invalid UTF-8 are accepted here and cleaned when the text is used
                continue;
            }
            if (p >= end)
                return -1;
            const char e = *p++;
            switch (e)
            {
                case '"': s += '"'; break;
                case '\\': s += '\\'; break;
                case '/': s += '/'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'u':
                {
                    uint32_t cp;
                    if (! hex4 (cp))
                        return -1;
                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        uint32_t lo = 0;
                        if (end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                        {
                            const char* save = p;
                            p += 2;
                            if (hex4 (lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else
                            {
                                p = save;
                                cp = 0xFFFD;
                            }
                        }
                        else
                            cp = 0xFFFD; // lone high surrogate
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        cp = 0xFFFD; // lone low surrogate
                    appendUtf8 (s, cp);
                    break;
                }
                default: return -1;
            }
        }
        if (! charge (s.size() + sizeof (std::string)))
            return -1;
        doc.strings.push_back (std::move (s));
        return static_cast<int32_t> (doc.strings.size() - 1);
    }

    int32_t newNode (Type type, int parent, int32_t key)
    {
        if (! charge (sizeof (Node)))
            return -1;
        doc.nodes.emplace_back();
        const int32_t idx = static_cast<int32_t> (doc.nodes.size() - 1);
        Node& n = doc.nodes.back();
        n.type = type;
        n.key = key;
        if (parent >= 0)
        {
            Node& par = doc.nodes[static_cast<size_t> (parent)];
            if (par.last >= 0)
                doc.nodes[static_cast<size_t> (par.last)].next = idx;
            else
                par.first = idx;
            par.last = idx;
            ++par.count;
        }
        return idx;
    }

    bool matchLiteral (const char* lit)
    {
        const size_t n = std::strlen (lit);
        if (static_cast<size_t> (end - p) < n || std::memcmp (p, lit, n) != 0)
            return false;
        p += n;
        return true;
    }
};
} // namespace

Status parse (const char* data, size_t size, int maxDepth, size_t maxWorkingBytes, Doc& out) noexcept
{
    try
    {
        out.nodes.clear();
        out.strings.clear();
        out.errorOffset = 0;
        Parser ps { data, data + size, data, out, maxWorkingBytes };
        // BOM
        if (size >= 3 && static_cast<unsigned char> (data[0]) == 0xEF && static_cast<unsigned char> (data[1]) == 0xBB && static_cast<unsigned char> (data[2]) == 0xBF)
            ps.p += 3;

        // explicit stack of the open containers
        constexpr int kHardDepth = 64;
        if (maxDepth > kHardDepth)
            maxDepth = kHardDepth;
        int stack[kHardDepth + 1];
        int depth = 0;

        const auto fail = [&] (Status s) { out.errorOffset = static_cast<size_t> (ps.p - ps.begin); return s; };

        int32_t pendingKey = -1;
        bool haveRoot = false;

        for (;;)
        {
            // ---- a value is expected here -------------------------------------------------------------------------------
            ps.skipWs();
            if (ps.atEnd())
                return fail (Status::malformed);
            const int parent = depth > 0 ? stack[depth - 1] : -1;
            const char c = *ps.p;
            bool closedEmpty = false;

            if ((c == ']' || c == '}') && depth > 0)
            {
                // an empty container: "[]" / "{}" (a closing bracket right after the opening one)
                const bool isArray = out.nodes[static_cast<size_t> (stack[depth - 1])].type == Type::array;
                if (pendingKey < 0 && out.nodes[static_cast<size_t> (stack[depth - 1])].count == 0 && ((c == ']') == isArray))
                {
                    ++ps.p;
                    --depth;
                    closedEmpty = true;
                }
                else
                    return fail (Status::malformed);
            }

            if (! closedEmpty)
            {
                if (c == '{' || c == '[')
                {
                    if (depth >= maxDepth)
                        return fail (Status::tooDeep);
                    const int32_t idx = ps.newNode (c == '{' ? Type::object : Type::array, parent, pendingKey);
                    if (idx < 0)
                        return fail (Status::budget);
                    pendingKey = -1;
                    if (parent < 0)
                        haveRoot = true;
                    stack[depth++] = idx;
                    ++ps.p;
                    // first element (or the closing bracket) is handled by the next iteration; an object needs its key first
                    ps.skipWs();
                    if (c == '{' && ! ps.atEnd() && *ps.p == '"')
                    {
                        pendingKey = ps.parseString();
                        if (pendingKey < 0)
                            return fail(ps.overBudget ? Status::budget : Status::malformed);
                        ps.skipWs();
                        if (ps.atEnd() || *ps.p != ':')
                            return fail (Status::malformed);
                        ++ps.p;
                    }
                    else if (c == '{' && (ps.atEnd() || *ps.p != '}'))
                        return fail (Status::malformed);
                    continue;
                }

                // scalar
                int32_t idx = -1;
                if (c == '"')
                {
                    const int32_t s = ps.parseString();
                    if (s < 0)
                        return fail (ps.overBudget ? Status::budget : Status::malformed);
                    idx = ps.newNode (Type::string, parent, pendingKey);
                    if (idx < 0)
                        return fail (Status::budget);
                    out.nodes[static_cast<size_t> (idx)].str = s;
                }
                else if (c == '-' || (c >= '0' && c <= '9'))
                {
                    double v = 0.0;
                    const char* next = ps.p;
                    // the strict JSON grammar: no leading zeros, no "+", no hex, no NaN / Infinity
                    if (! number::parseDecimal (ps.p, ps.end, true, v, next) || next == ps.p)
                        return fail (Status::malformed);
                    ps.p = next;
                    idx = ps.newNode (Type::number, parent, pendingKey);
                    if (idx < 0)
                        return fail (Status::budget);
                    out.nodes[static_cast<size_t> (idx)].number = v;
                }
                else if (c == 't' || c == 'f' || c == 'n')
                {
                    const bool isTrue = c == 't';
                    if (! (c == 't' ? ps.matchLiteral ("true") : (c == 'f' ? ps.matchLiteral ("false") : ps.matchLiteral ("null"))))
                        return fail (Status::malformed);
                    idx = ps.newNode (c == 'n' ? Type::null : Type::boolean, parent, pendingKey);
                    if (idx < 0)
                        return fail (Status::budget);
                    out.nodes[static_cast<size_t> (idx)].boolean = isTrue;
                }
                else
                    return fail (Status::malformed);
                pendingKey = -1;
                if (parent < 0)
                    haveRoot = true;
            }

            // ---- after a value: close containers / next member -----------------------------------------------------------
            for (;;)
            {
                if (depth == 0)
                    break;
                ps.skipWs();
                if (ps.atEnd())
                    return fail (Status::malformed);
                const char d = *ps.p;
                const Type ct = out.nodes[static_cast<size_t> (stack[depth - 1])].type;
                if (d == ',')
                {
                    ++ps.p;
                    ps.skipWs();
                    if (ct == Type::object)
                    {
                        if (ps.atEnd() || *ps.p != '"')
                            return fail (Status::malformed);
                        pendingKey = ps.parseString();
                        if (pendingKey < 0)
                            return fail (ps.overBudget ? Status::budget : Status::malformed);
                        ps.skipWs();
                        if (ps.atEnd() || *ps.p != ':')
                            return fail (Status::malformed);
                        ++ps.p;
                    }
                    break; // next value
                }
                if ((d == ']' && ct == Type::array) || (d == '}' && ct == Type::object))
                {
                    ++ps.p;
                    --depth;
                    continue;
                }
                return fail (Status::malformed);
            }
            if (depth == 0)
                break;
        }

        if (! haveRoot)
            return fail (Status::malformed);
        ps.skipWs();
        if (! ps.atEnd())
            return fail (Status::malformed);
        return Status::ok;
    }
    catch (const std::bad_alloc&)
    {
        return Status::budget;
    }
}
} // namespace json
} // namespace vitalimport
} // namespace synthyboy
