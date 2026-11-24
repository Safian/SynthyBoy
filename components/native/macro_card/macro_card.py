# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC MacroCard component, definitions, and renderer."""
import copy
import hashlib
import math
from pathlib import Path

from components.style import (
    argb as _style_argb,
    color as _style_color,
    opacity as _style_opacity,
    rgba as _style_rgba,
    rect as _style_rect,
    size as _style_size,
)
import tempfile
from types import SimpleNamespace

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

CARD_WIDTH, CARD_HEIGHT = _style_size('macro_card.width'), _style_size('macro_card.height')
DIAL_RECT = tuple(_style_size('macro_card.dial_rect'))
KNOB_RECT = tuple(_style_size('macro_card.knob_rect'))
NATIVE_KNOB_RECT = tuple(_style_size('macro_card.native_knob_rect'))
NAME_RECT = tuple(_style_size('macro_card.name_rect'))
NAME_HIT_RECT = tuple(_style_size('macro_card.name_hit_rect'))
VALUE_RECT = tuple(_style_size('macro_card.value_rect'))
TARGET_RECT = tuple(_style_size('macro_card.target_rect'))
DIAL_HEIGHT = DIAL_RECT[3]
ACTION_SLOT_HEIGHT = TARGET_RECT[3]
DIAL_BUTTON_GAP = _style_size('macro_card.action.gap')
SUB_DIAL_SIZE, SUB_DIAL_FRAMES = KNOB_RECT[2], 127
MACRO_CARD_POSITIONS = [tuple(pos) for pos in _style_size('macros.card_positions')]
DEFAULT_MACRO_DIAL_RECTS = {
    "focus": DIAL_RECT,
    "knob": KNOB_RECT,
    "name": NAME_RECT,
}

FONT_FILE = Path(__file__).resolve().parent / "img" / _style_size('font.file.semibold')


def semibold(size):
    if ImageFont is None or not FONT_FILE.is_file():
        return None
    return ImageFont.truetype(str(FONT_FILE), size)


def _value_label(name, rect, height=_style_size('macro_card._value_label.geometry')):
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


def default_dial_template():
    """Fallback unipolar knob template matching SDK knobUni structure."""
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
                    "bounds": _style_rect("macro_card.default_dial_template.geometry"),
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
                    "bounds": _style_rect("macro_card.default_dial_template.geometry.offset1"),
                },
            },
        ],
    }


def write_level_strip(path, frame_size=SUB_DIAL_SIZE):
    if Image is None:
        raise RuntimeError("Pillow is required to generate dial filmstrips")
    scale = _style_size('macro_card.write_level_strip.scale')
    center = (SUB_DIAL_SIZE - _style_size('macro_card.write_level_strip.center')) * scale / 2
    radius = _style_size('macro_card.write_level_strip.radius') * scale

    def point(angle, r):
        return (
            center + r * math.cos(math.radians(angle)),
            center + r * math.sin(math.radians(angle)),
        )

    strip = Image.new("RGBA", (frame_size, frame_size * SUB_DIAL_FRAMES))
    bounds = (center - radius, center - radius, center + radius, center + radius)
    for i in range(SUB_DIAL_FRAMES):
        frame = Image.new("RGBA", (SUB_DIAL_SIZE * scale, SUB_DIAL_SIZE * scale))
        draw = ImageDraw.Draw(frame)
        angle = _style_size('macro_card.write_level_strip.angle') + _style_size('macro_card.write_level_strip.angle.offset1') * i / (SUB_DIAL_FRAMES - _style_size('macro_card.write_level_strip.angle.offset2'))
        draw.arc(bounds, _style_size('macro_card.write_level_strip.arc'), _style_size('macro_card.write_level_strip.arc.offset1'), fill=_style_color("dial.track"), width=_style_size('stroke.arc') * scale)
        start = _style_size('macro_card.write_level_strip.start')
        if angle != start:
            draw.arc(bounds, min(angle, start), max(angle, start), fill=_style_color("dial.value"), width=_style_size('stroke.arc.active') * scale)
        draw.line(
            (point(angle, radius * 0.25), point(angle, radius * 0.6)),
            fill=_style_color("text.value"),
            width=round(_style_size('macro_card.write_level_strip.round') * scale),
        )
        x, y = point(angle, radius - _style_size('macro_card.write_level_strip.x.y') * scale)
        dot = _style_size('macro_card.write_level_strip.dot') * scale
        draw.ellipse((x - dot, y - dot, x + dot, y + dot), fill=_style_color("text.value"))
        strip.paste(
            frame.resize((frame_size, frame_size), Image.Resampling.LANCZOS),
            (0, i * frame_size),
        )
    strip.save(path)


def synthyboy_level_dial_data(skins_dir, frame_size=SUB_DIAL_SIZE):
    filename = f"play_sub_level_{frame_size}.png"
    if skins_dir is not None:
        write_level_strip(Path(skins_dir) / filename, frame_size=frame_size)
    return {
        "version": 3,
        "knobType": "FilmStrip",
        "filmStrip": filename,
        "numFrames": SUB_DIAL_FRAMES,
        "invert": False,
        "dragOrientation": "Vertical",
        "handleName": "Data",
    }


def compact_dial_definition(template, rects=None, polarity="UniPolar"):
    definition = copy.deepcopy(template)
    for state in ("focussed", "unfocussed"):
        if state in definition.get("backgroundData", {}):
            definition["backgroundData"][state]["colour"] = _style_argb("surface.control")

    if rects is None:
        rects = copy.deepcopy(DEFAULT_MACRO_DIAL_RECTS)

    for item in definition.get("componentsData", []):
        control = item["componentData"]
        name = control["name"]
        if name in rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % rects[name]
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
            font["height"] = _style_size('macro_card.compact_dial_definition.font.height')
            control["data"]["textStyle"]["colour"] = _style_argb("text.caption")

    definition["actions"] = [
        a for a in definition.get("actions", []) if a.get("handler") != "Show Overlay"
    ]
    return definition


def sub_dial_definition(template, skins_dir, *, bipolar=False, include_name=True):
    definition = copy.deepcopy(template)
    controls = {item["componentData"]["name"]: item for item in definition["componentsData"]}
    controls["knob"]["componentData"]["data"] = synthyboy_level_dial_data(skins_dir)
    if include_name:
        if "name" not in controls:
            default_name = next(
                copy.deepcopy(c) for c in default_dial_template()["componentsData"] if c["componentData"]["name"] == "name"
            )
            definition["componentsData"].append(default_name)
            controls["name"] = default_name
        caption = controls["name"]
        caption["bounds"]["bounds"] = _style_rect("macro_card.name_rect")
        style = caption["componentData"]["data"]["textStyle"]
        style["colour"] = _style_argb("text.caption")
        style["font"]["height"] = _style_size('macro_card.sub_dial_definition.style.font.height')
        style["justification"] = "horizontallyCentred verticallyCentred"
    value = _value_label("value", VALUE_RECT, _style_size('macro_card.sub_dial_definition.value'))
    value_style = value["componentData"]["data"]["textStyle"]
    value_style.update(colour=_style_argb("text.value"), justification="horizontallyCentred verticallyCentred")
    definition["componentsData"] = [
        c for c in definition["componentsData"]
        if c["componentData"]["name"] != "value" and (include_name or c["componentData"]["name"] != "name")
    ]
    definition["componentsData"].append(value)
    return definition


def macro_level_definition(template=None, skins_dir=None, rects=None, *, include_name=True):
    """Produces the authentic MacroCard dial component definition from shared MacroCard tokens."""
    if template is None:
        template = default_dial_template()
    dial_rects = rects or DEFAULT_MACRO_DIAL_RECTS
    level = compact_dial_definition(template, dial_rects, polarity="UniPolar")

    return sub_dial_definition(level, skins_dir, bipolar=False, include_name=include_name)


def action_art(*, icon=None, text=None, width=None, colour=_style_color("text.icon")):
    if Image is None:
        raise RuntimeError("Pillow is required for action_art")
    if bool(icon) == bool(text):
        raise ValueError("Supply exactly one icon or text label")
    art = _style_size('button.art')
    scale, height = art['scale'], art['height']
    font = semibold(art['font'] * scale)
    minimum = height if icon else math.ceil(font.getlength(text) / scale) + art['padding']
    width = minimum if width is None else width
    if width < minimum or (icon and width != height):
        raise ValueError("Button width does not fit its content")
    image = Image.new("RGBA", (width * scale, height * scale))
    pen = ImageDraw.Draw(image)
    pen.rounded_rectangle(
        (0, 0, image.width - art['outline'], image.height - art['outline']),
        radius=art['radius'] * scale,
        fill=_style_color("button.surface"), outline=_style_color("button.border"), width=art['outline'] * scale,
    )

    def stroke(points, stroke_width):
        ox, oy = art['origin']
        points = [(round((x - ox) * scale), round((y - oy) * scale)) for x, y in points]
        pen.line(points, fill=colour, width=round(stroke_width * scale), joint="curve")
        radius = stroke_width * scale / 2
        for x, y in (points[0], points[-1]):
            pen.ellipse((x - radius, y - radius, x + radius, y + radius), fill=colour)

    if icon == "power":
        geometry = art['power']
        cx, cy = geometry['center']
        radius = geometry['radius']
        arc = [(cx + radius * math.cos(math.radians(a)), cy + radius * math.sin(math.radians(a)))
               for a in range(*geometry['arc'])]
        stroke(arc, geometry['stroke'])
        stroke(geometry['stem'], geometry['stroke'])
    elif icon == "expand":
        geometry = art['expand']
        arm = geometry['arm']
        for x, y, dx, dy in geometry['corners']:
            stroke([(x, y + dy * arm), (x, y), (x + dx * arm, y)], geometry['stroke'])
    elif text:
        pen.text((image.width / 2, image.height / 2), text, font=font, fill=colour, anchor="mm")
    else:
        raise ValueError(icon)
    return image.resize((width, height), Image.Resampling.LANCZOS)


def in_slot(face, size):
    if Image is None:
        raise RuntimeError("Pillow is required for in_slot")
    w, h = size
    if w < face.width or h < face.height:
        raise ValueError("Button exceeds its slot")
    image = Image.new("RGBA", size)
    image.alpha_composite(face, ((w - face.width) // 2, (h - face.height) // 2))
    return image


def entry(name, kind, data, rect, remaps=(), gates=(), focus="Yes"):
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
    return entry(
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


def action_button(ctx, name, rect, action, *, icon=None, text=None, width=None, gates=()):
    """Bind one immediate MPC action to a centered 48 px icon or label."""
    if not action:
        raise ValueError("An action handle is required")
    image = in_slot(action_art(icon=icon, text=text, width=width), tuple(rect[2:]))
    digest = hashlib.sha256(str(image.size).encode() + image.tobytes()).hexdigest()[:20]
    filename = f"native_action_{digest}.png"
    skins_dir = getattr(ctx, "skins_dir", ctx)
    image.save(Path(skins_dir) / filename)
    return button(name, rect, filename, remaps=(("Data", action),), gates=gates)


TARGET_X = TARGET_RECT[0]
TARGET_SLOT_WIDTH = TARGET_RECT[2]


def build_macro_card_definition(skins_dir, action_text="NO TARGET", *, level_type="macroSubLevel"):
    """Build the card entry. Use build_macro_card_definitions to export the complete standalone skin definitions."""
    level = entry(
        "level",
        level_type,
        {"version": 1, "handleName": "Data"},
        DIAL_RECT,
        (("Data", "Data"), ("Caption", "Caption")),
    )
    target = action_button(
        SimpleNamespace(skins_dir=Path(skins_dir)),
        "target",
        TARGET_RECT,
        "Target",
        text=action_text,
        width=None,
    )
    background = {"version": 1, "colour": _style_argb("surface.panel"), "image": ""}
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": background.copy(), "unfocussed": background},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [level, target],
    }


def build_macro_card_definitions(
    skins_dir, action_text="NO TARGET", dial_template=None, *, include_name=True, level_type="macroSubLevel"
):
    """Export both SDK definitions and all images required by a standalone MacroCard."""
    return {
        level_type: macro_level_definition(dial_template, skins_dir, include_name=include_name),
        "macroCard": build_macro_card_definition(skins_dir, action_text, level_type=level_type),
    }


def build_macro_card_instance(
    name, param_idx, rect, *, target_handle, caption=None, card_type="macroCard", root_gate=None
):
    """Bind the continuous value and target action independently for each card."""
    if not target_handle or param_idx < 0:
        raise ValueError("The macro parameter and target action must both be supplied")
    model = f"Parameter {param_idx}"
    handles = (
        ("Data", model),
        ("Caption", f"WithName[{caption}]/" + model if caption else model),
        ("Target", target_handle),
    )
    return entry(
        name,
        card_type,
        {"version": 1, "handleName": "Data"},
        rect,
        handles,
        gates=(root_gate,) if root_gate else (),
        focus="Children",
    )


def render_macro_card(
    level_definition,
    strip_image,
    name,
    percent,
    target,
    width=CARD_WIDTH,
    height=CARD_HEIGHT,
    button_width=None,
):
    """Renders one authentic MacroCard reading geometry and assets from level_definition."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_macro_card")
    card = Image.new("RGBA", (width, height), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(card)

    draw.rectangle((0, 0, width - _style_size('macro_card.macro_card.rectangle'), height - _style_size('macro_card.macro_card.rectangle')), fill=_style_color("surface.panel"))
    level_bg = level_definition["backgroundData"]["unfocussed"]["colour"]
    draw.rectangle((0, 0, width - _style_size('macro_card.macro_card.rectangle'), DIAL_HEIGHT - _style_size('macro_card.macro_card.rectangle')), fill="#" + level_bg[-6:])

    # 1. Read knob geometry from level_definition
    knob_comp = next(
        c for c in level_definition["componentsData"] if c["componentData"]["name"] == "knob"
    )
    knob_bounds = [int(v) for v in knob_comp["bounds"]["bounds"].split()]
    knob_data = knob_comp["componentData"]["data"]
    num_frames = knob_data.get("numFrames", SUB_DIAL_FRAMES)

    # Crop knob frame from filmstrip
    norm_val = max(0.0, min(100.0, float(percent))) / 100.0
    frame_idx = round(norm_val * (num_frames - _style_size('macro_card.macro_card.frame.idx')))
    dial_frame = strip_image.crop(
        (0, frame_idx * SUB_DIAL_SIZE, SUB_DIAL_SIZE, (frame_idx + _style_size('macro_card.macro_card.dial.frame')) * SUB_DIAL_SIZE)
    )
    card.alpha_composite(dial_frame, (knob_bounds[0], knob_bounds[1]))

    # 2. Read name geometry from level_definition
    name_comp = next(
        c for c in level_definition["componentsData"] if c["componentData"]["name"] == "name"
    )
    name_bounds = [int(v) for v in name_comp["bounds"]["bounds"].split()]
    name_style = name_comp["componentData"]["data"]["textStyle"]
    name_font_size = round(name_style["font"]["height"])
    name_colour_hex = "#" + name_style["colour"][-6:]
    f_name = semibold(name_font_size)
    y_name = name_bounds[1] + name_bounds[3] // 2
    if "horizontallyCentred" in name_style.get("justification", ""):
        draw.text((name_bounds[0] + name_bounds[2] // 2, y_name), name, font=f_name, fill=name_colour_hex, anchor="mm")
    else:
        draw.text((name_bounds[0], y_name), name, font=f_name, fill=name_colour_hex, anchor="lm")

    # 3. Read value geometry from level_definition (frameless percentage)
    val_comp = next(
        c for c in level_definition["componentsData"] if c["componentData"]["name"] == "value"
    )
    val_bounds = [int(v) for v in val_comp["bounds"]["bounds"].split()]
    val_style = val_comp["componentData"]["data"]["textStyle"]
    val_font_size = round(val_style["font"]["height"])
    val_colour_hex = "#" + val_style["colour"][-6:]
    f_val = semibold(val_font_size)
    y_val = val_bounds[1] + val_bounds[3] // 2
    if "horizontallyCentred" in val_style.get("justification", ""):
        draw.text((val_bounds[0] + val_bounds[2] // 2, y_val), f"{round(percent)}%", font=f_val, fill=val_colour_hex, anchor="mm")
    else:
        draw.text((val_bounds[0], y_val), f"{round(percent)}%", font=f_val, fill=val_colour_hex, anchor="lm")

    # 4. Action button (content-sized face in reading column slot)
    btn_colour = _style_color("text.icon")
    btn_face = action_art(text=target or "NO TARGET", width=button_width, colour=btn_colour)
    btn_slot = in_slot(btn_face, (TARGET_RECT[2], TARGET_RECT[3]))
    card.alpha_composite(btn_slot, (TARGET_RECT[0], TARGET_RECT[1]))

    return card


def render_macro_preview(output_path, dial_template=None):
    """Renders authentic 3-card comparison preview and saves to output_path."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_macro_preview")
    samples = [
        ("MACRO 1", 50, "F1 CUTOFF"),
        ("MACRO 2", 75, "F1 RES (+2)"),
        ("MACRO 3", 0, "NO TARGET"),
    ]

    card_w, card_h = CARD_WIDTH, CARD_HEIGHT
    pad_x, pad_y = _style_size('macro_card.macro_preview.pad.x.pad.y'), _style_size('macro_card.macro_preview.pad.x.pad.y')
    total_w = len(samples) * card_w + (len(samples) + _style_size('macro_card.macro_preview.total.w')) * pad_x
    total_h = card_h + _style_size('macro_card.macro_preview.total.h') * pad_y

    canvas = Image.new("RGBA", (total_w, total_h), _style_color("surface.control"))

    with tempfile.TemporaryDirectory() as td:
        skins_dir = Path(td)
        level_def = macro_level_definition(dial_template, skins_dir)
        strip_path = skins_dir / "play_sub_level_90.png"
        with Image.open(strip_path) as strip_im:
            for idx, (name, val, tgt) in enumerate(samples):
                card_im = render_macro_card(
                    level_def, strip_im, name, val, tgt, width=card_w, height=card_h
                )
                x = pad_x + idx * (card_w + pad_x)
                canvas.alpha_composite(card_im, (x, pad_y))

    out_file = Path(output_path)
    out_file.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out_file)
    return str(out_file)


class MacroCard:
    """Component namespace and factory class for MacroCard."""
    build_definition = staticmethod(build_macro_card_definition)
    build_instance = staticmethod(build_macro_card_instance)
    level_definition = staticmethod(macro_level_definition)
    render_card = staticmethod(render_macro_card)
    render_preview = staticmethod(render_macro_preview)
