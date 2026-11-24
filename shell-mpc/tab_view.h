// SynthyBoy - MPC shell: the chunk-only view state of the merged Env / Filter / LFO tabs ("mpcTabView", docs/MPC.md "Tab view state"). Two small numbers, no host parameter and no engine parameter:
//   rnd    0 .. 4   the random LFO being edited on the LFO tab (0 = a standard LFO; the standard one is the Play LFO selector)
//   filter 0 .. 11  the state of the Filter tab:
//                   legacy 0..3: 0 (F1, NOTE, Split), 1 (F2, NOTE, Split), 2 (F1, NOTE, ExpandedCurve), 3 (F1, VELO, ExpandedCurve)
//                   split/expanded 4..11: remaining combinations of (filter 0..1, scalar 0..1, expand 0..2)
// The chunk member is `,"mpcTabView":{"rnd":2,"filter":1}` before the last closing brace, after the Play selectors (and before the limiter and pad pressure members, which are read from the tail). A chunk without it
// (an older one) gives the defaults (0, 0) at a project reload. Pure functions: no OS headers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace synthyboy_mpc { namespace tabview {

constexpr int kFields = 2;                 // rnd, filter
constexpr int kMax[kFields] = { 4, 11 };   // the largest value of each field

enum FilterExpand { kFilterSplit = 0, kFilterExpandedFilter = 1, kFilterExpandedCurve = 2 };

/** Encodes independent Filter selection (0 F1, 1 F2), Scalar selection (0 NOTE, 1 VELO),
    and layout mode (0 Split, 1 ExpandedFilter, 2 ExpandedCurve) while keeping legacy 0..3 intact. */
inline int encodeFilterSplit (int filter, int scalar, int expand) noexcept
{
    filter = std::clamp (filter, 0, 1);
    scalar = std::clamp (scalar, 0, 1);
    expand = std::clamp (expand, 0, 2);
    if (expand == kFilterSplit && scalar == 0)
        return filter;                       // 0: (F1, NOTE, Split), 1: (F2, NOTE, Split)
    if (expand == kFilterExpandedCurve && filter == 0)
        return 2 + scalar;                   // 2: (F1, NOTE, ExpandedCurve), 3: (F1, VELO, ExpandedCurve)
    if (expand == kFilterSplit)
        return 4 + filter;                   // 4: (F1, VELO, Split), 5: (F2, VELO, Split)
    if (expand == kFilterExpandedFilter)
        return 6 + filter + 2 * scalar;      // 6..9: ExpandedFilter
    return 10 + scalar;                      // 10: (F2, NOTE, ExpandedCurve), 11: (F2, VELO, ExpandedCurve)
}

inline void decodeFilterSplit (int raw, int& filter, int& scalar, int& expand) noexcept
{
    switch (std::clamp (raw, 0, kMax[1]))
    {
        case 0:  filter = 0; scalar = 0; expand = kFilterSplit; break;
        case 1:  filter = 1; scalar = 0; expand = kFilterSplit; break;
        case 2:  filter = 0; scalar = 0; expand = kFilterExpandedCurve; break;
        case 3:  filter = 0; scalar = 1; expand = kFilterExpandedCurve; break;
        case 4:  filter = 0; scalar = 1; expand = kFilterSplit; break;
        case 5:  filter = 1; scalar = 1; expand = kFilterSplit; break;
        case 6:  filter = 0; scalar = 0; expand = kFilterExpandedFilter; break;
        case 7:  filter = 1; scalar = 0; expand = kFilterExpandedFilter; break;
        case 8:  filter = 0; scalar = 1; expand = kFilterExpandedFilter; break;
        case 9:  filter = 1; scalar = 1; expand = kFilterExpandedFilter; break;
        case 10: filter = 1; scalar = 0; expand = kFilterExpandedCurve; break;
        default: filter = 1; scalar = 1; expand = kFilterExpandedCurve; break;
    }
}

/** The chunk with the member before the last closing brace. Values outside the range are clamped. */
inline std::string chunkWithTabView (std::string chunk, const int v[kFields])
{
    const size_t close = chunk.rfind ('}');
    if (close == std::string::npos)
        return chunk;
    char buf[64];
    std::snprintf (buf, sizeof buf, ",\"mpcTabView\":{\"rnd\":%d,\"filter\":%d}", std::clamp (v[0], 0, kMax[0]), std::clamp (v[1], 0, kMax[1]));
    chunk.insert (close, buf);
    return chunk;
}

/** Reads the member from the last 400 bytes of a chunk (the tail holds the Play selectors, the limiter and the pad pressure members after it at most). true: it is there and well formed (v is set, values in range);
    false leaves `v` as it is. Never reads outside `size`. */
inline bool tabViewFromChunk (const void* data, size_t size, int v[kFields]) noexcept
{
    if (data == nullptr || size < 40)
        return false;
    const size_t tailLen = size < 400 ? size : 400;
    const std::string tail (static_cast<const char*> (data) + (size - tailLen), tailLen);
    static const char kKey[] = "\"mpcTabView\":{\"rnd\":";
    static const char kFilter[] = ",\"filter\":";
    const size_t a = tail.rfind (kKey);
    if (a == std::string::npos)
        return false;
    size_t p = a + sizeof (kKey) - 1;
    int x[kFields] = {};
    for (int f = 0; f < kFields; ++f)
    {
        size_t q = p;
        int n = 0;
        while (q < tail.size() && tail[q] >= '0' && tail[q] <= '9' && q - p < 3)
            n = n * 10 + (tail[q++] - '0');
        if (q == p || n > kMax[f])
            return false;
        x[f] = n;
        p = q;
        if (f == 0)
        {
            if (tail.compare (p, sizeof (kFilter) - 1, kFilter) != 0)
                return false;
            p += sizeof (kFilter) - 1;
        }
    }
    if (p >= tail.size() || tail[p] != '}')
        return false;
    v[0] = x[0];
    v[1] = x[1];
    return true;
}

}} // namespace synthyboy_mpc::tabview
