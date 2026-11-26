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
import native_ui_params as handles
from components.native.selector.selector import dropdown_asset_name


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
        cls.items = {c["componentData"]["name"]: c for c in cls.mod["componentDefinition"]["componentsData"]}
        cls.base = handles.native_base_index(len(json.loads(skin.PARAMS_JSON.read_text())["params"]))
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
            "mod_action_edit.png": (76, 56),
            "mod_detail_delete.png": (120, 56),
            "mod_detail_confirm.png": (120, 56),
            dropdown_asset_name(260, 56): (260, 56),
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
        add = self.items["Mod Native ADD"]["componentData"]
        back = self.items["Mod Native Picker BACK"]["componentData"]
        groups = self.items["Mod Native Picker GROUPS"]["componentData"]
        self.assertEqual([c["type"] for c in (add, back, groups)], ["Button"] * 3)
        self.assertEqual(len({c["data"]["onImage"] for c in (add, back, groups)}), 3)
        self.assertEqual(self.definitions["modNative_action_76x56"]["backgroundData"]["unfocussed"]["image"],
                         "mod_action_edit.png")
        self.assertEqual(self.definitions["modNative_group_260x56"]["componentsData"][0]["componentData"]["data"]["image"],
                         dropdown_asset_name(260, 56))

    def test_no_visible_qlink_bounds_highlight(self):
        # Q-Link frames must not draw visible numbered host badges over the controls
        self.assertEqual(self.mod.get("qlinkBoundsData"), [])

    def test_component_count_and_markers_preserved(self):
        items = self.mod["componentDefinition"]["componentsData"]
        # Must have Native Modulation Editor at front
        self.assertEqual(items[0]["componentData"]["name"], "Native Modulation Editor")
        self.assertEqual(sum(c["componentData"]["name"].startswith("Overlay Marker mod ") for c in items), 2)
        self.assertIn("Overlay Instance mod", self.items)
        self.assertEqual(sum(name.startswith("Mod Native Board Chip ") for name in self.items), 17)
        self.assertEqual(sum(name.startswith("Mod Native Picker Row ") for name in self.items), handles.MOD_LIST_ROWS)

    def test_matrix_board_picker_and_detail_do_not_overlap(self):
        def visible(item, values):
            for gate in item["bounds"]["additionalInvalidatingHandles"]:
                parts = gate.split("/")
                value = values.get(int(parts[-1].split()[1]) - self.base, 0)
                if parts[0] == "IndexedEnabling":
                    if value != int(parts[1]):
                        return False
                elif parts[0] == "RangedEnabling":
                    if not int(parts[1]) <= value <= int(parts[2]):
                        return False
                else:
                    self.fail(f"Unknown Mod visibility gate {gate}")
            return True

        def shown(values, prefix):
            return {name for name, item in self.items.items() if name.startswith(prefix) and visible(item, values)}

        # Force all picker and board flags high to model stale host parameters.
        # The matrix view and navigation mode still exclude both sets of widgets.
        stale = {handles.MOD_LIST_GATE: 1, handles.MOD_BOARD_GATE: 1, handles.MOD_LIST_ROW_GATE: 1}
        for frame in (1, 2, 3, 4):
            state = dict(stale)
            state.update({handles.MOD_VIEW: frame, handles.MOD_NAVIGATION: 1 if frame < 3 else 0,
                          handles.MOD_ROWS: 1})
            self.assertFalse(shown(state, "Mod Native Board"))
            self.assertFalse(shown(state, "Mod Native Picker"))
            self.assertEqual(bool(shown(state, "Mod Native Detail Back")), frame >= 3)
            self.assertEqual(bool(shown(state, "Mod Native ADD")), frame < 3)
            self.assertEqual(bool(shown(state, "Mod Native Normal Row")), frame == 1)
            self.assertEqual(bool(shown(state, "Mod Native Scroll Row")), frame == 2)
        board = {**stale, handles.MOD_NAVIGATION: 1}
        self.assertTrue(shown(board, "Mod Native Board"))
        self.assertFalse(shown(board, "Mod Native Picker"))
        self.assertFalse(shown(stale, "Mod Native Board"))
        self.assertEqual(shown(stale, "Mod Native Picker Row"), {"Mod Native Picker Row 1"})


if __name__ == "__main__":
    unittest.main()
