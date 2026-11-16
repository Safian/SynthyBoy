// SynthyBoy - MPC in-host drawing (experimental): which buffer an atomic commit is about to show. libdrm keeps the properties of a request in a struct it does not publish (xf86drmMode.c: a counter, a size and
// an array of {object, property, 64 bit value} items, in libdrm 2.4.100 and later with a cursor pointer after the value). The hook reads that array. The layout is NOT trusted: before the hook is installed
// the module asks libdrm itself to build a request with known items (drmModeAtomicAlloc / drmModeAtomicAddProperty, found with dlsym) and checks that this reader finds exactly those items; the item size
// that reproduces them (24 or 16 bytes) is the one used. A libdrm whose request cannot be read back is refused (the plane overlay stays).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace synthyboy_mpc { namespace overlay { namespace inhost {

struct ReqHeader
{
    uint32_t cursor;        // items in use
    uint32_t sizeItems;     // items allocated
    const uint8_t* items;
};

/** Reads the header and the value of the item `index` of a request whose items are `stride` bytes apart (value at offset 8). No allocation, no system call. */
inline bool readHeader (const void* req, ReqHeader& h) noexcept
{
    if (req == nullptr)
        return false;
    uint32_t w[2];
    std::memcpy (w, req, sizeof w);
    h.cursor = w[0];
    h.sizeItems = w[1];
    const void* p = nullptr;
    std::memcpy (&p, static_cast<const uint8_t*> (req) + 8, sizeof p);
    h.items = static_cast<const uint8_t*> (p);
    return h.items != nullptr && h.cursor <= h.sizeItems && h.sizeItems <= 4096u && (reinterpret_cast<uintptr_t> (h.items) & 3u) == 0;
}

/** The value of the property `prop` of object `obj` in the request; false when the request has none (or is not readable). `stride`: bytes per item (24 or 16). */
inline bool findItem (const void* req, size_t stride, uint32_t obj, uint32_t prop, uint64_t& value) noexcept
{
    ReqHeader h;
    if ((stride != 24 && stride != 16) || ! readHeader (req, h))
        return false;
    bool found = false;
    for (uint32_t i = 0; i < h.cursor; ++i)
    {
        const uint8_t* it = h.items + static_cast<size_t> (i) * stride;
        uint32_t o, p;
        uint64_t v;
        std::memcpy (&o, it, 4);
        std::memcpy (&p, it + 4, 4);
        std::memcpy (&v, it + 8, 8);
        if (o == obj && p == prop)
        {
            value = v;     // the last one counts (a property set twice in one request)
            found = true;
        }
    }
    return found;
}

/** The test against libdrm's own request: `req` was built with exactly the items (obj, prop, value) listed. Returns the stride that reads all of them back (24 or 16), 0 when none does. */
inline size_t detectStride (const void* req, const uint32_t (*items)[2], const uint64_t* values, int n) noexcept
{
    static const size_t kStrides[2] = { 24, 16 };
    for (size_t stride : kStrides)
    {
        bool all = true;
        ReqHeader h;
        if (! readHeader (req, h) || h.cursor != static_cast<uint32_t> (n))
            continue;
        for (int i = 0; i < n && all; ++i)
        {
            uint64_t v = 0;
            all = findItem (req, stride, items[i][0], items[i][1], v) && v == values[i];
        }
        if (all)
            return stride;
    }
    return 0;
}

}}} // namespace synthyboy_mpc::overlay::inhost
