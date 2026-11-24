#!/usr/bin/env python3
# SynthyBoy - tests of 99_ui_screen_mod.py (native Mod screen controls and artwork).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import test_mpc_skin_extras as skin


@unittest.skipUnless((skin.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK unavailable")
class TestUiScreenMod(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.skins = skin.generate_skins(Path(cls.tmp.name), skin.LAYOUT_JSON)
        result = skin.run_apply(cls.skins, skin.REAL_EXTRAS)
        if result != (0, ""):
            raise AssertionError(result)
        page = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))["pageData"]
        cls.mod = next(tab for tab in page["tabs"] if tab["tabName"] == "Mod")
        cls.definitions = {d["key"]: d["value"] for d in page["componentDefinitions"]["localComponentDefinitions"]}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_generated_assets_exist_with_correct_dimensions(self):
        expected_assets = {
            "mod_mode_mode1_on.png": (80, 56),
            "mod_mode_mode1_off.png": (80, 56),
            "mod_mode_mode2_on.png": (140, 56),
            "mod_mode_mode2_off.png": (140, 56),
            "mod_mode_mode3_on.png": (120, 56),
            "mod_mode_mode3_off.png": (120, 56),
            "mod_action_add.png": (120, 56),
            "mod_action_edit.png": (76, 56),
            "mod_detail_delete.png": (120, 56),
            "mod_detail_confirm.png": (120, 56),
            "mod_selector_260x56.png": (260, 56),
        }
        for filename, (expected_w, expected_h) in expected_assets.items():
            path = self.skins / filename
            self.assertTrue(path.is_file(), f"Missing asset: {filename}")
            with Image.open(path) as img:
                self.assertEqual(img.size, (expected_w, expected_h),
                                 f"Asset {filename} size mismatch: {img.size} != {(expected_w, expected_h)}")
                self.assertEqual(img.mode, "RGBA", f"Asset {filename} mode mismatch: {img.mode}")

    def test_mode_buttons_use_verified_segmented_assets(self):
        mode_defs = [
            ("modNative_mode1_80x56", "mod_mode_mode1_on.png", "mod_mode_mode1_off.png", 1),
            ("modNative_mode2_140x56", "mod_mode_mode2_on.png", "mod_mode_mode2_off.png", 2),
            ("modNative_mode3_120x56", "mod_mode_mode3_on.png", "mod_mode_mode3_off.png", 3),
        ]
        for def_key, expected_on, expected_off, expected_id in mode_defs:
            self.assertIn(def_key, self.definitions)
            definition = self.definitions[def_key]
            button = definition["componentsData"][0]["componentData"]
            self.assertEqual(button["type"], "Button")
            self.assertEqual(button["data"]["onImage"], expected_on)
            self.assertEqual(button["data"]["offImage"], expected_off)
            self.assertEqual(button["data"]["buttonId"], expected_id)
            self.assertEqual(button["data"]["numButtonsInGroup"], 4)
            self.assertEqual(button["data"]["gestureBehaviour"], "Instant")

    def test_action_and_selector_background_assets_assigned(self):
        self.assertEqual(self.definitions["modNative_action_120x56"]["backgroundData"]["unfocussed"]["image"],
                         "mod_action_add.png")
        self.assertEqual(self.definitions["modNative_action_76x56"]["backgroundData"]["unfocussed"]["image"],
                         "mod_action_edit.png")
        self.assertEqual(self.definitions["modNative_group_260x56"]["backgroundData"]["unfocussed"]["image"],
                         "mod_selector_260x56.png")

    def test_no_visible_qlink_bounds_highlight(self):
        # Q-Link frames must not draw visible numbered host badges over the controls
        self.assertEqual(self.mod.get("qlinkBoundsData"), [])

    def test_component_count_and_markers_preserved(self):
        items = self.mod["componentDefinition"]["componentsData"]
        # Must have Native Modulation Editor at front
        self.assertEqual(items[0]["componentData"]["name"], "Native Modulation Editor")
        # Must end with Overlay Markers and Instance
        self.assertTrue(all(w["componentData"]["name"].startswith("Overlay ") for w in items[-3:]))
        self.assertEqual(len(items), 44)


if __name__ == "__main__":
    unittest.main()
