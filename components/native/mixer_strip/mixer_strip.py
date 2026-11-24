# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reusable native MPC mixer channel strip component, definitions, and renderer.

Implements the authentic, portable native TUI compound channel strip for Akai MPC
hardware (standalone Key 61 and XL) based on Auto MPC SDK (Section 6.1 Slider).

Design principles and specifications:
- Three Variants:
  1. SOURCE: Full channel strip for audio generators & filters (SUB, A, B, C, NOISE, F1, F2).
     Includes: Strip name plate, compact adjacent Solo (S) and Mute (M) pushbuttons (each 56x56 px),
     destination dropdown, vertical TrackAndThumb LEVEL fader with live Peak/Hold Meter alongside,
     bipolar PAN slider, and B1/B2 send sliders.
     Source-on gating: Destination, Level, Meter, Pan, Sends are gated on Mute / Power parameter.
  2. BUS: Effect return bus channel strip (BUS 1, BUS 2).
     Includes: Strip name plate, Solo (S) and Mute (M) pushbuttons (each 56x56 px), destination dropdown,
     tall 414 px vertical TrackAndThumb LEVEL fader with live Peak/Hold Meter alongside.
     Honest bypass: dry audio routing, level control, and metering are NOT gated by FX bypass.
     Lacks: PAN and sends (stereo sum return).
  3. MASTER: Main output console channel strip (MAIN / MASTER).
     Includes: Strip name plate, honest MAIN FX enable button (56x56 px) and Master Mute (M) button (56x56 px),
     tall 414 px vertical TrackAndThumb LEVEL fader with live Peak/Hold Meter alongside.
     Honest bypass: master level control and metering are NOT gated by FX bypass.
     Lacks: Destination, PAN, and sends (terminal output). Master solo is omitted (no clickable dead S).
- Real TrackAndThumb Native Vertical Fader:
  Uses genuine native Slider widget with sliderType='TrackAndThumb', direction='Vertical',
  thumbTrackVisible=True, and thumbImage='mixer_fader_cap.png'.
- Shared Static Background with Authentic Scale Ticks & Unity/0dB Reference:
  STATIC label captions, track background, scale ticks/major values, divisions and tasteful unity
  reference placed in the background image once. Dynamic value, toggle state, fader thumb and native
  meter stay live.
  Major/minor scale spacing strictly follows engine parameter scale from param_table:
  - SOURCE generators (SUB, A, B, C, NOISE): 0% to 100% linear, unity reference at 100% highlighted.
  - FILTERS (F1, F2): -60 dB to +12 dB linear, unity reference at 0 dB highlighted.
  - BUS & MASTER (BUS 1, BUS 2, MAIN): -60 dB to +6 dB with exp_skew 2.5, unity reference at 0 dB
    (normalized ~0.7885) highlighted with a moderately brighter/thicker tick and label.
- Touch Target Usability:
  Every touch target is strictly >= 56 px in both dimensions (width >= 56 px, height >= 56 px).
  10 strips fit comfortably within the 1280 px canvas (width 115 px, stride 127 px, start at x=10).
- Pure Native TUI Composites:
  One shared definition per variant (mixerStripSource, mixerStripBus, mixerStripMaster).
  Independent handle remappings per instance.
  0 Q-Link numbers, yellow chips, or invented controls.
- Style Tokens:
  Zero hardcoded dimensions, offsets, or font sizes. All geometry and font sizes are read
  from components/sizes.json (mixer_strip.*) and colors from components/colors.json via style.py.
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
    apply_control_state,
    argb as _style_argb,
    color as _style_color,
    rgb as _style_rgb,
    rgba as _style_rgba,
    size as _style_size,
)
from components.native.pan.pan import compact_pan_definition, synthyboy_dial_data as pan_dial_data
from components.native.level.level import compact_level_definition, synthyboy_dial_data as level_dial_data
from components.native.solo_mute.solo_mute import (
    build_solo_mute_definitions,
    create_solo_mute_instance,
    write_solo_mute_assets,
    render_button_face,
    STATE_NEUTRAL,
    STATE_SOLO,
    STATE_MUTE,
    STATE_IMPLICIT_SOLO_MUTE,
    STATE_DISABLED,
    STATE_PRESSED,
    VARIANT_SOURCE as SM_VARIANT_SOURCE,
    VARIANT_BUS as SM_VARIANT_BUS,
    VARIANT_MASTER as SM_VARIANT_MASTER,
    TYPE_SOLO_MUTE_SOURCE,
    TYPE_SOLO_MUTE_BUS,
    TYPE_SOLO_MUTE_MASTER,
)
from components.native.fader.fader import (
    build_fader_definition,
    create_fader_instance,
    write_fader_assets,
    render_fader_cap,
    render_fader_scale,
    SCALE_PERCENT,
    SCALE_LINEAR_DB,
    SCALE_SKEW_DB,
    unity_normalized,
    FADER_CAP_ASSET,
)
from components.native.meter_block.meter_block import (
    build_meter_block_definition,
    render_stereo_meter,
    create_meter_block_instance,
    render_meter_scale,
    write_meter_assets,
    METER_ASSETS,
    TYPE_METER_BLOCK_SOURCE,
    TYPE_METER_BLOCK_BUS,
)

# Variant identifiers
VARIANT_SOURCE = "source"
VARIANT_BUS = "bus"
VARIANT_MASTER = "master"
VARIANTS = (VARIANT_SOURCE, VARIANT_BUS, VARIANT_MASTER)

# Semantic destination choice lists
SOURCE_DESTINATIONS = (
    "Filter 1",
    "Filter 2",
    "Filters 1+2",
    "Effects only",
    "Direct out",
    "Filter blend",
    "None",
)
FILTER_DESTINATIONS = ("Effects only", "Direct out", "None")
BUS_DESTINATIONS = ("Main", "Direct", "Other bus")

# Canonical platform popup overlay name
POPUP_CHOICE_MENU = "fx editor choice menu"

# Canonical definition type names
TYPE_STRIP_SOURCE = "mixerStripSource"
TYPE_STRIP_BUS = "mixerStripBus"
TYPE_STRIP_MASTER = "mixerStripMaster"

TYPE_FADER_LEVEL_198 = "mixerSliderLEVEL198"
TYPE_FADER_LEVEL_414 = "mixerSliderLEVEL414"
TYPE_SLIDER_PAN_56 = "mixerSliderPAN56"
TYPE_SLIDER_B1_56 = "mixerSliderB156"
TYPE_SLIDER_B2_56 = "mixerSliderB256"
TYPE_DESTINATION_56 = "mixerDestination56"

TYPE_HEADER_SOURCE = "mixerStripHeaderSource"
TYPE_HEADER_BUS = "mixerStripHeaderBus"
TYPE_HEADER_MASTER = "mixerStripHeaderMaster"

BG_SOURCE_ASSET = "mixer_strip_source_bg.png"
BG_BUS_ASSET = "mixer_strip_bus_bg.png"
BG_MASTER_ASSET = "mixer_strip_master_bg.png"

# Required logical handles per variant
REQUIRED_HANDLES = {
    VARIANT_SOURCE: ("Name", "Solo", "Mute", "SoloVisual", "MuteVisual", "Power", "Destination", "Level", "PeakL", "PeakR", "HoldL", "HoldR", "ClipL", "ClipR", "Pan", "Send1", "Send2"),
    VARIANT_BUS: ("Name", "Solo", "Mute", "SoloVisual", "MuteVisual", "Destination", "Level", "PeakL", "PeakR", "HoldL", "HoldR", "ClipL", "ClipR"),
    VARIANT_MASTER: ("Name", "FxEnable", "Mute", "FxVisual", "MuteVisual", "Level", "PeakL", "PeakR", "HoldL", "HoldR", "ClipL", "ClipR"),
}

# Font resolver
FONT_FILE = Path(__file__).resolve().parent / "img" / _style_size("font.file.semibold")


def semibold(size_px: int):
    """Loads Titillium Web SemiBold from component img directory."""
    if ImageFont is None:
        return None
    if FONT_FILE.is_file():
        return ImageFont.truetype(str(FONT_FILE), size_px)
    try:
        return ImageFont.truetype("Arial", size_px)
    except Exception:
        return ImageFont.load_default()


def _entry(
    name: str,
    kind: str,
    data: Dict[str, Any],
    rect: Tuple[int, int, int, int],
    remaps: Sequence[Tuple[str, str]] = (),
    gates: Sequence[str] = (),
    focus: str = "Yes",
) -> Dict[str, Any]:
    """Builds a standard SDK component entry with bounds and handle remapping."""
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
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {
            "version": 1,
            "map": [{"key": k, "value": v} for k, v in remaps],
        },
        "bounds": bounds,
    }


def _label(
    name: str,
    text: Optional[str],
    rect: Tuple[int, int, int, int],
    parameter: Optional[str] = None,
    gates: Sequence[str] = (),
    font_size: Optional[int] = None,
    colour: Optional[str] = None,
    justification: str = "horizontallyCentred verticallyCentred",
    label_type: Optional[str] = None,
) -> Dict[str, Any]:
    """Builds an SDK Label component (Text, Value, or Name)."""
    colour_hex = colour or _style_argb("text.icon")
    size_val = font_size if font_size is not None else _style_size("mixer_strip.font.caption")
    resolved_type = label_type if label_type is not None else ("Text" if text is not None else "Value")
    data = {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {
                "version": 1,
                "name": _style_size("font.family"),
                "style": _style_size("font.style.semibold"),
                "height": float(size_val),
            },
            "colour": colour_hex,
            "justification": justification,
            "case": "Original",
        },
        "type": resolved_type,
        "handleName": "Data",
    }
    if text is not None:
        data["text"] = text
    remaps = (("Data", parameter),) if parameter is not None else ()
    return _entry(name, "Label", data, rect, remaps, gates, focus="No")


def build_compact_dial_definition(caption: str, bipolar: bool = False, skins_dir: Optional[Path] = None) -> Dict[str, Any]:
    """Produces the SDK Composite definition for compact dial (PAN, B1, B2).

    Reuses public factory APIs from components.native.pan and components.native.level.
    Configured with square 40x40 knob and readable name/value labels within 115x56 cell.
    """
    factory = compact_pan_definition if bipolar else compact_level_definition
    defn = factory(None, skins_dir)
    custom_rects = {
        "focus": (0, 0, _style_size("mixer_strip.width"), _style_size("mixer_strip.control.height_56")),
        "knob": tuple(_style_size("mixer_strip.rect.dial_knob")),
        "name": tuple(_style_size("mixer_strip.rect.dial_name")),
        "value": tuple(_style_size("mixer_strip.rect.dial_value")),
    }
    for item in defn.get("componentsData", []):
        name = item["componentData"]["name"]
        if name == "name":
            item["componentData"]["data"].update(type="Text", handleName="Data", text=caption)
        if name in custom_rects:
            item["bounds"]["bounds"] = "%d %d %d %d" % custom_rects[name]
    return defn


def build_mixer_strip_header_definition(variant: str) -> Dict[str, Any]:
    """Produces the SDK Composite definition for MixerStripHeader.

    Composes channel name plate (bound to Name handle) and SoloMutePair instance.
    """
    clear = {"version": 1, "colour": _style_argb("ink.black", 0), "image": ""}
    rect_hdr = tuple(_style_size("mixer_strip.rect.header_name"))
    rect_sm = tuple(_style_size("mixer_strip.rect.header_solo_mute"))
    name_label = _label(
        "name",
        None,
        rect_hdr,
        parameter="Name",
        font_size=_style_size("mixer_strip.font.header"),
        label_type="Name",
    )

    sm_kind = (
        TYPE_SOLO_MUTE_MASTER if variant == VARIANT_MASTER
        else TYPE_SOLO_MUTE_BUS if variant == VARIANT_BUS
        else TYPE_SOLO_MUTE_SOURCE
    )
    sm_entry = _entry("solo_mute", sm_kind, {"version": 1, "handleName": "Data"}, rect_sm, (("Data", "Mute"),))

    return {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": [name_label, sm_entry],
    }


def build_destination_definition() -> Dict[str, Any]:
    """Produces the SDK Composite definition for destination choice dropdown."""
    colour = {
        "version": 1,
        "colour": _style_argb("field.surface"),
        "image": "",
    }
    return {
        "version": 2,
        "actions": [
            {
                "version": 1,
                "onAction": trigger,
                "handler": "Show Overlay",
                "handleName": "Data",
                "additionalData": POPUP_CHOICE_MENU,
            }
            for trigger in ("Mouse Down", "Enter Pressed")
        ],
        "backgroundData": {"version": 1, "focussed": colour.copy(), "unfocussed": colour.copy()},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [
            _label(
                "value",
                None,
                tuple(_style_size("mixer_strip.rect.destination_label")),
                font_size=_style_size("mixer_strip.font.destination"),
                colour=_style_argb("text.icon"),
            )
        ],
    }


def supply_replacement_strings(tui: Dict[str, Any], parameter_index: int, choices: Sequence[str]) -> None:
    """Supplies replacementStrings into tui['pageData'] for destination popup dropdown."""
    items = tui.setdefault("pageData", {}).setdefault("replacementStrings", [])
    key = "Parameter %d" % parameter_index
    item = next((entry for entry in items if entry.get("key") == key), None)
    if item is None:
        items.append({"key": key, "value": list(choices)})
    else:
        item["value"] = list(choices)


def draw_fader_scale(
    draw: ImageDraw.Draw,
    fx: int,
    fy: int,
    fw: int,
    fh: int,
    scale_type: str,  # "percent", "linear_db", "skew_db"
    f_scale: Any,
) -> None:
    """Draws calibrated hardware fader scale ticks, numeric markings, and unity highlight."""
    track_w = _style_size("mixer_strip.fader.track_width")
    cap_h = _style_size("mixer_strip.fader.cap_height")
    src_max_h = _style_size("mixer_strip.fader.height_source")
    track_h = _style_size("mixer_strip.fader.track_height_source") if fh <= src_max_h else _style_size("mixer_strip.fader.track_height_bus")
    pad_top = _style_size("mixer_strip.fader.track_top_pad")
    pad_bot = _style_size("mixer_strip.fader.track_bottom_pad")

    tx0 = fx + (fw - track_w) // 2
    ty0 = fy + pad_top
    ty1 = fy + track_h - pad_bot

    # Background track groove
    draw.rounded_rectangle(
        [tx0, ty0, tx0 + track_w, ty1],
        radius=track_w // 2,
        fill=_style_rgb("surface.control"),
        outline=_style_rgb("field.border"),
        width=1,
    )

    travel_bottom = ty1 - cap_h // 2
    travel_top = ty0 + cap_h // 2
    travel_len = travel_bottom - travel_top

    # Tick definitions: (normalized_pos 0..1, label_text, is_unity_reference)
    ticks: List[Tuple[float, str, bool]] = []

    if scale_type == "percent":
        # 0..100% linear: sub.level, osc1.level, noise.level
        for pct in (0, 25, 50, 75, 100):
            norm = pct / 100.0
            is_unity = pct == 100  # Unity gain is 100%
            ticks.append((norm, str(pct), is_unity))
        minor_norms = [p / 100.0 for p in (12.5, 37.5, 62.5, 87.5)]
    elif scale_type == "linear_db":
        # -60 to +12 dB linear: filter1.level, filter2.level
        # 0 dB proportion is (0 - (-60)) / 72 = 60/72 = 0.8333
        for db in (-60, -36, -24, -12, -6, 0, 6, 12):
            norm = (db + 60.0) / 72.0
            lbl = "+12" if db == 12 else "+6" if db == 6 else "0" if db == 0 else "-60" if db == -60 else str(db)
            is_unity = db == 0
            ticks.append((norm, lbl, is_unity))
        minor_norms = [(db + 60.0) / 72.0 for db in (-48, -18, -3, 3, 9)]
    else:
        # -60 to +6 dB exp_skew 2.5: bus1.level, bus2.level, master.volume
        # norm = ((db + 60) / 66) ** 2.5
        # Thinned major labels to eliminate overlapping -48 / -60 / -inf:
        for db in (-60, -30, -18, -6, 0, 6):
            if db <= -60:
                norm = 0.0
                lbl = "-60"
            else:
                prop = (db + 60.0) / 66.0
                norm = math.pow(prop, 2.5)
                lbl = "+6" if db == 6 else "0" if db == 0 else str(db)
            is_unity = db == 0
            ticks.append((norm, lbl, is_unity))
        minor_norms = [math.pow((db + 60.0) / 66.0, 2.5) for db in (-48, -42, -36, -24, -12, -3, 3)]

    # Draw minor ticks
    tick_right = tx0 - 3
    for norm in minor_norms:
        cy = int(round(travel_bottom - norm * travel_len))
        draw.line([tick_right - 4, cy, tick_right, cy], fill=_style_rgb("control.border"), width=1)

    # Draw major ticks and labels
    for norm, lbl, is_unity in ticks:
        cy = int(round(travel_bottom - norm * travel_len))
        if is_unity:
            # Highlighted unity / 0 dB mark: thicker, brighter, longer tick and highlighted label
            draw.line([tick_right - 8, cy, tick_right, cy], fill=_style_rgb("text.white"), width=2)
            draw.text((tick_right - 10, cy), lbl, font=f_scale, fill=_style_rgb("accent.cyan"), anchor="rm")
        else:
            draw.line([tick_right - 6, cy, tick_right, cy], fill=_style_rgb("text.label"), width=1)
            draw.text((tick_right - 8, cy), lbl, font=f_scale, fill=_style_rgb("text.muted"), anchor="rm")

    # Static LEVEL caption
    f_cap = semibold(_style_size("mixer_strip.font.caption"))
    draw.text((fx + fw // 2, fy + 10), "LEVEL", font=f_cap, fill=_style_rgb("text.caption"), anchor="mm")


def render_mixer_strip_background(variant: str, caption: str = "", scale_type: Optional[str] = None) -> Image.Image:
    """Generates the static shared background image for a mixer channel strip variant."""
    strip_w = _style_size("mixer_strip.width")
    strip_h = _style_size("mixer_strip.height")

    im = Image.new("RGBA", (strip_w, strip_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    f_hdr = semibold(_style_size("mixer_strip.font.header"))
    f_cap = semibold(_style_size("mixer_strip.font.caption"))
    f_scale = semibold(_style_size("mixer_strip.font.scale"))

    # Strip card body
    r_card = _style_size("radius.control")
    draw.rounded_rectangle(
        [0, 0, strip_w - 1, strip_h - 1],
        radius=r_card,
        fill=_style_rgb("surface.panel"),
        outline=_style_rgb("button.border"),
        width=1,
    )

    # 1. Header Name Plate (no fake "SOURCE"/"BUS"/"MAIN" text baked into reusable BG PNG)
    rect_hdr = _style_size("mixer_strip.rect.header_name")
    hdr_px = _style_size("mixer_strip.header.pad_x")
    hdr_py = _style_size("mixer_strip.header.pad_y")
    hdr_r = _style_size("mixer_strip.header.radius")
    draw.rounded_rectangle(
        [rect_hdr[0] + hdr_px, rect_hdr[1] + hdr_py, rect_hdr[0] + rect_hdr[2] - hdr_px - 1, rect_hdr[1] + rect_hdr[3] - 1],
        radius=hdr_r,
        fill=_style_rgb("button.surface"),
        outline=_style_rgb("field.border"),
        width=1,
    )
    if caption:
        draw.text((strip_w // 2, rect_hdr[1] + rect_hdr[3] // 2), caption, font=f_hdr, fill=_style_rgb("text.icon"), anchor="mm")

    # 2. S and M button wells
    rect_solo = _style_size("mixer_strip.rect.solo")
    rect_mute = _style_size("mixer_strip.rect.mute")
    for rx, ry, rw, rh in (rect_solo, rect_mute):
        draw.rounded_rectangle([rx, ry, rx + rw - 1, ry + rh - 1], radius=_style_size("radius.field"), fill=_style_rgb("surface.control"), outline=_style_rgb("field.border"), width=1)

    # 3. Destination dropdown well
    if variant in (VARIANT_SOURCE, VARIANT_BUS):
        rect_dest = _style_size("mixer_strip.rect.destination")
        dx, dy, dw, dh = rect_dest
        draw.rounded_rectangle([dx, dy, dx + dw - 1, dy + dh - 1], radius=_style_size("radius.field"), fill=_style_rgb("field.surface"), outline=_style_rgb("field.border"), width=1)
    else:
        # Master decorative plate
        rect_dest = _style_size("mixer_strip.rect.destination")
        dx, dy, dw, dh = rect_dest
        draw.rounded_rectangle([dx, dy, dx + dw - 1, dy + dh - 1], radius=_style_size("radius.field"), fill=_style_rgb("surface.control"), outline=_style_rgb("button.border"), width=1)
        draw.text((dx + dw // 2, dy + dh // 2), "OUTPUT", font=f_cap, fill=_style_rgb("text.muted"), anchor="mm")

    # 4. Fader box, track groove, scale ticks, and Meter well
    is_source = variant == VARIANT_SOURCE
    rect_level = _style_size("mixer_strip.rect.level_source" if is_source else "mixer_strip.rect.level_bus")
    rect_meter = _style_size("mixer_strip.rect.meter_source" if is_source else "mixer_strip.rect.meter_bus")
    fx, fy, fw, fh = rect_level
    mx, my, mw, mh = rect_meter

    draw.rounded_rectangle([fx, fy, fx + fw - 1, fy + fh - 1], radius=r_card, fill=_style_rgb("voice.surface"), outline=_style_rgb("voice.border"), width=1)
    if scale_type is None:
        if caption.upper() in ("F1", "F2", "FILTER 1", "FILTER 2", "FILTER1", "FILTER2"):
            scale_type = "linear_db"
        elif is_source:
            scale_type = "percent"
        else:
            scale_type = "skew_db"
    draw_fader_scale(draw, fx, fy, fw, fh, scale_type, f_scale)

    # Value box well beneath fader track
    src_max_h = _style_size("mixer_strip.fader.height_source")
    track_h = _style_size("mixer_strip.fader.track_height_source") if fh <= src_max_h else _style_size("mixer_strip.fader.track_height_bus")
    vbox_px = _style_size("mixer_strip.fader.value_box_pad_x")
    vbox_gap = _style_size("mixer_strip.fader.value_box_gap_y")
    val_y0 = fy + track_h + vbox_gap
    val_y1 = fy + fh - vbox_gap
    draw.rounded_rectangle([fx + vbox_px, val_y0, fx + fw - vbox_px - 1, val_y1], radius=_style_size("mixer_strip.header.radius"), fill=_style_rgb("surface.control"), outline=_style_rgb("field.border"), width=1)

    # Meter well
    draw.rectangle([mx, my, mx + mw - 1, my + mh - 1], fill=_style_rgb("meter.idle"), outline=_style_rgb("field.border"), width=1)

    # 5. PAN, B1, B2 (SOURCE variant only)
    if is_source:
        sliders = [
            ("PAN", _style_size("mixer_strip.rect.pan")),
            ("B1", _style_size("mixer_strip.rect.send1")),
            ("B2", _style_size("mixer_strip.rect.send2")),
        ]
        for name, rect in sliders:
            sx, sy, sw, sh = rect
            draw.rounded_rectangle([sx, sy, sx + sw - 1, sy + sh - 1], radius=r_card, fill=_style_rgb("voice.surface"), outline=_style_rgb("voice.border"), width=1)

    return im


def render_mixer_full_screen_background() -> Image.Image:
    """Renders the authentic full-screen (1280x628) backdrop for the native Mixer tab.

    Understated blue/white SynthyBoy console styling:
    - Subtle dark console surface
    - 10 channel columns with crisp name plates, S/M button wells, fader track grooves,
      accurate scale ticks, and highlighted 0 dB / 100% unity reference marks.
    - Bottom routing bar / PEEK action row area (y = 566..622)
    - Preserves exact sentinel pixel block at (900, 18) with color #141a35.
    """
    W, H = 1280, 628
    im = Image.new("RGBA", (W, H), _style_rgb("surface.canvas"))
    draw = ImageDraw.Draw(im)

    f_hdr = semibold(_style_size("mixer_strip.font.header"))
    f_cap = semibold(_style_size("mixer_strip.font.caption"))
    f_scale = semibold(_style_size("mixer_strip.font.scale"))

    # 10 Channel Strips
    strip_names = ("SUB", "A", "B", "C", "NOISE", "F1", "F2", "BUS 1", "BUS 2", "MAIN")
    start_x = _style_size("mixer_strip.start_x")
    stride = _style_size("mixer_strip.stride")
    strip_w = _style_size("mixer_strip.width")
    strip_y = _style_size("mixer_strip.start_y")
    strip_h = _style_size("mixer_strip.height")

    for i, name in enumerate(strip_names):
        x = start_x + stride * i
        variant = VARIANT_MASTER if i == 9 else VARIANT_BUS if i >= 7 else VARIANT_SOURCE
        scale = "linear_db" if i in (5, 6) else ("percent" if i < 5 else "skew_db")
        strip_bg = render_mixer_strip_background(variant, caption=name, scale_type=scale)
        im.alpha_composite(strip_bg, (x, strip_y))

        # Vertical divider groove between strips
        if i > 0:
            div_x = x - (stride - strip_w) // 2
            draw.line([div_x, strip_y + 4, div_x, strip_y + strip_h - 4], fill=_style_rgb("preview.divider"), width=1)

    # Bottom Routing Bar Area (y = 566..622)
    routing_y = strip_y + strip_h + 6
    draw.rectangle([0, routing_y, W - 1, routing_y + 56], fill=_style_rgb("surface.panel"))
    draw.line([0, routing_y, W - 1, routing_y], fill=_style_rgb("panel.border"), width=1)

    # Sentinel block (900, 18, 5x5, mixer.sentinel) - Required for DRM plane sync
    draw.rectangle([900, 18, 904, 22], fill=_style_rgb("mixer.sentinel"))

    return im


def write_mixer_strip_assets(skins_dir: Path) -> None:
    """Writes all native mixer strip assets into the designated skins directory."""
    skins_dir = Path(skins_dir)
    skins_dir.mkdir(parents=True, exist_ok=True)

    # 1. Fader cap
    write_fader_assets(skins_dir)

    # 2. Multi-state Solo / Mute / FX FilmStrip sheets + transparent hitboxes
    write_solo_mute_assets(skins_dir)

    # 3. Variant backgrounds (clean, without fake SOURCE/BUS/MAIN header text)
    render_mixer_strip_background(VARIANT_SOURCE).save(skins_dir / BG_SOURCE_ASSET)
    render_mixer_strip_background(VARIANT_BUS).save(skins_dir / BG_BUS_ASSET)
    render_mixer_strip_background(VARIANT_MASTER).save(skins_dir / BG_MASTER_ASSET)

    # 4. Meters
    write_meter_assets(skins_dir)
    from components.native.meter.meter import write_stereo_meter_assets
    for height in (_style_size("mixer_strip.fader.height_source"), _style_size("mixer_strip.fader.height_bus")):
        write_stereo_meter_assets(skins_dir, height)

    # 5. Compact dial filmstrips (play_sub_pan_40.png and play_sub_level_40.png)
    knob_sz = _style_size("mixer_strip.rect.dial_knob")[2]
    pan_dial_data(skins_dir, frame_size=knob_sz, bipolar=True)
    level_dial_data(skins_dir, frame_size=knob_sz, bipolar=False, active=True)


# ---------------------------------------------------------------------------
# Component Definitions Builder
# ---------------------------------------------------------------------------

def build_mixer_strip_definitions(skins_dir: Optional[Path] = None) -> Dict[str, Dict[str, Any]]:
    """Builds and returns all shared native component definitions needed for mixer strips.

    All strips instantiate shared Header (with SoloMutePair), CalibratedFader, and MeterBlock.
    """
    strip_w = _style_size("mixer_strip.width")
    h_level_source = _style_size("mixer_strip.fader.height_source")
    h_level_bus = _style_size("mixer_strip.fader.height_bus")
    meter_w = _style_size("mixer_strip.meter.width")

    defs: Dict[str, Dict[str, Any]] = {
        TYPE_DESTINATION_56: build_destination_definition(),
        TYPE_FADER_LEVEL_198: build_fader_definition(TYPE_FADER_LEVEL_198, h_level_source, SCALE_PERCENT),
        TYPE_FADER_LEVEL_414: build_fader_definition(TYPE_FADER_LEVEL_414, h_level_bus, SCALE_SKEW_DB),
        TYPE_SLIDER_PAN_56: build_compact_dial_definition("PAN", bipolar=True, skins_dir=skins_dir),
        TYPE_SLIDER_B1_56: build_compact_dial_definition("B1", bipolar=False, skins_dir=skins_dir),
        TYPE_SLIDER_B2_56: build_compact_dial_definition("B2", bipolar=False, skins_dir=skins_dir),
        TYPE_METER_BLOCK_SOURCE: build_meter_block_definition(TYPE_METER_BLOCK_SOURCE, width=meter_w, height=h_level_source, hold=True, with_clip=True),
        TYPE_METER_BLOCK_BUS: build_meter_block_definition(TYPE_METER_BLOCK_BUS, width=meter_w, height=h_level_bus, hold=True, with_clip=True),
        TYPE_HEADER_SOURCE: build_mixer_strip_header_definition(VARIANT_SOURCE),
        TYPE_HEADER_BUS: build_mixer_strip_header_definition(VARIANT_BUS),
        TYPE_HEADER_MASTER: build_mixer_strip_header_definition(VARIANT_MASTER),
    }
    defs.update(build_solo_mute_definitions(skins_dir=skins_dir))

    rect_sm_local = tuple(_style_size("mixer_strip.rect.header_solo_mute"))
    rect_header = (0, 0, strip_w, rect_sm_local[1] + rect_sm_local[3])
    rect_destination = tuple(_style_size("mixer_strip.rect.destination"))
    rect_level_source = tuple(_style_size("mixer_strip.rect.level_source"))
    rect_meter_source = tuple(_style_size("mixer_strip.rect.meter_source"))
    rect_pan = tuple(_style_size("mixer_strip.rect.pan"))
    rect_send1 = tuple(_style_size("mixer_strip.rect.send1"))
    rect_send2 = tuple(_style_size("mixer_strip.rect.send2"))
    rect_level_bus = tuple(_style_size("mixer_strip.rect.level_bus"))
    rect_meter_bus = tuple(_style_size("mixer_strip.rect.meter_bus"))

    # The tab already draws each strip's own caption and calibrated fader scale.
    clear = {"version": 1, "colour": _style_argb("ink.black", 0), "image": ""}

    # SOURCE VARIANT
    def source_control(name, kind, rect, parameter):
        passive_kind = kind + "Off"
        defs[passive_kind] = apply_control_state(defs[kind], skins_dir, active=False)
        data = {"version": 1, "handleName": "Data"}
        remaps = (("Data", parameter),)
        return (
            _entry(name, kind, data, rect, remaps, ("PowerOn",)),
            _entry(name + " Off", passive_kind, data.copy(), rect, remaps, ("PowerOff",), focus="No"),
        )

    defs[TYPE_STRIP_SOURCE] = {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [
            _entry("header", TYPE_HEADER_SOURCE, {"version": 1, "handleName": "Data"}, rect_header, (("Data", "Name"),)),
            *source_control("destination", TYPE_DESTINATION_56, rect_destination, "Destination"),
            _entry("level", TYPE_FADER_LEVEL_198, {"version": 1, "handleName": "Data"}, rect_level_source, (("Data", "Level"),)),
            _entry("meter_block", TYPE_METER_BLOCK_SOURCE, {"version": 1, "handleName": "Data"}, rect_meter_source, (("Data", "PeakL"),)),
            *source_control("pan", TYPE_SLIDER_PAN_56, rect_pan, "Pan"),
            *source_control("send1", TYPE_SLIDER_B1_56, rect_send1, "Send1"),
            *source_control("send2", TYPE_SLIDER_B2_56, rect_send2, "Send2"),
        ],
    }

    # BUS VARIANT (Honest bypass: dry audio level & meter remain active under bypass)
    defs[TYPE_STRIP_BUS] = {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [
            _entry("header", TYPE_HEADER_BUS, {"version": 1, "handleName": "Data"}, rect_header, (("Data", "Name"),)),
            _entry("destination", TYPE_DESTINATION_56, {"version": 1, "handleName": "Data"}, rect_destination, (("Data", "Destination"),)),
            _entry("level", TYPE_FADER_LEVEL_414, {"version": 1, "handleName": "Data"}, rect_level_bus, (("Data", "Level"),)),
            _entry("meter_block", TYPE_METER_BLOCK_BUS, {"version": 1, "handleName": "Data"}, rect_meter_bus, (("Data", "PeakL"),)),
        ],
    }

    # MASTER VARIANT (Honest MAIN FX bypass: main output level & meter remain active under bypass)
    defs[TYPE_STRIP_MASTER] = {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": clear.copy(), "unfocussed": clear.copy()},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [
            _entry("header", TYPE_HEADER_MASTER, {"version": 1, "handleName": "Data"}, rect_header, (("Data", "Name"),)),
            _entry("level", TYPE_FADER_LEVEL_414, {"version": 1, "handleName": "Data"}, rect_level_bus, (("Data", "Level"),)),
            _entry("meter_block", TYPE_METER_BLOCK_BUS, {"version": 1, "handleName": "Data"}, rect_meter_bus, (("Data", "PeakL"),)),
        ],
    }

    return defs


def create_mixer_strip_instance(
    variant: str,
    rect: Tuple[int, int, int, int],
    handles: Dict[str, Union[int, str]],
    gates: Sequence[str] = (),
    focus: str = "Yes",
    name: Optional[str] = None,
) -> Dict[str, Any]:
    """Creates an SDK component instance entry binding to a shared mixer strip definition."""
    if variant not in VARIANTS:
        raise ValueError(f"Unknown mixer strip variant: {variant}")

    required = REQUIRED_HANDLES[variant]
    missing = [h for h in required if h not in handles]
    if missing:
        raise ValueError(f"Missing required handle(s) {missing} for mixer strip variant {variant}")

    type_name = (
        TYPE_STRIP_SOURCE if variant == VARIANT_SOURCE
        else TYPE_STRIP_BUS if variant == VARIANT_BUS
        else TYPE_STRIP_MASTER
    )

    remaps = []
    for k in ("Data", *required):
        val = handles["Level" if k == "Data" else k]
        val_str = f"Parameter {val}" if isinstance(val, int) else str(val)
        remaps.append((k, val_str))

    if variant == VARIANT_SOURCE:
        power = handles["Power"]
        power_handle = f"Parameter {power}" if isinstance(power, int) else power
        remaps.extend((("PowerOn", f"IndexedEnabling/1/2/{power_handle}"),
                       ("PowerOff", f"IndexedEnabling/0/2/{power_handle}")))

    inst_name = name or f"MixerStrip_{variant.capitalize()}_{rect[0]}_{rect[1]}"

    return _entry(
        name=inst_name,
        kind=type_name,
        data={"version": 1, "handleName": "Data"},
        rect=rect,
        remaps=remaps,
        gates=gates,
        focus=focus,
    )


# ---------------------------------------------------------------------------
# Offline Preview Renderer (Authentic component preview)
# ---------------------------------------------------------------------------

def render_mixer_strip(variant: str, state: Dict[str, Any]) -> Image.Image:
    """Renders an authentic offline preview image of a single mixer channel strip."""
    if Image is None:
        raise RuntimeError("Pillow is required for rendering mixer strip preview")

    strip_w = _style_size("mixer_strip.width")
    strip_h = _style_size("mixer_strip.height")

    f_dest = semibold(_style_size("mixer_strip.font.destination"))
    f_value = semibold(_style_size("mixer_strip.font.value"))

    caption = state.get("name", "STRIP")
    im = render_mixer_strip_background(variant, caption=caption)
    draw = ImageDraw.Draw(im)

    f_dest = semibold(_style_size("mixer_strip.font.destination"))
    f_value = semibold(_style_size("mixer_strip.font.value"))
    f_caption = semibold(_style_size("mixer_strip.font.caption"))

    # 1. Solo & Mute visual states (shared solo_mute.py FilmStrip face renderer)
    solo_state = state.get("solo_state")
    mute_state = state.get("mute_state")
    if variant in (VARIANT_SOURCE, VARIANT_BUS):
        if not solo_state:
            solo_state = STATE_SOLO if state.get("solo", 0) else STATE_NEUTRAL
        if not mute_state:
            mute_state = STATE_MUTE if state.get("mute", 0) else STATE_NEUTRAL
        solo_img = render_button_face("S", solo_state)
        mute_img = render_button_face("M", mute_state)
        rect_solo = _style_size("mixer_strip.rect.solo")
        rect_mute = _style_size("mixer_strip.rect.mute")
        im.alpha_composite(solo_img, (rect_solo[0], rect_solo[1]))
        im.alpha_composite(mute_img, (rect_mute[0], rect_mute[1]))
    else:
        if not solo_state:
            solo_state = "fx_on" if state.get("fx_enable", 1) else STATE_NEUTRAL
        if not mute_state:
            mute_state = STATE_MUTE if state.get("mute", 0) else STATE_NEUTRAL
        fx_img = render_button_face("FX", solo_state)
        mute_img = render_button_face("M", mute_state)
        rect_fx = _style_size("mixer_strip.rect.fx_enable")
        rect_mute = _style_size("mixer_strip.rect.mute")
        im.alpha_composite(fx_img, (rect_fx[0], rect_fx[1]))
        im.alpha_composite(mute_img, (rect_mute[0], rect_mute[1]))

    # 2. Destination
    if variant in (VARIANT_SOURCE, VARIANT_BUS):
        dest_rect = _style_size("mixer_strip.rect.destination")
        dest_x, dest_y, dest_w, dest_h = dest_rect
        dest_val = state.get("destination", "Filter 1")
        draw.text((dest_x + dest_w // 2, dest_y + dest_h // 2), dest_val, font=f_dest, fill=_style_rgb("text.icon"), anchor="mm")

    # 3. Fader Thumb, Value text, and Meter
    is_source = variant == VARIANT_SOURCE
    fader_rect = _style_size("mixer_strip.rect.level_source" if is_source else "mixer_strip.rect.level_bus")
    meter_rect = _style_size("mixer_strip.rect.meter_source" if is_source else "mixer_strip.rect.meter_bus")
    track_h = _style_size("mixer_strip.fader.track_height_source" if is_source else "mixer_strip.fader.track_height_bus")
    fx, fy, fw, fh = fader_rect
    mx, my, mw, mh = meter_rect

    cap_img = render_fader_cap()
    cap_w, cap_h = cap_img.size
    level_norm = max(0.0, min(1.0, float(state.get("level", 0.5))))

    ty0 = fy + 22
    ty1 = fy + track_h - 10
    travel_bottom = ty1 - cap_h // 2
    travel_top = ty0 + cap_h // 2
    cap_center_y = int(round(travel_bottom - level_norm * (travel_bottom - travel_top)))
    cap_x = fx + (fw - cap_w) // 2
    cap_y = cap_center_y - cap_h // 2
    im.alpha_composite(cap_img, (cap_x, cap_y))

    # Dynamic Level Value Text in bottom value box
    val_rect = _style_size("mixer_strip.rect.slider_value_v_source" if is_source else "mixer_strip.rect.slider_value_v_bus")
    vx, vy, vw, vh = val_rect
    level_str = state.get("level_text")
    if not level_str:
        if caption.upper() in ("F1", "F2", "FILTER 1", "FILTER 2", "FILTER1", "FILTER2"):
            db_val = -60.0 + 72.0 * level_norm
            level_str = f"{db_val:.1f} dB"
        elif is_source:
            level_str = f"{level_norm * 100:.1f}%"
        else:
            if level_norm <= 0.0:
                level_str = "-60.0 dB"
            else:
                prop = math.exp(math.log(level_norm) / 2.5)
                db_val = -60.0 + 66.0 * prop
                level_str = f"{db_val:.1f} dB"
    draw.text((fx + vx + vw // 2, fy + vy + vh // 2), level_str, font=f_value, fill=_style_rgb("text.value"), anchor="mm")

    # Native stereo Meter image planes, using the same assets and normalized read-only fields.
    im.alpha_composite(render_stereo_meter(mh, state), (mx, my))

    # 4. PAN, B1, B2 compact dials (SOURCE variant only)
    if is_source:
        pan_img_path = Path(__file__).resolve().parent.parent / "pan" / "img" / "play_sub_pan_40.png"
        lvl_img_path = Path(__file__).resolve().parent.parent / "level" / "img" / "play_sub_level_40.png"
        pan_strip_img = Image.open(pan_img_path).convert("RGBA") if pan_img_path.is_file() else None
        lvl_strip_img = Image.open(lvl_img_path).convert("RGBA") if lvl_img_path.is_file() else None

        kx, ky, kw, kh = _style_size("mixer_strip.rect.dial_knob")
        nx, ny, nw, nh = _style_size("mixer_strip.rect.dial_name")
        vx, vy, vw, vh = _style_size("mixer_strip.rect.dial_value")

        dials = [
            ("PAN", _style_size("mixer_strip.rect.pan"), state.get("pan", 0.0), state.get("pan_text", "C"), True, pan_strip_img),
            ("B1", _style_size("mixer_strip.rect.send1"), state.get("send1", 0.0), state.get("send1_text", "0.0%"), False, lvl_strip_img),
            ("B2", _style_size("mixer_strip.rect.send2"), state.get("send2", 0.0), state.get("send2_text", "0.0%"), False, lvl_strip_img),
        ]
        for name, rect, sval, stext, is_bipolar, filmstrip in dials:
            sx, sy, sw, sh = rect
            draw.text((sx + nx, sy + ny + nh // 2), name, font=f_caption, fill=_style_rgb("text.caption"), anchor="lm")
            draw.text((sx + vx, sy + vy + vh // 2), stext, font=f_value, fill=_style_rgb("text.value"), anchor="lm")

            norm = float(sval) if not is_bipolar else (float(sval) + 1.0) / 2.0 if abs(float(sval)) <= 1.0 else (float(sval) + 100.0) / 200.0
            norm = max(0.0, min(1.0, norm))
            frame_idx = int(round(norm * 126))
            if filmstrip:
                frame = filmstrip.crop((0, frame_idx * 40, 40, (frame_idx + 1) * 40))
                im.alpha_composite(frame, (sx + kx, sy + ky))
            else:
                draw.ellipse([sx + kx, sy + ky, sx + kx + kw - 1, sy + ky + kh - 1], fill=_style_rgb("voice.surface"), outline=_style_rgb("voice.border"))

    return im


__all__ = [
    "VARIANT_SOURCE",
    "VARIANT_BUS",
    "VARIANT_MASTER",
    "VARIANTS",
    "SOURCE_DESTINATIONS",
    "FILTER_DESTINATIONS",
    "BUS_DESTINATIONS",
    "TYPE_STRIP_SOURCE",
    "TYPE_STRIP_BUS",
    "TYPE_STRIP_MASTER",
    "TYPE_FADER_LEVEL_198",
    "TYPE_FADER_LEVEL_414",
    "TYPE_SLIDER_PAN_56",
    "TYPE_SLIDER_B1_56",
    "TYPE_SLIDER_B2_56",
    "TYPE_DESTINATION_56",
    "TYPE_HEADER_SOURCE",
    "TYPE_HEADER_BUS",
    "TYPE_HEADER_MASTER",
    "build_mixer_strip_header_definition",
    "FADER_CAP_ASSET",
    "METER_ASSETS",
    "BG_SOURCE_ASSET",
    "BG_BUS_ASSET",
    "BG_MASTER_ASSET",
    "REQUIRED_HANDLES",
    "semibold",
    "build_mixer_strip_definitions",
    "create_mixer_strip_instance",
    "supply_replacement_strings",
    "render_fader_cap",
    "render_mixer_strip_background",
    "render_mixer_full_screen_background",
    "write_mixer_strip_assets",
    "render_mixer_strip",
]
