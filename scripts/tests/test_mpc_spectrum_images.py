#!/usr/bin/env python3
# SynthyBoy - test of the two pictures of the spectrum columns (scripts/gen-mpc-spectrum-images.py, committed in scripts/mpc-skin-extras/spectrum-images/).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "gen-mpc-spectrum-images.py"
ASSETS = ROOT / "scripts" / "mpc-skin-extras" / "spectrum-images"
NAMES = ("spectrum_off.png", "spectrum_on.png")
WIDTH, HEIGHT = 30, 136
OFF = (42, 41, 44, 255)
TOP, BOTTOM = (244, 63, 110), (211, 17, 69)


def decode(data):
    """Chunk list and the RGBA rows of a PNG (colour type 6, 8 bit, no interlace, filter 0 only)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, chunks, idat = 8, [], b""
    ihdr = None
    while pos < len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body, crc = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n], data[pos + 8 + n:pos + 12 + n]
        assert struct.unpack(">I", crc)[0] == zlib.crc32(kind + body) & 0xffffffff, "bad CRC in %r" % kind
        chunks.append(kind)
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        if kind == b"IDAT":
            idat += body
        pos += 12 + n
    w, h, depth, ctype, comp, filt, interlace = ihdr
    raw = zlib.decompress(idat)
    stride = 1 + 4 * w
    assert len(raw) == h * stride
    rows = []
    for y in range(h):
        assert raw[y * stride] == 0, "filter byte of row %d is not 0" % y
        row = raw[y * stride + 1:(y + 1) * stride]
        rows.append([tuple(row[4 * x:4 * x + 4]) for x in range(w)])
    return chunks, ihdr, rows


def load_script():
    spec = importlib.util.spec_from_file_location("gen_mpc_spectrum_images", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def gradient(y):
    t = y / (HEIGHT - 1)
    return tuple(int(a + (b - a) * t + 0.5) for a, b in zip(TOP, BOTTOM)) + (255,)


class SpectrumImagesTest(unittest.TestCase):
    def test_script_exists(self):
        self.assertTrue(SCRIPT.is_file(), "scripts/gen-mpc-spectrum-images.py is missing")

    def test_module_constants_and_functions(self):
        m = load_script()
        self.assertEqual((m.WIDTH, m.HEIGHT), (WIDTH, HEIGHT))
        self.assertEqual(m.FILES, NAMES)
        self.assertIsInstance(m.off_png(), bytes)
        self.assertIsInstance(m.on_png(), bytes)

    def test_off_picture_is_the_dark_flat_colour(self):
        chunks, ihdr, rows = decode(load_script().off_png())
        self.assertEqual(chunks, [b"IHDR", b"IDAT", b"IEND"])
        self.assertEqual(ihdr, (WIDTH, HEIGHT, 8, 6, 0, 0, 0))
        self.assertTrue(all(p == OFF for row in rows for p in row))

    def test_on_picture_is_the_vertical_gradient(self):
        chunks, ihdr, rows = decode(load_script().on_png())
        self.assertEqual(chunks, [b"IHDR", b"IDAT", b"IEND"])
        self.assertEqual(ihdr, (WIDTH, HEIGHT, 8, 6, 0, 0, 0))
        for y, row in enumerate(rows):
            self.assertEqual(set(row), {gradient(y)}, "row %d" % y)
        self.assertEqual(rows[0][0], TOP + (255,))
        self.assertEqual(rows[-1][0], BOTTOM + (255,))

    def test_generation_is_deterministic(self):
        m = load_script()
        self.assertEqual(m.off_png(), m.off_png())
        self.assertEqual(m.on_png(), m.on_png())

    def test_committed_files_are_the_regenerated_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = subprocess.run([sys.executable, str(SCRIPT), "--out", tmp], capture_output=True, text=True)
            self.assertEqual((r.returncode, r.stderr), (0, ""))
            self.assertEqual(sorted(p.name for p in Path(tmp).iterdir()), sorted(NAMES))
            for name in NAMES:
                self.assertTrue((ASSETS / name).is_file(), "%s is not committed in scripts/mpc-skin-extras/spectrum-images/" % name)
                self.assertEqual((Path(tmp) / name).read_bytes(), (ASSETS / name).read_bytes(), name)

    def test_decoded_size(self):
        # 2 pictures of 30 x 136 pixels, 4 bytes each
        self.assertEqual(2 * WIDTH * HEIGHT * 4, 32640)


if __name__ == "__main__":
    unittest.main()
