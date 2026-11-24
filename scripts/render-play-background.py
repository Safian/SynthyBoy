#!/usr/bin/env python3
# SynthyBoy - rebuild the native Play transition image from the approved factory-look gallery.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
REFERENCE = ROOT / "docs/screens/play.jpg"
ASSET = ROOT / "scripts/mpc-skin-extras/overlay-backgrounds/overlay_bg_play.png"
TAB_Y, TAB_H = 110, 628
# playrows::displayRect for the three compact views, in the 1280 x 800 gallery canvas.
PANELS = ((14, 422), (446, 834), (864, 1260))


def furniture(canvas, source, left, right, top, bottom, sample_x, cap):
    """Keep the curved edges and extend an empty vertical strip across the interior."""
    # The top and bottom caps contain the frame alone. The narrow side strips
    # contain its border; no glyph, curve, meter or selection is inside them.
    # Use only the source oscillator frame: envelope handles can touch their
    # frame while the right and centre panels have different widths.
    src_left, src_right = PANELS[0]
    width = right - left
    for y in range(top, bottom):
        if y < top + cap or y >= bottom - cap:
            row = source.crop((src_left, y, src_right, y + 1)).resize((width, 1), Image.Resampling.NEAREST)
        else:
            edge = 3
            row = Image.new("RGB", (width, 1), source.getpixel((sample_x, y)))
            row.paste(source.crop((src_left, y, src_left + edge, y + 1)), (0, 0))
            row.paste(source.crop((src_right - edge, y, src_right, y + 1)), (width - edge, 0))
        canvas.paste(row, (left, y - TAB_Y))


def render():
    source = Image.open(REFERENCE).convert("RGB")
    # x=8 is uninterrupted factory ground over every row. Build from that ground
    # and an allowlist of furniture, never from an entire preset screenshot.
    canvas = source.crop((8, TAB_Y, 9, TAB_Y + TAB_H)).resize((1280, TAB_H), Image.Resampling.NEAREST)
    for left, right in PANELS:
        # Every carousel has the same frame; its selected entry and neighbours are live.
        furniture(canvas, source, left, right, 120, 176, 100, 7)
        # x=20 is the blank margin left of the oscillator plot, beyond its frame.
        furniture(canvas, source, left, right, 184, 476, 20, 6)
        # Expand icon from the ENV display. The oscillator adds power and routing
        # before this icon and the LFO may change to a random source.
        expand = source.crop((766, 192, 830, 248))
        canvas.paste(expand, (right - 68, 192 - TAB_Y))
    # Fixed ENV knob captions only. The value, dial, modulation, Q chip and group
    # frame change with state or device, as do the oscillator and LFO controls.
    for left in (550, 746):
        for top in (532, 656):
            box = (left, top, left + 70, top + 22)
            canvas.paste(source.crop(box), (left, top - TAB_Y))
    return canvas


if __name__ == "__main__":
    render().save(ASSET, optimize=True)
