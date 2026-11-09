// SynthyBoy - core/serum-import: a self contained Zstandard decoder (RFC 8878) for the body of a .SerumPreset.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Written from the format specification (RFC 8878): frames, raw / RLE / compressed blocks, Huffman and FSE entropy coding, the repeat offset rules,
// the xxHash64 content checksum. No dictionary support (a dictionary id other than 0 is malformed). Every length is checked before it is used.
#include <algorithm>
#include <cstring>

#include "serum_internal.h"

namespace synthyboy::serumimport
{
namespace
{
constexpr uint32_t kMagic = 0xFD2FB528u;
constexpr size_t kMaxBlock = 128u * 1024u;

// ----- xxHash64 --------------------------------------------------------------------------------------------------------------------------
constexpr uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull, P3 = 1609587929392839161ull, P4 = 9650029242287828579ull,
                   P5 = 2870177450012600261ull;
inline uint64_t rotl (uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t rd64 (const uint8_t* p) { uint64_t v; std::memcpy (&v, p, 8); return v; } // little endian hosts only (checked by the caller)
inline uint32_t rd32 (const uint8_t* p) { uint32_t v; std::memcpy (&v, p, 4); return v; }
inline uint64_t round1 (uint64_t acc, uint64_t in) { return rotl (acc + in * P2, 31) * P1; }
inline uint64_t merge (uint64_t acc, uint64_t v) { return (acc ^ round1 (0, v)) * P1 + P4; }

uint64_t xxh64 (const uint8_t* p, size_t n)
{
    const uint8_t* end = p + n;
    uint64_t h;
    if (n >= 32)
    {
        uint64_t v1 = P1 + P2, v2 = P2, v3 = 0, v4 = 0 - P1;
        while (p + 32 <= end)
        {
            v1 = round1 (v1, rd64 (p));
            v2 = round1 (v2, rd64 (p + 8));
            v3 = round1 (v3, rd64 (p + 16));
            v4 = round1 (v4, rd64 (p + 24));
            p += 32;
        }
        h = rotl (v1, 1) + rotl (v2, 7) + rotl (v3, 12) + rotl (v4, 18);
        h = merge (h, v1); h = merge (h, v2); h = merge (h, v3); h = merge (h, v4);
    }
    else
        h = P5;
    h += n;
    while (p + 8 <= end) { h ^= round1 (0, rd64 (p)); h = rotl (h, 27) * P1 + P4; p += 8; }
    if (p + 4 <= end) { h ^= static_cast<uint64_t> (rd32 (p)) * P1; h = rotl (h, 23) * P2 + P3; p += 4; }
    while (p < end) { h ^= static_cast<uint64_t> (*p) * P5; h = rotl (h, 11) * P1; ++p; }
    h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
    return h;
}

inline int highBit (uint32_t v) { int r = 0; while (v >>= 1) ++r; return r; }

// ----- bit readers -----------------------------------------------------------------------------------------------------------------------
/** Forward reader (little endian bit order): the table descriptions. */
struct FwdBits
{
    const uint8_t* p;
    size_t nBits;
    size_t pos = 0;
    bool over = false;
    FwdBits (const uint8_t* d, size_t n) : p (d), nBits (n * 8) {}
    uint32_t peek (int n) const
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
        {
            const size_t b = pos + static_cast<size_t> (i);
            if (b < nBits && ((p[b >> 3] >> (b & 7)) & 1u)) v |= 1u << i;
        }
        return v;
    }
    uint32_t read (int n) { const uint32_t v = peek (n); pos += static_cast<size_t> (n); if (pos > nBits) over = true; return v; }
    size_t bytesUsed() const { return (pos + 7) >> 3; }
};

/** Backward reader: the stream ends with a byte whose highest set bit is a sentinel; bits are read from the sentinel downwards. */
struct BackBits
{
    const uint8_t* p = nullptr;
    int64_t pos = 0; // number of unread bits (index of the next bit + 1)
    bool init (const uint8_t* d, size_t n)
    {
        p = d;
        if (n == 0 || d[n - 1] == 0) return false;
        pos = static_cast<int64_t> (n) * 8 - (8 - highBit (d[n - 1])); // the sentinel bit and the padding above it are not data
        return true;
    }
    uint64_t peek (int n) const // the n bits ending at pos (zero where the stream has no more bits)
    {
        uint64_t v = 0;
        const int64_t start = pos - n;
        for (int i = 0; i < n; ++i)
        {
            const int64_t b = start + i;
            if (b >= 0 && ((p[b >> 3] >> (b & 7)) & 1u)) v |= 1ull << i;
        }
        return v;
    }
    uint64_t read (int n) { const uint64_t v = peek (n); pos -= n; return v; }
    bool overflow() const { return pos < 0; }
};

// ----- FSE -------------------------------------------------------------------------------------------------------------------------------
struct FseEntry { uint16_t base; uint8_t sym; uint8_t bits; };
struct FseTable
{
    int log = 0;
    std::vector<FseEntry> e;
    bool valid() const { return log > 0 && ! e.empty(); }
};

bool buildFse (const int16_t* norm, int numSyms, int log, FseTable& t)
{
    if (log < 1 || log > 15) return false;
    const int size = 1 << log;
    int sum = 0;
    for (int s = 0; s < numSyms; ++s) sum += norm[s] < 0 ? 1 : norm[s];
    if (sum != size) return false;
    t.log = log;
    t.e.assign (static_cast<size_t> (size), FseEntry {});
    std::vector<uint16_t> next (static_cast<size_t> (numSyms));
    int high = size - 1;
    for (int s = 0; s < numSyms; ++s)
        if (norm[s] < 0) { t.e[static_cast<size_t> (high--)].sym = static_cast<uint8_t> (s); next[static_cast<size_t> (s)] = 1; }
        else next[static_cast<size_t> (s)] = static_cast<uint16_t> (norm[s]);
    const int step = (size >> 1) + (size >> 3) + 3, mask = size - 1;
    int pos = 0;
    for (int s = 0; s < numSyms; ++s)
        for (int i = 0; i < norm[s]; ++i)
        {
            t.e[static_cast<size_t> (pos)].sym = static_cast<uint8_t> (s);
            do pos = (pos + step) & mask; while (pos > high);
        }
    if (pos != 0) return false;
    for (int i = 0; i < size; ++i)
    {
        FseEntry& en = t.e[static_cast<size_t> (i)];
        const uint32_t nx = next[en.sym]++;
        const int nb = log - highBit (nx);
        en.bits = static_cast<uint8_t> (nb);
        en.base = static_cast<uint16_t> ((nx << nb) - static_cast<uint32_t> (size));
    }
    return true;
}

/** Reads a table description from the forward stream; returns the bytes used or 0 on error. */
size_t readFseTable (const uint8_t* d, size_t n, int maxLog, int maxSym, FseTable& t, int16_t* norm /*[maxSym+1]*/, int& numSyms)
{
    FwdBits br (d, n);
    const int log = 5 + static_cast<int>(br.read (4));
    if (log > maxLog) return 0;
    int remaining = (1 << log) + 1, threshold = 1 << log, nbBits = log + 1, sym = 0;
    bool prev0 = false;
    for (int i = 0; i <= maxSym; ++i) norm[i] = 0;
    while (remaining > 1 && sym <= maxSym)
    {
        if (prev0)
        {
            int zeros = 0;
            for (;;)
            {
                const int r = static_cast<int> (br.read (2));
                zeros += r;
                if (r != 3) break;
                if (br.over) return 0;
            }
            if (sym + zeros > maxSym + 1) return 0;
            sym += zeros;
            prev0 = false;
            if (br.over) return 0;
            continue;
        }
        const int max = (2 * threshold - 1) - remaining;
        int count;
        const uint32_t low = br.peek (nbBits - 1) & static_cast<uint32_t> (threshold - 1);
        if (static_cast<int> (low) < max) { count = static_cast<int> (low); br.read (nbBits - 1); }
        else
        {
            count = static_cast<int> (br.peek (nbBits) & static_cast<uint32_t> (2 * threshold - 1));
            if (count >= threshold) count -= max;
            br.read (nbBits);
        }
        --count;
        remaining -= count < 0 ? -count : count;
        norm[sym++] = static_cast<int16_t> (count);
        prev0 = count == 0;
        while (remaining < threshold && threshold > 1) { --nbBits; threshold >>= 1; }
        if (br.over) return 0;
    }
    if (remaining != 1) return 0;
    numSyms = sym;
    if (! buildFse (norm, numSyms, log, t)) return 0;
    return br.bytesUsed();
}

// ----- Huffman ---------------------------------------------------------------------------------------------------------------------------
struct HufTable
{
    int maxBits = 0;
    std::vector<uint8_t> sym, bits; // 1 << maxBits entries
    bool valid() const { return maxBits > 0; }
};

bool buildHuf (const uint8_t* weights, int numWeights /* without the implied last one */, HufTable& t)
{
    uint32_t sum = 0;
    for (int i = 0; i < numWeights; ++i)
    {
        if (weights[i] > 11) return false;
        if (weights[i]) sum += 1u << (weights[i] - 1);
    }
    if (sum == 0) return false;
    const int maxBits = highBit (sum) + 1;
    if (maxBits > 11) return false;
    const uint32_t rest = (1u << maxBits) - sum;
    if (rest == 0 || (rest & (rest - 1)) != 0) return false;
    uint8_t w[256];
    if (numWeights > 255) return false;
    for (int i = 0; i < numWeights; ++i) w[i] = weights[i];
    w[numWeights] = static_cast<uint8_t> (highBit (rest) + 1);
    const int n = numWeights + 1;
    t.maxBits = maxBits;
    t.sym.assign (size_t { 1 } << maxBits, 0);
    t.bits.assign (size_t { 1 } << maxBits, 0);
    size_t pos = 0;
    for (int wt = 1; wt <= maxBits; ++wt)
        for (int s = 0; s < n; ++s)
            if (w[s] == wt)
            {
                const size_t cnt = size_t { 1 } << (wt - 1);
                if (pos + cnt > t.sym.size()) return false;
                for (size_t k = 0; k < cnt; ++k) { t.sym[pos + k] = static_cast<uint8_t> (s); t.bits[pos + k] = static_cast<uint8_t> (maxBits + 1 - wt); }
                pos += cnt;
            }
    return pos == t.sym.size();
}

/** Reads the tree description at d (n bytes available); returns bytes used, 0 on error. */
size_t readHufTree (const uint8_t* d, size_t n, HufTable& t)
{
    if (n < 1) return 0;
    const int hb = d[0];
    uint8_t w[256];
    int nw = 0;
    size_t used;
    if (hb >= 128)
    {
        nw = hb - 127;
        used = 1 + static_cast<size_t> ((nw + 1) / 2);
        if (used > n || nw > 255) return 0;
        for (int i = 0; i < nw; ++i) w[i] = (i & 1) ? (d[1 + i / 2] & 15) : (d[1 + i / 2] >> 4);
    }
    else
    {
        used = 1 + static_cast<size_t> (hb);
        if (hb == 0 || used > n) return 0;
        FseTable ft;
        int16_t norm[64];
        int ns = 0;
        const size_t tb = readFseTable (d + 1, static_cast<size_t> (hb), 6, 12, ft, norm, ns);
        if (tb == 0 || tb >= static_cast<size_t> (hb)) return 0;
        BackBits bb;
        if (! bb.init (d + 1 + tb, static_cast<size_t> (hb) - tb)) return 0;
        uint32_t s1 = static_cast<uint32_t> (bb.read (ft.log)), s2 = static_cast<uint32_t> (bb.read (ft.log));
        for (;;)
        {
            if (nw >= 255) return 0;
            const FseEntry& a = ft.e[s1];
            w[nw++] = a.sym;
            s1 = a.base + static_cast<uint32_t> (bb.read (a.bits));
            if (bb.overflow()) { if (nw >= 255) return 0; w[nw++] = ft.e[s2].sym; break; }
            if (nw >= 255) return 0;
            const FseEntry& b = ft.e[s2];
            w[nw++] = b.sym;
            s2 = b.base + static_cast<uint32_t> (bb.read (b.bits));
            if (bb.overflow()) { if (nw >= 255) return 0; w[nw++] = ft.e[s1].sym; break; }
        }
    }
    if (! buildHuf (w, nw, t)) return 0;
    return used;
}

bool decodeHufStream (const HufTable& t, const uint8_t* d, size_t n, uint8_t* out, size_t count)
{
    BackBits bb;
    if (! bb.init (d, n)) return false;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t idx = static_cast<size_t> (bb.peek (t.maxBits));
        out[i] = t.sym[idx];
        bb.pos -= t.bits[idx];
        if (bb.pos < 0) return false;
    }
    return bb.pos == 0;
}

// ----- sequences -------------------------------------------------------------------------------------------------------------------------
const int16_t kLLNorm[36] = { 4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1 };
const int16_t kMLNorm[53] = { 1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1 };
const int16_t kOFNorm[29] = { 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1 };
const uint32_t kLLBase[36] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
const uint8_t kLLBits[36] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
const uint32_t kMLBase[53] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
                               35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539 };
const uint8_t kMLBits[53] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                              1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

struct State
{
    FseTable ll, of, ml;
    HufTable huf;
    uint32_t rep[3] = { 1, 4, 8 };
};

const FseTable& predefined (int which)
{
    static const FseTable tabs[3] = {
        [] { FseTable t; buildFse (kLLNorm, 36, 6, t); return t; }(),
        [] { FseTable t; buildFse (kOFNorm, 29, 5, t); return t; }(),
        [] { FseTable t; buildFse (kMLNorm, 53, 6, t); return t; }() };
    return tabs[which];
}

/** Selects the table of one symbol type. mode: 0 predefined, 1 RLE, 2 compressed, 3 repeat. Returns false on error; `p` advances. */
bool selectTable (int mode, int which, int maxLog, int maxSym, const uint8_t*& p, const uint8_t* end, FseTable& cur)
{
    if (mode == 0) { cur = predefined (which); return cur.valid(); }
    if (mode == 1)
    {
        if (p >= end) return false;
        const uint8_t s = *p++;
        if (s > maxSym) return false;
        cur.log = 0; // special: a table that always yields `s` with no bits
        cur.e.assign (1, FseEntry { 0, s, 0 });
        return true;
    }
    if (mode == 3) return ! cur.e.empty();
    int16_t norm[64];
    int ns = 0;
    FseTable t;
    const size_t used = readFseTable (p, static_cast<size_t> (end - p), maxLog, maxSym, t, norm, ns);
    if (used == 0) return false;
    p += used;
    cur = std::move (t);
    return true;
}

ZstdStatus decodeBlock (const uint8_t* b, size_t n, std::vector<uint8_t>& out, size_t maxOut, State& st, std::vector<uint8_t>& lit)
{
    const uint8_t* p = b;
    const uint8_t* end = b + n;
    if (n < 1) return ZstdStatus::malformed;
    // literals section
    const int ltype = p[0] & 3, sf = (p[0] >> 2) & 3;
    size_t regen = 0, comp = 0;
    int streams = 1;
    lit.clear();
    if (ltype <= 1)
    {
        if (sf == 0 || sf == 2) { regen = p[0] >> 3; p += 1; }
        else if (sf == 1) { if (n < 2) return ZstdStatus::malformed; regen = (p[0] >> 4) + (static_cast<size_t> (p[1]) << 4); p += 2; }
        else { if (n < 3) return ZstdStatus::malformed; regen = (p[0] >> 4) + (static_cast<size_t> (p[1]) << 4) + (static_cast<size_t> (p[2]) << 12); p += 3; }
        if (regen > kMaxBlock) return ZstdStatus::malformed;
        if (ltype == 0)
        {
            if (static_cast<size_t> (end - p) < regen) return ZstdStatus::malformed;
            lit.assign (p, p + regen);
            p += regen;
        }
        else
        {
            if (p >= end) return ZstdStatus::malformed;
            lit.assign (regen, *p++);
        }
    }
    else
    {
        size_t hdr;
        int bitsPer;
        if (sf == 0) { hdr = 3; bitsPer = 10; streams = 1; }
        else if (sf == 1) { hdr = 3; bitsPer = 10; streams = 4; }
        else if (sf == 2) { hdr = 4; bitsPer = 14; streams = 4; }
        else { hdr = 5; bitsPer = 18; streams = 4; }
        if (n < hdr) return ZstdStatus::malformed;
        uint64_t v = 0;
        for (size_t i = 0; i < hdr; ++i) v |= static_cast<uint64_t> (p[i]) << (8 * i);
        v >>= 4;
        regen = static_cast<size_t> (v & ((1ull << bitsPer) - 1));
        comp = static_cast<size_t> ((v >> bitsPer) & ((1ull << bitsPer) - 1));
        p += hdr;
        if (regen > kMaxBlock || static_cast<size_t> (end - p) < comp) return ZstdStatus::malformed;
        const uint8_t* sec = p;
        size_t secLen = comp;
        if (ltype == 2)
        {
            const size_t used = readHufTree (sec, secLen, st.huf);
            if (used == 0) return ZstdStatus::malformed;
            sec += used;
            secLen -= used;
        }
        else if (! st.huf.valid())
            return ZstdStatus::malformed;
        lit.assign (regen, 0);
        if (streams == 1)
        {
            if (! decodeHufStream (st.huf, sec, secLen, lit.data(), regen)) return ZstdStatus::malformed;
        }
        else
        {
            if (secLen < 6) return ZstdStatus::malformed;
            const size_t s1 = sec[0] | (sec[1] << 8), s2 = sec[2] | (sec[3] << 8), s3 = sec[4] | (sec[5] << 8);
            if (6 + s1 + s2 + s3 >= secLen) return ZstdStatus::malformed;
            const size_t s4 = secLen - 6 - s1 - s2 - s3;
            const size_t per = (regen + 3) / 4;
            if (per * 3 > regen) return ZstdStatus::malformed;
            const uint8_t* sp = sec + 6;
            const size_t sz[4] = { s1, s2, s3, s4 };
            const size_t cn[4] = { per, per, per, regen - per * 3 };
            size_t off = 0;
            for (int k = 0; k < 4; ++k)
            {
                if (! decodeHufStream (st.huf, sp, sz[k], lit.data() + off, cn[k])) return ZstdStatus::malformed;
                sp += sz[k];
                off += cn[k];
            }
        }
        p += comp;
    }
    // sequences section
    if (p >= end) return ZstdStatus::malformed;
    size_t numSeq = *p++;
    if (numSeq >= 128)
    {
        if (numSeq == 255) { if (end - p < 2) return ZstdStatus::malformed; numSeq = static_cast<size_t> (p[0]) + (static_cast<size_t> (p[1]) << 8) + 0x7F00; p += 2; }
        else { if (p >= end) return ZstdStatus::malformed; numSeq = ((numSeq - 128) << 8) + *p++; }
    }
    size_t litPos = 0;
    if (numSeq == 0)
    {
        if (out.size() + lit.size() > maxOut) return ZstdStatus::tooLarge;
        out.insert (out.end(), lit.begin(), lit.end());
        return ZstdStatus::ok;
    }
    if (p >= end) return ZstdStatus::malformed;
    const int modes = *p++;
    if ((modes & 3) != 0) return ZstdStatus::malformed;
    if (! selectTable ((modes >> 6) & 3, 0, 9, 35, p, end, st.ll)) return ZstdStatus::malformed;
    if (! selectTable ((modes >> 4) & 3, 1, 8, 31, p, end, st.of)) return ZstdStatus::malformed;
    if (! selectTable ((modes >> 2) & 3, 2, 9, 52, p, end, st.ml)) return ZstdStatus::malformed;
    BackBits bb;
    if (! bb.init (p, static_cast<size_t> (end - p))) return ZstdStatus::malformed;
    uint32_t sLL = static_cast<uint32_t> (bb.read (st.ll.log)), sOF = static_cast<uint32_t> (bb.read (st.of.log)), sML = static_cast<uint32_t> (bb.read (st.ml.log));
    for (size_t q = 0; q < numSeq; ++q)
    {
        const FseEntry& eLL = st.ll.e[sLL];
        const FseEntry& eOF = st.of.e[sOF];
        const FseEntry& eML = st.ml.e[sML];
        const int ofCode = eOF.sym, mlCode = eML.sym, llCode = eLL.sym;
        if (ofCode > 31 || mlCode > 52 || llCode > 35) return ZstdStatus::malformed;
        const uint64_t ofVal = (1ull << ofCode) + bb.read (ofCode);
        const size_t ml = kMLBase[mlCode] + static_cast<size_t> (bb.read (kMLBits[mlCode]));
        const size_t ll = kLLBase[llCode] + static_cast<size_t> (bb.read (kLLBits[llCode]));
        if (q + 1 < numSeq)
        {
            sLL = eLL.base + static_cast<uint32_t> (bb.read (eLL.bits));
            sML = eML.base + static_cast<uint32_t> (bb.read (eML.bits));
            sOF = eOF.base + static_cast<uint32_t> (bb.read (eOF.bits));
        }
        if (bb.overflow()) return ZstdStatus::malformed;
        uint32_t offset;
        if (ofVal > 3) { offset = static_cast<uint32_t> (ofVal - 3); st.rep[2] = st.rep[1]; st.rep[1] = st.rep[0]; st.rep[0] = offset; }
        else
        {
            const int idx = static_cast<int> (ofVal) - 1 + (ll == 0 ? 1 : 0);
            if (idx == 0) offset = st.rep[0];
            else
            {
                offset = idx == 3 ? st.rep[0] - 1 : st.rep[idx];
                if (offset == 0) return ZstdStatus::malformed;
                if (idx == 1) { st.rep[1] = st.rep[0]; st.rep[0] = offset; }
                else { st.rep[2] = st.rep[1]; st.rep[1] = st.rep[0]; st.rep[0] = offset; }
            }
        }
        if (ll > lit.size() - litPos) return ZstdStatus::malformed;
        if (out.size() + ll + ml > maxOut) return ZstdStatus::tooLarge;
        out.insert (out.end(), lit.begin() + static_cast<std::ptrdiff_t> (litPos), lit.begin() + static_cast<std::ptrdiff_t> (litPos + ll));
        litPos += ll;
        if (offset == 0 || offset > out.size()) return ZstdStatus::malformed;
        size_t from = out.size() - offset;
        const size_t base = out.size();
        out.resize (base + ml);
        for (size_t k = 0; k < ml; ++k) out[base + k] = out[from + k];
    }
    if (bb.pos != 0) return ZstdStatus::malformed;
    const size_t restLit = lit.size() - litPos;
    if (out.size() + restLit > maxOut) return ZstdStatus::tooLarge;
    out.insert (out.end(), lit.begin() + static_cast<std::ptrdiff_t> (litPos), lit.end());
    return ZstdStatus::ok;
}
} // namespace

ZstdStatus zstdDecompress (const uint8_t* src, size_t size, size_t maxOut, std::vector<uint8_t>& out) noexcept
{
    try
    {
        const uint16_t probe = 1;
        if (*reinterpret_cast<const uint8_t*> (&probe) != 1) return ZstdStatus::malformed; // the checksum code reads little endian words
        size_t pos = 0;
        bool any = false;
        std::vector<uint8_t> lit;
        while (pos < size)
        {
            if (size - pos < 4) return ZstdStatus::truncated;
            const uint32_t magic = rd32 (src + pos);
            if ((magic & 0xFFFFFFF0u) == 0x184D2A50u)
            {
                if (size - pos < 8) return ZstdStatus::truncated;
                const size_t len = rd32 (src + pos + 4);
                if (size - pos - 8 < len) return ZstdStatus::truncated;
                pos += 8 + len;
                continue;
            }
            if (magic != kMagic) return ZstdStatus::malformed;
            pos += 4;
            if (pos >= size) return ZstdStatus::truncated;
            const int fhd = src[pos++];
            const int fcsFlag = fhd >> 6, single = (fhd >> 5) & 1, checksum = (fhd >> 2) & 1, dictFlag = fhd & 3;
            if (fhd & 8) return ZstdStatus::malformed;
            if (! single) { if (pos >= size) return ZstdStatus::truncated; ++pos; } // window descriptor
            const size_t dictBytes = dictFlag == 0 ? 0 : (dictFlag == 1 ? 1 : (dictFlag == 2 ? 2 : 4));
            if (dictBytes) return ZstdStatus::malformed; // dictionaries are not supported
            const size_t fcsBytes = fcsFlag == 0 ? (single ? 1 : 0) : (fcsFlag == 1 ? 2 : (fcsFlag == 2 ? 4 : 8));
            if (size - pos < fcsBytes) return ZstdStatus::truncated;
            uint64_t fcs = 0;
            for (size_t i = 0; i < fcsBytes; ++i) fcs |= static_cast<uint64_t> (src[pos + i]) << (8 * i);
            if (fcsFlag == 1) fcs += 256;
            pos += fcsBytes;
            if (fcsBytes && fcs > maxOut - std::min (maxOut, out.size())) return ZstdStatus::tooLarge;
            const size_t frameStart = out.size();
            State st;
            for (;;)
            {
                if (size - pos < 3) return ZstdStatus::truncated;
                const uint32_t bh = src[pos] | (src[pos + 1] << 8) | (src[pos + 2] << 16);
                pos += 3;
                const bool last = bh & 1;
                const int type = (bh >> 1) & 3;
                const size_t bsz = bh >> 3;
                if (type == 3 || bsz > kMaxBlock) return ZstdStatus::malformed;
                if (type == 0)
                {
                    if (size - pos < bsz) return ZstdStatus::truncated;
                    if (out.size() + bsz > maxOut) return ZstdStatus::tooLarge;
                    out.insert (out.end(), src + pos, src + pos + bsz);
                    pos += bsz;
                }
                else if (type == 1)
                {
                    if (pos >= size) return ZstdStatus::truncated;
                    if (out.size() + bsz > maxOut) return ZstdStatus::tooLarge;
                    out.insert (out.end(), bsz, src[pos]);
                    ++pos;
                }
                else
                {
                    if (size - pos < bsz) return ZstdStatus::truncated;
                    const ZstdStatus s = decodeBlock (src + pos, bsz, out, maxOut, st, lit);
                    if (s != ZstdStatus::ok) return s;
                    pos += bsz;
                }
                if (last) break;
            }
            if (fcsBytes && out.size() - frameStart != fcs) return ZstdStatus::malformed;
            if (checksum)
            {
                if (size - pos < 4) return ZstdStatus::truncated;
                const uint32_t want = rd32 (src + pos);
                pos += 4;
                const uint32_t got = static_cast<uint32_t> (xxh64 (out.data() + frameStart, out.size() - frameStart));
                if (want != got) return ZstdStatus::malformed;
            }
            any = true;
        }
        return any ? ZstdStatus::ok : ZstdStatus::truncated;
    }
    catch (...)
    {
        return ZstdStatus::tooLarge;
    }
}
} // namespace synthyboy::serumimport
