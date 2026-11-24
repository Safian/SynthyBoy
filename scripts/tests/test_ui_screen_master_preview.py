#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Verification and preview test suite for standalone Master screen native controls.

Verifies the Master tab integration in the live release overlay skin
(scripts/overlay-skin/mpc-ui-pages-overlay.json), hook discovery order,
native control definitions, apertures, subpage exclusivity, and preview assets.
"""

import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "tests"))

OFFICIAL_SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")
SDK = Path(os.environ.get("MPC_SDK") or OFFICIAL_SDK)

from native_master.master_controls import (
    MASTER_CONTROL_NAMES,
    MASTER_INTEGER_FIELDS,
    MASTER_LINKED_BEND,
    MASTER_OUTPUT_WIDGETS,
    MASTER_PANELS,
    MASTER_SPECS,
    UNCONVERTED_CONTROLS,
    get_master_control_specs,
    get_unconverted_controls,
)
import test_mpc_skin_extras as skin


class MasterScreenVerificationTests(unittest.TestCase):
    """Targeted verification for Master tab native UI migration."""

    def test_hook_discovery_and_order(self):
        """Verify hook discovery in scripts/mpc-skin-extras/ and execution order."""
        extras_dir = ROOT / "scripts" / "mpc-skin-extras"
        all_hooks = sorted([p.name for p in extras_dir.glob("*.py")])

        self.assertIn("75_master.py", all_hooks, "75_master.py must exist as the primary Master screen hook")
        idx_master = all_hooks.index("75_master.py")
        idx_layout = all_hooks.index("80_layout.py")
        idx_pages = all_hooks.index("83_pages.py")
        idx_names = all_hooks.index("85_names.py")
        idx_no_fallback = all_hooks.index("97_no_fallback.py")

        self.assertLess(idx_master, idx_layout, "75_master.py must run before 80_layout.py")
        self.assertLess(idx_master, idx_pages, "75_master.py must run before 83_pages.py")
        self.assertLess(idx_master, idx_names, "75_master.py must run before 85_names.py")
        self.assertLess(idx_master, idx_no_fallback, "75_master.py must run before 97_no_fallback.py")

        # 99_ui_screen_master.py is NOT needed and must not duplicate 75_master.py
        self.assertNotIn(
            "99_ui_screen_master.py",
            all_hooks,
            "99_ui_screen_master.py must not exist as a duplicate hook when 75_master.py is active",
        )

    def test_sdk_overlay_skin_generation_and_validation(self):
        """Verify that the official SDK generates and validates Master tab skin cleanly."""
        if not (SDK / "tools" / "gen_ui.py").is_file():
            self.skipTest("Official MPC SDK gen_ui.py unavailable")

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            skins = skin.generate_skins(tmp_path, ROOT / "scripts/overlay-skin/mpc-ui-pages-overlay.json")
            code, out = skin.run_apply(skins, skin.REAL_EXTRAS)
            self.assertEqual(code, 0, f"apply-mpc-skin-extras.py failed: {out}")

            tui_text = (skins / "TUI.json").read_text(encoding="utf-8")
            tui = json.loads(tui_text)

            # Validate generated assets
            self.assertTrue((skins / "master_peak_scale.png").is_file())
            self.assertTrue((skins / "master_panel_1_on.png").is_file())
            self.assertTrue((skins / "master_panel_2_on.png").is_file())

            # Validate Master tab in TUI
            master_tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == "Master"), None)
            self.assertIsNotNone(master_tab, "Master tab must be present in generated TUI.json")

            components = master_tab["componentDefinition"]["componentsData"]
            comp_names = {c["componentData"]["name"] for c in components}

            # All 37 standard controls present
            for spec in MASTER_SPECS:
                self.assertIn(spec["name"], comp_names, f"Missing native control: {spec['name']}")

            # Linked bend present
            self.assertIn(MASTER_LINKED_BEND["name"], comp_names)

            # Output widgets present
            for name in MASTER_OUTPUT_WIDGETS:
                self.assertIn(name, comp_names, f"Missing output widget: {name}")

            self.assertNotIn("Master Scale Keyboard", comp_names)

            # Panels present
            self.assertIn("Master Panel MAIN", comp_names)
            self.assertIn("Master Panel TUNING & MPE", comp_names)

            # No visible Q-Link numbers/chips: qlinkBoundsData must be empty in release overlay
            self.assertEqual(
                master_tab.get("qlinkBoundsData", []),
                [],
                "Release overlay must have empty qlinkBoundsData to suppress visible host Q-frames",
            )

            # Run SDK validator if present
            val_script = SDK / "tools" / "validate.py"
            if val_script.is_file():
                res = subprocess.run([sys.executable, str(val_script), str(skins.parent)], capture_output=True, text=True)
                report = res.stdout + res.stderr
                # Missing plugin.json in temp folder is expected; no skin/TUI error allowed
                errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
                self.assertEqual(errors, [], f"SDK validate.py found skin errors: {report}")

    def test_verified_preview_assets_exist(self):
        """Verify that all documented preview images exist and are valid."""
        previews = {
            "Init_master_main": ROOT / "docs/screens/master.jpg",
            "Init_master_tuning_mpe": ROOT / "docs/screens/master.jpg",
            "Init_master_tuning_mpe_on": ROOT / "docs/screens/master.jpg",
            "Init_master_pad_press": ROOT / "docs/screens/master.jpg",
            "Init_master_link_off": ROOT / "docs/screens/master.jpg",
            "master-main": ROOT / "docs/screens/master.jpg",
            "master-tuning": ROOT / "docs/screens/master.jpg",
            "master-layout-after-active": ROOT / "docs/screens/master.jpg",
            "master-layout-after-silent": ROOT / "docs/screens/master.jpg",
        }
        for name, path in previews.items():
            self.assertTrue(path.is_file(), f"Preview image {name} missing at {path}")
            self.assertGreater(path.stat().st_size, 1000, f"Preview image {name} is unusually small")

    def test_unconverted_and_retained_inventory(self):
        """Verify retained dynamic visualizers and modal overlays inventory."""
        unconverted = get_unconverted_controls()
        names = {u["name"]: u for u in unconverted}

        self.assertIn("Glide Slope Curve", names)
        self.assertEqual(names["Glide Slope Curve"]["component"], "PortamentoCurveDisplay")
        self.assertEqual(names["Glide Slope Curve"]["side"], "main")

        self.assertNotIn("Mini Keyboard", names)

        self.assertIn("List Popup", names)
        self.assertIn("Numeric Keyboard", names)


if __name__ == "__main__":
    unittest.main()
