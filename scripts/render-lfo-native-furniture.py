# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render native LFO backgrounds from the existing production layout."""
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "scripts/mpc-skin-extras/overlay-backgrounds"


def render():
    with tempfile.TemporaryDirectory(prefix="lfo-native-furniture-") as temp:
        temp = Path(temp)
        tool = temp / "render"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I", str(ROOT / "core/include"),
                        str(ROOT / "tools/render-lfo-native-furniture.cpp"), "-o", str(tool)], check=True)
        subprocess.run([str(tool), str(temp)], check=True)
        for layout in (1, 3, 4, 6):
            filename = f"lfo_native_furniture_{layout}"
            with Image.open(temp / (filename + ".ppm")) as image:
                # Preserve the existing LFO detector sentinel underneath widgets.
                image.paste((0x0b, 0x10, 0x29), (900, 2, 905, 7))
                image.save(ASSETS / (filename + ".png"), optimize=True)


if __name__ == "__main__":
    render()
