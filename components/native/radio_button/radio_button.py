# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Segmented radio buttons for the Akai MPC host."""

import hashlib
from pathlib import Path

from components.native.button.button import button
from components.style import (
    color as _style_color,
    opacity as _style_opacity,
    rgba as _style_rgba,
    size as _style_size,
)
from PIL import Image, ImageDraw, ImageFont

VARIANT_SEGMENTED = "segmented"


def semibold(size):
    font = Path(__file__).with_name("img") / _style_size('font.file.semibold')
    return ImageFont.truetype(str(font), size)


def _vgrad(draw, x0, y0, x1, y1, c0, c1):
    for y in range(int(y0), int(y1)):
        t = (y - y0) / max(1, (y1 - y0))
        c = tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(len(c0)))
        draw.line([(x0, y), (x1, y)], fill=c)


def render_segmented_button_art(label, active, width, height, *, scale=_style_size('radio_button.segmented_button_art.geometry')):
    """Render a segmented button with full active background."""
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(im)
    pad = _style_size('radio_button.segmented_button_art.pad') * scale
    radius = _style_size('radius.control') * scale

    mask = Image.new("L", (w, h), _style_opacity("transparent"))
    ImageDraw.Draw(mask).rounded_rectangle((pad, pad, w - pad - _style_size('radio_button.segmented_button_art.rounded_rectangle'), h - pad - _style_size('radio_button.segmented_button_art.rounded_rectangle')), radius=radius, fill=_style_opacity("opaque"))

    grad = Image.new("RGBA", (w, h))
    gdraw = ImageDraw.Draw(grad)
    if active:
        # Full active cyan background gradient
        _vgrad(gdraw, 0, 0, w, h, _style_rgba("accent.cyan"), _style_rgba("radio.active.bottom"))
        outline_col = _style_color("radio.active.border")
    else:
        # Dark body gradient
        _vgrad(gdraw, 0, 0, w, h, _style_rgba("radio.surface.top"), _style_rgba("radio.surface.bottom"))
        outline_col = _style_color("control.border")

    im.paste(grad, (0, 0), mask)
    draw.rounded_rectangle((pad, pad, w - pad - _style_size('radio_button.segmented_button_art.rounded_rectangle'), h - pad - _style_size('radio_button.segmented_button_art.rounded_rectangle')), radius=radius, outline=outline_col, width=scale)

    font_size = _style_size('font.option') * scale if len(label) <= 4 else _style_size('font.caption') * scale
    font = semibold(font_size)
    text_col = _style_color("radio.active.text") if active else _style_color("radio.text")
    draw.text((w // 2, h // 2), label, fill=text_col, font=font, anchor="mm")

    return im.resize((width, height), Image.Resampling.LANCZOS)


def _clean_name(name):
    return "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")


def segmented_radio_entry(name, label, rect, index, count, handle, skins_dir, *, gates=(), focus="Yes"):
    """Bind one choice, including grid or vertically stacked screen layouts."""
    width, height = rect[2:]
    digest = hashlib.sha256(label.encode("utf-8")).hexdigest()[:16]
    stem = f"choice_segmented_{_clean_name(label)}_{digest}_{width}x{height}"
    skins = Path(skins_dir)
    skins.mkdir(parents=True, exist_ok=True)
    on, off = stem + "_on.png", stem + "_off.png"
    for selected, file in ((True, on), (False, off)):
        render_segmented_button_art(label, selected, width, height).save(skins / file)
    remaps = (("Data", handle),) if handle is not None else ()
    item = button(name, rect, on, off, index=index, count=count, remaps=remaps, gates=gates)
    item["bounds"]["acceptsHWFocus"] = focus
    return item


def create_segmented_radio_button_group(name, rect, labels, handle, skins_dir, *, gates=(), focus="Yes", vertical=False):
    """Tile the available width or height, including the last remainder pixel."""
    count = len(labels)
    if count < 2:
        raise ValueError("Choice group must have at least 2 choices")
    x, y, width, height = rect
    extent = height if vertical else width
    items = []
    for i, label in enumerate(labels):
        start, end = extent * i // count, extent * (i + 1) // count
        cell = (x, y + start, width, end - start) if vertical else (x + start, y, end - start, height)
        items.append(segmented_radio_entry(f"{name} {label}", label, cell, i, count,
                                           handle, skins_dir, gates=gates, focus=focus))
    return items


def build_radio_button_entries(name, rect, labels, variant, handle, skins_dir, *, gates=(), focus="Yes"):
    if variant != VARIANT_SEGMENTED:
        raise ValueError(f"Unknown choice group variant: {variant!r}")
    return create_segmented_radio_button_group(name, rect, labels, handle, skins_dir, gates=gates, focus=focus)


def render_segmented_status_art(label, selected, used, width, height):
    """Shared segmented selection face with an optional source-in-use marker."""
    image = render_segmented_button_art(label, selected, width, height)
    if used:
        geometry = _style_size("radio.status_dot")
        radius, bottom = geometry["radius"], geometry["bottom"]
        ImageDraw.Draw(image).ellipse((width // 2 - radius, height - bottom - 2 * radius,
                                       width // 2 + radius, height - bottom),
                                      fill=_style_color("radio.active.text" if selected else "accent.cyan"))
    return image
