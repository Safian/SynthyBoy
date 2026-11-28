// Hand-maintained (no generator): edit this file directly.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "native_play_osc.h"

namespace synthyboy_mpc::nativeui::playoscui
{
struct ContractRect { int x, y, w, h; };

inline constexpr std::array<ContractRect, kFields> kSubContractRects {{ { 18, 82, 64, 56 }, { 354, 82, 64, 56 }, { 0, 0, 0, 0 }, { 14, 404, 292, 72 }, { 310, 412, 112, 56 }, { 14, 510, 202, 108 }, { 220, 510, 202, 108 } }};
inline constexpr std::array<ContractRect, kFields> kAbcContractRects {{ { 18, 82, 64, 56 }, { 354, 82, 64, 56 }, { 282, 78, 64, 64 }, { 14, 404, 202, 72 }, { 220, 404, 202, 72 }, { 14, 510, 202, 108 }, { 220, 510, 202, 108 } }};
inline constexpr std::array<ContractRect, kFields> kNoiseContractRects {{ { 18, 82, 64, 56 }, { 354, 82, 64, 56 }, { 282, 78, 64, 64 }, { 14, 404, 202, 72 }, { 220, 404, 202, 72 }, { 14, 510, 202, 108 }, { 220, 510, 202, 108 } }};

constexpr ContractRect contractRect (int layout, int field) noexcept
{
    if (field < 0 || field >= kFields)
        return {};
    return layout == Sub ? kSubContractRects[field]
         : layout == Noise ? kNoiseContractRects[field]
         : kAbcContractRects[field];
}
} // namespace synthyboy_mpc::nativeui::playoscui
