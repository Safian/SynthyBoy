# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the compact OSC routing dropdown and button faces."""

import os
from pathlib import Path
import sys
import tempfile
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from components.native.selector import selector as nrs
from components.native.selector.selector import (
    COMPACT_OSC_ROUTING_HEIGHT,
    OSC_COMPACT_ROUTING_COMPONENT_TYPE,
    OSC_ROUTING_CHOICES,
    OSC_ROUTING_OVERLAY_NAME,
    STANDALONE_COMPACT_OSC_ROUTING_RECT,
    build_compact_osc_routing_component,
    build_compact_osc_routing_definition,
    write_routing_assets,
)


class TestCompactRouteSelector(unittest.TestCase):
    def test_compact_constants_and_geometry(self):
        self.assertEqual(nrs.COMPACT_HEIGHT, 48)
        self.assertEqual(COMPACT_OSC_ROUTING_HEIGHT, 48)
        self.assertEqual(nrs.STANDALONE_RECT[3], 48)
        self.assertEqual(STANDALONE_COMPACT_OSC_ROUTING_RECT, (1122, 37, 150, 48))
        self.assertEqual(nrs.BUTTON_COMPACT_RECT, (282, 86, 64, 48))
        self.assertEqual(OSC_COMPACT_ROUTING_COMPONENT_TYPE, "oscNativeCompactRouting")

    def test_factory_destination_choices(self):
        self.assertEqual(len(OSC_ROUTING_CHOICES), 7)
        expected_choices = (
            "Filter 1",
            "Filter 2",
            "Filters 1+2",
            "Effects only",
            "Direct out",
            "Filter blend",
            "None",
        )
        self.assertEqual(OSC_ROUTING_CHOICES, expected_choices)
        self.assertEqual(len(nrs.OSC_ROUTING_SHORT_NAMES), 7)
        self.assertEqual(nrs.OSC_ROUTING_SHORT_NAMES[0], "F1")
        self.assertEqual(nrs.OSC_ROUTING_SHORT_NAMES[1], "F2")
        self.assertEqual(nrs.OSC_ROUTING_SHORT_NAMES[2], "F1+F2")

    def test_build_compact_routing_definition(self):
        source_def = {
            "actions": [
                {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
            ],
            "componentsData": [
                {"componentData": {"type": "Focus"}, "bounds": {"bounds": "0 0 200 56"}},
            ],
        }
        with tempfile.TemporaryDirectory() as skins:
            res = build_compact_osc_routing_definition(source_def, skins)
        # Actions must trigger Show Overlay pointing to OSC_ROUTING_OVERLAY_NAME
        show_actions = [a for a in res["actions"] if a.get("handler") == "Show Overlay"]
        self.assertEqual(len(show_actions), 2)
        triggers = {a["onAction"] for a in show_actions}
        self.assertEqual(triggers, {"Mouse Down", "Enter Pressed"})
        for a in show_actions:
            self.assertEqual(a["additionalData"], OSC_ROUTING_OVERLAY_NAME)
            self.assertEqual(a["handleName"], "Data")

        # Must have Focus, Image frame, and Value Label
        comp_types = [c["componentData"]["type"] for c in res["componentsData"]]
        self.assertIn("Focus", comp_types)
        self.assertIn("Image", comp_types)
        self.assertIn("Label", comp_types)

        # Image frame bounds height must be 48
        frame_comp = next(c for c in res["componentsData"] if c["componentData"]["type"] == "Image")
        self.assertEqual(frame_comp["bounds"]["bounds"], "0 0 150 48")

        # Focus bounds height must be 48
        focus_comp = next(c for c in res["componentsData"] if c["componentData"]["type"] == "Focus")
        self.assertEqual(focus_comp["bounds"]["bounds"], "0 0 150 48")

    def test_build_compact_routing_component(self):
        comp = build_compact_osc_routing_component(
            dest_param_idx=42,
            power_param_idx=10,
            visibility_param_idx=20,
        )
        self.assertEqual(comp["componentData"]["type"], OSC_COMPACT_ROUTING_COMPONENT_TYPE)
        self.assertEqual(comp["bounds"]["bounds"], "1122 37 150 48")
        self.assertEqual(comp["bounds"]["acceptsHWFocus"], "Yes")  # Physical binding retained!
        self.assertEqual(comp["bounds"]["showWhenDataModelInvalid"], "Hide")
        self.assertEqual(comp["bounds"]["whenVisible"], "Always")

        # Parameter mapping to destination parameter
        remap = comp["handle remapping"]["map"]
        self.assertEqual(remap, [{"key": "Data", "value": "Parameter 42"}])

    def test_asset_generation(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            write_routing_assets(tmp_path)

            self.assertEqual(list(tmp_path.glob("osc_routing_frame*.png")), [])

            bg = tmp_path / "osc_routing_overlay_bg.png"
            self.assertTrue(bg.exists())
            with Image.open(bg) as im:
                self.assertEqual(im.size, (16, 16))

    def test_render_compact_button_face(self):
        im_b1 = nrs.render_compact_button_face(64, 48, target="F1", seg0=True, seg1=False)
        self.assertEqual(im_b1.size, (64, 48))

        im_b2 = nrs.render_compact_button_face(64, 48, target="F2", seg0=False, seg1=True)
        self.assertEqual(im_b2.size, (64, 48))

    def test_render_choice_overlay_art(self):
        im = nrs.render_choice_overlay_art(active_index=0, width=408)
        self.assertEqual(im.width, 408)
        self.assertGreater(im.height, 300)



if __name__ == "__main__":
    unittest.main()
