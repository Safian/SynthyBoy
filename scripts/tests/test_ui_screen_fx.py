#!/usr/bin/env python3
# SynthyBoy - targeted test suite for the FX screen (scripts/mpc-skin-extras/99_ui_screen_fx.py & 20_fx.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests of the native FX screen components, contract, hook order, and SDK compliance."""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
EXTRAS_DIR = REPO_ROOT / "scripts" / "mpc-skin-extras"
OVERLAY_LAYOUT = REPO_ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
DEFAULT_SDK = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")

sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import test_mpc_skin_extras as base
from native_ui_params import (
    FX_CELL_COUNT, FX_DOMAIN_COUNT, FX_DOMAIN_OFFSET,
    FX_BAND_BASE, FX_BAND_MODE, FX_HEADER, FX_HEADER_ITEMS,
    FX_KIND_OFFSET, FX_LAYOUT_OFFSET, FX_VALUE_OFFSET,
    FX_CHAIN, native_base_index,
)


class FxHookOrderAndDiscoveryTest(unittest.TestCase):
    """Test hook discovery and ordering in scripts/mpc-skin-extras/."""

    def test_hook_files_exist_and_order_is_correct(self):
        hook_20 = EXTRAS_DIR / "20_fx.py"
        hook_99 = EXTRAS_DIR / "99_ui_screen_fx.py"
        self.assertTrue(hook_20.is_file(), "20_fx.py must exist")
        self.assertTrue(hook_99.is_file(), "99_ui_screen_fx.py must exist")

        all_hooks = sorted(p.name for p in EXTRAS_DIR.glob("*.py"))
        idx_20 = all_hooks.index("20_fx.py")
        idx_99 = all_hooks.index("99_ui_screen_fx.py")
        self.assertLess(idx_20, idx_99, "20_fx.py must run before 99_ui_screen_fx.py in alphabetical sort")

    def test_99_ui_screen_fx_has_callable_apply(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location("hook_99", EXTRAS_DIR / "99_ui_screen_fx.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        self.assertTrue(callable(getattr(mod, "apply", None)), "99_ui_screen_fx.py must define apply(tui, qlinks, ctx)")


class FxNativeContractAndSdkTest(unittest.TestCase):
    """Verify FX screen components using the actual SDK."""

    @classmethod
    def setUpClass(cls):
        sdk_path = Path(os.environ.get("MPC_SDK", str(DEFAULT_SDK)))
        if not (sdk_path / "tools" / "gen_ui.py").is_file():
            raise unittest.SkipTest(f"MPC SDK not found at {sdk_path}")

        cls.sdk_path = sdk_path
        cls._tmp = tempfile.TemporaryDirectory()
        cls.skins = base.generate_skins(Path(cls._tmp.name), OVERLAY_LAYOUT)
        cls.code, cls.err = base.run_apply(cls.skins, EXTRAS_DIR)
        if cls.code != 0:
            raise AssertionError(f"apply-mpc-skin-extras.py failed: {cls.err}")

        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))
        cls.manifest = json.loads((EXTRAS_DIR / "fx-native-contract.json").read_text(encoding="utf-8"))
        cls.definitions = {
            d["key"]: d["value"]
            for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        }

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_apply_succeeded_without_duplicate_errors(self):
        self.assertEqual(self.code, 0, f"Skin extra execution failed: {self.err}")

    def test_fx_tab_exists_and_contains_native_surface(self):
        tabs = self.tui["pageData"]["tabs"]
        fx_tab = next((t for t in tabs if t.get("tabName") == "FX"), None)
        self.assertIsNotNone(fx_tab, "FX tab must exist in generated TUI")

        components = fx_tab["componentDefinition"]["componentsData"]
        surface = next((c for c in components if c["componentData"]["name"] == "FX Native Surface"), None)
        self.assertIsNotNone(surface, "FX Native Surface must be present on FX tab")
        self.assertEqual(surface["componentData"]["type"], "fxNativeEditorSurface")
        self.assertEqual(surface["bounds"]["bounds"], "0 0 1280 628")
        self.assertEqual(surface["bounds"]["acceptsHWFocus"], "Children")

    def test_all_native_cell_definitions_present(self):
        expected_cells = (
            "fxNativeCellUni",
            "fxNativeCellBi",
            "fxNativeCellSwitch",
            "fxNativeCellChoice",
            "fxNativeCellNumber",
            "fxNativeCellReadOnly",
        )
        for name in expected_cells:
            with self.subTest(cell=name):
                self.assertIn(name, self.definitions, f"{name} definition must be registered")

    def test_all_native_popups_present_with_markers(self):
        expected_popups = (
            ("fx editor choice menu", "MenuOverlay"),
            ("fx editor number menu", "NumericOverlay"),
            ("fx editor rack menu", "MenuOverlay"),
            ("fx chain add menu", "MenuOverlay"),
        )
        for name, kind in expected_popups:
            with self.subTest(popup=name):
                self.assertIn(name, self.definitions, f"{name} popup definition must be registered")
                popup = self.definitions[name]
                children = popup.get("componentsData", [])
                overlay_types = [c["componentData"]["type"] for c in children if c["componentData"]["type"] == kind]
                self.assertEqual(len(overlay_types), 1, f"{name} must contain exactly one {kind}")
                marker_images = [
                    c for c in children
                    if c["componentData"]["type"] == "Image" and "Marker" in c["componentData"]["name"]
                ]
                self.assertGreaterEqual(len(marker_images), 1, f"{name} must contain modal marker image(s)")
                # Check modal marker bounds (packed 30x6 at x=16 y=120, or individual 6x6)
                has_valid_marker_bounds = any(
                    c["bounds"]["bounds"] in ("16 120 30 6", "16 120 6 6") for c in marker_images
                )
                self.assertTrue(has_valid_marker_bounds, f"{name} must have modal marker at y=120")

    def test_native_header_definition_and_controls(self):
        self.assertIn("fxNativeHeader", self.definitions)
        header_def = self.definitions["fxNativeHeader"]
        children = header_def["componentsData"]

        names = [c["componentData"]["name"] for c in children]
        self.assertIn("power", names)
        self.assertTrue(any("rack" in n for n in names))
        self.assertTrue(any("remove" in n for n in names))
        self.assertTrue(any("Band LOW" in n for n in names))
        self.assertTrue(any("Band MID" in n for n in names))
        self.assertTrue(any("Band HIGH" in n for n in names))

    def test_native_chain_card_and_gallery(self):
        self.assertIn("fxNativeChainCard", self.definitions)
        self.assertIn("fxNativeChainRow", self.definitions)
        self.assertIn("fxNativeGalleryCard", self.definitions)
        self.assertIn("fxNativeGalleryRow1", self.definitions)
        self.assertIn("fxNativeGalleryRow2", self.definitions)

    def test_no_visible_qlink_numbers_or_lanes(self):
        """Per owner 2026-11-24 rule: no visible Q-Link numbers, chips, or lanes on native controls."""
        surface = self.definitions["fxNativeEditorSurface"]
        for comp in surface.get("componentsData", []):
            name = comp["componentData"]["name"]
            # Ensure no Q1-Q16 badge or Q-Link number label is drawn as a visible text child
            c_data = comp["componentData"].get("data", {})
            text = c_data.get("text", "")
            for q_num in range(1, 17):
                self.assertNotEqual(text, f"Q{q_num}", f"Control {name} has visible Q-Link chip text {text}")
                self.assertNotEqual(text, f"Q-Link {q_num}", f"Control {name} has visible Q-Link chip text {text}")

    def test_all_contract_widgets_present_in_surface(self):
        surface = self.definitions["fxNativeEditorSurface"]
        cell_names = {c["componentData"]["name"] for c in surface.get("componentsData", [])}
        for layout, slot, kind in self.manifest["widgets"]:
            expected_name = f"FX Cell {layout}.{slot + 1}.{kind}"
            self.assertIn(expected_name, cell_names, f"{expected_name} must be in fxNativeEditorSurface")


if __name__ == "__main__":
    unittest.main()
