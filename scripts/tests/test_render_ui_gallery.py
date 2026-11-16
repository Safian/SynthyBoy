#!/usr/bin/env python3
# SynthyBoy - tests of scripts/render-ui-gallery.py (MPC overlay UI gallery renderer).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "render-ui-gallery.py"
LAYOUT_JSON = ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"


class TestRenderUiGallery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Locate host binary
        cls.host_bin = None
        for bdir in [ROOT / "build", ROOT / "build-test", ROOT / "build-mpc"]:
            for sub in ["shell-mpc", ""]:
                b = bdir / sub / "ui_gallery_host"
                if b.is_file():
                    cls.host_bin = b
                    break
            if cls.host_bin:
                break

    def test_layout_views_present_in_gallery(self):
        with open(LAYOUT_JSON, "r", encoding="utf-8") as f:
            layout = json.load(f)
        views = layout.get("views", [])
        self.assertGreaterEqual(len(views), 21, "overlay_layout.json must contain at least 21 views")

        with tempfile.TemporaryDirectory() as tmp_dir:
            cmd = [
                sys.executable, "-I", str(SCRIPT),
                "--out", tmp_dir,
                "--presets", str(ROOT / "content" / "presets" / "Bass" / "scream_filter_bass.sbpreset"),
            ]
            if self.host_bin:
                cmd.extend(["--host-bin", str(self.host_bin)])

            res = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT))
            self.assertEqual(res.returncode, 0, f"Script failed with output:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}")

            out_path = Path(tmp_dir)
            index_file = out_path / "index.html"
            self.assertTrue(index_file.is_file(), "index.html must be generated in output directory")
            index_content = index_file.read_text(encoding="utf-8")

            png_files = list(out_path.glob("*.png"))
            self.assertGreater(len(png_files), 0, "Gallery must generate PNG files")

            # Check that every view from overlay_layout.json has at least one picture
            missing_views = []
            for v in views:
                v_name = v["name"]
                found = any(v_name in p.name for p in png_files)
                if not found:
                    missing_views.append(v_name)
            self.assertEqual(missing_views, [], f"Every view of overlay_layout.json must have at least one picture. Missing: {missing_views}")

            # Check that every picture is 1280x800 and not empty (not a single colour)
            for png_file in png_files:
                self.assertIn(png_file.name, index_content, f"index.html must reference {png_file.name}")
                if HAVE_PIL:
                    with Image.open(png_file) as im:
                        self.assertEqual(im.size, (1280, 800), f"{png_file.name} must be 1280x800")
                        colors = len(set(im.getdata()))
                        self.assertGreater(colors, 1, f"{png_file.name} must not be a single colour (found {colors} colors)")
                else:
                    self.assertGreater(png_file.stat().st_size, 1000, f"{png_file.name} is too small to be valid")


if __name__ == "__main__":
    unittest.main()
