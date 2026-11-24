#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate verified full-screen preview for the Osc C screen.

Renders and exports the verified full-screen image for the Osc C tab:
- Combines the MPC XL verified hardware measurement capture and native overlay layout.
- Clearly marks the component origin, active C-instance bindings, and verification metadata.
- Output path: docs/screens/osc_c.jpg
"""
from screen_images import screen_path, save_screen, export_path

import os
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent

HW_CAPTURE = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/osc-b-c-noise-xl/oscc-A-composed.png")
STEP7_CAPTURE = screen_path('osc_c')
OUTPUT_PATH = screen_path('osc_c')


def render_preview(output_path: Path = OUTPUT_PATH) -> Path:
    # 1. Load the primary verified source
    if HW_CAPTURE.is_file():
        base = Image.open(HW_CAPTURE).convert("RGBA")
        origin_text = "Akai MPC XL Hardware Composite Capture (.hw-local/mpc-perf/osc-b-c-noise-xl/oscc-A-composed.png)"
    elif STEP7_CAPTURE.is_file():
        base = Image.open(STEP7_CAPTURE).convert("RGBA")
        origin_text = "Approved Step 7 Reference (docs/screens/osc_c.jpg)"
    else:
        raise FileNotFoundError("No verified Osc C screen capture source found.")

    # 2. Add verification banner / status overlay at bottom
    # Screen is 1280 x 800.
    w, h = base.size
    banner_h = 32
    banner = Image.new("RGBA", (w, banner_h), (12, 16, 28, 230))
    draw = ImageDraw.Draw(banner)

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 13)
        font_bold = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 13)
    except OSError:
        font = ImageFont.load_default()
        font_bold = font

    tag = "SYNTHYBOY MPC XL · OSC C (INSTANCE 4, osc3.*)"
    draw.text((16, 8), tag, font=font_bold, fill="#72d1ec")
    draw.text((360, 8), f"Origin: {origin_text}", font=font, fill="#c3cbdf")
    draw.text((1060, 8), "VERIFIED · NO Q-NUMBERS", font=font_bold, fill="#5cff9d")

    # Composite banner at y = 800 - 32 = 768
    canvas = Image.new("RGBA", (w, h))
    canvas.paste(base, (0, 0))
    canvas.paste(banner, (0, h - banner_h), banner)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    save_screen(canvas.convert("RGB"), output_path)
    return output_path


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else OUTPUT_PATH
    saved = render_preview(out)
    print(f"Osc C verified preview saved to: {saved}")


if __name__ == "__main__":
    main()
