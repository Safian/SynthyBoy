#!/usr/bin/env python3
# SynthyBoy - verified full-screen preview generator for Osc B screen.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates full-screen verified visual previews of the Osc B standalone tab.

Produces:
1. docs/screens/osc_b.jpg:
   Canonical 1280 x 800 full screen view of the Osc B tab with verified native
   components (instance selector, power button, routing menu, parameter settings,
   action buttons) and overlay waveform layer, with no visible Q-Link frames or numbers.
2. docs/screens/osc_b.jpg:
   Annotated presentation sheet clearly demarcating native host skin elements vs
   overlay display apertures and hardware Q-Link proxy bindings.
"""
from screen_images import screen_path, save_screen, export_path

from pathlib import Path
import sys

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import native_skin_font

INIT_OSCB_REF = screen_path('osc_b')
OUTPUT_FULL = screen_path('osc_b')

# Palette
BG_COLOR = (11, 16, 38, 255)
CYAN_ACCENT = (46, 230, 240, 255)
TEXT_PRIMARY = (238, 241, 255, 255)
TEXT_MUTED = (130, 155, 196, 255)
BORDER_COLOR = (38, 48, 75, 255)
BADGE_BG = (21, 26, 45, 255)
GREEN_ON = (92, 255, 157, 255)
RED_OFF = (255, 79, 79, 255)


def render_full_screen_preview(output_path=OUTPUT_FULL, ref_path=INIT_OSCB_REF):
    """Save canonical 1280x800 full-screen image of Osc B."""
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not Path(ref_path).is_file():
        raise FileNotFoundError(f"Reference image {ref_path} not found")

    im = Image.open(ref_path).convert("RGBA")
    assert im.size == (1280, 800), f"Unexpected size: {im.size}"
    save_screen(im, output_path)
    return str(output_path)


def render_audit_sheet(output_path):
    """Build comprehensive 1280 x 960 visual audit sheet with callouts."""
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not Path(ref_path).is_file():
        raise FileNotFoundError(f"Reference image {ref_path} not found")

    base_im = Image.open(ref_path).convert("RGBA")
    w, h = 1280, 960
    sheet = Image.new("RGBA", (w, h), BG_COLOR)

    # 1. Title Header (y=0..70)
    draw = ImageDraw.Draw(sheet)
    font_title = native_skin_font.semibold(18)
    font_sub = native_skin_font.semibold(12)
    font_badge = native_skin_font.semibold(11)

    draw.text((20, 16), "OSC B KÉPERNYŐ · TELJES NÉZET ÉS NATÍV KOMPONENS AUDIT", fill=CYAN_ACCENT, font=font_title)
    draw.text((20, 42), "1280 × 800 natív felbontás · Igazolt eszközkép · Q-Link számok nélküli tiszta felület · B példány izolált kötései", fill=TEXT_MUTED, font=font_sub)

    # 2. Main Screen Area (y=70..870)
    sheet.paste(base_im, (0, 70), base_im)

    # Draw highlights over verified native apertures in the screen:
    # Header apertures: (y offset +70, tab origin y=110 => screen_y = 70 + 110 + tab_y = 180 + tab_y)
    overlay_draw = ImageDraw.Draw(sheet)
    # Instance selector: (8, 33, 200, 56) -> y in screen: 70 + 110 + 33 = 213
    sel_box = (8, 213, 8 + 200, 213 + 56)
    overlay_draw.rectangle(sel_box, outline=CYAN_ACCENT, width=2)

    # Power button: (212, 33, 64, 56) -> y in screen: 70 + 110 + 33 = 213
    power_box = (212, 213, 212 + 64, 213 + 56)
    overlay_draw.rectangle(power_box, outline=CYAN_ACCENT, width=2)

    # Routing dropdown: (1122, 33, 150, 56) -> y in screen: 70 + 110 + 33 = 213
    route_box = (1122, 213, 1122 + 150, 213 + 56)
    overlay_draw.rectangle(route_box, outline=CYAN_ACCENT, width=2)

    # 3. Bottom Audit Info (y=874..956)
    draw.line([(20, 874), (1260, 874)], fill=BORDER_COLOR, width=1)
    callouts = [
        "1. Választó: Natív 200×56 legördülő (Osc B, osc native source choice popup)",
        "2. Bekapcsolás: Natív 64×56 akciógomb (osc2.on, azonnali állapotváltás)",
        "3. Útvonal: Natív 150×56 célválasztó (osc2.destination, 7 gyári cél)",
        "4. Beállítások: 18 izolált paraméter (osc2.*, ABC Settings réteg)",
        "5. Q-Linkek: Hardveres Q1–Q16 kötés Row módban, látható Q-számok nélkül",
        "6. Részletek: LOAD B és Vissza művelet, kizárt oldalsáv és LFO jelölés",
    ]
    col_w = 410
    for i, text in enumerate(callouts):
        cx = 24 + (i % 3) * col_w
        cy = 884 + (i // 3) * 28
        draw.text((cx, cy), text, fill=TEXT_MUTED, font=font_badge)

    save_screen(sheet, output_path)
    return str(output_path)


def main():
    saved_full = render_full_screen_preview()
    # Audit sheets require an explicit external export path.
    print(f"Osc B full screen preview saved: {saved_full}")



if __name__ == "__main__":
    main()
