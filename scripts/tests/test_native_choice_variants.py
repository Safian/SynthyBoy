# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the two native choice group variants.

Tests:
1. Segmented buttons with full active background (VARIANT_SEGMENTED)
2. Factory exclusionary radio buttons with cyan filled/empty dot skin (VARIANT_RADIO_DOT)
3. 3 choices (HZ, BPM, KEY) and 2 choices (two banks)
4. Common value binding compatibility
5. Touch target geometry (full option touchable, >= 56 px)
6. Image artwork assets (cyan filled vs empty dot, full active background)
"""

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

    def test_radio_dot_variant_3_choices(self):
        rect = (100, 200, 248, 48)
        labels = ("HZ", "BPM", "KEY")
        handle = "Parameter 3348"
        items = ncg.create_radio_dot_choice_group("LFO Sync Radio", rect, labels, handle, self.skins_dir)

        self.assertEqual(len(items), 3)
        total_width = 0
        for i, (item, label) in enumerate(zip(items, labels)):
            comp = item["componentData"]
            self.assertEqual(comp["type"], "Button")
            self.assertEqual(comp["name"], f"LFO Sync Radio {label}")

            data = comp["data"]
            self.assertEqual(data["buttonId"], i)
            self.assertEqual(data["numButtonsInGroup"], 3)
            self.assertEqual(data["handleName"], "Data")
            self.assertEqual(data["gestureBehaviour"], "Instant")

            remap = item["handle remapping"]["map"]
            self.assertEqual(remap, [{"key": "Data", "value": handle}])

            bx, by, bw, bh = map(int, item["bounds"]["bounds"].split())
            self.assertEqual(by, 200)
            self.assertEqual(bh, 48)
            self.assertGreaterEqual(bw, 56)
            total_width += bw

            # Check images and cyan dot verification
            on_path = self.skins_dir / data["onImage"]
            off_path = self.skins_dir / data["offImage"]
            self.assertTrue(on_path.is_file())
            self.assertTrue(off_path.is_file())

            with Image.open(on_path) as on_im, Image.open(off_path) as off_im:
                self.assertEqual(on_im.size, (bw, bh))
                self.assertEqual(off_im.size, (bw, bh))

                # Dot center: cx is around 15 + 6 = 21 px, cy is bh // 2 = 24
                dot_cx, dot_cy = 21, bh // 2
                on_dot_pixel = on_im.getpixel((dot_cx, dot_cy))
                off_dot_pixel = off_im.getpixel((dot_cx, dot_cy))

                # Active has filled cyan dot (high green & blue)
                self.assertGreater(on_dot_pixel[1], 180, "Active dot must be cyan filled")
                self.assertGreater(on_dot_pixel[2], 200, "Active dot must be cyan filled")

                # Inactive center has empty dot (body color, low brightness)
                self.assertLess(off_dot_pixel[1], 60, "Inactive dot center must be empty")
                self.assertLess(off_dot_pixel[2], 60, "Inactive dot center must be empty")

        self.assertEqual(total_width, 248)

    def test_radio_dot_variant_2_banks(self):
        rect = (0, 0, 278, 56)
        labels = ("BANK 1", "BANK 2")
        handle = "Parameter 4020"
        items = ncg.create_radio_dot_choice_group("Bank Choice", rect, labels, handle, self.skins_dir)

        self.assertEqual(len(items), 2)
        for i, item in enumerate(items):
            data = item["componentData"]["data"]
            self.assertEqual(data["buttonId"], i)
            self.assertEqual(data["numButtonsInGroup"], 2)
            self.assertEqual(item["handle remapping"]["map"], [{"key": "Data", "value": handle}])

            bx, by, bw, bh = map(int, item["bounds"]["bounds"].split())
            self.assertEqual(bh, 56)
            self.assertGreaterEqual(bw, 56)

            on_path = self.skins_dir / data["onImage"]
            with Image.open(on_path) as on_im:
                self.assertEqual(on_im.size, (bw, bh))

    def test_common_value_binding(self):
        """Verify that both variants share the identical binding contract and can be swapped."""
        rect = (50, 50, 240, 48)
        labels = ("HZ", "BPM", "KEY")
        handle = "Parameter 3348"

        seg_items = ncg.create_segmented_choice_group("Test", rect, labels, handle, self.skins_dir)
        radio_items = ncg.create_radio_dot_choice_group("Test", rect, labels, handle, self.skins_dir)

        self.assertEqual(len(seg_items), len(radio_items))
        for s_item, r_item in zip(seg_items, radio_items):
            s_data = s_item["componentData"]["data"]
            r_data = r_item["componentData"]["data"]

            # Same value binding properties
            self.assertEqual(s_data["buttonId"], r_data["buttonId"])
            self.assertEqual(s_data["numButtonsInGroup"], r_data["numButtonsInGroup"])
            self.assertEqual(s_data["handleName"], r_data["handleName"])
            self.assertEqual(s_data["gestureBehaviour"], r_data["gestureBehaviour"])

            # Same handle remapping
            self.assertEqual(s_item["handle remapping"], r_item["handle remapping"])

            # Same bounds
            self.assertEqual(s_item["bounds"]["bounds"], r_item["bounds"]["bounds"])
            self.assertEqual(s_item["bounds"]["acceptsHWFocus"], r_item["bounds"]["acceptsHWFocus"])



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
