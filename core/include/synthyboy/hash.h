// SynthyBoy - core: parameter key hash.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>

namespace synthyboy
{
/**
 * The hash of a key for ASCII keys (h = 31 * h + c, 32-bit wraparound). ParamInfo::id == hashKey (ParamInfo::key): the id of a
 * parameter is stable and never changes while its key stays.
 */
constexpr uint32_t hashKey (const char* key) noexcept
{
    uint32_t h = 0;
    for (; *key != '\0'; ++key)
        h = 31u * h + static_cast<uint32_t> (static_cast<unsigned char> (*key));
    return h;
}
} // namespace synthyboy
