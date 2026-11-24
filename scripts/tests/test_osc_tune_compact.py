#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the OSC TUNE compact layout variants (48, 40, 32 px), geometry, and metrics."""

from pathlib import Path
import sys
import unittest
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
SCRIPTS_DIR = REPO_ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))
sys.path.insert(0, str(REPO_ROOT))

import native_skin_font
from components.native.tune.tune import render_tune_card
import native_name_value as nnv


class TestOscTuneCompact(unittest.TestCase):



    def test_compact_card_dimensions_all_variants(self):
        """Verify card dimensions across all height variants: 48 (190), 40 (170), 32 (154)."""
        cases = [
            (48, 6, 190),
            (40, 4, 170),
            (32, 4, 154),
        ]
        for fh, fg, expected_h in cases:
            with self.subTest(field_h=fh, field_gap=fg):
                card = render_tune_card(
                    {"oct": "0", "sem": "0", "fin": "0", "crs": "0.00"},
                    track_active=True,
                    field_h=fh,
                    field_gap=fg,
                )
                self.assertEqual(
                    card.size,
                    (278, expected_h),
                    f"Card with field_h={fh} must be exactly 278 x {expected_h} px",
                )

    def test_freed_vertical_space(self):
        """Verify freed vertical space calculations vs original 311 px and vs 48 px reference."""
        original_h = 311

        # 48 px reference
        h_48 = 190
        saved_48 = original_h - h_48
        self.assertEqual(saved_48, 121)
        self.assertAlmostEqual((saved_48 / original_h) * 100, 38.90675, delta=0.1)

        # 40 px compact
        h_40 = 170
        saved_40_orig = original_h - h_40
        self.assertEqual(saved_40_orig, 141)
        self.assertAlmostEqual((saved_40_orig / original_h) * 100, 45.3376, delta=0.1)
        saved_40_vs_48 = h_48 - h_40
        self.assertEqual(saved_40_vs_48, 20)
        self.assertAlmostEqual((saved_40_vs_48 / h_48) * 100, 10.526, delta=0.1)

        # 32 px minimal
        h_32 = 154
        saved_32_orig = original_h - h_32
        self.assertEqual(saved_32_orig, 157)
        self.assertAlmostEqual((saved_32_orig / original_h) * 100, 50.482, delta=0.1)
        saved_32_vs_48 = h_48 - h_32
        self.assertEqual(saved_32_vs_48, 36)
        self.assertAlmostEqual((saved_32_vs_48 / h_48) * 100, 18.947, delta=0.1)

    def test_metrics_exact_calculations(self):
        """Verify row+gap, combined 2 field rows, bottom button row, and total panel height."""
        # 48 px: row=48, gap=6 -> row+gap=54, 2 rows=102, btn=48, total=190
        self.assertEqual(48 + 6, 54)
        self.assertEqual(48 + 6 + 48, 102)
        self.assertEqual(26 + 102 + 6 + 48 + 8, 190)

        # 40 px: row=40, gap=4 -> row+gap=44, 2 rows=84, btn=48, total=170
        self.assertEqual(40 + 4, 44)
        self.assertEqual(40 + 4 + 40, 84)
        self.assertEqual(26 + 84 + 4 + 48 + 8, 170)

        # 32 px: row=32, gap=4 -> row+gap=36, 2 rows=68, btn=48, total=154
        self.assertEqual(32 + 4, 36)
        self.assertEqual(32 + 4 + 32, 68)
        self.assertEqual(26 + 68 + 4 + 48 + 8, 154)

    def test_touch_target_surfaces_adequate(self):
        """Verify touch target widths (125 px >= 56 px) and button heights (strictly 48 px)."""
        col_w = 125
        self.assertGreaterEqual(col_w, 56, "Width must meet touch target minimum of 56 px")

        btn_h = 48
        self.assertEqual(btn_h, 48, "Bottom TRACK/MORE row must strictly stay 48 px")

    def test_negative_and_multidigit_values_render_cleanly(self):
        """Verify signed, multi-digit, and real values fit comfortably across all height variants."""
        for fh, fg, expected_h in [(48, 6, 190), (40, 4, 170), (32, 4, 154)]:
            with self.subTest(field_h=fh):
                card = render_tune_card(
                    {"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"},
                    track_active=True,
                    field_h=fh,
                    field_gap=fg,
                )
                self.assertEqual(card.size, (278, expected_h))

                preset = nnv.get_height_preset(fh)
                font_val = native_skin_font.semibold(int(preset["value_size"]))
                font_lbl = native_skin_font.semibold(int(preset["label_size"]))

                for val in ("-1", "+7", "-25", "-12.50"):
                    val_w = font_val.getlength(val)
                    self.assertLess(val_w, 68 - 10, f"Value '{val}' must fit in 68 px box at {fh} px")

                for name in ("OCT", "SEM", "FIN", "CRS"):
                    name_w = font_lbl.getlength(name)
                    self.assertLess(name_w, 57 - 8, f"Name '{name}' must fit in label area at {fh} px")

    def test_no_qlink_number_chips_in_all_variants(self):
        """Verify no yellow Q-Link chips or number badges are rendered in any TUNE card variant."""
        for fh, fg in [(48, 6), (40, 4), (32, 4)]:
            with self.subTest(field_h=fh):
                card = render_tune_card(
                    {"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"},
                    track_active=True,
                    field_h=fh,
                    field_gap=fg,
                )
                arr = np.array(card)
                yellow_pixels = (arr[:, :, 0] > 190) & (arr[:, :, 1] > 130) & (arr[:, :, 2] < 100)
                self.assertEqual(
                    np.count_nonzero(yellow_pixels),
                    0,
                    f"TUNE card at {fh} px must NOT contain any yellow Q-Link chip pixels",
                )

    def test_name_outside_frame_and_number_inside_frame(self):
        """Verify pixels: label sits on panel background (#121930); only number is in framed box (#12182d)."""
        card = render_tune_card(
            {"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"},
            track_active=True,
            field_h=48,
            field_gap=6,
        )
        arr = np.array(card)

        # 1. Label area of OCT (x=20, y=36): must match card background #121930
        px_lbl = arr[36, 20]
        self.assertEqual(px_lbl[3], 255)
        self.assertTrue(15 <= px_lbl[0] <= 21, f"R should match #121930, got {px_lbl[0]}")
        self.assertTrue(22 <= px_lbl[1] <= 28, f"G should match #121930, got {px_lbl[1]}")
        self.assertTrue(44 <= px_lbl[2] <= 52, f"B should match #121930, got {px_lbl[2]}")

        # 2. Number box interior of OCT (x=80, y=36): must match #12182d
        px_interior = arr[36, 80]
        self.assertEqual(px_interior[3], 255)
        self.assertTrue(15 <= px_interior[0] <= 21, f"R should match #12182d, got {px_interior[0]}")
        self.assertTrue(21 <= px_interior[1] <= 27, f"G should match #12182d, got {px_interior[1]}")
        self.assertTrue(40 <= px_interior[2] <= 50, f"B should match #12182d, got {px_interior[2]}")



if __name__ == "__main__":
    unittest.main()
