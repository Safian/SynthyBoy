# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Reusable host-native MPC ModRow compound component and definitions.

Faithfully preserves our authentic dark blue / white / cyan aesthetic
from docs/screens/mod_old.jpg:
- Slim bipolar horizontal amount bar with center zero tick, center-origin fill, and signed readout
- Full 56px input rectangle for native Slider (touch target >= 56px)
- Source and destination dropdown selectors with subtle chevron
- Compact UNI/BI polarity and ON/OFF route state toggles
- Dedicated MORE action button
- Arrow separator between source and target
- Shared JSON compound row definition factory with independent handle bindings per row instance
"""

import copy
from pathlib import Path

from components.style import (
    argb as _style_argb,
    native_color as _style_native_color,
    color as _style_color,
    size as _style_size,
)
from components.native.level.level import synthyboy_dial_data as level_dial_data
from components.native.pan.pan import synthyboy_dial_data as pan_dial_data
from components.native.selector.selector import dropdown_definition, render_dropdown_face, save_png_filter0
from components.native.button.button import action_art, button as native_button, entry as _entry, in_slot

try:
    from PIL import Image, ImageColor, ImageDraw, ImageFont
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
TRACK_HEIGHT = _style_size("mod_row.track_height")
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


def _label_entry(kind, bounds_rect, justification="horizontallyCentred verticallyCentred", font_size=_style_size("mod_row.font.label")):
    from components.native.name_value.name_value import _label_component
    name_mode = kind == "Name"
    label = _label_component(kind.lower(), kind, "Caption" if name_mode else "Data", bounds_rect,
                             font_size, _style_argb("mod_row.text_label" if name_mode else "mod_row.text_value"),
                             justification)
    label["bounds"]["showWhenDataModelInvalid"] = "Hide"
    return label


def build_cell_definition(kind, width, height, *, image_name=None, skins_dir=None, choice_menu="fx editor choice menu"):
    """Factory for standard cell definitions used within a ModRow.
    
    Ensures all interactive components have a touch hitbox of at least 56px.
    """
    actions = []
    bg_unfocussed = {"version": 1, "colour": _style_argb("mod_row.button_surface"), "image": image_name or ""}
    bg_focussed = {"version": 1, "colour": _style_argb("mod_row.focus"), "image": image_name or ""}

    if kind == "bar":
        left, bar_w = amount_track_geometry(width)
        track_img = image_name or f"mod_slider_track_{bar_w}x{height}.png"
        revealed_img = f"mod_slider_revealed_{bar_w}x{height}.png"
        thumb_img = f"mod_slider_thumb_{height}.png"
        panel = f"mod_amount_{width}x{height}.png"
        if skins_dir is not None:
            write_amount_assets(skins_dir, width, height)

        slider_data = {
            "version": 4,
            "sliderType": "TrackAndThumb",
            "direction": "Horizontal",
            "thumbImage": thumb_img,
            "revealedImage": revealed_img,
            "thumbTrackVisible": False,
            "revealType": "Bipolar",
            "backgroundColour": _style_native_color("transparent"),
            "outlineColour": _style_native_color("transparent"),
            "valueColour": _style_argb("mod_row.amount_fill"),
            "isBipolar": True,
            "handleName": "Data",
        }
        track_bg = _entry(
            "track_bg",
            "Image",
            {"version": 2, "imageType": "Regular", "colour": "0", "image": track_img},
            (left, 0, bar_w, height),
            focus="No",
        )
        control = _entry("amount", "Slider", slider_data, (left, 0, bar_w, height), focus="Yes")
        readout = _label_entry(
            "Value",
            (width - READOUT_WIDTH, 0, READOUT_WIDTH - _style_size("mod_row.amount.readout_inset"), height),
            justification="right verticallyCentred",
            font_size=_style_size("mod_row.font.amount"),
        )
        children = [track_bg, control, readout]
        bg_unfocussed = {"version": 1, "colour": _style_native_color("transparent"), "image": panel}
        bg_focussed = dict(bg_unfocussed, colour=_style_argb("mod_row.focus"))

    elif kind == "toggle":
        if width > _style_size("mod_row.build_cell_definition.split_threshold"):
            name_w = int(width * 0.55)
            val_w = width - name_w
            name_lbl = _label_entry("Name", (_style_size("mod_row.build_cell_definition.double_padding"), 0, name_w - _style_size("mod_row.build_cell_definition.double_padding"), height), justification="left verticallyCentred")
            val_lbl = _label_entry("Value", (name_w, 0, val_w - _style_size("mod_row.build_cell_definition.double_padding"), height), justification="right verticallyCentred")
            children = [name_lbl, val_lbl]
        else:
            children = [_label_entry("Value", (0, 0, width, height), font_size=_style_size("mod_row.font.value"))]
        actions = [
            {"version": 1, "onAction": trigger, "handler": "Toggle Switch", "handleName": "Data", "additionalData": ""}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]

    elif kind in ("list", "group"):
        handler, extra = ("Show Overlay", choice_menu) if kind == "group" else ("Toggle Switch", "")
        actions = [
            {"version": 1, "onAction": trigger, "handler": handler, "handleName": "Data", "additionalData": extra}
            for trigger in ("Mouse Down", "Enter Pressed")
        ]

        definition = dropdown_definition(width, height, actions=actions, skins_dir=skins_dir)
        definition["disableCoarseDataWheel"] = True
        return definition

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
    clear = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
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
        defs[f"modNative_bar_{w}x56"] = build_cell_definition("bar", w, ROW_HEIGHT, skins_dir=skins_dir)
    defs["modNative_toggle_72x56"] = build_cell_definition("toggle", POLARITY_WIDTH, ROW_HEIGHT)
    defs["modNative_list_176x56"] = build_cell_definition("list", SOURCE_WIDTH, ROW_HEIGHT, skins_dir=skins_dir)
    defs["modNative_list_244x56"] = build_cell_definition("list", TARGET_WIDTH, ROW_HEIGHT, skins_dir=skins_dir)
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


def draw_arrow(width=_style_size("mod_row.draw_arrow.width"), height=_style_size("mod_row.draw_arrow.height")):
    """Draw slim arrow separator -> between source and target."""
    if Image is None:
        return None
    scale = _style_size("mod_row.draw_arrow.unit")
    w, h = width * scale, height * scale
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)

    cy = h // 2
    ax = _style_size("mod_row.draw_arrow.unit") * scale
    aw = _style_size("mod_row.draw_arrow.shaft") * scale
    col = _style_color("mod_row.arrow")
    draw.line([(ax, cy), (ax + aw, cy)], fill=col, width=_style_size("mod_row.draw_arrow.stroke") * scale)
    draw.line(
        [(ax + aw - _style_size("mod_row.draw_arrow.head_width") * scale, cy - _style_size("mod_row.draw_arrow.unit") * scale), (ax + aw, cy), (ax + aw - _style_size("mod_row.draw_arrow.head_width") * scale, cy + _style_size("mod_row.draw_arrow.unit") * scale)],
        fill=col,
        width=_style_size("mod_row.draw_arrow.stroke") * scale,
    )

    return im.resize((width, height), Image.Resampling.LANCZOS)


def amount_track_geometry(width):
    left = _style_size("mod_row.amount.track_left")
    return left, width - READOUT_WIDTH - 2 * left


def _slider_art(width, height, *, revealed=False, thumb=False):
    scale = _style_size("mod_row.draw_slider_track.unit")
    w, h = width * scale, height * scale
    art = Image.new("RGBA", (w, h))
    pen = ImageDraw.Draw(art)
    if thumb:
        pen.ellipse((0, (h - w) // 2, w - 1, (h + w) // 2 - 1), fill=_style_color("mod_row.amount_thumb"))
    else:
        inset = _style_size("mod_row.amount.track_inset") * scale
        rail = TRACK_HEIGHT * scale
        pen.rounded_rectangle((inset, (h - rail) // 2, w - inset - 1, (h + rail) // 2 - 1),
                              radius=rail // 2, fill=_style_color("mod_row.amount_fill" if revealed else "mod_row.track"))
        if revealed:
            colors = [ImageColor.getrgb(_style_color(f"theme.neon.flow.{i}")) for i in range(4)]
            for x in range(inset, w - inset):
                # The host reveals a slice from zero toward either endpoint.
                phase = abs(x - w / 2) / (w / 2 - inset) * (len(colors) - 1)
                index = min(int(phase), len(colors) - 2)
                mix = phase - index
                rgb = tuple(round(a + (b - a) * mix) for a, b in zip(colors[index], colors[index + 1]))
                pen.line((x, (h - rail) // 2, x, (h + rail) // 2 - 1), fill=rgb)
        else:
            tick = _style_size("mod_row.amount.center_height") * scale
            stroke = round(_style_size("mod_row.amount.center_width") * scale)
            pen.rectangle(((w - stroke) // 2, (h - tick) // 2, (w + stroke) // 2 - 1, (h + tick) // 2 - 1),
                          fill=_style_color("theme.neon.textDim"))
    return art.resize((width, height), Image.Resampling.LANCZOS)


def write_amount_assets(directory, width, height):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    _, rail_width = amount_track_geometry(width)
    images = {
        f"mod_amount_{width}x{height}.png": render_dropdown_face(width, height, chevron=False),
        f"mod_slider_track_{rail_width}x{height}.png": _slider_art(rail_width, height),
        f"mod_slider_revealed_{rail_width}x{height}.png": _slider_art(rail_width, height, revealed=True),
        f"mod_slider_thumb_{height}.png": _slider_art(_style_size("mod_row.draw_slider_thumb.width"), height, thumb=True),
    }
    for filename, image in images.items():
        save_png_filter0(image, directory / filename)


def paint_amount_value(canvas, origin, normalized, revealed, thumb):
    x, y = origin
    width, height = revealed.size
    normalized = min(1, max(0, normalized))
    center = width // 2
    handle = round(thumb.width / 2 + normalized * (width - thumb.width))
    start, end = sorted((center, handle))
    if end > start:
        canvas.alpha_composite(revealed.crop((start, 0, end, height)), (x + start, y))
    canvas.alpha_composite(thumb, (x + handle - thumb.width // 2, y))


def render_standalone_mod_row(slot_num="01", source="LFO 1", target="Filter 1 Cutoff",
                              amount=0.65, polarity="BI", route="ON", width=ROW_FULLWIDTH, height=ROW_HEIGHT):
    """Compose the catalogue row from its native field layout and actual skin images."""
    from tempfile import TemporaryDirectory
    im = Image.new("RGBA", (width, height), _style_color("mod_row.surface"))
    pen = ImageDraw.Draw(im)
    origin = _style_size("mod_matrix.header_center_source") - SOURCE_WIDTH // 2
    pen.text((_style_size("mod_matrix.slot_num_x"), height // 2), slot_num,
             fill=_style_color("mod_row.text_label"), font=semibold(_style_size("mod_row.font.label")), anchor="mm")
    im.alpha_composite(draw_arrow(ARROW_WIDTH, height), (origin + SOURCE_WIDTH + 6, 0))
    values = {"SOURCE": source, "TARGET": target, "AMOUNT": f"{amount * 100:+.1f}".removesuffix(".0") + "%",
              "POLARITY": polarity, "ROUTE": route}
    with TemporaryDirectory() as tmp:
        directory = Path(tmp)
        definitions = build_mod_row_definitions(directory)
        for caption, kind, (offset, y, w, h) in ROW_FIELDS_NORMAL:
            x = origin + offset
            if kind == "action":
                im.alpha_composite(in_slot(action_art(text="EDIT", width=w), (w, h)), (x, y))
                continue
            cell = definitions[f"modNative_{kind}_{w}x{h}"]
            background = cell["backgroundData"]["unfocussed"]
            if background["image"]:
                with Image.open(directory / background["image"]) as face:
                    im.alpha_composite(face, (x, y))
            elif kind == "toggle":
                pen.rectangle((x, y, x + w - 1, y + h - 1), fill=_style_color("mod_row.button_surface"))
            for child in cell["componentsData"]:
                info = child["componentData"]
                data = info["data"]
                dx, dy, cw, ch = map(int, child["bounds"]["bounds"].split())
                cx, cy = x + dx, y + dy
                if info["type"] == "Image":
                    with Image.open(directory / data["image"]) as frame:
                        im.alpha_composite(frame.convert("RGBA"), (cx, cy))
                elif info["type"] == "Slider":
                    with Image.open(directory / data["revealedImage"]) as fill, Image.open(directory / data["thumbImage"]) as thumb:
                        paint_amount_value(im, (cx, cy), (amount + 1) / 2, fill, thumb)
                elif info["type"] == "Label":
                    style = data["textStyle"]
                    right = "right" in style["justification"]
                    font = {"bar": _style_size("mod_row.font.amount_preview"), "list": _style_size("selector.dropdown.preview_font")}.get(kind, style["font"]["height"])
                    pen.text((cx + (cw if right else cw // 2), cy + ch // 2), values[caption],
                             fill="#" + style["colour"][-6:], font=semibold(font), anchor="rm" if right else "mm")
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
    """Export the runtime assets and standalone component catalog artwork."""
    dest_dir = Path(dest_dir)
    for width in (AMOUNT_WIDTH_NORMAL, AMOUNT_WIDTH_SCROLL):
        write_amount_assets(dest_dir, width, ROW_HEIGHT)

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

    arr = draw_arrow(ARROW_WIDTH, ROW_HEIGHT)
    if arr:
        arr.save(dest_dir / "mod_arrow_22x56.png")

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
            "HISTORICAL BEFORE (GEN 1): REAL FIRST-ROW CROP FROM docs/screens/mod_old.jpg",
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


def write_route_button_art(directory, key, caption, rect, active):
    """Shared route editor action face with native button state palette."""
    from components.native.button.button import text_face
    image = text_face(caption, tuple(rect[2:]), geometry="filter.pz.button",
                      surface="filter.pz.active.surface",
                      border="mod_targets.border.active" if active else "filter.pz.active.border",
                      text="text.icon")
    filename = f"mod_targets_{key}.png"
    image.save(Path(directory) / filename)
    return filename


def write_route_pagination_art(directory, width, height):
    """Availability filmstrip; page value and hit targets remain native widgets."""
    from components.native.button.button import text_face
    geometry = _style_size("mod_targets.pagination")
    film = Image.new("RGBA", (width, height * 4))
    for mode in range(1, 4):
        image = text_face("", (width, height), geometry="filter.pz.button",
                          surface="filter.pz.active.surface", border="filter.pz.active.border", text="text.icon")
        pen = ImageDraw.Draw(image)
        inset = geometry["arrow_inset"]
        for arrow, x, active in (("‹", inset, mode <= 2), ("›", width - inset, mode >= 2)):
            pen.text((x, height / 2), arrow, anchor="mm", font=semibold(geometry["arrow_font"]),
                     fill=_style_color("text.icon" if active else "mod_targets.arrow.disabled"))
        film.paste(image, (0, height * mode))
    filename = "mod_targets_pages.png"
    film.save(Path(directory) / filename)
    return filename


def editor_label(kind, rect, just="horizontallyCentred verticallyCentred"):
    name = kind == "Name"
    font_h = _style_size("mod_row.font.label") if name else _style_size("mod_row.font.value")
    return _label_entry(kind, rect, justification=just, font_size=font_h)


def build_editor_cell_definition(kind, w, h, *, choice_menu="fx editor choice menu", skins_dir=None):
    if kind in ("bar", "toggle", "list", "group", "action", "delete"):
        return build_cell_definition("action" if kind == "delete" else kind, w, h, skins_dir=skins_dir, choice_menu=choice_menu)
    actions = []
    if kind.startswith("mode"):
        mode = native_button("mode", (0, 0, w, h), f"mod_mode_{kind}_on.png",
                             f"mod_mode_{kind}_off.png", index=int(kind[4:]), count=4)
        mode["bounds"]["acceptsHWFocus"] = "No"
        children = [mode]
    elif kind in ("dial_uni", "dial_bi"):
        dial_size = _style_size("dial.diameter")
        dial_x = (w - dial_size) // 2
        name_h, val_h = _style_size("mod_row.build_editor_cell_definition.label_height"), _style_size("mod_row.build_editor_cell_definition.label_height")
        knob_y = name_h + max(0, (h - name_h - val_h - dial_size) // 2)
        dial_data = pan_dial_data if kind == "dial_bi" else level_dial_data
        knob_data = dial_data(skins_dir, frame_size=dial_size)
        knob_data.update(dragOrientation="Vertical", handleName="Data")
        control = _entry("knob", "Knob", knob_data, (dial_x, knob_y, dial_size, dial_size), focus="Yes")
        name_lbl = editor_label("Name", (_style_size("mod_row.build_editor_cell_definition.padding"), _style_size("mod_row.build_editor_cell_definition.padding"), w - _style_size("mod_row.build_editor_cell_definition.double_padding"), name_h), just="horizontallyCentred verticallyCentred")
        val_lbl = editor_label("Value", (_style_size("mod_row.build_editor_cell_definition.padding"), knob_y + dial_size + _style_size("mod_row.build_editor_cell_definition.padding"), w - _style_size("mod_row.build_editor_cell_definition.double_padding"), val_h), just="horizontallyCentred verticallyCentred")
        children = [name_lbl, control, val_lbl]
    else:
        raise ValueError(f"Unsupported native modulation editor cell: {kind}")
    default = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    return {"version": 2, "actions": actions,
            "backgroundData": {"version": 1, "focussed": dict(default, colour=_style_argb("mod_row.focus")), "unfocussed": default},
            "ignoreMousePresses": False, "disableCoarseDataWheel": kind in ("action", "list", "group"), "componentsData": children}
