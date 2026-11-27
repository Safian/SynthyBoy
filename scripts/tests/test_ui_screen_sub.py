# SynthyBoy - tests of the Sub screen host-native component integration.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
OVERLAY_LAYOUT = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
SHARED_LAYOUT = ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"
EXTRAS_DIR = ROOT / "scripts" / "mpc-skin-extras"
OFFICIAL_SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")

sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "tests"))
from test_mpc_skin_extras import generate_skins, run_apply, REAL_EXTRAS


class TestSubScreenNativeIntegration(unittest.TestCase):
    """Targeted verification for the Sub oscillator screen native components."""

    def test_sub_metadata_and_layout_contract(self):
        """Verifies Sub tab declaration in the overlay skin and layout definitions."""
        overlay = json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        sub_page = next((p for p in overlay["pages"] if p["name"] == "Sub"), None)
        self.assertIsNotNone(sub_page, "Sub page must be present in mpc-ui-pages-overlay.json")
        self.assertEqual(sub_page["fn"], "Osc", "Sub must be grouped under function key 'Osc'")
        self.assertEqual(sub_page["kick"], ["sub_main"], "Sub page must be bound to kick page 'sub_main'")

        shared = json.loads(SHARED_LAYOUT.read_text(encoding="utf-8"))
        sub_view = next((v for v in shared["views"] if v["name"] == "oscsub"), None)
        self.assertIsNotNone(sub_view, "oscsub view must be present in overlay_layout.json")
        self.assertEqual(sub_view["kind"], "oscui")
        self.assertEqual(sub_view["index"], 1, "Sub oscillator instance index must be 1")
        self.assertEqual(sub_view["tab"], "Sub")

    def test_sub_native_settings_contract(self):
        """Verifies that osc-native-settings.json preserves Sub waveform, octave and controls."""
        settings_contract = json.loads((EXTRAS_DIR / "osc-native-settings.json").read_text(encoding="utf-8"))
        sub_variant = next((v for v in settings_contract if v.get("layout") == 1), None)
        self.assertIsNotNone(sub_variant, "Layout 1 must define the Sub oscillator settings")

        fields = {f["key"]: f for f in sub_variant["fields"]}
        self.assertIn("sub.waveform", fields)
        wf = fields["sub.waveform"]
        self.assertEqual(wf["label"], "WAVEFORM")
        self.assertEqual(wf["choices"], ["Sine", "Rounded Rect", "Triangle", "Saw", "Square", "Pulse"])
        self.assertEqual(wf["min"], 0)
        self.assertEqual(wf["max"], 5)

        self.assertIn("sub.octave", fields)
        octave = fields["sub.octave"]
        self.assertEqual(octave["label"], "OCT")
        self.assertEqual(octave["min"], -4)
        self.assertEqual(octave["max"], 4)

        self.assertIn("sub.tune", fields)
        self.assertEqual(fields["sub.tune"]["label"], "CRS")
        self.assertEqual(fields["sub.tune"]["kind"], "dial_bi")

        self.assertIn("sub.phase", fields)
        self.assertEqual(fields["sub.phase"]["label"], "PHASE")
        self.assertEqual(fields["sub.phase"]["kind"], "dial_uni")

        self.assertIn("sub.env1", fields)
        self.assertEqual(fields["sub.env1"]["label"], "ENV 1")
        self.assertEqual(fields["sub.env1"]["kind"], "toggle")

        self.assertIn("sub.mono", fields)
        self.assertEqual(fields["sub.mono"]["label"], "MONO")
        self.assertEqual(fields["sub.mono"]["kind"], "toggle")

        self.assertIn("sub.phase_contiguous", fields)
        self.assertEqual(fields["sub.phase_contiguous"]["label"], "FREE RUN")
        self.assertEqual(fields["sub.phase_contiguous"]["kind"], "toggle")

    def test_hook_discovery_and_autoload_order(self):
        """Verifies that 99_ui_screen_sub.py is discovered and runs in the autoload chain."""
        sub_hook = EXTRAS_DIR / "99_ui_screen_sub.py"
        self.assertTrue(sub_hook.is_file(), "scripts/mpc-skin-extras/99_ui_screen_sub.py must exist")

        all_modules = sorted(p.name for p in EXTRAS_DIR.glob("*.py"))
        self.assertIn("99_ui_screen_sub.py", all_modules)
        # Verify execution order: runs after 98_osc_surface.py and 99_backgrounds.py
        self.assertLess(all_modules.index("98_osc_surface.py"), all_modules.index("99_ui_screen_sub.py"))
        self.assertLess(all_modules.index("99_backgrounds.py"), all_modules.index("99_ui_screen_sub.py"))

    def test_sub_full_skin_generation_with_official_sdk(self):
        """Runs the skin generation pipeline using the official SDK and verifies Sub components."""
        self.assertTrue(OFFICIAL_SDK.is_dir(), f"Official SDK must exist at {OFFICIAL_SDK}")
        old_sdk = os.environ.get("MPC_SDK")
        try:
            os.environ["MPC_SDK"] = str(OFFICIAL_SDK)
            with tempfile.TemporaryDirectory() as tmp:
                tmp_path = Path(tmp)
                skins = generate_skins(tmp_path)
                run_apply(skins, REAL_EXTRAS)

                tui = json.loads((skins / "TUI.json").read_text(encoding="utf-8"))
                qlinks = json.loads((skins / "Q-Links.json").read_text(encoding="utf-8"))
                defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

                sub_tabs = [t for t in tui["pageData"]["tabs"] if t["tabName"] == "Sub"]
                self.assertEqual(len(sub_tabs), 1, "Exactly one Sub tab must exist in TUI.json")
                sub_tab = sub_tabs[0]

                # Verify that no visible Q-Link numbers or badge highlights exist
                self.assertEqual(sub_tab.get("qlinkBoundsData"), [], "qlinkBoundsData must be empty (no host Q-Link number badges)")

                # Verify Q-Links mapping
                ql_entries = [m for m in qlinks["Screen Mode Q-Links"]["map"] if m.get("Tab") == sub_tab["fnKeyIndex"] + 1 and m.get("SubTab") == 1]
                self.assertEqual(len(ql_entries), 1)
                ql_map = ql_entries[0]["Q-Links"]
                self.assertTrue(all(ql_map[f"Q-Link {i}"] > 0 for i in range(1, 17)), "All 16 Q-Links must be mapped to router proxy parameters")

                # Verify top-level components on Sub
                components = sub_tab["componentDefinition"]["componentsData"]
                comp_names = {c["componentData"]["name"] for c in components}
                self.assertIn("Osc Native Surface", comp_names)
                self.assertIn("Native Modulation Editor", comp_names)
                self.assertIn("Overlay Marker oscsub left", comp_names)
                self.assertIn("Overlay Marker oscsub right", comp_names)
                self.assertIn("Overlay Instance oscsub", comp_names)

                # Verify oscNativeSurface children
                surface = defs["oscNativeSurface"]
                surface_names = {c["componentData"]["name"] for c in surface["componentsData"]}
                self.assertNotIn("Osc Native Source Tabs", surface_names)
                self.assertIn("Osc Native Power", surface_names)
                self.assertIn("Osc Native Routing", surface_names)
                self.assertIn("Osc Native Settings", surface_names)
                self.assertIn("Osc Native Detail", surface_names)

                # Verify background image
                bg = sub_tab["componentDefinition"]["backgroundData"]
                self.assertEqual(bg["focussed"]["image"], "")
                self.assertEqual(bg["unfocussed"]["image"], "")
        finally:
            if old_sdk is not None:
                os.environ["MPC_SDK"] = old_sdk
            else:
                os.environ.pop("MPC_SDK", None)

    def test_sub_preview_image_exists_and_valid(self):
        """Verifies the existence and dimensions of the verified Sub screen preview image."""
        preview_path = (Path(__file__).resolve().parents[2] / 'docs/screens/sub.jpg')
        self.assertTrue(preview_path.is_file(), "docs/screens/sub.jpg must exist")

        from PIL import Image
        with Image.open(preview_path) as im:
            self.assertEqual(im.size, (1280, 800), "Full-screen preview image must be exactly 1280x800")


if __name__ == "__main__":
    unittest.main()
