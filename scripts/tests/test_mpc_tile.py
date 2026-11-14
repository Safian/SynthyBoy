#!/usr/bin/env python3
# SynthyBoy - tests of the MPC browser artwork tile (scripts/gen-mpc-tile.py, 98_tile.py, Plugin Skins/browser_images/soundsmode.png).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
GEN_SCRIPT = ROOT / "scripts" / "gen-mpc-tile.py"
COMMITTED_TILE = ROOT / "scripts" / "mpc-skin-extras" / "browser_images" / "soundsmode.png"
TILE_EXTRA = ROOT / "scripts" / "mpc-skin-extras" / "98_tile.py"

WIDTH, HEIGHT = 270, 110


def png_dimensions(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a valid PNG")
    return struct.unpack(">II", data[16:24])


class TestMpcTile(unittest.TestCase):
    def test_committed_tile_asset_exists_and_format(self):
        """The committed asset must exist, be a valid PNG, and have exactly 270x110 dimensions."""
        self.assertTrue(COMMITTED_TILE.is_file(), f"Missing committed tile: {COMMITTED_TILE}")
        w, h = png_dimensions(COMMITTED_TILE)
        self.assertEqual((w, h), (WIDTH, HEIGHT), f"Expected {WIDTH}x{HEIGHT}, got {w}x{h}")
        self.assertGreater(COMMITTED_TILE.stat().st_size, 1000, "Tile PNG file is suspiciously small")

    def test_committed_tile_content_is_non_empty_and_own_artwork(self):
        """The tile must not be empty or a flat background, and must have valid own artwork components."""
        img = Image.open(COMMITTED_TILE)
        self.assertEqual(img.size, (WIDTH, HEIGHT))

        # Check color variety across image (not a monochrome dummy tile)
        colors = img.getcolors(maxcolors=WIDTH * HEIGHT)
        self.assertIsNotNone(colors)
        self.assertGreater(len(colors), 500, "Tile image lacks colour complexity / gradient")

        # Check left area (wordmark "SYNTHYBOY")
        wordmark_box = (16, 20, 160, 60)
        wordmark_crop = img.crop(wordmark_box)
        wordmark_colors = wordmark_crop.getcolors(maxcolors=WIDTH * HEIGHT)
        self.assertIsNotNone(wordmark_colors)
        self.assertGreater(len(wordmark_colors), 50, "Wordmark region appears blank")

        # Check right area (waveforms box)
        wave_box = (175, 20, 255, 90)
        wave_crop = img.crop(wave_box)
        wave_colors = wave_crop.getcolors(maxcolors=WIDTH * HEIGHT)
        self.assertIsNotNone(wave_colors)
        self.assertGreater(len(wave_colors), 100, "Waveform region appears blank")

        # Verify background corners are dark panel colors (no stray border artifact or white background)
        for corner in [(0, 0), (WIDTH - 1, 0), (0, HEIGHT - 1), (WIDTH - 1, HEIGHT - 1)]:
            pixel = img.getpixel(corner)
            r, g, b = pixel[:3]
            self.assertLess(r, 40)
            self.assertLess(g, 40)
            self.assertLess(b, 50)

    def test_generator_script_cli(self):
        """gen-mpc-tile.py can be executed via CLI to produce a 270x110 PNG and preview."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            out_png = Path(tmp_dir) / "test_tile.png"
            preview_png = Path(tmp_dir) / "preview.png"
            res = subprocess.run(
                [
                    sys.executable,
                    str(GEN_SCRIPT),
                    "--out",
                    str(out_png),
                    "--preview",
                    str(preview_png),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, f"Generator script failed: {res.stderr}")
            self.assertTrue(out_png.is_file())
            self.assertEqual(png_dimensions(out_png), (WIDTH, HEIGHT))
            self.assertTrue(preview_png.is_file())
            self.assertEqual(png_dimensions(preview_png), (WIDTH * 3, HEIGHT * 3))

    def test_skin_extra_98_tile_applies_cleanly(self):
        """98_tile.py copies the browser artwork tile into ctx.skins_dir/browser_images/soundsmode.png."""
        spec = importlib.util.spec_from_file_location("extra_98_tile", TILE_EXTRA)
        extra_mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(extra_mod)

        with tempfile.TemporaryDirectory() as tmp_dir:
            skins_dir = Path(tmp_dir) / "Plugin Skins"
            skins_dir.mkdir(parents=True)

            class FakeContext:
                def __init__(self, skins):
                    self.skins_dir = skins

            ctx = FakeContext(skins_dir)
            tui = {"tui": True}
            qlinks = {"qlinks": True}

            extra_mod.apply(tui, qlinks, ctx)

            # Verification
            target = skins_dir / "browser_images" / "soundsmode.png"
            self.assertTrue(target.is_file(), "98_tile did not copy soundsmode.png")
            self.assertEqual(png_dimensions(target), (WIDTH, HEIGHT))
            self.assertEqual(target.read_bytes(), COMMITTED_TILE.read_bytes())

            # Verify that tui and qlinks were not mutated
            self.assertEqual(tui, {"tui": True})
            self.assertEqual(qlinks, {"qlinks": True})

    def test_skin_extra_rejects_missing_or_corrupt_asset(self):
        """98_tile.py raises appropriate errors when source asset is invalid."""
        spec = importlib.util.spec_from_file_location("extra_98_tile_err", TILE_EXTRA)
        extra_mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(extra_mod)

        with tempfile.TemporaryDirectory() as tmp_dir:
            bad_assets = Path(tmp_dir) / "bad_assets"
            bad_assets.mkdir()
            extra_mod.ASSETS = bad_assets

            class FakeContext:
                def __init__(self, skins):
                    self.skins_dir = skins

            ctx = FakeContext(Path(tmp_dir) / "skins")

            # Missing file
            with self.assertRaises(FileNotFoundError):
                extra_mod.apply({}, {}, ctx)

            # Corrupt file (not a PNG)
            bad_file = bad_assets / "soundsmode.png"
            bad_file.write_bytes(b"NOT_A_PNG_FILE_AT_ALL")
            with self.assertRaises(ValueError):
                extra_mod.apply({}, {}, ctx)

            # Wrong size (e.g. 100x100)
            img = Image.new("RGB", (100, 100), (0, 0, 0))
            img.save(bad_file, format="PNG")
            with self.assertRaises(ValueError):
                extra_mod.apply({}, {}, ctx)


if __name__ == "__main__":
    unittest.main()
