# SynthyBoy - tests for Osc B screen native component integration and verification.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
GEN = ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = ROOT / "scripts" / "run-gen-ui.py"
APPLY = ROOT / "scripts" / "apply-mpc-skin-extras.py"
OFFICIAL_SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")

# 99_ui_screen_osc_b is loaded dynamically in the test


class MockContext:
    def __init__(self, tui, params, meta):
        self._tui = tui
        self.params = params.get("params", [])
        self._keys = [p["key"] for p in self.params]
        self.param_count = len(self._keys)
        self.meta = meta


class TestUiScreenOscB(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sdk_path = OFFICIAL_SDK if OFFICIAL_SDK.is_dir() else Path(os.environ.get("MPC_SDK", ""))
        if not cls.sdk_path.is_dir():
            raise unittest.SkipTest(f"Official MPC SDK not found at {OFFICIAL_SDK}")

        cls.tmp_dir = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp_dir.name)
        ui = tmp / "ui"
        env = os.environ.copy()
        env["PYTHONPATH"] = f"scripts:/Users/szabi/Library/Python/3.9/lib/python/site-packages:{env.get('PYTHONPATH', '')}"

        # 1. Generate UI metadata
        subprocess.run(
            [sys.executable, str(GEN), "--params", str(PARAMS_JSON), "--pages", str(PAGES_JSON),
             "--layout", str(LAYOUT_JSON), "--out", str(ui)],
            check=True, env=env, capture_output=True
        )

        # 2. Run official SDK generator
        cls.skins_dir = tmp / "pf" / "Plugin Skins"
        subprocess.run(
            [sys.executable, str(RUN_GEN_UI), "--sdk", str(cls.sdk_path),
             "--params", str(ui / "params-hostsim.json"), "--ui", str(ui / "ui.json"),
             "--out", str(cls.skins_dir), "--quiet"],
            check=True, env=env, capture_output=True
        )

        # 3. Apply skin extras pipeline
        meta = ui / "skin-meta.json"
        res = subprocess.run(
            [sys.executable, str(APPLY), "--skins", str(cls.skins_dir),
             "--params", str(PARAMS_JSON), "--meta", str(meta)],
            capture_output=True, text=True, env=env
        )
        if res.returncode != 0:
            raise RuntimeError(f"apply-mpc-skin-extras failed: {res.stderr}")

        cls.tui = json.loads((cls.skins_dir / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins_dir / "Q-Links.json").read_text(encoding="utf-8"))
        cls.params_doc = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))
        cls.meta_doc = json.loads(meta.read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp_dir.cleanup()

    def test_osc_b_tab_structure(self):
        tabs = self.tui["pageData"]["tabs"]
        tab = next((t for t in tabs if t["tabName"] == "Osc B"), None)
        self.assertIsNotNone(tab, "Tab 'Osc B' must exist in TUI.json")

        # Function key 1 (Osc), SubTab 1 (Osc B)
        self.assertEqual(tab["fnKeyIndex"], 1)
        self.assertEqual(tab["fnKeySubIndex"], 1)

        # Owner rule: No visible Q-Link frames or numbered badges on the native screen
        self.assertEqual(tab["qlinkBoundsData"], [], "qlinkBoundsData must be empty [] to avoid host Q frames")

        # Markers and surface
        comp_names = [c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]]
        self.assertIn("Overlay Marker oscb left", comp_names)
        self.assertIn("Overlay Marker oscb right", comp_names)
        self.assertIn("Overlay Instance oscb", comp_names)
        self.assertIn("Osc Native Surface", comp_names)

    def test_osc_b_native_surface_components(self):
        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        surface = defs.get("oscNativeSurface")
        self.assertIsNotNone(surface, "oscNativeSurface definition must exist")

        names = [c["componentData"]["name"] for c in surface["componentsData"]]
        expected = [
            "Osc Native Instance Select",
            "Osc Native Pan",
            "Osc Native Level",
            "Osc Native Routing",
            "Osc Native Power",
            "Osc Native Settings",
            "Osc Native Detail",
            "Osc Native Tune A",
            "Osc Native Tune B",
            "Osc Native Tune C",
        ]
        self.assertEqual(names, expected)

        # Verify touch rectangles
        child_by_name = {c["componentData"]["name"]: c for c in surface["componentsData"]}
        self.assertEqual(child_by_name["Osc Native Instance Select"]["bounds"]["bounds"], "8 33 200 56")
        self.assertEqual(child_by_name["Osc Native Pan"]["bounds"]["bounds"], "708 37 202 48")
        self.assertEqual(child_by_name["Osc Native Level"]["bounds"]["bounds"], "914 37 202 48")
        self.assertEqual(child_by_name["Osc Native Routing"]["bounds"]["bounds"], "1122 37 150 48")
        self.assertEqual(child_by_name["Osc Native Power"]["bounds"]["bounds"], "12 194 352 56")

    def test_osc_b_bindings_isolation(self):
        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

        # Power button for Osc B: osc2.on -> Parameter 2974
        power_layer = defs["oscNativePowerLayer"]
        power_b = next((c for c in power_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Osc B Power"), None)
        self.assertIsNotNone(power_b)
        self.assertEqual(power_b["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 2974"}])

        # Routing dropdown for Osc B: osc2.destination -> Parameter 3008
        routing_layer = defs["oscNativeRoutingLayer"]
        routing_b = next((c for c in routing_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Routing Osc B"), None)
        self.assertIsNotNone(routing_b)
        self.assertEqual(routing_b["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 3008"}])

        # Pan dial for Osc B: osc2.pan -> Parameter 37
        pan_layer = defs["oscNativePanLayer"]
        pan_b = next((c for c in pan_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Pan Osc B"), None)
        self.assertIsNotNone(pan_b)
        self.assertEqual(pan_b["handle remapping"]["map"][0]["value"], "Parameter 37")
        self.assertEqual(pan_b["handle remapping"]["map"][1]["value"], "WithName[PAN]/Parameter 37")

        # Level dial for Osc B: osc2.level -> Parameter 36
        level_layer = defs["oscNativeLevelLayer"]
        level_b = next((c for c in level_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Level Osc B"), None)
        self.assertIsNotNone(level_b)
        self.assertEqual(level_b["handle remapping"]["map"][0]["value"], "Parameter 36")
        self.assertEqual(level_b["handle remapping"]["map"][1]["value"], "WithName[LEVEL]/Parameter 36")

        # Settings layer for Osc B: maps OscAbc0..17 to Parameter 3030..3047 (osc2.* parameters)
        settings_layer = defs["oscNativeSettingsLayer"]
        settings_b = next((c for c in settings_layer["componentsData"] if c["componentData"]["name"] == "Osc B"), None)
        self.assertIsNotNone(settings_b)
        remap = {m["key"]: m["value"] for m in settings_b["handle remapping"]["map"]}
        for slot in range(18):
            self.assertEqual(remap[f"OscAbc{slot}"], f"Parameter {3030 + slot}")

        # Detail loader: LOAD B bound to Parameter 3499
        detail_layer = defs["oscNativeDetail"]
        load_b = next((c for c in detail_layer["componentsData"] if c["componentData"]["name"] == "LOAD B"), None)
        self.assertIsNotNone(load_b)
        self.assertEqual(load_b["handle remapping"]["map"][0]["value"], "FromGesture/Parameter 3499")

    def test_osc_b_qlinks_mapping(self):
        entries = self.qlinks["Screen Mode Q-Links"]["map"]
        entry = next((e for e in entries if e["Tab"] == 2 and e["SubTab"] == 2), None)
        self.assertIsNotNone(entry, "Osc B entry (Tab 2, SubTab 2) must exist in Q-Links.json")
        self.assertEqual(entry["Bank Direction"], "Row")
        ql = entry["Q-Links"]
        self.assertEqual(len(ql), 16)
        # Verify proxy range 2703..2718
        for i in range(1, 17):
            self.assertEqual(ql[f"Q-Link {i}"], 2702 + i)

    def test_rejection_rules(self):
        # 1. No rejected OSC sidebar layout: tab must not have separate sidebar components outside standard bands
        tab = next((t for t in self.tui["pageData"]["tabs"] if t["tabName"] == "Osc B"), None)
        names = [c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]]
        self.assertNotIn("Osc Sidebar", names)
        self.assertNotIn("Osc Side Controls", names)

        # 2. No visible Q-Link frames
        self.assertEqual(tab["qlinkBoundsData"], [])

    def test_verify_osc_b_screen_helper(self):
        sys.path.insert(0, str(SCRIPTS / "mpc-skin-extras"))
        import importlib
        mod = importlib.import_module("99_ui_screen_osc_b")
        ctx = MockContext(self.tui, self.params_doc, self.meta_doc)

        ok, msg = mod.verify_osc_b_screen(self.tui, self.qlinks, ctx)
        self.assertTrue(ok, f"Verification helper failed on clean skin: {msg}")

        # Test failure on corrupted qlinkBoundsData
        corrupted_tui = copy.deepcopy(self.tui)
        corrupted_tab = next(t for t in corrupted_tui["pageData"]["tabs"] if t["tabName"] == "Osc B")
        corrupted_tab["qlinkBoundsData"] = ["0 0 100 100"]
        ok, msg = mod.verify_osc_b_screen(corrupted_tui, self.qlinks, ctx)
        self.assertFalse(ok)
        self.assertIn("visible Q-Link frames", msg)


    def test_preview_generation(self):
        import render_ui_screen_osc_b_preview as r_mod
        from PIL import Image

        with tempfile.TemporaryDirectory() as p_tmp:
            p_tmp = Path(p_tmp)
            full_path = p_tmp / "osc_b_full.png"
            audit_path = p_tmp / "osc_b_audit.png"

            r_mod.render_full_screen_preview(output_path=full_path)
            r_mod.render_audit_sheet(output_path=audit_path)

            self.assertTrue(full_path.is_file())
            self.assertTrue(audit_path.is_file())

            with Image.open(full_path) as im_f:
                self.assertEqual(im_f.size, (1280, 800))
            with Image.open(audit_path) as im_a:
                self.assertEqual(im_a.size, (1280, 960))


if __name__ == "__main__":
    unittest.main()
