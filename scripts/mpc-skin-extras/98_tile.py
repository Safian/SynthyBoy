# SynthyBoy - MPC skin extra: copies the browser artwork tile (Plugin Skins/browser_images/soundsmode.png).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The MPC Sounds > INSTRUMENTS browser looks for a 270x110 PNG at
# <plugin folder>/Plugin Skins/browser_images/soundsmode.png.
# When present, the host displays it as an artwork tile in the plugin selector.

import shutil
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "browser_images"
TILE_NAME = "soundsmode.png"
EXPECTED_SIZE = (270, 110)


def png_size(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    return struct.unpack(">II", data[16:24])


def apply(tui, qlinks, ctx):
    src = ASSETS / TILE_NAME
    if not src.is_file():
        raise FileNotFoundError(f"{src} is missing (run python3 scripts/gen-mpc-tile.py)")

    size = png_size(src)
    if size != EXPECTED_SIZE:
        raise ValueError(f"{src} is {size[0]}x{size[1]}, expected {EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}")

    target_dir = ctx.skins_dir / "browser_images"
    target_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, target_dir / TILE_NAME)
