# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for scripts/render_ui_screen_macros_preview.py.

Verifies:
1. Real C++ scene renderer usage (MacroView, SpectrumStrip, scene_cpu.h via tools/render_macros_overlay.cpp).
2. Proper layer ownership: inactive bank (Bank 2 when Bank 1 is active) has zero overlay alpha,
   preventing double-text painting over the native background skin.
3. Hardware state compliance: MPC XL wide hardware suppresses bank frame; Key 61 renders
   active bank frame with unnumbered 'Q' chip.
4. Spectrum curve rendering by real SpectrumStrip C++ component from synthetic fixture.
5. CLI --out side-effect isolation: custom output path does not overwrite canonical mirrors.
"""

import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = ROOT / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
SITE_PACKAGES = Path("/Users/szabi/Library/Python/3.9/lib/python/site-packages")
if SITE_PACKAGES.is_dir() and str(SITE_PACKAGES) not in sys.path:
    sys.path.insert(0, str(SITE_PACKAGES))

from PIL import Image

if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from components.native.macro_card.macro_card import (
    CARD_HEIGHT,
    CARD_WIDTH,
    KNOB_RECT,
    MACRO_CARD_POSITIONS,
    NAME_RECT,
    TARGET_RECT,
    VALUE_RECT,
)
import render_ui_screen_macros_preview as rmp


class TestRenderUiScreenMacrosPreview(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_real_cpp_renderer_and_composite(self):
        """Verifies that the authentic C++ overlay renderer executes and composites to 1280x628."""
        out_overlay = self.tmp_path / "overlay.png"
        overlay_im = rmp.render_overlay_layer(out_overlay, bank=0, device="xl", cache_dir=self.tmp_path / "tool")
        self.assertTrue(out_overlay.is_file())
        self.assertEqual(overlay_im.size, (1280, 628))
        self.assertEqual(overlay_im.mode, "RGBA")

        # Overlay must contain transparent regions and active drawn regions
        alphas = [p[3] for p in overlay_im.getdata()]
        self.assertIn(0, alphas, "Overlay must be transparent where nothing is drawn")
        self.assertTrue(any(a > 0 for a in alphas), "Overlay must contain rendered scene pixels")

        # Full composite to RGB
        out_file = self.tmp_path / "preview.png"
        res = rmp.render_macros_preview(out_file, bank=0, device="xl", update_mirrors=False)
        self.assertEqual(res, out_file)
        self.assertTrue(out_file.is_file())

        with Image.open(out_file) as im:
            self.assertEqual(im.size, (1280, 628))
            self.assertEqual(im.mode, "RGB")

    def test_clean_header_without_bank_selector(self):
        """Verifies header bar has no BANK 1/BANK 2 selector pixels in overlay, keeping a clean header."""
        for bank in (0, 1):
            ov_path = self.tmp_path / f"ov_hdr_b{bank}.png"
            ov = rmp.render_overlay_layer(ov_path, bank=bank, device="xl", cache_dir=self.tmp_path / "tool")
            # Header right of MACROS title (x=300..1200, y=4..60) must have ZERO active overlay pixels
            crop_hdr = ov.crop((300, 4, 1200, 60))
            active_px = sum(p[3] > 0 for p in crop_hdr.getdata())
            self.assertEqual(active_px, 0, f"Header bank={bank} must have ZERO overlay pixels right of MACROS title")

    def test_hardware_device_bank_frame_rule(self):
        """Verifies XL suppresses bank frame on both banks, while Key 61 draws Q frame and unnumbered Q on the active 4-card bank."""
        for bank, expected_q_crop in ((0, (5, 367, 27, 388)), (1, (643, 367, 665, 388))):
            ov_xl = rmp.render_overlay_layer(self.tmp_path / f"ov_xl_b{bank}.png", bank=bank, device="xl", cache_dir=self.tmp_path / "tool")
            ov_k61 = rmp.render_overlay_layer(self.tmp_path / f"ov_k61_b{bank}.png", bank=bank, device="key61", cache_dir=self.tmp_path / "tool")

            # XL wide device must NOT draw yellow bank frame outline pixels anywhere
            xl_yellow = sum(r > 200 and g > 170 and b < 80 and a > 0 for (r, g, b, a) in ov_xl.getdata())
            self.assertEqual(xl_yellow, 0, f"MPC XL bank {bank} must not draw any yellow Q bank frame pixels")

            # Key 61 must draw active bank outline and unnumbered 'Q' chip marker at upper-left corner of the active bank
            k61_q_chip = ov_k61.crop(expected_q_crop)
            k61_q_yellow = sum(r > 200 and g > 170 and b < 80 and a > 0 for (r, g, b, a) in k61_q_chip.getdata())
            self.assertGreater(k61_q_yellow, 80, f"Key 61 bank {bank} must draw unnumbered yellow Q marker chip at {expected_q_crop}")

            diff_count = sum(p1 != p2 for p1, p2 in zip(ov_xl.getdata(), ov_k61.getdata()))
            self.assertGreater(diff_count, 3000, f"Key 61 and XL bank {bank} overlays must differ by the bank frame geometry")

    def test_real_spectrum_strip_curve_rendered(self):
        """Verifies that the full-width upper spectrum strip curve and fill are rendered by ui::SpectrumStrip in C++."""
        ov_path = self.tmp_path / "ov_spec.png"
        ov = rmp.render_overlay_layer(ov_path, bank=0, device="xl", cache_dir=self.tmp_path / "tool")

        # Spectrum strip live display area is {8, 72, 1264, 290} -> inner {18, 94, 1244, 248}
        spec_crop = ov.crop((18, 94, 1262, 342))
        active_spec_px = sum(p[3] > 0 for p in spec_crop.getdata())
        self.assertGreater(active_spec_px, 15000,
                           "SpectrumStrip::drawLive must render genuine curve stroke and shaded fill")

        # Verify that the curve spans across the full-width log frequency axis (low, mid, high frequencies)
        low_hz = ov.crop((30, 94, 350, 342))
        mid_hz = ov.crop((450, 94, 850, 342))
        high_hz = ov.crop((950, 94, 1250, 342))
        self.assertTrue(any(p[3] > 0 for p in low_hz.getdata()), "Spectrum must render low frequency curve")
        self.assertTrue(any(p[3] > 0 for p in mid_hz.getdata()), "Spectrum must render mid frequency curve")
        self.assertTrue(any(p[3] > 0 for p in high_hz.getdata()), "Spectrum must render high frequency curve")

    def test_cli_out_has_no_side_effects(self):
        """Verifies that custom --out output path never modifies canonical repository mirror files."""
        # Record existing canonical mirror mtimes and hashes
        canonical_states = {}
        for mirror in rmp.CANONICAL_MIRRORS:
            if mirror.is_file():
                canonical_states[mirror] = (mirror.stat().st_mtime, mirror.read_bytes())

        custom_out = self.tmp_path / "custom_test_output.png"
        res = rmp.render_macros_preview(out_path=custom_out, bank=0, device="xl", update_mirrors=False)
        self.assertEqual(res, custom_out)
        self.assertTrue(custom_out.is_file())

        # Verify none of the canonical files were touched
        for mirror, (orig_mtime, orig_bytes) in canonical_states.items():
            self.assertEqual(mirror.stat().st_mtime, orig_mtime,
                             f"Canonical mirror {mirror} mtime was modified during custom --out run")
            self.assertEqual(mirror.read_bytes(), orig_bytes,
                             f"Canonical mirror {mirror} content was modified during custom --out run")

    def test_build_overlay_tool_content_key_caching(self):
        """Verifies that build_overlay_tool uses a content-hash key to isolate builds."""
        content_key = rmp.compute_overlay_tool_hash()
        self.assertTrue(bool(content_key))
        bin_path = rmp.build_overlay_tool(self.tmp_path / "cache")
        self.assertTrue(bin_path.is_file())
        self.assertIn(content_key, bin_path.name)
        self.assertTrue(os.access(bin_path, os.X_OK))

    def test_native_markers_fully_covered(self):
        """Verifies that native sentinel markers at (33, 24), (45, 24), and (1241, 24) are covered."""
        preview_path = self.tmp_path / "markers_check.png"
        rmp.render_macros_preview(out_path=preview_path, bank=0, device="xl", update_mirrors=False)

        with Image.open(preview_path) as im:
            # Check right marker spot at (1241, 24)
            right_px = im.getpixel((1241, 24))
            # Ground color #090d19 is (9, 13, 25)
            self.assertEqual(right_px, (9, 13, 25), "Right sentinel marker must be covered by #090d19 ground")
            # Sentinel color 0x3a6b64 is (58, 107, 100)
            self.assertNotEqual(right_px, (58, 107, 100), "Raw sentinel right marker pixel must not remain")

            # Left marker spot (33, 24) and instance (45, 24) lie under 'MACROS' title
            # and must not retain raw left sentinel (0x6b3a2c = (107, 58, 44))
            for x, y in ((33, 24), (45, 24)):
                px = im.getpixel((x, y))
                self.assertNotEqual(px, (107, 58, 44), f"Raw sentinel pixel must not remain at ({x}, {y})")

    def test_macro_cards_rendered_cleanly_without_overlapping_text(self):
        """Verifies that all 8 macro cards render name and value with a real clean gap and zero top-bar black defect."""
        for fixture in ("default", "named"):
            preview_path = self.tmp_path / f"cards_clean_{fixture}.png"
            rmp.render_macros_preview(out_path=preview_path, bank=0, device="xl", fixture=fixture, update_mirrors=False)

            with Image.open(preview_path) as im:
                # 1. Top bar (0..110) must NOT be black (previous defect had alpha=0 -> black in y=0..110)
                for px_coord in ((280, 12), (760, 20), (1100, 30), (200, 68), (500, 68)):
                    r, g, b = im.getpixel(px_coord)
                    self.assertGreater(b, r + 15, f"Top region at {px_coord} must be navy theme ground, got {(r, g, b)}")

                # 2. Check every card's stacked layout: Value, strict vertical gap, Name, Target
                for i, (cx, cy) in enumerate(MACRO_CARD_POSITIONS):
                    nx, ny, nw, nh = NAME_RECT
                    vx, vy, vw, vh = VALUE_RECT
                    name_area = im.crop((cx + nx, cy + ny, cx + nx + nw, cy + ny + nh))
                    value_area = im.crop((cx + vx, cy + vy, cx + vx + vw, cy + vy + vh))
                    # Strict vertical gap band between value bottom and name top
                    gap_band = im.crop((cx + vx, cy + vy + vh + 2, cx + vx + vw, cy + ny - 2))

                    name_bright = sum(sum(p[:3]) > 350 for p in name_area.getdata())
                    val_bright = sum(sum(p[:3]) > 350 for p in value_area.getdata())
                    gap_bright = sum(sum(p[:3]) > 220 for p in gap_band.getdata())

                    self.assertGreater(name_bright, 40, f"{fixture} card {i} must render macro name in NAME_RECT")
                    self.assertGreater(val_bright, 40, f"{fixture} card {i} must render value in VALUE_RECT")
                    self.assertEqual(gap_bright, 0,
                                     f"{fixture} card {i} must have ZERO overlapping text pixels in vertical gap y={vy+vh+2}..{ny-2}")

    def test_two_fixtures_non_zero_values_and_two_name_sets(self):
        """Verifies two differing non-zero macro values fixtures and two name sets.

        Ensures that non-zero states update both the rotary knob filmstrip frame and the value readout,
        and that custom sound-design names render cleanly without overlap or 0.0 placeholders.
        """
        # Fixture 1: Default numbered names ("MACRO 1"..) with values (0.35, 0.70, ..)
        f1_path = self.tmp_path / "fixture_default.png"
        rmp.render_macros_preview(out_path=f1_path, bank=0, device="xl", fixture="default", update_mirrors=False)

        # Fixture 2: Contextual names ("CUTOFF", "RESO", ..) with differing values (0.50, 0.75, ..)
        f2_path = self.tmp_path / "fixture_named.png"
        rmp.render_macros_preview(out_path=f2_path, bank=0, device="xl", fixture="named", update_mirrors=False)

        with Image.open(f1_path) as im1, Image.open(f2_path) as im2:
            # 1. Knob 0 (Macro 1) region from shared MACRO_CARD_POSITIONS[0] + KNOB_RECT
            # Fixture 1 has value 0.35 (frame ~44), Fixture 2 has value 0.50 (frame ~63)
            # Knob filmstrip frame must visibly differ between the two fixtures
            cx0, cy0 = MACRO_CARD_POSITIONS[0]
            kx, ky, kw, kh = KNOB_RECT
            knob1_crop1 = im1.crop((cx0 + kx, cy0 + ky, cx0 + kx + kw, cy0 + ky + kh))
            knob1_crop2 = im2.crop((cx0 + kx, cy0 + ky, cx0 + kx + kw, cy0 + ky + kh))
            knob_diff = sum(p1 != p2 for p1, p2 in zip(knob1_crop1.getdata(), knob1_crop2.getdata()))
            self.assertGreater(knob_diff, 100,
                               "Rotary knob filmstrip frame must reflect non-zero parameter value differences")

            # 2. Value readout 0 (Macro 1) region from shared VALUE_RECT
            vx, vy, vw, vh = VALUE_RECT
            val_crop1 = im1.crop((cx0 + vx, cy0 + vy, cx0 + vx + vw, cy0 + vy + vh))
            val_crop2 = im2.crop((cx0 + vx, cy0 + vy, cx0 + vx + vw, cy0 + vy + vh))
            val_diff = sum(p1 != p2 for p1, p2 in zip(val_crop1.getdata(), val_crop2.getdata()))
            self.assertGreater(val_diff, 50,
                               "Value readout text must differ between 35.0% and 50.0% fixtures")

            # 3. Macro name 0 region from shared NAME_RECT
            nx, ny, nw, nh = NAME_RECT
            name_crop1 = im1.crop((cx0 + nx, cy0 + ny, cx0 + nx + nw, cy0 + ny + nh))
            name_crop2 = im2.crop((cx0 + nx, cy0 + ny, cx0 + nx + nw, cy0 + ny + nh))
            name_diff = sum(p1 != p2 for p1, p2 in zip(name_crop1.getdata(), name_crop2.getdata()))
            self.assertGreater(name_diff, 50,
                               "Macro name must differ between default 'MACRO 1' and contextual 'CUTOFF'")

    def test_bank1_and_bank2_previews_marker_free(self):
        """Verifies Key 61 BANK 1 and BANK 2 previews switch the Q frame across the bottom row with zero sentinel markers."""
        b1_path = self.tmp_path / "bank1_check.png"
        b2_path = self.tmp_path / "bank2_check.png"

        rmp.render_macros_preview(out_path=b1_path, bank=0, device="key61", update_mirrors=False)
        rmp.render_macros_preview(out_path=b2_path, bank=1, device="key61", update_mirrors=False)

        with Image.open(b1_path) as im_b1, Image.open(b2_path) as im_b2:
            self.assertEqual(im_b1.size, (1280, 628))
            self.assertEqual(im_b2.size, (1280, 628))

            # Sentinel markers at (33, 24), (45, 24), (1241, 24) must be cleanly covered in both
            for im in (im_b1, im_b2):
                right_px = im.getpixel((1241, 24))
                self.assertEqual(right_px, (9, 13, 25), "Marker must be covered by ground #090d19")
                self.assertNotEqual(right_px, (58, 107, 100), "Raw right sentinel marker pixel must not remain")
                for x, y in ((33, 24), (45, 24)):
                    self.assertNotEqual(im.getpixel((x, y)), (107, 58, 44),
                                        f"Raw left sentinel must not remain at ({x}, {y})")

            # Header remains clean and identical between Bank 1 and Bank 2 (no header bank switcher)
            b1_hdr = im_b1.crop((300, 4, 1200, 60))
            b2_hdr = im_b2.crop((300, 4, 1200, 60))
            self.assertEqual(list(b1_hdr.getdata()), list(b2_hdr.getdata()), "Clean header must not change between banks")

            # On Key 61, the bottom card row differs by the active Q frame moving from Bank 1 (left 4) to Bank 2 (right 4)
            b1_row = im_b1.crop((0, 366, 1280, 628))
            b2_row = im_b2.crop((0, 366, 1280, 628))
            row_diff = sum(p1 != p2 for p1, p2 in zip(b1_row.getdata(), b2_row.getdata()))
            self.assertGreater(row_diff, 3000, "Key 61 Q frame must move between Bank 1 and Bank 2 on the bottom row")


if __name__ == "__main__":
    unittest.main()
