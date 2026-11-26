# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for host-native ModRow component, shared row definitions,
real 56px slider hitbox, handle resolution, and gating contract.
"""

from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from components import style
import components.native.mod_row.mod_row as nmr


class TestNativeModRow(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.tmp_dir.name)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_unknown_editor_cell_fails_generation(self):
        with self.assertRaisesRegex(ValueError, "Unsupported native modulation editor cell"):
            nmr.build_editor_cell_definition("unsupported", 80, 60)

    def test_shared_row_registered_once_with_seven_instances(self):
        """ONE canonical row definition registered, instantiated 7 times for 7 matrix rows."""
        defs = nmr.build_mod_row_definitions(self.skins_dir)

        # Canonical row definition exists
        self.assertIn("modRow", defs)
        self.assertIn("modNativeRowNormal", defs)
        self.assertIn("modNativeRowScroll", defs)

        row_def = defs["modRow"]
        self.assertEqual(row_def["version"], 2)
        self.assertTrue(row_def["ignoreMousePresses"])
        self.assertEqual(row_def["backgroundData"]["unfocussed"]["colour"], style.native_color("transparent"))

        # 6 child interactive controls in row
        children = {c["componentData"]["name"]: c for c in row_def["componentsData"]}
        self.assertEqual(set(children.keys()), {"AMOUNT", "POLARITY", "ROUTE", "SOURCE", "TARGET", "MORE"})

        # Seven independent instances instantiated using the shared definition
        field_base = 2896
        nav_base = 2944
        instances = []
        for r in range(7):
            instance = nmr.build_mod_row_instance(
                f"Mod Native Row {r + 1}",
                r,
                (66, 102 + r * 72, 1198, 56),
                row_type="modRow",
                slot_handle=2889 + r,
                amount_handle=field_base + r * 3 + 0,
                polarity_handle=field_base + r * 3 + 1,
                route_handle=field_base + r * 3 + 2,
                source_handle=nav_base + 4 + r * 3 + 0,
                target_handle=nav_base + 4 + r * 3 + 1,
                more_handle=nav_base + 4 + r * 3 + 2,
                gates=[f"IndexedEnabling/1/5/Parameter 2888", f"RangedEnabling/1/64/65/Parameter {2889 + r}"],
                caption=f"{r + 1:02d}",
            )
            instances.append(instance)

        self.assertEqual(len(instances), 7)
        self.assertTrue(all(inst["componentData"]["type"] == "modRow" for inst in instances))
        self.assertTrue(all(inst["bounds"]["acceptsHWFocus"] == "Children" for inst in instances))

    def test_resolve_every_instance_handles_independently(self):
        """Every instance resolves its 6 handles independently with zero cross-instance pollution."""
        field_base = 2896
        nav_base = 2944

        instances = [
            nmr.build_mod_row_instance(
                f"Row {r + 1}",
                r,
                (66, 102 + r * 72, 1198, 56),
                slot_handle=2889 + r,
                amount_handle=field_base + r * 3 + 0,
                polarity_handle=field_base + r * 3 + 1,
                route_handle=field_base + r * 3 + 2,
                source_handle=nav_base + 4 + r * 3 + 0,
                target_handle=nav_base + 4 + r * 3 + 1,
                more_handle=nav_base + 4 + r * 3 + 2,
                caption=f"{r + 1:02d}",
            )
            for r in range(7)
        ]

        # Check independent handle bindings per row
        for r, inst in enumerate(instances):
            remap = {m["key"]: m["value"] for m in inst["handle remapping"]["map"]}
            self.assertEqual(remap["Data"], f"Parameter {2889 + r}")
            self.assertEqual(remap["Caption"], f"WithName[{r + 1:02d}]/Parameter {2889 + r}")
            self.assertEqual(remap["AMOUNT"], f"Parameter {field_base + r * 3 + 0}")
            self.assertEqual(remap["POLARITY"], f"Parameter {field_base + r * 3 + 1}")
            self.assertEqual(remap["ROUTE"], f"Parameter {field_base + r * 3 + 2}")
            self.assertEqual(remap["Bypass"], f"Parameter {field_base + r * 3 + 2}")
            self.assertEqual(remap["SOURCE"], f"Parameter {nav_base + 4 + r * 3 + 0}")
            self.assertEqual(remap["Source"], f"Parameter {nav_base + 4 + r * 3 + 0}")
            self.assertEqual(remap["TARGET"], f"Parameter {nav_base + 4 + r * 3 + 1}")
            self.assertEqual(remap["Target"], f"Parameter {nav_base + 4 + r * 3 + 1}")
            self.assertEqual(remap["EDIT"], f"Parameter {nav_base + 4 + r * 3 + 2}")
            self.assertEqual(remap["Edit"], f"Parameter {nav_base + 4 + r * 3 + 2}")
            self.assertEqual(remap["MORE"], f"Parameter {nav_base + 4 + r * 3 + 2}")
            self.assertEqual(remap["OpenMore"], f"Parameter {nav_base + 4 + r * 3 + 2}")

    def test_real_slider_56px_input_rectangle_and_touch_targets(self):
        """Slider input rectangle must be exactly 56px high (not 16px). All interactive controls >= 56px."""
        defs = nmr.build_mod_row_definitions(self.skins_dir)

        # 1. Bar cell definition
        bar_def = defs["modNative_bar_508x56"]
        components = {c["componentData"]["name"]: c for c in bar_def["componentsData"]}
        self.assertIn("amount", components)
        self.assertIn("value", components)

        amount_comp = components["amount"]
        self.assertEqual(amount_comp["componentData"]["type"], "Slider")
        slider_bounds = [int(v) for v in amount_comp["bounds"]["bounds"].split()]
        self.assertEqual(slider_bounds[3], 56, "Real native Slider hitbox must be 56px high")
        self.assertEqual(amount_comp["bounds"]["acceptsHWFocus"], "Yes")

        slider_data = amount_comp["componentData"]["data"]
        self.assertEqual(slider_data["sliderType"], "TrackAndThumb")
        self.assertEqual(slider_data["direction"], "Horizontal")
        self.assertTrue(slider_data["isBipolar"])
        self.assertEqual(slider_data["revealType"], "Bipolar")
        self.assertFalse(slider_data["thumbTrackVisible"])

        # 2. Check all interactive row children are at least 56px in height
        row_def = defs["modNativeRowNormal"]
        for child in row_def["componentsData"]:
            name = child["componentData"]["name"]
            c_type = child["componentData"]["type"]
            c_rect = [int(v) for v in child["bounds"]["bounds"].split()]
            self.assertGreaterEqual(c_rect[3], 56, f"{name} height {c_rect[3]} < 56")
            child_def = defs[c_type]
            for leaf in child_def["componentsData"]:
                leaf_rect = [int(v) for v in leaf["bounds"]["bounds"].split()]
                if leaf["componentData"]["type"] == "Slider":
                    self.assertEqual(leaf_rect[3], 56, f"Leaf slider {name} height must be 56px")
                elif name in ("SOURCE", "TARGET"):
                    self.assertEqual(leaf["bounds"]["acceptsHWFocus"], "No")
                    self.assertFalse(child_def["ignoreMousePresses"])
                    self.assertEqual([action["onAction"] for action in child_def["actions"]],
                                     ["Mouse Down", "Enter Pressed"])

    def test_row_before_crop_and_after_render_assets(self):
        """Verify mod_row_before is real crop with Q badges and after render is fullwidth 1280."""
        nmr.write_assets(self.skins_dir)

        before_file = self.skins_dir / "mod_row_before.png"
        self.assertTrue(before_file.is_file())
        with Image.open(before_file) as before_im:
            self.assertEqual(before_im.size, (1280, 72))
            # Verify yellow pixels exist in historical before crop (Q badges)
            pixels = before_im.load()
            yellow_pixels = [
                (x, y)
                for y in range(before_im.height)
                for x in range(before_im.width)
                if pixels[x, y][0] > 200 and pixels[x, y][1] > 180 and pixels[x, y][2] < 100
            ]
            self.assertGreater(len(yellow_pixels), 100, "Historical crop must preserve yellow Q badges")

        after_file = self.skins_dir / "mod_row_after.png"
        self.assertTrue(after_file.is_file())
        with Image.open(after_file) as after_im:
            self.assertEqual(after_im.size, (1280, 56))

        comparison_file = self.skins_dir / "mod_row_before_after.png"
        self.assertTrue(comparison_file.is_file())
        with Image.open(comparison_file) as comp_im:
            self.assertEqual(comp_im.size, (1280, 238))

    def test_column_headers_canonical_centering(self):
        """Header centers must match canonical column centers on both axes at y=83."""
        self.assertEqual(style.size("mod_matrix.header_center_source"), 154)
        self.assertEqual(style.size("mod_matrix.header_center_target"), 390)
        self.assertEqual(style.size("mod_matrix.header_center_amount"), 772)
        self.assertEqual(style.size("mod_matrix.header_center_polarity"), 1068)
        self.assertEqual(style.size("mod_matrix.header_center_state"), 1146)
        self.assertEqual(style.size("mod_matrix.header_center_y"), 83)

    def test_slider_track_ticks_and_background_stretch(self):
        """No bar background stretch into value field; dedicated track Image child component; EDIT asset."""
        defs = nmr.build_mod_row_definitions(self.skins_dir)
        bar_def = defs["modNative_bar_508x56"]

        # 1. Panel and track are independent assets
        self.assertEqual(bar_def["backgroundData"]["unfocussed"]["image"], "mod_amount_508x56.png")

        # 2. Dedicated child Image component for track background
        components = {c["componentData"]["name"]: c for c in bar_def["componentsData"]}
        self.assertIn("track_bg", components)
        track_bg_comp = components["track_bg"]
        self.assertEqual(track_bg_comp["componentData"]["type"], "Image")
        tb_bounds = [int(v) for v in track_bg_comp["bounds"]["bounds"].split()]
        self.assertEqual(tb_bounds, [10, 0, 392, 56])
        self.assertEqual(track_bg_comp["componentData"]["data"]["image"], "mod_slider_track_392x56.png")

        # 3. Dedicated action EDIT asset must be generated with width 76
        nmr.write_assets(self.skins_dir)
        edit_file = self.skins_dir / "mod_action_edit.png"
        self.assertTrue(edit_file.is_file())
        with Image.open(edit_file) as edit_im:
            self.assertEqual(edit_im.size, (76, 56))

        # 4. Track image exists and has a center tick taller than the rail
        track_file = self.skins_dir / "mod_slider_track_392x56.png"
        self.assertTrue(track_file.is_file())
        with Image.open(track_file) as track_im:
            self.assertEqual(track_im.size, (392, 56))
            self.assertGreater(track_im.getpixel((196, 20))[3], 100)
            self.assertEqual(track_im.getpixel((100, 20))[3], 0)
        with Image.open(self.skins_dir / "mod_slider_thumb_56.png") as thumb:
            self.assertEqual(thumb.size, (16, 56))
            self.assertGreater(thumb.getpixel((8, 28))[0], 225)
            self.assertLess(thumb.getpixel((0, 20))[3], 5)

    def test_detail_screen_components_and_assets(self):
        """Detail screen: 90px dial filmstrips, 3-column furniture, Back/Delete assets, contract rects."""
        import json

        # 1. Dial diameter must be 90px
        self.assertEqual(style.size("dial.diameter"), 90)

        # 2. Contract must define detail and detail_navigation rects
        contract_path = REPO_ROOT / "scripts" / "mpc-skin-extras" / "mod-native-contract.json"
        self.assertTrue(contract_path.is_file())
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        self.assertEqual(len(contract["detail"]), 12)
        self.assertIn("detail_navigation", contract)
        det_nav = contract["detail_navigation"]
        self.assertIn("back", det_nav)
        self.assertIn("delete", det_nav)
        self.assertEqual(det_nav["back"], [66, 4, 100, 56])
        self.assertEqual(det_nav["delete"], [1120, 4, 120, 56])

        # The actual MOD controls must reuse the artwork installed for the shared dials.
        for kind, filename in (("dial_uni", "play_sub_level_90.png"), ("dial_bi", "play_sub_pan_90.png")):
            definition = nmr.build_editor_cell_definition(kind, 140, 160, skins_dir=self.skins_dir)
            knob = next(child["componentData"]["data"] for child in definition["componentsData"]
                        if child["componentData"]["type"] == "Knob")
            self.assertEqual(knob["filmStrip"], filename)
            with Image.open(self.skins_dir / filename) as image:
                self.assertEqual(image.size, (90, 90 * knob["numFrames"]))

        # 4. Mod detail handles and confirm presentation handle
        import native_ui_params as handles
        self.assertEqual(handles.MOD_DETAIL_HANDLES, 6)
        self.assertEqual(handles.MOD_DETAIL_CONFIRM, handles.MOD_DETAIL_CONTROL + 5)


if __name__ == "__main__":
    unittest.main()
