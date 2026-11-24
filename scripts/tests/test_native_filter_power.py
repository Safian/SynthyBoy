# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import copy
import importlib.util
from pathlib import Path
import unittest

from components.native.button.button import build_filter_power, filter_power_param_key, sanitize_tab_navigation


def _load_filter_power_extra():
    path = Path(__file__).resolve().parent.parent / "mpc-skin-extras" / "52_filter_power.py"
    spec = importlib.util.spec_from_file_location("mpc_skin_extra_52_filter_power", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestNativeFilterPower(unittest.TestCase):
    def setUp(self):
        self.template = {
            "componentData": {"type": "toggleIcon", "name": "Template Power"},
            "parameterIndex": 0,
            "handle remapping": {
                "map": [
                    {"key": "Value", "value": "Parameter 0"},
                    {"from": "Enable", "value": "Parameter 0"},
                ]
            },
            "bounds": {
                "acceptsHWFocus": "Yes",
                "showWhenDataModelInvalid": "Hide",
                "whenVisible": "WhenValid",
                "bounds": "0 0 56 56",
                "additionalInvalidatingHandles": [
                    "IndexedEnabling/1/2/Parameter 42",
                    "SomeOtherHandle",
                ],
            },
        }

    def clone(self, template, name, parameter, rect):
        c = copy.deepcopy(template)
        c["componentData"]["name"] = name
        c["parameterIndex"] = parameter
        c["bounds"]["bounds"] = "%d %d %d %d" % rect
        return c

    def test_param_keys(self):
        self.assertEqual(filter_power_param_key(1), "filter1.on")
        self.assertEqual(filter_power_param_key(2), "filter2.on")

    def test_build_filter_power_per_instance(self):
        p1 = build_filter_power(self.clone, self.template, 1, 101, (10, 20, 56, 56))
        p2 = build_filter_power(self.clone, self.template, 2, 202, (80, 20, 56, 56))

        self.assertEqual(p1["componentData"]["name"], "Filter 1 Power")
        self.assertEqual(p1["parameterIndex"], 101)
        self.assertEqual(p2["componentData"]["name"], "Filter 2 Power")
        self.assertEqual(p2["parameterIndex"], 202)

        # Mapping Value updated to respective parameter
        val_map1 = next(m for m in p1["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(val_map1["value"], "Parameter 101")
        val_map2 = next(m for m in p2["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(val_map2["value"], "Parameter 202")

    def test_build_filter_power_with_data_handle_remapping(self):
        data_template = {
            "componentData": {"type": "toggleIcon", "name": "Template Power"},
            "handle remapping": {
                "map": [
                    {"from": "Data", "value": "Parameter 11"},
                    {"from": "Enable", "value": "Parameter 11"},
                ]
            },
            "bounds": {"bounds": "0 0 56 56", "additionalInvalidatingHandles": []},
        }
        p = build_filter_power(self.clone, data_template, 1, 87, (12, 16, 56, 56))
        val_map = next(m for m in p["handle remapping"]["map"] if m.get("from") == "Data" or m.get("key") == "Data")
        self.assertEqual(val_map["value"], "Parameter 87")
        self.assertFalse(any(m.get("from") == "Enable" for m in p["handle remapping"]["map"]))

    def test_build_filter_power_missing_handle_remapping(self):
        empty_template = {
            "componentData": {"type": "toggleIcon", "name": "Template Power"},
            "handle remapping": {"map": []},
            "bounds": {"bounds": "0 0 56 56", "additionalInvalidatingHandles": []},
        }
        p = build_filter_power(self.clone, empty_template, 2, 102, (12, 16, 56, 56))
        val_map = next(m for m in p["handle remapping"]["map"] if m.get("key") == "Data")
        self.assertEqual(val_map["value"], "Parameter 102")

    def test_clone_component_data_and_value_mapping(self):
        extra = _load_filter_power_extra()
        # Template with from: "Data"
        t1 = {
            "componentData": {"name": "Old", "type": "toggleIcon"},
            "handle remapping": {"map": [{"from": "Data", "value": "Parameter 11"}]},
            "bounds": {"bounds": "0 0 10 10"},
        }
        c1 = extra._clone_component(t1, "Filter 1 Power", 87, (12, 16, 56, 56))
        val1 = next(m for m in c1["handle remapping"]["map"] if m.get("from") == "Data")
        self.assertEqual(val1["value"], "Parameter 87")

        # Template with no existing map
        t2 = {
            "componentData": {"name": "Old", "type": "toggleIcon"},
            "bounds": {"bounds": "0 0 10 10"},
        }
        c2 = extra._clone_component(t2, "Filter 2 Power", 102, (12, 16, 56, 56))
        val2 = next(m for m in c2["handle remapping"]["map"] if m.get("key") == "Data")
        self.assertEqual(val2["value"], "Parameter 102")

    def test_build_filter_power_self_disabling_stripped(self):
        # Template had invalidating handle for 42; pass parameter 42
        p = build_filter_power(self.clone, self.template, 1, 42, (0, 0, 56, 56))

        # 'Enable' mapping must be stripped
        maps = p["handle remapping"]["map"]
        self.assertFalse(any(m.get("from") == "Enable" or m.get("key") == "Enable" for m in maps))

        # Self-disabling IndexedEnabling handle must be stripped
        handles = p["bounds"]["additionalInvalidatingHandles"]
        self.assertNotIn("IndexedEnabling/1/2/Parameter 42", handles)
        self.assertIn("SomeOtherHandle", handles)

    def test_build_filter_power_operability_bounds(self):
        p = build_filter_power(self.clone, self.template, 1, 100, (0, 0, 56, 56))
        self.assertEqual(p["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(p["bounds"]["showWhenDataModelInvalid"], "Show")
        self.assertEqual(p["bounds"]["whenVisible"], "Always")

    def test_build_filter_power_overlay_precedence(self):
        p = build_filter_power(self.clone, self.template, 1, 100, (0, 0, 56, 56), overlay_param=500, last_visible_frame=18)
        handles = p["bounds"]["additionalInvalidatingHandles"]
        self.assertIn("RangedEnabling/0/18/33/Parameter 500", handles)

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

        self.assertEqual(len(tab1_comps), 1)
        self.assertEqual(tab1_comps[0]["componentData"]["name"], "Filter 1 Power")
        self.assertEqual(tab1_comps[0]["bounds"]["bounds"], "12 16 56 56")
        val1 = next(m for m in tab1_comps[0]["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(val1["value"], "Parameter 10")

        self.assertEqual(len(tab2_comps), 1)
        self.assertEqual(tab2_comps[0]["componentData"]["name"], "Filter 2 Power")
        self.assertEqual(tab2_comps[0]["bounds"]["bounds"], "12 16 56 56")
        val2 = next(m for m in tab2_comps[0]["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(val2["value"], "Parameter 20")

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
            "build_filter_power",
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

        f1_entry = button_mod.build_filter_power(self.clone, self.template, 1, 10, (0, 0, 56, 56))
        f2_entry = button_mod.build_filter_power(self.clone, self.template, 2, 20, (0, 0, 56, 56))
        self.assertEqual(f1_entry["componentData"]["name"], "Filter 1 Power")
        self.assertEqual(f2_entry["componentData"]["name"], "Filter 2 Power")
        f1_map = next(m["value"] for m in f1_entry["handle remapping"]["map"] if m.get("key") == "Value")
        f2_map = next(m["value"] for m in f2_entry["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(f1_map, "Parameter 10")
        self.assertEqual(f2_map, "Parameter 20")


if __name__ == "__main__":
    unittest.main()
