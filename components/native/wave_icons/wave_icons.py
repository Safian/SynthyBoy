# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""WAVE ICONS: the waveform pictograms (sine, saw, square, pulse, triangle, S&H, noise), catalogue sheets.

The recipes are `selector.render_wave_icon(width, height, color, shape=...)`: all shapes share the sine's box, margins,
amplitude, stroke (`stroke.icon`) and rounding. A sheet is one picture of all icons in a row at the `rotary_selector.icon` box
(24 x 12), `wave_icons.gap` apart, the selected one `accent.cyan` and the others `selector.wave` (as in the RotarySelector);
`zoom` N shows the same pixels magnified N times (nearest neighbour). Docs: docs/ui-components/wave_icons.md.
"""
from components.native.selector.selector import WAVE_SHAPES, render_wave_icon
from components.style import color as _style_color, size as _style_size

try:
    from PIL import Image
except ImportError:
    Image = None

SHEET_KEYS = {"id", "file", "alt", "caption", "zoom", "selected"}


def icon_box():
    box = _style_size("rotary_selector")["icon"]
    return box["width"], box["height"]


def render_icon(shape, selected=False):
    width, height = icon_box()
    return render_wave_icon(width, height, _style_color("accent.cyan" if selected else "selector.wave"), shape=shape)


def sheet_origins():
    """The left edge of every icon in the 1:1 sheet."""
    width, _ = icon_box()
    return [i * (width + _style_size("wave_icons")["gap"]) for i in range(len(WAVE_SHAPES))]


def render_sheet(sheet):
    width, height = icon_box()
    gap = _style_size("wave_icons")["gap"]
    image = Image.new("RGBA", (len(WAVE_SHAPES) * width + (len(WAVE_SHAPES) - 1) * gap, height))
    for index, (shape, left) in enumerate(zip(WAVE_SHAPES, sheet_origins())):
        image.alpha_composite(render_icon(shape, index == sheet["selected"]), (left, 0))
    zoom = sheet["zoom"]
    return image if zoom == 1 else image.resize((image.width * zoom, image.height * zoom), Image.Resampling.NEAREST)


def sheet_size(sheet):
    width, height = icon_box()
    gap = _style_size("wave_icons")["gap"]
    return (len(WAVE_SHAPES) * width + (len(WAVE_SHAPES) - 1) * gap) * sheet["zoom"], height * sheet["zoom"]


def sheet_samples(entry):
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("wave_icons entry needs a nonempty samples list")
    for item in entry["samples"]:
        if (set(item) != SHEET_KEYS or type(item["zoom"]) is not int or item["zoom"] < 1
                or not (item["selected"] is None or (type(item["selected"]) is int and 0 <= item["selected"] < len(WAVE_SHAPES)))):
            raise ValueError(f"wave icon sheet needs exactly {sorted(SHEET_KEYS)}, an integer zoom and a selected index or null")
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("wave icon sheet ids must be unique")
    return entry["samples"]


def render_sample(sample):
    return render_sheet(sample)


def catalog_sections(entry):
    samples = sheet_samples(entry)
    shapes = ", ".join(WAVE_SHAPES)
    sections = [{"heading": f"Sorrend: {shapes}", "images": [
        {"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples]}]
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{} × {}".format(*sheet_size(i))] for i in samples]}})
    return sections
