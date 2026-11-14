#!/usr/bin/env python3
# SynthyBoy - generates the 270x110 browser artwork tile (soundsmode.png) for the MPC plugin selector.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage:
#   python3 scripts/gen-mpc-tile.py [--out <path|dir>] [--font <ttf>] [--preview <path>]
#
# Generates the 270x110 PNG artwork tile displayed in the MPC Sounds > INSTRUMENTS browser.
# Uses Pillow to draw a 4x supersampled vector/gradient canvas and downsamples with Lanczos
# filter for crisp rendering on high-density displays.
# Contains own artwork only: SynthyBoy wordmark, analog hybrid subtitle, glowing waveforms,
# and status indicators. No vendor, stock, or third-party assets (no Akai/AIR/NI).

import argparse
import math
import os
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "scripts" / "mpc-skin-extras" / "browser_images" / "soundsmode.png"

WIDTH = 270
HEIGHT = 110
SUPERSAMPLE = 4

# Visual palette (cyan -> purple -> magenta neon gradient on dark panel)
CY = (70, 230, 255)
PU = (150, 110, 255)
MG = (255, 70, 170)
BG_TOP = (14, 18, 30)
BG_BOTTOM = (8, 10, 18)
OUTLINE = (40, 48, 66)
WAVE_BG = (10, 13, 22)
WAVE_OUTLINE = (34, 42, 60)
SUBTITLE_COLOR = (150, 165, 190)
LED_LABEL_COLOR = (120, 132, 155)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def grad3(t):
    return lerp(CY, PU, t * 2) if t < 0.5 else lerp(PU, MG, (t - 0.5) * 2)


def resolve_font(custom_path=None):
    """Find a suitable font file honoring repo licensing (Titillium Web OFL if available, else repo Roboto)."""
    candidates = []
    if custom_path:
        candidates.append(Path(custom_path))
    if os.environ.get("MPC_TILE_FONT"):
        candidates.append(Path(os.environ["MPC_TILE_FONT"]))
    if os.environ.get("MPC_PREVIEW_FONTS"):
        candidates.append(Path(os.environ["MPC_PREVIEW_FONTS"]) / "TitilliumWeb-Bold.ttf")
    # Known local workstation font path
    local_titillium = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-community/open-mpc/tools/html_art/fonts/TitilliumWeb-Bold.ttf")
    if local_titillium.is_file():
        candidates.append(local_titillium)
    # Repo bundled font (Apache 2.0 license)
    repo_roboto = ROOT / "shell-clap" / "bitmap" / "fonts" / "Roboto-Bold.ttf"
    if repo_roboto.is_file():
        candidates.append(repo_roboto)

    for c in candidates:
        if c.is_file():
            return c
    return None


def render_tile(font_path=None, scale=SUPERSAMPLE):
    """Renders the 270x110 PIL Image."""
    s = scale
    w = WIDTH * s
    h = HEIGHT * s

    # Base gradient background
    img = Image.new("RGB", (w, h))
    px = ImageDraw.Draw(img)
    for y in range(h):
        px.line([(0, y), (w, y)], fill=lerp(BG_TOP, BG_BOTTOM, y / h))

    # Outer panel with subtle bevel
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([6 * s, 6 * s, w - 6 * s, h - 6 * s], radius=5 * s, outline=OUTLINE, width=s, fill=None)

    # Waveform display box on the right
    x0, x1, y0, y1 = 170 * s, 258 * s, 16 * s, 94 * s
    d.rounded_rectangle([x0, y0, x1, y1], radius=4 * s, fill=WAVE_BG, outline=WAVE_OUTLINE, width=s)

    # Multi-layered glowing waveforms
    glow = Image.new("RGB", (w, h), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    cy = (y0 + y1) / 2
    for k in range(5):
        pts = []
        for i in range(0, int(x1 - x0) - 4 * s, 2):
            t = i / (x1 - x0)
            x = x0 + 2 * s + i
            a = (0.30 + 0.10 * k) * (y1 - y0) / 2 * math.sin(math.pi * t)
            y = cy + a * math.sin(2 * math.pi * (1.2 + 0.5 * k) * t + k * 0.8) * (1 - 0.5 * abs(2 * t - 1))
            pts.append((x, y))
        col = grad3(k / 4)
        g.line(pts, fill=col, width=int(1.6 * s))

    blur = glow.filter(ImageFilter.GaussianBlur(3 * s))
    img = ImageChops.add(img, blur)
    img = ImageChops.add(img, glow)
    d = ImageDraw.Draw(img)

    # Resolve fonts
    f_path = resolve_font(font_path)
    if f_path:
        f_title = ImageFont.truetype(str(f_path), int(21 * s))
        f_sub = ImageFont.truetype(str(f_path), int(8.5 * s))
        f_led = ImageFont.truetype(str(f_path), int(7 * s))
    else:
        f_title = ImageFont.load_default(int(21 * s))
        f_sub = ImageFont.load_default(int(8.5 * s))
        f_led = ImageFont.load_default(int(7 * s))

    # Gradient wordmark on the left: "SYNTHY" + "BOY"
    text1, text2 = "SYNTHY", "BOY"
    mask = Image.new("L", (w, h), 0)
    m = ImageDraw.Draw(mask)
    m.text((16 * s, 26 * s), text1, font=f_title, fill=255)
    w1 = m.textlength(text1, font=f_title)
    m.text((16 * s + w1, 26 * s), text2, font=f_title, fill=255)

    gradimg = Image.new("RGB", (w, h))
    gp = ImageDraw.Draw(gradimg)
    for x in range(w):
        gp.line([(x, 0), (x, h)], fill=grad3(min(1, max(0, (x - 16 * s) / (w1 * 1.65)))))

    img = Image.composite(gradimg, img, mask)
    d = ImageDraw.Draw(img)

    # Subtitle
    d.text((17 * s, 64 * s), "WAVETABLE / ANALOG HYBRID", font=f_sub, fill=SUBTITLE_COLOR)

    # Status LEDs: POLY (Cyan), ARP (Muted), FX (Magenta)
    leds = (("POLY", CY), ("ARP", (110, 120, 140)), ("FX", MG))
    for i, (lab, c) in enumerate(leds):
        cx = (20 + i * 30) * s
        cyy = 84 * s
        d.ellipse([cx - 2 * s, cyy - 2 * s, cx + 2 * s, cyy + 2 * s], fill=c)
        d.text((cx + 5 * s, cyy - 5 * s), lab, font=f_led, fill=LED_LABEL_COLOR)

    # Downsample to target 270x110
    final_img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
    return final_img


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate MPC browser artwork tile (soundsmode.png).")
    parser.add_argument("--out", "-o", default=str(DEFAULT_OUT), help="Output image file or directory")
    parser.add_argument("--font", help="Path to TTF font file")
    parser.add_argument("--preview", help="Optional path to save an enlarged 3x preview image")
    args = parser.parse_args(argv)

    out_path = Path(args.out)
    if out_path.is_dir() or args.out.endswith(("/", "\\")):
        out_path = out_path / "soundsmode.png"

    out_path.parent.mkdir(parents=True, exist_ok=True)

    img = render_tile(font_path=args.font)
    img.save(out_path, format="PNG")
    print(f"Generated MPC browser tile: {out_path} ({img.size[0]}x{img.size[1]})")

    if args.preview:
        prev_path = Path(args.preview)
        prev_path.parent.mkdir(parents=True, exist_ok=True)
        img.resize((WIDTH * 3, HEIGHT * 3), Image.NEAREST).save(prev_path, format="PNG")
        print(f"Generated preview: {prev_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
