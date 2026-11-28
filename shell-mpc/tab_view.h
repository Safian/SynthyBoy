// SynthyBoy - MPC shell: the chunk-only view state of the merged LFO tab ("mpcTabView", docs/MPC.md "Tab view state"). One small number, no host parameter and no engine parameter:
//   rnd    0 .. 4   the random LFO being edited on the LFO tab (0 = a standard LFO; the standard one is the Play LFO selector)
// The chunk member is `,"mpcTabView":{"rnd":2}` before the last closing brace, after the Play selectors (and before the limiter and pad pressure members, which are read from the tail). A chunk without it
// (an older one) gives the default (0) at a project reload. An older chunk's member `{"rnd":2,"filter":1}` is still read (the `filter` number is ignored: the Filter tab state is the sub-tab's). Pure functions: no OS headers.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace synthyboy_mpc { namespace tabview {

constexpr int kFields = 1;                 // rnd
constexpr int kMax[kFields] = { 4 };       // the largest value of each field

/** The chunk with the member before the last closing brace. Values outside the range are clamped. */
inline std::string chunkWithTabView (std::string chunk, const int v[kFields])
{
    const size_t close = chunk.rfind ('}');
    if (close == std::string::npos)
        return chunk;
    char buf[64];
    std::snprintf (buf, sizeof buf, ",\"mpcTabView\":{\"rnd\":%d}", std::clamp (v[0], 0, kMax[0]));
    chunk.insert (close, buf);
    return chunk;
}

/** Reads the member from the last 400 bytes of a chunk (the tail holds the Play selectors, the limiter and the pad pressure members after it at most). true: it is there and well formed (v is set, values in range);
    false leaves `v` as it is. The member of an older chunk (`{"rnd":N,"filter":M}`) is accepted, `filter` is ignored. Never reads outside `size`. */
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
    size_t q = p;
    int n = 0;
    while (q < tail.size() && tail[q] >= '0' && tail[q] <= '9' && q - p < 3)
        n = n * 10 + (tail[q++] - '0');
    if (q == p || n > kMax[0])
        return false;
    p = q;
    if (tail.compare (p, sizeof (kFilter) - 1, kFilter) == 0)   // the old member: skip the number of the retired field
    {
        p += sizeof (kFilter) - 1;
        q = p;
        while (q < tail.size() && tail[q] >= '0' && tail[q] <= '9' && q - p < 3)
            ++q;
        if (q == p)
            return false;
        p = q;
    }
    if (p >= tail.size() || tail[p] != '}')
        return false;
    v[0] = n;
    return true;
}

}} // namespace synthyboy_mpc::tabview
