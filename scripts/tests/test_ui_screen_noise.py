# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted verification for the standalone Noise screen MPC skin integration."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
EXTRAS_DIR = ROOT / "scripts" / "mpc-skin-extras"
GEN = ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = ROOT / "scripts" / "run-gen-ui.py"
APPLY = ROOT / "scripts" / "apply-mpc-skin-extras.py"
SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")


class TestUiScreenNoise(unittest.TestCase):
    def test_hook_discovery_and_order(self):
        """Verifies that 99_ui_screen_noise.py is discovered and ordered properly."""
        modules = sorted(EXTRAS_DIR.glob("*.py"))
        module_names = [m.name for m in modules]
        self.assertIn("99_ui_screen_noise.py", module_names)

        # Must run after earlier osc and background hooks
        idx_54 = module_names.index("54_osc_selector.py")
        idx_55 = module_names.index("55_osc_routing.py")
        idx_56 = module_names.index("56_osc_power.py")
        idx_57 = module_names.index("57_osc_settings.py")
        idx_58 = module_names.index("58_osc_detail.py")
        idx_98 = module_names.index("98_osc_surface.py")
        idx_bg = module_names.index("99_backgrounds.py")
        idx_noise = module_names.index("99_ui_screen_noise.py")

        self.assertLess(idx_54, idx_noise)
        self.assertLess(idx_55, idx_noise)
        self.assertLess(idx_56, idx_noise)
        self.assertLess(idx_57, idx_noise)
        self.assertLess(idx_58, idx_noise)
        self.assertLess(idx_98, idx_noise)
        self.assertLess(idx_bg, idx_noise)

        # Test apply function exists and is callable
        spec = importlib.util.spec_from_file_location("noise_extra", EXTRAS_DIR / "99_ui_screen_noise.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        self.assertTrue(callable(getattr(mod, "apply", None)))

    def test_noise_screen_pipeline_and_sdk_validation(self):
        """Builds skin through SDK and runs validate.py and structure checks."""
        self.assertTrue((SDK / "tools" / "gen_ui.py").is_file(), f"SDK not found at {SDK}")

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            ui = tmp / "ui"
            skins = tmp / "Plugin Skins"

            # 1. gen-mpc-ui.py
            subprocess.run(
                [sys.executable, str(GEN), "--params", str(PARAMS_JSON), "--pages", str(PAGES_JSON),
                 "--layout", str(LAYOUT_JSON), "--out", str(ui)],
                check=True, capture_output=True,
            )
            # 2. run-gen-ui.py with official SDK
            subprocess.run(
                [sys.executable, str(RUN_GEN_UI), "--sdk", str(SDK), "--params", str(ui / "params-hostsim.json"),
                 "--ui", str(ui / "ui.json"), "--out", str(skins), "--quiet"],
                check=True, capture_output=True,
            )
            # 3. apply-mpc-skin-extras.py (including 99_ui_screen_noise.py)
            subprocess.run(
                [sys.executable, str(APPLY), "--skins", str(skins), "--params", str(PARAMS_JSON),
                 "--meta", str(ui / "skin-meta.json")],
                check=True, capture_output=True,
            )

            # SDK validation check
            val = subprocess.run(
                [sys.executable, "-I", str(SDK / "tools" / "validate.py"), str(skins.parent)],
                capture_output=True, text=True,
            )
            report = val.stdout + val.stderr
            errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
            self.assertEqual(errors, [], f"SDK validation failed: {report}")

            # Structure verification
            tui = json.loads((skins / "TUI.json").read_text(encoding="utf-8"))
            defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

            # Noise tab existence and metadata
            noise_tab = next((t for t in tui["pageData"]["tabs"] if t["tabName"] == "Noise"), None)
            self.assertIsNotNone(noise_tab, "Noise tab missing from TUI.json")
            self.assertEqual(noise_tab.get("fnKeyIndex"), 1)
            self.assertEqual(noise_tab.get("fnKeySubIndex"), 4)

            # Clean Q-Link bounds (no visible box clutter)
            self.assertEqual(noise_tab.get("qlinkBoundsData", []), [])

            # Root background is overlay_bg_osc.png
            root_bg = noise_tab["componentDefinition"]["backgroundData"]["focussed"].get("image")
            self.assertEqual(root_bg, "overlay_bg_osc.png")

            # oscNativeSurface presence
            tab_components = noise_tab["componentDefinition"]["componentsData"]
            surface_entry = next((c for c in tab_components if c["componentData"]["type"] == "oscNativeSurface"), None)
            self.assertIsNotNone(surface_entry, "oscNativeSurface missing from Noise tab")
            self.assertEqual(surface_entry["bounds"]["bounds"], "0 0 1280 628")

            # oscNativeSurface contents
            surface_def = defs.get("oscNativeSurface")
            self.assertIsNotNone(surface_def, "oscNativeSurface definition missing")
            sub_names = [c["componentData"]["name"] for c in surface_def["componentsData"]]
            self.assertEqual(
                sub_names,
                [
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
                ],
            )

            # Verify Noise Power mapping
            power_layer = defs.get("oscNativePowerLayer")
            noise_power = next(c for c in power_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Noise Power")
            self.assertEqual(noise_power["bounds"]["bounds"], "0 0 64 56")
            self.assertEqual(noise_power["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 2976"}])

            # Verify Noise Routing mapping
            routing_layer = defs.get("oscNativeRoutingLayer")
            noise_routing = next(c for c in routing_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Routing Noise")
            self.assertEqual(noise_routing["bounds"]["bounds"], "0 0 150 48")
            self.assertEqual(noise_routing["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 3010"}])

            # Verify Settings Layer has Noise layouts (layout 2 and 3)
            settings_json = json.loads((EXTRAS_DIR / "osc-native-settings.json").read_text(encoding="utf-8"))
            layouts = {item["layout"] for item in settings_json}
            self.assertIn(2, layouts, "Noise generated layout (2) missing from osc-native-settings.json")
            self.assertIn(3, layouts, "Noise sample layout (3) missing from osc-native-settings.json")


if __name__ == "__main__":
    unittest.main()
