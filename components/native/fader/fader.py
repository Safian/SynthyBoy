# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native Calibrated Fader component, definitions, and scale math.

Conforms strictly to Auto MPC SDK Section 6.1 Slider:
- sliderType: 'TrackAndThumb'
- direction: 'Vertical'
- thumbTrackVisible: True
- thumbImage: 'mixer_fader_cap.png'
- Dynamic value label is placed strictly BENEATH the physical slider track travel,
  guaranteeing that the fader thumb never obscures the readout.
- Three Calibrated Scale Contracts:
  1. SCALE_PERCENT: 0% to 100% linear (Sources: SUB, A, B, C, NOISE), unity at 100%.
  2. SCALE_LINEAR_DB: -60 dB to +12 dB linear (Filters: F1, F2), unity at 0 dB (norm ~0.8333).
  3. SCALE_SKEW_DB: -60 dB to +6 dB with exp skew 2.5 (Bus & Master), unity at 0 dB (norm ~0.7885).
"""

import copy
import math
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

from components.style import (
    argb as _style_argb,
    color as _style_color,
    rgb as _style_rgb,
    rgba as _style_rgba,
    size as _style_size,
)
from components.native.button.button import semibold

SCALE_PERCENT = "percent"
SCALE_LINEAR_DB = "linear_db"
SCALE_SKEW_DB = "skew_db"

FADER_CAP_ASSET = "mixer_fader_cap.png"

# Normalization & Unity reference constants
def unity_normalized(scale_mode: str) -> float:
    """Returns the normalized position (0.0 .. 1.0) of unity / 0dB."""
    if scale_mode == SCALE_PERCENT:
        return 1.0
    elif scale_mode == SCALE_LINEAR_DB:
        # 0 dB in -60 .. +12 dB range
        return (0.0 - (-60.0)) / (12.0 - (-60.0))  # 60 / 72 ≈ 0.8333
    elif scale_mode == SCALE_SKEW_DB:
        # 0 dB in -60 .. +6 dB with skew 2.5:
        # linear fraction = (0 - (-60)) / (6 - (-60)) = 60 / 66 ≈ 0.9091
        # with exp_skew 2.5: norm = 0.9091 ^ 2.5 ≈ 0.7891
        lin = 60.0 / 66.0
        return math.pow(lin, 2.5)
    return 1.0


def format_fader_value(raw: float, scale_mode: str) -> str:
    """Formats raw parameter value for display beneath fader."""
    if scale_mode == SCALE_PERCENT:
        return f"{int(round(raw))}%"
    elif scale_mode in (SCALE_LINEAR_DB, SCALE_SKEW_DB):
        if raw <= -59.5:
            return "-inf"
        return f"{raw:+.1f} dB" if raw != 0.0 else "0.0 dB"
    return f"{raw:.1f}"


def render_fader_cap(
    width: Optional[int] = None,
    height: Optional[int] = None,
    radius: Optional[int] = None,
    notch_width: Optional[int] = None,
    notch_height: Optional[int] = None,
) -> Any:
    """Renders hardware-style textured fader cap PNG using central style tokens."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_fader_cap")

    w = _style_size("mixer_strip.fader.cap_width") if width is None else width
    h = _style_size("mixer_strip.fader.cap_height") if height is None else height
    r = _style_size("mixer_strip.fader.cap_radius") if radius is None else radius
    outline_th = _style_size("mixer_strip.fader.cap_outline")
    nw = _style_size("mixer_strip.fader.cap_notch_width") if notch_width is None else notch_width
    nh = _style_size("mixer_strip.fader.cap_notch_height") if notch_height is None else notch_height

    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pen = ImageDraw.Draw(image)

    bg = _style_rgb("fader.cap.surface")
    border = _style_rgb("fader.cap.border")
    notch = _style_rgb("fader.cap.notch")

    pen.rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=bg, outline=border, width=outline_th)
    nx0 = (w - nw) // 2
    ny0 = (h - nh) // 2
    pen.rectangle([nx0, ny0, nx0 + nw - 1, ny0 + nh - 1], fill=notch)
    return image


def write_fader_assets(directory: Path) -> None:
    """Writes the fader cap PNG into directory."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    render_fader_cap().save(directory / FADER_CAP_ASSET, format="PNG")


def build_fader_definition(
    def_name: str,
    height: int,
    scale_mode: str = SCALE_PERCENT,
) -> Dict[str, Any]:
    """Produces the SDK Composite definition for a vertical TrackAndThumb fader slider."""
    width = _style_size("mixer_strip.fader.width")
    is_source = height <= _style_size("mixer_strip.fader.height_source")
    track_h = _style_size(
        "mixer_strip.fader.track_height_source" if is_source else "mixer_strip.fader.track_height_bus"
    )
    val_token = (
        "mixer_strip.rect.slider_value_v_source" if is_source else "mixer_strip.rect.slider_value_v_bus"
    )
    value_rect = tuple(_style_size(val_token))

    clear = {"version": 1, "colour": _style_argb("ink.black", 0), "image": ""}

    slider_data = {
        "version": 3,
        "sliderType": "TrackAndThumb",
        "direction": "Vertical",
        "thumbImage": FADER_CAP_ASSET,
        "thumbTrackVisible": True,
        "revealedImage": "",
        "handleName": "Data",
    }

    slider_entry = {
        "version": 2,
        "componentData": {"version": 1, "name": "Slider", "type": "Slider", "data": slider_data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "Yes",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": f"0 0 {width} {track_h}",
        },
    }

    value_data = {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {
                "version": 1,
                "name": _style_size("font.family"),
                "style": _style_size("font.style.semibold"),
                "height": float(_style_size("mixer_strip.font.value")),
            },
            "colour": _style_argb("text.value"),
            "justification": "horizontallyCentred verticallyCentred",
            "case": "Original",
        },
        "type": "Value",
        "handleName": "Data",
    }

    value_entry = {
        "version": 2,
        "componentData": {"version": 1, "name": "Value", "type": "Label", "data": value_data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": f"{value_rect[0]} {value_rect[1]} {value_rect[2]} {value_rect[3]}",
        },
    }

    return {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": [slider_entry, value_entry],
    }


def create_fader_instance(
    name: str,
    def_name: str,
    rect: Tuple[int, int, int, int],
    param_handle: Union[int, str],
    gates: Sequence[str] = (),
    focus: str = "Yes",
) -> Dict[str, Any]:
    """Instantiates a calibrated fader composite with handle remapping."""
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % rect,
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)

    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": def_name, "data": {}},
        "handle remapping": {
            "version": 1,
            "map": [{"key": "Data", "value": f"Parameter {param_handle}"}],
        },
        "bounds": bounds,
    }


def render_fader_scale(
    draw: Any,
    x: int,
    y: int,
    width: int,
    height: int,
    scale_mode: str,
    font: Optional[Any] = None,
) -> None:
    """Draws static scale ticks and dB/percent markings beside fader track."""
    track_top = y + _style_size("mixer_strip.fader.scale_pad_top")
    track_bottom = y + height - _style_size("mixer_strip.fader.scale_pad_bottom")
    travel = track_bottom - track_top

    # Scale definition ticks
    if scale_mode == SCALE_PERCENT:
        marks = [(1.0, "100%", True), (0.75, "75", False), (0.5, "50", False), (0.25, "25", False), (0.0, "0", False)]
    elif scale_mode == SCALE_LINEAR_DB:
        marks = [(1.0, "+12", False), (60.0 / 72.0, "0", True), (48.0 / 72.0, "-12", False), (36.0 / 72.0, "-24", False), (0.0, "-60", False)]
    else:  # SCALE_SKEW_DB
        unity_n = unity_normalized(SCALE_SKEW_DB)
        marks = [(1.0, "+6", False), (unity_n, "0", True), (unity_n * 0.7, "-12", False), (unity_n * 0.45, "-24", False), (0.0, "-60", False)]

    tick_x0 = x + width - 12
    tick_x1 = x + width - 4
    for norm, label, is_unity in marks:
        ty = int(round(track_bottom - norm * travel))
        tick_col = _style_rgb("accent.cyan") if is_unity else _style_rgb("button.border")
        draw.line([(tick_x0, ty), (tick_x1, ty)], fill=tick_col, width=2 if is_unity else 1)
        if font and label:
            text_col = _style_rgb("text.value") if is_unity else _style_rgb("preview.annotation")
            draw.text((tick_x0 - 3, ty), label, font=font, fill=text_col, anchor="rm")


__all__ = [
    "SCALE_PERCENT",
    "SCALE_LINEAR_DB",
    "SCALE_SKEW_DB",
    "FADER_CAP_ASSET",
    "unity_normalized",
    "format_fader_value",
    "render_fader_cap",
    "write_fader_assets",
    "build_fader_definition",
    "create_fader_instance",
    "render_fader_scale",
]
