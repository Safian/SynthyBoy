# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone Oscillator native TUNE block component, SDK definitions, and tab bindings.

Enforces Owner Decisions and Architecture Specifications:
- Approved Base Layout (1d6a118b):
  * Outer card dimensions: 278 x 154 px (saves 157 px, -50.5% vs original 311 px).
  * 32 px fields: 2x2 grid of OCT, SEM, FIN, CRS (125 x 32 px cells).
  * 4 px vertical and horizontal gaps between fields (0 px hitbox overlap, >= 4 px physical deadband).
  * Shared bottom row: TRACK toggle button and MORE action button strictly 48 px (125 x 48 px cells).
- Layout Comparison Support:
  * 48 px reference (278 x 190 px, 6 px field gap) and 40 px compact (278 x 170 px, 4 px field gap)
    remain fully supported via parameterization.
- Clean Component Hierarchy (No Raw Definition Inlining):
  * The parent TUNE component definition contains regular component entries in componentsData,
    referencing sharedNativeNameValue for the 4 fields and Button for TRACK and MORE.
  * Real shared native name-value component:
    - Short parameter name on the left OUTSIDE the frame (#8fa2c0 on #121930 card background).
    - ONLY the host-formatted value is inside the framed box (#12182d fill, #1c2651 border).
    - Touch gesture opens 'fx editor number menu' on the value box.
- Real Numeric Host Parameter IDs (from docs/params.json, no fictitious 'Parameter oscN.*'):
  * Osc A (Osc 1): Transpose=14, FIN=15, CRS=1508, TRACK=1509
  * Osc B (Osc 2): Transpose=38, FIN=39, CRS=1520, TRACK=1521
  * Osc C (Osc 3): Transpose=62, FIN=63, CRS=1532, TRACK=1533
- Transposition Route for OCT and SEM:
  * SEM: maps directly to the semitone Transpose parameter (14, 38, 62).
  * OCT: maps to the AbcOctave setting (base + OSC_ABC_SETTINGS + abc_oct_slot) where:
    Osc A slot=3 (source 0), Osc B slot=21 (source 1), Osc C slot=39 (source 2).
    Preserves signed semitone remainder across octave changes.
- Proven Native OSC_DETAIL MORE Offset (No Fallback 97):
  * Navigation offset is proven from shell-mpc/native_osc_detail.h (kNavigation = kKind + kCells)
    and scripts/mpc-skin-extras/58_osc_detail.py (base + 1 + 3 * count + 0):
    OSC_DETAIL_MORE_OFFSET = 1 + 3 * OSC_DETAIL_CELLS = 97.
  * MORE parameter handle: FromGesture/Parameter {base + OSC_DETAIL + 97}.
  * Strict error handling: If base cannot be resolved from base or param_count,
    a ValueError is raised. There is NEVER a bogus fallback to 97 (which is filter1.formant.x).
- Strict Tab Validation (No Fallback to Osc A):
  * Only ('Osc', 'Osc A', 'Osc B', 'Osc C') are valid oscillator tab names ('Osc' aliases to 'Osc A').
  * Any unknown tab name raises KeyError immediately; never silently defaults to Osc A.
- Real SDK Buttons and Renderer-Derived Assets:
  * TRACK: LED indicator dot button (cyan #45c8f0 when active, #224455 when inactive).
  * MORE: action button triggering native detail page (oscspec::kMore).
  * Renderer functions produce exact 125x48 px PNG assets for skin directories.
- Full Set of Standalone SDK Definitions:
  * build_standalone_osc_tune_definitions() and register_standalone_osc_tune_definitions()
    provide both sharedNativeNameValue and oscNativeTune definitions for registration.
- Strictly NO visible Q-Link numbers, chips, or dedicated number lanes (0 yellow pixels).
"""

import copy
import math
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
BG_CANVAS = _style_color("surface.canvas")
BG_PANEL = _style_color("surface.panel")
BORDER_PANEL = _style_color("panel.border")
BG_CELL = _style_color("field.surface")
BORDER_CELL = _style_color("field.border")
BG_CELL_FOCUS = _style_color("field.focus")
TEXT_LABEL = _style_color("text.label")
TEXT_VALUE = _style_color("field.value")
CYAN_ACCENT = _style_color("tune.accent")
SAVED_BG = _style_color("tune.saved.surface")
SAVED_BORDER = _style_color("tune.saved.border")
SAVED_TEXT = _style_color("tune.saved.text")

# Approved default geometry on 1280 x 628 tab surface (tab origin y=110)
# 278 x 154 px card (1d6a118b approved minimal layout)
STANDALONE_OSC_TUNE_RECT = tuple(_style_size('tune.standalone_osc_tune_rect'))

DEFAULT_CARD_WIDTH = _style_size('tune.width')
DEFAULT_CARD_HEIGHT = _style_size('tune.height')
DEFAULT_COL_WIDTH = _style_size('name_value.width')
DEFAULT_BOX_WIDTH = _style_size('name_value.box.width')
DEFAULT_FIELD_HEIGHT = _style_size('tune.field.height')
DEFAULT_FIELD_GAP = _style_size('gap.field')
DEFAULT_BTN_HEIGHT = _style_size('height.medium')
DEFAULT_BTN_GAP = _style_size('gap.field')

# Host parameter offset constants (from native_ui_params schema)
LEGACY_EXTRAS = 82


def mixer_peek_index(param_count):
    return param_count + LEGACY_EXTRAS


OSC_ABC_SETTINGS = 293
OSC_ABC_SETTINGS_FIELDS = 18
OSC_DETAIL = 631
OSC_DETAIL_CELLS = 32

# Native detail navigation offsets proven from shell-mpc/native_osc_detail.h
# and scripts/mpc-skin-extras/58_osc_detail.py:
# kNavigation = kKind + kCells = 1 + 3 * kCells = 1 + 3 * 32 = 97
OSC_DETAIL_MORE_OFFSET = 1 + 3 * OSC_DETAIL_CELLS  # 97
OSC_DETAIL_BACK_OFFSET = OSC_DETAIL_MORE_OFFSET + 1  # 98
OSC_DETAIL_LOAD_OFFSET = OSC_DETAIL_BACK_OFFSET + 1  # 99

POPUP_NUMBER_MENU = "fx editor number menu"

TRACK_ON_IMAGE = "tune_track_on.png"
TRACK_OFF_IMAGE = "tune_track_off.png"
MORE_UP_IMAGE = "tune_more_up.png"
MORE_DOWN_IMAGE = "tune_more_down.png"

# Exact numeric host parameter IDs from docs/params.json:
# Osc 1 (A): transpose=14, tune=15, pitch_coarse=1508, pitch_track=1509
# Osc 2 (B): transpose=38, tune=39, pitch_coarse=1520, pitch_track=1521
# Osc 3 (C): transpose=62, tune=63, pitch_coarse=1532, pitch_track=1533
OSC_TUNE_PARAM_IDS = {
    "Osc A": {
        "transpose": 14,
        "oct": 14,
        "sem": 14,
        "fin": 15,
        "crs": 1508,
        "track": 1509,
        "abc_oct_slot": 3,
        "source_index": 0,
    },
    "Osc B": {
        "transpose": 38,
        "oct": 38,
        "sem": 38,
        "fin": 39,
        "crs": 1520,
        "track": 1521,
        "abc_oct_slot": OSC_ABC_SETTINGS_FIELDS + 3,  # 18 + 3 = 21
        "source_index": 1,
    },
    "Osc C": {
        "transpose": 62,
        "oct": 62,
        "sem": 62,
        "fin": 63,
        "crs": 1532,
        "track": 1533,
        "abc_oct_slot": 2 * OSC_ABC_SETTINGS_FIELDS + 3,  # 36 + 3 = 39
        "source_index": 2,
    },
}

# Real schema parameter keys corresponding to docs/params.json
OSC_TUNE_PARAM_KEYS = {
    "Osc A": {
        "transpose": "osc1.transpose",
        "oct": "osc1.transpose",
        "sem": "osc1.transpose",
        "fin": "osc1.tune",
        "crs": "osc1.pitch_coarse",
        "track": "osc1.pitch_track",
    },
    "Osc B": {
        "transpose": "osc2.transpose",
        "oct": "osc2.transpose",
        "sem": "osc2.transpose",
        "fin": "osc2.tune",
        "crs": "osc2.pitch_coarse",
        "track": "osc2.pitch_track",
    },
    "Osc C": {
        "transpose": "osc3.transpose",
        "oct": "osc3.transpose",
        "sem": "osc3.transpose",
        "fin": "osc3.tune",
        "crs": "osc3.pitch_coarse",
        "track": "osc3.pitch_track",
    },
}

VALID_OSC_TABS = ("Osc", "Osc A", "Osc B", "Osc C")

HEIGHT_PRESETS = {_style_size(height): {key: _style_size(token) for key, token in preset.items()}
                  for height, preset in _style_size('name_value.presets').items()}


def _get_font(size):
    font = Path(__file__).with_name("img") / _style_size('font.file.semibold')
    return ImageFont.truetype(str(font), size)


def get_height_preset(height):
    """Return the calibrated styling preset for the given height."""
    if height in HEIGHT_PRESETS:
        return HEIGHT_PRESETS[height]
    if height >= _style_size('tune.get_height_preset.geometry'):
        return HEIGHT_PRESETS[_style_size('height.medium')]
    elif height >= _style_size('tune.get_height_preset.geometry.offset1'):
        return HEIGHT_PRESETS[_style_size('height.intermediate')]
    else:
        return HEIGHT_PRESETS[_style_size('height.small')]


def render_name_value_box_face(width=DEFAULT_BOX_WIDTH, height=DEFAULT_FIELD_HEIGHT, *, radius=None, scale=_style_size('tune.name_value_box_face.geometry')):
    """Render antialiased background cell face for the number box."""
    if radius is None:
        radius = get_height_preset(height)["radius"]
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    pad = scale // 2
    draw.rounded_rectangle(
        (pad, pad, w - pad - _style_size('tune.name_value_box_face.rounded_rectangle'), h - pad - _style_size('tune.name_value_box_face.rounded_rectangle')),
        radius=radius * scale,
        fill=BG_CELL,
        outline=BORDER_CELL,
        width=scale,
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


def render_name_value_preview(
    name,
    value,
    width=DEFAULT_COL_WIDTH,
    height=DEFAULT_FIELD_HEIGHT,
    *,
    box_width=DEFAULT_BOX_WIDTH,
    radius=None,
    scale=_style_size('tune.name_value_preview.geometry'),
    pad_x=None,
    font_name=None,
    font_value=None,
    bg_canvas=None,
):
    """Render the real native name-value component visual with given name and value."""
    preset = get_height_preset(height)
    r = radius if radius is not None else preset["radius"]
    px = pad_x if pad_x is not None else preset["pad_x"]

    w, h = width * scale, height * scale
    bw = min(box_width, width - _style_size('tune.name_value_preview.bw')) * scale
    bx = w - bw

    if bg_canvas:
        im = Image.new("RGBA", (w, h), bg_canvas)
    else:
        im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    pad = scale // 2
    draw.rounded_rectangle(
        (bx + pad, pad, w - pad - _style_size('tune.name_value_preview.rounded_rectangle'), h - pad - _style_size('tune.name_value_preview.rounded_rectangle')),
        radius=r * scale,
        fill=BG_CELL,
        outline=BORDER_CELL,
        width=scale,
    )

    fn = font_name or _get_font(int(preset["label_size"] * scale))
    fv = font_value or _get_font(int(preset["value_size"] * scale))

    draw.text((px * scale, h / 2), str(name), fill=TEXT_LABEL, font=fn, anchor="lm")
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
    rect=(0, 0, DEFAULT_COL_WIDTH, DEFAULT_FIELD_HEIGHT),
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
    """Build the Akai MPC native component definition dictionary for a name-value field."""
    w, h = rect[2], rect[3]
    preset = get_height_preset(h)

    r = radius if radius is not None else preset["radius"]
    px = pad_x if pad_x is not None else preset["pad_x"]
    lbl_sz = label_size if label_size is not None else preset["label_size"]
    val_sz = value_size if value_size is not None else preset["value_size"]

    bw = min(box_width, w - _style_size('tune.build_name_value_definition.bw'))
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

    bg_unfocused = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    bg_focused = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}

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
                "backgroundInset": _style_size('tune.build_name_value_definition.focus.ring'),
                "outlineThickness": _style_size('tune.build_name_value_definition.focus.ring.offset1'),
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": dict(_bounds((bx, by, bw, bh)), whenVisible="WhenFocussed"),
    }

    children = [focus_ring]

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
    """Instantiate a native name-value component entry."""
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


def _button_entry(name, kind, data, rect, remaps=(), gates=(), focus="Yes"):
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
    return _button_entry(
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


def resolve_native_base(base=None, param_count=None):
    """Resolve the MPC handle base from the host configuration supplied by the caller."""
    if base is not None:
        return base
    if param_count is not None:
        return mixer_peek_index(param_count)
    raise ValueError("Supply the native host base or parameter count to bind TUNE.")


def render_tune_track_button(active=True, width=DEFAULT_COL_WIDTH, height=DEFAULT_BTN_HEIGHT, scale=_style_size('tune.tune_track_button.geometry')):
    """Render the TRACK toggle button face image with cyan LED indicator dot."""
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    pad = scale // 2
    draw.rounded_rectangle(
        (pad, pad, w - pad - _style_size('tune.tune_track_button.rounded_rectangle'), h - pad - _style_size('tune.tune_track_button.rounded_rectangle')),
        radius=_style_size('radius.field') * scale,
        fill=BG_CELL,
        outline=BORDER_CELL,
        width=scale,
    )

    dot_r = _style_size('tune.tune_track_button.dot.r') * scale
    dot_cx = _style_size('tune.tune_track_button.dot.cx') * scale
    dot_cy = h // 2
    dot_col = CYAN_ACCENT if active else _style_color("tune.track.off")
    draw.ellipse(
        (dot_cx - dot_r, dot_cy - dot_r, dot_cx + dot_r, dot_cy + dot_r),
        fill=dot_col,
    )

    font_btn = _get_font(int(_style_size('font.label') * scale))
    draw.text(
        (dot_cx + dot_r + _style_size('tune.tune_track_button.text') * scale, dot_cy),
        "TRACK",
        fill=TEXT_VALUE,
        font=font_btn,
        anchor="lm",
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


def render_tune_more_button(pressed=False, width=DEFAULT_COL_WIDTH, height=DEFAULT_BTN_HEIGHT, scale=_style_size('tune.tune_more_button.geometry')):
    """Render the MORE action button face image."""
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    pad = scale // 2
    fill_col = _style_color("field.focus") if pressed else BG_CELL
    border_col = CYAN_ACCENT if pressed else BORDER_CELL
    text_col = _style_color("text.white") if pressed else TEXT_VALUE

    draw.rounded_rectangle(
        (pad, pad, w - pad - _style_size('tune.tune_more_button.rounded_rectangle'), h - pad - _style_size('tune.tune_more_button.rounded_rectangle')),
        radius=_style_size('radius.field') * scale,
        fill=fill_col,
        outline=border_col,
        width=scale,
    )

    font_btn = _get_font(int(_style_size('font.label') * scale))
    draw.text(
        (w // 2, h // 2),
        "MORE",
        fill=text_col,
        font=font_btn,
        anchor="mm",
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


def write_tune_assets(skins_dir, field_h=DEFAULT_FIELD_HEIGHT, box_width=DEFAULT_BOX_WIDTH):
    """Write all rendered button and number box PNG assets into the skin directory."""
    path = Path(skins_dir)
    path.mkdir(parents=True, exist_ok=True)

    preset = get_height_preset(field_h)
    box_im = render_name_value_box_face(box_width, field_h, radius=preset["radius"])
    box_im.save(path / f"name_value_box_{box_width}x{field_h}.png")

    _write_tune_button_assets(path)


def _write_tune_button_assets(path):
    path = Path(path)
    path.mkdir(parents=True, exist_ok=True)
    track_on = render_tune_track_button(active=True)
    track_on.save(path / TRACK_ON_IMAGE)

    track_off = render_tune_track_button(active=False)
    track_off.save(path / TRACK_OFF_IMAGE)

    more_up = render_tune_more_button(pressed=False)
    more_up.save(path / MORE_UP_IMAGE)

    more_down = render_tune_more_button(pressed=True)
    more_down.save(path / MORE_DOWN_IMAGE)


def _header_label(name, text, rect):
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
                        "height": _style_size('font.small'),
                    },
                    "colour": _style_argb("text.label"),
                    "justification": "left verticallyCentred",
                    "case": "Original",
                },
                "type": "Text",
                "handleName": "Invalid",
                "text": text,
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds(rect, focus="No"),
    }


def build_standalone_osc_tune_definition(
    component_name="oscNativeTune",
    rect=None,
    skins_dir=None,
    *,
    field_h=DEFAULT_FIELD_HEIGHT,
    field_gap=DEFAULT_FIELD_GAP,
    btn_gap=None,
    btn_h=DEFAULT_BTN_HEIGHT,
    name_value_kind="sharedNativeNameValue",
    with_face_art=True,
    with_button_art=True,
):
    """Build the Akai MPC native component definition dictionary for the compact TUNE block."""
    col_w = DEFAULT_COL_WIDTH
    gap_x = _style_size('tune.build_standalone_osc_tune_definition.gap.x')
    gap_y = field_gap
    gap_b = field_gap if btn_gap is None else btn_gap

    x0 = _style_size('tune.build_standalone_osc_tune_definition.x0')
    x1 = x0 + col_w + gap_x

    y_r1 = _style_size('tune.build_standalone_osc_tune_definition.y.r1')
    y_r2 = y_r1 + field_h + gap_y
    y_r3 = y_r2 + field_h + gap_b

    card_h = y_r3 + btn_h + _style_size('tune.build_standalone_osc_tune_definition.card.h')
    if rect is None:
        rect = (*STANDALONE_OSC_TUNE_RECT[:2], DEFAULT_CARD_WIDTH, card_h)

    bg_unfocused = {"version": 1, "colour": _style_argb("surface.panel"), "image": ""}
    bg_focused = {"version": 1, "colour": _style_argb("surface.panel"), "image": ""}

    if skins_dir:
        write_tune_assets(skins_dir, field_h=field_h, box_width=DEFAULT_BOX_WIDTH)

    children = []

    # 1. Header label "TUNE"
    children.append(_header_label("HeaderTune", "TUNE", (_style_size('tune.build_standalone_osc_tune_definition._header_label'), _style_size('tune.build_standalone_osc_tune_definition._header_label.offset1'), _style_size('tune.build_standalone_osc_tune_definition._header_label.offset2'), _style_size('tune.build_standalone_osc_tune_definition._header_label.offset3'))))

    # 2. 2x2 Grid of regular native name-value child entries
    children.append(
        create_name_value_entry(
            name="tune_oct",
            kind=name_value_kind,
            rect=(x0, y_r1, col_w, field_h),
            data_handle="Data_Oct",
            caption_handle="Caption_Oct",
            focus="Yes",
        )
    )
    children.append(
        create_name_value_entry(
            name="tune_sem",
            kind=name_value_kind,
            rect=(x1, y_r1, col_w, field_h),
            data_handle="Data_Sem",
            caption_handle="Caption_Sem",
            focus="Yes",
        )
    )
    children.append(
        create_name_value_entry(
            name="tune_fin",
            kind=name_value_kind,
            rect=(x0, y_r2, col_w, field_h),
            data_handle="Data_Fin",
            caption_handle="Caption_Fin",
            focus="Yes",
        )
    )
    children.append(
        create_name_value_entry(
            name="tune_crs",
            kind=name_value_kind,
            rect=(x1, y_r2, col_w, field_h),
            data_handle="Data_Crs",
            caption_handle="Caption_Crs",
            focus="Yes",
        )
    )

    # 3. Row 3: TRACK button and MORE button (strictly 48 px high)
    on_img = TRACK_ON_IMAGE if with_button_art else None
    off_img = TRACK_OFF_IMAGE if with_button_art else None
    children.append(
        button(
            "tune_track",
            (x0, y_r3, col_w, btn_h),
            on_img or "",
            off_img or "",
            remaps=(("Data", "Data_Track"),),
        )
    )
    children.append(
        button(
            "tune_more",
            (x1, y_r3, col_w, btn_h),
            MORE_DOWN_IMAGE if with_button_art else "",
            MORE_UP_IMAGE if with_button_art else "",
            remaps=(("Data", "Data_More"),),
        )
    )

    return {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": bg_focused,
            "unfocussed": bg_unfocused,
        },
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": children,
    }


def build_standalone_osc_tune_definitions(
    component_name="oscNativeTune",
    name_value_kind="sharedNativeNameValue",
    skins_dir=None,
    *,
    field_h=DEFAULT_FIELD_HEIGHT,
    field_gap=DEFAULT_FIELD_GAP,
    btn_gap=None,
    btn_h=DEFAULT_BTN_HEIGHT,
    box_width=DEFAULT_BOX_WIDTH,
    with_face_art=True,
    with_button_art=True,
):
    """Build the complete, self-contained set of SDK component definitions required for the TUNE block."""
    defs = {}
    if skins_dir:
        _write_tune_button_assets(skins_dir)

    nv_def = build_name_value_definition(
        name=name_value_kind,
        rect=(0, 0, DEFAULT_COL_WIDTH, field_h),
        skins_dir=skins_dir,
        box_width=box_width,
        handle="Data",
        caption_handle="Caption",
        with_face_art=with_face_art,
    )
    defs[name_value_kind] = nv_def

    tune_def = build_standalone_osc_tune_definition(
        component_name=component_name,
        skins_dir=None,
        field_h=field_h,
        field_gap=field_gap,
        btn_gap=btn_gap,
        btn_h=btn_h,
        name_value_kind=name_value_kind,
        with_face_art=with_face_art,
        with_button_art=with_button_art,
    )
    defs[component_name] = tune_def
    return defs


def register_standalone_osc_tune_definitions(
    ctx,
    component_name="oscNativeTune",
    name_value_kind="sharedNativeNameValue",
    *,
    field_h=DEFAULT_FIELD_HEIGHT,
    field_gap=DEFAULT_FIELD_GAP,
    btn_gap=None,
    btn_h=DEFAULT_BTN_HEIGHT,
    box_width=DEFAULT_BOX_WIDTH,
    with_face_art=True,
    with_button_art=True,
):
    """Register all required TUNE SDK definitions into an MPC skin extras context."""
    skins_dir = getattr(ctx, "skins_dir", None)
    defs = build_standalone_osc_tune_definitions(
        component_name=component_name,
        name_value_kind=name_value_kind,
        skins_dir=skins_dir,
        field_h=field_h,
        field_gap=field_gap,
        btn_gap=btn_gap,
        btn_h=btn_h,
        box_width=box_width,
        with_face_art=with_face_art,
        with_button_art=with_button_art,
    )
    for name, definition in defs.items():
        ctx.add_component_definition(name, definition)
    return defs


def build_standalone_osc_tune(
    tab_name,
    rect=STANDALONE_OSC_TUNE_RECT,
    component_type="oscNativeTune",
    name="Osc Native Tune",
    focus="Children",
    *,
    base=None,
    param_count=None,
    gates=(),
):
    """Instantiate a native OSC TUNE component entry for placement on an MPC tab."""
    if tab_name not in VALID_OSC_TABS:
        raise KeyError(
            f"Unknown oscillator tab name: {tab_name!r}. Expected one of {VALID_OSC_TABS}; "
            "refusing fallback to Osc A."
        )

    key_tab = "Osc A" if tab_name == "Osc" else tab_name
    spec = OSC_TUNE_PARAM_IDS[key_tab]

    transpose_id = spec["transpose"]
    fin_id = spec["fin"]
    crs_id = spec["crs"]
    track_id = spec["track"]

    resolved_base = resolve_native_base(base=base, param_count=param_count)

    oct_id = resolved_base + OSC_ABC_SETTINGS + spec["abc_oct_slot"]
    more_id = resolved_base + OSC_DETAIL + OSC_DETAIL_MORE_OFFSET

    sem_id = transpose_id

    handle_map = [
        {"key": "Data_Oct", "value": f"Parameter {oct_id}"},
        {"key": "Caption_Oct", "value": f"WithName[OCT]/Parameter {oct_id}"},
        {"key": "Data_Sem", "value": f"Parameter {sem_id}"},
        {"key": "Caption_Sem", "value": f"WithName[SEM]/Parameter {sem_id}"},
        {"key": "Data_Fin", "value": f"Parameter {fin_id}"},
        {"key": "Caption_Fin", "value": f"WithName[FIN]/Parameter {fin_id}"},
        {"key": "Data_Crs", "value": f"Parameter {crs_id}"},
        {"key": "Caption_Crs", "value": f"WithName[CRS]/Parameter {crs_id}"},
        {"key": "Data_Track", "value": f"Parameter {track_id}"},
        {"key": "Data_More", "value": f"FromGesture/Parameter {more_id}"},
    ]

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
            "type": component_type,
            "data": {"version": 1, "handleName": "Data"},
        },
        "handle remapping": {"version": 1, "map": handle_map},
        "bounds": bounds,
    }


def render_tune_card(
    values,
    track_active=True,
    q_style="none",
    track_framed=True,
    scale=_style_size('tune.tune_card.geometry'),
    field_h=_style_size('tune.tune_card.geometry.offset1'),
    field_gap=_style_size('tune.tune_card.geometry.offset2'),
    btn_gap=None,
    btn_h=_style_size('tune.tune_card.geometry.offset3'),
):
    """Render a compact OSC TUNE card using real native name-value components."""
    col_w = _style_size('tune.tune_card.col.w') * scale
    gap_x = _style_size('tune.tune_card.gap.x') * scale
    box_w = _style_size('tune.tune_card.box.w') * scale

    gap_y = field_gap * scale
    gap_b = (field_gap if btn_gap is None else btn_gap) * scale
    field_h_s = field_h * scale
    btn_h_s = btn_h * scale

    top_pad = _style_size('tune.tune_card.top.pad') * scale
    bottom_pad = _style_size('tune.tune_card.bottom.pad') * scale

    y_r1 = top_pad
    y_r2 = y_r1 + field_h_s + gap_y
    y_r3 = y_r2 + field_h_s + gap_b

    card_w = _style_size('tune.tune_card.card.w') * scale
    card_h = y_r3 + btn_h_s + bottom_pad

    im = Image.new("RGBA", (card_w, card_h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(im)

    draw.rounded_rectangle(
        (0, 0, card_w - _style_size('tune.tune_card.rounded_rectangle'), card_h - _style_size('tune.tune_card.rounded_rectangle')),
        radius=_style_size('radius.panel') * scale,
        fill=BG_PANEL,
        outline=BORDER_PANEL,
        width=scale,
    )

    font_header = _get_font(_style_size('font.small') * scale)
    draw.text((_style_size('tune.tune_card.text') * scale, _style_size('tune.tune_card.text.offset1') * scale), "TUNE", fill=TEXT_LABEL, font=font_header)

    font_btn = _get_font(_style_size('font.label') * scale)

    x0 = _style_size('tune.tune_card.x0') * scale
    x1 = x0 + col_w + gap_x

    preset = get_height_preset(field_h)

    def draw_cell(x, y, name, val):
        comp_im = render_name_value_preview(
            name,
            val,
            width=_style_size('tune.draw_cell.comp.im.offset1') * scale,
            height=field_h_s,
            box_width=box_w,
            radius=preset["radius"] * scale,
            pad_x=preset["pad_x"] * scale,
            scale=_style_size('tune.draw_cell.comp.im'),
            font_name=_get_font(int(preset["label_size"] * scale)),
            font_value=_get_font(int(preset["value_size"] * scale)),
        )
        im.paste(comp_im, (x, y), comp_im)

    draw_cell(x0, y_r1, "OCT", values["oct"])
    draw_cell(x1, y_r1, "SEM", values["sem"])
    draw_cell(x0, y_r2, "FIN", values["fin"])
    draw_cell(x1, y_r2, "CRS", values["crs"])

    if track_framed:
        draw.rounded_rectangle(
            (x0, y_r3, x0 + col_w - _style_size('tune.tune_card.rounded_rectangle'), y_r3 + btn_h_s - _style_size('tune.tune_card.rounded_rectangle')),
            radius=_style_size('radius.field') * scale,
            fill=BG_CELL,
            outline=BORDER_CELL,
            width=scale,
        )
    dot_r = _style_size('tune.tune_card.dot.r') * scale
    dot_cx = x0 + _style_size('tune.tune_card.dot.cx') * scale
    dot_cy = y_r3 + btn_h_s // 2
    dot_col = CYAN_ACCENT if track_active else _style_color("tune.track.off")
    draw.ellipse((dot_cx - dot_r, dot_cy - dot_r, dot_cx + dot_r, dot_cy + dot_r), fill=dot_col)
    draw.text((dot_cx + dot_r + _style_size('tune.tune_card.text.offset2') * scale, dot_cy), "TRACK", fill=TEXT_VALUE, font=font_btn, anchor="lm")

    draw.rounded_rectangle(
        (x1, y_r3, x1 + col_w - _style_size('tune.tune_card.rounded_rectangle'), y_r3 + btn_h_s - _style_size('tune.tune_card.rounded_rectangle')),
        radius=_style_size('radius.field') * scale,
        fill=BG_CELL,
        outline=BORDER_CELL,
        width=scale,
    )

    gx = x1 + _style_size('tune.tune_card.gx') * scale
    gy = y_r3 + btn_h_s // 2
    r_out = _style_size('tune.tune_card.r.out') * scale
    r_in = _style_size('tune.tune_card.r.in') * scale
    r_hole = _style_size('tune.tune_card.r.hole') * scale
    for i in range(8):
        ang = i * (math.pi / 4)
        tx = gx + r_out * math.cos(ang)
        ty = gy + r_out * math.sin(ang)
        draw.line([(gx, gy), (tx, ty)], fill=CYAN_ACCENT, width=round(_style_size('tune.tune_card.round') * scale))
    draw.ellipse((gx - r_in, gy - r_in, gx + r_in, gy + r_in), fill=CYAN_ACCENT)
    draw.ellipse((gx - r_hole, gy - r_hole, gx + r_hole, gy + r_hole), fill=BG_CELL)

    draw.text((gx + r_out + _style_size('tune.tune_card.text.offset3') * scale, y_r3 + btn_h_s // 2), "MORE", fill=TEXT_VALUE, font=font_btn, anchor="lm")

    target_h = card_h // scale
    return im.resize((_style_size('tune.tune_card.resize'), target_h), Image.Resampling.LANCZOS)


__all__ = [
    "DEFAULT_BOX_WIDTH",
    "DEFAULT_BTN_GAP",
    "DEFAULT_BTN_HEIGHT",
    "DEFAULT_CARD_HEIGHT",
    "DEFAULT_CARD_WIDTH",
    "DEFAULT_COL_WIDTH",
    "DEFAULT_FIELD_GAP",
    "DEFAULT_FIELD_HEIGHT",
    "MORE_DOWN_IMAGE",
    "MORE_UP_IMAGE",
    "OSC_DETAIL_BACK_OFFSET",
    "OSC_DETAIL_LOAD_OFFSET",
    "OSC_DETAIL_MORE_OFFSET",
    "OSC_TUNE_PARAM_IDS",
    "OSC_TUNE_PARAM_KEYS",
    "POPUP_NUMBER_MENU",
    "STANDALONE_OSC_TUNE_RECT",
    "TRACK_OFF_IMAGE",
    "TRACK_ON_IMAGE",
    "VALID_OSC_TABS",
    "build_standalone_osc_tune",
    "build_standalone_osc_tune_definition",
    "build_standalone_osc_tune_definitions",
    "register_standalone_osc_tune_definitions",
    "render_tune_card",
    "render_tune_more_button",
    "render_tune_track_button",
    "resolve_native_base",
    "write_tune_assets",
]
