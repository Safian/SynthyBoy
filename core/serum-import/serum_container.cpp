// SynthyBoy - core/serum-import: the container of a Serum 2 file (header JSON + size + format + compressed body), inflate and UBJSON.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// docs/ui-design/19-serum-preset-format.md. Layout: "XferJson\0", uint64 LE header length N, N bytes of JSON (a trailing NUL is part of N in the old
// files), uint32 LE size of the decoded body, uint32 LE format (2 = zstd + CBOR, 0 = zlib + UBJSON), then the compressed body.
#include <algorithm>
#include <cstring>

#include "serum_internal.h"
#include "vital_import_internal.h"

namespace synthyboy::serumimport
{
namespace
{
// ----- inflate ---------------------------------------------------------------------------------------------------------------------------
struct InflateBits
{
    const uint8_t* p;
    size_t n, pos = 0; // pos in bits
    bool bad = false;
    uint32_t bits (int count)
    {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i)
        {
            if ((pos >> 3) >= n) { bad = true; return v; }
            v |= static_cast<uint32_t> ((p[pos >> 3] >> (pos & 7)) & 1u) << i;
            ++pos;
        }
        return v;
    }
};

struct Huff
{
    uint16_t count[16] = {};
    uint16_t symbol[320] = {};
    bool build (const uint8_t* lengths, int n)
    {
        std::memset (count, 0, sizeof (count));
        for (int i = 0; i < n; ++i) ++count[lengths[i]];
        if (count[0] == n) return true;
        int left = 1;
        for (int len = 1; len <= 15; ++len) { left <<= 1; left -= count[len]; if (left < 0) return false; }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t> (offs[len] + count[len]);
        for (int i = 0; i < n; ++i) if (lengths[i]) symbol[offs[lengths[i]]++] = static_cast<uint16_t> (i);
        return true;
    }
    int decode (InflateBits& br) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len)
        {
            code |= static_cast<int> (br.bits (1));
            const int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
            if (br.bad) return -1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint16_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint16_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
} // namespace

bool inflateZlib (const uint8_t* src, size_t size, size_t maxOut, std::vector<uint8_t>& out) noexcept
{
    try
    {
        if (size < 6 || (src[0] & 15) != 8 || ((src[0] << 8) | src[1]) % 31 != 0 || (src[1] & 0x20)) return false;
        InflateBits br { src + 2, size - 2 };
        const size_t base = out.size();
        bool last = false;
        while (! last)
        {
            last = br.bits (1) != 0;
            const uint32_t type = br.bits (2);
            if (br.bad) return false;
            if (type == 0)
            {
                br.pos = (br.pos + 7) & ~size_t { 7 };
                const size_t at = br.pos >> 3;
                if (at + 4 > br.n) return false;
                const size_t len = br.p[at] | (br.p[at + 1] << 8), nlen = br.p[at + 2] | (br.p[at + 3] << 8);
                if ((len ^ nlen) != 0xFFFF || at + 4 + len > br.n || out.size() - base + len > maxOut) return false;
                out.insert (out.end(), br.p + at + 4, br.p + at + 4 + len);
                br.pos += (4 + len) * 8;
                continue;
            }
            if (type == 3) return false;
            Huff lit, dist;
            if (type == 1)
            {
                uint8_t l[288];
                for (int i = 0; i < 144; ++i) l[i] = 8;
                for (int i = 144; i < 256; ++i) l[i] = 9;
                for (int i = 256; i < 280; ++i) l[i] = 7;
                for (int i = 280; i < 288; ++i) l[i] = 8;
                lit.build (l, 288);
                uint8_t d[30];
                for (auto& x : d) x = 5;
                dist.build (d, 30);
            }
            else
            {
                const int nlen = static_cast<int> (br.bits (5)) + 257, ndist = static_cast<int> (br.bits (5)) + 1, ncode = static_cast<int> (br.bits (4)) + 4;
                if (nlen > 286 || ndist > 30) return false;
                static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                uint8_t cl[19] = {};
                for (int i = 0; i < ncode; ++i) cl[order[i]] = static_cast<uint8_t> (br.bits (3));
                Huff clh;
                if (! clh.build (cl, 19)) return false;
                uint8_t lens[320] = {};
                int idx = 0;
                while (idx < nlen + ndist)
                {
                    const int sym = clh.decode (br);
                    if (sym < 0) return false;
                    if (sym < 16) lens[idx++] = static_cast<uint8_t> (sym);
                    else
                    {
                        int rep, val = 0;
                        if (sym == 16) { if (idx == 0) return false; val = lens[idx - 1]; rep = 3 + static_cast<int> (br.bits (2)); }
                        else if (sym == 17) rep = 3 + static_cast<int> (br.bits (3));
                        else rep = 11 + static_cast<int> (br.bits (7));
                        if (idx + rep > nlen + ndist) return false;
                        while (rep--) lens[idx++] = static_cast<uint8_t> (val);
                    }
                }
                if (lens[256] == 0 || ! lit.build (lens, nlen) || ! dist.build (lens + nlen, ndist)) return false;
            }
            for (;;)
            {
                const int sym = lit.decode (br);
                if (sym < 0 || br.bad) return false;
                if (sym < 256)
                {
                    if (out.size() - base >= maxOut) return false;
                    out.push_back (static_cast<uint8_t> (sym));
                }
                else if (sym == 256) break;
                else
                {
                    const int li = sym - 257;
                    if (li >= 29) return false;
                    const size_t len = kLenBase[li] + br.bits (kLenExtra[li]);
                    const int ds = dist.decode (br);
                    if (ds < 0 || ds >= 30) return false;
                    const size_t d = kDistBase[ds] + br.bits (kDistExtra[ds]);
                    if (br.bad || d > out.size() - base || out.size() - base + len > maxOut) return false;
                    size_t from = out.size() - d;
                    for (size_t k = 0; k < len; ++k) out.push_back (out[from + k]);
                }
            }
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

namespace
{
// ----- UBJSON ----------------------------------------------------------------------------------------------------------------------------
struct Ub
{
    const uint8_t* p;
    const uint8_t* end;
    cbor::Doc& doc;
    int maxDepth;
    size_t budget;
    cbor::Status status = cbor::Status::ok;

    bool fail (cbor::Status s) { if (status == cbor::Status::ok) status = s; return false; }
    bool charge (size_t n) { if (n > budget) return fail (cbor::Status::budget); budget -= n; return true; }
    bool need (size_t n) { if (static_cast<size_t> (end - p) < n) return fail (cbor::Status::malformed); return true; }
    uint64_t be (int n) { uint64_t v = 0; for (int i = 0; i < n; ++i) v = (v << 8) | *p++; return v; }

    bool length (uint8_t marker, uint64_t& len)
    {
        switch (marker)
        {
            case 'i': if (! need (1)) return false; len = be (1); return static_cast<int8_t> (len) >= 0 || fail (cbor::Status::malformed);
            case 'U': if (! need (1)) return false; len = be (1); return true;
            case 'I': if (! need (2)) return false; len = be (2); return static_cast<int16_t> (len) >= 0 || fail (cbor::Status::malformed);
            case 'l': if (! need (4)) return false; len = be (4); return static_cast<int32_t> (len) >= 0 || fail (cbor::Status::malformed);
            case 'L': if (! need (8)) return false; len = be (8); return static_cast<int64_t> (len) >= 0 || fail (cbor::Status::malformed);
            default: return fail (cbor::Status::malformed);
        }
    }

    bool lengthPrefixed (uint64_t& len)
    {
        if (! need (1)) return false;
        const uint8_t m = *p++;
        return length (m, len);
    }

    /** A scalar of the given marker; fills `n`. Containers are handled by the caller. */
    bool scalar (uint8_t m, cbor::Node& n)
    {
        using cbor::Type;
        switch (m)
        {
            case 'Z': case 'N': n.type = Type::null; return true;
            case 'T': n.type = Type::boolean; n.b = true; return true;
            case 'F': n.type = Type::boolean; n.b = false; return true;
            case 'i': if (! need (1)) return false; n.type = Type::integer; n.i = static_cast<int8_t> (be (1)); n.d = static_cast<double> (n.i); return true;
            case 'U': if (! need (1)) return false; n.type = Type::integer; n.i = static_cast<int64_t> (be (1)); n.d = static_cast<double> (n.i); return true;
            case 'I': if (! need (2)) return false; n.type = Type::integer; n.i = static_cast<int16_t> (be (2)); n.d = static_cast<double> (n.i); return true;
            case 'l': if (! need (4)) return false; n.type = Type::integer; n.i = static_cast<int32_t> (be (4)); n.d = static_cast<double> (n.i); return true;
            case 'L': if (! need (8)) return false; n.type = Type::integer; n.i = static_cast<int64_t> (be (8)); n.d = static_cast<double> (n.i); return true;
            case 'd': { if (! need (4)) return false; const uint32_t u = static_cast<uint32_t> (be (4)); float f; std::memcpy (&f, &u, 4); n.type = Type::real; n.d = f; return true; }
            case 'D': { if (! need (8)) return false; const uint64_t u = be (8); double d; std::memcpy (&d, &u, 8); n.type = Type::real; n.d = d; return true; }
            case 'C': if (! need (1)) return false; n.type = Type::text; n.str = static_cast<uint32_t> (doc.blob.size()); n.len = 1; doc.blob.push_back (static_cast<char> (*p++)); return true;
            case 'S': case 'H':
            {
                uint64_t len;
                if (! lengthPrefixed (len) || ! need (static_cast<size_t> (len)) || ! charge (static_cast<size_t> (len))) return false;
                n.type = Type::text;
                n.str = static_cast<uint32_t> (doc.blob.size());
                n.len = static_cast<uint32_t> (len);
                doc.blob.insert (doc.blob.end(), reinterpret_cast<const char*> (p), reinterpret_cast<const char*> (p) + len);
                p += len;
                return true;
            }
            default: return fail (cbor::Status::malformed);
        }
    }

    static bool numberMarker (uint8_t m) { return m == 'i' || m == 'U' || m == 'I' || m == 'l' || m == 'L' || m == 'd' || m == 'D'; }

    long value (int depth, int forcedMarker = -1)
    {
        if (depth > maxDepth) { fail (cbor::Status::tooDeep); return -1; }
        if (! charge (sizeof (cbor::Node))) return -1;
        int m = forcedMarker;
        if (m < 0)
        {
            do { if (! need (1)) return -1; m = *p++; } while (m == 'N');
        }
        doc.nodes.emplace_back();
        const uint32_t idx = static_cast<uint32_t> (doc.nodes.size() - 1);
        cbor::Node n;
        if (m == '[')
        {
            n.type = cbor::Type::array;
            int elemType = -1;
            long long count = -1;
            if (need (1) && *p == '$') { ++p; if (! need (1)) return -1; elemType = *p++; }
            if (need (1) && *p == '#') { ++p; uint64_t c; if (! lengthPrefixed (c)) return -1; count = static_cast<long long> (c); }
            std::vector<uint32_t> tmp;
            std::vector<double> nums;
            bool allNum = true;
            for (long long k = 0;; ++k)
            {
                if (count >= 0) { if (k >= count) break; }
                else
                {
                    if (! need (1)) return -1;
                    if (*p == ']') { ++p; break; }
                }
                if (elemType >= 0 && numberMarker (static_cast<uint8_t> (elemType)))
                {
                    cbor::Node e;
                    if (! scalar (static_cast<uint8_t> (elemType), e)) return -1;
                    nums.push_back (e.d);
                    continue;
                }
                if (allNum && elemType < 0 && need (1) && numberMarker (*p))
                {
                    cbor::Node e;
                    const uint8_t mk = *p++;
                    if (! scalar (mk, e)) return -1;
                    nums.push_back (e.d);
                    continue;
                }
                if (allNum)
                {
                    allNum = false;
                    if (! charge (nums.size() * sizeof (cbor::Node))) return -1;
                    for (double d : nums)
                    {
                        cbor::Node e;
                        e.type = (d == static_cast<double> (static_cast<int64_t> (d))) ? cbor::Type::integer : cbor::Type::real;
                        e.d = d;
                        e.i = static_cast<int64_t> (d);
                        doc.nodes.push_back (e);
                        tmp.push_back (static_cast<uint32_t> (doc.nodes.size() - 1));
                    }
                    nums.clear();
                }
                const long c = value (depth + 1, elemType);
                if (c < 0) return -1;
                tmp.push_back (static_cast<uint32_t> (c));
            }
            if (allNum && ! nums.empty())
            {
                if (! charge (nums.size() * sizeof (double))) return -1;
                n.floatArray = true;
                n.count = static_cast<uint32_t> (nums.size());
                n.numFirst = static_cast<uint32_t> (doc.nums.size());
                doc.nums.insert (doc.nums.end(), nums.begin(), nums.end());
            }
            else
            {
                if (! charge (tmp.size() * 4)) return -1;
                n.count = static_cast<uint32_t> (tmp.size());
                n.first = static_cast<uint32_t> (doc.kids.size());
                doc.kids.insert (doc.kids.end(), tmp.begin(), tmp.end());
            }
        }
        else if (m == '{')
        {
            n.type = cbor::Type::map;
            int elemType = -1;
            long long count = -1;
            if (need (1) && *p == '$') { ++p; if (! need (1)) return -1; elemType = *p++; }
            if (need (1) && *p == '#') { ++p; uint64_t c; if (! lengthPrefixed (c)) return -1; count = static_cast<long long> (c); }
            std::vector<uint32_t> tmp;
            for (long long k = 0;; ++k)
            {
                if (count >= 0) { if (k >= count) break; }
                else
                {
                    if (! need (1)) return -1;
                    if (*p == '}') { ++p; break; }
                }
                uint64_t klen;
                if (! lengthPrefixed (klen) || ! need (static_cast<size_t> (klen)) || ! charge (static_cast<size_t> (klen) + sizeof (cbor::Node))) return -1;
                cbor::Node key;
                key.type = cbor::Type::text;
                key.str = static_cast<uint32_t> (doc.blob.size());
                key.len = static_cast<uint32_t> (klen);
                doc.blob.insert (doc.blob.end(), reinterpret_cast<const char*> (p), reinterpret_cast<const char*> (p) + klen);
                p += klen;
                doc.nodes.push_back (key);
                tmp.push_back (static_cast<uint32_t> (doc.nodes.size() - 1));
                const long v = value (depth + 1, elemType);
                if (v < 0) return -1;
                tmp.push_back (static_cast<uint32_t> (v));
            }
            if (! charge (tmp.size() * 4)) return -1;
            n.count = static_cast<uint32_t> (tmp.size() / 2);
            n.first = static_cast<uint32_t> (doc.kids.size());
            doc.kids.insert (doc.kids.end(), tmp.begin(), tmp.end());
        }
        else if (! scalar (static_cast<uint8_t> (m), n))
            return -1;
        doc.nodes[idx] = n;
        return static_cast<long> (idx);
    }
};
} // namespace

cbor::Status parseUbjson (const uint8_t* data, size_t size, int maxDepth, size_t maxWorkingBytes, cbor::Doc& out) noexcept
{
    try
    {
        out = cbor::Doc {};
        Ub ub { data, data + size, out, maxDepth, maxWorkingBytes };
        if (ub.value (0) < 0) { out.errorOffset = static_cast<size_t> (ub.p - data); return ub.status == cbor::Status::ok ? cbor::Status::malformed : ub.status; }
        if (ub.p != ub.end) { out.errorOffset = static_cast<size_t> (ub.p - data); return cbor::Status::malformed; }
        return cbor::Status::ok;
    }
    catch (...)
    {
        return cbor::Status::budget;
    }
}

const char* containerStatusText (ContainerStatus s) noexcept
{
    switch (s)
    {
        case ContainerStatus::ok: return "ok";
        case ContainerStatus::notSerum: return "not a Serum file";
        case ContainerStatus::truncated: return "truncated file";
        case ContainerStatus::unsupportedFormat: return "unsupported body format";
        case ContainerStatus::decompressFailed: return "the body does not decompress";
        case ContainerStatus::malformedBody: return "the body is malformed";
        case ContainerStatus::tooLarge: return "file too large";
        case ContainerStatus::budget: return "memory budget exceeded";
    }
    return "?";
}

ContainerStatus openContainer (const uint8_t* data, size_t size, const ContainerLimits& limits, Container& out) noexcept
{
    try
    {
        out = Container {};
        if (size < 17 || std::memcmp (data, "XferJson\0", 9) != 0) return ContainerStatus::notSerum;
        if (size > limits.maxFileBytes) return ContainerStatus::tooLarge;
        uint64_t n = 0;
        for (int i = 7; i >= 0; --i) n = (n << 8) | data[9 + i];
        if (n > size - 17) return ContainerStatus::truncated;
        size_t jsonLen = static_cast<size_t> (n);
        while (jsonLen > 0 && data[17 + jsonLen - 1] == 0) --jsonLen;
        out.headerText.assign (reinterpret_cast<const char*> (data + 17), jsonLen);
        vitalimport::json::Doc hdr;
        if (vitalimport::json::parse (out.headerText.data(), out.headerText.size(), 6, 1u << 20, hdr) != vitalimport::json::Status::ok || hdr.nodes.empty()
            || hdr.root().type != vitalimport::json::Type::object)
            return ContainerStatus::notSerum;
        const int ft = hdr.find (0, "fileType");
        if (ft >= 0 && hdr.nodes[static_cast<size_t> (ft)].type == vitalimport::json::Type::string)
            out.fileType = hdr.text (hdr.nodes[static_cast<size_t> (ft)].str);
        const uint8_t* rest = data + 17 + static_cast<size_t> (n);
        const size_t restLen = size - 17 - static_cast<size_t> (n);
        if (restLen < 8) return ContainerStatus::truncated;
        uint32_t usz = 0, fmt = 0;
        for (int i = 3; i >= 0; --i) { usz = (usz << 8) | rest[i]; fmt = (fmt << 8) | rest[4 + i]; }
        out.format = fmt;
        out.declaredBodySize = usz;
        if (usz > limits.maxBodyBytes) return ContainerStatus::tooLarge;
        std::vector<uint8_t> body;
        body.reserve (usz);
        if (fmt == 2)
        {
            const ZstdStatus z = zstdDecompress (rest + 8, restLen - 8, limits.maxBodyBytes, body);
            if (z == ZstdStatus::tooLarge) return ContainerStatus::tooLarge;
            if (z != ZstdStatus::ok) return ContainerStatus::decompressFailed;
        }
        else if (fmt == 0)
        {
            if (! inflateZlib (rest + 8, restLen - 8, limits.maxBodyBytes, body)) return ContainerStatus::decompressFailed;
        }
        else
            return ContainerStatus::unsupportedFormat;
        if (body.size() != usz) return ContainerStatus::decompressFailed;
        const cbor::Status s = fmt == 2 ? cbor::parse (body.data(), body.size(), limits.maxDepth, limits.maxWorkingBytes, out.body)
                                        : parseUbjson (body.data(), body.size(), limits.maxDepth, limits.maxWorkingBytes, out.body);
        if (s == cbor::Status::budget) return ContainerStatus::budget;
        if (s != cbor::Status::ok) return ContainerStatus::malformedBody;
        return ContainerStatus::ok;
    }
    catch (...)
    {
        return ContainerStatus::budget;
    }
}
} // namespace synthyboy::serumimport
