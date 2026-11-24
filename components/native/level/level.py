# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC LEVEL dial component and generator with Smooth and CompactDial variants."""
import copy
import math
from pathlib import Path

from components.style import (
    argb as _style_argb,
    native_color as _style_native_color,
    color as _style_color,
    opacity as _style_opacity,
    rgba as _style_rgba,
    rect as _style_rect,
    size as _style_size,
)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

SIZE = _style_size('dial.diameter')
FRAMES = 127

COMPACT_48_RECTS = {
    "focus": tuple(_style_size('level.compact_48_rects.focus')),
    "knob": tuple(_style_size('level.compact_48_rects.knob')),
    "name": tuple(_style_size('level.compact_48_rects.name')),
    "value": tuple(_style_size('level.compact_48_rects.value')),
}

DEFAULT_DIAL_RECTS = {
    "focus": tuple(_style_size('level.default_dial_rects.focus')),
    "knob": tuple(_style_size('level.default_dial_rects.knob')),
    "name": tuple(_style_size('level.default_dial_rects.name')),
}

DEFAULT_RECTS = {
    "focus": tuple(_style_size('level.default_rects.focus')),
    "knob": tuple(_style_size('level.default_rects.knob')),
    "name": tuple(_style_size('level.default_rects.name')),
}


_SMOOTH_RECTS = {
    "focus": tuple(_style_size('level._smooth_rects.focus')),
    "knob": tuple(_style_size('level._smooth_rects.knob')),
    "name": tuple(_style_size('level._smooth_rects.name')),
    "value": tuple(_style_size('level._smooth_rects.value')),
}
PLAY_SMOOTH_RECTS = {
    "focus": tuple(_style_size('level.play_smooth_rects.focus')),
    "knob": tuple(_style_size('level.play_smooth_rects.knob')),
    "name": tuple(_style_size('level.play_smooth_rects.name')),
    "value": tuple(_style_size('level.play_smooth_rects.value')),
}
_SMOOTH_NAME_HEIGHT = _style_size('smooth.caption.font')
_SMOOTH_VALUE_HEIGHT = _style_size('smooth.value.font')


def value_label(name, rect, height=_style_size('level._value_label.geometry')):
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
            "bounds": "%d %d %d %d" % tuple(rect),
        },
    }


_value_label = value_label


def default_level_template():
    return {
        "version": 2,
        "actions": [{"version": 1, "handler": "Show Overlay"}],
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "backgroundData": {
            "version": 1,
            "focussed": {"version": 1, "colour": _style_native_color("transparent"), "image": ""},
            "unfocussed": {"version": 1, "colour": _style_native_color("transparent"), "image": ""},
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
                        "polarity": "UniPolar",
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
                    "bounds": _style_rect("level.default_level_template.geometry"),
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
                    "bounds": _style_rect("level.default_level_template.geometry.offset1"),
                },
            },
        ],
    }


def write_strip(path, frame_size=SIZE, bipolar=False, active=True):
    if Image is None:
        raise RuntimeError("Pillow is required to generate dial filmstrips")
    scale = _style_size('level.write_strip.scale')
    center = (SIZE - _style_size('level.write_strip.center')) * scale / 2
    radius = _style_size('level.write_strip.radius') * scale

    def point(angle, r):
        return (
            center + r * math.cos(math.radians(angle)),
            center + r * math.sin(math.radians(angle)),
        )

    strip = Image.new("RGBA", (frame_size, frame_size * FRAMES))
    bounds = (center - radius, center - radius, center + radius, center + radius)
    track_col = _style_color("dial.native.track") if active else _style_color("smooth.track.off")
    arc_col = _style_color("dial.native.value") if active else _style_color("smooth.arc.off")
    val_col = _style_color("dial.native.pointer") if active else _style_color("smooth.pointer.off")

    for i in range(FRAMES):
        frame = Image.new("RGBA", (SIZE * scale, SIZE * scale))
        draw = ImageDraw.Draw(frame)
        angle = _style_size('level.write_strip.angle') + _style_size('level.write_strip.angle.offset1') * i / (FRAMES - _style_size('level.write_strip.angle.offset2'))
        draw.arc(bounds, _style_size('level.write_strip.arc'), _style_size('level.write_strip.arc.offset1'), fill=track_col, width=_style_size('stroke.arc') * scale)
        if bipolar:
            draw.line(
                (point(_style_size('level.write_strip.point'), radius - _style_size('level.write_strip.point.offset1') * scale), point(_style_size('level.write_strip.point'), radius + _style_size('level.write_strip.point.offset2') * scale)),
                fill=_style_color("text.caption") if active else _style_color("smooth.pointer.off"),
                width=round(_style_size('level.write_strip.round.offset1') * scale),
            )
        start = _style_size('level.write_strip.start') if bipolar else _style_size('level.write_strip.start.offset1')
        if angle != start:
            draw.arc(bounds, min(angle, start), max(angle, start), fill=arc_col, width=_style_size('stroke.arc.active') * scale)
        draw.line(
            (point(angle, radius * 0.25), point(angle, radius * 0.6)),
            fill=val_col,
            width=round(_style_size('dial.native.pointer.stroke' if active else 'level.write_strip.round') * scale),
        )
        x, y = point(angle, radius - _style_size('level.write_strip.x.y') * scale)
        dot = _style_size('level.write_strip.dot') * scale
        draw.ellipse((x - dot, y - dot, x + dot, y + dot), fill=val_col)
        strip.paste(
            frame.resize((frame_size, frame_size), Image.Resampling.LANCZOS),
            (0, i * frame_size),
        )
    strip.save(path)


def synthyboy_dial_data(skins_dir, *, frame_size=SIZE, bipolar=False, active=True):
    filename = f"play_sub_level_{frame_size}.png" if active else f"play_sub_level_{frame_size}_inactive.png"
    if skins_dir is not None:
        output = Path(skins_dir) / filename
        bundled = Path(__file__).resolve().parent / "img" / filename
        if bundled.is_file() and not bipolar:
            if bundled.resolve() != output.resolve():
                output.write_bytes(bundled.read_bytes())
        else:
            write_strip(output, frame_size=frame_size, bipolar=bipolar, active=active)
    return {
        "version": 3,
        "knobType": "FilmStrip",
        "filmStrip": filename,
        "numFrames": FRAMES,
        "invert": False,
        "dragOrientation": "Vertical" if active else "None",
        "handleName": "Data",
    }


def sub_dial_definition(template, skins_dir, *, bipolar=False):
    definition = copy.deepcopy(template)
    controls = {item["componentData"]["name"]: item for item in definition["componentsData"]}
    controls["knob"]["componentData"]["data"] = synthyboy_dial_data(
        skins_dir, frame_size=SIZE, bipolar=bipolar
    )
    caption = controls["name"]
    caption["bounds"]["bounds"] = _style_rect("level.sub_dial_definition.caption.bounds.bounds")
    style = caption["componentData"]["data"]["textStyle"]
    style["colour"] = _style_argb("text.caption")
    style["font"]["height"] = _style_size('level.sub_dial_definition.style.font.height')
    style["justification"] = "left verticallyCentred"
    value = _value_label("value", (_style_size('level.sub_dial_definition.value.offset1'), _style_size('level.sub_dial_definition.value.offset2'), _style_size('level.sub_dial_definition.value.offset3'), _style_size('level.sub_dial_definition.value.offset4')), _style_size('level.sub_dial_definition.value'))
    value_style = value["componentData"]["data"]["textStyle"]
    value_style.update(colour=_style_argb("text.value"), justification="left verticallyCentred")
    definition["componentsData"] = [
        c for c in definition["componentsData"] if c["componentData"]["name"] != "value"
    ]
    definition["componentsData"].append(value)
    return definition


def level_definition(template=None, skins_dir=None, rects=None):
    if template is None:
        template = default_level_template()
    dial_rects = copy.deepcopy(rects if rects is not None else DEFAULT_DIAL_RECTS)
    definition = copy.deepcopy(template)

    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state].update(colour=_style_native_color("transparent"), image="")

    for item in definition["componentsData"]:
        control = item["componentData"]
        name = control["name"]
        if name in dial_rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % tuple(dial_rects[name])
        if name == "knob":
            control["data"] = {
                "version": 5,
                "knobType": "GenericLegacy",
                "polarity": "UniPolar",
                "dragOrientation": "Vertical",
                "handleName": "Data",
            }
        elif name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.level_definition.font.height')
            control["data"]["textStyle"]["colour"] = _style_argb("text.caption")

    definition["actions"] = [
        a for a in definition.get("actions", []) if a.get("handler") != "Show Overlay"
    ]

    return sub_dial_definition(definition, skins_dir, bipolar=False)


def compact_level_definition(template=None, skins_dir=None):
    if template is None:
        template = default_level_template()
    definition = copy.deepcopy(template)
    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state].update(colour=_style_native_color("transparent"), image="")

    rects = copy.deepcopy(COMPACT_48_RECTS)
    for item in definition["componentsData"]:
        control = item["componentData"]
        name = control["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % tuple(rects[name])
        if name == "knob":
            control["data"] = {
                "version": 5,
                "knobType": "GenericLegacy",
                "polarity": "UniPolar",
                "dragOrientation": "Vertical",
                "handleName": "Data",
            }
        elif name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.compact_level_definition.font.height')
            control["data"]["textStyle"]["colour"] = _style_argb("text.icon")
        elif name == "value":
            control["data"]["handleName"] = "Data"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.compact_level_definition.font.height.offset1')
            control["data"]["textStyle"]["colour"] = _style_argb("text.value")

    definition["actions"] = [
        a for a in definition.get("actions", []) if a.get("handler") != "Show Overlay"
    ]

    controls = {item["componentData"]["name"]: item for item in definition["componentsData"]}
    controls["knob"]["componentData"]["data"] = synthyboy_dial_data(
        skins_dir, frame_size=COMPACT_48_RECTS["knob"][2], bipolar=False
    )
    if "value" not in controls:
        definition["componentsData"].append(_value_label("value", COMPACT_48_RECTS["value"], _style_size('level.compact_level_definition._value_label')))
    return definition


# -------------------------------------------------------------------------
# CompactDial variant API
# -------------------------------------------------------------------------

def compact_dial_definition(template, rects=None, polarity="UniPolar", height=None):
    definition = copy.deepcopy(template)
    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state].update(colour=_style_native_color("transparent"), image="")

    if rects is None:
        if height == _style_size('level.compact_dial_definition.geometry'):
            rects = copy.deepcopy(COMPACT_48_RECTS)
        else:
            rects = copy.deepcopy(DEFAULT_RECTS)

    has_value_target = "value" in rects
    if not has_value_target:
        definition["componentsData"] = [
            c for c in definition["componentsData"] if c["componentData"]["name"] != "value"
        ]

    is_compact = rects.get("focus", (0, 0, 0, _style_size('level.compact_dial_definition.is.compact.offset1')))[3] <= _style_size('level.compact_dial_definition.is.compact')

    for item in definition["componentsData"]:
        control = item["componentData"]
        name = control["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % tuple(rects[name])
        if name == "knob":
            control["data"] = {
                "version": 5,
                "knobType": "GenericLegacy",
                "polarity": polarity,
                "dragOrientation": "Vertical",
                "handleName": "Data",
            }
        elif name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.compact_dial_definition.font.height') if is_compact else _style_size('level.compact_dial_definition.font.height.offset1')
            control["data"]["textStyle"]["colour"] = _style_argb("text.icon")
        elif name == "value":
            control["data"]["handleName"] = "Data"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.compact_dial_definition.font.height.offset2') if is_compact else _style_size('level.compact_dial_definition.font.height.offset3')
            control["data"]["textStyle"]["colour"] = _style_argb("text.value")

    definition["actions"] = [
        a for a in definition.get("actions", []) if a.get("handler") != "Show Overlay"
    ]
    return definition


# -------------------------------------------------------------------------
# Smooth variant API
# -------------------------------------------------------------------------

def clone(template, name, parameter, rect):
    """Clone an MPC component template into an independent entry."""
    entry = copy.deepcopy(template)
    if isinstance(entry, dict):
        if "componentData" in entry and isinstance(entry["componentData"], dict):
            entry["componentData"]["name"] = name
        bounds_str = "%d %d %d %d" % tuple(rect) if isinstance(rect, (tuple, list)) else str(rect)
        if "bounds" in entry and isinstance(entry["bounds"], dict):
            entry["bounds"]["bounds"] = bounds_str
        else:
            entry["bounds"] = {
                "version": 1,
                "acceptsHWFocus": "Yes",
                "showWhenDataModelInvalid": "Show",
                "whenVisible": "Always",
                "boundsType": "Absolute",
                "bounds": bounds_str,
            }
        if parameter is not None:
            remap = entry.setdefault("handle remapping", {"version": 1, "map": []})
            remap_list = remap.setdefault("map", [])
            for m in remap_list:
                if isinstance(m, dict) and m.get("key") in ("Value", "Data"):
                    m["value"] = "Parameter %d" % parameter
                    break
            else:
                remap_list.append({"key": "Value", "value": "Parameter %d" % parameter})
    return entry


def is_smooth_applicable(instance):
    """Returns True if the LFO instance supports waveform smoothing (standard LFO 1..10).

    Random LFOs (RND 1..4 / instances 11..14) do not have a smoothing filter or parameter.
    """
    if isinstance(instance, str):
        inst_lower = instance.strip().lower()
        if inst_lower.startswith(("r", "rnd")):
            return False
        digits = "".join(c for c in inst_lower if c.isdigit())
        if digits:
            return 1 <= int(digits) <= 10
        return False
    elif isinstance(instance, (int, float)):
        return 1 <= int(instance) <= 10
    return False


def lfo_smooth_param_key(instance):
    """The engine parameter key for LFO smooth switch: 'lfoN.smooth' (N = 1..10).

    Returns None for random LFOs as smoothing is not applicable.
    """
    if not is_smooth_applicable(instance):
        return None
    num = int(instance) if isinstance(instance, (int, float)) else int("".join(c for c in instance if c.isdigit()))
    return "lfo%d.smooth" % num


def smooth_binding(toggle_template, clone_fn, proxy_indices, led_rect):
    """Smooth on/off UI component binding for compact LFO Play proxy."""
    cloner = clone_fn if clone_fn is not None else clone
    return cloner(toggle_template, "Smooth", proxy_indices[4], led_rect)


def build_standalone_smooth(toggle_template, instance_num, param_idx, rect, clone_fn=None, overlay_param=-1, last_visible_frame=18):
    """Build a standalone native LFO Smooth toggle component with correct per-instance remapping.

    Only applicable for standard LFOs 1..10. Returns None if not applicable.
    Ensures:
    1. Correct per-instance binding (lfoN.smooth -> Parameter <param_idx>).
    2. Operability:
       - No self-disabling condition (strips 'Enable' mapping and IndexedEnabling handles).
       - Toggle remains touchable and visible (showWhenDataModelInvalid='Show', whenVisible='Always').
    3. Overlay precedence:
       - In overlay skin variant, optionally adds RangedEnabling on overlay_param so the host widget
         hides while the plugin's GPU overlay is active (frame 24) and remains visible during frames 0..18.
    """
    if not is_smooth_applicable(instance_num):
        return None
    cloner = clone_fn if clone_fn is not None else clone
    instance = lfo_smooth_param_key(instance_num).split(".")[0][3:]
    name = f"LFO {instance} Smooth Native"
    entry = cloner(toggle_template, name, param_idx, rect)

    # Invalidate / remapping safety: strip self-disabling handles
    if "handle remapping" in entry:
        entry["handle remapping"]["map"] = [
            m for m in entry["handle remapping"].get("map", [])
            if m.get("from") != "Enable" and m.get("key") != "Enable"
        ]
        caption_map = next((m for m in entry["handle remapping"]["map"] if m.get("key") == "Caption"), None)
        if caption_map:
            caption_map["value"] = "WithName[Smooth]/Parameter %d" % param_idx
        else:
            entry["handle remapping"]["map"].append({"key": "Caption", "value": "WithName[Smooth]/Parameter %d" % param_idx})

    bounds = entry.setdefault("bounds", {})
    bounds["acceptsHWFocus"] = toggle_template.get("bounds", {}).get("acceptsHWFocus", "Yes") if isinstance(toggle_template, dict) else "Yes"
    bounds["showWhenDataModelInvalid"] = "Show"
    bounds["whenVisible"] = "Always"

    # Remove any IndexedEnabling handles on its own smooth parameter
    handles = list(bounds.get("additionalInvalidatingHandles", []))
    handles = [h for h in handles if not h.endswith("Parameter %d" % param_idx)]

    # Overlay hiding precedence (if overlay_param is supplied)
    if overlay_param >= 0:
        overlay_handle = "RangedEnabling/0/%d/33/Parameter %d" % (last_visible_frame, overlay_param)
        if overlay_handle not in handles:
            handles.append(overlay_handle)

    if handles:
        bounds["version"] = 2
    bounds["additionalInvalidatingHandles"] = handles
    return entry


def instantiate_standalone_smooth_for_tab(tab, instance_num, param_idx, rect, template=None, clone_fn=None):
    """Instantiate and append native Smooth toggle component to tab's componentsData, only when applicable."""
    if not is_smooth_applicable(instance_num):
        return None
    comp = build_standalone_smooth(template or {}, instance_num, param_idx, rect, clone_fn=clone_fn)
    if comp is not None and isinstance(tab, dict):
        comps = tab.setdefault("componentDefinition", {}).setdefault("componentsData", [])
        comps.append(comp)
    return comp


def _get_pil():
    try:
        from PIL import Image, ImageDraw, ImageFont
        return Image, ImageDraw, ImageFont
    except ImportError:
        raise RuntimeError("Pillow is required for rendering dial artwork")


def _get_font(size):
    _, _, ImageFont = _get_pil()
    font_path = Path(__file__).resolve().parent / "img" / _style_size('font.file.semibold')
    return ImageFont.truetype(str(font_path), size)


def _render_smooth_caption(on, scale=_style_size('level._render_smooth_caption.geometry')):
    """The exact bright/dim caption artwork used by the native state control."""
    Image, ImageDraw, _ = _get_pil()
    _, _, width, height = _SMOOTH_RECTS["name"]
    image = Image.new("RGBA", (width * scale, height * scale), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(image)
    draw.text((0, height * scale / 2), "SMOOTH", anchor="lm",
              font=_get_font(_SMOOTH_NAME_HEIGHT * scale),
              fill=_style_color("accent.cyan") if on else _style_color("smooth.caption.off"))
    return image






def build_native_smooth_dial_with_indicator_definition(
    unipolar_template=None,
    rects=None,
    on_image="smooth_caption_on.png",
    off_image="smooth_caption_off.png",
    interactive_toggle=True,
    handle_name="State",
    skins_dir=None,
    active=True,
):
    """Construct a native dial whose bright/dim SMOOTH caption is its state control.

    Layout within typical 116 x 72 cell:
    - focus: (0, 0, 116, 72)
    - knob: (2, 6, 60, 60)
    - name: (64, 16, 50, 24) - bright/dim 'SMOOTH' caption and toggle surface
    - value: (64, 42, 50, 24) - value readout

    Handles exposed:
    - Data: maps to smoothing time parameter
    - State / Active: maps to smoothing enable/switch parameter (feeds the caption Button/Indicator)
    """
    if rects is None:
        rects = _SMOOTH_RECTS

    background = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}

    focus_comp = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "focus",
            "type": "Focus",
            "data": {
                "version": 1,
                "backgroundColour": _style_argb("text.white", _style_opacity("focus")),
                "outlineColour": _style_argb("focus.cyan"),
                "backgroundInset": _style_size('level.build_native_smooth_dial_with_indicator_definition.focus.comp'),
                "outlineThickness": _style_size('level.build_native_smooth_dial_with_indicator_definition.focus.comp.offset1'),
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "Yes" if active else "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "WhenFocussed",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % tuple(rects["focus"]),
        },
    }

    if skins_dir is not None:
        knob_data = synthyboy_dial_data(skins_dir, frame_size=rects["knob"][2], bipolar=False, active=active)
    else:
        knob_data = {
            "version": 5,
            "knobType": "GenericLegacy",
            "polarity": "UniPolar",
            "dragOrientation": "Vertical" if active else "None",
            "handleName": "Data",
        }
    if not active:
        knob_data["dragOrientation"] = "None"
        knob_data["ignoreMousePresses"] = True

    knob_comp = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "knob",
            "type": "Knob",
            "data": knob_data,
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % tuple(rects["knob"]),
        },
    }

    if interactive_toggle:
        caption_data = {
            "version": 2,
            "onImage": on_image,
            "offImage": off_image,
            "buttonId": 1,
            "numButtonsInGroup": 1,
            "handleName": handle_name,
            "gestureBehaviour": "Instant",
        }
        caption_type = "Button"
    else:
        caption_data = {
            "version": 1,
            "onImage": on_image,
            "offImage": off_image,
            "indicatorId": 1,
            "numIndicatorsInGroup": 1,
            "handleName": handle_name,
        }
        caption_type = "Indicator"

    name_comp = {
        "version": 2,
        "componentData": {"version": 1, "name": "name", "type": caption_type, "data": caption_data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % tuple(rects["name"]),
        },
    }

    value_comp = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "value",
            "type": "Label",
            "data": {
                "version": 1,
                "textStyle": {
                    "version": 1,
                    "font": {"version": 1, "name": _style_size('font.family'), "style": _style_size('font.style.regular'), "height": float(_SMOOTH_VALUE_HEIGHT)},
                    "colour": _style_argb("smooth.value.on" if active else "smooth.value.off"),
                    "justification": "left verticallyCentred",
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
            "bounds": "%d %d %d %d" % tuple(rects["value"]),
        },
    }

    return {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": background.copy(),
            "unfocussed": background,
        },
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": not active,
        "componentsData": [focus_comp, knob_comp, name_comp, value_comp],
    }


def build_native_smooth_dual_state_pair(
    time_param_idx,
    switch_param_idx,
    rect=(1064, 546, 196, 72),
    active_type="compactNativeSmoothWithIndicator",
    passive_type="compactNativeSmoothReadout",
    name_prefix="Play LFO Smooth",
):
    """Build a pair of active and passive native entries gated via IndexedEnabling.

    When switch_param_idx is 1: active component is shown (knob is active, SMOOTH caption bright).
    When switch_param_idx is 0: passive component is shown (readout only, knob inactive, SMOOTH caption dim).
    """
    bounds_str = "%d %d %d %d" % tuple(rect) if isinstance(rect, (tuple, list)) else str(rect)
    remap = [
        {"key": "Data", "value": f"Parameter {time_param_idx}"},
        {"key": "Caption", "value": f"WithName[SMOOTH]/Parameter {time_param_idx}"},
        {"key": "State", "value": f"Parameter {switch_param_idx}"},
        {"key": "Active", "value": f"Parameter {switch_param_idx}"},
    ]

    active_entry = {
        "version": 2,
        "componentData": {"version": 1, "name": f"{name_prefix} Active", "type": active_type},
        "handle remapping": {"version": 1, "map": copy.deepcopy(remap)},
        "bounds": {
            "version": 2,
            "acceptsHWFocus": "Yes",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": bounds_str,
            "additionalInvalidatingHandles": [f"IndexedEnabling/1/2/Parameter {switch_param_idx}"],
        },
    }

    passive_entry = {
        "version": 2,
        "componentData": {"version": 1, "name": f"{name_prefix} Off", "type": passive_type},
        "handle remapping": {"version": 1, "map": copy.deepcopy(remap)},
        "bounds": {
            "version": 2,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": bounds_str,
            "additionalInvalidatingHandles": [f"IndexedEnabling/0/2/Parameter {switch_param_idx}"],
        },
    }

    return active_entry, passive_entry


__all__ = [
    # Level API
    "SIZE",
    "FRAMES",
    "COMPACT_48_RECTS",
    "DEFAULT_DIAL_RECTS",
    "value_label",
    "_value_label",
    "default_level_template",
    "write_strip",
    "synthyboy_dial_data",
    "sub_dial_definition",
    "level_definition",
    "compact_level_definition",
    # CompactDial API
    "DEFAULT_RECTS",
    "compact_dial_definition",
    # Smooth API
    "PLAY_SMOOTH_RECTS",
    "clone",
    "is_smooth_applicable",
    "lfo_smooth_param_key",
    "smooth_binding",
    "build_standalone_smooth",
    "instantiate_standalone_smooth_for_tab",
    "build_native_smooth_dial_with_indicator_definition",
    "build_native_smooth_dual_state_pair",
]


def blue_dial_definition(skins_dir, width, height, frame_size, knob_rect, name_rect, value_rect, *,
                         caption_text=None, caption_height=None, value_height=None):
    """Compose the shared LEVEL filmstrip, focus and native name/value labels."""
    from components.native.name_value.name_value import _label_component

    rects = {"focus": (0, 0, width, height), "knob": tuple(knob_rect),
             "name": tuple(name_rect), "value": tuple(value_rect)}
    definition = build_native_smooth_dial_with_indicator_definition(rects=rects, skins_dir=skins_dir)
    focus, knob = definition["componentsData"][:2]
    knob["bounds"]["acceptsHWFocus"] = "Yes"
    knob["componentData"]["data"] = synthyboy_dial_data(skins_dir, frame_size=frame_size, bipolar=False)
    caption_height = _style_size("level.blue.caption.font") if caption_height is None else caption_height
    value_height = _style_size("level.blue.value.font") if value_height is None else value_height
    name = _label_component("name", "Text" if caption_text is not None else "Name",
                            "Data" if caption_text is not None else "Caption", rects["name"],
                            caption_height, _style_argb("text.caption"), "left verticallyCentred")
    if caption_text is not None:
        name["componentData"]["data"]["text"] = caption_text
    value = _label_component("value", "Value", "Data", rects["value"], value_height,
                             _style_argb("text.value"), "left verticallyCentred")
    definition["componentsData"] = [focus, knob, name, value]
    return definition


def master_control_definition(kind, w, h):
    """Native master control composition with calibrated relative dial geometry."""
    from components.native.button.button import entry
    from components.native.name_value.name_value import _label_component
    metrics = _style_size("level.master.control")
    inset, half, label_h = metrics["padding"], metrics["half_padding"], metrics["label_height"]

    def label(caption, rect):
        child = _label_component("caption" if caption else "value", "Name" if caption else "Value",
                                 "Caption" if caption else "Data", rect,
                                 metrics["caption_font" if caption else "value_font"],
                                 _style_argb("master.meter.scale.text" if caption else "text.icon"),
                                 "horizontallyCentred verticallyCentred")
        child["bounds"]["showWhenDataModelInvalid"] = "Hide"
        return child

    def action(trigger, handler, popup=""):
        return {"version": 1, "onAction": trigger, "handler": handler,
                "handleName": "Data", "additionalData": popup}

    if kind.startswith("dial_"):
        children = [label(True, (inset, 0, w - 2 * inset, label_h)),
                    entry("dial", "Knob", {"version": 5, "knobType": "GenericNoLabel",
                          "polarity": "BiPolar" if kind == "dial_bi" else "UniPolar",
                          "dragOrientation": "Vertical", "handleName": "Data"},
                          (inset, label_h, w - 2 * inset, h - 2 * label_h), focus="No"),
                    label(False, (inset, h - label_h, w - 2 * inset, label_h))]
        for child, relative in zip(children, metrics["relative_bounds"]):
            bounds = child["bounds"]
            bounds.pop("bounds")
            bounds.update(boundsType="Relative", relativeBounds=relative)
        actions = [action(trigger, "Show Overlay", "fx editor number menu")
                   for trigger in ("Double Click", "Enter Pressed")]
    else:
        caption_width = w * 3 // 5
        children = [label(True, (half, 0, caption_width - inset, h)),
                    label(False, (caption_width, 0, w - caption_width - half, h))]
        if kind == "selector":
            children = [label(True, (inset, 0, w - 2 * inset, label_h)),
                        label(False, (inset, label_h, w - 2 * inset, h - label_h))]
        handler = "Toggle Switch" if kind == "toggle" else "Show Overlay"
        popup = {"selector": "fx editor choice menu", "number": "fx editor number menu"}.get(kind, "")
        actions = [action(trigger, handler, popup) for trigger in ("Mouse Down", "Enter Pressed")]
    return {"version": 2, "actions": actions,
            "backgroundData": {"version": 1,
                "focussed": {"version": 1, "colour": _style_argb("field.focus"), "image": ""},
                "unfocussed": {"version": 1, "colour": _style_argb("button.surface"), "image": ""}},
            "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": children}
