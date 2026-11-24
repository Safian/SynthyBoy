# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import copy
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from components.native.level.level import compact_dial_definition, compact_level_definition
from components.native.pan.pan import compact_pan_definition


def template():
    return {
        "backgroundData": {state: {"colour": "original"} for state in ("focussed", "unfocussed")},
        "actions": [{"handler": "Focus Lost"}, {"handler": "Show Overlay"}],
        "componentsData": [
            {"componentData": {"name": name, "data": {"handleName": "Data", "textStyle": {"font": {}}}},
             "bounds": {"bounds": "0 0 1 1"}}
            for name in ("focus", "knob", "name", "value")
        ],
    }


class CompactNativeDialTests(unittest.TestCase):
    def test_layout_polarity_and_actions(self):
        custom = {"focus": (10, 20, 300, 200), "knob": (5, 5, 80, 80), "name": (60, 10, 40, 30)}
        defaults = {"focus": (0, 0, 192, 108), "knob": (2, 9, 90, 90), "name": (94, 33, 98, 40)}
        for options, polarity, bounds in (({}, "UniPolar", defaults),
                                           ({"rects": custom, "polarity": "BiPolar"}, "BiPolar", custom)):
            with self.subTest(polarity=polarity):
                result = compact_dial_definition(template(), **options)
                components = {c["componentData"]["name"]: c for c in result["componentsData"]}
                self.assertCountEqual([c["componentData"]["name"] for c in result["componentsData"]], bounds)
                for name, rect in bounds.items():
                    self.assertEqual(components[name]["bounds"]["bounds"], "%d %d %d %d" % rect)
                knob = components["knob"]["componentData"]["data"]
                self.assertEqual((knob["handleName"], knob["polarity"]), ("Data", polarity))
                self.assertEqual(components["name"]["componentData"]["data"]["handleName"], "Caption")
                self.assertEqual(result["actions"], [{"handler": "Focus Lost"}])

    def test_outputs_and_sdk_template_are_independent(self):
        source = template()
        original = copy.deepcopy(source)
        first = compact_dial_definition(source)
        second = compact_dial_definition(source)
        first["actions"][0]["handler"] = "changed"
        first["backgroundData"]["focussed"]["colour"] = "changed"
        self.assertEqual(source, original)
        self.assertEqual(second["actions"], [{"handler": "Focus Lost"}])
        self.assertEqual(second["backgroundData"]["focussed"]["colour"], "ff060913")

    def test_compact_48_pan_and_level_dial_definition(self):
        compact_48_bounds = {
            "focus": (0, 0, 202, 48),
            "knob": (2, 4, 40, 40),
            "name": (48, 4, 150, 18),
            "value": (48, 22, 150, 22),
        }
        for polarity, label in (("BiPolar", "PAN"), ("UniPolar", "LEVEL")):
            with self.subTest(polarity=polarity, label=label):
                result = compact_dial_definition(template(), height=48, polarity=polarity)
                components = {c["componentData"]["name"]: c for c in result["componentsData"]}
                self.assertCountEqual([c["componentData"]["name"] for c in result["componentsData"]], compact_48_bounds)
                for name, rect in compact_48_bounds.items():
                    self.assertEqual(components[name]["bounds"]["bounds"], "%d %d %d %d" % rect)

                knob = components["knob"]["componentData"]["data"]
                self.assertEqual((knob["handleName"], knob["polarity"]), ("Data", polarity))
                self.assertEqual(knob["knobType"], "GenericLegacy")

                name_ctrl = components["name"]["componentData"]["data"]
                self.assertEqual(name_ctrl["handleName"], "Caption")
                self.assertEqual(name_ctrl["textStyle"]["font"]["height"], 14)
                self.assertEqual(name_ctrl["textStyle"]["colour"], "ffe6e6e6")

                val_ctrl = components["value"]["componentData"]["data"]
                self.assertEqual(val_ctrl["handleName"], "Data")
                self.assertEqual(val_ctrl["textStyle"]["font"]["height"], 16)
                self.assertEqual(val_ctrl["textStyle"]["colour"], "ffeef1ff")

                self.assertEqual(result["actions"], [{"handler": "Focus Lost"}])
                self.assertEqual(result["backgroundData"]["focussed"]["colour"], "ff060913")
                self.assertEqual(result["backgroundData"]["unfocussed"]["colour"], "ff060913")

    def test_native_skin_strips_and_readout(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for bipolar, filename in ((True, "play_sub_pan_40.png"), (False, "play_sub_level_40.png")):
                source = template()
                source["componentsData"].pop()  # Some SDK knobs only carry the caption.
                factory = compact_pan_definition if bipolar else compact_level_definition
                definition = factory(source, directory)
                children = {c["componentData"]["name"]: c for c in definition["componentsData"]}
                self.assertEqual(children["value"]["bounds"]["bounds"], "48 22 150 22")
                data = children["knob"]["componentData"]["data"]
                self.assertEqual((data["knobType"], data["filmStrip"]), ("FilmStrip", filename))
                self.assertEqual(data["handleName"], "Data")
                with Image.open(directory / filename) as strip:
                    self.assertEqual(strip.size, (40, 40 * data["numFrames"]))
