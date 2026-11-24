# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Behavioral regressions found during the canonical native component review."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"components/native/{name}/{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class NativeComponentReviewTest(unittest.TestCase):
    def test_distinct_radio_labels_keep_their_own_art_across_groups(self):
        radio = load("radio_button")
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            for variant, renderer in ((radio.VARIANT_SEGMENTED, radio.render_segmented_button_art),
                                      (radio.VARIANT_RADIO_DOT, radio.render_radio_dot_button_art)):
                first = radio.build_radio_button_entries("First", (0, 0, 240, 48),
                    ("A-B", "OTHER"), variant, "Parameter 10", tmp)
                second = radio.build_radio_button_entries("Second", (0, 0, 240, 48),
                    ("A B", "OTHER"), variant, "Parameter 20", tmp)
                one = first[0]["componentData"]["data"]
                two = second[0]["componentData"]["data"]
                self.assertNotEqual(one["onImage"], two["onImage"])
                for data, label in ((one, "A-B"), (two, "A B")):
                    for active, key in ((True, "onImage"), (False, "offImage")):
                        with Image.open(Path(tmp) / data[key]) as actual:
                            self.assertEqual(actual.tobytes(), renderer(label, active, 120, 48).tobytes())

    def test_gated_name_value_hides_invalid_model(self):
        nv = load("name_value")
        gated = nv.create_name_value_entry("Value", "sharedNativeNameValue", (0, 0, 125, 48),
            param=10, gates=("IndexedEnabling/1/2/Parameter 20",))
        self.assertEqual(gated["bounds"]["showWhenDataModelInvalid"], "Hide")
        self.assertEqual(gated["bounds"]["version"], 2)
        plain = nv.create_name_value_entry("Value", "sharedNativeNameValue", (0, 0, 125, 48), param=10)
        self.assertEqual(plain["bounds"]["showWhenDataModelInvalid"], "Show")

    def test_name_value_instances_do_not_share_caller_remaps(self):
        for name in ("name_value", "tune"):
            with self.subTest(component=name):
                module = load(name)
                remaps = [{"key": "Data", "value": "Parameter 10"}]
                a = module.create_name_value_entry("A", "field", (0, 0, 125, 32), remap=remaps)
                b = module.create_name_value_entry("B", "field", (0, 0, 125, 32), remap=remaps)
                a["handle remapping"]["map"][0]["value"] = "Parameter 99"
                self.assertEqual(remaps[0]["value"], "Parameter 10")
                self.assertEqual(b["handle remapping"]["map"][0]["value"], "Parameter 10")

    def test_name_value_corner_variants_do_not_overwrite_faces(self):
        for name in ("name_value", "tune"):
            with self.subTest(component=name), tempfile.TemporaryDirectory(dir=ROOT) as tmp:
                module = load(name)
                definitions = [module.build_name_value_definition(skins_dir=tmp, radius=r) for r in (2, 8)]
                paths = [next(c for c in d["componentsData"] if c["componentData"]["name"] == "Face")
                         ["componentData"]["data"]["image"] for d in definitions]
                self.assertNotEqual(*paths)
                for filename, radius in zip(paths, (2, 8)):
                    with Image.open(Path(tmp) / filename) as actual:
                        height = actual.height
                        self.assertEqual(actual.tobytes(), module.render_name_value_box_face(68, height, radius=radius).tobytes())

    def test_tune_export_writes_each_referenced_asset_once(self):
        tune = load("tune")
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            saved = []
            original = Image.Image.save
            def record(image, path, *args, **kwargs):
                saved.append(Path(path).name)
                return original(image, path, *args, **kwargs)
            with patch.object(Image.Image, "save", record):
                definitions = tune.build_standalone_osc_tune_definitions(skins_dir=tmp)
            referenced = []
            for definition in definitions.values():
                for child in definition["componentsData"]:
                    data = child["componentData"].get("data", {})
                    referenced.extend(data[key] for key in ("image", "onImage", "offImage") if data.get(key))
            self.assertEqual(set(saved), set(referenced))
            self.assertEqual(len(saved), len(set(saved)))

    def test_smooth_caption_and_value_fit_native_and_preview_geometry(self):
        level = load("level")
        definition = level.build_native_smooth_dial_with_indicator_definition()
        children = {c["componentData"]["name"]: c for c in definition["componentsData"]}
        for name, text in (("name", "SMOOTH"), ("value", "150 ms")):
            child = children[name]
            x, y, width, height = map(int, child["bounds"]["bounds"].split())
            font_height = (level._SMOOTH_NAME_HEIGHT if name == "name" else
                           child["componentData"]["data"]["textStyle"]["font"]["height"])
            for scale in (1, 2, 4):
                font = level._get_font(round(font_height * scale))
                left, top, right, bottom = font.getbbox(text, anchor="lm")
                self.assertGreaterEqual(left, 0)
                self.assertLessEqual(right, width * scale)
                self.assertGreaterEqual(height * scale / 2 + top, 0)
                self.assertLessEqual(height * scale / 2 + bottom, height * scale)
            self.assertLessEqual(x + width, 116)
            self.assertLessEqual(y + height, 72)
        self.assertNotIn("led", children)
        self.assertEqual(len(children), 4)
        caption = children["name"]["componentData"]
        self.assertEqual(caption["type"], "Button")
        self.assertEqual(caption["data"]["handleName"], "State")
        for on, key in ((True, "onImage"), (False, "offImage")):
            with Image.open(ROOT / "components/native/level/img" / caption["data"][key]) as asset:
                self.assertEqual(asset.tobytes(), level._render_smooth_caption(on).tobytes())


    def test_macro_definition_without_export_does_not_render_discarded_strip(self):
        macro = load("macro_card")
        with patch.object(macro, "write_level_strip", side_effect=AssertionError("unexpected render")):
            definition = macro.macro_level_definition()
        knob = next(c for c in definition["componentsData"] if c["componentData"]["name"] == "knob")
        self.assertEqual(knob["componentData"]["data"]["filmStrip"], "play_sub_level_90.png")


    def test_standalone_builders_use_gate_capable_bounds(self):
        template = {
            "version": 2,
            "componentData": {"version": 1, "name": "template", "type": "Button"},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 1, "bounds": "0 0 56 56"},
        }
        button = load("button")
        level = load("level")
        selector = load("selector")
        entries = [
            button.build_filter_power(None, template, 1, 10, (0, 0, 56, 56), overlay_param=20),
            level.build_standalone_smooth(template, "LFO 2", 10, (0, 0, 56, 56), overlay_param=20),
            selector.build_standalone_osc_selector(None, template, "Osc A", 10, overlay_param=20),
        ]
        for entry in entries:
            self.assertEqual(entry["bounds"]["version"], 2)
            self.assertEqual(entry["bounds"]["additionalInvalidatingHandles"],
                             ["RangedEnabling/0/18/33/Parameter 20"])
        self.assertEqual(entries[1]["componentData"]["name"], "LFO 2 Smooth Native")
        self.assertEqual(template["bounds"]["version"], 1)


if __name__ == "__main__":
    unittest.main()
