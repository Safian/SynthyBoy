// Edit this file directly.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "core.h"

namespace synthyboy_mpc::ui::macrocard
{
struct Point { int x, y; };

inline constexpr int kCardW = 152;
inline constexpr int kCardH = 238;

inline constexpr Rect kNativeKnobRect { 29, 2, 94, 92 };
inline constexpr Rect kNameBoxRect { 8, 136, 136, 24 };
inline constexpr Rect kNameHitRect { 4, 120, 144, 56 };
inline constexpr Rect kTargetRect { 4, 178, 144, 56 };

inline constexpr int kBarH = 64;
inline constexpr int kMargin = 8;
inline constexpr int kGap = 6;
inline constexpr Rect kTitleRect { 14, 4, 260, 56 };
inline constexpr Rect kSpectrumRect { 8, 72, 1264, 290 };

inline constexpr int kPickerRowStep = 60;
inline constexpr Rect kPickerRow { 40, 96, 1200, 56 };
inline constexpr Rect kPickerTitle { 280, 12, 720, 56 };
inline constexpr Rect kPickerBack { 40, 12, 120, 56 };
inline constexpr Rect kPickerGroups { 170, 12, 100, 56 };
inline constexpr Rect kPickerPrev { 40, 548, 150, 56 };
inline constexpr Rect kPickerNext { 1090, 548, 150, 56 };

inline constexpr std::array<Point, 8> kCardPositions {{
    { 8, 382 },
    { 166, 382 },
    { 324, 382 },
    { 482, 382 },
    { 646, 382 },
    { 804, 382 },
    { 962, 382 },
    { 1120, 382 }
}};
} // namespace synthyboy_mpc::ui::macrocard
