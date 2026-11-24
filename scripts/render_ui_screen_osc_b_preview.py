#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render the OSC B screen using the production OSC fixture."""

import sys

from render_osc_screen_preview import render_page
from screen_images import require_external_output, save_screen, screen_path
from PIL import Image


def render_full_screen_preview(output_path=None, ref_path=None):
    if ref_path is not None:
        with Image.open(ref_path) as image:
            if image.size != (1280, 800):
                raise ValueError(f"Unexpected OSC B reference size: {image.size}")
            return str(save_screen(image, output_path or screen_path("osc_b")))
    return str(render_page("osc_b_on", output_path))


def render_audit_sheet(output_path, ref_path=None):
    """Export the OSC B image with its source identified below the display."""
    from PIL import ImageDraw
    import native_skin_font

    destination = require_external_output(output_path)
    source = ref_path or screen_path("osc_b")
    with Image.open(source) as image:
        if image.size != (1280, 800):
            raise ValueError(f"Unexpected OSC B reference size: {image.size}")
        sheet = Image.new("RGB", (1280, 960))
        sheet.paste(image.convert("RGB"), (0, 0))
    ImageDraw.Draw(sheet).text((24, 835), f"OSC B   |   {source}", font=native_skin_font.semibold(20))
    return str(save_screen(sheet, destination))


if __name__ == "__main__":
    print(render_full_screen_preview(sys.argv[1] if len(sys.argv) > 1 else None))
