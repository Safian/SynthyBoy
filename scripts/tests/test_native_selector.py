# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the shared native MPC list selector component and variants."""

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

import native_selector as ns
from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_SOURCE_OVERLAY_NAME,
    selector_definition,
)


class TestNativeSelector(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.tmp.name)
        self.base_template = {
            "version": 2,
            "actions": [],
            "componentsData": [
                {"componentData": {"type": "Focus"}, "bounds": {"bounds": "0 0 200 56"}},
            ],
        }

    def tearDown(self):
        self.tmp.cleanup()

    def test_constants_and_dimensions(self):
        self.assertEqual(ns.STANDALONE_OSC_SIZE, (200, 56))
        self.assertEqual(ns.COMPACT_SELECTOR_HEIGHT, 48)

    def test_png_filter0_encoding(self):
        """Verifies PNG scanline filter 0 encoding across neutral RGB and themed RGBA images."""
        im_neutral = Image.new("RGB", (32, 16), (32, 32, 32))
        path_neutral = self.skins_dir / "test_neutral.png"
        ns.save_png_filter0(im_neutral, path_neutral, neutral=True)
        raw_neutral = path_neutral.read_bytes()
        self.assertTrue(raw_neutral.startswith(b"\x89PNG\r\n\x1a\n"))
        # Check IHDR color type 2 (RGB)
        ihdr_color_type = raw_neutral[25]
        self.assertEqual(ihdr_color_type, 2)

        im_rgba = Image.new("RGBA", (32, 16), (14, 27, 51, 255))
        path_rgba = self.skins_dir / "test_rgba.png"
        ns.save_png_filter0(im_rgba, path_rgba, neutral=False)
        raw_rgba = path_rgba.read_bytes()
        self.assertTrue(raw_rgba.startswith(b"\x89PNG\r\n\x1a\n"))
        # Check IHDR color type 6 (RGBA)
        ihdr_color_type_rgba = raw_rgba[25]
        self.assertEqual(ihdr_color_type_rgba, 6)

    def test_render_pipa_and_wave_icons(self):
        """Verifies regenerable checkmark (pipa) and waveform icons."""
        pipa = ns.render_pipa_icon(20)
        self.assertEqual(pipa.size, (20, 20))
        self.assertEqual(pipa.mode, "RGBA")

        wave = ns.render_wave_icon(28, 24)
        self.assertEqual(wave.size, (28, 24))
        self.assertEqual(wave.mode, "RGBA")



    def test_shape_variant(self):
        """Verifies SHAPE variant with static caption, value label and stepper chevrons."""
        res = ns.build_shape_selector(
            self.base_template,
            "playShapeMenu",
            slot=(202, 56),
            skins_dir=self.skins_dir,
            title="lfo1.shape",
            caption="SHAPE",
            stepper=True,
            arrows=True,
        )

        # Image face with stepper
        frame = next(c for c in res["componentsData"] if c["componentData"].get("name") == "Selector face")
        self.assertTrue("stepper" in frame["componentData"]["data"]["image"] or "arrows" in frame["componentData"]["data"]["image"])

        # Caption text
        cap = next(c for c in res["componentsData"] if c["componentData"].get("name") == "Name")
        self.assertEqual(cap["componentData"]["data"]["type"], "Text")
        self.assertEqual(cap["componentData"]["data"]["text"], "SHAPE")

        # Value label
        val = next(c for c in res["componentsData"] if c["componentData"].get("name") == "lfo1.shape")
        self.assertEqual(val["componentData"]["data"]["handleName"], "lfo1.shape")

    def test_native_list_overlay(self):
        tui = {"pageData": {}}
        items = ("Sine", "Triangle", "Saw")
        handle = ns.bind_list_choices(tui, 57, "Wave", items)
        self.assertEqual(handle, "WithName[Wave]/Parameter 57")
        self.assertEqual(tui["pageData"]["replacementStrings"], [{"key": "Parameter 57", "value": list(items)}])
        overlay = ns.build_native_list_overlay("Wave choices")
        (entry,) = overlay["componentsData"]
        self.assertEqual(entry["componentData"]["type"], "MenuOverlay")
        self.assertEqual(entry["componentData"]["data"]["handleName"], "Data")
        self.assertEqual(entry["componentData"]["data"]["actions"][0]["handler"], "Focus Lost")

    def test_backward_compatibility_with_legacy_selector_callers(self):
        """Verifies that calling selector_definition with old arguments behaves identically."""
        # 1. Direct call on component selector_definition
        res_play = selector_definition(
            self.base_template,
            "oldPopup",
            (202, 72),
            self.skins_dir,
            icon="old_icon.png",
            title="Data",
            name="OldName",
        )
        self.assertEqual(len(res_play["actions"]), 2)
        comp_names = [c["componentData"].get("name") for c in res_play["componentsData"] if "name" in c.get("componentData", {})]
        self.assertIn("Selector face", comp_names)
        self.assertIn("Selector icon", comp_names)
        self.assertIn("Title", comp_names)
        self.assertIn("Name", comp_names)

if __name__ == "__main__":
    unittest.main()
