# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native SoloMutePair compound component and multi-state modeling.

Implements the dedicated, reusable host-native compound S/M button pair for
Akai MPC hardware (Key 61 and XL) per owner feedback:
- Width 115 px supports two 56x56 px touch targets with 3 px spacing:
  Solo (S): x=0, y=0, w=56, h=56
  Mute (M): x=59, y=0, w=56, h=56
- Multi-state modeling:
  1. NEUTRAL: normal unselected/inactive state (dark surface, subtle border, white/light text)
  2. EXPLICIT_SOLO: channel solo selected (accent cyan/bright active state, bold 'S')
  3. EXPLICIT_MUTE: channel mute selected (accent red active state, bold 'M')
  4. IMPLICIT_SOLO_MUTE: channel muted by another channel's solo (muted outline and text,
     distinct from user mute, does NOT mutate DSP mute parameter or preset state)
  5. DISABLED: channel source OFF or module inactive (dim greyed out control)
  6. PRESSED: transient touch feedback
- Master Variant:
  No misleading dead 'S' button; composed of honest MAIN FX Enable button (56x56 px)
  and Master Mute (56x56 px).
- Gating and State Integrity:
  Component consumes parameter truth and derived effective states from engine/owner.
  Does NOT hold authoritative state or compute routing graphs internally.
"""

import copy
from pathlib import Path
from typing import Any, Dict, Optional, Sequence, Tuple, Union

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

from components.native.button.button import gradient_face
from components.style import (
    argb as _style_argb,
    control_look as _control_look,
    rgb as _style_rgb,
    rgba as _style_rgba,
    size as _style_size,
)
from components.native.button.button import TRANSPARENT_HIT as ASSET_HIT, semibold, write_transparent_hit

# Component state constants (indices 0..4 align 1:1 with FilmStrip frames & mixer_visual ABI)
STATE_NEUTRAL = "neutral"
STATE_SOLO = "solo"
STATE_MUTE = "mute"
STATE_IMPLICIT_SOLO_MUTE = "implicit_solo_mute"
STATE_DISABLED = "disabled"
STATE_PRESSED = "pressed"

STATES = (
    STATE_NEUTRAL,
    STATE_SOLO,
    STATE_MUTE,
    STATE_IMPLICIT_SOLO_MUTE,
    STATE_DISABLED,
    STATE_PRESSED,
)

NUM_VISUAL_FRAMES = 5

# Variants
VARIANT_SOURCE = "source"
VARIANT_BUS = "bus"
VARIANT_MASTER = "master"

TYPE_SOLO_MUTE_SOURCE = "soloMutePairSource"
TYPE_SOLO_MUTE_BUS = "soloMutePairBus"
TYPE_SOLO_MUTE_MASTER = "soloMutePairMaster"

# Multi-state FilmStrip asset filenames + transparent 56x56 touch hitbox
ASSET_SOLO_STATES = "solo_mute_s_states.png"
ASSET_MUTE_STATES = "solo_mute_m_states.png"
ASSET_FX_STATES = "solo_mute_fx_states.png"

ALL_ASSETS = (
    ASSET_SOLO_STATES,
    ASSET_MUTE_STATES,
    ASSET_FX_STATES,
    ASSET_HIT,
)


def render_button_face(
    text: str,
    state: str,
    width: Optional[int] = None,
    height: Optional[int] = None,
    font_size: Optional[int] = None,
) -> Any:
    """Renders a single button face PNG for the given multi-state using central style tokens."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_button_face")

    w = _style_size("mixer_strip.button.solo_w") if width is None else width
    h = _style_size("mixer_strip.button.solo_h") if height is None else height
    fsz = _style_size("mixer_strip.font.sm") if font_size is None else font_size
    radius = _style_size("radius.field")
    half = 2  # integer center

    if state == STATE_DISABLED:
        from components.style import dim_artwork
        return dim_artwork(render_button_face(text, STATE_NEUTRAL, w, h, font_size=font_size))

    image = Image.new("RGBA", (w, h), _style_rgba("ink.black", 0))
    pen = ImageDraw.Draw(image)
    font = semibold(fsz)

    if state == STATE_NEUTRAL:
        bg = _style_rgb("button.surface")
        border = _style_rgb("button.border")
        fg = _style_rgb("text.icon")
    elif state == STATE_SOLO:
        bg = _style_rgb("button.surface")
        border = _style_rgb("accent.cyan")
        fg = _style_rgb("accent.cyan")
    elif state == STATE_MUTE:
        bg = _style_rgb("button.surface")
        border = _style_rgb("accent.red")
        fg = _style_rgb("accent.red")
    elif state == STATE_IMPLICIT_SOLO_MUTE:
        bg = _style_rgb("button.surface")
        border = _style_rgb("text.muted")
        fg = _style_rgb("text.muted")
    elif state == STATE_DISABLED:
        bg = _style_rgb("button.surface")
        border = _style_rgb("smooth.border.off")
        fg = _style_rgb("smooth.caption.off")
    elif state == STATE_PRESSED:
        bg = _style_rgb("surface.focus")
        border = _style_rgb("accent.cyan")
        fg = _style_rgb("text.white")
    elif state == "fx_on":
        bg = _style_rgb("button.surface")
        border = _style_rgb("dial.value")
        fg = _style_rgb("dial.value")
    else:
        bg = _style_rgb("button.surface")
        border = _style_rgb("button.border")
        fg = _style_rgb("text.icon")

    if _control_look() == "shaded":
        # The shared button gradient (button.gradient.*); the state keyline colour stays, the pressed fill is its own.
        scale = _style_size("sampling.standard")
        fill = (bg, bg) if state == STATE_PRESSED else (_style_rgb("button.gradient.top"), _style_rgb("button.gradient.bottom"))
        if state == STATE_NEUTRAL:
            line = (_style_rgb("button.gradient.border"), _style_rgb("button.gradient.border.bottom"))
        else:
            line = (border, border)
        face, _ = gradient_face((w * scale, h * scale), inset=0, radius=radius * scale, frame=scale,
                                span=_style_size("control.gradient"), fill=fill, line=line)
        image = face.resize((w, h), Image.Resampling.LANCZOS)
        pen = ImageDraw.Draw(image)
    else:
        pen.rounded_rectangle([0, 0, w - 1, h - 1], radius=radius, fill=bg, outline=border, width=1)
    if font:
        pen.text((w // half, h // half), text, font=font, fill=fg, anchor="mm")
    return image


def render_filmstrip_sheet(text: str, active_state: str) -> Any:
    """Renders a 5-frame vertical FilmStrip sheet matching mixer_visual ABI (0..4)."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_filmstrip_sheet")
    w = _style_size("mixer_strip.button.solo_w")
    h = _style_size("mixer_strip.button.solo_h")
    states = (STATE_NEUTRAL, active_state, STATE_IMPLICIT_SOLO_MUTE, STATE_DISABLED, STATE_PRESSED)
    sheet = Image.new("RGBA", (w, h * len(states)), (0, 0, 0, 0))
    for i, st in enumerate(states):
        sheet.alpha_composite(render_button_face(text, st, width=w, height=h), (0, i * h))
    return sheet


def write_solo_mute_assets(directory: Path) -> None:
    """Generates multi-state FilmStrip sheets and transparent touch hitbox asset."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    render_filmstrip_sheet("S", STATE_SOLO).save(directory / ASSET_SOLO_STATES, format="PNG")
    render_filmstrip_sheet("M", STATE_MUTE).save(directory / ASSET_MUTE_STATES, format="PNG")
    render_filmstrip_sheet("FX", "fx_on").save(directory / ASSET_FX_STATES, format="PNG")
    write_transparent_hit(directory)


def derive_channel_presentation_state(
    is_source_on: bool,
    user_solo: bool,
    user_mute: bool,
    any_solo_active: bool,
    is_routing_active: bool,
) -> Tuple[str, str]:
    """Pure functional helper to derive presentation state for Solo and Mute buttons.

    Returns (solo_state, mute_state).
    Does NOT mutate DSP truth or preset parameters.
    - If channel source is OFF: returns STATE_DISABLED for both.
    - If user explicitly muted: mute_state is STATE_MUTE.
    - If user explicitly soloed: solo_state is STATE_SOLO.
    - If another channel is soloed and this channel is not in the active routing graph:
      mute_state is STATE_IMPLICIT_SOLO_MUTE (outline/dim M), without changing writable Mute!
    """
    if not is_source_on:
        return STATE_DISABLED, STATE_DISABLED

    solo_state = STATE_SOLO if user_solo else STATE_NEUTRAL

    if user_mute:
        mute_state = STATE_MUTE
    elif any_solo_active and not is_routing_active:
        mute_state = STATE_IMPLICIT_SOLO_MUTE
    else:
        mute_state = STATE_NEUTRAL

    return solo_state, mute_state


def _filmstrip_entry(name: str, rect: Tuple[int, int, int, int], film_asset: str, param: str) -> Dict[str, Any]:
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Knob",
            "data": {
                "version": 3,
                "knobType": "FilmStrip",
                "filmStrip": film_asset,
                "invert": False,
                "dragOrientation": "None",
                "numFrames": NUM_VISUAL_FRAMES,
                "handleName": "Data",
            },
        },
        "handle remapping": {
            "version": 1,
            "map": [{"key": "Data", "value": param}],
        },
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % rect,
        },
    }


def _btn_entry(name: str, rect: Tuple[int, int, int, int], on_art: str, off_art: str, param: str) -> Dict[str, Any]:
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Button",
            "data": {
                "version": 2,
                "onImage": on_art,
                "offImage": off_art,
                "buttonId": 1,
                "numButtonsInGroup": 1,
                "handleName": "Data",
                "gestureBehaviour": "Instant",
            },
        },
        "handle remapping": {
            "version": 1,
            "map": [{"key": "Data", "value": param}],
        },
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "Yes",
            "showWhenDataModelInvalid": "Hide",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % rect,
        },
    }


def build_solo_mute_definitions(skins_dir: Optional[Path] = None) -> Dict[str, Any]:
    """Produces shared SDK definitions with FilmStrip visuals and 56x56 transparent Button hitboxes."""
    if skins_dir:
        write_solo_mute_assets(skins_dir)

    clear = {"version": 1, "colour": _style_argb("ink.black", 0), "image": ""}
    rect_solo = tuple(_style_size("mixer_strip.rect.solo_local"))
    rect_mute = tuple(_style_size("mixer_strip.rect.mute_local"))
    rect_fx = tuple(_style_size("mixer_strip.rect.fx_enable_local"))

    solo_vis = _filmstrip_entry("Solo Visual", rect_solo, ASSET_SOLO_STATES, "SoloVisual")
    mute_vis = _filmstrip_entry("Mute Visual", rect_mute, ASSET_MUTE_STATES, "MuteVisual")
    solo_btn = _btn_entry("Solo Button", rect_solo, ASSET_HIT, ASSET_HIT, "Solo")
    mute_btn = _btn_entry("Mute Button", rect_mute, ASSET_HIT, ASSET_HIT, "Mute")

    sm_pair_def = {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": [solo_vis, mute_vis, solo_btn, mute_btn],
    }

    fx_vis = _filmstrip_entry("FX Enable Visual", rect_fx, ASSET_FX_STATES, "FxVisual")
    master_mute_vis = _filmstrip_entry("Master Mute Visual", rect_mute, ASSET_MUTE_STATES, "MuteVisual")
    fx_btn = _btn_entry("FX Enable Button", rect_fx, ASSET_HIT, ASSET_HIT, "FxEnable")
    master_mute_btn = _btn_entry("Master Mute Button", rect_mute, ASSET_HIT, ASSET_HIT, "Mute")

    master_pair_def = {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": [fx_vis, master_mute_vis, fx_btn, master_mute_btn],
    }

    return {
        TYPE_SOLO_MUTE_SOURCE: sm_pair_def,
        TYPE_SOLO_MUTE_BUS: copy.deepcopy(sm_pair_def),
        TYPE_SOLO_MUTE_MASTER: master_pair_def,
    }


def create_solo_mute_instance(
    variant: str,
    rect: Tuple[int, int, int, int],
    handles: Dict[str, Union[int, str]],
    gates: Sequence[str] = (),
    focus: str = "Yes",
    name: Optional[str] = None,
) -> Dict[str, Any]:
    """Creates a native instance of the SoloMutePair compound component."""
    def _val(h):
        return f"Parameter {h}" if isinstance(h, int) else str(h)

    if variant == VARIANT_MASTER:
        kind = TYPE_SOLO_MUTE_MASTER
        remaps = [
            {"key": "FxEnable", "value": _val(handles["FxEnable"])},
            {"key": "Mute", "value": _val(handles["Mute"])},
            {"key": "FxVisual", "value": _val(handles["FxVisual"])},
            {"key": "MuteVisual", "value": _val(handles["MuteVisual"])},
        ]
    else:
        kind = TYPE_SOLO_MUTE_SOURCE if variant == VARIANT_SOURCE else TYPE_SOLO_MUTE_BUS
        remaps = [
            {"key": "Solo", "value": _val(handles["Solo"])},
            {"key": "Mute", "value": _val(handles["Mute"])},
            {"key": "SoloVisual", "value": _val(handles["SoloVisual"])},
            {"key": "MuteVisual", "value": _val(handles["MuteVisual"])},
        ]

    bounds: Dict[str, Any] = {
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
        "componentData": {
            "version": 1,
            "name": name or f"SoloMute {variant.capitalize()}",
            "type": kind,
            "data": {},
        },
        "handle remapping": {"version": 1, "map": remaps},
        "bounds": bounds,
    }


__all__ = [
    "STATE_NEUTRAL",
    "STATE_SOLO",
    "STATE_MUTE",
    "STATE_IMPLICIT_SOLO_MUTE",
    "STATE_DISABLED",
    "STATE_PRESSED",
    "STATES",
    "VARIANT_SOURCE",
    "VARIANT_BUS",
    "VARIANT_MASTER",
    "TYPE_SOLO_MUTE_SOURCE",
    "TYPE_SOLO_MUTE_BUS",
    "TYPE_SOLO_MUTE_MASTER",
    "NUM_VISUAL_FRAMES",
    "ASSET_SOLO_STATES",
    "ASSET_MUTE_STATES",
    "ASSET_FX_STATES",
    "ASSET_HIT",
    "ALL_ASSETS",
    "render_button_face",
    "render_filmstrip_sheet",
    "write_solo_mute_assets",
    "derive_channel_presentation_state",
    "build_solo_mute_definitions",
    "create_solo_mute_instance",
]
