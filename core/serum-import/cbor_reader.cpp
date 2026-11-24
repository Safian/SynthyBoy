// SynthyBoy - core/serum-import: bounded CBOR (RFC 8949) reader for the body of a .SerumPreset.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Definite lengths only (the Serum writer never emits indefinite ones; they are rejected). Numeric arrays are stored compactly. Depth and memory are bounded.
#include <cmath>
#include <cstring>

#include "serum_internal.h"

namespace synthyboy::serumimport::cbor
{
namespace
{
struct Parser
{
    const uint8_t* p;
    const uint8_t* end;
    Doc& doc;
    int maxDepth;
    size_t budget; // bytes still allowed
    Status status = Status::ok;

    bool fail (Status s) { if (status == Status::ok) status = s; return false; }
    bool charge (size_t bytes) { if (bytes > budget) return fail (Status::budget); budget -= bytes; return true; }

    bool head (int& major, int& ai, uint64_t& arg)
    {
        if (p >= end) return fail (Status::malformed);
        const uint8_t ib = *p++;
        major = ib >> 5;
        ai = ib & 31;
        arg = 0;
        if (major == 7) return true; // simple / float: the argument is read by the caller
        if (ai < 24) { arg = static_cast<uint64_t> (ai); return true; }
        const int n = ai == 24 ? 1 : ai == 25 ? 2 : ai == 26 ? 4 : ai == 27 ? 8 : 0;
        if (n == 0) return fail (Status::malformed); // 28..30 reserved, 31 indefinite
        if (end - p < n) return fail (Status::malformed);
        for (int i = 0; i < n; ++i) arg = (arg << 8) | *p++;
        return true;
    }

    static double halfToDouble (uint16_t h)
    {
        const int e = (h >> 10) & 31, m = h & 1023;
        double v = e == 0 ? std::ldexp (static_cast<double> (m), -24) : (e == 31 ? (m ? NAN : INFINITY) : std::ldexp (static_cast<double> (m + 1024), e - 25));
        return (h & 0x8000) ? -v : v;
    }

    bool readReal (int ai, double& d)
    {
        if (ai == 25) { if (end - p < 2) return fail (Status::malformed); d = halfToDouble (static_cast<uint16_t> ((p[0] << 8) | p[1])); p += 2; return true; }
        if (ai == 26)
        {
            if (end - p < 4) return fail (Status::malformed);
            uint32_t u = (static_cast<uint32_t> (p[0]) << 24) | (static_cast<uint32_t> (p[1]) << 16) | (static_cast<uint32_t> (p[2]) << 8) | p[3];
            float f;
            std::memcpy (&f, &u, 4);
            d = f;
            p += 4;
            return true;
        }
        if (end - p < 8) return fail (Status::malformed);
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i) u = (u << 8) | p[i];
        std::memcpy (&d, &u, 8);
        p += 8;
        return true;
    }

    static bool numericHead (uint8_t ib)
    {
        const int major = ib >> 5, ai = ib & 31;
        return major <= 1 || (major == 7 && (ai == 25 || ai == 26 || ai == 27));
    }

    // returns the node index, or -1 on failure
    long item (int depth)
    {
        if (depth > maxDepth) { fail (Status::tooDeep); return -1; }
        if (! charge (sizeof (Node))) return -1;
        int major, ai;
        uint64_t arg;
        if (! head (major, ai, arg)) return -1;
        doc.nodes.emplace_back();
        const uint32_t idx = static_cast<uint32_t> (doc.nodes.size() - 1);
        Node n;
        switch (major)
        {
            case 0: n.type = Type::integer; n.i = static_cast<int64_t> (arg); n.d = static_cast<double> (arg); break;
            case 1: n.type = Type::integer; n.i = -1 - static_cast<int64_t> (arg); n.d = static_cast<double> (n.i); break;
            case 2:
            case 3:
            {
                if (arg > static_cast<uint64_t> (end - p)) { fail (Status::malformed); return -1; }
                if (! charge (static_cast<size_t> (arg))) return -1;
                n.type = major == 2 ? Type::bytes : Type::text;
                n.str = static_cast<uint32_t> (doc.blob.size());
                n.len = static_cast<uint32_t> (arg);
                doc.blob.insert (doc.blob.end(), reinterpret_cast<const char*> (p), reinterpret_cast<const char*> (p) + arg);
                p += arg;
                break;
            }
            case 4:
            {
                n.type = Type::array;
                if (arg > static_cast<uint64_t> (end - p)) { fail (Status::malformed); return -1; } // every element needs at least a byte
                n.count = static_cast<uint32_t> (arg);
                if (! parseArray (n, depth)) return -1;
                break;
            }
            case 5:
            {
                n.type = Type::map;
                if (arg > static_cast<uint64_t> (end - p) / 2) { fail (Status::malformed); return -1; }
                n.count = static_cast<uint32_t> (arg);
                std::vector<uint32_t> tmp;
                tmp.reserve (static_cast<size_t> (arg) * 2);
                for (uint64_t k = 0; k < arg * 2; ++k)
                {
                    const long c = item (depth + 1);
                    if (c < 0) return -1;
                    tmp.push_back (static_cast<uint32_t> (c));
                }
                if (! charge (tmp.size() * 4)) return -1;
                n.first = static_cast<uint32_t> (doc.kids.size());
                doc.kids.insert (doc.kids.end(), tmp.begin(), tmp.end());
                break;
            }
            case 6: // tag: the tagged item replaces the tag
            {
                doc.nodes.pop_back();
                budget += sizeof (Node);
                return item (depth + 1);
            }
            default:
                if (ai == 20 || ai == 21) { n.type = Type::boolean; n.b = ai == 21; }
                else if (ai == 22 || ai == 23) n.type = Type::null;
                else if (ai >= 25 && ai <= 27) { n.type = Type::real; if (! readReal (ai, n.d)) return -1; }
                else if (ai == 24) { if (p >= end) { fail (Status::malformed); return -1; } ++p; n.type = Type::null; }
                else if (ai < 24) n.type = Type::null;
                else { fail (Status::malformed); return -1; }
                break;
        }
        doc.nodes[idx] = n;
        return static_cast<long> (idx);
    }

    bool parseArray (Node& n, int depth)
    {
        std::vector<uint32_t> tmp;
        std::vector<double> nums;
        const uint64_t count = n.count;
        uint64_t k = 0;
        // fast path: numbers only
        while (k < count && p < end && numericHead (*p))
        {
            int major, ai;
            uint64_t arg;
            if (! head (major, ai, arg)) return false;
            double d;
            if (major == 0) d = static_cast<double> (arg);
            else if (major == 1) d = -1.0 - static_cast<double> (arg);
            else if (! readReal (ai, d)) return false;
            nums.push_back (d);
            ++k;
        }
        if (k == count && count > 0)
        {
            if (! charge (nums.size() * sizeof (double))) return false;
            n.floatArray = true;
            n.numFirst = static_cast<uint32_t> (doc.nums.size());
            doc.nums.insert (doc.nums.end(), nums.begin(), nums.end());
            return true;
        }
        // mixed: the numbers read so far become integer / real nodes
        tmp.reserve (static_cast<size_t> (count));
        if (! charge (nums.size() * sizeof (Node))) return false;
        for (double d : nums)
        {
            Node e;
            e.type = (std::floor (d) == d && std::fabs (d) < 9e15) ? Type::integer : Type::real;
            e.d = d;
            e.i = static_cast<int64_t> (d);
            doc.nodes.push_back (e);
            tmp.push_back (static_cast<uint32_t> (doc.nodes.size() - 1));
        }
        for (; k < count; ++k)
        {
            const long c = item (depth + 1);
            if (c < 0) return false;
            tmp.push_back (static_cast<uint32_t> (c));
        }
        if (! charge (tmp.size() * 4)) return false;
        n.first = static_cast<uint32_t> (doc.kids.size());
        doc.kids.insert (doc.kids.end(), tmp.begin(), tmp.end());
        return true;
    }
};
} // namespace

Status parse (const uint8_t* data, size_t size, int maxDepth, size_t maxWorkingBytes, Doc& out) noexcept
{
    try
    {
        out = Doc {};
        Parser ps { data, data + size, out, maxDepth, maxWorkingBytes };
        const long root = ps.item (0);
        if (root < 0) { out.errorOffset = static_cast<size_t> (ps.p - data); return ps.status == Status::ok ? Status::malformed : ps.status; }
        if (ps.p != ps.end) { out.errorOffset = static_cast<size_t> (ps.p - data); return Status::malformed; }
        return Status::ok;
    }
    catch (...)
    {
        return Status::budget;
    }
}

std::string Doc::text (uint32_t node) const
{
    const Node& n = nodes[node];
    if (n.type != Type::text) return {};
    return std::string (blob.data() + n.str, n.len);
}

bool Doc::isText (uint32_t node, const char* s) const
{
    const Node& n = nodes[node];
    return n.type == Type::text && n.len == std::strlen (s) && std::memcmp (blob.data() + n.str, s, n.len) == 0;
}

int Doc::find (uint32_t map, const char* key) const noexcept
{
    const Node& m = nodes[map];
    if (m.type != Type::map) return -1;
    int found = -1;
    for (uint32_t k = 0; k < m.count; ++k)
        if (isText (kids[m.first + 2 * k], key)) found = static_cast<int> (kids[m.first + 2 * k + 1]);
    return found;
}

uint32_t Doc::arraySize (uint32_t node) const noexcept
{
    const Node& n = nodes[node];
    return n.type == Type::array ? n.count : 0;
}

double Doc::arrayNumber (uint32_t node, uint32_t k) const noexcept
{
    const Node& n = nodes[node];
    if (n.type != Type::array || k >= n.count) return 0.0;
    if (n.floatArray) return nums[n.numFirst + k];
    return number (kids[n.first + k]);
}

double Doc::number (uint32_t node, double fallback) const noexcept
{
    const Node& n = nodes[node];
    return (n.type == Type::integer || n.type == Type::real) ? n.d : fallback;
}
} // namespace synthyboy::serumimport::cbor
