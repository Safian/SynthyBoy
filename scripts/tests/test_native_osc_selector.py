# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from PIL import Image

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
    STANDALONE_OSC_SELECTOR_RECT,
    build_standalone_osc_selector,
    build_standalone_osc_selector_definition,
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
    def setUp(self):
        self.template = {
            "componentData": {"type": "sliderWithFocusMenu", "name": "Template Selector"},
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
                "bounds": "0 0 200 56",
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

    def test_constants_and_formatting(self):
        self.assertEqual(len(OSC_INSTANCE_NAMES), 5)
        self.assertEqual(OSC_INSTANCE_NAMES, ("Sub", "Osc A", "Osc B", "Osc C", "Noise"))
        self.assertEqual(OSC_POWER_PARAM_KEYS, ("sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on"))
        self.assertEqual(OSC_TAB_NAMES, ("Sub", "Osc A", "Osc B", "Osc C", "Noise"))
        self.assertEqual(STANDALONE_OSC_SELECTOR_RECT, (8, 33, 200, 56))

        self.assertEqual(format_osc_choice_label("Osc A", True), "Osc A (Active)")
        self.assertEqual(format_osc_choice_label("Osc A", False), "Osc A (Inactive)")
        self.assertEqual(format_osc_choice_label("Sub", False), "Sub (Inactive)")

    def test_build_standalone_osc_selector_definition(self):
        source = {
            "actions": [
                {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
                {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "old"},
            ]
        }
        source["componentsData"] = [
            {"componentData": {"type": "Focus"}, "bounds": {"bounds": "0 0 278 107"}},
            {"componentData": {"type": "Slider"}, "bounds": {"bounds": "15 11 248 77"}},
        ]
        original = copy.deepcopy(source)
        res = build_standalone_osc_selector_definition(source, overlay_name="menu overlay")
        self.assertEqual(source, original)
        self.assertEqual([child["componentData"]["type"] for child in res["componentsData"]], ["Focus", "Label"])
        self.assertEqual(res["componentsData"][-1]["componentData"]["data"]["handleName"], "Data")
        self.assertEqual(len(res["actions"]), 2)
        show_actions = [a for a in res["actions"] if a["handler"] == "Show Overlay"]
        self.assertEqual(len(show_actions), 2)
        triggers = {a["onAction"] for a in show_actions}
        self.assertEqual(triggers, {"Mouse Down", "Enter Pressed"})
        for a in show_actions:
            self.assertEqual(a["additionalData"], "menu overlay")
            self.assertEqual(a["handleName"], "Data")

    def test_build_standalone_osc_selector_definition_with_skins_dir(self):
        source = {
            "actions": [
                {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
                {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "old"},
            ]
        }
        source["componentsData"] = [
            {"componentData": {"type": "Focus"}, "bounds": {"bounds": "0 0 200 56", "acceptsHWFocus": "Yes"}},
            {"componentData": {"type": "Slider"}, "bounds": {"bounds": "15 11 200 56"}},
        ]
        with tempfile.TemporaryDirectory() as tmpdir:
            skins_dir = Path(tmpdir)
            res = build_standalone_osc_selector_definition(source, overlay_name=OSC_SOURCE_OVERLAY_NAME, skins_dir=skins_dir)
            frame_file = skins_dir / "osc_instance_selector_frame.png"
            self.assertTrue(frame_file.is_file(), "osc_instance_selector_frame.png must be created in skins_dir")
            with Image.open(frame_file) as im:
                self.assertEqual(im.size, (200, 48), "selector face must be 200x48 px")
            comp_types = [child["componentData"]["type"] for child in res["componentsData"]]
            self.assertEqual(comp_types, ["Focus", "Image", "Label"])
            # Frame decoration
            img_comp = res["componentsData"][1]
            self.assertEqual(img_comp["componentData"]["name"], "Osc selector frame")
            self.assertEqual(img_comp["componentData"]["data"]["image"], "osc_instance_selector_frame.png")
            self.assertEqual(img_comp["bounds"]["bounds"], "0 4 200 48")
            # Value label
            val_comp = res["componentsData"][2]
            self.assertEqual(val_comp["componentData"]["data"]["handleName"], "Data")
            self.assertEqual(val_comp["componentData"]["data"]["textStyle"]["justification"], "horizontallyCentred verticallyCentred")
            # Actions
            show_actions = [a for a in res["actions"] if a["handler"] == "Show Overlay"]
            self.assertEqual(len(show_actions), 2)
            self.assertEqual({a["onAction"] for a in show_actions}, {"Mouse Down", "Enter Pressed"})
            for a in show_actions:
                self.assertEqual(a["additionalData"], OSC_SOURCE_OVERLAY_NAME)
                self.assertEqual(a["handleName"], "Data")

    def test_build_standalone_osc_selector(self):
        s = build_standalone_osc_selector(self.clone, self.template, "Osc A", 10, overlay_param=50)
        self.assertEqual(s["componentData"]["name"], "Osc A Select")
        self.assertEqual(s["parameterIndex"], 10)
        self.assertEqual(s["bounds"]["bounds"], "8 33 200 56")

        # Value mapping updated
        val_map = next(m for m in s["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(val_map["value"], "Parameter 10")

        # Enable stripped
        self.assertFalse(any(m.get("from") == "Enable" or m.get("key") == "Enable" for m in s["handle remapping"]["map"]))

        # Operability bounds
        self.assertEqual(s["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(s["bounds"]["showWhenDataModelInvalid"], "Show")
        self.assertEqual(s["bounds"]["whenVisible"], "Always")

        # Additional invalidating handles: overlay precedence
        handles = s["bounds"]["additionalInvalidatingHandles"]
        self.assertIn("RangedEnabling/0/18/33/Parameter 50", handles)

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
        self.assertIn("oscNativeSelector", defs)
        osc_actions = defs["oscNativeSelector"]["actions"]
        show_actions = [a for a in osc_actions if a.get("handler") == "Show Overlay"]
        self.assertEqual(len(show_actions), 2, "oscNativeSelector must retain Show Overlay for Mouse Down and Enter Pressed")
        self.assertEqual({a["onAction"] for a in show_actions}, {"Mouse Down", "Enter Pressed"})
        for a in show_actions:
            self.assertEqual(a["additionalData"], OSC_SOURCE_OVERLAY_NAME)

        # Check replacementStrings populated
        replacements = {r["key"]: r["value"] for r in tui["pageData"]["replacementStrings"]}
        self.assertEqual(replacements[f"Parameter {source_idx}"], ["Sub", "Osc A", "Osc B", "Osc C", "Noise"])

        children = defs[OSC_SOURCE_OVERLAY_NAME]["componentsData"]
        by_name = {item["componentData"]["name"]: item for item in children}
        self.assertEqual(sum(item["componentData"]["type"] == "MenuOverlay" for item in children), 1)
        self.assertFalse(any(item["componentData"]["type"] == "Button" for item in children))
        for i, name in enumerate(OSC_INSTANCE_NAMES):
            self.assertEqual(by_name[f"Osc source name {i}"]["componentData"]["data"]["text"], name)
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
            select = next(c for c in items if c["componentData"]["name"] == "Osc Native Instance Select")
            self.assertEqual(select["componentData"]["type"], "oscNativeSelector")
            self.assertEqual(select["bounds"]["bounds"], "8 33 200 56")
            self.assertEqual(select["bounds"]["showWhenDataModelInvalid"], "Hide")
            self.assertEqual(select["bounds"]["additionalInvalidatingHandles"], [f"NonZero/Parameter {visibility_idx}"])
            self.assertEqual(select["bounds"]["whenVisible"], "Always")
            self.assertEqual(select["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {source_idx}"}])

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
