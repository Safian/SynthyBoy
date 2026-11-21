#!/usr/bin/env python3
# SynthyBoy - tests of scripts/render-ui-gallery.py (MPC overlay UI gallery renderer).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "render-ui-gallery.py"
LAYOUT_JSON = ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"

spec = importlib.util.spec_from_file_location("render_ui_gallery", SCRIPT)
gal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gal)


class TestRenderUiGallery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Locate host binary
        cls.host_bin = None
        env_host = os.environ.get("SYNTHYBOY_UI_GALLERY_HOST")
        if env_host and Path(env_host).is_file():
            cls.host_bin = Path(env_host)
            return

        for bdir in [ROOT / "build", ROOT / "build-test", ROOT / "build-mpc", ROOT / "build-native"]:
            for sub in ["shell-mpc", ""]:
                b = bdir / sub / "ui_gallery_host"
                if b.is_file():
                    cls.host_bin = b
                    break
            if cls.host_bin:
                break

    def test_layout_views_present_in_gallery(self):
        if not self.host_bin:
            raise unittest.SkipTest("No ui_gallery_host binary given or found (pass --host-bin or set SYNTHYBOY_UI_GALLERY_HOST)")

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

    def test_no_build_invoked_when_host_bin_not_given(self):
        """Assert that render-ui-gallery does not invoke cmake/ninja when no host binary is given."""
        # Case 1: Configured build dir exists with CMakeCache.txt
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_root = Path(tmp_dir)
            bdir = tmp_root / "build"
            bdir.mkdir()
            (bdir / "CMakeCache.txt").write_text("# mock CMakeCache without ui_gallery_host\n")

            with patch.object(gal.subprocess, "run") as mock_run:
                with self.assertRaises(RuntimeError):
                    gal.find_or_build_host(tmp_root)

                for call in mock_run.call_args_list:
                    cmd_args = call[0][0] if call[0] else []
                    cmd_str = " ".join(str(c) for c in cmd_args)
                    self.assertNotIn("cmake", cmd_str, f"cmake was unexpectedly invoked: {cmd_str}")
                    self.assertNotIn("ninja", cmd_str, f"ninja was unexpectedly invoked: {cmd_str}")
                self.assertEqual(mock_run.call_count, 0, "No build commands (cmake/ninja) should be executed")

        # Case 2: No configured build dir exists
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_root = Path(tmp_dir)
            with patch.object(gal.subprocess, "run") as mock_run:
                with self.assertRaises(RuntimeError):
                    gal.find_or_build_host(tmp_root)
                self.assertEqual(mock_run.call_count, 0, "No build commands (cmake/ninja) should be executed")


if __name__ == "__main__":
    unittest.main()
