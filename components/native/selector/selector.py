# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reusable native MPC list selector component and variant factories.

Native component using shared family style data.
Key features:
- SHAPE variant: static caption 'SHAPE', current value label, stepper chevrons (< and >),
  center touch opens shape choice list, single tap writes to the bound instance's shape parameter.
- Factory list overlay: left-side drawer with title header, choice row names, active checkmark
  ('aktuális pipa') gated by IndexedEnabling/{index}/{count}/{handle}, single tap selection,
  and backdrop dismissal without modifying other instances.
- Filter type 0 PNG encoding: uncompressed scanline filter 0 PNG assets compliant with Akai MPC
  hardware decoders (Rule: verified hardware assets).
- Backward compatibility: fully compatible with existing selector_definition callers and existing skins.
"""

import copy
from hashlib import sha256
import math
from pathlib import Path

from components.native.button.button import TRANSPARENT_HIT, gradient_face, write_transparent_hit
from components.native.level.level import value_label
from components.style import (
    focus_ring_data,
    native_font_em as _native_font_em,
    argb as _style_argb,
    color as _style_color,
    control_look as _control_look,
    opacity as _style_opacity,
    native_color as _style_native_color,
    rgb as _style_rgb,
    rgba as _style_rgba,
    rect as _style_rect,
    size as _style_size,
)
import struct
import tempfile
import zlib
from PIL import Image, ImageDraw, ImageFont

# Standard dimensions
STANDALONE_OSC_SIZE = tuple(_style_size('selector.standalone_osc_size'))
COMPACT_SELECTOR_HEIGHT = _style_size('height.medium')
MODAL_MARKER_COLS = tuple(_style_size('selector.modal_marker_cols'))


def semibold(size):
    font = Path(__file__).with_name("img") / _style_size('font.file.semibold')
    return ImageFont.truetype(str(font), size)


def _entry(name, kind, data, rect, remaps=(), gates=(), focus="Yes"):
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": " ".join(map(str, rect)),
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": [{"key": a, "value": b} for a, b in remaps]},
        "bounds": bounds,
    }


def button(name, rect, on, off=None, index=1, count=1, remaps=(), gates=()):
    return _entry(
        name,
        "Button",
        {
            "version": 2,
            "onImage": on,
            "offImage": off or on,
            "buttonId": index,
            "numButtonsInGroup": count,
            "handleName": "Data",
            "gestureBehaviour": "Instant",
        },
        rect,
        remaps,
        gates,
    )


def _value_label(name, rect, height=_style_size('selector._value_label.geometry')):
    return value_label(name, rect, height)


def build_choice_definition(source_def, overlay_name, rect, label, decoration=(), label_height=_style_size('font.value')):
    """Use the project focus ring and let the parent handle touch and wheel input."""
    definition = copy.deepcopy(source_def) if source_def else {"actions": [], "componentsData": []}
    menu_triggers = ("Mouse Down", "Enter Pressed")
    definition["actions"] = [
        action for action in definition.get("actions", [])
        if action.get("handler") != "Show Overlay" and action.get("onAction") not in menu_triggers
    ]
    for trigger in menu_triggers:
        definition["actions"].append({
            "version": 1,
            "onAction": trigger,
            "handler": "Show Overlay",
            "handleName": "Data",
            "additionalData": overlay_name,
        })
    if decoration:
        for state in ("focussed", "unfocussed"):
            if state in definition.get("backgroundData", {}):
                definition["backgroundData"][state].update(colour=_style_native_color("transparent"), image="")
    bounds = (0, 0, rect[2], rect[3])
    children = [child for child in definition.get("componentsData", [])
                if child.get("componentData", {}).get("type") == "Focus"]
    for child in children:
        child["componentData"]["data"] = focus_ring_data()
        child["bounds"]["bounds"] = "%d %d %d %d" % bounds
    definition["componentsData"] = children + list(decoration) + [_value_label(label, bounds, label_height)]
    return definition


def _image_component(name, file, rect):
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Image",
            "data": {"version": 2, "imageType": "Regular", "colour": _style_native_color("transparent"), "image": file},
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % tuple(rect),
        },
    }


_image = _image_component


def _choice_overlay(overlay_name):
    items = [
        _image_component("%s background" % overlay_name, "osc_routing_overlay_bg.png", (0, 0, _style_size('selector._choice_overlay.items'), _style_size('selector._choice_overlay.items.offset1'))),
        {
            "version": 2,
            "componentData": {
                "version": 1,
                "name": "%s close" % overlay_name,
                "type": "MenuOverlay",
                "data": {
                    "version": 1,
                    "actions": [
                        {
                            "version": 1,
                            "onAction": "Focus Lost",
                            "handler": "Focus Lost",
                            "handleName": "Focus Lost",
                            "additionalData": "",
                        }
                    ],
                    "withNumbers": False,
                    "handleName": "Data",
                },
            },
            "handle remapping": {"version": 1, "map": []},
            "bounds": {
                "version": 1,
                "acceptsHWFocus": "No",
                "showWhenDataModelInvalid": "Show",
                "whenVisible": "Always",
                "boundsType": "Absolute",
                "bounds": _style_rect("selector._choice_overlay.items.offset2"),
            },
        },
    ]
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": _style_argb("menu.background"), "image": ""},
            "unfocussed": {"version": 1, "colour": _style_argb("menu.background"), "image": ""},
        },
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "hideQLinkBounds": True,
        "componentsData": items,
    }



def save_png_filter0(image, path, neutral=False):
    """Encodes and saves an image with strict PNG scanline filter type 0 for Akai MPC hardware."""
    im = image.convert("RGB" if neutral else ("RGBA" if "A" in image.getbands() else "RGB"))
    width, height = im.size
    is_rgba = "A" in im.getbands()
    channels = 4 if is_rgba else 3
    colour_type = 6 if is_rgba else 2
    raw_data = im.tobytes()
    stride = width * channels
    lines = [b"\x00" + raw_data[y * stride:(y + 1) * stride] for y in range(height)]
    raw = b"".join(lines)
    idat = zlib.compress(raw, 9)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0))
        + chunk(b"IDAT", idat)
        + chunk(b"IEND", b"")
    )
    Path(path).write_bytes(png)


def render_pipa_icon(size=_style_size('selector.pipa_icon.geometry'), color=_style_color("text.status.on")):
    """Renders a high-contrast checkmark (pipa) for the active list item."""
    scale = _style_size('selector.pipa_icon.scale')
    s = size * scale
    im = Image.new("RGBA", (s, s), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)
    points = [
        (int(s * 0.20), int(s * 0.52)),
        (int(s * 0.42), int(s * 0.78)),
        (int(s * 0.82), int(s * 0.26)),
    ]
    draw.line(points, fill=color, width=_style_size('selector.pipa_icon.line') * scale // 2, joint="curve")
    return im.resize((size, size), Image.Resampling.LANCZOS)


WAVE_SHAPES = ("sine", "saw", "square", "pulse", "triangle", "sh", "noise")


def _wave_shape_points(shape):
    """One icon's polyline as (t, y) with t in 0..1 along the icon and y in -1..1 (up positive)."""
    if shape == "saw":
        return [(0, -1), (0.5, 1), (0.5, -1), (1, 1)]
    if shape == "square":
        return [(0, 0), (0, 1), (0.5, 1), (0.5, -1), (1, -1), (1, 0)]
    if shape == "pulse":
        return [(0, -1), (0.4, -1), (0.4, 1), (0.6, 1), (0.6, -1), (1, -1)]
    if shape == "triangle":
        return [(0, 0), (0.25, 1), (0.75, -1), (1, 0)]
    if shape == "sh":
        levels = _style_size("wave_icons")["sh_levels"]
        points = []
        for i, level in enumerate(levels):
            points += [(i / len(levels), level), ((i + 1) / len(levels), level)]
        return points
    if shape == "noise":
        levels = _style_size("wave_icons")["noise_levels"]
        return [(i / (len(levels) - 1), level) for i, level in enumerate(levels)]
    raise ValueError(f"wave shape must be one of {WAVE_SHAPES}: {shape!r}")


def render_wave_icon(width=_style_size('selector.wave.width'), height=_style_size('selector.wave_icon.geometry'), color=_style_color("selector.wave"), shape="sine"):
    """Renders a waveform icon (default the sine) for wave selectors.

    The other shapes (saw, square, pulse, triangle, sh = stepped random, noise = jagged random) use the sine's box, margins
    (selector.wave_icon.points), amplitude (38% of the height), stroke (stroke.icon) and colour; corners are round (curve joints, round end caps).
    """
    scale = _style_size('selector.wave_icon.scale')
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)
    if shape != "sine":
        margin, stroke = _style_size('selector.wave_icon.points') * scale, _style_size('stroke.icon') * scale
        points = [(margin + t * (w - 2 * margin), h // 2 - y * h * 0.38) for t, y in _wave_shape_points(shape)]
        draw.line(points, fill=color, width=stroke, joint="curve")
        for x, y in (points[0], points[-1]):
            draw.ellipse((x - stroke / 2, y - stroke / 2, x + stroke / 2, y + stroke / 2), fill=color)
        return im.resize((width, height), Image.Resampling.LANCZOS)
    points = [
        (x, h // 2 - int((h * 0.38) * math.sin((x - _style_size('selector.wave_icon.points') * scale) * math.tau / (w - _style_size('selector.wave_icon.points.offset1') * scale))))
        for x in range(_style_size('selector.wave_icon.points') * scale, w - _style_size('selector.wave_icon.points') * scale)
    ]
    draw.line(points, fill=color, width=_style_size('stroke.icon') * scale)
    return im.resize((width, height), Image.Resampling.LANCZOS)


def build_step_buttons(skins_dir, width, height, hit_width, labels, count, *, neutral=False, handle_name="Data"):
    """Generates previous/next stepping buttons with IndexedEnabling/{index}/{count}/{handle_name} gates."""
    skins_dir = Path(skins_dir)
    color = _style_color("selector.neutral.ink") if neutral else _style_color("text.muted")
    for delta, key in ((-1, "Previous"), (1, "Next")):
        left = 0 if delta < 0 else width - hit_width
        for index in range(max(0, -delta), min(count, count - delta)):
            target = index + delta
            label = labels[target] if labels and 0 <= target < len(labels) else None
            stem = label.lower().replace(" ", "_") if label else key.lower()
            tag = "_neutral" if neutral else ""
            filename = f"selector_step_{stem}_{hit_width}x{height}{tag}.png"
            path = skins_dir / filename
            if not path.is_file():
                scale = _style_size('sampling.standard')
                im = Image.new("RGBA", (hit_width * scale, height * scale), _style_rgba("ink.black", _style_opacity("transparent")))
                draw = ImageDraw.Draw(im)
                if label:
                    try:
                        font = semibold(_style_size('font.label') * scale)
                    except Exception:
                        font = ImageFont.load_default()
                    draw.text((im.width // 2, im.height // 2), label, color,
                              font=font, anchor="mm")
                else:
                    cx, cy = im.width // 2, im.height // 2
                    back = cx - delta * _style_size('selector.step.chevron.half_width')
                    tip = cx + delta * _style_size('selector.step.chevron.half_width')
                    draw.line(((back, cy - _style_size('selector.step.chevron.half_height')), (tip, cy), (back, cy + _style_size('selector.step.chevron.half_height'))),
                              fill=color, width=_style_size('stroke.icon') * scale)
                resized = im.resize((hit_width, height), Image.Resampling.LANCZOS)
                save_png_filter0(resized, path)
            gate = f"IndexedEnabling/{index}/{count}/{handle_name}"
            step = button(f"{key} from {index}", (left, 0, hit_width, height),
                          filename, index=target, count=count, gates=(gate,))
            step["bounds"]["acceptsHWFocus"] = "No"
            step["componentData"]["data"]["handleName"] = handle_name
            yield step


def dropdown_asset_name(width, height):
    return f"selector_dropdown_{width}x{height}.png"


# Dropdown face tokens per look: fill top, fill bottom, keyline top, keyline bottom, gradient span (start/end as a
# fraction of the box height: constant above `start`, linear to `end`, constant below). "neon" is the skin's theme
# (mod_row, FX). "shaded" is the Play-panel dropdown with the contrast of the catalogue's Voice selector (owner,
# 2026-11-27): fill #141a38 to #0b0f24, keyline #27306a, the same gradient shape as "neon". Only the catalogue and the
# Header ask for "shaded"; other components keep "neon".
DROPDOWN_LOOKS = {
    "neon": ("theme.neon.panel0", "theme.neon.panel1", "theme.neon.keyline", "theme.neon.keyline",
             "selector.dropdown.gradient.neon"),
    "shaded": ("selector.dropdown.top", "selector.dropdown.bottom", "selector.dropdown.border",
               "selector.dropdown.border.bottom", "selector.dropdown.gradient.shaded"),
}


def render_dropdown_face(width, height, *, chevron=True, look="neon", size=None, inset=None, neutral=False, radius=None):
    """Reference MenuButton panel and V glyph, both inside the native touch bounds.

    `size` names a selector.dropdown.size.<height> token whose inset (0: the drawn box fills the slot) and chevron measures replace the default ones.
    `inset` overrides the inset (0: the box fills the slot, as the Play-panel selectors do). `radius` overrides the corner radius (the Play selectors use radius.control). `neutral` is the inactive / read-only
    face (selector.neutral.surface and border, flat grey): the Play OSC source selector and the disabled variants."""
    if look == "neon" and _control_look() == "shaded":
        look = "shaded"   # the skin default follows control.look (A/B: SYNTHYBOY_CONTROL_LOOK=flat)
    top_token, bottom_token, keyline_token, keyline_bottom_token, gradient_token = DROPDOWN_LOOKS[look]
    span = _style_size(gradient_token)
    geometry = _style_size("selector.dropdown")
    if size:
        geometry.update({k: v for k, v in _style_size(size).items() if k == "inset" or k.startswith("chevron_")})
    theme = _style_size("theme.neon")
    scale = _style_size("sampling.standard")
    size = (width * scale, height * scale)
    inset_px = geometry["inset"] if inset is None else inset
    inset = inset_px * scale
    radius = round((theme["rControl"] if radius is None else radius) * scale)
    fill = (_style_rgb(top_token), _style_rgb(bottom_token))
    line = (_style_rgb(keyline_token), _style_rgb(keyline_bottom_token))
    if neutral:
        fill = (_style_rgb("selector.neutral.surface"),) * 2
        line = (_style_rgb("selector.neutral.border"),) * 2
    face, pen = gradient_face(size, inset=inset, radius=radius, frame=round(theme["frame"] * scale), span=span,
                              fill=fill, line=line)
    if chevron:
        cx = (width - inset_px - geometry["chevron_lane"] / 2) * scale
        cy = height * scale / 2
        dx, dy = geometry["chevron_width"] * scale / 2, geometry["chevron_height"] * scale / 2
        pen.line(((cx - dx, cy - dy), (cx, cy + dy), (cx + dx, cy - dy)),
                 fill=_style_color("selector.neutral.ink" if neutral else "theme.neon.textDim"),
                 width=round(geometry["chevron_stroke"] * scale), joint="curve")
    return face.resize((width, height), Image.Resampling.LANCZOS)


def dropdown_definition(width, height, *, actions, skins_dir=None, value_handle="Data", value_name="Value",
                        remaps=(), filename=None, frame_name="Selector face", look="neon", size=None):
    """A native value selector whose caller owns all commands and value bindings."""
    filename = filename or dropdown_asset_name(width, height)
    if look != "neon":
        filename = filename.replace(".png", f"_{look}.png")
    if size:
        filename = filename.replace(".png", f"_{size.rsplit('.', 1)[-1]}.png")
    if skins_dir is not None:
        directory = Path(skins_dir)
        directory.mkdir(parents=True, exist_ok=True)
        save_png_filter0(render_dropdown_face(width, height, look=look, size=size), directory / filename)
    style = _style_size("selector.dropdown")
    inset = style["inset"]
    left = inset + style["text_inset"]
    if size:
        # A sized dropdown: the value box spans the full height, starts pad_x from the edge and ends chevron_gap before
        # the chevron lane, so the text never reaches the arrow (available = box - pad_x - chevron_lane - chevron_gap).
        pad = _style_size(size)["pad_x"]
        rect = (pad, 0, width - pad - style["chevron_lane"] - _style_size("selector.dropdown.chevron_gap"), height)
    else:
        rect = (left, left, width - left - inset - style["chevron_lane"], height - 2 * left)
    value = _value_label(value_name, rect, style["font"])
    value["componentData"]["data"]["handleName"] = value_handle
    value["componentData"]["data"]["textStyle"]["colour"] = _style_argb("theme.neon.text")
    value["handle remapping"]["map"] = [{"key": key, "value": target} for key, target in remaps]
    face = _image_component(frame_name, filename, (0, 0, width, height))
    definition = passive_layer([face, value])
    definition.update(ignoreMousePresses=False, actions=copy.deepcopy(list(actions)))
    return definition


def selector_definition(
    template,
    popup,
    slot,
    skins_dir,
    *,
    icon=None,
    title="Data",
    name=None,
    stepper=False,
    arrows=False,
    neutral=False,
    frame_name="Selector face",
    filename=None,
    label_height=_style_size('font.selector'),
    justification=None,
    caption_text=None,
    frame=None,
    neighbors=(),
    steps=0,
    play_face=False,
    dropdown_look="neon",
    dropdown_size=None,
):
    """Creates a shared native MPC selector definition with support for all variants."""
    if template is None:
        template = {
            "version": 2,
            "actions": [],
            "backgroundData": {
                "version": 1,
                "focussed": {"version": 1, "colour": _style_native_color("transparent"), "image": ""},
                "unfocussed": {"version": 1, "colour": _style_native_color("transparent"), "image": ""},
            },
            "ignoreMousePresses": False,
            "disableCoarseDataWheel": False,
            "componentsData": [],
        }
    skins_dir = Path(skins_dir)
    skins_dir.mkdir(parents=True, exist_ok=True)
    width, slot_height = slot

    if dropdown_size:
        height = slot_height  # a sized dropdown fills its slot (32 / 40 / 48 px)
    elif frame:
        frame_name, filename = frame
        height = slot_height
    elif play_face:
        height = _style_size('selector.selector_definition.height') if name else _style_size('selector.selector_definition.height.offset1')
    elif name or caption_text:
        height = _style_size('selector.selector_definition.height')
        if slot_height == _style_size('height.medium'):
            height = slot_height
    elif slot_height == _style_size('selector.selector_definition.geometry') and not neutral and not stepper and not arrows and not neighbors and not steps:
        height = _style_size('selector.selector_definition.height')
    else:
        height = _style_size('selector.selector_definition.height.offset2')

    if slot_height < height:
        raise ValueError(f"the selector face is {height} px high, the slot is only {slot_height} px")
    top = (slot_height - height) // 2
    scale = _style_size('selector.selector_definition.scale')

    navigable = bool(neighbors or steps)

    plain = not (frame or play_face or icon or name or caption_text or stepper or arrows or navigable or neutral)
    if plain:
        # Keep the caller's navigation, focus and any explicit typography overrides.
        label_name = "Title" if title == "Data" else title
        popup_definition = build_choice_definition(template, popup, (0, 0, width, slot_height), label_name)
        for action in popup_definition["actions"]:
            if action.get("handler") == "Show Overlay":
                action["handleName"] = title
        native = dropdown_definition(width, height, skins_dir=skins_dir, actions=popup_definition["actions"],
                                    value_handle=title, value_name=label_name, filename=filename, frame_name=frame_name,
                                    look=dropdown_look, size=dropdown_size)
        frame_child, value = native["componentsData"]
        for child in (frame_child, value):
            x, y, w, h = map(int, child["bounds"]["bounds"].split())
            child["bounds"]["bounds"] = f"{x} {y + top} {w} {h}"
        text_style = value["componentData"]["data"]["textStyle"]
        if justification:
            text_style["justification"] = justification
        if label_height != _style_size("font.selector"):
            text_style["font"]["height"] = float(label_height)
        popup_definition["backgroundData"] = native["backgroundData"]
        popup_definition["componentsData"][-1:] = native["componentsData"]
        return popup_definition

    if not frame:
        arrow_col = _style_rgb("selector.neutral.border") if neutral else _style_color("text.muted")

        # The face is the dropdown component's box (shared gradient, control.look); only the arrows are drawn here.
        base = render_dropdown_face(width, height, chevron=not (stepper or arrows or navigable), inset=0, neutral=neutral,
                                    radius=_style_size('radius.control'))
        face = Image.new("RGBA", (width * scale, height * scale))
        pen = ImageDraw.Draw(face)

        mid_y = height * scale // 2
        if stepper or arrows:
            # Left chevron <
            x_l = _style_size('selector.selector_definition.x.l') * scale
            pen.line(((x_l + _style_size('selector.selector_definition.line') * scale, mid_y - _style_size('selector.selector_definition.line.offset1') * scale),
                      (x_l - _style_size('selector.selector_definition.line') * scale, mid_y),
                      (x_l + _style_size('selector.selector_definition.line') * scale, mid_y + _style_size('selector.selector_definition.line.offset2') * scale)),
                     fill=arrow_col, width=_style_size('stroke.icon') * scale)
            # Right chevron >
            x_r = (width - _style_size('selector.selector_definition.x.r')) * scale
            pen.line(((x_r - _style_size('selector.selector_definition.line') * scale, mid_y - _style_size('selector.selector_definition.line.offset3') * scale),
                      (x_r + _style_size('selector.selector_definition.line') * scale, mid_y),
                      (x_r - _style_size('selector.selector_definition.line') * scale, mid_y + _style_size('selector.selector_definition.line.offset4') * scale)),
                     fill=arrow_col, width=_style_size('stroke.icon') * scale)
            if stepper and not arrows:
                x_v = (width - _style_size('selector.selector_definition.x.v')) * scale
                pen.line(((x_v - _style_size('selector.selector_definition.line.offset5') * scale, mid_y - _style_size('selector.selector_definition.line.offset6') * scale),
                          (x_v, mid_y + _style_size('selector.selector_definition.line') * scale),
                          (x_v + _style_size('selector.selector_definition.line.offset7') * scale, mid_y - _style_size('selector.selector_definition.line.offset8') * scale)),
                         fill=arrow_col, width=_style_size('stroke.icon') * scale)

        if filename is None:
            tag = ""
            if stepper:
                tag += "_stepper"
            elif arrows:
                tag += "_arrows"
            if neutral:
                tag += "_neutral"
            if navigable:
                tag += "_step"
            filename = f"selector_face_{width}x{height}{tag}.png"

        resized = base
        resized.alpha_composite(face.resize((width, height), Image.Resampling.LANCZOS))
        if play_face:
            resized.save(skins_dir / filename)
        else:
            save_png_filter0(resized, skins_dir / filename)

    decorations = [_image_component(frame_name, filename, (0, top, width, height))]

    if navigable:
        left = max(_style_size('selector.hit.minimum'), width // 4) if neighbors else (_style_size('selector.selector_definition.left.offset1') if width >= _style_size('selector.selector_definition.left.offset5') else _style_size('selector.selector_definition.left.offset2'))
    elif icon:
        decorations.append(_image_component("Selector icon", icon, (_style_size('selector.selector_definition._image_component'), top + (height - _style_size('selector.selector_definition._image_component.offset3')) // 2, _style_size('selector.selector_definition._image_component.offset1'), _style_size('selector.selector_definition._image_component.offset2'))))
        left = _style_size('selector.selector_definition.left')
    elif stepper or arrows:
        left = _style_size('selector.selector_definition.left.offset3')
    else:
        left = _style_size('selector.selector_definition.left.offset4')

    text_width = width - left - (left if navigable else (_style_size('selector.selector_definition.text.width') if stepper or arrows else _style_size('selector.selector_definition.text.width.offset1')))
    title_label = "Title" if play_face or title == "Data" else title
    definition = build_choice_definition(
        template, popup, (0, 0, width, slot_height),
        title_label, decorations, label_height=label_height if label_height != _style_size('selector.selector_definition.definition') else (_style_size('selector.selector_definition.definition.offset1') if neighbors else _style_size('selector.selector_definition.definition'))
    )

    for action in definition["actions"]:
        if action.get("handler") == "Show Overlay":
            action["handleName"] = "Data" if play_face else title
    value = definition["componentsData"][-1]
    value["componentData"]["data"]["handleName"] = title
    val_just = justification or ("horizontallyCentred verticallyCentred" if (navigable or arrows) else "left verticallyCentred")
    value["componentData"]["data"]["textStyle"]["justification"] = val_just
    value_y = _style_size('selector.selector_definition.value.bounds.bounds.offset1') if name or caption_text else 0
    value_h = min(_style_size('selector.selector_definition.value.bounds.bounds'), height - value_y) if name or caption_text else height
    value["bounds"]["bounds"] = f"{left} {top + value_y} {text_width} {value_h}"

    if name or caption_text:
        caption = _value_label("Name", (left, top + _style_size('selector.selector_definition.caption.offset2'), text_width, _style_size('selector.selector_definition.caption.offset1')), _style_size('selector.selector_definition.caption'))
        data = caption["componentData"]["data"]
        if caption_text is not None:
            data.update(type="Text", text=caption_text)
            if name:
                data["handleName"] = name
        else:
            data.update(type="Name", handleName=name)
        data["textStyle"].update(colour=_style_argb("text.muted"), justification=val_just)
        definition["componentsData"].append(caption)

    if navigable:
        count = steps or len(neighbors)
        step_list = list(build_step_buttons(skins_dir, width, slot_height, left, neighbors, count, neutral=neutral, handle_name=title))
        definition["componentsData"].extend(step_list)

    return definition


def build_shape_selector(
    template,
    popup,
    slot=(_style_size('selector.build_shape_selector.geometry'), _style_size('selector.build_shape_selector.geometry.offset1')),
    skins_dir=None,
    *,
    title="Data",
    caption="SHAPE",
    stepper=False,
    arrows=False,
    steps=0,
    neighbors=(),
    neutral=False,
):
    """Builds the SHAPE selector as a dropdown face (caption, value, down chevron)."""
    return selector_definition(
        template,
        popup,
        slot,
        skins_dir,
        title=title,
        caption_text=caption,
        stepper=stepper,
        arrows=arrows,
        steps=steps,
        neighbors=neighbors,
        neutral=neutral,
        label_height=_style_size('selector.build_shape_selector.selector_definition'),
    )


TITLED_SELECTOR_SIZE = (
    _style_size('control.width'),
    _style_size('height.high') + _style_size('active_text.default.height'),
)


def build_titled_selector(
    template,
    popup="fx editor choice menu",
    slot=TITLED_SELECTOR_SIZE,
    skins_dir=None,
    *,
    title="Data",
    caption=None,
    caption_handle="Caption",
    title_text=None,
    title_height=None,
    selector_height=None,
    label_height=None,
    value_label_height=_style_size('font.selector'),
    play_face=True,
    **kwargs,
):
    """Additive public factory composing an outer title label with an existing Selector.

    Args:
        template: Base template dictionary or None.
        popup: Choice overlay / drawer name (default: 'fx editor choice menu').
        slot: Total outer (width, height) bounding box (default: (control.width, height.high + active_text.default.height) = (202, 80)).
        skins_dir: Path to directory for generated skin assets.
        title: Handle name for the value/selection parameter (default: 'Data').
        caption: Caller title text or handle name (unambiguous).
        caption_handle: Handle name for the title label when bound dynamically (default: 'Caption').
        title_text: Explicit static title text string (overrides dynamic handle).
        title_height: Height of the title label aperture (default: active_text.default.height = 24 px).
        selector_height: Height of the selector touch aperture (default: slot height - title_height >= height.high = 56 px).
        label_height: Font height for the title label (default: font.button / 18.0).
        value_label_height: Font height for the selector value text (default: font.selector / 19.0).
        play_face: Whether to use the styled play selector face (default: True).
        **kwargs: Forwarded to existing selector_definition (icons, arrows, stepper, etc.).
    """
    if kwargs.get("boundsType") == "Proportional" or kwargs.get("bounds_type") == "Proportional":
        raise ValueError("Proportional boundsType is not supported in titled selector layout")

    width, total_height = slot
    if width <= 0 or total_height <= 0:
        raise ValueError(f"Outer slot dimensions ({width}, {total_height}) must be positive")

    title_h = int(title_height) if title_height is not None else int(_style_size('active_text.default.height'))
    if title_h <= 0:
        raise ValueError(f"Title height ({title_h}) must be positive")

    child_h = int(selector_height) if selector_height is not None else (total_height - title_h)
    min_selector_h = int(_style_size('height.high'))
    if child_h < min_selector_h:
        raise ValueError(f"Selector child height ({child_h}) must be >= {min_selector_h} (height.high)")
    if child_h + title_h > total_height:
        raise ValueError(f"Selector child height ({child_h}) + title height ({title_h}) exceeds outer height ({total_height})")

    title_font = float(label_height) if label_height is not None else float(_style_size('font.button'))

    if caption is not None and title_text is None:
        if caption == "Caption" or caption.startswith("Parameter "):
            caption_handle = caption
        else:
            title_text = caption

    child_def = selector_definition(
        template,
        popup,
        (width, child_h),
        skins_dir,
        title=title,
        play_face=play_face,
        label_height=value_label_height,
        **kwargs,
    )

    definition = copy.deepcopy(child_def)
    for child in definition.get("componentsData", []):
        bounds = child.get("bounds", {})
        btype = bounds.get("boundsType", "Absolute")
        if btype == "Absolute":
            x, y, w, h = map(int, bounds["bounds"].split())
            bounds["bounds"] = f"{x} {y + title_h} {w} {h}"
        else:
            raise ValueError(
                f"Unsupported boundsType '{btype}' in titled selector child "
                f"'{child.get('componentData', {}).get('name', '')}': only 'Absolute' is supported"
            )

    title_component = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "name",
            "type": "Label",
            "data": {
                "version": 1,
                "textStyle": {
                    "version": 1,
                    "font": {
                        "version": 1,
                        "name": _style_size('font.family'),
                        "style": _style_size('font.style.semibold'),
                        "height": title_font,
                    },
                    "colour": _style_argb("text.caption"),
                    "justification": "left verticallyCentred",
                    "case": "Original",
                },
                "type": "Text" if title_text is not None else "Name",
                "handleName": caption_handle,
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": f"0 0 {width} {title_h}",
        },
    }
    if title_text is not None:
        title_component["componentData"]["data"]["text"] = title_text

    definition["componentsData"].append(title_component)
    return definition


titled_selector_definition = build_titled_selector


def build_native_list_overlay(name):
    """The host drawer owns the checkmark, single selection and dismissal."""
    overlay = _choice_overlay(name)
    overlay["componentsData"] = [child for child in overlay["componentsData"]
                                 if child["componentData"]["type"] == "MenuOverlay"]
    return overlay


def filter_chain_definition(skins_dir, popup):
    """One native tap target that opens the filter routing menu (Parallel, F1 to F2, F2 to F1), with two independent power lamps. The face names the routing ("CHAIN"), not the filter: the edited
    filter is the hardware sub-tab (the page title says which), so nothing on the chip looks like a filter selector."""
    geometry = _style_size("filter.chain")
    w, h = geometry["width"], geometry["height"]
    face_y, gap = geometry["face_y"], geometry["lamp_gap"]
    size = (w, h - face_y)
    faces = Image.new("RGBA", (w, size[1] * 4))
    for n in range(2):
        face = render_dropdown_face(*size, chevron=False)
        draw = ImageDraw.Draw(face)
        font_size = _style_size("selector.dropdown.preview_font")
        while font_size > 8 and draw.textlength("CHAIN", font=semibold(font_size)) > w - 8:   # the label must fit the 56 px chip
            font_size -= 1
        draw.text((w / 2, size[1] / 2), "CHAIN", font=semibold(font_size), fill=_style_color("theme.neon.text"), anchor="mm")
        for frame in (2 * n, 2 * n + 1):
            faces.paste(face, (0, frame * size[1]))
    filename = "filter_chain_selected.png"
    save_png_filter0(faces, Path(skins_dir) / filename)
    items = [_entry("Edited filter", "Image", {"version": 2, "imageType": "FilmStrip", "image": filename,
                    "colour": _style_native_color("transparent"), "numFrames": 4, "handleName": "Edited"},
                    (0, face_y, *size), focus="No")]
    lamp_w = (w - gap) // 2
    lamp_h = geometry["lamp_height"]
    lamps = Image.new("RGBA", (lamp_w, 2 * lamp_h))
    for frame, colour in enumerate(("theme.neon.track", "accent.cyan")):
        top = frame * lamp_h
        ImageDraw.Draw(lamps).rounded_rectangle((0, top, lamp_w - 1, top + lamp_h - 1),
                                                radius=lamp_h // 2, fill=_style_color(colour))
    lamp_image = "filter_chain_power.png"
    save_png_filter0(lamps, Path(skins_dir) / lamp_image)
    for n in range(2):
        items.append(_entry(f"F{n + 1} power", "Image",
                            {"version": 2, "imageType": "FilmStrip", "image": lamp_image, "colour": _style_native_color("transparent"),
                             "numFrames": 2, "handleName": f"Power{n + 1}"},
                            (n * (lamp_w + gap), geometry["lamp_y"], lamp_w, lamp_h), focus="No"))
    definition = passive_layer(items)
    definition.update(ignoreMousePresses=False, disableCoarseDataWheel=True, actions=[
        {"version": 1, "onAction": event, "handler": "Show Overlay", "handleName": "Data", "additionalData": popup}
        for event in ("Mouse Down", "Enter Pressed")])
    return definition


def bind_list_choices(tui, parameter_index, title, choices):
    """Supply the drawer labels and return its titled, instance-specific handle."""
    choices = tuple(choices)
    if parameter_index < 0 or len(choices) < 2 or not title or "]" in title:
        raise ValueError("Expected a parameter index, a drawer title and at least two choices")
    ensure_routing_replacement_strings(tui, [parameter_index], choices)
    return f"WithName[{title}]/Parameter {parameter_index}"


OSC_INSTANCE_NAMES = ("Sub", "Osc A", "Osc B", "Osc C", "Noise")
# the rows of the Osc source list (oscNativeSourceChoices); the tabs and the selector face keep OSC_INSTANCE_NAMES
OSC_SOURCE_LIST_NAMES = ("Sub", "Oscillator A", "Oscillator B", "Oscillator C", "Noise")

OSC_ROUTING_CHOICES = (
    "Filter 1",
    "Filter 2",
    "Filters 1+2",
    "Effects only",
    "Direct out",
    "Filter blend",
    "None",
)

OSC_ROUTING_TAB_SPECS = {
    "Osc": ("osc1.destination", "osc1.on"),
    "Osc A": ("osc1.destination", "osc1.on"),
    "Osc B": ("osc2.destination", "osc2.on"),
    "Osc C": ("osc3.destination", "osc3.on"),
    "Sub": ("sub.destination", "sub.on"),
    "Noise": ("noise.destination", "noise.on"),
}

OSC_ROUTING_PARAM_KEYS = (
    "sub.destination",
    "osc1.destination",
    "osc2.destination",
    "osc3.destination",
    "noise.destination",
)

OSC_PAN_PARAM_KEYS = (
    "sub.pan",
    "osc1.pan",
    "osc2.pan",
    "osc3.pan",
    "noise.pan",
)

OSC_LEVEL_PARAM_KEYS = (
    "sub.level",
    "osc1.level",
    "osc2.level",
    "osc3.level",
    "noise.level",
)

STANDALONE_OSC_ROUTING_RECT = tuple(_style_size('selector.standalone_osc_routing_rect'))

COMPACT_OSC_ROUTING_HEIGHT = _style_size('height.medium')

STANDALONE_COMPACT_OSC_ROUTING_RECT = tuple(_style_size('selector.standalone_compact_osc_routing_rect'))

STANDALONE_COMPACT_OSC_PAN_RECT = tuple(_style_size('selector.standalone_compact_osc_pan_rect'))

STANDALONE_COMPACT_OSC_LEVEL_RECT = tuple(_style_size('selector.standalone_compact_osc_level_rect'))

OSC_ROUTING_COMPONENT_TYPE = "oscNativeRouting"

OSC_COMPACT_ROUTING_COMPONENT_TYPE = "oscNativeCompactRouting"

OSC_COMPACT_PAN_COMPONENT_TYPE = "oscNativeCompactPan"

OSC_COMPACT_LEVEL_COMPONENT_TYPE = "oscNativeCompactLevel"

OSC_ROUTING_OVERLAY_NAME = "oscNativeRoutingChoices"

OSC_SOURCE_OVERLAY_NAME = "oscNativeSourceChoices"

OSC_ROUTING_CONTROL_NAME = "Osc Native Routing"

OSC_PAN_CONTROL_NAME = "Osc Native Pan"

OSC_LEVEL_CONTROL_NAME = "Osc Native Level"

OSC_PAN_LAYER = "oscNativePanLayer"

OSC_LEVEL_LAYER = "oscNativeLevelLayer"



OSC_ROUTING_OVERLAY_BG_IMAGE = "osc_routing_overlay_bg.png"

BG_RGB = _style_rgb("menu.background")

FRAME_FILL_RGB = _style_rgb("route.surface")

FRAME_EDGE_RGB = _style_rgb("route.frame.edge")

def _encode_rgb_png(width, height, rgb_bytes):
    """Encodes an opaque RGB buffer (width * height * 3 bytes) as a valid PNG."""
    def chunk(kind, body):
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        )

    stride = width * 3
    raw = b"".join(b"\x00" + bytes(rgb_bytes[y * stride:(y + 1) * stride]) for y in range(height))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )

def png_solid(width, height, rgb):
    """Creates a solid-color opaque RGB PNG."""
    return _encode_rgb_png(width, height, bytes(rgb) * (width * height))

def write_routing_assets(skins_dir):
    """Writes all PNG artwork needed by oscNativeRouting and oscNativeRoutingChoices."""
    skins = Path(skins_dir)
    skins.mkdir(parents=True, exist_ok=True)
    write_transparent_hit(skins)
    (skins / OSC_ROUTING_OVERLAY_BG_IMAGE).write_bytes(png_solid(_style_size('selector.write_routing_assets.png_solid'), _style_size('selector.write_routing_assets.png_solid'), BG_RGB))


def build_compact_osc_routing_definition(source_def, skins_dir):
    width, height = STANDALONE_COMPACT_OSC_ROUTING_RECT[2:]
    return selector_definition(source_def, OSC_ROUTING_OVERLAY_NAME, (width, height), skins_dir,
                               filename=f"selector_face_{width}x{height}.png")


def _mark_native_menu(items):
    for i, x in enumerate(MODAL_MARKER_COLS, 1):
        items.append(_image(f"Osc native popup marker {i}", f"fx_editor_modal_marker_{i}.png", (x, _style_size('selector.marker.y'), _style_size('selector.marker.size'), _style_size('selector.marker.size'))))

def build_standalone_osc_routing_choice_overlay(
    overlay_name=OSC_ROUTING_OVERLAY_NAME,
):
    """Builds the host-native routing menu with cancellation and modal markers."""
    overlay = _choice_overlay(overlay_name)
    items = overlay["componentsData"]
    _mark_native_menu(items)
    return overlay

def labelled_choice_drawer(name, choices, *, columns=1, power_indices=None):
    """Bind each visible native row to an explicit choice of the drawer's Data handle."""
    overlay = _choice_overlay(name)
    items = overlay["componentsData"]
    x, top, width, pitch = _style_size("selector.source.drawer_row")
    label_x, _, name_width, _ = _style_size("selector.source.drawer_name")
    if power_indices is not None:
        power_indices = tuple(power_indices)
        if len(power_indices) != len(choices):
            raise ValueError("Each powered choice needs a power parameter")
    rows = (len(choices) + columns - 1) // columns
    pitch = min(pitch, (800 - top) // rows)
    for index, choice in enumerate(choices):
        column, row = divmod(index, rows)
        left, y = x + column * width, top + row * pitch
        label_width = name_width if power_indices is not None else width - 2 * label_x
        label = _value_label(choice, (left + label_x, y, label_width, pitch))
        data = label["componentData"]["data"]
        data.update(type="Text", text=choice)
        data["textStyle"]["justification"] = "left verticallyCentred"
        items.append(label)
        if power_indices is not None:
            status_width = _style_size("selector.build_standalone_osc_source_overlay.label.offset2")
            status_rect = (left + width - label_x - status_width, y, status_width, pitch)
            for on, status in enumerate(("OFF", "ON")):
                badge = _value_label(f"Osc source power {index} {status}", status_rect)
                badge_data = badge["componentData"]["data"]
                badge_data.update(type="Text", text=status)
                badge_data["textStyle"]["colour"] = _style_argb("text.status." + status.lower())
                badge["bounds"].update(version=2, showWhenDataModelInvalid="Hide",
                                       additionalInvalidatingHandles=[f"IndexedEnabling/{on}/2/Parameter {power_indices[index]}"])
                items.append(badge)
        items.append(button("Choose " + choice, (left, y, width, pitch),
                            TRANSPARENT_HIT, index=index, count=len(choices)))
    _mark_native_menu(items)
    return overlay


def build_standalone_osc_source_overlay():
    """The Osc source list: names only (no ON/OFF badges); the check mark is drawn by the host."""
    return labelled_choice_drawer(OSC_SOURCE_OVERLAY_NAME, OSC_SOURCE_LIST_NAMES)


def build_standalone_osc_source_definitions():
    """The local definitions of the Osc source list: the drawer."""
    return [{"key": OSC_SOURCE_OVERLAY_NAME, "value": build_standalone_osc_source_overlay()}]


def build_standalone_osc_routing_component(
    dest_param_idx,
    power_param_idx,
    source_component=None,
    component_type=OSC_ROUTING_COMPONENT_TYPE,
    name=OSC_ROUTING_CONTROL_NAME,
    rect=STANDALONE_OSC_ROUTING_RECT,
    overlay_param=-1,
    last_visible_frame=18,
    visibility_param_idx=-1,
):
    """Builds an Osc Native Routing instance bound to a per-source destination parameter."""
    if source_component is not None:
        entry = copy.deepcopy(source_component)
    else:
        entry = {
            "version": 2,
            "componentData": {
                "version": 1,
                "name": name,
                "type": component_type,
                "data": {"version": 1, "handleName": "Data"},
            },
            "handle remapping": {"version": 1, "map": []},
            "bounds": {},
        }

    comp_data = entry.setdefault("componentData", {})
    comp_data["version"] = 1
    comp_data["name"] = name
    comp_data["type"] = component_type
    comp_data["data"] = {"version": 1, "handleName": "Data"}

    remap = entry.setdefault("handle remapping", {})
    remap["version"] = 1
    remap["map"] = [{"key": "Data", "value": "Parameter %d" % dest_param_idx}]

    handles = []
    if power_param_idx is not None and power_param_idx >= 0:
        handles.append("IndexedEnabling/1/2/Parameter %d" % power_param_idx)
    if visibility_param_idx >= 0:
        handles.append("IndexedEnabling/2/3/Parameter %d" % visibility_param_idx)
    if overlay_param is not None and overlay_param >= 0:
        handles.append("RangedEnabling/0/%d/33/Parameter %d" % (last_visible_frame, overlay_param))

    bounds = entry.setdefault("bounds", {})
    bounds["version"] = 2
    bounds["acceptsHWFocus"] = "Yes"
    bounds["showWhenDataModelInvalid"] = "Hide"
    bounds["whenVisible"] = "Always"
    bounds["boundsType"] = "Absolute"
    bounds["bounds"] = "%d %d %d %d" % tuple(rect)
    if handles:
        bounds["version"] = 2
    bounds["additionalInvalidatingHandles"] = handles
    return entry

def build_compact_osc_routing_component(
    dest_param_idx,
    power_param_idx=None,
    source_component=None,
    component_type=OSC_COMPACT_ROUTING_COMPONENT_TYPE,
    name=OSC_ROUTING_CONTROL_NAME,
    rect=STANDALONE_COMPACT_OSC_ROUTING_RECT,
    overlay_param=-1,
    last_visible_frame=18,
    visibility_param_idx=-1,
):
    """Builds a 48 px compact Osc Native Routing instance bound to a per-source destination parameter."""
    return build_standalone_osc_routing_component(
        dest_param_idx=dest_param_idx,
        power_param_idx=power_param_idx,
        source_component=source_component,
        component_type=component_type,
        name=name,
        rect=rect,
        overlay_param=overlay_param,
        last_visible_frame=last_visible_frame,
        visibility_param_idx=visibility_param_idx,
    )


COMPACT_HEIGHT = _style_size('height.medium')

STANDALONE_RECT = STANDALONE_COMPACT_OSC_ROUTING_RECT

BUTTON_COMPACT_RECT = tuple(_style_size('selector.button_compact_rect'))

OSC_ROUTING_SHORT_NAMES = (
    "F1",
    "F2",
    "F1+F2",
    "M",
    "DIR",
    "BAL",
    "OFF",
)

BG_DARK = _style_color("surface.canvas")

CYAN_F1 = _style_color("accent.cyan")

GREEN_F2 = _style_color("accent.green")

SEG_INACTIVE = _style_color("route.segment.off")

TEXT_VALUE = _style_color("text.value")

TEXT_LABEL = _style_color("text.label")

def render_compact_button_face(
    width=_style_size('button.slot.width'),
    height=COMPACT_HEIGHT,
    target="F1",
    seg0=True,
    seg1=False,
    *,
    scale=_style_size('selector.compact_button_face.geometry'),
):
    """Render pixel-accurate 64x48 px compact RouteButton matching shell-mpc/ui/base.h."""
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    pad = _style_size('selector.compact_button_face.pad') * scale
    draw.rounded_rectangle(
        (pad, pad, w - pad - _style_size('selector.compact_button_face.rounded_rectangle'), h - pad - _style_size('selector.compact_button_face.rounded_rectangle')),
        radius=_style_size('radius.control') * scale,
        fill=_style_color("route.button.surface"),
        outline=_style_color("route.button.border"),
        width=scale,
    )

    strip_x = _style_size('selector.compact_button_face.strip.x') * scale
    strip_y = _style_size('selector.compact_button_face.strip.y') * scale
    strip_w = w - _style_size('selector.compact_button_face.strip.w') * scale
    strip_h = _style_size('selector.compact_button_face.strip.h') * scale
    half_w = (strip_w - _style_size('selector.compact_button_face.half.w') * scale) // 2

    col0 = CYAN_F1 if seg0 else SEG_INACTIVE
    col1 = GREEN_F2 if seg1 else SEG_INACTIVE

    draw.rounded_rectangle(
        (strip_x, strip_y, strip_x + half_w, strip_y + strip_h),
        radius=_style_size('radius.indicator') * scale,
        fill=col0,
    )
    draw.rounded_rectangle(
        (strip_x + half_w + _style_size('selector.compact_button_face.rounded_rectangle.offset1') * scale, strip_y, strip_x + strip_w, strip_y + strip_h),
        radius=_style_size('radius.indicator') * scale,
        fill=col1,
    )

    body_y = strip_y + strip_h + _style_size('selector.compact_button_face.body.y') * scale
    body_h = h - body_y - _style_size('selector.compact_button_face.body.h') * scale
    draw.rounded_rectangle(
        (strip_x, body_y, strip_x + strip_w, body_y + body_h),
        radius=_style_size('radius.field.small') * scale,
        fill=_style_color("button.surface"),
        outline=_style_color("button.border"),
        width=scale,
    )

    font = semibold(_style_size('font.option') * scale)
    draw.text((w // 2, body_y + body_h // 2), target, fill=TEXT_VALUE, font=font, anchor="mm")

    return im.resize((width, height), Image.Resampling.LANCZOS)

def render_choice_overlay_art(
    active_index=0,
    width=_style_size('selector.choice.width'),
    *,
    scale=_style_size('selector.choice_overlay_art.geometry'),
):
    """Render the factory 7-destination choice overlay menu (oscNativeRoutingChoices)."""
    row_h = _style_size('selector.choice_overlay_art.row.h') * scale
    pitch = _style_size('selector.choice_overlay_art.pitch') * scale
    n = len(OSC_ROUTING_CHOICES)
    total_h = n * pitch + _style_size('selector.choice_overlay_art.total.h') * scale
    w = width * scale

    im = Image.new("RGBA", (w, total_h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    draw.rounded_rectangle(
        (0, 0, w - _style_size('selector.choice_overlay_art.rounded_rectangle'), total_h - _style_size('selector.choice_overlay_art.rounded_rectangle')),
        radius=_style_size('radius.menu') * scale,
        fill=_style_color("menu.surface"),
        outline=_style_color("menu.border"),
        width=scale,
    )

    font = semibold(_style_size('font.caption') * scale)
    font_sub = semibold(_style_size('font.small') * scale)

    for i, choice in enumerate(OSC_ROUTING_CHOICES):
        y = _style_size('selector.choice_overlay_art.y') * scale + i * pitch
        active = (i == active_index)

        row_fill = _style_color("menu.selection") if active else _style_color("menu.row")
        row_border = CYAN_F1 if active else _style_color("menu.row.border")
        draw.rounded_rectangle(
            (_style_size('selector.choice_overlay_art.rounded_rectangle.offset1') * scale, y, w - _style_size('selector.choice_overlay_art.rounded_rectangle.offset2') * scale, y + row_h),
            radius=_style_size('radius.control') * scale,
            fill=row_fill,
            outline=row_border,
            width=scale if active else _style_size('selector.choice_overlay_art.rounded_rectangle'),
        )

        dot_cx = _style_size('selector.choice_overlay_art.dot.cx') * scale
        dot_cy = y + row_h // 2
        r = _style_size('selector.choice_overlay_art.r') * scale
        if active:
            draw.ellipse((dot_cx - r - _style_size('selector.choice_overlay_art.ellipse') * scale, dot_cy - r - _style_size('selector.choice_overlay_art.ellipse') * scale, dot_cx + r + _style_size('selector.choice_overlay_art.ellipse') * scale, dot_cy + r + _style_size('selector.choice_overlay_art.ellipse') * scale),
                         outline=_style_rgba("accent.cyan", _style_opacity("halo")), width=scale)
            draw.ellipse((dot_cx - r, dot_cy - r, dot_cx + r, dot_cy + r), fill=CYAN_F1)
        else:
            draw.ellipse((dot_cx - r, dot_cy - r, dot_cx + r, dot_cy + r), outline=_style_color("menu.dot.off"), width=scale)

        text_col = TEXT_VALUE if active else _style_color("menu.text")
        draw.text((_style_size('selector.choice_overlay_art.text') * scale, y + row_h // 2), choice, fill=text_col, font=font, anchor="lm")

        tag = OSC_ROUTING_SHORT_NAMES[i]
        draw.text((w - _style_size('selector.choice_overlay_art.text.offset1') * scale, y + row_h // 2), f"[{tag}]", fill=TEXT_LABEL, font=font_sub, anchor="rm")

    return im.resize((width, total_h // scale), Image.Resampling.LANCZOS)


def ensure_routing_replacement_strings(tui, dest_param_indices, choices=OSC_ROUTING_CHOICES):
    """Populates replacementStrings entries for each destination parameter index."""
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    choice_list = list(choices)
    for idx in dest_param_indices:
        if idx is None or idx < 0:
            continue
        pkey = "Parameter %d" % idx
        entry = next((r for r in replacement if r.get("key") == pkey), None)
        if entry is None:
            replacement.append({"key": pkey, "value": list(choice_list)})
        else:
            entry["value"] = list(choice_list)


OSC_POWER_PARAM_KEYS = ("sub.on", "osc1.on", "osc2.on", "osc3.on", "noise.on")
OSC_TAB_NAMES = OSC_INSTANCE_NAMES


def passive_layer(children):
    """Share the active source's widgets across the standalone oscillator tabs."""
    background = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": background, "unfocussed": background},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": children,
    }


def format_osc_choice_label(name, is_on):
    """Formats dynamic oscillator choice label indicating active/inactive state."""
    return ("%s (Active)" if is_on else "%s (Inactive)") % name


def sanitize_osc_tab_navigation(tab, osc_power_params):
    """Ensures standalone oscillator tab navigation is not locked out when power is OFF.

    Strips any IndexedEnabling handles pointing to oscillator power parameters.
    """
    for param_idx in osc_power_params.values():
        if param_idx is None or param_idx < 0:
            continue
        target_suffix = "Parameter %d" % param_idx
        handles = tab.get("additionalInvalidatingHandles", [])
        if handles:
            tab["additionalInvalidatingHandles"] = [
                h for h in handles if not h.endswith(target_suffix)
            ]
        comp_def = tab.get("componentDefinition", {})
        cd_handles = comp_def.get("additionalInvalidatingHandles", [])
        if cd_handles:
            comp_def["additionalInvalidatingHandles"] = [
                h for h in cd_handles if not h.endswith(target_suffix)
            ]


def macro_bank_definition(template):
    """Produces a native 2-choice selector definition for the 380x56 bank selector."""
    definition = copy.deepcopy(template)
    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state]["colour"] = _style_argb("surface.control")
    rects = {
        "focus": (0, 0, _style_size('selector.macro_bank_definition.rects'), _style_size('selector.macro_bank_definition.rects.offset1')),
        "slider": (0, 0, _style_size('selector.macro_bank_definition.rects'), _style_size('selector.macro_bank_definition.rects.offset2')),
    }
    for item in definition.get("componentsData", []):
        if item["componentData"]["type"] == "Focus":
            item["componentData"]["data"] = focus_ring_data()
        name = item["componentData"]["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % tuple(rects[name])
    return definition


def append_choice_rows(ctx, definition, labels, field, *, header_title=None, marks=()):
    """Native explicit rows for hosts whose drawer loses proxy step metadata.

    The existing host drawer header remains the commit/dismiss target.
    """
    from components.native.button.button import button

    geometry = _style_size("selector.choice.rows")
    definition["ignoreMousePresses"] = False
    font = semibold(geometry["font"])
    prefix = field if header_title else f"play_sub_{field}"
    hint = f"{prefix}_apply.png"
    header_rect = geometry["header"]
    header = Image.new("RGBA", tuple(header_rect[2:]))
    draw = ImageDraw.Draw(header)
    draw.line(tuple(geometry["back"]), fill=_style_color("text.white"), width=geometry["stroke"])
    title = header_title or ("PLAY SUB WAVEFORM" if field == "wave" else "PLAY SUB OCTAVE")
    draw.text(tuple(geometry["title"]), f"Tap {title} to apply", anchor="lm", font=font,
              fill=_style_color("text.white"))
    header.save(ctx.skins_dir / hint)
    definition["componentsData"].append(_entry("Apply hint", "Image", {
        "version": 2, "imageType": "Regular", "colour": "0", "image": hint
    }, header_rect, focus="No"))
    x, y, width, height = geometry["row"]
    for i, label in enumerate(labels):
        rect = (x, y + i * geometry["stride"], width, height)
        images = []
        for state in ("on", "off"):
            filename = f"{prefix}_{i}_{state}.png"
            # A drawer row is the Button recipe (shaded face; the highlighted row is look "active"), centred in its row slot.
            from components.native.button.button import action_art
            face = (action_art(text=label, width=width, height=48, look="active", colour=_style_color("radio.active.text"))
                    if state == "on" else action_art(text=label, width=width, height=48, look="shaded"))
            image = Image.new("RGBA", (width, height))
            image.alpha_composite(face, (0, (height - 48) // 2))
            image.save(ctx.skins_dir / filename)
            images.append(filename)
        definition["componentsData"].append(button(label, rect, *images, index=i, count=len(labels)))
        if marks:
            # Independent marks track the actual edited source and signal routing.
            # Reading the popup's single staged Data choice cannot represent both.
            parameter, frames = marks[i]
            tick = render_pipa_icon()
            strip = Image.new("RGBA", (tick.width, tick.height * len(frames)))
            for frame, marked in enumerate(frames):
                if marked:
                    strip.paste(tick, (0, frame * tick.height))
            filename = f"{prefix}_mark_{i}.png"
            save_png_filter0(strip, ctx.skins_dir / filename)
            tx = x + width - 2 * tick.width
            ty = rect[1] + (height - tick.height) // 2
            definition["componentsData"].append(_entry(label + " current", "Image", {
                "version": 2, "imageType": "FilmStrip", "image": filename, "numFrames": len(frames),
                "colour": _style_native_color("transparent"), "handleName": "Data"
            }, (tx, ty, tick.width, tick.height), remaps=(("Data", f"Parameter {parameter}"),), focus="No"))



def write_source_navigation_assets(skins_dir):
    """Shared source selector chevrons and transparent center hit artwork."""
    artwork = {"empty": write_transparent_hit(skins_dir)}
    arrow_w, _, art_h = _style_size("play.osc.source.art_size")
    low, high, top, middle, bottom, stroke = _style_size("play.osc.source.arrow_geometry")
    for key in ("left", "right"):
        filename = f"play_osc_source_{key}.png"
        image = Image.new("RGBA", (arrow_w, art_h))
        front, back = (low, high) if key == "left" else (high, low)
        ImageDraw.Draw(image).line((back, top, front, middle, back, bottom),
                                   fill=_style_color("text.icon"), width=stroke)
        image.save(Path(skins_dir) / filename)
        artwork[key] = filename
    return artwork


def write_wave_selector_icon(skins_dir):
    """Shared waveform selector glyph, independent of the live waveform graph."""
    filename = "play_selector_wave.png"
    image = Image.new("RGBA", tuple(_style_size("play.wave.source_size")))
    mid, amplitude, left, right, period, stroke = _style_size("play.wave.geometry")
    points = [(x, mid - amplitude * math.sin((x - left) * math.tau / period)) for x in range(left, right)]
    ImageDraw.Draw(image).line(points, fill=_style_color("play.wave.icon"), width=stroke)
    image.resize(tuple(_style_size("play.wave.icon_size")), Image.Resampling.LANCZOS).save(Path(skins_dir) / filename)
    return filename


def command_stepper_definition(skins_dir, slot, caption):
    """Show the bound value and dispatch previous/open/next as command IDs 1/2/3."""
    width, height = slot
    face = selector_definition(None, "", slot, skins_dir, arrows=True, caption_text=caption, justification="horizontallyCentred verticallyCentred")   # a dropdown face; the Previous / Open / Next hit zones below stay
    face["actions"] = []
    hit = write_transparent_hit(skins_dir)
    edge = _style_size("selector.hit.minimum")
    for command, (name, x, span) in enumerate((
        ("Previous", 0, edge),
        ("Open", edge, width - 2 * edge),
        ("Next", width - edge, edge),
    ), 1):
        face["componentsData"].append(button(name, (x, 0, span, height), hit, index=command, count=4))
    return face


def _ellipsize(draw, text, font, room):
    """`text` cut at the end and closed with an ellipsis until it fits `room` px."""
    while text and draw.textbbox((0, 0), text + "\u2026", font=font)[2] > room:
        text = text[:-1].rstrip()
    return text + "\u2026"


def render_selector_face(width, height, *, name=None, value="Sine", ellipsis=False, **variants):
    """Compose only the images and labels emitted by the actual native definition.

    ellipsis: a value that does not fit its label box is left-aligned and cut with an ellipsis at the end."""
    with tempfile.TemporaryDirectory() as tmp:
        skins = Path(tmp)
        definition = selector_definition(
            None, "catalog list", (width, height), skins,
            caption_text=name, **variants,
        )
        result = Image.new("RGBA", (width, height))
        for item in definition["componentsData"]:
            component = item["componentData"]
            data = component["data"]
            x, y, w, h = map(int, item["bounds"]["bounds"].split())
            if component["type"] == "Image":
                with Image.open(skins / data["image"]) as art:
                    result.alpha_composite(art.convert("RGBA").resize((w, h)), (x, y))
            elif component["type"] == "Label":
                style = data["textStyle"]
                text = data.get("text", value)
                font_height = style["font"]["height"]
                if font_height == _style_size("selector.dropdown")["font"] and not (
                        name or {k: v for k, v in variants.items() if k not in ("dropdown_look", "dropdown_size")}):
                    font_height = _style_size("selector.dropdown.preview_font")
                font = semibold(round(_native_font_em(font_height)))
                draw = ImageDraw.Draw(result)
                bounds = draw.textbbox((0, 0), text, font=font)
                tw, th = bounds[2] - bounds[0], bounds[3] - bounds[1]
                justification = style["justification"]
                if ellipsis and tw > w:
                    text = _ellipsize(draw, text, font, w)
                    bounds = draw.textbbox((0, 0), text, font=font)
                    tw, th = bounds[2] - bounds[0], bounds[3] - bounds[1]
                    justification = "left verticallyCentred"
                assert tw <= w and th <= h, (text, (tw, th), (w, h))
                tx = x + (w - tw) / 2 if "horizontallyCentred" in justification else x
                ty = y + (h - th) / 2 if "verticallyCentred" in justification else y
                draw.text((tx - bounds[0], ty - bounds[1]), text, font=font,
                          fill="#" + style["colour"][-6:])
        return result


DROPDOWN_PLACEMENTS = ("none", "above", "inside")
DROPDOWN_ALIGNS = ("center", "left")
# The label above a dropdown has the dial-caption style of the Label component (font, text.caption, upper case).
DROPDOWN_LABEL_FONT = "pan.sub_dial_definition.style.font.height"
DROPDOWN_KEYS = {"id", "file", "alt", "caption", "width", "value", "placement", "align"}


def render_dropdown_variant(width, height, value, *, label=None, placement="none", align="center"):
    """One dropdown from the one selector recipe (render_selector_face); `height` is the box height.

    The value size and the chevron follow the box: selector.dropdown.size.<height> (32 / 40 / 48 px; the value em is
    about half the box height).

    placement: "none"   value only, no label (the old Play-panel look when align is "center");
               "above"  `label` outside and above the box, left-aligned with the box, in the Label dial-caption style;
               "inside" `label` as the small caption in the box (the catalogue's MODEL look, value left).
    align:     "center" or "left" value; with "inside" the recipe's own left value is kept.
    The "above" picture is taller than the box by the label line and one gap.field. A value that does not fit is
    left-aligned and cut with an ellipsis; it ends selector.dropdown.chevron_gap before the chevron lane.
    """
    if placement not in DROPDOWN_PLACEMENTS or align not in DROPDOWN_ALIGNS:
        raise ValueError(f"placement must be one of {DROPDOWN_PLACEMENTS} and align one of {DROPDOWN_ALIGNS}")
    if (placement == "none") == bool(label):
        raise ValueError("a label is given exactly when the placement is above or inside")
    if placement == "inside":
        return render_selector_face(width, height, name=label, value=value, label_height=_style_size("font.button"))
    justification = {"left": "left verticallyCentred", "center": "horizontallyCentred verticallyCentred"}[align]
    size = f"selector.dropdown.size.{height}"
    try:
        font_em = _style_size(size)["font_em"]
    except ValueError:
        size, font_em = None, None
    # The recipe takes a TUI font height; the host draws em = height / font.host.height.per.em.
    tui = font_em * _style_size("font.host.height.per.em") if font_em else _style_size("selector.dropdown.preview_font")
    box = render_selector_face(width, height, value=value, justification=justification, dropdown_look="shaded",
                               dropdown_size=size, label_height=tui, ellipsis=True)
    if placement == "none":
        return box
    from components.native.label.label import render_label
    text = render_label(label, DROPDOWN_LABEL_FONT, "text.caption", case="Upper Case")
    gap = _style_size("gap.field")
    picture = Image.new("RGBA", (width, text.height + gap + height))
    picture.alpha_composite(text, (0, 0))   # the title is left-aligned with the box's left edge
    picture.alpha_composite(box, (0, text.height + gap))
    return picture


def dropdown_variants(entry):
    """The dropdown variants of a selector catalogue "dropdown" entry, validated (label is optional)."""
    if not isinstance(entry, dict) or set(entry) != {"variants"} or not entry["variants"]:
        raise ValueError("dropdown entry needs a nonempty variants list")
    for item in entry["variants"]:
        if not DROPDOWN_KEYS <= set(item) <= DROPDOWN_KEYS | {"label", "height"}:
            raise ValueError(f"dropdown variant needs {sorted(DROPDOWN_KEYS)} (+ optional label, height)")
        if item["placement"] not in DROPDOWN_PLACEMENTS or item["align"] not in DROPDOWN_ALIGNS:
            raise ValueError(f"dropdown variant {item['id']!r}: placement/align out of range")
    if len({v["id"] for v in entry["variants"]}) != len(entry["variants"]):
        raise ValueError("dropdown variant ids must be unique")
    return entry["variants"]


def catalog_sections(entry):
    return [{"heading": "Legördülő: felirat és érték elrendezése", "images": [
        {"presentation": "sample", "file": v["file"], "alt": v["alt"], "caption": v["caption"]}
        for v in dropdown_variants(entry)]}]
