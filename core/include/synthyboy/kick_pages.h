// SynthyBoy - core: Kick remote-control page table and macro defaults (docs/ui-design/09-kick-integration.md).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace synthyboy
{
constexpr size_t kMaxKnobsPerPage = 8;
constexpr size_t kKickPageCount = 135;

struct Page
{
    const char* id;
    const char* title;
    const char* section;
    const char* keys[kMaxKnobsPerPage];
    size_t count;
    const char* const* moreKeys;
    size_t moreCount;
};

struct MacroTarget
{
    const char* destination;
    float amount;
    bool bipolar;
};

struct MacroAssignment
{
    const char* macroKey; // "macro.1" .. "macro.4"
    const char* label;    // max 12 chars
    const MacroTarget* targets;
    size_t targetCount;
};

struct CategoryMacroDefault
{
    const char* category;
    MacroAssignment macros[4];
};

const Page* kickPageTable() noexcept;
size_t kickPageCount() noexcept;

const CategoryMacroDefault* categoryMacroDefaults() noexcept;
size_t categoryMacroDefaultCount() noexcept;
} // namespace synthyboy
