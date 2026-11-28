// SynthyBoy - MPC native WARP choice groups: the single constexpr two-level grouping of the 99 warp types (0 = None, 1..98 in ten groups).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "native_choice_list.h"

namespace synthyboy_mpc::nativeui::warpgroups
{
constexpr int kNumWarpTypes = 99;
constexpr int kFirstSpectralWarp = 76;
constexpr int kMaxGroupEntries = 20;

struct Group
{
    const char* name;
    bool spectral;
    int count;
    int entries[kMaxGroupEntries];
};

/** The ten groups of warp types in Level 1 order (0 = "None" is a direct final row before them).
    Union of all entries is 1..98 with no duplicates; spectral groups (entries 76..98) are only available
    when the oscillator is Spectral and the engine reports kCap2SpectralWarps. */
inline constexpr Group kGroups[] = {
    { "Sync + Shape",   false,  8, { 1, 11, 2, 3, 5, 6, 18, 19 } },
    { "Bend + Asym",    false,  7, { 4, 12, 13, 14, 15, 16, 17 } },
    { "Remap",          false,  5, { 20, 21, 22, 23, 24 } },
    { "Filter",         false,  2, { 25, 26 } },
    { "Distortion",     false, 14, { 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40 } },
    { "FM",             false, 20, { 7, 8, 41, 42, 43, 44, 45, 46, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 } },
    { "PD",             false,  7, { 47, 48, 49, 64, 65, 66, 67 } },
    { "AM + RM",        false, 12, { 50, 51, 68, 69, 70, 71, 9, 10, 72, 73, 74, 75 } },
    { "Spectral",       true,  17, { 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92 } },
    { "Mask + Vocode",  true,   6, { 93, 94, 95, 96, 97, 98 } },
};
constexpr int kGroupCount = static_cast<int> (sizeof (kGroups) / sizeof (kGroups[0]));

/** Compile-time check that kGroups partitions 1..98 exactly once and spectral flags match kFirstSpectralWarp. */
constexpr bool isValidGroupTable() noexcept
{
    std::array<bool, kNumWarpTypes> seen {};
    seen[0] = true;
    for (int g = 0; g < kGroupCount; ++g)
    {
        const auto& grp = kGroups[g];
        if (grp.name == nullptr || grp.count <= 0 || grp.count > kMaxGroupEntries)
            return false;
        for (int i = 0; i < grp.count; ++i)
        {
            const int idx = grp.entries[i];
            if (idx <= 0 || idx >= kNumWarpTypes || seen[static_cast<size_t> (idx)])
                return false;
            if ((idx >= kFirstSpectralWarp) != grp.spectral)
                return false;
            seen[static_cast<size_t> (idx)] = true;
        }
    }
    for (int i = 0; i < kNumWarpTypes; ++i)
        if (! seen[static_cast<size_t> (i)])
            return false;
    return true;
}

static_assert (isValidGroupTable(), "warp group table must partition 1..98 exactly once");

/** Populates `items` with the two-level WARP list from the warp table names/values (`nameOf(idx)`, `valueOf(idx)`) and entry availability (`isAvailable(idx)`).
    Level 1 has "None" (index 0) as a final row followed by each group that has at least one available entry. */
template <class NameFn, class ValueFn, class AvailFn>
inline bool populateWarpItems (choicelist::Items& items, NameFn&& nameOf, ValueFn&& valueOf, AvailFn&& isAvailable) noexcept
{
    items.clear();
    items.setTitle ("WARP");
    for (int i = 0; i < kNumWarpTypes; ++i)
    {
        if (! items.add (nameOf (i), valueOf (i)))
            return false;
    }
    if (isAvailable (0) && ! items.addRootItem (0))
        return false;
    for (int g = 0; g < kGroupCount; ++g)
    {
        const auto& grp = kGroups[g];
        int available[kMaxGroupEntries] {};
        int n = 0;
        for (int i = 0; i < grp.count; ++i)
        {
            const int idx = grp.entries[i];
            if (isAvailable (idx))
                available[n++] = idx;
        }
        if (n > 0 && ! items.addGroup (grp.name, available, n))
            return false;
    }
    return items.count > 0 && items.rootCount > 0;
}
} // namespace synthyboy_mpc::nativeui::warpgroups
