#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates shell-mpc/ui/macro_card_contract.h from components/sizes.json."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from components.style import size as style_size

OUT_PATH = ROOT / "shell-mpc" / "ui" / "macro_card_contract.h"


def _rect_lit(vals):
    return "{ %d, %d, %d, %d }" % tuple(int(v) for v in vals)


def render_header():
    card_w = int(style_size("macro_card.width"))
    card_h = int(style_size("macro_card.height"))
    dial_rect = style_size("macro_card.dial_rect")
    knob_rect = style_size("macro_card.knob_rect")
    native_knob_rect = style_size("macro_card.native_knob_rect")
    name_rect = style_size("macro_card.name_rect")
    name_hit_rect = style_size("macro_card.name_hit_rect")
    value_rect = style_size("macro_card.value_rect")
    target_rect = style_size("macro_card.target_rect")
    chips_pad_x = int(style_size("macro_card.chips_pad_x"))
    chips_pad_bottom = int(style_size("macro_card.chips_pad_bottom"))
    chips_h = int(style_size("macro_card.chips_height"))

    bar_h = int(style_size("macros.bar_height"))
    margin = int(style_size("macros.margin"))
    gap = int(style_size("macros.gap"))
    bank_gap = int(style_size("macros.bank_gap"))
    title_rect = style_size("macros.title_rect")
    spectrum_rect = style_size("macros.spectrum_rect")
    positions = style_size("macros.card_positions")

    pos_lines = ",\n    ".join("{ %d, %d }" % (int(p[0]), int(p[1])) for p in positions)

    return f"""// Generated from components/sizes.json by scripts/gen-macro-contract.py. Do not edit by hand.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>

#include "core.h"

namespace synthyboy_mpc::ui::macrocard
{{
struct Point {{ int x, y; }};

inline constexpr int kCardW = {card_w};
inline constexpr int kCardH = {card_h};
inline constexpr int kChipsPadX = {chips_pad_x};
inline constexpr int kChipsPadBottom = {chips_pad_bottom};
inline constexpr int kChipsH = {chips_h};

inline constexpr Rect kDialRect {_rect_lit(dial_rect)};
inline constexpr Rect kKnobRect {_rect_lit(knob_rect)};
inline constexpr Rect kNativeKnobRect {_rect_lit(native_knob_rect)};
inline constexpr Rect kNameBoxRect {_rect_lit(name_rect)};
inline constexpr Rect kNameHitRect {_rect_lit(name_hit_rect)};
inline constexpr Rect kValueRect {_rect_lit(value_rect)};
inline constexpr Rect kTargetRect {_rect_lit(target_rect)};

inline constexpr int kBarH = {bar_h};
inline constexpr int kMargin = {margin};
inline constexpr int kGap = {gap};
inline constexpr int kBankGap = {bank_gap};
inline constexpr Rect kTitleRect {_rect_lit(title_rect)};
inline constexpr Rect kSpectrumRect {_rect_lit(spectrum_rect)};

inline constexpr std::array<Point, 8> kCardPositions {{{{
    {pos_lines}
}}}};
}} // namespace synthyboy_mpc::ui::macrocard
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify generated header matches components/sizes.json")
    args = parser.parse_args()

    text = render_header()
    if args.check:
        if not OUT_PATH.is_file() or OUT_PATH.read_text(encoding="utf-8") != text:
            sys.stderr.write(f"{OUT_PATH} is out of date; run scripts/gen-macro-contract.py\n")
            return 1
        return 0

    OUT_PATH.write_text(text, encoding="utf-8")
    print(f"Generated {OUT_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
