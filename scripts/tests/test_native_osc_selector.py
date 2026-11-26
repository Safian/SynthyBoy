# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS = ROOT / "scripts"
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from scripts.native_ui_params import OSC_SOURCE, OSC_VISIBILITY, mixer_peek_index
from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_POWER_PARAM_KEYS,
    OSC_SOURCE_OVERLAY_NAME,
    OSC_TAB_NAMES,
    format_osc_choice_label,
    sanitize_osc_tab_navigation,
)


def _load_osc_selector_extra():
    path = Path(__file__).resolve().parent.parent / "mpc-skin-extras" / "54_osc_selector.py"
    spec = importlib.util.spec_from_file_location("mpc_skin_extra_54_osc_selector", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestNativeOscSelector(unittest.TestCase):
    def test_constants_and_formatting(self):
        self.assertEqual(len(OSC_INSTANCE_NAMES), 5)
        self.assertEqual(OSC_INSTANCE_NAMES, ("Sub", "Osc A", "Osc B", "Osc C", "Noise"))
        self.assertEqual(OSC_POWER_PARAM_KEYS, ("sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on"))
        self.assertEqual(OSC_TAB_NAMES, ("Sub", "Osc A", "Osc B", "Osc C", "Noise"))

        self.assertEqual(format_osc_choice_label("Osc A", True), "Osc A (Active)")
        self.assertEqual(format_osc_choice_label("Osc A", False), "Osc A (Inactive)")
        self.assertEqual(format_osc_choice_label("Sub", False), "Sub (Inactive)")

    def test_sanitize_osc_tab_navigation(self):
        tab = {
            "tabName": "Osc A",
            "additionalInvalidatingHandles": [
                "IndexedEnabling/1/2/Parameter 101",
                "KeepThisHandle",
            ],
            "componentDefinition": {
                "additionalInvalidatingHandles": [
                    "IndexedEnabling/1/2/Parameter 102",
                    "KeepComponentHandle",
                ]
            },
        }
        sanitize_osc_tab_navigation(tab, {"sub": 100, "osc1": 101, "osc2": 102})
        self.assertEqual(tab["additionalInvalidatingHandles"], ["KeepThisHandle"])
        self.assertEqual(tab["componentDefinition"]["additionalInvalidatingHandles"], ["KeepComponentHandle"])

    def test_skin_extra_osc_selector(self):
        extra = _load_osc_selector_extra()
        tui = {
            "pageData": {
                "componentDefinitions": {
                    "localComponentDefinitions": [
                        {
                            "key": "sliderWithFocusMenu",
                            "value": {
                                "actions": [
                                    {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
                                    {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"},
                                    {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"},
                                ],
                            },
                        }
                    ]
                },
                "tabs": [
                    {
                        "tabName": "Osc",
                        "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Parameter 101"],
                        "componentDefinition": {
                            "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Parameter 101"],
                            "componentsData": [
                                {
                                    "componentData": {"name": "Existing Menu", "type": "sliderWithFocusMenu"},
                                    "bounds": {"bounds": "0 0 100 50"},
                                    "handle remapping": {"map": []},
                                }
                            ],
                        },
                    },
                    {
                        "tabName": "Sub",
                        "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Parameter 100"],
                        "componentDefinition": {
                            "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Parameter 100"],
                            "componentsData": [],
                        },
                    },
                ],
            }
        }

        class MockCtx:
            meta = {"variant": "overlay"}
            param_count = 100
            _extra = {150: "shell.play_osc"}
            def param_index(self, key):
                mapping = {"sub.on": 100, "osc1.on": 101, "osc2.on": 102, "osc3.on": 103, "noise.on": 104}
                return mapping.get(key, -1)

        extra.apply(tui, {}, MockCtx())

        source_idx = mixer_peek_index(MockCtx.param_count) + OSC_SOURCE
        visibility_idx = mixer_peek_index(MockCtx.param_count) + OSC_VISIBILITY

        # Check definition created with Show Overlay intact
        defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertNotIn("oscNativeSelector", defs)

        # Check replacementStrings populated
        replacements = {r["key"]: r["value"] for r in tui["pageData"]["replacementStrings"]}
        self.assertEqual(replacements[f"Parameter {source_idx}"], ["Sub", "Osc A", "Osc B", "Osc C", "Noise"])

        children = defs[OSC_SOURCE_OVERLAY_NAME]["componentsData"]
        by_name = {item["componentData"]["name"]: item for item in children}
        self.assertEqual(sum(item["componentData"]["type"] == "MenuOverlay" for item in children), 1)
        self.assertEqual(sum(item["componentData"]["type"] == "Button" for item in children), 5)
        for i, name in enumerate(OSC_INSTANCE_NAMES):
            self.assertEqual(by_name[name]["componentData"]["data"]["text"], name)
            hit = by_name["Choose " + name]
            y = 110 + 80 * i
            self.assertEqual(hit["bounds"]["bounds"], f"0 {y} 426 80")
            data = hit["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["handleName"]), (i, 5, "Data"))
            for item in (by_name[name], by_name[f"Osc source power {i} ON"], by_name[f"Osc source power {i} OFF"]):
                left, top, width, height = map(int, item["bounds"]["bounds"].split())
                self.assertEqual((top, height), (y, 80))
                self.assertTrue(0 <= left < left + width <= 426)
                self.assertLess(children.index(item), children.index(hit))
            for on, status in ((0, "OFF"), (1, "ON")):
                badge = by_name[f"Osc source power {i} {status}"]
                direct_power = MockCtx().param_index(OSC_POWER_PARAM_KEYS[i])
                self.assertEqual(badge["bounds"]["additionalInvalidatingHandles"],
                                 [f"IndexedEnabling/{on}/2/Parameter {direct_power}"])
                self.assertEqual(badge["bounds"]["acceptsHWFocus"], "No")
        self.assertEqual([c["bounds"]["bounds"] for c in children[-3:]], ["16 120 6 6", "28 120 6 6", "40 120 6 6"])

        # Check components created on tabs
        for tab in tui["pageData"]["tabs"]:
            items = tab["componentDefinition"]["componentsData"]
            # One tab page = one screen: the hardware sub-tab is the only source selector, no in-screen strip.
            self.assertEqual([c for c in items if c["componentData"]["name"].startswith("Osc Native Source ")], [])

            # Inactive navigation: check that tab power handles were sanitized
            self.assertEqual(tab["additionalInvalidatingHandles"], [])
            self.assertEqual(tab["componentDefinition"]["additionalInvalidatingHandles"], [])

    def test_skin_extra_non_overlay_ignored(self):
        extra = _load_osc_selector_extra()
        tui = {
            "pageData": {
                "tabs": [{"tabName": "Osc", "componentDefinition": {"componentsData": []}}],
                "componentDefinitions": {"localComponentDefinitions": []},
            }
        }
        class MockCtx:
            meta = {"variant": "production"}
        extra.apply(tui, {}, MockCtx())
        self.assertEqual(len(tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]), 0)


if __name__ == "__main__":
    unittest.main()
