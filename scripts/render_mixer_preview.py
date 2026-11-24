#!/usr/bin/env python3
# SynthyBoy - Canonical preview renderer for the native Mixer screen and diagnostic state matrix.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the canonical Mixer screen preview (docs/screens/mixer.jpg) and host static bg.

Composes every element strictly from the actual native component definitions
(`build_mixer_strip_definitions`, `solo_mute`, `fader`, `meter_block`) and central
`components/sizes.json` + `components/colors.json` style tokens so the rendered preview
and the native TUI skin JSON match pixel-for-pixel (including LEVEL above PAN/B1/B2 and the
persistent 6-tab bottom bar).
"""

import argparse
import json
from pathlib import Path
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))

from screen_images import require_external_output, save_screen, screen_path
from components.native.tab_bar.tab_bar import render_tab_bar
from components.native.mixer_strip.mixer_strip import (
    build_mixer_strip_definitions,
    render_mixer_full_screen_background,
    render_mixer_strip,
    semibold,
    VARIANT_BUS,
    VARIANT_MASTER,
    VARIANT_SOURCE,
)
from components.native.solo_mute.solo_mute import (
    render_button_face,
    STATE_DISABLED,
    STATE_IMPLICIT_SOLO_MUTE,
    STATE_MUTE,
    STATE_NEUTRAL,
    STATE_PRESSED,
    STATE_SOLO,
)
from components.native.meter_block.meter_block import dbfs_to_normalized
from components.style import color as _style_color, rgb as _style_rgb, size as _style_size

CHROME_REF = ROOT / "docs" / "screens" / "mixer_old.jpg"


def _render_mixer_tab_canvas(active_tab: int = 0, meter_readout=None) -> Image.Image:
    """Renders the 1280x628 native Mixer tab image directly from component definitions."""
    defs = build_mixer_strip_definitions()
    assert "mixerStripSource" in defs and "mixerStripBus" in defs and "mixerStripMaster" in defs

    tab_bg = render_mixer_full_screen_background()
    start_x = _style_size("mixer_strip.start_x")
    stride = _style_size("mixer_strip.stride")
    strip_y = _style_size("mixer_strip.start_y")

    strips_data = [
        {"variant": VARIANT_SOURCE, "name": "SUB", "solo": 0, "mute": 0, "destination": "Filters 1+2",
         "level": 0.50, "level_text": "50.0%", "peak": dbfs_to_normalized(-12.0), "hold": dbfs_to_normalized(-6.0),
         "pan": 0.0, "pan_text": "C", "send1": 0.0, "send1_text": "0.0%", "send2": 0.0, "send2_text": "0.0%"},
        {"variant": VARIANT_SOURCE, "name": "A", "solo": 0, "mute": 0, "destination": "Filters 1+2",
         "level": 0.80, "level_text": "80.0%", "peak": dbfs_to_normalized(-6.0), "hold": dbfs_to_normalized(-3.0),
         "pan": 0.0, "pan_text": "C", "send1": 0.25, "send1_text": "25.0%", "send2": 0.0, "send2_text": "0.0%"},
        {"variant": VARIANT_SOURCE, "name": "B", "solo": 0, "mute": 0, "destination": "Filter 1",
         "level": 0.60, "level_text": "60.0%", "peak": dbfs_to_normalized(-18.0), "hold": dbfs_to_normalized(-12.0),
         "pan": -0.35, "pan_text": "L 35", "send1": 0.0, "send1_text": "0.0%", "send2": 0.0, "send2_text": "0.0%"},
        {"variant": VARIANT_SOURCE, "name": "C", "solo": 0, "mute": 0, "destination": "Filter 2",
         "level": 0.40, "level_text": "40.0%", "peak": dbfs_to_normalized(-24.0), "hold": dbfs_to_normalized(-18.0),
         "pan": 0.35, "pan_text": "R 35", "send1": 0.0, "send1_text": "0.0%", "send2": 0.10, "send2_text": "10.0%"},
        {"variant": VARIANT_SOURCE, "name": "NOISE", "solo": 0, "mute": 0, "destination": "Direct out",
         "level": 0.25, "level_text": "25.0%", "peak": dbfs_to_normalized(-36.0), "hold": dbfs_to_normalized(-24.0),
         "pan": 0.0, "pan_text": "C", "send1": 0.0, "send1_text": "0.0%", "send2": 0.0, "send2_text": "0.0%"},
        {"variant": VARIANT_SOURCE, "name": "F1", "solo": 0, "mute": 0, "destination": "Effects only",
         "level": 60.0 / 72.0, "level_text": "0.0 dB", "peak": dbfs_to_normalized(-6.0), "hold": dbfs_to_normalized(-3.0),
         "pan": 0.0, "pan_text": "C", "send1": 0.0, "send1_text": "0.0%", "send2": 0.0, "send2_text": "0.0%"},
        {"variant": VARIANT_SOURCE, "name": "F2", "solo": 0, "mute": 0, "destination": "Effects only",
         "level": 60.0 / 72.0, "level_text": "0.0 dB", "peak": dbfs_to_normalized(-12.0), "hold": dbfs_to_normalized(-6.0),
         "pan": 0.0, "pan_text": "C", "send1": 0.0, "send1_text": "0.0%", "send2": 0.0, "send2_text": "0.0%"},
        {"variant": VARIANT_BUS, "name": "BUS 1", "solo": 0, "mute": 0, "destination": "Main",
         "level": 0.7885, "level_text": "0.0 dB", "peak": dbfs_to_normalized(-12.0), "hold": dbfs_to_normalized(-6.0)},
        {"variant": VARIANT_BUS, "name": "BUS 2", "solo": 0, "mute": 0, "destination": "Main",
         "level": 0.7885, "level_text": "0.0 dB", "peak": dbfs_to_normalized(-18.0), "hold": dbfs_to_normalized(-12.0)},
        {"variant": VARIANT_MASTER, "name": "MAIN", "fx_enable": 1, "mute": 0,
         "level": 0.4504, "level_text": "-12.0 dB", "peak": dbfs_to_normalized(-6.0), "hold": dbfs_to_normalized(-3.0)},
    ]

    # Offline example levels use independent L/R values; --meter-readout accepts actual engine fixture output.
    examples = [(-12, -14), (-2.5, -5), (-14, -20), (-23, -17), (-36, -32),
                (-3, -5), (-10, -13), (-8, -11), (-4, -6), (-7, -8.5)]
    for i, st in enumerate(strips_data):
        if meter_readout is not None:
            st.update(meter_readout["strips"][i])
        else:
            left, right = examples[i]
            st.update(PeakL=dbfs_to_normalized(left), PeakR=dbfs_to_normalized(right),
                      HoldL=dbfs_to_normalized(min(0, left + 4)), HoldR=dbfs_to_normalized(min(0, right + 3)),
                      ClipL=0, ClipR=0)
        strip_img = render_mixer_strip(st["variant"], st)
        tab_bg.alpha_composite(strip_img, (start_x + stride * i, strip_y))

    labels = ("MIXER", "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")
    bar = render_tab_bar(labels, active_tab, _style_size("mixer_strip.tab.width"), _style_size("mixer_strip.tab.height"))
    y = strip_y + _style_size("mixer_strip.height") + _style_size("mixer_strip.tab.gap_y")
    tab_bg.alpha_composite(bar, (_style_size("mixer_strip.tab.start_x"), y))

    return tab_bg


def render_state_variants_sheet(out_dir: Path) -> None:
    """Renders multi-state SoloMutePair + strip presentation diagnostic outside repository."""
    out_dir = require_external_output(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    defs = build_mixer_strip_definitions()

    # 1. Solo / Mute / FX FilmStrip state matrix from actual solo_mute.py face renderer
    w = 640
    h = 260
    sheet = Image.new("RGBA", (w, h), _style_rgb("surface.canvas"))
    d = ImageDraw.Draw(sheet)
    f_title = semibold(_style_size("mixer_strip.font.preview_sub"))
    rows = (
        ("S", [("NEUTRAL", STATE_NEUTRAL), ("SOLO", STATE_SOLO), ("DIM", STATE_IMPLICIT_SOLO_MUTE),
               ("OFF", STATE_DISABLED), ("PRESS", STATE_PRESSED)]),
        ("M", [("NEUTRAL", STATE_NEUTRAL), ("MUTE", STATE_MUTE), ("DIM", STATE_IMPLICIT_SOLO_MUTE),
               ("OFF", STATE_DISABLED), ("PRESS", STATE_PRESSED)]),
        ("FX", [("NEUTRAL", STATE_NEUTRAL), ("FX ON", "fx_on"), ("DIM", STATE_IMPLICIT_SOLO_MUTE),
                ("OFF", STATE_DISABLED), ("PRESS", STATE_PRESSED)]),
    )
    for r, (glyph, cols) in enumerate(rows):
        for c, (label, st) in enumerate(cols):
            px, py = 20 + c * 120, 20 + r * 80
            face = render_button_face(glyph, st)
            sheet.alpha_composite(face, (px, py))
            d.text((px + 62, py + 28), label, font=f_title, fill=_style_rgb("text.caption"), anchor="lm")
    save_screen(sheet, out_dir / "solo_mute_states.png")

    # 2. Strip states side by side using actual definitions & render_mixer_strip
    assert "soloMutePairSource" in defs and "mixerMeterBlockSource" in defs
    strip_cases = [
        ("NEUTRAL", {"variant": VARIANT_SOURCE, "name": "SUB", "destination": "Filters 1+2", "level": 0.5, "level_text": "50%"}),
        ("SOLO", {"variant": VARIANT_SOURCE, "name": "A (SOLO)", "solo_state": STATE_SOLO, "destination": "Filter 1", "level": 0.8, "level_text": "80%"}),
        ("DIM S-MUTE", {"variant": VARIANT_SOURCE, "name": "B (DIM)", "mute_state": STATE_IMPLICIT_SOLO_MUTE, "destination": "Filter 2", "level": 0.6, "level_text": "60%"}),
        ("USER MUTE", {"variant": VARIANT_SOURCE, "name": "C (MUTE)", "mute_state": STATE_MUTE, "destination": "Filter 1", "level": 0.6, "level_text": "60%"}),
        ("SOURCE OFF", {"variant": VARIANT_SOURCE, "name": "NOISE (OFF)", "solo_state": STATE_DISABLED, "mute_state": STATE_DISABLED, "destination": "Direct out", "level": 0.0, "level_text": "0%"}),
        ("MAIN FX+MUTE", {"variant": VARIANT_MASTER, "name": "MAIN", "fx_enable": 1, "mute_state": STATE_MUTE, "level": 0.45, "level_text": "-12.0 dB"}),
    ]
    sw = _style_size("mixer_strip.width")
    sh = _style_size("mixer_strip.height")
    board = Image.new("RGBA", (len(strip_cases) * (sw + 12) + 12, sh + 40), _style_rgb("surface.canvas"))
    bd = ImageDraw.Draw(board)
    for idx, (title, st) in enumerate(strip_cases):
        x = 12 + idx * (sw + 12)
        bd.text((x + sw // 2, 16), title, font=f_title, fill=_style_rgb("preview.heading"), anchor="mm")
        board.alpha_composite(render_mixer_strip(st["variant"], st), (x, 30))
    save_screen(board, out_dir / "mixer_strip_states.png")


def render_preview(states_out: Path = None, meter_readout=None):
    if CHROME_REF.is_file():
        screen = Image.open(CHROME_REF).convert("RGBA")
    else:
        screen = Image.new("RGBA", (1280, 800), _style_rgb("surface.canvas"))

    tab_bg = _render_mixer_tab_canvas(active_tab=0, meter_readout=meter_readout)
    screen.alpha_composite(tab_bg, (0, 110))

    canonical = screen_path("mixer")
    save_screen(screen, canonical)
    print(f"Saved canonical mixer screen to {canonical}")

    if states_out is not None:
        render_state_variants_sheet(states_out)
        print(f"Saved diagnostic state previews outside repo to {states_out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Render canonical Mixer preview and external diagnostic sheets.")
    parser.add_argument("--states-out", type=Path, default=None, help="External temporary directory for multi-state diagnostics.")
    parser.add_argument("--meter-readout", type=Path, help="Actual engine fixture JSON (strips with PeakL/R, HoldL/R, ClipL/R).")
    args = parser.parse_args()
    readout = json.loads(args.meter_readout.read_text()) if args.meter_readout else None
    render_preview(args.states_out, readout)
