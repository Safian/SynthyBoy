#!/usr/bin/env python3
# SynthyBoy - generates the two spectrum column pictures for the MPC skin. Deterministic; needs only the standard library.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/gen-mpc-spectrum-images.py [--out DIR]   (default DIR: scripts/mpc-skin-extras/spectrum-images, next to this script)
#
# Writes into DIR:
#   spectrum_off.png   every pixel (42, 41, 44, 255)
#   spectrum_on.png    vertical gradient from (244, 63, 110) at top to (211, 17, 69) at bottom, alpha 255

import argparse
import struct
import sys
import zlib
from pathlib import Path

WIDTH = 30
HEIGHT = 136
FILES = ("spectrum_off.png", "spectrum_on.png")

TOP = (244, 63, 110)
BOTTOM = (211, 17, 69)
OFF = (42, 41, 44, 255)


def _chunk(kind, body):
    crc = zlib.crc32(kind + body) & 0xffffffff
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)


def _make_ihdr():
    return _chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))


def _make_idat(raw):
    return _chunk(b"IDAT", zlib.compress(raw, 9))


def _make_iend():
    return _chunk(b"IEND", b"")


def _build_png(rows):
    """rows: list of WIDTH-length tuples of (R, G, B, A)."""
    stride = 1 + 4 * WIDTH
    raw = b""
    for row in rows:
        raw += b"\x00"  # filter byte 0
        for r, g, b, a in row:
            raw += struct.pack("BBBB", r, g, b, a)
    return b"\x89PNG\r\n\x1a\n" + _make_ihdr() + _make_idat(raw) + _make_iend()


def off_png():
    row = [OFF] * WIDTH
    return _build_png([row] * HEIGHT)


def on_png():
    rows = []
    for y in range(HEIGHT):
        t = y / (HEIGHT - 1)
        rgb = tuple(int(a + (b - a) * t + 0.5) for a, b in zip(TOP, BOTTOM))
        pixel = rgb + (255,)
        rows.append([pixel] * WIDTH)
    return _build_png(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate MPC spectrum images")
    parser.add_argument("--out", default=None, help="output directory")
    args = parser.parse_args(argv)

    out_dir = Path(args.out) if args.out else (Path(__file__).resolve().parent / "mpc-skin-extras" / "spectrum-images")
    out_dir.mkdir(parents=True, exist_ok=True)

    for name, func in zip(FILES, (off_png, on_png)):
        (out_dir / name).write_bytes(func())

    return 0


if __name__ == "__main__":
    sys.exit(main())
