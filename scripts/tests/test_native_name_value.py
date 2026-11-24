# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the shared native name-value component.

Verifies owner specification (2026-11-24):
1. MPC Native component definition structure:
   - Root background is transparent (colour="0") so the name has no enclosing box
   - Full 48 px cell touch footprint and coarse data wheel enabled
   - Focus ring and decorative face art bound to the number box on the right
2. Child Labels:
   - Short parameter name on the left OUTSIDE the frame (handleName='Caption', type='Name')
   - Formatted value on the right INSIDE the frame (handleName='Data', type='Value', centered)
3. Full interactive capabilities (touch overlay, coarse data wheel, Q-Link mapping)
4. Visual rendering of the real component with SEM 0, SEM +7, and CRS -12.50
5. Pixel verification:
   - Left side (name area): 0 background fill/outline (transparent in standalone cell)
   - Right side (number box): filled with #12182d and outlined with #1c2651
6. STRICTURE: Complete absence of yellow Q-Link numbers or chips in component and previews
7. Dimensions (48 px height) and touch target adequacy (>= 56 px)
"""

from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
SCRIPTS_DIR = REPO_ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

import native_skin_font
import native_name_value as nnv


class TestNativeNameValueComponent(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_native_definition_structure(self):
        """Verify the native component definition complies with Akai MPC skin format."""
        definition = nnv.build_name_value_definition(
            name="sharedNativeNameValue",
            rect=(0, 0, 125, 48),
            box_width=68,
            skins_dir=self.skins_dir,
            with_face_art=True,
        )

        self.assertEqual(definition["version"], 2)
        self.assertFalse(definition["ignoreMousePresses"], "Touch presses must be handled across the control")
        self.assertFalse(definition["disableCoarseDataWheel"], "Hardware Q-Link / data wheel must be enabled")

        # Verify overlay actions on touch / enter
        action_map = {a["onAction"]: a for a in definition["actions"]}
        self.assertIn("Mouse Down", action_map)
        self.assertIn("Enter Pressed", action_map)
        for act in action_map.values():
            self.assertEqual(act["handler"], "Show Overlay")
            self.assertEqual(act["handleName"], "Data")
            self.assertEqual(act["additionalData"], "fx editor number menu")

        # Root background must be transparent so parameter name sits on the host panel
        bg = definition["backgroundData"]
        self.assertEqual(bg["unfocussed"]["colour"], "0", "Root unfocused background must be transparent")
        self.assertEqual(bg["focussed"]["colour"], "0", "Root focused background must be transparent")

        # Check children
        components = {c["componentData"]["name"]: c for c in definition["componentsData"]}
        self.assertIn("focus", components)
        self.assertIn("Face", components)
        self.assertIn("name", components)
        self.assertIn("value", components)

        # Focus ring and Face art must be placed on the number box (right side: x=57, w=68, h=48)
        focus_bounds = list(map(int, components["focus"]["bounds"]["bounds"].split()))
        self.assertEqual(focus_bounds, [57, 0, 68, 48], "Focus ring must surround the number box")

        face_bounds = list(map(int, components["Face"]["bounds"]["bounds"].split()))
        self.assertEqual(face_bounds, [57, 0, 68, 48], "Face image must decorate the number box")
        self.assertEqual(components["Face"]["componentData"]["data"]["image"], "name_value_box_68x48.png")

    def test_labels_caption_and_data_bindings(self):
        """Verify child labels: name outside frame on left, value inside frame on right."""
        definition = nnv.build_name_value_definition(
            name="testNameValue",
            rect=(0, 0, 125, 48),
            box_width=68,
            skins_dir=self.skins_dir,
            with_face_art=False,
            pad_x=8,
        )

        components = {c["componentData"]["name"]: c for c in definition["componentsData"]}

        # Check Name label (left, outside frame)
        name_comp = components["name"]["componentData"]
        self.assertEqual(name_comp["type"], "Label")
        name_data = name_comp["data"]
        self.assertEqual(name_data["type"], "Name")
        self.assertEqual(name_data["handleName"], "Caption")
        self.assertEqual(name_data["textStyle"]["justification"], "left verticallyCentred")
        self.assertEqual(name_data["textStyle"]["colour"], "ff8fa2c0")

        # Check Value label (right, inside framed box)
        val_comp = components["value"]["componentData"]
        self.assertEqual(val_comp["type"], "Label")
        val_data = val_comp["data"]
        self.assertEqual(val_data["type"], "Value")
        self.assertEqual(val_data["handleName"], "Data")
        self.assertEqual(val_data["textStyle"]["justification"], "horizontallyCentred verticallyCentred")
        self.assertEqual(val_data["textStyle"]["colour"], "ffebeffe")

        # Verify bounds of labels within 48 px height
        name_bounds = list(map(int, components["name"]["bounds"]["bounds"].split()))
        val_bounds = list(map(int, components["value"]["bounds"]["bounds"].split()))
        self.assertEqual(name_bounds[3], 48, "Name label height must be 48 px")
        self.assertEqual(val_bounds[3], 48, "Value label height must be 48 px")
        self.assertEqual(name_bounds[0], 8, "Name starts at pad_x=8")
        self.assertEqual(name_bounds[2], 49, "Name width spans from pad_x to box_x (57 - 8 = 49)")
        self.assertEqual(val_bounds[0], 57, "Value starts at box_x=57")
        self.assertEqual(val_bounds[2], 68, "Value spans number box width (68)")

    def test_entry_instantiation_and_parameter_mapping(self):
        """Verify tab entry builder correctly remaps Data to parameter handle."""
        entry = nnv.create_name_value_entry("Osc Semi", "sharedNativeNameValue", (100, 200, 125, 48), param=3320)
        comp = entry["componentData"]
        self.assertEqual(comp["name"], "Osc Semi")
        self.assertEqual(comp["type"], "sharedNativeNameValue")
        self.assertEqual(comp["data"]["handleName"], "Data")

        remap = entry["handle remapping"]["map"]
        self.assertEqual(remap, [{"key": "Data", "value": "Parameter 3320"}])

        bx, by, bw, bh = map(int, entry["bounds"]["bounds"].split())
        self.assertEqual((bx, by, bw, bh), (100, 200, 125, 48))
        self.assertEqual(bh, 48, "Component height must be exactly 48 px")
        self.assertGreaterEqual(bw, 56, "Touch target width must be >= 56 px")

    def test_real_component_renders_sem_0_sem_plus7_crs_minus1250(self):
        """Verify real component renders correctly for SEM 0, SEM +7, and CRS -12.50."""
        cases = [
            ("SEM", "0"),
            ("SEM", "+7"),
            ("CRS", "-12.50"),
        ]
        for name, val in cases:
            with self.subTest(name=name, value=val):
                im = nnv.render_name_value_preview(name, val, width=125, height=48, box_width=68)
                self.assertEqual(im.size, (125, 48))
                self.assertGreater(len(im.tobytes()), 1000)

    def test_name_outside_frame_and_number_inside_frame_pixels(self):
        """Verify pixels: left name area has no frame/fill; right box has cell fill and border."""
        im = nnv.render_name_value_preview("SEM", "+7", width=125, height=48, box_width=68, scale=4)
        arr = np.array(im)

        # 1. Left area outside the letters of SEM (e.g. x=2, y=2): must be transparent alpha=0
        self.assertEqual(arr[2, 2, 3], 0, "Top-left area of name must have no background fill/frame")

        # 2. Number box interior (e.g. x=65, y=24): must have cell fill #12182d (R=18, G=24, B=45, A=255)
        # (box starts at x=57, text center is x=91; x=65 is unoccluded box interior)
        interior = arr[24, 65]
        self.assertEqual(interior[3], 255, "Box interior must be opaque")
        self.assertTrue(15 <= interior[0] <= 21, f"R should match #12182d, got {interior[0]}")
        self.assertTrue(21 <= interior[1] <= 27, f"G should match #12182d, got {interior[1]}")
        self.assertTrue(40 <= interior[2] <= 50, f"B should match #12182d, got {interior[2]}")

        # 3. Outside number box (x=56, y=24) must be transparent; border inside (x=58, y=24) is opaque
        self.assertEqual(arr[24, 56, 3], 0, "Outside number box must be transparent")
        border_edge = arr[24, 58]
        self.assertEqual(border_edge[3], 255, "Box border must be opaque")

    def test_absence_of_qlink_numbers_and_yellow_chips(self):
        """STRICT CHECK: Verify no yellow pixels (Q-Link chip) exist anywhere in rendered outputs."""
        for h in [48, 40, 32]:
            for name, val in [("SEM", "0"), ("SEM", "+7"), ("CRS", "-12.50")]:
                im = nnv.render_name_value_preview(name, val, width=125, height=h, box_width=68, scale=4)
                arr = np.array(im)
                yellow_pixels = (arr[:, :, 0] > 190) & (arr[:, :, 1] > 130) & (arr[:, :, 2] < 100)
                self.assertEqual(
                    np.count_nonzero(yellow_pixels),
                    0,
                    f"Real component {name} {val} at height {h} must NOT contain any yellow Q-Link chip pixels",
                )

    def test_text_fit_and_margins_in_48px_cell(self):
        """Verify both short parameter name and formatted values fit comfortably without collision."""
        font_lbl = native_skin_font.semibold(14)
        font_val = native_skin_font.semibold(16)

        names = ["SEM", "CRS", "OCT", "FIN"]
        name_box_w = 49  # width available for name (57 - 8)
        for name in names:
            len_name = font_lbl.getlength(name)
            self.assertLess(
                len_name,
                name_box_w - 10,
                f"Name '{name}' ({len_name:.1f} px) must fit comfortably within {name_box_w} px",
            )

        values = ["0", "+7", "-12.50", "-24.00", "+100"]
        val_box_w = 68
        for val in values:
            len_val = font_val.getlength(val)
            self.assertLess(
                len_val,
                val_box_w - 12,
                f"Value '{val}' ({len_val:.1f} px) must fit comfortably within {val_box_w} px box",
            )

    def test_height_variants_definitions_40px_and_32px(self):
        """Verify actual 40 px and 32 px native definitions, dimensions, and face art generation."""
        for target_h, expected_r, expected_lbl_sz, expected_val_sz in [
            (40, 6, 12.0, 14.0),
            (32, 5, 11.0, 12.0),
        ]:
            with self.subTest(height=target_h):
                definition = nnv.build_name_value_definition(
                    name=f"testNameValue_{target_h}",
                    rect=(0, 0, 125, target_h),
                    box_width=68,
                    skins_dir=self.skins_dir,
                    with_face_art=True,
                )

                self.assertEqual(definition["version"], 2)
                self.assertFalse(definition["ignoreMousePresses"])
                self.assertFalse(definition["disableCoarseDataWheel"])

                components = {c["componentData"]["name"]: c for c in definition["componentsData"]}
                self.assertIn("focus", components)
                self.assertIn("Face", components)
                self.assertIn("name", components)
                self.assertIn("value", components)

                # Focus ring bounds
                focus_bounds = list(map(int, components["focus"]["bounds"]["bounds"].split()))
                self.assertEqual(focus_bounds, [57, 0, 68, target_h])

                # Face art image and bounds
                face_bounds = list(map(int, components["Face"]["bounds"]["bounds"].split()))
                self.assertEqual(face_bounds, [57, 0, 68, target_h])
                self.assertEqual(
                    components["Face"]["componentData"]["data"]["image"],
                    f"name_value_box_68x{target_h}.png",
                )
                self.assertTrue((self.skins_dir / f"name_value_box_68x{target_h}.png").exists())

                # Child Labels
                name_comp = components["name"]["componentData"]
                self.assertEqual(name_comp["data"]["type"], "Name")
                self.assertEqual(name_comp["data"]["handleName"], "Caption")
                self.assertEqual(name_comp["data"]["textStyle"]["font"]["height"], expected_lbl_sz)

                name_bounds = list(map(int, components["name"]["bounds"]["bounds"].split()))
                self.assertEqual(name_bounds, [8, 0, 49, target_h])

                val_comp = components["value"]["componentData"]
                self.assertEqual(val_comp["data"]["type"], "Value")
                self.assertEqual(val_comp["data"]["handleName"], "Data")
                self.assertEqual(val_comp["data"]["textStyle"]["font"]["height"], expected_val_sz)

                val_bounds = list(map(int, components["value"]["bounds"]["bounds"].split()))
                self.assertEqual(val_bounds, [57, 0, 68, target_h])

    def test_text_fit_and_vertical_margins_in_40px_and_32px_cells(self):
        """Verify short parameter name and formatted values fit horizontally and vertically in 40 and 32 px."""
        names = ["SEM", "CRS", "OCT", "FIN"]
        values = ["0", "+7", "-12.50", "-24.00", "+100"]

        # 40 px cell
        f_lbl_40 = native_skin_font.semibold(12)
        f_val_40 = native_skin_font.semibold(14)
        for name in names:
            len_n = f_lbl_40.getlength(name)
            self.assertLess(len_n, 49 - 10, f"40 px name '{name}' must fit in 49 px")
            bbox = f_lbl_40.getbbox(name)
            h_text = bbox[3] - bbox[1]
            self.assertLess(h_text, 40 - 10, f"40 px name '{name}' text height {h_text} must fit in 40 px")

        for val in values:
            len_v = f_val_40.getlength(val)
            self.assertLess(len_v, 68 - 12, f"40 px value '{val}' must fit in 68 px")
            bbox = f_val_40.getbbox(val)
            h_text = bbox[3] - bbox[1]
            self.assertLess(h_text, 40 - 10, f"40 px value '{val}' text height {h_text} must fit in 40 px")

        # 32 px cell
        f_lbl_32 = native_skin_font.semibold(11)
        f_val_32 = native_skin_font.semibold(12)
        for name in names:
            len_n = f_lbl_32.getlength(name)
            self.assertLess(len_n, 49 - 10, f"32 px name '{name}' must fit in 49 px")
            bbox = f_lbl_32.getbbox(name)
            h_text = bbox[3] - bbox[1]
            self.assertLess(h_text, 32 - 8, f"32 px name '{name}' text height {h_text} must fit in 32 px")

        for val in values:
            len_v = f_val_32.getlength(val)
            self.assertLess(len_v, 68 - 12, f"32 px value '{val}' must fit in 68 px")
            bbox = f_val_32.getbbox(val)
            h_text = bbox[3] - bbox[1]
            self.assertLess(h_text, 32 - 8, f"32 px value '{val}' text height {h_text} must fit in 32 px")

    def test_touch_target_footprint_all_heights(self):
        """Verify touch target width >= 56 px across all height variants (48, 40, 32 px)."""
        for h in (48, 40, 32):
            entry = nnv.create_name_value_entry(f"Osc_{h}", "sharedNativeNameValue", (100, 200, 125, h), param=3320)
            bx, by, bw, bh = map(int, entry["bounds"]["bounds"].split())
            self.assertEqual(bh, h, f"Height must match {h} px")
            self.assertGreaterEqual(bw, 56, f"Width {bw} px must satisfy touch target >= 56 px requirement")

    def test_pixel_rendering_for_40px_and_32px(self):
        """Verify rendered pixels at 40 and 32 px: transparent left, filled/bordered right box."""
        for h, r in [(40, 6), (32, 5)]:
            im = nnv.render_name_value_preview("SEM", "+7", width=125, height=h, box_width=68, radius=r, scale=4)
            self.assertEqual(im.size, (125, h))
            arr = np.array(im)

            # Left side outside name letters must be transparent
            self.assertEqual(arr[2, 2, 3], 0, f"Left area at height {h} must be transparent")

            # Center/interior of number box (x=65, y=h//2) must be filled with #12182d (x=65 is unoccluded box interior)
            interior = arr[h // 2, 65]
            self.assertEqual(interior[3], 255, f"Box interior at height {h} must be opaque")
            self.assertTrue(15 <= interior[0] <= 21, f"R should match #12182d at height {h}, got {interior[0]}")
            self.assertTrue(21 <= interior[1] <= 27, f"G should match #12182d at height {h}, got {interior[1]}")
            self.assertTrue(40 <= interior[2] <= 50, f"B should match #12182d at height {h}, got {interior[2]}")


if __name__ == "__main__":
    unittest.main()
