"""Regenerate the factory Master, Voice, OSC, Mod, ENV, Mixer and LFO tab PNGs from their UI draw methods."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "scripts/mpc-skin-extras/overlay-backgrounds"


def render(destination):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        tool = tmp / "render"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I", str(ROOT / "core/include"),
                        str(ROOT / "tools/render-native-furniture.cpp"), "-o", str(tool)], check=True)
        subprocess.run([str(tool), str(tmp)], check=True)
        for page in ("master", "voice", "osc", "mod", "env", "mixer", "lfo"):
            with Image.open(tmp / f"{page}.ppm") as image:
                if page == "osc":
                    spec = importlib.util.spec_from_file_location("native_backgrounds", ASSETS.parent / "99_backgrounds.py")
                    backgrounds = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(backgrounds)
                    x, y, rgb = backgrounds.OSC_SENTINEL
                    image.paste(tuple(bytes.fromhex(rgb)), (x, y, x + 5, y + 5))
                image.save(destination / f"overlay_bg_{page}.png", optimize=True)


if __name__ == "__main__":
    render(ASSETS)
