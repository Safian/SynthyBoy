# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Segmented radio buttons for the Akai MPC host."""

import hashlib
from pathlib import Path

from components.native.button.button import action_art, button, gradient_face
from components.style import (
    color as _style_color,
    control_look as _control_look,
    rgb as _style_rgb,
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

    if _control_look() == "legacy":
        # The shared button gradient (button.gradient.*, control.gradient); the selected face is the accent on the same shape.
        if active:
            fill = (_style_rgb("accent.cyan"), _style_rgb("radio.active.bottom"))
            line = (_style_rgb("radio.active.border"),) * 2
        else:
            fill = (_style_rgb("button.gradient.top"), _style_rgb("button.gradient.bottom"))
            line = (_style_rgb("button.gradient.border"), _style_rgb("button.gradient.border.bottom"))
        im, draw = gradient_face((w, h), inset=pad, radius=radius, frame=scale, span=_style_size("control.gradient"),
                                 fill=fill, line=line)
    else:
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


def render_grid_cell_art(label, active, width, height, *, ends=(True, True), vertical=False):
    """One cell of a RadioGrid-style row or column laid over a tiled hit slot: the Button recipe (action_art: legacy face,
    or look "active" for the selected cell), the largest button size that fits the slot, centred across the group, with the
    RadioGrid gap (`radio_grid.gap`) kept between neighbouring cells. `ends` marks the first / last cell of the group."""
    gap = _style_size("radio_grid")["gap"]
    along = height if vertical else width
    left = 0 if ends[0] else gap // 2
    right = 0 if ends[1] else gap - gap // 2
    extent = along - left - right
    across = width if vertical else height
    limit = extent if vertical else across
    box = max(size for size in (32, 40, 48) if size <= limit) if limit >= 32 else 32
    kind = {"look": "active", "colour": _style_color("radio.active.text")} if active else {}
    if vertical:
        face = action_art(text=label, width=width, height=box, **kind)
        top = left + (extent - box) // 2
        slot = Image.new("RGBA", (width, height))
        slot.alpha_composite(face, (0, top))
        return slot
    face = action_art(text=label, width=extent, height=box, **kind)
    slot = Image.new("RGBA", (width, height))
    slot.alpha_composite(face, (left, (height - box) // 2))
    return slot


def _clean_name(name):
    return "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")


def segmented_radio_entry(name, label, rect, index, count, handle, skins_dir, *, gates=(), focus="Yes", grid=None):
    """Bind one choice, including grid or vertically stacked screen layouts.

    grid: None draws the segmented faces; "row" / "column" draw the RadioGrid recipe (render_grid_cell_art) instead."""
    width, height = rect[2:]
    digest = hashlib.sha256(label.encode("utf-8")).hexdigest()[:16]
    stem = f"choice_segmented_{_clean_name(label)}_{digest}_{width}x{height}"
    if grid:
        stem = f"choice_grid_{grid}_{_clean_name(label)}_{digest}_{width}x{height}_{int(index == 0)}{int(index == count - 1)}"
    skins = Path(skins_dir)
    skins.mkdir(parents=True, exist_ok=True)
    on, off = stem + "_on.png", stem + "_off.png"
    for selected, file in ((True, on), (False, off)):
        face = (render_grid_cell_art(label, selected, width, height, ends=(index == 0, index == count - 1), vertical=grid == "column")
                if grid else render_segmented_button_art(label, selected, width, height))
        face.save(skins / file)
    remaps = (("Data", handle),) if handle is not None else ()
    item = button(name, rect, on, off, index=index, count=count, remaps=remaps, gates=gates)
    item["bounds"]["acceptsHWFocus"] = focus
    return item


def titled_radio_grid(skins_dir, labels, rects, *, size, caption_rect, caption_height):
    """A focusable group of native radio choices with a parameter-bound title."""
    from components.native.button.button import entry
    from components.native.name_value.name_value import _label_component
    from components.native.selector.selector import passive_layer
    from components.style import argb, focus_ring_data

    if len(labels) != len(rects) or len(labels) < 2:
        raise ValueError("A radio grid needs one cell for each of at least two choices")
    focus = entry("focus", "Focus", focus_ring_data(), (0, 0, *size), focus="No")
    focus["bounds"]["whenVisible"] = "WhenFocussed"
    caption = _label_component("name", "Name", "Caption", caption_rect, caption_height,
                               argb("text.caption"), "horizontallyCentred verticallyCentred")
    return passive_layer([focus, caption, *[
        segmented_radio_entry(f"row {index}", label, rect, index, len(labels), None, skins_dir)
        for index, (label, rect) in enumerate(zip(labels, rects))
    ]])


def create_segmented_radio_button_group(name, rect, labels, handle, skins_dir, *, gates=(), focus="Yes", vertical=False, grid=False):
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
                                           handle, skins_dir, gates=gates, focus=focus,
                                           grid=("column" if vertical else "row") if grid else None))
    return items


def build_radio_button_entries(name, rect, labels, variant, handle, skins_dir, *, gates=(), focus="Yes"):
    if variant != VARIANT_SEGMENTED:
        raise ValueError(f"Unknown choice group variant: {variant!r}")
    return create_segmented_radio_button_group(name, rect, labels, handle, skins_dir, gates=gates, focus=focus)


def write_segmented_strip(path, size, frames):
    """Pack native radio faces in host frame order; None reserves a blank frame."""
    width, height = size
    strip = Image.new("RGBA", (width, height * len(frames)))
    for index, frame in enumerate(frames):
        if frame is not None:
            label, selected = frame
            strip.paste(render_segmented_button_art(label, selected, width, height), (0, index * height))
    strip.save(path)
    return Path(path).name
