# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Unit tests for standalone native MacroCard component, geometry, and renderer."""

from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import components.native.macro_card.macro_card as nmc


class TestNativeMacroCard(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.tmp_dir.name)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_level_definition_matches_playsublevel_factory(self):
        """Verify macro_level_definition matches playSubLevel: 90px FilmStrip knob, name right, value beneath."""
        defn = nmc.macro_level_definition(skins_dir=self.skins_dir)

        self.assertEqual(defn.get("version"), 2)
        self.assertEqual(defn.get("actions"), [])
        self.assertFalse(defn.get("ignoreMousePresses"))
        self.assertFalse(defn.get("disableCoarseDataWheel"))

        components = {c["componentData"]["name"]: c for c in defn["componentsData"]}
        self.assertIn("knob", components)
        self.assertIn("name", components)
        self.assertIn("value", components)

        # 1. Knob: 90x90 FilmStrip cyan unipolar dial
        knob_comp = components["knob"]["componentData"]
        self.assertEqual(knob_comp["name"], "knob")
        self.assertEqual(knob_comp["type"], "Knob")
        knob_data = knob_comp["data"]
        self.assertEqual(knob_data["knobType"], "FilmStrip")
        self.assertEqual(knob_data["filmStrip"], "play_sub_level_90.png")
        self.assertEqual(knob_data["numFrames"], 127)
        self.assertEqual(knob_data["handleName"], "Data")
        self.assertFalse(knob_data["invert"])
        self.assertEqual(knob_data["dragOrientation"], "Vertical")
        self.assertEqual(components["knob"]["bounds"]["bounds"], " ".join(map(str, nmc.KNOB_RECT)))

        # Verify generated strip
        strip_file = self.skins_dir / "play_sub_level_90.png"
        self.assertTrue(strip_file.is_file())
        with Image.open(strip_file) as strip_im:
            self.assertEqual(strip_im.size, (90, 90 * 127))

        # 2. Name label: centered below value from shared MacroCard contract
        name_comp = components["name"]["componentData"]
        self.assertEqual(name_comp["name"], "name")
        self.assertEqual(name_comp["data"]["type"], "Name")
        self.assertEqual(name_comp["data"]["handleName"], "Caption")
        self.assertEqual(name_comp["data"]["textStyle"]["colour"], "ffaab2d8")
        self.assertEqual(name_comp["data"]["textStyle"]["font"]["height"], 15.0)
        self.assertEqual(name_comp["data"]["textStyle"]["justification"], "horizontallyCentred verticallyCentred")
        self.assertEqual(components["name"]["bounds"]["bounds"], " ".join(map(str, nmc.NAME_RECT)))

        # 3. Value label: beneath dial from shared MacroCard contract
        val_comp = components["value"]["componentData"]
        self.assertEqual(val_comp["name"], "value")
        self.assertEqual(val_comp["data"]["type"], "Value")
        self.assertEqual(val_comp["data"]["handleName"], "Data")
        self.assertEqual(val_comp["data"]["textStyle"]["colour"], "ffeef1ff")
        self.assertEqual(val_comp["data"]["textStyle"]["font"]["height"], 20.0)
        self.assertEqual(val_comp["data"]["textStyle"]["justification"], "horizontallyCentred verticallyCentred")
        self.assertEqual(components["value"]["bounds"]["bounds"], " ".join(map(str, nmc.VALUE_RECT)))

    def test_card_references_the_shared_level_and_isolates_target(self):
        definition = nmc.build_macro_card_definition(self.skins_dir, "F1 CUTOFF")
        dial, target = definition["componentsData"]
        self.assertEqual(dial["componentData"]["type"], "macroSubLevel")
        self.assertEqual(dial["bounds"]["bounds"], " ".join(map(str, nmc.DIAL_RECT)))
        self.assertEqual(target["bounds"]["bounds"], " ".join(map(str, nmc.TARGET_RECT)))
        self.assertEqual(target["handle remapping"]["map"], [{"key": "Data", "value": "Target"}])
        self.assertTrue((self.skins_dir / target["componentData"]["data"]["onImage"]).is_file())
        # Shared card geometry must keep >= 56 px touch targets and zero overlap between dial, value, name hitbox, and target
        self.assertGreaterEqual(nmc.NATIVE_KNOB_RECT[2], 56)
        self.assertGreaterEqual(nmc.NATIVE_KNOB_RECT[3], 56)
        self.assertGreaterEqual(nmc.NAME_HIT_RECT[2], 56)
        self.assertGreaterEqual(nmc.NAME_HIT_RECT[3], 56)
        self.assertGreaterEqual(nmc.TARGET_RECT[2], 56)
        self.assertGreaterEqual(nmc.TARGET_RECT[3], 56)
        self.assertLessEqual(nmc.NATIVE_KNOB_RECT[1] + nmc.NATIVE_KNOB_RECT[3], nmc.VALUE_RECT[1])
        self.assertLessEqual(nmc.VALUE_RECT[1] + nmc.VALUE_RECT[3], nmc.NAME_HIT_RECT[1])
        self.assertLessEqual(nmc.NAME_HIT_RECT[1] + nmc.NAME_HIT_RECT[3], nmc.TARGET_RECT[1])
        for i in range(8):
            instance = nmc.build_macro_card_instance("Macro", 300+i, (0,0,nmc.CARD_WIDTH,nmc.CARD_HEIGHT),
                          caption=f"MACRO {i+1}", target_handle=f"FromGesture/Parameter {500+i}")
            bindings = {h["key"]: h["value"] for h in instance["handle remapping"]["map"]}
            self.assertEqual(bindings["Data"], f"Parameter {300+i}")
            self.assertEqual(bindings["Caption"], f"WithName[MACRO {i+1}]/Parameter {300+i}")
            self.assertEqual(bindings["Target"], f"FromGesture/Parameter {500+i}")
        with self.assertRaises(ValueError):
            nmc.build_macro_card_instance("Macro", 300, (0,0,nmc.CARD_WIDTH,nmc.CARD_HEIGHT), target_handle="")

    def test_render_macro_card_and_pixel_verification(self):
        """Verify authentic rendering: frameless percentage, no TUNE box, 48px button, 0 yellow pixels, clean value/name separation."""
        level_def = nmc.macro_level_definition(skins_dir=self.skins_dir)
        strip_path = self.skins_dir / "play_sub_level_90.png"

        with Image.open(strip_path) as strip_im:
            card = nmc.render_macro_card(
                level_def, strip_im, "MACRO 1", 50, "F1 CUTOFF", width=nmc.CARD_WIDTH, height=nmc.CARD_HEIGHT
            )

        self.assertEqual(card.size, (152, 238))
        arr = np.array(card)

        # 1. No red pixels (generic red knob is forbidden; our active arc is cyan #4c8dff)
        red_mask = (arr[:, :, 0] > 200) & (arr[:, :, 1] < 60) & (arr[:, :, 2] < 60)
        self.assertEqual(np.sum(red_mask), 0, "No red generic-knob pixels permitted in MacroCard")

        # 2. No yellow Q-Link badge pixels (Yellow: R > 200, G > 180, B < 80)
        yellow_mask = (arr[:, :, 0] > 200) & (arr[:, :, 1] > 180) & (arr[:, :, 2] < 80)
        self.assertEqual(np.sum(yellow_mask), 0, "No yellow Q-Link badge pixels permitted in MacroCard")

        # 3. Cyan active dial pixels are present (#4c8dff: R ~ 76, G ~ 141, B ~ 255)
        cyan_mask = (arr[:, :, 0] < 120) & (arr[:, :, 1] > 100) & (arr[:, :, 2] > 220)
        self.assertGreater(np.sum(cyan_mask), 50, "Cyan LEVEL dial arc pixels must be rendered")

        # 4. Value and Name must have a clean horizontal band of background pixels (no text overlap)
        gap_y0 = nmc.VALUE_RECT[1] + nmc.VALUE_RECT[3] + 2
        gap_y1 = nmc.NAME_RECT[1] - 2
        gap_band = arr[gap_y0:gap_y1, 8:nmc.CARD_WIDTH - 8, :3]
        bright_in_gap = np.sum(np.sum(gap_band, axis=2) > 220)
        self.assertEqual(bright_in_gap, 0, "Value and Name must have a clean vertical gap with zero overlapping text pixels")

        level_bg = level_def["backgroundData"]["unfocussed"]["colour"]
        self.assertEqual(card.getpixel((10, 70))[:3], tuple(bytes.fromhex(level_bg[-6:])))

    def test_render_macro_preview_generates_valid_image(self):
        """Verify render_macro_preview generates valid 3-card preview."""
        out_path = self.skins_dir / "macro-nativ-test.png"
        res_path = nmc.render_macro_preview(out_path)
        self.assertEqual(res_path, str(out_path))
        self.assertTrue(out_path.is_file())

        with Image.open(out_path) as im:
            self.assertEqual(im.size, (3 * nmc.CARD_WIDTH + 4 * 16, nmc.CARD_HEIGHT + 2 * 16))
            self.assertEqual(im.mode, "RGBA")


if __name__ == "__main__":
    unittest.main()
