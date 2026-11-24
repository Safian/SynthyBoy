# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native choice geometry, artwork and parameter bindings."""

import tempfile
import unittest
from pathlib import Path
from PIL import Image

import native_choice_group as ncg


class TestNativeChoiceVariants(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_segmented_variant_3_choices(self):
        rect = (100, 200, 248, 48)
        labels = ("HZ", "BPM", "KEY")
        handle = "Parameter 3348"
        items = ncg.create_segmented_choice_group("LFO Sync Mode", rect, labels, handle, self.skins_dir)

        self.assertEqual(len(items), 3)
        total_width = 0
        for i, (item, label) in enumerate(zip(items, labels)):
            comp = item["componentData"]
            self.assertEqual(comp["type"], "Button")
            self.assertEqual(comp["name"], f"LFO Sync Mode {label}")

            data = comp["data"]
            self.assertEqual(data["version"], 2)
            self.assertEqual(data["buttonId"], i)
            self.assertEqual(data["numButtonsInGroup"], 3)
            self.assertEqual(data["handleName"], "Data")
            self.assertEqual(data["gestureBehaviour"], "Instant")

            remap = item["handle remapping"]["map"]
            self.assertEqual(remap, [{"key": "Data", "value": handle}])

            bounds = item["bounds"]
            self.assertEqual(bounds["acceptsHWFocus"], "Yes")
            self.assertEqual(bounds["showWhenDataModelInvalid"], "Hide")
            self.assertEqual(bounds["whenVisible"], "Always")

            bx, by, bw, bh = map(int, bounds["bounds"].split())
            self.assertEqual(by, 200)
            self.assertEqual(bh, 48)
            self.assertGreaterEqual(bw, 56, "Touch target width must be >= 56 px")
            total_width += bw

            # Check that images were saved and match button bounds
            on_path = self.skins_dir / data["onImage"]
            off_path = self.skins_dir / data["offImage"]
            self.assertTrue(on_path.is_file(), f"Missing onImage: {on_path}")
            self.assertTrue(off_path.is_file(), f"Missing offImage: {off_path}")

            with Image.open(on_path) as on_im, Image.open(off_path) as off_im:
                self.assertEqual(on_im.size, (bw, bh))
                self.assertEqual(off_im.size, (bw, bh))
                # On image center should have active cyan color
                on_center = on_im.getpixel((bw // 2, 8))  # top area above centered text
                off_center = off_im.getpixel((bw // 2, 8))
                # Cyan active: G and B much higher than R
                self.assertGreater(on_center[1], 150)
                self.assertGreater(on_center[2], 180)
                # Dark inactive: low brightness
                self.assertLess(off_center[0], 50)
                self.assertLess(off_center[1], 60)

        self.assertEqual(total_width, 248, "Option widths must tile the full slot width")

    def test_segmented_variant_2_banks(self):
        rect = (50, 100, 248, 56)
        labels = ("BANK A", "BANK B")
        handle = "Parameter 4010"
        items = ncg.create_segmented_choice_group("Osc Bank", rect, labels, handle, self.skins_dir)

        self.assertEqual(len(items), 2)
        for i, item in enumerate(items):
            data = item["componentData"]["data"]
            self.assertEqual(data["buttonId"], i)
            self.assertEqual(data["numButtonsInGroup"], 2)
            self.assertEqual(item["handle remapping"]["map"], [{"key": "Data", "value": handle}])

            bx, by, bw, bh = map(int, item["bounds"]["bounds"].split())
            self.assertEqual(bw, 124)
            self.assertEqual(bh, 56)
            self.assertGreaterEqual(bw, 56)

            on_path = self.skins_dir / data["onImage"]
            with Image.open(on_path) as on_im:
                self.assertEqual(on_im.size, (124, 56))

    def test_play_lfo_choice_group_binding(self):
        rect = (0, 0, 248, 48)
        labels = ("HZ", "BPM", "KEY")
        handle = "Parameter 3348"
        items = ncg.build_choice_group_entries("Play LFO Sync", rect, labels, ncg.VARIANT_SEGMENTED, handle, self.skins_dir)
        self.assertEqual(len(items), 3)
        self.assertEqual(items[1]["componentData"]["data"]["buttonId"], 1)
        self.assertEqual(items[1]["handle remapping"]["map"], [{"key": "Data", "value": handle}])



if __name__ == "__main__":
    unittest.main()
