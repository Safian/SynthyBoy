# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native name-value component for Akai MPC standalone UI.

Refined based on owner specification (2026-11-24):
- Heights supported: 48 px (base), 40 px (compact), 32 px (minimal)
- Parameter name on the left, OUTSIDE the frame (e.g. SEM, CRS, OCT, FIN)
- ONLY the right-side host-formatted value is inside the frame (e.g. [ 0 ], [ +7 ], [ -12.50 ])
- Visual example: SEM [ +7 ]
- NO Q-Link number or yellow chip is drawn on or inside the component face
- Reuses existing NumberField / Label native definitions and style
- Preserves full Q-Link binding, touch gesture (numeric overlay popup), and coarse data wheel editing
"""

import copy
from pathlib import Path

from components.style import (
    argb as _style_argb,
    color as _style_color,
    opacity as _style_opacity,
    native_color as _style_native_color,
    rgba as _style_rgba,
    size as _style_size,
)
from PIL import Image, ImageDraw, ImageFont

# Visual theme palette matching the reference image and SynthyBoy MPC dark theme
BG_CELL = _style_color("field.surface")
BORDER_CELL = _style_color("field.border")
BG_CELL_FOCUS = _style_color("field.focus")
TEXT_LABEL = _style_color("text.label")
TEXT_VALUE = _style_color("field.value")

DEFAULT_HEIGHT = _style_size('height.medium')
DEFAULT_WIDTH = _style_size('name_value.width')
DEFAULT_BOX_WIDTH = _style_size('name_value.box.width')
DEFAULT_RADIUS = _style_size('radius.field')
DEFAULT_PAD_X = _style_size('name_value.pad')
POPUP_NUMBER_MENU = "fx editor number menu"

HEIGHT_VARIANTS = tuple(_style_size('name_value.height_variants'))

HEIGHT_PRESETS = {_style_size(height): {key: _style_size(token) for key, token in preset.items()}
                  for height, preset in _style_size('name_value.presets').items()}


def _get_font(size):
    font = Path(__file__).with_name("img") / _style_size('font.file.semibold')
    return ImageFont.truetype(str(font), size)


def semibold(size):
    """Convenience font helper for callers/previews."""
    return _get_font(size)


def get_height_preset(height):
    """Return the calibrated styling preset for the given height."""
    if height in HEIGHT_PRESETS:
        return HEIGHT_PRESETS[height]
    if height >= _style_size('name_value.get_height_preset.geometry'):
        return HEIGHT_PRESETS[_style_size('height.medium')]
    elif height >= _style_size('name_value.get_height_preset.geometry.offset1'):
        return HEIGHT_PRESETS[_style_size('height.intermediate')]
    else:
        return HEIGHT_PRESETS[_style_size('height.small')]


def render_name_value_box_face(width=DEFAULT_BOX_WIDTH, height=DEFAULT_HEIGHT, *, radius=None, scale=_style_size('name_value.name_value_box_face.geometry')):
    """Render antialiased background cell face (rounded rectangle) for the number box."""
    if radius is None:
        radius = get_height_preset(height)["radius"]
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    pad = scale // 2
    draw.rounded_rectangle(
        (pad, pad, w - pad - _style_size('name_value.name_value_box_face.rounded_rectangle'), h - pad - _style_size('name_value.name_value_box_face.rounded_rectangle')),
        radius=radius * scale,
        fill=BG_CELL,
        outline=BORDER_CELL,
        width=scale,
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


# Alias for backwards compatibility
render_name_value_face = render_name_value_box_face


def render_name_value_preview(
    name,
    value,
    width=DEFAULT_WIDTH,
    height=DEFAULT_HEIGHT,
    *,
    box_width=DEFAULT_BOX_WIDTH,
    radius=None,
    scale=_style_size('name_value.name_value_preview.geometry'),
    pad_x=None,
    font_name=None,
    font_value=None,
    bg_canvas=None,
):
    """Render the real native name-value component visual with given name and value.

    Strictly adheres to:
    - 48 px, 40 px, or 32 px height
    - Parameter name on the left, OUTSIDE the frame
    - ONLY the right-side host-formatted value inside the frame (e.g. SEM [ +7 ])
    - NO yellow Q-Link badge or number
    """
    preset = get_height_preset(height)
    r = radius if radius is not None else preset["radius"]
    px = pad_x if pad_x is not None else preset["pad_x"]

    w, h = width * scale, height * scale
    bw = min(box_width, width - _style_size('name_value.name_value_preview.bw')) * scale
    bx = w - bw

    if bg_canvas:
        im = Image.new("RGBA", (w, h), bg_canvas)
    else:
        im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    # Right side: Framed rounded cell for the number ONLY
    pad = scale // 2
    draw.rounded_rectangle(
        (bx + pad, pad, w - pad - _style_size('name_value.name_value_preview.rounded_rectangle'), h - pad - _style_size('name_value.name_value_preview.rounded_rectangle')),
        radius=r * scale,
        fill=BG_CELL,
        outline=BORDER_CELL,
        width=scale,
    )

    fn = font_name or _get_font(int(preset["label_size"] * scale))
    fv = font_value or _get_font(int(preset["value_size"] * scale))

    # Left: Short parameter name (outside the frame)
    draw.text((px * scale, h / 2), str(name), fill=TEXT_LABEL, font=fn, anchor="lm")

    # Right: Formatted value centered inside the framed box
    draw.text((bx + bw / 2, h / 2), str(value), fill=TEXT_VALUE, font=fv, anchor="mm")

    return im.resize((width, height), Image.Resampling.LANCZOS)


def _bounds(rect, focus="No", relative=False):
    bx, by, bw, bh = rect
    return {
        "version": 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Show",
        "whenVisible": "Always",
        "boundsType": "Proportional" if relative else "Absolute",
        "bounds": f"{bx} {by} {bw} {bh}",
    }


def _label_component(name, kind, handle, rect, font_height, colour, justification):
    """Create a native Label child component."""
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Label",
            "data": {
                "version": 1,
                "textStyle": {
                    "version": 1,
                    "font": {
                        "version": 1,
                        "name": _style_size('font.family'),
                        "style": _style_size('font.style.semibold'),
                        "height": float(font_height),
                    },
                    "colour": colour,
                    "justification": justification,
                    "case": "Original",
                },
                "type": kind,
                "handleName": handle,
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds(rect, focus="No"),
    }


def build_name_value_definition(
    name="sharedNativeNameValue",
    rect=(0, 0, DEFAULT_WIDTH, DEFAULT_HEIGHT),
    skins_dir=None,
    *,
    box_width=DEFAULT_BOX_WIDTH,
    popup=POPUP_NUMBER_MENU,
    handle="Data",
    caption_handle="Caption",
    label_size=None,
    value_size=None,
    label_colour=_style_argb("text.label"),
    value_colour=_style_argb("field.value"),
    pad_x=None,
    radius=None,
    with_face_art=True,
):
    """Build the Akai MPC native component definition dictionary for a name-value field.

    Supports:
    1. 48 px standard height, 40 px compact height, or 32 px minimal height (or slot-defined height).
    2. Left-aligned Label for short parameter name outside the frame (handleName='Caption', type='Name').
    3. Framed number box on the right enclosing ONLY the host-formatted value (handleName='Data', type='Value').
    4. NO Q-Link number or yellow badge is placed in the component.
    5. Interactive touch and editing via 'Show Overlay' to 'fx editor number menu'
       on Mouse Down and Enter Pressed across the entire control footprint.
    6. Hardware Q-Link and data wheel editing enabled (disableCoarseDataWheel=False).
    7. SDK Focus ring child included for hardware navigation, focused on the number box.
    """
    w, h = rect[2], rect[3]
    preset = get_height_preset(h)

    r = radius if radius is not None else preset["radius"]
    px = pad_x if pad_x is not None else preset["pad_x"]
    lbl_sz = label_size if label_size is not None else preset["label_size"]
    val_sz = value_size if value_size is not None else preset["value_size"]

    bw = min(box_width, w - _style_size('name_value.build_name_value_definition.bw'))
    bx = w - bw
    by = 0
    bh = h

    triggers = ("Mouse Down", "Enter Pressed")

    actions = [
        {
            "version": 1,
            "onAction": trigger,
            "handler": "Show Overlay",
            "handleName": handle,
            "additionalData": popup,
        }
        for trigger in triggers
    ]

    # Root background is transparent: parameter name sits on the parent panel without a bounding frame
    bg_unfocused = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    bg_focused = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}

    # Focus ring surrounds the framed number box
    focus_ring = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "focus",
            "type": "Focus",
            "data": {
                "version": 1,
                "backgroundColour": _style_argb("text.white", _style_opacity("focus")),
                "outlineColour": _style_argb("focus.field"),
                "backgroundInset": _style_size('name_value.build_name_value_definition.focus.ring'),
                "outlineThickness": _style_size('name_value.build_name_value_definition.focus.ring.offset1'),
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": dict(_bounds((bx, by, bw, bh)), whenVisible="WhenFocussed"),
    }

    children = [focus_ring]

    # Optional antialiased background face image decoration for the number box only
    if skins_dir and with_face_art:
        skins_path = Path(skins_dir)
        skins_path.mkdir(parents=True, exist_ok=True)
        suffix = "" if r == preset["radius"] else f"_r{r}"
        filename = f"name_value_box_{bw}x{bh}{suffix}.png"
        face_im = render_name_value_box_face(bw, bh, radius=r)
        face_im.save(skins_path / filename)

        decor = {
            "version": 2,
            "componentData": {
                "version": 1,
                "name": "Face",
                "type": "Image",
                "data": {
                    "version": 2,
                    "imageType": "Regular",
                    "colour": _style_native_color("transparent"),
                    "image": filename,
                },
            },
            "handle remapping": {"version": 1, "map": []},
            "bounds": _bounds((bx, by, bw, bh)),
        }
        children.append(decor)

    # Left: Parameter name outside the frame
    name_w = bx - px
    label_rect = (px, 0, name_w, h)
    label_comp = _label_component(
        "name",
        "Name",
        caption_handle,
        label_rect,
        lbl_sz,
        label_colour,
        "left verticallyCentred",
    )

    # Right: Value centered inside the framed number box
    value_rect = (bx, 0, bw, bh)
    value_comp = _label_component(
        "value",
        "Value",
        handle,
        value_rect,
        val_sz,
        value_colour,
        "horizontallyCentred verticallyCentred",
    )

    children.extend([label_comp, value_comp])

    return {
        "version": 2,
        "actions": actions,
        "backgroundData": {
            "version": 1,
            "focussed": bg_focused,
            "unfocussed": bg_unfocused,
        },
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": children,
    }


def create_name_value_entry(
    name,
    kind,
    rect,
    param=None,
    *,
    data_handle=None,
    caption_handle=None,
    remap=None,
    gates=(),
    focus="Yes",
):
    """Instantiate a native name-value component entry for placement on an MPC tab or parent component."""
    if remap is not None:
        value_map = copy.deepcopy(list(remap))
    elif data_handle is not None or caption_handle is not None:
        value_map = []
        if data_handle is not None:
            value_map.append({"key": "Data", "value": data_handle})
        elif param is not None:
            value_map.append({"key": "Data", "value": f"Parameter {param}"})
        if caption_handle is not None:
            value_map.append({"key": "Caption", "value": caption_handle})
    else:
        value_map = [] if param is None else [{"key": "Data", "value": f"Parameter {param}"}]

    bounds = _bounds(rect, focus=focus)
    if gates:
        bounds["version"] = 2
        bounds["showWhenDataModelInvalid"] = "Hide"
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": kind,
            "data": {"version": 1, "handleName": "Data"},
        },
        "handle remapping": {"version": 1, "map": value_map},
        "bounds": bounds,
    }


__all__ = [
    "BG_CELL",
    "BORDER_CELL",
    "BG_CELL_FOCUS",
    "TEXT_LABEL",
    "TEXT_VALUE",
    "DEFAULT_HEIGHT",
    "DEFAULT_WIDTH",
    "DEFAULT_BOX_WIDTH",
    "DEFAULT_RADIUS",
    "DEFAULT_PAD_X",
    "POPUP_NUMBER_MENU",
    "HEIGHT_VARIANTS",
    "HEIGHT_PRESETS",
    "semibold",
    "get_height_preset",
    "render_name_value_box_face",
    "render_name_value_face",
    "render_name_value_preview",
    "build_name_value_definition",
    "create_name_value_entry",
]
