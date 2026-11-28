// Hand-maintained (no generator): edit this file directly.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "native_play_osc.h"

namespace synthyboy_mpc::nativeui::playoscui
{
struct ContractRect { int x, y, w, h; };

inline constexpr std::array<ContractRect, kFields> kSubContractRects {{ { 14, 10, 56, 56 }, { 366, 10, 56, 56 }, { 0, 0, 0, 0 }, { 14, 439, 284, 60 }, { 306, 451, 116, 56 }, { 14, 524, 200, 94 }, { 222, 524, 200, 94 } }};
inline constexpr std::array<ContractRect, kFields> kAbcContractRects {{ { 14, 10, 56, 56 }, { 366, 10, 56, 56 }, { 294, 10, 72, 56 }, { 14, 439, 200, 60 }, { 222, 439, 200, 60 }, { 14, 524, 200, 94 }, { 222, 524, 200, 94 } }};
inline constexpr std::array<ContractRect, kFields> kNoiseContractRects {{ { 14, 10, 56, 56 }, { 366, 10, 56, 56 }, { 294, 10, 72, 56 }, { 14, 439, 200, 60 }, { 222, 439, 200, 60 }, { 14, 524, 200, 94 }, { 222, 524, 200, 94 } }};

constexpr ContractRect contractRect (int layout, int field) noexcept
{
    if (field < 0 || field >= kFields)
        return {};
    return layout == Sub ? kSubContractRects[field]
         : layout == Noise ? kNoiseContractRects[field]
         : kAbcContractRects[field];
}
} // namespace synthyboy_mpc::nativeui::playoscui
