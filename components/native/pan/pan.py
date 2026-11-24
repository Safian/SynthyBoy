# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC PAN dial component and generator."""
import copy
import math
from pathlib import Path

from components.style import (
    argb as _style_argb,
    color as _style_color,
    rect as _style_rect,
    size as _style_size,
)

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

SIZE = _style_size('dial.diameter')
FRAMES = 127

COMPACT_48_RECTS = {
    "focus": tuple(_style_size('pan.compact_48_rects.focus')),
    "knob": tuple(_style_size('pan.compact_48_rects.knob')),
    "name": tuple(_style_size('pan.compact_48_rects.name')),
    "value": tuple(_style_size('pan.compact_48_rects.value')),
}

DEFAULT_DIAL_RECTS = {
    "focus": tuple(_style_size('pan.default_dial_rects.focus')),
    "knob": tuple(_style_size('pan.default_dial_rects.knob')),
    "name": tuple(_style_size('pan.default_dial_rects.name')),
}


def _value_label(name, rect, height=_style_size('pan._value_label.geometry')):
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
                    "font": {"version": 1, "name": _style_size('font.family'), "style": _style_size('font.style.semibold'), "height": float(height)},
                    "colour": _style_argb("text.icon"),
                    "justification": "horizontallyCentred verticallyCentred",
                    "case": "Original",
                },
                "type": "Value",
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
            "bounds": "%d %d %d %d" % rect,
        },
    }


def default_pan_template():
    return {
        "version": 2,
        "actions": [{"version": 1, "handler": "Show Overlay"}],
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": _style_argb("surface.control"), "image": ""},
            "unfocussed": {"version": 1, "colour": _style_argb("surface.control"), "image": ""},
        },
        "componentsData": [
            {
                "version": 2,
                "componentData": {
                    "version": 1,
                    "name": "knob",
                    "type": "Knob",
                    "data": {
                        "version": 5,
                        "knobType": "GenericLegacy",
                        "polarity": "BiPolar",
                        "dragOrientation": "Vertical",
                        "handleName": "Data",
                    },
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {
                    "version": 1,
                    "acceptsHWFocus": "Yes",
                    "showWhenDataModelInvalid": "Show",
                    "whenVisible": "Always",
                    "boundsType": "Absolute",
                    "bounds": _style_rect("pan.default_pan_template.geometry"),
                },
            },
            {
                "version": 2,
                "componentData": {
                    "version": 1,
                    "name": "name",
                    "type": "Label",
                    "data": {
                        "version": 1,
                        "type": "Name",
                        "handleName": "Caption",
                        "textStyle": {
                            "version": 1,
                            "font": {
                                "version": 1,
                                "name": _style_size('font.family'),
                                "style": _style_size('font.style.semibold'),
                                "height": _style_size('font.caption'),
                            },
                            "colour": _style_argb("text.caption"),
                            "justification": "left verticallyCentred",
                            "case": "Original",
                        },
                    },
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {
                    "version": 1,
                    "acceptsHWFocus": "No",
                    "showWhenDataModelInvalid": "Show",
                    "whenVisible": "Always",
                    "boundsType": "Absolute",
                    "bounds": _style_rect("pan.default_pan_template.geometry.offset1"),
                },
            },
        ],
    }


def write_strip(path, frame_size=SIZE, bipolar=True):
    if Image is None:
        raise RuntimeError("Pillow is required to generate dial filmstrips")
    scale = _style_size('pan.write_strip.scale')
    center = (SIZE - _style_size('pan.write_strip.center')) * scale / 2
    radius = _style_size('pan.write_strip.radius') * scale

    def point(angle, r):
        return (
            center + r * math.cos(math.radians(angle)),
            center + r * math.sin(math.radians(angle)),
        )

    strip = Image.new("RGBA", (frame_size, frame_size * FRAMES))
    bounds = (center - radius, center - radius, center + radius, center + radius)
    for i in range(FRAMES):
        frame = Image.new("RGBA", (SIZE * scale, SIZE * scale))
        draw = ImageDraw.Draw(frame)
        angle = _style_size('pan.write_strip.angle') + _style_size('pan.write_strip.angle.offset1') * i / (FRAMES - _style_size('pan.write_strip.angle.offset2'))
        draw.arc(bounds, _style_size('pan.write_strip.arc'), _style_size('pan.write_strip.arc.offset1'), fill=_style_color("dial.native.track"), width=_style_size('stroke.arc') * scale)
        if bipolar:
            draw.line(
                (point(_style_size('pan.write_strip.point'), radius - _style_size('pan.write_strip.point.offset1') * scale), point(_style_size('pan.write_strip.point'), radius + _style_size('pan.write_strip.point.offset2') * scale)),
                fill=_style_color("text.caption"),
                width=round(_style_size('pan.write_strip.round.offset1') * scale),
            )
        start = _style_size('pan.write_strip.start') if bipolar else _style_size('pan.write_strip.start.offset1')
        if angle != start:
            draw.arc(bounds, min(angle, start), max(angle, start), fill=_style_color("dial.native.value"), width=_style_size('stroke.arc.active') * scale)
        draw.line(
            (point(angle, radius * 0.25), point(angle, radius * 0.6)),
            fill=_style_color("dial.native.pointer"),
            width=round(_style_size('dial.native.pointer.stroke') * scale),
        )
        x, y = point(angle, radius - _style_size('pan.write_strip.x.y') * scale)
        dot = _style_size('pan.write_strip.dot') * scale
        draw.ellipse((x - dot, y - dot, x + dot, y + dot), fill=_style_color("dial.native.pointer"))
        strip.paste(
            frame.resize((frame_size, frame_size), Image.Resampling.LANCZOS),
            (0, i * frame_size),
        )
    strip.save(path)


def synthyboy_dial_data(skins_dir, *, frame_size=SIZE, bipolar=True):
    filename = f"play_sub_pan_{frame_size}.png"
    if skins_dir is not None:
        output = Path(skins_dir) / filename
        bundled = Path(__file__).with_name("img") / filename
        if bundled.is_file() and bipolar == True:
            if bundled.resolve() != output.resolve():
                output.write_bytes(bundled.read_bytes())
        else:
            write_strip(output, frame_size=frame_size, bipolar=bipolar)
    return {
        "version": 3,
        "knobType": "FilmStrip",
        "filmStrip": filename,
        "numFrames": FRAMES,
        "invert": False,
        "dragOrientation": "Vertical",
        "handleName": "Data",
    }


def sub_dial_definition(template, skins_dir, *, bipolar=True):
    definition = copy.deepcopy(template)
    controls = {item["componentData"]["name"]: item for item in definition["componentsData"]}
    controls["knob"]["componentData"]["data"] = synthyboy_dial_data(
        skins_dir, frame_size=SIZE, bipolar=bipolar
    )
    caption = controls["name"]
    caption["bounds"]["bounds"] = _style_rect("pan.sub_dial_definition.caption.bounds.bounds")
    style = caption["componentData"]["data"]["textStyle"]
    style["colour"] = _style_argb("text.caption")
    style["font"]["height"] = _style_size('pan.sub_dial_definition.style.font.height')
    style["justification"] = "left verticallyCentred"
    value = _value_label("value", (_style_size('pan.sub_dial_definition.value.offset1'), _style_size('pan.sub_dial_definition.value.offset2'), _style_size('pan.sub_dial_definition.value.offset3'), _style_size('pan.sub_dial_definition.value.offset4')), _style_size('pan.sub_dial_definition.value'))
    value_style = value["componentData"]["data"]["textStyle"]
    value_style.update(colour=_style_argb("text.value"), justification="left verticallyCentred")
    definition["componentsData"] = [
        c for c in definition["componentsData"] if c["componentData"]["name"] != "value"
    ]
    definition["componentsData"].append(value)
    return definition


def pan_definition(template=None, skins_dir=None, rects=None):
    if template is None:
        template = default_pan_template()
    dial_rects = copy.deepcopy(rects if rects is not None else DEFAULT_DIAL_RECTS)
    definition = copy.deepcopy(template)

    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state]["colour"] = _style_argb("surface.control")

    for item in definition["componentsData"]:
        control = item["componentData"]
        name = control["name"]
        if name in dial_rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % dial_rects[name]
        if name == "knob":
            control["data"] = {
                "version": 5,
                "knobType": "GenericLegacy",
                "polarity": "BiPolar",
                "dragOrientation": "Vertical",
                "handleName": "Data",
            }
        elif name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('pan.pan_definition.font.height')
            control["data"]["textStyle"]["colour"] = _style_argb("text.caption")

    definition["actions"] = [
        a for a in definition.get("actions", []) if a.get("handler") != "Show Overlay"
    ]

    return sub_dial_definition(definition, skins_dir, bipolar=True)


def compact_pan_definition(template=None, skins_dir=None):
    if template is None:
        template = default_pan_template()
    definition = copy.deepcopy(template)
    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state]["colour"] = _style_argb("surface.control")

    rects = copy.deepcopy(COMPACT_48_RECTS)
    for item in definition["componentsData"]:
        control = item["componentData"]
        name = control["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % rects[name]
        if name == "knob":
            control["data"] = {
                "version": 5,
                "knobType": "GenericLegacy",
                "polarity": "BiPolar",
                "dragOrientation": "Vertical",
                "handleName": "Data",
            }
        elif name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('pan.compact_pan_definition.font.height')
            control["data"]["textStyle"]["colour"] = _style_argb("text.icon")
        elif name == "value":
            control["data"]["handleName"] = "Data"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('pan.compact_pan_definition.font.height.offset1')
            control["data"]["textStyle"]["colour"] = _style_argb("text.value")

    definition["actions"] = [
        a for a in definition.get("actions", []) if a.get("handler") != "Show Overlay"
    ]

    controls = {item["componentData"]["name"]: item for item in definition["componentsData"]}
    controls["knob"]["componentData"]["data"] = synthyboy_dial_data(
        skins_dir, frame_size=COMPACT_48_RECTS["knob"][2], bipolar=True
    )
    if "value" not in controls:
        definition["componentsData"].append(_value_label("value", COMPACT_48_RECTS["value"], _style_size('pan.compact_pan_definition._value_label')))
    return definition
