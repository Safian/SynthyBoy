# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

from components.native.button.button import filter_power_param_key, sanitize_tab_navigation


def _load_filter_power_extra():
    path = Path(__file__).resolve().parent.parent / "mpc-skin-extras" / "52_filter_power.py"
    spec = importlib.util.spec_from_file_location("mpc_skin_extra_52_filter_power", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestNativeFilterPower(unittest.TestCase):
    def test_param_keys(self):
        self.assertEqual(filter_power_param_key(1), "filter1.on")
        self.assertEqual(filter_power_param_key(2), "filter2.on")

    def test_sanitize_tab_navigation(self):
        tab = {
            "tabName": "Filter 1",
            "additionalInvalidatingHandles": [
                "IndexedEnabling/1/2/Parameter 100",
                "ValidHandle",
            ],
            "componentDefinition": {
                "additionalInvalidatingHandles": [
                    "IndexedEnabling/1/2/Parameter 200",
                    "KeepMe",
                ]
            },
        }
        sanitize_tab_navigation(tab, {1: 100, 2: 200})
        self.assertEqual(tab["additionalInvalidatingHandles"], ["ValidHandle"])
        self.assertEqual(tab["componentDefinition"]["additionalInvalidatingHandles"], ["KeepMe"])

    def test_skin_extra_standalone_tabs(self):
        extra = _load_filter_power_extra()
        tui = {
            "pageData": {
                "tabs": [
                    {"tabName": "Filter 1", "componentDefinition": {"componentsData": []}},
                    {"tabName": "Filter 2", "componentDefinition": {"componentsData": []}},
                ]
            }
        }
        class MockCtx:
            meta = {"variant": "overlay"}
            param_count = 100
            def param_index(self, key):
                return 10 if key == "filter1.on" else 20 if key == "filter2.on" else -1

        extra.apply(tui, {}, MockCtx())
        tab1_comps = tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]
        tab2_comps = tui["pageData"]["tabs"][1]["componentDefinition"]["componentsData"]

        self.assertEqual(tab1_comps, [])
        self.assertEqual(tab2_comps, [])

    def test_skin_extra_unified_filter_tab(self):
        extra = _load_filter_power_extra()
        tui = {
            "pageData": {
                "tabs": [
                    {"tabName": "Filter", "componentDefinition": {"componentsData": []}},
                ]
            }
        }
        class MockCtx:
            meta = {"variant": "overlay"}
            param_count = 100
            def param_index(self, key):
                return 10 if key == "filter1.on" else 20 if key == "filter2.on" else -1

        extra.apply(tui, {}, MockCtx())
        comps = tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]
        self.assertEqual(comps, [], "the unified Filter gets only the late state-gated widgets")

    def test_skin_extra_non_overlay_ignored(self):
        extra = _load_filter_power_extra()
        tui = {
            "pageData": {
                "tabs": [
                    {"tabName": "Filter 1", "componentDefinition": {"componentsData": []}},
                ]
            }
        }
        class MockCtx:
            meta = {"variant": "production"}
            def param_index(self, key):
                return 10

        extra.apply(tui, {}, MockCtx())
        self.assertEqual(len(tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]), 0)

    def test_button_exports_and_filter_isolation(self):
        import components.native.button.button as button_mod

        filter_symbols = [
            "filter_power_param_key",
            "sanitize_tab_navigation",
        ]
        for sym in filter_symbols:
            self.assertTrue(hasattr(button_mod, sym), f"button_mod missing {sym}")
            self.assertIn(sym, button_mod.__all__)

        button_symbols = [
            "FONT_FILE",
            "semibold",
            "action_art",
            "in_slot",
            "entry",
            "button",
            "action_button",
            "icon_button",
        ]
        for sym in button_symbols:
            self.assertTrue(hasattr(button_mod, sym), f"button_mod missing {sym}")
            self.assertIn(sym, button_mod.__all__)

        # Explicit Filter 1 vs Filter 2 parameter key and building isolation
        self.assertEqual(button_mod.filter_power_param_key(1), "filter1.on")
        self.assertEqual(button_mod.filter_power_param_key(2), "filter2.on")
        self.assertNotEqual(button_mod.filter_power_param_key(1), button_mod.filter_power_param_key(2))



if __name__ == "__main__":
    unittest.main()
