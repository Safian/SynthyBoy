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

    # Ensure required toggle assets referenced by native switch components exist in Plugin Skins
    import os
    for toggle_name in ("toggle_on.png", "toggle_off.png"):
        dst = ctx.skins_dir / toggle_name
        if not dst.is_file():
            sdk = os.environ.get("MPC_SDK")
            candidates = [
                Path(sdk) / "ui-kit/mpc-red" / toggle_name if sdk else None,
                Path("/Users/szabi/.local/share/local-worker/ext/mpc-sdk/ui-kit/mpc-red") / toggle_name,
                Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk/ui-kit/mpc-red") / toggle_name,
            ]
            copied = False
            for c in candidates:
                if c and c.is_file():
                    shutil.copyfile(c, dst)
                    copied = True
                    break
            if not copied:
                from PIL import Image, ImageDraw
                im = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
                draw = ImageDraw.Draw(im)
                is_on = "on" in toggle_name
                fill = (46, 230, 240, 255) if is_on else (21, 26, 45, 255)
                outline = (38, 48, 75, 255)
                draw.rounded_rectangle([0, 0, 63, 31], radius=16, fill=fill, outline=outline, width=1)
                thumb_x0 = 34 if is_on else 2
                draw.ellipse([thumb_x0, 2, thumb_x0 + 27, 29], fill=(230, 230, 230, 255))
                im.save(dst)
