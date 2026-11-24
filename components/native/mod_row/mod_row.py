# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reusable host-native MPC ModRow compound component and definitions.

Faithfully preserves our authentic dark blue / white / cyan aesthetic
from docs/user-guide/img/mod.png:
- Slim bipolar horizontal amount bar with center zero tick, center-origin fill, and signed readout
- Full 56px input rectangle for native Slider (touch target >= 56px)
- Source and destination dropdown selectors with subtle chevron
- Compact UNI/BI polarity and ON/OFF route state toggles
- Dedicated MORE action button
- Arrow separator between source and target
- Shared JSON compound row definition factory with independent handle bindings per row instance
"""

import copy
import math
from pathlib import Path

from components.style import (
    argb as _style_argb,
    color as _style_color,
    size as _style_size,
)
from components.native.button.button import action_art, in_slot

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

IMG_DIR = Path(__file__).resolve().parent / "img"
HISTORICAL_SOURCE_IMAGE = (
    Path(__file__).resolve().parents[3] / "docs/screens/mod_old.jpg"
    if (Path(__file__).resolve().parents[3] / "docs/screens/mod_old.jpg").is_file()
    else Path(__file__).resolve().parents[3] / "docs/user-guide/img/mod.png"
)

# Style semantic dimensions
ROW_HEIGHT = _style_size("mod_row.height")
ROW_WIDTH = _style_size("mod_row.width")
ROW_FULLWIDTH = _style_size("mod_row.fullwidth")
ROW_STEP = _style_size("mod_row.step")
SLOT_WIDTH = _style_size("mod_row.slot_width")
SOURCE_WIDTH = _style_size("mod_row.source_width")
ARROW_WIDTH = _style_size("mod_row.arrow_width")
TARGET_WIDTH = _style_size("mod_row.target_width")
AMOUNT_WIDTH_NORMAL = _style_size("mod_row.amount_width_normal")
AMOUNT_WIDTH_SCROLL = _style_size("mod_row.amount_width_scroll")
SLIDER_WIDTH_NORMAL = _style_size("mod_row.slider_width_normal")
SLIDER_WIDTH_SCROLL = _style_size("mod_row.slider_width_scroll")
SLIDER_HEIGHT = _style_size("mod_row.slider_height")
TRACK_HEIGHT = _style_size("mod_row.track_height")
TRACK_RADIUS = _style_size("mod_row.track_radius")
POLARITY_WIDTH = _style_size("mod_row.polarity_width")
ROUTE_WIDTH = _style_size("mod_row.route_width")
MORE_WIDTH = _style_size("mod_row.more_width")
READOUT_WIDTH = _style_size("mod_row.readout_width")

ROW_FIELDS_NORMAL = [
    ("AMOUNT", "bar", (452, 0, AMOUNT_WIDTH_NORMAL, ROW_HEIGHT)),
    ("POLARITY", "toggle", (966, 0, POLARITY_WIDTH, ROW_HEIGHT)),
    ("ROUTE", "toggle", (1044, 0, ROUTE_WIDTH, ROW_HEIGHT)),
    ("SOURCE", "list", (0, 0, SOURCE_WIDTH, ROW_HEIGHT)),
    ("TARGET", "list", (202, 0, TARGET_WIDTH, ROW_HEIGHT)),
    ("MORE", "action", (1122, 0, MORE_WIDTH, ROW_HEIGHT)),
]

ROW_FIELDS_SCROLL = [
    ("AMOUNT", "bar", (452, 0, AMOUNT_WIDTH_SCROLL, ROW_HEIGHT)),
    ("POLARITY", "toggle", (922, 0, POLARITY_WIDTH, ROW_HEIGHT)),
    ("ROUTE", "toggle", (1000, 0, ROUTE_WIDTH, ROW_HEIGHT)),
    ("SOURCE", "list", (0, 0, SOURCE_WIDTH, ROW_HEIGHT)),
    ("TARGET", "list", (202, 0, TARGET_WIDTH, ROW_HEIGHT)),
    ("MORE", "action", (1078, 0, MORE_WIDTH, ROW_HEIGHT)),
]


def semibold(font_size):
    font_file = IMG_DIR / _style_size("font.file.semibold")
    if ImageFont is None or not font_file.is_file():
        return None
    return ImageFont.truetype(str(font_file), int(round(font_size)))


def _entry(name, kind, data, bounds_rect, remaps=(), gates=(), focus="Yes"):
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % tuple(bounds_rect),
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remaps]},
        "bounds": bounds,
    }


def _label_entry(kind, bounds_rect, justification="horizontallyCentred verticallyCentred", font_size=16.0):
    name_mode = kind == "Name"
    data = {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {
                "version": 1,
                "name": _style_size("font.family"),
                "style": _style_size("font.style.semibold"),
                "height": float(font_size),
            },
            "colour": _style_argb("mod_row.text_label") if name_mode else _style_argb("mod_row.text_value"),
            "justification": justification,
            "case": "Original",
        },
        "type": kind,
        "handleName": "Caption" if name_mode else "Data",
    }
    return _entry(kind.lower(), "Label", data, bounds_rect, focus="No")


def build_cell_definition(kind, width, height, *, image_name=None):
    """Factory for standard cell definitions used within a ModRow.
    
    Ensures all interactive components have a touch hitbox of at least 56px.
    """
    actions = []
    bg_unfocussed = {"version": 1, "colour": _style_argb("mod_row.button_surface"), "image": image_name or ""}
    bg_focussed = {"version": 1, "colour": _style_argb("mod_row.focus"), "image": image_name or ""}

    if kind == "bar":
        # Full 56px native Slider with slim centered bipolar track artwork and signed readout
        bar_w = width - 108
        track_img = image_name or f"mod_slider_track_{bar_w}x56.png"
        revealed_img = f"mod_slider_revealed_{bar_w}x56.png"
        thumb_img = "mod_slider_thumb.png"

        slider_data = {
            "version": 4,
            "sliderType": "TrackAndThumb",
            "direction": "Horizontal",
            "thumbImage": thumb_img,
            "revealedImage": revealed_img,
            "thumbTrackVisible": False,
            "revealType": "Bipolar",
            "backgroundColour": "00000000",
            "outlineColour": "00000000",
            "valueColour": _style_argb("mod_row.amount_fill"),
            "isBipolar": True,
            "handleName": "Data",
        }
        # Explicit Image component for track artwork placed at (4, 0, bar_w, height),
        # strictly avoiding background stretch across the entire cell into the value readout.
        track_bg = _entry(
            "track_bg",
            "Image",
            {"version": 2, "imageType": "Regular", "colour": "0", "image": track_img},
            (4, 0, bar_w, height),
            focus="No",
        )
        control = _entry("amount", "Slider", slider_data, (4, 0, bar_w, height), focus="Yes")
        readout = _label_entry(
            "Value",
            (width - 96, 0, 96, height),
            justification="right verticallyCentred",
            font_size=_style_size("mod_row.font.value"),
        )
        children = [track_bg, control, readout]
        bg_unfocussed = {"version": 1, "colour": "00000000", "image": ""}
        bg_focussed = {"version": 1, "colour": _style_argb("mod_row.focus"), "image": ""}

    elif kind == "toggle":
        if width > 100:
            name_w = int(width * 0.55)
            val_w = width - name_w
            name_lbl = _label_entry("Name", (8, 0, name_w - 8, height), justification="left verticallyCentred")
            val_lbl = _label_entry("Value", (name_w, 0, val_w - 8, height), justification="right verticallyCentred")
            children = [name_lbl, val_lbl]
        else:
            children = [_label_entry("Value", (0, 0, width, height), font_size=_style_size("mod_row.font.value"))]
        actions = [
            {"version": 1, "onAction": trigger, "handler": "Toggle Switch", "handleName": "Data", "additionalData": ""}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]

    elif kind in ("list", "group"):
        # Dropdown list selector box with left-aligned value text and chevron
        children = [
            _label_entry(
                "Value",
                (12, 0, width - 36, height),
                justification="left verticallyCentred",
                font_size=_style_size("mod_row.font.value"),
            )
        ]
        handler, extra = ("Show Overlay", "fx editor choice menu") if kind == "group" else ("Toggle Switch", "")
        actions = [
            {"version": 1, "onAction": trigger, "handler": handler, "handleName": "Data", "additionalData": extra}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]

    elif kind == "action":
        # Action button (e.g. MORE)
        children = [_label_entry("Name", (0, 0, width, height), font_size=_style_size("mod_row.font.label"))]
        actions = [
            {"version": 1, "onAction": trigger, "handler": "Toggle Switch", "handleName": "Data", "additionalData": ""}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]

    else:
        children = [_label_entry("Value", (0, 0, width, height))]

    return {
        "version": 2,
        "actions": actions,
        "backgroundData": {"version": 1, "focussed": bg_focussed, "unfocussed": bg_unfocussed},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": kind in ("action", "list", "group"),
        "componentsData": children,
    }


def build_mod_row_definition(skins_dir=None, *, layout="normal", row_type=None):
    """Build canonical host-native ModRow compound component definition.
    
    Contains all 6 interactive controls: AMOUNT, POLARITY, ROUTE, SOURCE, TARGET, MORE.
    Each control has >= 56px height touch hitbox.
    """
    fields = ROW_FIELDS_SCROLL if layout == "scroll" else ROW_FIELDS_NORMAL
    children = []
    for caption, kind, rect in fields:
        w, h = rect[2], rect[3]
        cell_def_name = f"modNative_{kind}_{w}x{h}"
        caption_name = "EDIT" if caption == "MORE" else caption
        children.append(
            _entry(
                caption,
                cell_def_name,
                {"version": 1, "handleName": "Data"},
                rect,
                [("Data", caption), ("Caption", f"WithName[{caption_name}]/{caption}")],
                focus="Yes",
            )
        )
    clear = {"version": 1, "colour": "00000000", "image": ""}
    return {
        "version": 2,
        "actions": [],
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "backgroundData": {"version": 1, "focussed": clear, "unfocussed": clear},
        "componentsData": children,
    }


def build_mod_row_definitions(skins_dir=None):
    """Export all localComponentDefinitions required by ModRow compounds."""
    defs = {}
    # 1. Base cell definitions
    for w in (AMOUNT_WIDTH_NORMAL, AMOUNT_WIDTH_SCROLL):
        defs[f"modNative_bar_{w}x56"] = build_cell_definition("bar", w, ROW_HEIGHT)
    defs["modNative_toggle_72x56"] = build_cell_definition("toggle", POLARITY_WIDTH, ROW_HEIGHT)
    defs["modNative_list_176x56"] = build_cell_definition("list", SOURCE_WIDTH, ROW_HEIGHT)
    defs["modNative_list_244x56"] = build_cell_definition("list", TARGET_WIDTH, ROW_HEIGHT)
    defs["modNative_action_76x56"] = build_cell_definition("action", MORE_WIDTH, ROW_HEIGHT)

    # 2. Compound row definitions
    row_normal = build_mod_row_definition(skins_dir, layout="normal")
    row_scroll = build_mod_row_definition(skins_dir, layout="scroll")
    defs["modNativeRowNormal"] = row_normal
    defs["modNativeRowScroll"] = row_scroll
    defs["modRow"] = copy.deepcopy(row_normal)  # Canonical public row definition
    return defs


def build_mod_row_instance(
    name,
    row_idx,
    rect,
    *,
    row_type="modNativeRowNormal",
    slot_handle=None,
    amount_handle=None,
    polarity_handle=None,
    route_handle=None,
    source_handle=None,
    target_handle=None,
    more_handle=None,
    edit_handle=None,
    gates=(),
    caption=None,
):
    """Instantiate a single ModRow with independent handle bindings."""
    slot = f"Parameter {slot_handle}" if isinstance(slot_handle, int) else (slot_handle or f"Slot {row_idx + 1}")
    bindings = [
        ("Data", slot),
        ("Caption", f"WithName[{caption}]/{slot}" if caption else slot),
    ]
    if amount_handle is not None:
        amt = f"Parameter {amount_handle}" if isinstance(amount_handle, int) else amount_handle
        bindings.extend([("AMOUNT", amt), ("Amount", amt)])
    if polarity_handle is not None:
        pol = f"Parameter {polarity_handle}" if isinstance(polarity_handle, int) else polarity_handle
        bindings.extend([("POLARITY", pol), ("Polarity", pol)])
    if route_handle is not None:
        rt = f"Parameter {route_handle}" if isinstance(route_handle, int) else route_handle
        bindings.extend([("ROUTE", rt), ("Route", rt), ("Bypass", rt)])
    if source_handle is not None:
        src = f"Parameter {source_handle}" if isinstance(source_handle, int) else source_handle
        bindings.extend([("SOURCE", src), ("Source", src)])
    if target_handle is not None:
        dst = f"Parameter {target_handle}" if isinstance(target_handle, int) else target_handle
        bindings.extend([("TARGET", dst), ("Target", dst)])
    action_h = edit_handle if edit_handle is not None else more_handle
    if action_h is not None:
        mr = f"Parameter {action_h}" if isinstance(action_h, int) else action_h
        bindings.extend([
            ("EDIT", mr), ("Edit", mr),
            ("MORE", mr), ("More", mr), ("OpenMore", mr),
        ])

    entry = _entry(name, row_type, {"version": 1, "handleName": "Data"}, rect, bindings, gates=gates)
    entry["bounds"]["acceptsHWFocus"] = "Children"
    return entry


def draw_selector_frame(width, height, radius=6):
    """Draw a dark dropdown selector box with outline and subtle dropdown chevron."""
    if Image is None:
        return None
    scale = 4
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    pad = 2 * scale
    r = radius * scale

    draw.rounded_rectangle(
        (pad, pad, w - pad - 1, h - pad - 1),
        radius=r,
        fill=_style_color("mod_row.field_surface"),
        outline=_style_color("mod_row.field_border"),
        width=scale,
    )

    # Subtle dropdown chevron on the right side
    cx = w - 18 * scale
    cy = h // 2
    cw = 5 * scale
    draw.polygon(
        [(cx - cw, cy - 2 * scale), (cx + cw, cy - 2 * scale), (cx, cy + 3 * scale)],
        fill=_style_color("mod_row.chevron"),
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


def draw_arrow(width=22, height=56):
    """Draw slim arrow separator -> between source and target."""
    if Image is None:
        return None
    scale = 4
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    cy = h // 2
    ax = 4 * scale
    aw = 14 * scale
    col = _style_color("mod_row.arrow")
    draw.line([(ax, cy), (ax + aw, cy)], fill=col, width=2 * scale)
    draw.line(
        [(ax + aw - 5 * scale, cy - 4 * scale), (ax + aw, cy), (ax + aw - 5 * scale, cy + 4 * scale)],
        fill=col,
        width=2 * scale,
    )

    return im.resize((width, height), Image.Resampling.LANCZOS)


def draw_slider_track(width, height=56):
    """Draw thin track art inside a full 56px high image with center zero tick and quarter/half static marks."""
    if Image is None:
        return None
    scale = 4
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    track_h = TRACK_HEIGHT * scale
    track_y = (h - track_h) // 2
    pad = 4 * scale

    draw.rounded_rectangle(
        [(pad, track_y), (w - pad - 1, track_y + track_h - 1)],
        radius=TRACK_RADIUS * scale,
        fill=_style_color("mod_row.track"),
        outline=_style_color("mod_row.track_border"),
        width=scale,
    )

    # Static scale ticks (-100%, -75%, -50%, -25%, 0, +25%, +50%, +75%, +100%)
    inner_w = (w - pad - 1) - (pad + 1)
    mid_x = pad + 1 + inner_w // 2
    half_span = inner_w // 2

    # (fraction, height_fraction, color_token, line_width)
    ticks = [
        (0.0, 1.0, "mod_row.tick_center", 2),
        (-0.5, 0.65, "mod_row.tick_major", 1),
        (0.5, 0.65, "mod_row.tick_major", 1),
        (-0.25, 0.40, "mod_row.tick_minor", 1),
        (-0.75, 0.40, "mod_row.tick_minor", 1),
        (0.25, 0.40, "mod_row.tick_minor", 1),
        (0.75, 0.40, "mod_row.tick_minor", 1),
        (-1.0, 0.30, "mod_row.tick_minor", 1),
        (1.0, 0.30, "mod_row.tick_minor", 1),
    ]

    for frac, h_ratio, col_key, w_factor in ticks:
        tx = mid_x + int(round(frac * (half_span - scale)))
        th = int(round((track_h - 2 * scale) * h_ratio))
        ty0 = track_y + scale + (track_h - 2 * scale - th) // 2
        ty1 = ty0 + th
        draw.line([(tx, ty0), (tx, ty1)], fill=_style_color(col_key), width=w_factor * scale)

    return im.resize((width, height), Image.Resampling.LANCZOS)


def draw_slider_revealed(width, height=56):
    """Draw bipolar cyan fill art inside a full 56px high image for TrackAndThumb slider."""
    if Image is None:
        return None
    scale = 4
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    track_h = TRACK_HEIGHT * scale
    track_y = (h - track_h) // 2
    pad = 4 * scale

    # Full fill rectangle inside the track bounds
    draw.rectangle(
        [(pad + scale, track_y + scale), (w - pad - scale - 1, track_y + track_h - scale - 1)],
        fill=_style_color("mod_row.amount_fill"),
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


def draw_slider_thumb(width=8, height=56):
    """Draw thumb indicator cap inside a full 56px high image for TrackAndThumb slider."""
    if Image is None:
        return None
    scale = 4
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    track_h = TRACK_HEIGHT * scale
    track_y = (h - track_h) // 2
    cap_h = track_h + 4 * scale
    cap_y = track_y - 2 * scale
    mid_x = w // 2

    draw.rectangle(
        [(mid_x - scale, cap_y), (mid_x + scale - 1, cap_y + cap_h - 1)],
        fill=_style_color("mod_row.amount_thumb"),
    )
    return im.resize((width, height), Image.Resampling.LANCZOS)


def render_standalone_mod_row(
    slot_num="01",
    source="LFO 1",
    target="Filter 1 Cutoff",
    amount=0.65,
    polarity="BI",
    route="ON",
    width=ROW_FULLWIDTH,
    height=ROW_HEIGHT,
):
    """Render authentic standalone ModRow art at fullwidth using actual component definitions."""
    if Image is None:
        return None
    im = Image.new("RGBA", (width, height), _style_color("mod_row.surface"))
    draw = ImageDraw.Draw(im)

    font_main = semibold(_style_size("mod_row.font.label")) or ImageFont.load_default()
    font_bold = semibold(_style_size("mod_row.font.value")) or ImageFont.load_default()

    # 1. Slot Number in left lane (x ~ 33)
    draw.text(
        (_style_size("mod_matrix.slot_num_x"), height // 2),
        str(slot_num),
        fill=_style_color("mod_row.text_label"),
        font=font_main,
        anchor="mm",
    )

    # 2. Source dropdown
    sx, sy, sw, sh = 66, 0, SOURCE_WIDTH, height
    sel_src = draw_selector_frame(sw, sh)
    if sel_src:
        im.alpha_composite(sel_src, (sx, sy))
    draw.text((sx + 14, sy + sh // 2), source, fill=_style_color("mod_row.text_value"), font=font_main, anchor="lm")

    # 3. Arrow
    arr = draw_arrow(ARROW_WIDTH, height)
    if arr:
        im.alpha_composite(arr, (sx + sw + 6, 0))

    # 4. Target dropdown
    tx, ty, tw, th = sx + sw + 26, 0, TARGET_WIDTH, height
    sel_dst = draw_selector_frame(tw, th)
    if sel_dst:
        im.alpha_composite(sel_dst, (tx, ty))
    draw.text((tx + 14, ty + th // 2), target, fill=_style_color("mod_row.text_value"), font=font_main, anchor="lm")

    # 5. Amount bar (full 56px with thin track)
    bx, by, bw, bh = tx + tw + 6, 0, AMOUNT_WIDTH_NORMAL, height
    bar_w = SLIDER_WIDTH_NORMAL
    track_art = draw_slider_track(bar_w, height)
    if track_art:
        im.alpha_composite(track_art, (bx + 4, 0))

    # Fill deflection
    mid_x = bx + 4 + bar_w // 2
    track_y = (height - TRACK_HEIGHT) // 2
    val_fraction = max(-1.0, min(1.0, amount))
    fill_len = int(round(val_fraction * (bar_w // 2 - 4)))
    if fill_len > 0:
        draw.rectangle(
            [(mid_x, track_y + 2), (mid_x + fill_len, track_y + TRACK_HEIGHT - 3)],
            fill=_style_color("mod_row.amount_fill"),
        )
    elif fill_len < 0:
        draw.rectangle(
            [(mid_x + fill_len, track_y + 2), (mid_x, track_y + TRACK_HEIGHT - 3)],
            fill=_style_color("mod_row.amount_fill"),
        )

    # Thumb indicator
    thumb = draw_slider_thumb(8, height)
    if thumb:
        im.alpha_composite(thumb, (mid_x + fill_len - 4, 0))

    # Readout
    sign = "+" if amount > 0 else ""
    disp_text = f"{sign}{amount * 100.0:.1f} %"
    draw.text((bx + bw - 12, height // 2), disp_text, fill=_style_color("mod_row.text_value"), font=font_bold, anchor="rm")

    # 6. Polarity
    px, py, pw, ph = bx + bw + 6, 0, POLARITY_WIDTH, height
    draw.rounded_rectangle(
        [(px, 2), (px + pw - 1, ph - 3)],
        radius=_style_size("mod_row.button_radius"),
        fill=_style_color("mod_row.button_surface"),
        outline=_style_color("mod_row.button_border"),
        width=1,
    )
    draw.text((px + pw // 2, ph // 2), polarity, fill=_style_color("mod_row.text_value"), font=font_bold, anchor="mm")

    # 7. Route State (ON/OFF)
    rx, ry, rw, rh = px + pw + 6, 0, ROUTE_WIDTH, height
    draw.rounded_rectangle(
        [(rx, 2), (rx + rw - 1, rh - 3)],
        radius=_style_size("mod_row.button_radius"),
        fill=_style_color("mod_row.button_surface"),
        outline=_style_color("mod_row.button_border"),
        width=1,
    )
    state_col = _style_color("mod_row.status_on") if route == "ON" else _style_color("mod_row.status_off")
    draw.text((rx + rw // 2, rh // 2), route, fill=state_col, font=font_bold, anchor="mm")

    # 8. EDIT
    mx, my, mw, mh = rx + rw + 6, 0, MORE_WIDTH, height
    draw.rounded_rectangle(
        [(mx, 2), (mx + mw - 1, mh - 3)],
        radius=_style_size("mod_row.button_radius"),
        fill=_style_color("mod_row.button_surface"),
        outline=_style_color("mod_row.button_border"),
        width=1,
    )
    draw.text((mx + mw // 2, mh // 2), "EDIT", fill=_style_color("mod_row.text_value"), font=font_main, anchor="mm")

    # Bottom subtle divider line
    draw.line([(8, height - 1), (width - 8, height - 1)], fill=_style_color("mod_row.divider"), width=1)

    return im


def extract_historical_before_crop(source_path=HISTORICAL_SOURCE_IMAGE):
    """Crop the real first row from docs/screens/mod_old.jpg preserving original historical Q badges."""
    if Image is None or not Path(source_path).is_file():
        return None
    with Image.open(source_path) as im:
        y0 = 196 if im.height == 800 else 86
        y1 = y0 + 72
        return im.crop((0, y0, im.width, y1)).convert("RGBA")


def write_assets(dest_dir=IMG_DIR):
    """Generate canonical PNG assets into dest_dir."""
    dest_dir = Path(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)

    # 1. Standalone row previews at fullwidth (1280px)
    row_populated = render_standalone_mod_row(
        slot_num="01", source="LFO 1", target="Filter 1 Cutoff", amount=0.65, polarity="BI", route="ON"
    )
    if row_populated:
        row_populated.save(dest_dir / "mod_row_preview.png")
        row_populated.save(dest_dir / "catalog-mod-row.png")
        row_populated.save(dest_dir / "mod_row_after.png")

    # Real historical crop from mod.png
    row_before = extract_historical_before_crop()
    if row_before:
        row_before.save(dest_dir / "mod_row_before.png")

    # Inactive state
    row_inactive = render_standalone_mod_row(
        slot_num="--", source="---", target="---", amount=0.0, polarity="UNI", route="OFF"
    )
    if row_inactive:
        row_inactive.save(dest_dir / "inactive.png")

    # 2. Selectors
    s176 = draw_selector_frame(SOURCE_WIDTH, ROW_HEIGHT)
    if s176:
        s176.save(dest_dir / "mod_selector_176x56.png")

    s244 = draw_selector_frame(TARGET_WIDTH, ROW_HEIGHT)
    if s244:
        s244.save(dest_dir / "mod_selector_244x56.png")

    s260 = draw_selector_frame(260, ROW_HEIGHT)
    if s260:
        s260.save(dest_dir / "mod_selector_260x56.png")

    arr = draw_arrow(ARROW_WIDTH, ROW_HEIGHT)
    if arr:
        arr.save(dest_dir / "mod_arrow_22x56.png")

    # 3. Slider TrackAndThumb Assets (400x56 for normal, 356x56 for scroll)
    for w in (SLIDER_WIDTH_NORMAL, SLIDER_WIDTH_SCROLL):
        track_art = draw_slider_track(w, ROW_HEIGHT)
        if track_art:
            track_art.save(dest_dir / f"mod_slider_track_{w}x56.png")
        revealed_art = draw_slider_revealed(w, ROW_HEIGHT)
        if revealed_art:
            revealed_art.save(dest_dir / f"mod_slider_revealed_{w}x56.png")

    thumb_art = draw_slider_thumb(8, ROW_HEIGHT)
    if thumb_art:
        thumb_art.save(dest_dir / "mod_slider_thumb.png")

    # 4. Compact Buttons
    for txt, col_key, filename in (
        ("UNI", "mod_row.text_value", "mod_btn_uni.png"),
        ("BI", "mod_row.text_value", "mod_btn_bi.png"),
        ("ON", "mod_row.status_on", "mod_btn_on.png"),
        ("OFF", "mod_row.status_off", "mod_btn_off.png"),
        ("EDIT", "mod_row.text_value", "mod_action_edit.png"),
        ("MORE", "mod_row.text_value", "mod_action_more.png"),
        ("+ ADD", "mod_row.text_value", "mod_action_add.png"),
    ):
        w = MORE_WIDTH if txt in ("EDIT", "MORE") else (120 if txt == "+ ADD" else POLARITY_WIDTH)
        art = in_slot(action_art(text=txt, width=w, colour=_style_color(col_key)), (w, ROW_HEIGHT))
        art.save(dest_dir / filename)

    # 5. Standalone Row Before / After Comparison (fullwidth, NO clipping of MORE right edge)
    if row_before and row_populated:
        comp_w = 1280
        comp_h = 72 + ROW_HEIGHT + 110
        comp_im = Image.new("RGBA", (comp_w, comp_h), _style_color("surface.canvas"))
        draw_comp = ImageDraw.Draw(comp_im)
        font_cap = semibold(_style_size("mod_row.font.caption")) or ImageFont.load_default()
        font_sub = semibold(_style_size("mod_row.font.sub")) or font_cap

        # Before section
        draw_comp.text(
            (16, 12),
            "HISTORICAL BEFORE (GEN 1): REAL FIRST-ROW CROP FROM docs/user-guide/img/mod.png",
            fill=_style_color("accent.cyan"),
            font=font_cap,
        )
        draw_comp.text(
            (16, 30),
            "Original Akai MPC standalone screenshot · Yellow Q-Link bank frame & numbered Q badges preserved",
            fill=_style_color("text.muted"),
            font=font_sub,
        )
        comp_im.alpha_composite(row_before, (0, 48))

        # After section
        after_y = 48 + 72 + 20
        draw_comp.text(
            (16, after_y),
            "CURRENT AFTER: HOST-NATIVE REUSABLE MODROW COMPOUND CONTROL (OFFLINE PREVIEW)",
            fill=_style_color("accent.cyan"),
            font=font_cap,
        )
        draw_comp.text(
            (16, after_y + 18),
            "1280x56 fullwidth · Full 56px Slider hitbox · Authentic thin bipolar track & signed readout · Frameless Gen 2 physical routing",
            fill=_style_color("text.muted"),
            font=font_sub,
        )
        comp_im.alpha_composite(row_populated, (0, after_y + 36))

        comp_im.save(dest_dir / "mod_row_before_after.png")


if __name__ == "__main__":
    import sys
    repo_root = Path(__file__).resolve().parents[2]
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))
    write_assets()
    print("ModRow assets and previews generated successfully.")
