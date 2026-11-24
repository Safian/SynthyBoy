# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""RadioButton variants for Akai MPC standalone UI.

Native component using shared family style data.
Provides two interchangeable native choice/radio group styles:
1. Segmented buttons with full active background (VARIANT_SEGMENTED)
2. Factory exclusionary radio buttons with cyan filled/empty dot skin,
   where the entire option cell is touchable (VARIANT_RADIO_DOT).

Both support arbitrary choice counts (e.g. HZ/BPM/KEY or two banks)
and share the identical value binding model (Button with numButtonsInGroup=N,
buttonId=i, handleName="Data", gestureBehaviour="Instant").
"""

import hashlib
from pathlib import Path

from components.style import (
    color as _style_color,
    opacity as _style_opacity,
    rgba as _style_rgba,
    size as _style_size,
)
from PIL import Image, ImageDraw, ImageFont

VARIANT_SEGMENTED = "segmented"
VARIANT_RADIO_DOT = "radio_dot"


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


def render_radio_dot_button_art(label, active, width, height, *, scale=_style_size('radio_button.radio_dot_button_art.geometry')):
    """Render a factory-style radio button with cyan filled/empty dot skin.
    
    The entire option cell is part of the button image and touch target.
    """
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(im)
    pad = _style_size('radio_button.radio_dot_button_art.pad') * scale
    radius = _style_size('radius.control') * scale

    mask = Image.new("L", (w, h), _style_opacity("transparent"))
    ImageDraw.Draw(mask).rounded_rectangle((pad, pad, w - pad - _style_size('radio_button.radio_dot_button_art.rounded_rectangle'), h - pad - _style_size('radio_button.radio_dot_button_art.rounded_rectangle')), radius=radius, fill=_style_opacity("opaque"))

    grad = Image.new("RGBA", (w, h))
    gdraw = ImageDraw.Draw(grad)
    _vgrad(gdraw, 0, 0, w, h, _style_rgba("radio.dot.surface.top"), _style_rgba("radio.dot.surface.bottom"))
    im.paste(grad, (0, 0), mask)

    outline_col = _style_color("radio.dot.active.border") if active else _style_color("radio.dot.border")
    draw.rounded_rectangle((pad, pad, w - pad - _style_size('radio_button.radio_dot_button_art.rounded_rectangle'), h - pad - _style_size('radio_button.radio_dot_button_art.rounded_rectangle')), radius=radius, outline=outline_col, width=scale)

    # Dot positioning
    r = _style_size('radio_button.radio_dot_button_art.r') * scale
    cx = _style_size('radio_button.radio_dot_button_art.cx') * scale + r
    cy = h // 2

    if active:
        # Cyan filled dot ("teli pötty") with soft halo
        draw.ellipse((cx - r - _style_size('radio_button.radio_dot_button_art.ellipse') * scale, cy - r - _style_size('radio_button.radio_dot_button_art.ellipse') * scale, cx + r + _style_size('radio_button.radio_dot_button_art.ellipse') * scale, cy + r + _style_size('radio_button.radio_dot_button_art.ellipse') * scale),
                     outline=_style_rgba("accent.cyan", _style_opacity("halo")), width=scale)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=_style_color("accent.cyan"))
    else:
        # Cyan outline empty dot ("üres pötty")
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=_style_color("radio.dot.off"), width=int(_style_size('radio_button.radio_dot_button_art.int') * scale))

    # Text label next to dot (centered in remaining option area)
    font_size = _style_size('font.option') * scale if len(label) <= 4 else _style_size('font.caption') * scale
    font = semibold(font_size)
    text_col = _style_color("text.white") if active else _style_color("text.muted")
    text_x = (cx + r + _style_size('radio_button.radio_dot_button_art.text.x') * scale + w - pad) // 2
    draw.text((text_x, cy), label, fill=text_col, font=font, anchor="mm")

    return im.resize((width, height), Image.Resampling.LANCZOS)


def _clean_name(name):
    return "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")


def build_radio_button_entries(name, rect, labels, variant, handle, skins_dir, *, gates=(), focus="Yes"):
    """Build the list of native Button entries for a choice group.
    
    Parameters:
      name: base component name (e.g. "LFO Timing Mode" or "Osc Bank")
      rect: total bounding box (x, y, width, height) of the group
      labels: sequence of choice labels (e.g. ("HZ", "BPM", "KEY") or ("BANK A", "BANK B"))
      variant: VARIANT_SEGMENTED or VARIANT_RADIO_DOT
      handle: engine parameter handle to bind (e.g. "Parameter 3348" or "Data")
      skins_dir: directory path where button PNG assets are stored
      gates: optional visibility / invalidation gates
      focus: "Yes" or "No" for hardware focus
    """
    x, y, total_w, total_h = rect
    count = len(labels)
    if count < _style_size('radio_button.build_radio_button_entries.geometry'):
        raise ValueError("Choice group must have at least 2 choices")

    skins_path = Path(skins_dir)
    skins_path.mkdir(parents=True, exist_ok=True)

    items = []
    for i, label in enumerate(labels):
        left = x + total_w * i // count
        right = x + total_w * (i + _style_size('radio_button.build_radio_button_entries.right')) // count
        w_i = right - left

        clean_label = _clean_name(label)
        # Retain the exact label identity across groups sharing a skin directory.
        clean_label += "_" + hashlib.sha256(label.encode("utf-8")).hexdigest()[:16]
        on_file = f"choice_{variant}_{clean_label}_{w_i}x{total_h}_on.png"
        off_file = f"choice_{variant}_{clean_label}_{w_i}x{total_h}_off.png"

        if variant == VARIANT_SEGMENTED:
            on_art = render_segmented_button_art(label, True, w_i, total_h)
            off_art = render_segmented_button_art(label, False, w_i, total_h)
        elif variant == VARIANT_RADIO_DOT:
            on_art = render_radio_dot_button_art(label, True, w_i, total_h)
            off_art = render_radio_dot_button_art(label, False, w_i, total_h)
        else:
            raise ValueError(f"Unknown choice group variant: {variant!r}")

        on_art.save(skins_path / on_file)
        off_art.save(skins_path / off_file)

        bounds = {
            "version": _style_size('radio_button.build_radio_button_entries.bounds') if gates else _style_size('radio_button.build_radio_button_entries.bounds.offset1'),
            "acceptsHWFocus": focus,
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": f"{left} {y} {w_i} {total_h}",
        }
        if gates:
            bounds["additionalInvalidatingHandles"] = list(gates)

        items.append({
            "version": 2,
            "componentData": {
                "version": 1,
                "name": f"{name} {label}",
                "type": "Button",
                "data": {
                    "version": 2,
                    "onImage": on_file,
                    "offImage": off_file,
                    "buttonId": i,
                    "numButtonsInGroup": count,
                    "handleName": "Data",
                    "gestureBehaviour": "Instant",
                },
            },
            "handle remapping": {
                "version": 1,
                "map": [{"key": "Data", "value": handle}],
            },
            "bounds": bounds,
        })

    return items


def create_segmented_radio_button_group(name, rect, labels, handle, skins_dir, *, gates=(), focus="Yes"):
    """Convenience factory for Variant 1: segmented buttons with full active background."""
    return build_radio_button_entries(name, rect, labels, VARIANT_SEGMENTED, handle, skins_dir,
                                      gates=gates, focus=focus)


def create_radio_dot_group(name, rect, labels, handle, skins_dir, *, gates=(), focus="Yes"):
    """Convenience factory for Variant 2: factory exclusionary radio buttons with cyan dot."""
    return build_radio_button_entries(name, rect, labels, VARIANT_RADIO_DOT, handle, skins_dir,
                                      gates=gates, focus=focus)
