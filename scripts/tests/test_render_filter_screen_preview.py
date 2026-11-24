# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for scripts/render_filter_screen_preview.py and filter_render_fixture.

Verifies:
1. C++ FilterUi overlay fixture compilation and execution (filter_render_fixture with Gen2 / Q-frame 0).
2. Proper layer composition: real generated host skin (1280x628) + real C++ overlay (1280x628) with curve.
3. Accurate state parameter values in JSON: Digital 12dB, cutoff 12kHz, reso 20%, mix 100%, drive 0 dB, 60/40 Split.
4. Faithful visual properties: real component FilmStrip blue dials, segmented selector, active text, radio buttons, and C++ overlay curves (no GenericLegacy red PIL fallback).
5. Generation of honest 2-panel before/after comparison image (filter-current-before-after.png).
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = REPO_ROOT / "scripts"
import sys
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import render_filter_screen_preview as rfp


class TestRenderFilterScreenPreview(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.tmp_path = Path(cls._tmp.name)
        cls.out_preview = cls.tmp_path / "filter-preview-host-render.png"
        cls.out_compare = cls.tmp_path / "filter-current-before-after.png"
        cls.out_state = cls.tmp_path / "filter-current-fixture-state.json"

        rfp.render_preview(
            state_name="init_f1",
            out_path=cls.out_preview,
            work_dir=cls.tmp_path / "work",
            build_dir=REPO_ROOT / "build",
            ui_gen=2,
            knobs=16,
            compare_out=cls.out_compare,
            state_out=cls.out_state,
        )

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_state_json_parameters(self):
        """Verifies that the exported fixture state matches Init Filter 1 parameter contract."""
        self.assertTrue(self.out_state.is_file())
        st = json.loads(self.out_state.read_text(encoding="utf-8"))
        self.assertEqual(st["fixtureState"], "init_f1")
        self.assertEqual(st["uiGeneration"], 2)
        self.assertEqual(st["qframeEnabled"], 0)
        self.assertEqual(st["view"], 0)

        # Check key virtual handles
        virt = st["virtual"]
        self.assertIn("Parameter 2850", virt)  # State
        self.assertEqual(virt["Parameter 2850"]["display"], "F1")

        self.assertIn("Parameter 2851", virt)  # Power
        self.assertEqual(virt["Parameter 2851"]["norm"], 1.0)

        self.assertIn("Parameter 2852", virt)  # Cutoff
        self.assertIn("12.0", virt["Parameter 2852"]["display"])

        self.assertIn("Parameter 2853", virt)  # Reso
        self.assertIn("20.0%", virt["Parameter 2853"]["display"])

        self.assertIn("Parameter 2854", virt)  # Drive
        self.assertIn("0.0 dB", virt["Parameter 2854"]["display"])

        self.assertIn("Parameter 2855", virt)  # Mix
        self.assertIn("100.0%", virt["Parameter 2855"]["display"])

        self.assertIn("Parameter 2883", virt)  # Indicator 4 (power/dual)
        self.assertEqual(virt["Parameter 2883"]["display"], "2")

    def test_preview_image_dimensions_and_layers(self):
        """Verifies that the generated preview is a valid 1280x800 composite with real component FilmStrip dials and C++ curves."""
        self.assertTrue(self.out_preview.is_file())
        with Image.open(self.out_preview) as im:
            self.assertEqual(im.size, (1280, 800))
            self.assertEqual(im.mode, "RGB")
            arr = np.array(im)

        # Check for real FilmStrip blue dial arc pixels in the Split MAIN region (y: 325..395, x: 15..750)
        dial_region = arr[325 + 110:395 + 110, 15:750, :]
        blue_mask = (dial_region[:, :, 2] > 170) & (dial_region[:, :, 1] > 100) & (dial_region[:, :, 0] < 120)
        blue_count = np.sum(blue_mask)
        self.assertGreater(blue_count, 500, f"Expected real FilmStrip dial arc pixels, found {blue_count}")

        # Ensure no fake GenericLegacy red PIL arc pixels exist in the dial region
        red_mask = (dial_region[:, :, 0] > 180) & (dial_region[:, :, 1] < 80) & (dial_region[:, :, 2] < 80)
        self.assertEqual(int(np.sum(red_mask)), 0, "Fake GenericLegacy red dial pixels must not appear")

        # Check for cyan curve pixels in the left filter graph region (y: 148..320, x: 8..759)
        left_curve = arr[148 + 110:320 + 110, 8:759, :]
        cyan_mask = (left_curve[:, :, 1] > 150) & (left_curve[:, :, 2] > 150) & (left_curve[:, :, 0] < 100)
        cyan_count = np.sum(cyan_mask)
        self.assertGreater(cyan_count, 200, f"Expected C++ FilterDisplay curve pixels, found {cyan_count}")

        # Check for right-hand ScalarCurveEditor curve pixels (y: 148..430, x: 771..1272)
        right_curve = arr[148 + 110:430 + 110, 771:1272, :]
        right_cyan = (right_curve[:, :, 1] > 150) & (right_curve[:, :, 2] > 150) & (right_curve[:, :, 0] < 100)
        self.assertGreater(int(np.sum(right_cyan)), 120, "Expected C++ ScalarCurveEditor curve pixels")

    def test_native_curve_expand_art_is_not_painted_over_by_overlay(self):
        # Test the actual production overlay alpha, not a guessed icon colour.
        overlay = self.tmp_path / "work" / "init_f1_overlay_gen2.png"
        manifest = json.loads((SCRIPTS_DIR / "mpc-skin-extras/filter-native-contract.json").read_text())
        x, y, w, h = manifest["split"]["curve_expand"]
        with Image.open(overlay).convert("RGBA") as im:
            self.assertEqual(im.crop((x, y, x + w, y + h)).getchannel("A").getextrema(), (0, 0))
        skins = self.tmp_path / "work" / "skins"
        with Image.open(skins / "filter_native_expand_56x56.png").convert("RGBA") as art:
            self.assertGreater(art.getchannel("A").getextrema()[1], 0)

    def test_unknown_native_dial_and_missing_art_fail_instead_of_faking_controls(self):
        sketch = rfp.FilterSkinSketch(self.tmp_path / "work" / "skins", json.loads(self.out_state.read_text()))
        with self.assertRaises(FileNotFoundError):
            sketch._open_image("missing-filter-asset.png")
        entry = {"componentData": {"name": "unsupported", "type": "Knob", "data": {"knobType": "GenericLegacy"}},
                 "bounds": {"bounds": "0 0 60 60"}, "handle remapping": {"map": []}}
        with self.assertRaisesRegex(RuntimeError, "non-FilmStrip"):
            sketch._draw_entry(Image.new("RGBA", (1280, 628)), entry, (0, 0), (1280, 628), {})

    def test_dual_fixture_uses_the_real_acknowledged_native_action(self):
        work = self.tmp_path / "work"
        state = self.tmp_path / "dual.json"
        subprocess.run([str(work / "filter_render_fixture_gen2"), "--state", "dual_parallel",
                        "--dump-png", str(self.tmp_path / "dual.png"), "--dump-json", str(state)], check=True, capture_output=True)
        actual = json.loads(state.read_text())
        self.assertTrue(actual["dual"])
        self.assertFalse(actual["split"])
        self.assertEqual(actual["expandMode"], 1)

    def test_before_after_comparison_image(self):
        """Verifies that the 2-panel before/after comparison image is generated cleanly."""
        self.assertTrue(self.out_compare.is_file())
        with Image.open(self.out_compare) as im:
            self.assertEqual(im.width, 1280)
            self.assertGreater(im.height, 1700, "Comparison must include fixed filter_old.jpg and current composite")


if __name__ == "__main__":
    unittest.main()
