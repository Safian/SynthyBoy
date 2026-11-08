// SynthyBoy - core: a small, bounded zip reader (stored + deflate) with an inflate of our own. See zip_reader.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The inflate follows the structure of RFC 1951 (canonical Huffman codes decoded by code length counts, as in zlib's reference "puff");
// written from the RFC, not copied.

#include "synthyboy/zip_reader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace synthyboy
{
namespace
{
uint16_t rd16 (const uint8_t* p) noexcept { return static_cast<uint16_t> (p[0] | (p[1] << 8)); }
uint32_t rd32 (const uint8_t* p) noexcept
{
    return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8) | (static_cast<uint32_t> (p[2]) << 16)
         | (static_cast<uint32_t> (p[3]) << 24);
}

constexpr uint32_t kSigEndOfCentralDir = 0x06054b50u;
constexpr uint32_t kSigZip64Locator = 0x07064b50u;
constexpr uint32_t kSigCentralEntry = 0x02014b50u;
constexpr uint32_t kSigLocalHeader = 0x04034b50u;

// ---- inflate --------------------------------------------------------------------------------------------------------------------

constexpr int kMaxBits = 15;
constexpr int kMaxLitLenCodes = 286;
constexpr int kMaxDistCodes = 30;
constexpr int kFixedLitLenCodes = 288;

struct Huffman
{
    std::array<uint16_t, kMaxBits + 1> count {}; // number of codes of each length
    std::array<uint16_t, kFixedLitLenCodes> symbol {}; // symbols ordered by code
};

class Inflater
{
public:
    Inflater (const uint8_t* input, size_t inSize, uint8_t* output, size_t outSize) noexcept
        : in (input), inLen (inSize), out (output), outCap (outSize) {}

    ZipStatus run() noexcept
    {
        for (;;)
        {
            int last = 0, type = 0;
            if (! bits (1, last) || ! bits (2, type))
                return ZipStatus::corrupt;
            ZipStatus s = ZipStatus::corrupt;
            if (type == 0)
                s = stored();
            else if (type == 1)
                s = fixedBlock();
            else if (type == 2)
                s = dynamicBlock();
            if (s != ZipStatus::ok)
                return s;
            if (last != 0)
                break;
        }
        return outPos == outCap ? ZipStatus::ok : ZipStatus::corrupt; // the stream must end exactly at the declared size
    }

private:
    const uint8_t* in;
    size_t inLen, inPos = 0;
    uint8_t* out;
    size_t outCap, outPos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;

    bool bits (int need, int& value) noexcept
    {
        uint32_t v = bitBuf;
        while (bitCount < need)
        {
            if (inPos >= inLen)
                return false;
            v |= static_cast<uint32_t> (in[inPos++]) << bitCount;
            bitCount += 8;
        }
        bitBuf = v >> need;
        bitCount -= need;
        value = static_cast<int> (v & ((1u << need) - 1u));
        return true;
    }

    ZipStatus stored() noexcept
    {
        bitBuf = 0; // drop the rest of the current byte
        bitCount = 0;
        if (inLen - inPos < 4)
            return ZipStatus::corrupt;
        const unsigned len = rd16 (in + inPos), nlen = rd16 (in + inPos + 2);
        inPos += 4;
        if (len != (~nlen & 0xffffu) || inLen - inPos < len || outCap - outPos < len)
            return ZipStatus::corrupt;
        if (len > 0)
            std::memcpy (out + outPos, in + inPos, len);
        inPos += len;
        outPos += len;
        return ZipStatus::ok;
    }

    /** Builds the canonical code of `n` lengths. Returns 0 for a complete code, > 0 incomplete, < 0 over-subscribed. */
    static int construct (Huffman& h, const uint8_t* lengths, int n) noexcept
    {
        h.count.fill (0);
        for (int i = 0; i < n; ++i)
            ++h.count[lengths[i]];
        if (h.count[0] == n)
            return 0; // no codes: complete (decoding any symbol fails)
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            left <<= 1;
            left -= h.count[len];
            if (left < 0)
                return left;
        }
        std::array<uint16_t, kMaxBits + 2> offs {};
        for (int len = 1; len < kMaxBits; ++len)
            offs[len + 1] = static_cast<uint16_t> (offs[len] + h.count[len]);
        for (int sym = 0; sym < n; ++sym)
            if (lengths[sym] != 0)
                h.symbol[offs[lengths[sym]]++] = static_cast<uint16_t> (sym);
        return left;
    }

    int decode (const Huffman& h) noexcept
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
            int bit = 0;
            if (! bits (1, bit))
                return -1;
            code |= bit;
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[static_cast<size_t> (index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1; // ran out of codes
    }

    ZipStatus codes (const Huffman& lit, const Huffman& dist) noexcept
    {
        static constexpr uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
        static constexpr uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        static constexpr uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
        static constexpr uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

        for (;;)
        {
            int sym = decode (lit);
            if (sym < 0)
                return ZipStatus::corrupt;
            if (sym < 256)
            {
                if (outPos >= outCap)
                    return ZipStatus::corrupt;
                out[outPos++] = static_cast<uint8_t> (sym);
                continue;
            }
            if (sym == 256)
                return ZipStatus::ok;
            sym -= 257;
            if (sym >= 29)
                return ZipStatus::corrupt;
            int extra = 0;
            if (! bits (kLenExtra[sym], extra))
                return ZipStatus::corrupt;
            const size_t len = static_cast<size_t> (kLenBase[sym] + extra);

            const int dsym = decode (dist);
            if (dsym < 0 || dsym >= kMaxDistCodes)
                return ZipStatus::corrupt;
            if (! bits (kDistExtra[dsym], extra))
                return ZipStatus::corrupt;
            const size_t distance = static_cast<size_t> (kDistBase[dsym] + extra);
            if (distance > outPos || outCap - outPos < len)
                return ZipStatus::corrupt;
            for (size_t i = 0; i < len; ++i, ++outPos) // may overlap: byte by byte
                out[outPos] = out[outPos - distance];
        }
    }

    ZipStatus fixedBlock() noexcept
    {
        uint8_t lengths[kFixedLitLenCodes];
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < kFixedLitLenCodes; ++i) lengths[i] = 8;
        Huffman lit, dist;
        construct (lit, lengths, kFixedLitLenCodes);
        uint8_t dl[kMaxDistCodes];
        std::fill (dl, dl + kMaxDistCodes, static_cast<uint8_t> (5));
        construct (dist, dl, kMaxDistCodes);
        return codes (lit, dist);
    }

    ZipStatus dynamicBlock() noexcept
    {
        static constexpr uint8_t kOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
        int nlen = 0, ndist = 0, ncode = 0;
        if (! bits (5, nlen) || ! bits (5, ndist) || ! bits (4, ncode))
            return ZipStatus::corrupt;
        nlen += 257;
        ndist += 1;
        ncode += 4;
        if (nlen > kMaxLitLenCodes || ndist > kMaxDistCodes)
            return ZipStatus::corrupt;

        uint8_t lengths[kMaxLitLenCodes + kMaxDistCodes] = {};
        for (int i = 0; i < ncode; ++i)
        {
            int v = 0;
            if (! bits (3, v))
                return ZipStatus::corrupt;
            lengths[kOrder[i]] = static_cast<uint8_t> (v);
        }
        Huffman lencode;
        if (construct (lencode, lengths, 19) != 0)
            return ZipStatus::corrupt;

        int index = 0;
        uint8_t table[kMaxLitLenCodes + kMaxDistCodes] = {};
        while (index < nlen + ndist)
        {
            const int sym = decode (lencode);
            if (sym < 0)
                return ZipStatus::corrupt;
            if (sym < 16)
            {
                table[index++] = static_cast<uint8_t> (sym);
                continue;
            }
            int len = 0, repeat = 0;
            if (sym == 16)
            {
                if (index == 0)
                    return ZipStatus::corrupt;
                len = table[index - 1];
                if (! bits (2, repeat))
                    return ZipStatus::corrupt;
                repeat += 3;
            }
            else if (sym == 17)
            {
                if (! bits (3, repeat))
                    return ZipStatus::corrupt;
                repeat += 3;
            }
            else
            {
                if (! bits (7, repeat))
                    return ZipStatus::corrupt;
                repeat += 11;
            }
            if (index + repeat > nlen + ndist)
                return ZipStatus::corrupt;
            while (repeat-- > 0)
                table[index++] = static_cast<uint8_t> (len);
        }
        if (table[256] == 0)
            return ZipStatus::corrupt; // no end-of-block code

        Huffman lit, dist;
        const int litLeft = construct (lit, table, nlen);
        if (litLeft < 0 || (litLeft > 0 && nlen - lit.count[0] != 1))
            return ZipStatus::corrupt; // over-subscribed, or incomplete and not the single-code case
        const int distLeft = construct (dist, table + nlen, ndist);
        if (distLeft < 0 || (distLeft > 0 && ndist - dist.count[0] != 1))
            return ZipStatus::corrupt;
        return codes (lit, dist);
    }
};

std::array<uint32_t, 256> makeCrcTable() noexcept
{
    std::array<uint32_t, 256> t {};
    for (uint32_t n = 0; n < 256; ++n)
    {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}

bool pathIsSafe (const std::string& name) noexcept
{
    if (name.empty() || name[0] == '/' || name.find ('\\') != std::string::npos)
        return false;
    if (name.size() >= 2 && name[1] == ':')
        return false; // drive letter
    for (const char c : name)
        if (static_cast<unsigned char> (c) < 0x20)
            return false;
    size_t pos = 0;
    while (pos <= name.size())
    {
        const size_t next = name.find ('/', pos);
        if (name.compare (pos, next == std::string::npos ? std::string::npos : next - pos, "..") == 0)
            return false;
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
    return true;
}
} // namespace

const char* zipStatusName (ZipStatus status) noexcept
{
    switch (status)
    {
        case ZipStatus::ok: return "ok";
        case ZipStatus::notZip: return "not-zip";
        case ZipStatus::truncated: return "truncated";
        case ZipStatus::unsupported: return "unsupported";
        case ZipStatus::corrupt: return "corrupt";
        case ZipStatus::limitExceeded: return "limit-exceeded";
        case ZipStatus::crcMismatch: return "crc-mismatch";
        case ZipStatus::badIndex: return "bad-index";
    }
    return "?";
}

uint32_t crc32Of (const void* data, size_t size, uint32_t seed) noexcept
{
    static const std::array<uint32_t, 256> table = makeCrcTable();
    const auto* p = static_cast<const uint8_t*> (data);
    uint32_t c = seed ^ 0xffffffffu;
    for (size_t i = 0; i < size; ++i)
        c = table[(c ^ p[i]) & 0xffu] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

ZipStatus inflateRaw (const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) noexcept
{
    if (in == nullptr && inSize > 0)
        return ZipStatus::corrupt;
    if (out == nullptr && outSize > 0)
        return ZipStatus::corrupt;
    try
    {
        auto inflater = std::make_unique<Inflater> (in, inSize, out, outSize); // two Huffman tables per block live on the stack, the object is small
        return inflater->run();
    }
    catch (...)
    {
        return ZipStatus::corrupt;
    }
}

bool looksLikeZip (const void* data, size_t size) noexcept
{
    if (data == nullptr || size < 4)
        return false;
    const auto* p = static_cast<const uint8_t*> (data);
    const uint32_t sig = rd32 (p);
    return sig == kSigLocalHeader || sig == kSigEndOfCentralDir;
}

ZipStatus ZipArchive::open (const void* data, size_t size, const ZipLimits& limits) noexcept
{
    list.clear();
    base = nullptr;
    length = 0;
    lim = limits;
    if (data == nullptr || size < 22)
        return ZipStatus::notZip;
    const auto* p = static_cast<const uint8_t*> (data);

    // end of central directory: the last 22 + comment bytes
    size_t eocd = size;
    const size_t lowest = size > 22 + 65535 ? size - (22 + 65535) : 0;
    for (size_t pos = size - 22 + 1; pos-- > lowest;)
        if (rd32 (p + pos) == kSigEndOfCentralDir)
        {
            eocd = pos;
            break;
        }
    if (eocd == size)
        return ZipStatus::notZip;

    if (eocd >= 20 && rd32 (p + eocd - 20) == kSigZip64Locator)
        return ZipStatus::unsupported;
    const unsigned diskNumber = rd16 (p + eocd + 4), cdDisk = rd16 (p + eocd + 6);
    const unsigned entriesHere = rd16 (p + eocd + 8), total = rd16 (p + eocd + 10);
    const uint32_t cdSize = rd32 (p + eocd + 12), cdOffset = rd32 (p + eocd + 16);
    if (diskNumber != 0 || cdDisk != 0 || entriesHere != total)
        return ZipStatus::unsupported;
    if (total == 0xffffu || cdSize == 0xffffffffu || cdOffset == 0xffffffffu)
        return ZipStatus::unsupported;
    if (total > lim.maxEntries)
        return ZipStatus::limitExceeded;
    if (static_cast<size_t> (cdOffset) > eocd || static_cast<size_t> (cdSize) > eocd - cdOffset)
        return ZipStatus::truncated;

    try
    {
        list.reserve (total);
        size_t pos = cdOffset;
        const size_t cdEnd = static_cast<size_t> (cdOffset) + cdSize;
        size_t declaredTotal = 0;
        for (unsigned i = 0; i < total; ++i)
        {
            if (cdEnd - pos < 46 || rd32 (p + pos) != kSigCentralEntry)
                return ZipStatus::corrupt;
            const unsigned flags = rd16 (p + pos + 8), method = rd16 (p + pos + 10);
            ZipEntry e;
            e.method = static_cast<uint16_t> (method);
            e.crc32 = rd32 (p + pos + 16);
            e.compressedSize = rd32 (p + pos + 20);
            e.uncompressedSize = rd32 (p + pos + 24);
            const unsigned nameLen = rd16 (p + pos + 28), extraLen = rd16 (p + pos + 30), commentLen = rd16 (p + pos + 32);
            e.localHeaderOffset = rd32 (p + pos + 42);
            if (nameLen > lim.maxNameBytes)
                return ZipStatus::limitExceeded;
            if (cdEnd - pos - 46 < static_cast<size_t> (nameLen) + extraLen + commentLen)
                return ZipStatus::corrupt;
            if (e.compressedSize == 0xffffffffu || e.uncompressedSize == 0xffffffffu || e.localHeaderOffset == 0xffffffffu)
                return ZipStatus::unsupported; // zip64 values
            e.name.assign (reinterpret_cast<const char*> (p + pos + 46), nameLen);
            e.directory = ! e.name.empty() && e.name.back() == '/';
            e.safePath = pathIsSafe (e.directory ? e.name.substr (0, e.name.size() - 1) : e.name);
            e.encrypted = (flags & 1u) != 0;
            declaredTotal += e.uncompressedSize;
            if (declaredTotal > lim.maxTotalBytes)
                return ZipStatus::limitExceeded;
            list.push_back (std::move (e));
            pos += 46 + static_cast<size_t> (nameLen) + extraLen + commentLen;
        }
    }
    catch (const std::bad_alloc&)
    {
        list.clear();
        return ZipStatus::limitExceeded;
    }

    base = p;
    length = size;
    return ZipStatus::ok;
}

int ZipArchive::find (const std::string& name) const noexcept
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name)
            return static_cast<int> (i);
    return -1;
}

ZipStatus ZipArchive::extract (size_t index, std::vector<uint8_t>& out) const noexcept
{
    out.clear();
    if (base == nullptr || index >= list.size())
        return ZipStatus::badIndex;
    const ZipEntry& e = list[index];
    if (e.encrypted)
        return ZipStatus::unsupported;
    if (e.method != 0 && e.method != 8)
        return ZipStatus::unsupported;
    if (e.uncompressedSize > lim.maxEntryBytes)
        return ZipStatus::limitExceeded;

    const size_t off = e.localHeaderOffset;
    if (off > length || length - off < 30)
        return ZipStatus::truncated;
    if (rd32 (base + off) != kSigLocalHeader)
        return ZipStatus::corrupt;
    const size_t dataStart = off + 30 + rd16 (base + off + 26) + rd16 (base + off + 28);
    if (dataStart > length || length - dataStart < e.compressedSize)
        return ZipStatus::truncated;
    const uint8_t* src = base + dataStart;

    try
    {
        out.resize (e.uncompressedSize);
    }
    catch (const std::bad_alloc&)
    {
        out.clear();
        return ZipStatus::limitExceeded;
    }

    ZipStatus s = ZipStatus::ok;
    if (e.method == 0)
    {
        if (e.compressedSize != e.uncompressedSize)
            s = ZipStatus::corrupt;
        else if (e.uncompressedSize > 0)
            std::memcpy (out.data(), src, e.uncompressedSize);
    }
    else
    {
        s = inflateRaw (src, e.compressedSize, out.data(), out.size());
    }
    if (s == ZipStatus::ok && crc32Of (out.data(), out.size()) != e.crc32)
        s = ZipStatus::crcMismatch;
    if (s != ZipStatus::ok)
        out.clear();
    return s;
}
} // namespace synthyboy
