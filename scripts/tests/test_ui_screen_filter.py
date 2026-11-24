# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the standalone Filter screen and native components."""

import json
import os
from pathlib import Path
import unittest

import test_mpc_skin_extras as base
import test_mpc_skin_no_fallback as nfb

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
EXTRAS_DIR = REPO_ROOT / "scripts" / "mpc-skin-extras"


class TestFilterScreenNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests
        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        cls.tui = NoFallbackTests.tui
        cls.tab = next((t for t in cls.tui["pageData"]["tabs"] if t["tabName"] == "Filter"), None)
        assert cls.tab is not None, "Filter tab not found in TUI"
        cls.items = cls.tab["componentDefinition"]["componentsData"]
        cls.defs = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_hook_discovery_and_execution_order(self):
        """Verify that skin extras hooks are discovered in correct alphabetical order without duplication."""
        hook_files = sorted(p.name for p in EXTRAS_DIR.glob("*.py") if not p.name.startswith("__"))
        self.assertIn("50_filter_overlay.py", hook_files)
        self.assertIn("52_filter_power.py", hook_files)
        self.assertIn("98_filter_native.py", hook_files)
        self.assertIn("99_ui_screen_filter.py", hook_files)

        idx_50 = hook_files.index("50_filter_overlay.py")
        idx_52 = hook_files.index("52_filter_power.py")
        idx_98 = hook_files.index("98_filter_native.py")
        idx_99 = hook_files.index("99_ui_screen_filter.py")

        self.assertLess(idx_50, idx_52)
        self.assertLess(idx_52, idx_98)
        self.assertLess(idx_98, idx_99)

        # Verify no duplicate filter hooks exist
        filter_hooks = [f for f in hook_files if "filter" in f]
        expected_filter_hooks = ["50_filter_overlay.py", "52_filter_power.py", "98_filter_native.py", "99_ui_screen_filter.py"]
        self.assertEqual(filter_hooks, expected_filter_hooks)

    def test_single_filter_tab_and_surface_definition(self):
        """Verify ONE unified Filter tab exists and contains the shared editor surface."""
        tabs = [t["tabName"] for t in self.tui["pageData"]["tabs"]]
        self.assertIn("Filter", tabs)
        self.assertNotIn("Filter 1", tabs)
        self.assertNotIn("Filter 2", tabs)

        self.assertIn("filterNativeEditorSurface", self.defs)
        surface = next((c for c in self.items if c["componentData"]["type"] == "filterNativeEditorSurface"), None)
        self.assertIsNotNone(surface)
        self.assertEqual(surface["bounds"]["acceptsHWFocus"], "Children")

    def test_f1_f2_and_curve_state_controls_present(self):
        """Verify 4-state selector (F1, F2, NOTE, VELO) and dual toggle exist."""
        by_name = {c["componentData"]["name"]: c for c in self.items}
        for state_label in ("F1", "F2", "NOTE", "VELO"):
            name = f"Filter Native {state_label}"
            self.assertIn(name, by_name)
            self.assertEqual(by_name[name]["componentData"]["type"], "Button")

        self.assertIn("Filter Native dual", by_name)

    def test_mixer_peek_filter_integrity(self):
        """Verify Mixer tab includes the shared Filter PEEK surface."""
        mixer = next((t for t in self.tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer")), None)
        self.assertIsNotNone(mixer)
        peek = next((c for c in mixer["componentDefinition"]["componentsData"]
                     if c["componentData"]["name"] == "Mixer Native Peek Filter"), None)
        self.assertIsNotNone(peek)
        self.assertEqual(peek["componentData"]["type"], "mixerPeekFilterEditorSurface")
        self.assertEqual(peek["bounds"]["acceptsHWFocus"], "Children")
        self.assertEqual(peek["bounds"]["bounds"], "0 0 1280 564")

    def test_no_visible_qlink_numbers_or_lanes(self):
        """Verify owner rule: no visible Q-Link numbers, badges, or dedicated number lanes."""
        surface_controls = self.defs["filterNativeEditorSurface"]["componentsData"]
        for c in self.items + surface_controls:
            name = c["componentData"]["name"]
            self.assertFalse(name.startswith("QBadge"), f"Visible Q-Link badge forbidden: {name}")
            self.assertFalse("qlink_lane" in name.lower(), f"Dedicated Q-Link number lane forbidden: {name}")

    def test_sdk_model_and_style_choice_menus(self):
        """Verify native MODEL and STYLE steppers open SDK MenuOverlay."""
        self.assertIn("filterNativeModel", self.defs)
        self.assertIn("filterNativeStyle", self.defs)
        model_def = self.defs["filterNativeModel"]
        handlers = [a.get("handler") for a in model_def.get("actions", [])]
        self.assertIn("Show Overlay", handlers)


if __name__ == "__main__":
    unittest.main()
