# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC MacroCard component, definitions, and renderer."""
import copy
from pathlib import Path

from components.native.button.button import action_art, action_button, entry, in_slot
from components.native.level.level import FRAMES as SUB_DIAL_FRAMES, default_level_template, level_definition
from components.style import (
    argb as _style_argb,
    color as _style_color,
    opacity as _style_opacity,
    rgba as _style_rgba,
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
SUB_DIAL_SIZE = KNOB_RECT[2]
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


def macro_level_definition(template=None, skins_dir=None, rects=None, *, include_name=True):
    """Lay out the shared LEVEL dial in the MacroCard."""
    template = copy.deepcopy(template) if template is not None else default_level_template()
    if not any(c["componentData"]["name"] == "name" for c in template["componentsData"]):
        caption = next(c for c in default_level_template()["componentsData"] if c["componentData"]["name"] == "name")
        template["componentsData"].append(caption)
    level = level_definition(template, skins_dir, rects or DEFAULT_MACRO_DIAL_RECTS)
    for state in ("focussed", "unfocussed"):
        level["backgroundData"][state].update(colour=_style_argb("ink.black", 0), image="")
    for c in level["componentsData"]:
        name = c["componentData"]["name"]
        if name in ("name", "value"):
            rect = NAME_RECT if name == "name" else VALUE_RECT
            c["bounds"]["bounds"] = "%d %d %d %d" % rect
            style = c["componentData"]["data"]["textStyle"]
            style["justification"] = "horizontallyCentred verticallyCentred"
            style["font"]["height"] = _style_size("macro_card.sub_dial_definition.style.font.height"
                                                  if name == "name" else "macro_card.sub_dial_definition.value")
    if not include_name:
        level["componentsData"] = [c for c in level["componentsData"] if c["componentData"]["name"] != "name"]
    return level


TARGET_X = TARGET_RECT[0]
TARGET_SLOT_WIDTH = TARGET_RECT[2]


def build_macro_card_definition(skins_dir, action_text="NO TARGET", *, level_type="macroSubLevel"):
    """Build the card entry. Use build_macro_card_definitions to export the complete standalone skin definitions."""
    level = entry(
        "level",
        level_type,
        {"version": 1, "handleName": "Data"},
        DIAL_RECT,
    )
    target = action_button(
        SimpleNamespace(skins_dir=Path(skins_dir)),
        "target",
        TARGET_RECT,
        "Target",
        text=action_text,
        width=None,
    )
    background = {"version": 1, "colour": _style_argb("ink.black", 0), "image": ""}
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
