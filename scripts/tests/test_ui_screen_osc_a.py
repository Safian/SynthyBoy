#!/usr/bin/env python3
# SynthyBoy - Unit and integration tests for Osc A native UI screen integration (99_ui_screen_osc_a.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
EXTRAS_DIR = ROOT / "scripts" / "mpc-skin-extras"
OSC_A_MODULE_PATH = EXTRAS_DIR / "99_ui_screen_osc_a.py"
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
APPLY_SCRIPT = ROOT / "scripts" / "apply-mpc-skin-extras.py"
GEN_SCRIPT = ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = ROOT / "scripts" / "run-gen-ui.py"
MPC_SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestOscAScreen(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.osc_a_mod = load_module("ui_screen_osc_a", OSC_A_MODULE_PATH)

    def test_autoload_discovery_and_ordering(self):
        """Verify 99_ui_screen_osc_a.py is placed properly in sorted autoload order."""
        self.assertTrue(OSC_A_MODULE_PATH.is_file(), f"Module file missing: {OSC_A_MODULE_PATH}")
        extras_scripts = sorted(p.name for p in EXTRAS_DIR.glob("*.py"))
        self.assertIn("99_ui_screen_osc_a.py", extras_scripts)

        idx = extras_scripts.index("99_ui_screen_osc_a.py")
        # Must run after 98_osc_surface.py and 99_backgrounds.py
        surface_idx = extras_scripts.index("98_osc_surface.py")
        bg_idx = extras_scripts.index("99_backgrounds.py")
        self.assertGreater(idx, surface_idx, "99_ui_screen_osc_a.py must run after 98_osc_surface.py")
        self.assertGreater(idx, bg_idx, "99_ui_screen_osc_a.py must run after 99_backgrounds.py")

    def test_screen_contract_metadata(self):
        """Verify rebuilt components and missing compact variants are properly enumerated."""
        contract_rebuilt = self.osc_a_mod.REBUILT_NATIVE_COMPONENTS
        self.assertGreaterEqual(len(contract_rebuilt), 10, "Must list all 10 rebuilt native components")

        rebuilt_ids = {c["id"] for c in contract_rebuilt}
        self.assertIn("osc_instance_select", rebuilt_ids)
        self.assertIn("osc_power", rebuilt_ids)
        self.assertIn("osc_type", rebuilt_ids)
        self.assertIn("osc_source_table", rebuilt_ids)
        self.assertIn("osc_wt_pos", rebuilt_ids)
        self.assertIn("osc_octave", rebuilt_ids)
        self.assertIn("osc_routing", rebuilt_ids)
        self.assertIn("osc_unison_block", rebuilt_ids)
        self.assertIn("osc_warp_block", rebuilt_ids)
        self.assertIn("osc_actions", rebuilt_ids)

        missing = self.osc_a_mod.MISSING_COMPACT_VARIANTS
        self.assertEqual(len(missing), 5, "Must enumerate the 5 missing compact variants")
        missing_ids = {m["id"] for m in missing}
        self.assertIn("pan_compact", missing_ids)
        self.assertIn("level_compact", missing_ids)
        self.assertIn("route_button_compact", missing_ids)
        self.assertIn("waveform_curve_compact", missing_ids)
        self.assertIn("cyan_knob_compact", missing_ids)

        rejected = self.osc_a_mod.REJECTED_CONCEPTS
        rejected_text = " ".join(rejected).lower()
        self.assertIn("side-controls", rejected_text)
        self.assertIn("q-link", rejected_text)

    def test_no_q_link_numbers_sanitization(self):
        """Verify that any accidental visible Q-Link numbers/chips are detected and sanitized."""
        dummy_tab = {
            "componentDefinition": {
                "componentsData": [
                    {
                        "componentData": {
                            "name": "Dummy Knob",
                            "type": "Knob",
                            "text": "Q1 Cutoff",
                        }
                    },
                    {
                        "componentData": {
                            "name": "Badge",
                            "type": "Label",
                            "text": "Q 4",
                        }
                    },
                ]
            }
        }
        self.osc_a_mod._check_and_sanitize_no_q_numbers(dummy_tab)
        c0_text = dummy_tab["componentDefinition"]["componentsData"][0]["componentData"]["text"]
        c1_text = dummy_tab["componentDefinition"]["componentsData"][1]["componentData"]["text"]
        self.assertEqual(c0_text, "Cutoff")
        self.assertEqual(c1_text, "")

    def test_audit_osc_a_tab_validation(self):
        """Verify audit_osc_a_tab validates required components and fails on missing ones."""
        valid_tab = {
            "componentDefinition": {
                "componentsData": [
                    {"componentData": {"name": "Native Modulation Editor", "type": "nativeModulationEditor"}},
                    {"componentData": {"name": "Osc Native Surface", "type": "oscNativeSurface"}},
                ]
            }
        }
        defs = {
            "oscNativeSourceChoices": {},
            "oscNativeRoutingChoices": {},
            "oscNativeSurface": {"componentsData": []},
        }
        # Should pass without exception
        self.osc_a_mod.audit_osc_a_tab(valid_tab, defs)

        # Missing Native Modulation Editor should raise
        invalid_tab = copy.deepcopy(valid_tab)
        invalid_tab["componentDefinition"]["componentsData"].pop(0)
        with self.assertRaises(ValueError):
            self.osc_a_mod.audit_osc_a_tab(invalid_tab, defs)

    def test_full_skin_pipeline_end_to_end(self):
        """Verify full skin generation with apply-mpc-skin-extras.py and 99_ui_screen_osc_a.py."""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            ui_tmp = tmp / "ui"
            skins_tmp = tmp / "skins"
            ui_tmp.mkdir()
            skins_tmp.mkdir()

            # 1. gen-mpc-ui.py
            cmd_gen = [
                sys.executable, str(GEN_SCRIPT),
                "--params", str(PARAMS_JSON),
                "--pages", str(PAGES_JSON),
                "--layout", str(LAYOUT_JSON),
                "--out", str(ui_tmp),
            ]
            res = subprocess.run(cmd_gen, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"gen-mpc-ui failed: {res.stderr}")

            # 2. run-gen-ui.py
            cmd_run = [
                sys.executable, str(RUN_GEN_UI),
                "--sdk", str(MPC_SDK),
                "--params", str(ui_tmp / "params-hostsim.json"),
                "--ui", str(ui_tmp / "ui.json"),
                "--out", str(skins_tmp),
                "--quiet",
            ]
            res = subprocess.run(cmd_run, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"run-gen-ui failed: {res.stderr}")

            # 3. apply-mpc-skin-extras.py
            cmd_apply = [
                sys.executable, str(APPLY_SCRIPT),
                "--skins", str(skins_tmp),
                "--params", str(PARAMS_JSON),
                "--meta", str(ui_tmp / "skin-meta.json"),
            ]
            res = subprocess.run(cmd_apply, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"apply-mpc-skin-extras failed: {res.stderr}")

            # 4. Verify generated TUI.json
            tui = json.loads((skins_tmp / "TUI.json").read_text(encoding="utf-8"))
            osc_tab = next(t for t in tui["pageData"]["tabs"] if t.get("tabName") in ("Osc", "Osc A"))
            self.assertIsNotNone(osc_tab, "Osc A tab must exist in TUI.json")

            tab_comp_names = {c["componentData"]["name"] for c in osc_tab["componentDefinition"]["componentsData"]}
            self.assertIn("Native Modulation Editor", tab_comp_names)
            self.assertIn("Osc Native Surface", tab_comp_names)

            self.check_warp_picker(tui)

            # Check that no visible Q-Link numbers appear anywhere in Osc A tab text
            tab_str = json.dumps(osc_tab)
            import re
            self.assertFalse(bool(re.search(r'"text":\s*"[^"]*\bQ[1-9]\b[^"]*"', tab_str)),
                             "No visible Q-Link numbers (Q1..Q4) allowed in text fields")

    def check_warp_picker(self, tui):
        definitions = {item["key"]: item["value"] for item in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        params = {param["key"]: param for param in json.loads(PARAMS_JSON.read_text())["params"]}
        choices = params["osc1.warp.type"]["choices"]
        items = definitions["oscWarpChoices"]["componentsData"]
        local = "Local/7/synthyboyWarpPage"
        rows = [item for item in items if item["componentData"]["name"].startswith("Choose ")]
        self.assertEqual(len(rows), 99)
        for index, (choice, row) in enumerate(zip(choices, rows)):
            self.assertEqual(row["componentData"]["name"], "Choose " + choice)
            value = row["componentData"]["data"]
            self.assertEqual((value["buttonId"], value["numButtonsInGroup"], value["handleName"]), (index, 99, "Data"))
            gates = [f"IndexedEnabling/{index // 16}/7/{local}"]
            self.assertEqual(row["bounds"]["additionalInvalidatingHandles"], gates)
            label = next(item for item in items if item["componentData"]["name"] == choice)
            self.assertEqual(label["componentData"]["data"]["text"], choice)
            self.assertEqual(label["bounds"]["additionalInvalidatingHandles"], gates)
        navigation = [item for item in items if item["handle remapping"]["map"] == [{"key": "Data", "value": local}]]
        self.assertEqual([item["componentData"]["data"]["buttonId"] for item in navigation], list(range(7)))
        trigger = definitions["oscNativeSettingWarpChoice"]
        self.assertEqual({action["additionalData"] for action in trigger["actions"]}, {"oscWarpChoices"})
        for action in trigger["actions"]:
            self.assertEqual(action["handleName"], "Data")
        self.assertFalse(definitions["oscNativeSettingWarpChoiceOff"]["actions"])
        detail = definitions["oscDetailTile"]["componentsData"]
        self.assertTrue(any(item["componentData"]["type"] == "oscNativeSettingWarpChoice" for item in detail))

    def test_rendered_preview_exists_and_verified(self):
        """Verify that the full-screen verified visual preview image exists and has correct dimensions."""
        preview_file = (Path(__file__).resolve().parents[2] / 'docs/screens/osc_a.jpg')
        self.assertTrue(preview_file.is_file(), f"Preview file missing: {preview_file}")
        from PIL import Image
        with Image.open(preview_file) as im:
            self.assertEqual(im.size, (1280, 800), f"Preview dimensions must be 1280x800, got {im.size}")


if __name__ == "__main__":
    unittest.main()
