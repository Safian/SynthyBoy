# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import copy
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from components import style
from components.native.level.level import (
    compact_dial_definition, compact_level_definition, stacked_dial_definition,
)
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
                family = "pan" if polarity == "BiPolar" else "level"
                frame_size = int(components["knob"]["bounds"]["bounds"].split()[2])
                self.assertEqual((knob["handleName"], knob["knobType"]), ("Data", "FilmStrip"))
                self.assertEqual(knob["filmStrip"], f"play_sub_{family}_{frame_size}.png")
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
        self.assertEqual(second["backgroundData"]["focussed"]["colour"], style.native_color("transparent"))

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
                family = "pan" if polarity == "BiPolar" else "level"
                frame_size = int(components["knob"]["bounds"]["bounds"].split()[2])
                self.assertEqual((knob["handleName"], knob["knobType"]), ("Data", "FilmStrip"))
                self.assertEqual(knob["filmStrip"], f"play_sub_{family}_{frame_size}.png")

                name_ctrl = components["name"]["componentData"]["data"]
                self.assertEqual(name_ctrl["handleName"], "Caption")
                self.assertEqual(name_ctrl["textStyle"]["font"]["height"], 14)
                # the dial name: upper case, theme textDim
                self.assertEqual(name_ctrl["textStyle"]["colour"], style.argb("text.caption"))
                self.assertEqual(style.color("text.caption"), style.theme_colors("neon")["textDim"])
                self.assertEqual(name_ctrl["textStyle"]["case"], "Upper Case")

                val_ctrl = components["value"]["componentData"]["data"]
                self.assertEqual(val_ctrl["handleName"], "Data")
                self.assertEqual(val_ctrl["textStyle"]["font"]["height"], 16)
                self.assertEqual(val_ctrl["textStyle"]["colour"], "ffeef1ff")

                self.assertEqual(result["actions"], [{"handler": "Focus Lost"}])
                self.assertEqual(result["backgroundData"]["focussed"]["colour"], style.native_color("transparent"))
                self.assertEqual(result["backgroundData"]["unfocussed"]["colour"], style.native_color("transparent"))

    def test_stacked_dials_ship_art_and_bind_to_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            for polarity, family in (("UniPolar", "level"), ("BiPolar", "pan")):
                definition = stacked_dial_definition(tmp, (120, 124), diameter=90, caption_height=16, polarity=polarity)
                knob = next(child["componentData"]["data"] for child in definition["componentsData"]
                            if child["componentData"]["type"] == "Knob")
                self.assertEqual(knob["knobType"], "FilmStrip")
                self.assertEqual(knob["handleName"], "Data")
                self.assertEqual(knob["filmStrip"], f"play_sub_{family}_90.png")
                with Image.open(Path(tmp) / knob["filmStrip"]) as img:
                    self.assertEqual(img.size, (90, 90 * knob["numFrames"]))

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
