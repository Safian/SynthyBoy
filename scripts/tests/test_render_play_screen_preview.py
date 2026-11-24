# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted verification for Play native snapshot, live C++ overlay fixture, and composite preview."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))
sys.path.insert(0, str(HERE))

import test_mpc_skin_extras as base
import render_play_native_snapshot as snap
import render_play_screen_preview as prev

SNAPSHOT_SCRIPT = REPO_ROOT / "scripts" / "render_play_native_snapshot.py"
PREVIEW_SCRIPT = REPO_ROOT / "scripts" / "render_play_screen_preview.py"
FIXTURE_SRC = REPO_ROOT / "shell-mpc" / "overlay" / "tests" / "play_render_fixture.cpp"
KOMP_PREVIEW = (Path(__file__).resolve().parents[2] / 'docs/screens/play.jpg')
MAP_PREVIEW = (Path(__file__).resolve().parents[2] / 'docs/screens/play.jpg')


def file_sha256(path: Path) -> str:
    """Calculate SHA256 hex digest of a file if it exists."""
    if not path.is_file():
        return ""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


@unittest.skipUnless((base.SDK / "tools/gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class PlayPreviewRendererTests(unittest.TestCase):
    """Test suite verifying Play native snapshot, live C++ overlay origin, and composite preview."""

    def test_shared_fixture_state_consistency(self):
        """Verify documented shared parameter fixture state has no placeholders and covers all 3 compact modules."""
        # 1. State flags verify compact view with Sub, Env 1, LFO 1 active and modals closed
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 3529"), 0, "Mod editor must be closed")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 3537"), 0, "Expanded editors must be closed")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 2676"), 0, "Play Osc must be Sub (0)")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 2677"), 0, "Play Env must be Env 1 (0)")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 2678"), 0, "Play LFO must be LFO 1 (0)")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 1558"), 1, "Sub power must be ON")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 3312"), 1, "Sub compact sub-state must be active")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 2943"), 1, "LFO 1 must be standard LFO")
        self.assertEqual(snap.FIXTURE_STATE.get("Parameter 3302"), 1, "LFO 1 rate mode must be Hz")

        # 2. Check resolved fixture parameter metadata
        # All required labels must exist and contain NO placeholder text ("plugin parameter", "NAME", "0.0")
        required_keys = [
            "Parameter 2676",  # Play Osc
            "Parameter 2677",  # Play Env
            "Parameter 2678",  # Play LFO
            "Parameter 2685",  # Sub Pan
            "Parameter 2686",  # Sub Level
            "Parameter 2687",  # Attack
            "Parameter 2689",  # Decay
            "Parameter 2690",  # Sustain
            "Parameter 2691",  # Release
            "Parameter 2695",  # Rate
            "Parameter 2696",  # Rise
            "Parameter 2698",  # Time
            "Parameter 3305",  # SHAPE
            "Parameter 3306",  # RATE
            "Parameter 3311",  # Play LFO Choose
            "Parameter 3316",  # Sub WAVE
            "Parameter 3317",  # Sub OCT
            "Parameter 3318",  # Sub PAN
            "Parameter 3319",  # Sub LEVEL
            "sub.waveform",
            "sub.octave",
            "lfo.shape",
            "lfo.rate",
        ]
        for key in required_keys:
            self.assertIn(key, snap.FIXTURE_PARAMS, f"Missing fixture param {key}")
            param = snap.FIXTURE_PARAMS[key]
            name = param.get("name", "")
            val = param.get("value", "")
            self.assertNotIn("plugin parameter", name.lower(), f"Placeholder in param name: {key}")
            self.assertNotEqual(name, "NAME", f"Unresolved placeholder NAME: {key}")
            if key != "sub.octave":  # 0 octave is a legitimate value
                self.assertNotEqual(val, "0.0", f"Unresolved placeholder value 0.0: {key}")

    def test_play_native_snapshot_generates_valid_image(self):
        """Verify Play native snapshot runs, outputs 1280x628 image, and respects Rule 5b."""
        with tempfile.NamedTemporaryFile(suffix=".png") as tmp:
            res = subprocess.run([sys.executable, str(SNAPSHOT_SCRIPT), "--out", tmp.name],
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Snapshot failed: {res.stderr}")
            with Image.open(tmp.name) as im:
                self.assertEqual(im.size, (1280, 628))
                self.assertIn(im.mode, ("RGB", "RGBA"))

    def test_play_native_sketch_filtering_and_rule5b(self):
        """Verify PlaySketch filters out inactive components and enforces Rule 5b (0 Q-badges)."""
        # Ensure PlaySketch enforces draw_qlink_overlay = False
        self.assertFalse(snap.PlaySketch.draw_qlink_overlay, "Rule 5b violation: draw_qlink_overlay must be False")

        # Test component activity filtering against fixture state
        sketch = snap.PlaySketch.__new__(snap.PlaySketch)

        # Inactive components must be filtered out
        self.assertFalse(sketch.is_visible({"additionalInvalidatingHandles": ["RangedEnabling/1/1/handle/Parameter 3537"]}),
                         "Expanded editor component must be inactive")
        self.assertFalse(sketch.is_visible({"additionalInvalidatingHandles": ["RangedEnabling/1/1/handle/Parameter 3529"]}),
                         "Modulation editor component must be inactive")
        self.assertFalse(sketch.is_visible({"additionalInvalidatingHandles": ["IndexedEnabling/0/handle/Parameter 3312"]}),
                         "Osc A compact controls must be inactive when Sub is selected")
        self.assertFalse(sketch.is_visible({"additionalInvalidatingHandles": ["IndexedEnabling/4/handle/Parameter 3312"]}),
                         "Noise compact controls must be inactive when Sub is selected")

        # Active components must pass
        self.assertTrue(sketch.is_visible({"additionalInvalidatingHandles": ["IndexedEnabling/1/handle/Parameter 3312"]}),
                        "Sub compact controls must be active")
        self.assertTrue(sketch.is_visible({"additionalInvalidatingHandles": ["IndexedEnabling/1/handle/Parameter 2943"]}),
                        "Standard LFO controls must be active")

    def test_knob_aspect_ratio_and_image_filmstrip_slicing(self):
        """Verify own blue FilmStrip knobs, strict knob_uni.png rejection, 1:1 aspect ratio, and single Image frame."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            ui_dir = snap.generate_ui(tmp)
            skins_dir = snap.generate_skins(tmp, ui_dir)
            snap.apply_extras(skins_dir, ui_dir)
            sketch = snap.PlaySketch(skins_dir, json.loads(snap.PARAMS_JSON.read_text())["params"])

            # 0. Factory red knob_uni.png must be strictly rejected
            with self.assertRaises(RuntimeError):
                sketch.get_knob_frame("knob_uni.png", 0.265, (60, 60))

            # Every Play dial definition must be FilmStrip (zero GenericLegacy)
            for key in ("compactNativeDial", "compactNativeRiseDial", "compactNativeSmoothTime",
                        "playEnvLfoRate", "playSubPan", "playSubLevel", "playOscPan"):
                knobs = [c for c in sketch.defs[key]["componentsData"] if c["componentData"]["type"] == "Knob"]
                self.assertEqual(len(knobs), 1, key)
                kdata = knobs[0]["componentData"]["data"]
                self.assertEqual(kdata["knobType"], "FilmStrip", f"{key} must use FilmStrip, not GenericLegacy")
                self.assertNotEqual(kdata.get("filmStrip"), "knob_uni.png", key)
                self.assertTrue((skins_dir / kdata["filmStrip"]).is_file(), f"Missing strip for {key}")

            # 1. RATE knob holder is 171x36, and the square blue filmstrip must remain 1:1 (not elliptical)
            rate_knob = sketch.get_knob_frame("play_sub_level_60.png", 0.265, (171, 36))
            self.assertIsNotNone(rate_knob)
            self.assertEqual(rate_knob.width, rate_knob.height, "RATE dial must preserve 1:1 square aspect ratio")
            self.assertLessEqual(rate_knob.width, 36)

            # 2. Rendered snapshot must draw Overlay Instance play1 as a single 6x6 frame at (45, 24),
            # with NO vertical 33-frame strip leaking down over the header or Sub power button.
            rendered = sketch.render("Play").convert("RGB")
            for y in range(36, 140):
                px = rendered.getpixel((47, y))
                self.assertNotIn(px, ((20, 200, 216), (20, 20, 216), (200, 20, 216)),
                                 f"Instance filmstrip leaked vertically at (47, {y}): {px}")

    def test_env_single_label_owner_and_knob_values(self):
        """Verify blue own knobs (0 red pixels), single caption/value labels, OCT caption, and unclipped headers."""
        with tempfile.NamedTemporaryFile(suffix=".png") as tmp:
            res = subprocess.run([sys.executable, str(SNAPSHOT_SCRIPT), "--out", tmp.name],
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Snapshot failed: {res.stderr}")
            with Image.open(tmp.name) as raw_im:
                im = raw_im.convert("RGB")
                env_cells = [
                    (446, 386, 192, 108),  # Attack
                    (642, 386, 192, 108),  # Decay
                    (446, 510, 192, 108),  # Sustain
                    (642, 510, 192, 108),  # Release
                ]
                for idx, (cx, cy, cw, ch) in enumerate(env_cells):
                    # Background corner of each dial cell must be native #060913
                    self.assertEqual(im.getpixel((cx + cw - 4, cy + 4)), (6, 9, 19),
                                     f"Missing native background fill in ENV cell {(cx, cy, cw, ch)}")
                    # Verify no fallback cyan label pixels remain in the caption zone
                    caption_crop = im.crop((cx + 96, cy + 32, cx + 188, cy + 54))
                    cyan_pixels = sum(1 for r, g, b in caption_crop.getdata() if b > 180 and r < 140 and g > 140)
                    self.assertEqual(cyan_pixels, 0, f"Duplicate cyan fallback label pixels in ENV cell {(cx, cy)}: {cyan_pixels}")
                    self.assertGreater(sum(1 for r, g, b in caption_crop.getdata() if b > 140), 15,
                                       f"Missing caption pixels in ENV cell {(cx, cy)}")
                    # Verify own blue knob in (cx+2, cy+9, 90, 90) with ZERO factory red knob pixels
                    knob_crop = im.crop((cx + 2, cy + 9, cx + 92, cy + 99))
                    red_pixels = sum(1 for r, g, b in knob_crop.getdata() if r > 180 and g < 60 and b < 80)
                    self.assertEqual(red_pixels, 0, f"Factory red knob pixels found in ENV cell {(cx, cy)}")
                    blue_pixels = sum(1 for r, g, b in knob_crop.getdata() if b > r + 30 and b > 70)
                    self.assertGreater(blue_pixels, 150, f"Missing own blue knob arc/track pixels in ENV cell {(cx, cy)}")
                    if idx > 0:  # Decay, Sustain, Release are non-zero: must have active #4c8dff arc
                        active_blue = sum(1 for r, g, b in knob_crop.getdata() if b > 200 and g > 100 and r < 120)
                        self.assertGreater(active_blue, 50, f"Missing active blue arc in ENV cell {(cx, cy)}")
                    # Verify bright value pixels exist on the right under the caption (cx+96..cx+188, cy+54..cy+86)
                    val_crop = im.crop((cx + 96, cy + 54, cx + 188, cy + 86))
                    bright_pixels = sum(1 for r, g, b in val_crop.getdata() if r > 180 and g > 180 and b > 180)
                    self.assertGreater(bright_pixels, 15, f"Missing value readout pixels in ENV cell {(cx, cy)}")

                # Verify LFO RATE and Rise own blue knobs + zero red pixels + value readouts
                for lx, ly, lw in ((864, 458, 182), (864, 546, 196)):
                    knob_crop = im.crop((lx + 2, ly + 6, lx + 62, ly + 66))
                    red_pixels = sum(1 for r, g, b in knob_crop.getdata() if r > 180 and g < 60 and b < 80)
                    self.assertEqual(red_pixels, 0, f"Factory red knob pixels found at {(lx, ly)}")
                    blue_pixels = sum(1 for r, g, b in knob_crop.getdata() if b > r + 30 and b > 70)
                    self.assertGreater(blue_pixels, 80, f"Missing own blue knob pixels at {(lx, ly)}")
                    val_crop = im.crop((lx + 68, ly + 34, lx + lw - 4, ly + 62))
                    self.assertGreater(sum(1 for r, g, b in val_crop.getdata() if r > 180 and g > 180 and b > 180), 10)

                # Verify compact Sub WAVE (14, 404, 292, 72) and name_value OCT (310, 412, 112, 56; 40px visual at y=420..460)
                wave_val = im.crop((68, 433, 274, 462))
                self.assertGreater(sum(1 for r, g, b in wave_val.getdata() if r > 180 and g > 180 and b > 180), 15, "Missing Sub WAVE value")
                # OCT caption sits OUTSIDE the framed box on the left (x=318..354, y=420..460)
                oct_caption = im.crop((318, 420, 354, 460))
                self.assertGreater(sum(1 for r, g, b in oct_caption.getdata() if b > 140), 10, "Missing OCT caption outside box")
                # Left caption background must remain transparent/tab ground (NOT filled with #12182d box surface)
                self.assertNotEqual(im.getpixel((314, 424)), (18, 24, 45), "OCT caption must sit outside the framed box")
                # Framed box on right (x=354..422, y=420..460) has #12182d surface and bright centered value '0'
                self.assertEqual(im.getpixel((360, 424)), (18, 24, 45), "Missing name_value box surface fill in Sub OCT")
                oct_value = im.crop((354, 420, 422, 460))
                self.assertGreater(sum(1 for r, g, b in oct_value.getdata() if r > 180 and g > 180 and b > 180), 8, "Missing OCT value inside box")

                # Verify all three module headers have intact unclipped right chevrons
                for hx, hw in ((84, 338), (446, 388), (864, 338)):
                    right_chevron = im.crop((hx + hw - 26, 26, hx + hw - 10, 46))
                    self.assertGreater(sum(1 for r, g, b in right_chevron.getdata() if r > 70 or g > 70 or b > 70), 5,
                                       f"Missing/clipped right chevron in header {(hx, 10, hw, 56)}")

    def test_play_render_fixture_overlay_generation(self):
        """Verify live C++ Play overlay compiles, clears native apertures, and covers layout markers."""
        self.assertTrue(FIXTURE_SRC.is_file(), f"Missing C++ Play render fixture source {FIXTURE_SRC}")
        layout = json.loads((REPO_ROOT / "shell-mpc" / "overlay" / "overlay_layout.json").read_text())
        play_view = next(v for v in layout["views"] if v["name"] == "play1")

        with tempfile.NamedTemporaryFile(suffix=".png") as tmp:
            prev.render_cpp_overlay(Path(tmp.name))
            with Image.open(tmp.name) as overlay_im:
                self.assertEqual(overlay_im.size, (1280, 628))
                self.assertEqual(overlay_im.mode, "RGBA")

                # Native selector row interior must stay transparent (alpha == 0)
                self.assertEqual(overlay_im.getpixel((200, 20))[3], 0, "Overlay must be transparent on native selector row")

                # Every native button and control aperture must be 100% transparent in C++ overlay
                native_apertures = [
                    (18, 82, 82, 138),     # Sub power
                    (354, 82, 418, 138),   # Sub expand
                    (766, 82, 830, 138),   # Env expand
                    (1192, 82, 1256, 138), # LFO expand
                    (14, 404, 306, 476),   # Sub WAVE (292x72)
                    (310, 412, 422, 468),  # Sub OCT (112x56 touch aperture)
                    (14, 510, 216, 618),   # Sub PAN
                    (220, 510, 422, 618),  # Sub LEVEL
                    (446, 386, 638, 494),  # Env Attack
                    (642, 386, 834, 494),  # Env Decay
                    (446, 510, 638, 618),  # Env Sustain
                    (642, 510, 834, 618),  # Env Release
                    (864, 386, 1260, 442), # LFO SHAPE
                    (864, 458, 1046, 530), # LFO RATE
                    (1050, 466, 1260, 522),# LFO SYNC
                    (864, 546, 1060, 618), # LFO Rise
                    (1064, 546, 1260, 618),# LFO Smooth + Switch
                ]
                for x0, y0, x1, y1 in native_apertures:
                    ap_crop = overlay_im.crop((x0, y0, x1, y1))
                    non_zero = sum(1 for p in ap_crop.getdata() if p[3] != 0)
                    self.assertEqual(non_zero, 0, f"Overlay drew {non_zero} non-transparent pixels inside aperture {(x0, y0, x1, y1)}")

                # Layout markers and instance pixel must be covered by production native_marker_covers (#141a38)
                spots = [play_view["markers"][0], play_view["instance"], play_view["markers"][1]]
                for spot in spots:
                    cx = spot["x"] + spot["w"] // 2
                    cy = spot["y"] + spot["h"] // 2
                    px = overlay_im.getpixel((cx, cy))
                    self.assertEqual(px, (0x14, 0x1a, 0x38, 0xff), f"Marker spot {(cx, cy)} not covered properly: {px}")

                # Inside display boxes curve pixels exist (alpha > 0)
                osc_crop = overlay_im.crop((14, 74, 422, 366))
                osc_alphas = [p[3] for p in osc_crop.getdata() if p[3] > 0]
                self.assertGreater(len(osc_alphas), 5000, "Osc overlay must render curve pixels")

    def test_play_composite_preview_live_curves_and_apertures(self):
        """Verify composite preview integrates live C++ overlay curves, covers markers, and keeps native controls."""
        layout = json.loads((REPO_ROOT / "shell-mpc" / "overlay" / "overlay_layout.json").read_text())
        play_view = next(v for v in layout["views"] if v["name"] == "play1")

        with tempfile.NamedTemporaryFile(suffix=".png") as tmp:
            res = subprocess.run([sys.executable, str(PREVIEW_SCRIPT), "--out", tmp.name],
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Preview failed: {res.stderr}")
            with Image.open(tmp.name) as raw_im:
                im = raw_im.convert("RGBA")
                self.assertEqual(im.size, (1280, 628))

                # 1. Verify rich curve rendering in the three display areas (> 500 distinct colors from antialiasing)
                for i, box in enumerate(prev.DISPLAY_BOXES):
                    crop = im.crop(box)
                    unique_colors = len(set(crop.getdata()))
                    self.assertGreater(unique_colors, 500, f"Display box {i} lacks rich C++ curve rendering: {unique_colors} colors")

                # 2. Verify native apertures (buttons inside display headers are preserved)
                for i, ap in enumerate(prev.NATIVE_BUTTON_APERTURES):
                    ap_crop = im.crop(ap)
                    unique_colors = len(set(ap_crop.getdata()))
                    self.assertGreater(unique_colors, 50, f"Aperture {i} appears blank or occluded: {unique_colors} colors")

                # 3. Verify header markers are covered in the final composite (not left as raw marker colors)
                for spot in (play_view["markers"][0], play_view["instance"], play_view["markers"][1]):
                    cx = spot["x"] + spot["w"] // 2
                    cy = spot["y"] + spot["h"] // 2
                    self.assertEqual(im.getpixel((cx, cy)), (0x14, 0x1a, 0x38, 0xff),
                                     f"Raw marker pixel visible in final composite at {(cx, cy)}")

                # 4. Check native Sub PAN dial at (100, 560) is present and undisturbed
                pan_area_pixel = im.getpixel((100, 560))
                self.assertNotEqual(pan_area_pixel, (0, 0, 0, 180), "Phantom badge detected over control area")

    def test_build_dir_source_identity_check(self):
        """Verify build directory source identity validation rejects another checkout's CMakeCache."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            foreign_build = Path(tmp_dir)
            (foreign_build / "CMakeCache.txt").write_text(
                "CMAKE_HOME_DIRECTORY:INTERNAL=/tmp/some/other/checkout\n",
                encoding="utf-8",
            )
            with self.assertRaises(RuntimeError):
                prev.verify_build_dir_source(foreign_build)

        # Per-checkout default build directory must belong to REPO_ROOT
        prev.verify_build_dir_source(prev.default_fixture_build_dir())

    def test_custom_out_isolates_canonical_images(self):
        """Verify --out writes ONLY to specified file and does NOT touch canonical images."""
        komp_hash_before = file_sha256(KOMP_PREVIEW)
        map_hash_before = file_sha256(MAP_PREVIEW)

        with tempfile.NamedTemporaryFile(suffix=".png") as tmp:
            res = subprocess.run([sys.executable, str(PREVIEW_SCRIPT), "--out", tmp.name],
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Preview failed: {res.stderr}")
            self.assertTrue(Path(tmp.name).is_file())
            self.assertGreater(Path(tmp.name).stat().st_size, 50000)

        komp_hash_after = file_sha256(KOMP_PREVIEW)
        map_hash_after = file_sha256(MAP_PREVIEW)

        self.assertEqual(komp_hash_before, komp_hash_after, "Canonical komponensek image was touched by --out")
        self.assertEqual(map_hash_before, map_hash_after, "Canonical ui-map image was touched by --out")


if __name__ == "__main__":
    unittest.main()
