# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC LEVEL dial component and generator with Smooth and CompactDial variants."""
import copy
import functools
import math
from pathlib import Path

from components.style import (
    focus_ring_data,
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
                    "colour": _style_argb("text.value"),
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
        "actions": [],
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
                    "data": synthyboy_dial_data(None),
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
                            "case": "Upper Case",
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


# The look whose tokens the native artwork follows (the GPU overlay's default look 1).
DIAL_LOOK = "neon"


def dial_caption_style(text_style):
    """The name of a dial as ui::Knob writes it: upper case in the dim text colour (theme textDim)."""
    text_style["colour"] = _style_argb("text.caption")
    text_style["case"] = "Upper Case"
    return text_style


def _inactive_grey(rgb):
    """ui::Pen::inactiveGrey: the luma, darkened to knob.inactive.percent (the drawn off look)."""
    r, g, b = rgb
    value = (r * 30 + g * 59 + b * 11) // 100 * int(_style_size("knob.inactive.percent")) // 100
    return (value, value, value)


def dial_geometry(diameter):
    """ui::Knob::geom for a dial of `diameter` px: (radius, value stroke, track stroke) in px."""
    metrics = _style_size("theme." + DIAL_LOOK)
    stroke = max(_style_size("knob.stroke.min"), diameter * metrics["arcValue"])
    track = max(_style_size("knob.track.min"), diameter * metrics["arcTrack"])
    radius = diameter / 2 - stroke / 2 - _style_size("knob.radius.inset")
    return radius, stroke, track


def render_dial_frame(diameter, value, *, bipolar=False, active=True):
    """One dial picture exactly as ui::Knob draws it (drawStatic + drawLive), from the shared tokens.

    The track arc, the value arc in the four-stop flow gradient along the drawn piece, the
    bipolar centre tick, the pointer and the white thumb on the arc. Supersampled, then reduced.
    """
    import numpy as np
    from components.style import theme_colors

    palette = theme_colors(DIAL_LOOK)
    to_rgb = (lambda name: _inactive_grey(tuple(int(name[i:i + 2], 16) for i in (1, 3, 5)))) if not active \
        else (lambda name: tuple(int(name[i:i + 2], 16) for i in (1, 3, 5)))
    track_rgb, text_rgb, dim_rgb = to_rgb(palette["track"]), to_rgb(palette["text"]), to_rgb(palette["textDim"])
    flow = np.array([to_rgb(c) for c in palette["flow"]], dtype=np.float64)

    ss = int(_style_size("knob.supersample"))
    side = diameter * ss
    radius, stroke, track = (v * ss for v in dial_geometry(diameter))
    centre = side / 2
    start = math.radians(_style_size("knob.start.degrees"))
    sweep = math.radians(_style_size("knob.sweep.degrees"))
    angle_of = lambda v: start + min(max(v, 0.0), 1.0) * sweep

    ys, xs = np.mgrid[0:side, 0:side].astype(np.float64) + 0.5
    dx, dy = xs - centre, ys - centre
    dist = np.hypot(dx, dy)
    theta = np.mod(np.arctan2(dy, dx) - start, 2 * math.pi)   # 0 .. 2 pi from the start angle, clockwise on screen
    rgba = np.zeros((side, side, 4), dtype=np.float64)

    def over(mask_alpha, colour):
        alpha = np.clip(mask_alpha, 0.0, 1.0)[..., None]
        col = np.broadcast_to(np.asarray(colour, dtype=np.float64), rgba.shape[:2] + (3,)) if np.ndim(colour) == 1 else colour
        rgba[..., :3] = col * alpha + rgba[..., :3] * (1 - alpha)
        rgba[..., 3:] = alpha + rgba[..., 3:] * (1 - alpha)

    def arc_mask(a0, a1, width):
        """A round-capped arc from angle a0 to a1 (a0 <= a1, relative to the start), anti-aliased by the supersampling."""
        rel = theta
        inside = (rel >= a0) & (rel <= a1) & (np.abs(dist - radius) <= width / 2)
        mask = inside.astype(np.float64)
        for a in (a0, a1):
            ex, ey = centre + radius * math.cos(start + a), centre + radius * math.sin(start + a)
            mask = np.maximum(mask, (np.hypot(xs - ex, ys - ey) <= width / 2).astype(np.float64))
        return mask

    def segment_mask(r0, r1, a, width):
        ax, ay = centre + r0 * math.cos(a), centre + r0 * math.sin(a)
        bx, by = centre + r1 * math.cos(a), centre + r1 * math.sin(a)
        vx, vy = bx - ax, by - ay
        length2 = vx * vx + vy * vy
        u = np.clip(((xs - ax) * vx + (ys - ay) * vy) / length2, 0.0, 1.0)
        return (np.hypot(xs - (ax + u * vx), ys - (ay + u * vy)) <= width / 2).astype(np.float64)

    # drawStatic: the track over the whole sweep, the centre tick of a bipolar dial
    over(arc_mask(0.0, sweep, track), track_rgb)
    mid = sweep / 2
    if bipolar:
        overhang = _style_size("knob.centre.overhang") * ss
        tick = segment_mask(radius - stroke / 2 - overhang, radius + stroke / 2 + overhang, start + mid,
                            _style_size("knob.centre.stroke") * ss)
        over(tick * _style_size("knob.centre.alpha") / 255, dim_rgb)
    # drawLive: the value arc in the flow gradient along the drawn piece (from the start, or from the middle)
    av = angle_of(value) - start
    origin = mid if bipolar else 0.0
    lo, hi = min(origin, av), max(origin, av)
    if hi - lo > 0.004:
        t = np.clip((theta - lo) / (hi - lo), 0.0, 1.0)
        s = t * 3.0
        index = np.minimum(s.astype(int), 2)
        u = (s - index)[..., None]
        gradient = flow[index] * (1 - u) + flow[index + 1] * u
        # the round caps take the colour of their own end
        gradient = np.where((theta < lo)[..., None] | (theta > 2 * math.pi - 0.5)[..., None], flow[0], gradient)
        gradient = np.where(((theta > hi) & (theta <= 2 * math.pi - 0.5))[..., None], flow[3], gradient)
        over(arc_mask(lo, hi, stroke), gradient)
    # the pointer and the thumb
    a = start + av
    pointer = segment_mask(radius * _style_size("knob.pointer.inner"), radius * _style_size("knob.pointer.outer"), a,
                           _style_size("knob.pointer.stroke") * ss)
    over(pointer * _style_size("knob.pointer.alpha") / 255, text_rgb)
    thumb = max(_style_size("knob.thumb.min") * ss, stroke * _style_size("knob.thumb.ratio"))
    tx, ty = centre + radius * math.cos(a), centre + radius * math.sin(a)
    over((np.hypot(xs - tx, ys - ty) <= thumb).astype(np.float64), text_rgb)

    # reduce the supersampled picture with premultiplied box averaging
    premultiplied = np.concatenate([rgba[..., :3] * rgba[..., 3:], rgba[..., 3:]], axis=2)
    small = premultiplied.reshape(diameter, ss, diameter, ss, 4).mean(axis=(1, 3))
    alpha = small[..., 3:]
    colour = np.where(alpha > 0, small[..., :3] / np.maximum(alpha, 1e-9), 0.0)
    out = np.concatenate([colour, alpha * 255], axis=2)
    return Image.fromarray(np.clip(np.rint(out), 0, 255).astype(np.uint8), "RGBA")


@functools.lru_cache(maxsize=32)
def _strip_png(frame_size, bipolar, active):
    import io
    strip = Image.new("RGBA", (frame_size, frame_size * FRAMES))
    for i in range(FRAMES):
        strip.paste(render_dial_frame(frame_size, i / (FRAMES - 1), bipolar=bipolar, active=active), (0, i * frame_size))
    buffer = io.BytesIO()
    strip.save(buffer, format="PNG")
    return buffer.getvalue()


def write_strip(path, frame_size=SIZE, bipolar=False, active=True):
    """The native knob filmstrip: FRAMES pictures of render_dial_frame, minimum at the top."""
    if Image is None:
        raise RuntimeError("Pillow is required to generate dial filmstrips")
    Path(path).write_bytes(_strip_png(int(frame_size), bool(bipolar), bool(active)))


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
    for item in definition["componentsData"]:
        if item["componentData"].get("type") == "Focus":
            item["componentData"]["data"] = focus_ring_data()
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
        if name == "name":
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
        if control.get("type") == "Focus":
            control["data"] = focus_ring_data()
        name = control["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % tuple(rects[name])
        if name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.compact_level_definition.font.height')
            dial_caption_style(control["data"]["textStyle"])
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

def _dial_for_polarity(skins_dir, polarity, frame_size):
    if polarity == "BiPolar":
        from components.native.pan.pan import synthyboy_dial_data as pan_dial_data
        return pan_dial_data(skins_dir, frame_size=frame_size)
    if polarity == "UniPolar":
        return synthyboy_dial_data(skins_dir, frame_size=frame_size)
    raise ValueError(f"Unsupported native dial polarity: {polarity}")


def compact_dial_definition(template, rects=None, polarity="UniPolar", height=None, *, skins_dir=None):
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
        if control.get("type") == "Focus":
            control["data"] = focus_ring_data()
        name = control["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % tuple(rects[name])
        if name == "knob":
            control["data"] = _dial_for_polarity(skins_dir, polarity, rects["knob"][2])
        elif name == "name":
            control["data"]["handleName"] = "Caption"
            font = control["data"].setdefault("textStyle", {}).setdefault("font", {})
            font["height"] = _style_size('level.compact_dial_definition.font.height') if is_compact else _style_size('level.compact_dial_definition.font.height.offset1')
            dial_caption_style(control["data"]["textStyle"])
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
            "data": focus_ring_data(),
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

    knob_data = synthyboy_dial_data(skins_dir, frame_size=rects["knob"][2], active=active)
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
    "bare_dial_definition",
    "stacked_dial_definition",
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
    dial_caption_style(name["componentData"]["data"]["textStyle"])
    if caption_text is not None:
        name["componentData"]["data"]["text"] = caption_text
    value = _label_component("value", "Value", "Data", rects["value"], value_height,
                             _style_argb("text.value"), "left verticallyCentred")
    definition["componentsData"] = [focus, knob, name, value]
    return definition


def bare_dial_definition(skins_dir, diameter, *, polarity):
    """A focused, draggable native knob without a caption or value."""
    from components.native.button.button import entry
    from components.native.selector.selector import passive_layer

    rect = (0, 0, diameter, diameter)
    focus = entry("focus", "Focus", focus_ring_data(), rect, focus="No")
    focus["bounds"]["whenVisible"] = "WhenFocussed"
    knob = entry("knob", "Knob", _dial_for_polarity(skins_dir, polarity, diameter), rect)
    return passive_layer([focus, knob])


def stacked_dial_definition(skins_dir, size, *, diameter, caption_height,
                            polarity="UniPolar", alternate_caption=None):
    """Caption above an editable dial and host-formatted value below it.

    alternate_caption is (normal gate, alternate gate, alternate text).
    """
    from components.native.name_value.name_value import _label_component

    width, height = size
    rects = {"focus": (0, 0, width, height),
             "knob": ((width - diameter) // 2, caption_height, diameter, diameter),
             "name": (0, 0, width, caption_height),
             "value": (0, caption_height + diameter, width, height - caption_height - diameter)}
    if min(rects["value"][3], caption_height, diameter) <= 0 or diameter > width:
        raise ValueError("Stacked dial needs space for its caption, knob and value")

    definition = bare_dial_definition(skins_dir, diameter, polarity=polarity)
    focus, knob = definition["componentsData"]
    focus["bounds"]["bounds"] = " ".join(map(str, rects["focus"]))
    knob["bounds"]["bounds"] = " ".join(map(str, rects["knob"]))
    centre = "horizontallyCentred verticallyCentred"
    caption = _label_component("name", "Name", "Caption", rects["name"],
                               _style_size("font.caption"), _style_argb("text.caption"), centre)
    dial_caption_style(caption["componentData"]["data"]["textStyle"])
    value = _label_component("value", "Value", "Data", rects["value"],
                             _style_size("font.value"), _style_argb("text.value"), centre)
    definition["componentsData"].extend([caption, value])
    if alternate_caption:
        normal_gate, alt_gate, alt_text = alternate_caption
        caption["bounds"].update(version=2, showWhenDataModelInvalid="Hide", additionalInvalidatingHandles=[normal_gate])
        alternate = copy.deepcopy(caption)
        alternate["bounds"]["additionalInvalidatingHandles"] = [alt_gate]
        alternate["componentData"]["name"] = "alternate caption"
        alternate["componentData"]["data"].update(type="Text", text=alt_text)
        definition["componentsData"].append(alternate)
    definition["ignoreMousePresses"] = False
    definition["actions"] = [
        {"version": 1, "onAction": trigger, "handler": "Show Overlay",
         "handleName": "Data", "additionalData": "fx editor number menu"}
        for trigger in ("Double Click", "Enter Pressed")
    ]
    return definition
