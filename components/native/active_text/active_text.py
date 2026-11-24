# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native ActiveText (állapotszöveg) component for Akai MPC standalone UI.

Owner specification (2026-11-24):
- Text element indicating on/off state via color:
  - OFF: inactive gray (token: active_text.off)
  - ON: shared active status color (token: active_text.on)
- Text label remains unchanged between states.
- Outward-facing state API: single active on/off input (handle: Active).
- No internal toggle-truth or separate state palette; host/screen remains the owner.
- Standalone portable component: depends only on components.style.
"""

import hashlib
from pathlib import Path

from components.style import (
    argb as _style_argb,
    color as _style_color,
    native_color as _style_native_color,
    opacity as _style_opacity,
    rgba as _style_rgba,
    size as _style_size,
)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

FONT_FILE = Path(__file__).resolve().parent / "img" / _style_size("font.file.semibold")


def semibold(size):
    """Load the shared Titillium Web SemiBold font at the specified size."""
    if ImageFont is None or not FONT_FILE.is_file():
        return None
    return ImageFont.truetype(str(FONT_FILE), size)


def render_active_text_art(
    text,
    active,
    width=None,
    height=None,
    *,
    font_size=None,
    scale=None,
    anchor=None,
):
    """Render antialiased plain text in active or inactive state color on a transparent background."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_active_text_art")
    width = width if width is not None else _style_size("active_text.default.width")
    height = height if height is not None else _style_size("active_text.default.height")
    scale = scale if scale is not None else _style_size("active_text.scale")
    font_size = font_size if font_size is not None else _style_size("active_text.font")

    w, h = width * scale, height * scale
    image = Image.new("RGBA", (w, h), _style_rgba("ink.black", _style_opacity("transparent")))
    draw = ImageDraw.Draw(image)

    font = semibold(font_size * scale)
    fill_color = _style_color("active_text.on") if active else _style_color("active_text.off")

    effective_anchor = anchor or ("mm" if width > _style_size("active_text.default.width") else "lm")
    pos = (w / 2, h / 2) if effective_anchor == "mm" else (0, h / 2)
    draw.text(pos, text, font=font, fill=fill_color, anchor=effective_anchor)
    if scale == 1:
        return image
    return image.resize((width, height), Image.Resampling.LANCZOS)


def render_active_text_preview(text="SMOOTH", width=None, height=None, output_path=None):
    """Render a side-by-side comparison of active (ON) and inactive (OFF) states."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_active_text_preview")
    width = width if width is not None else _style_size("active_text.default.width")
    height = height if height is not None else _style_size("active_text.default.height")

    im_on = render_active_text_art(text, True, width, height)
    im_off = render_active_text_art(text, False, width, height)

    pad = _style_size("active_text.preview.pad")
    card_w = width * 2 + pad * 3
    card_h = height + pad * 2

    canvas = Image.new("RGBA", (card_w, card_h), _style_rgba("catalog.card"))
    canvas.alpha_composite(im_on, (pad, pad))
    canvas.alpha_composite(im_off, (pad * 2 + width, pad))

    if output_path is not None:
        out = Path(output_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        canvas.save(out)
    return canvas


def write_active_text_assets(
    directory,
    text="SMOOTH",
    width=None,
    height=None,
    *,
    on_name="active_text_on.png",
    off_name="active_text_off.png",
):
    """Write ON and OFF state image assets to the specified skin directory."""
    dir_path = Path(directory)
    dir_path.mkdir(parents=True, exist_ok=True)
    img_on = render_active_text_art(text, True, width=width, height=height)
    img_off = render_active_text_art(text, False, width=width, height=height)
    img_on.save(dir_path / on_name)
    img_off.save(dir_path / off_name)
    return on_name, off_name


def active_text_data(on_image, off_image, handle="Active"):
    """MPC Button data dictionary bound to the Active state handle."""
    return {
        "version": 2,
        "onImage": on_image,
        "offImage": off_image,
        "buttonId": 1,
        "numButtonsInGroup": 1,
        "handleName": handle,
        "gestureBehaviour": "Instant",
    }


def active_text_child(name, on_image, off_image, rect, handle="Active"):
    """MPC Button child component for embedding inside composite components (e.g. dials)."""
    bounds_str = "%d %d %d %d" % tuple(rect) if isinstance(rect, (tuple, list)) else str(rect)
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Button",
            "data": active_text_data(on_image, off_image, handle=handle),
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": bounds_str,
        },
    }


def active_text_entry(name, on_image, off_image, rect, handle="Active", gates=()):
    """Top-level MPC page entry for ActiveText."""
    bounds_str = "%d %d %d %d" % tuple(rect) if isinstance(rect, (tuple, list)) else str(rect)
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": "No",
        "showWhenDataModelInvalid": "Show",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": bounds_str,
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Button",
            "data": active_text_data(on_image, off_image, handle=handle),
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": bounds,
    }


def active_text_definition(name, on_image, off_image, size, handle="Active", actions=None):
    """Complete native component definition dictionary for standalone registration."""
    w, h = size
    child = active_text_child("button", on_image, off_image, (0, 0, w, h), handle=handle)
    bg = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    return {
        "version": 2,
        "actions": list(actions or []),
        "backgroundData": {"version": 1, "focussed": bg, "unfocussed": bg},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": True,
        "componentsData": [child],
    }


def build_titled_active_text_group_definition(
    name,
    title,
    items,
    size=None,
    *,
    skins_dir=None,
    title_rect=None,
    title_handle="Caption",
    title_font_size=None,
    font_size=None,
    actions=None,
    asset_prefix=None,
):
    """Additive public factory producing a titled ActiveText composite definition.

    Composes a caller title label with arbitrary independent ActiveText buttons.
    Each child button is an independent toggle bound to its own handle (default: Active0, Active1...),
    operating as disjoint >= 56px touch targets with gray OFF and cyan ON state colours.
    The composite definition is passive: actions list is empty, with zero double-parent actions,
    and ignoreMousePresses is True so child button interactions are preserved directly.

    Args:
        name: Name of the component definition.
        title: Caller title text string, or None for dynamic Name label.
        items: List of child button specifications. Each entry is a dict:
               - 'label' / 'caption': button text (e.g. 'OSC A')
               - 'rect': (x, y, w, h) bounds relative to the group
               - 'handle' (optional): per-child state handle (defaults to f'Active{i}')
               - 'name' (optional): child component name (defaults to f'{name}_btn_{i}')
               - 'on_image' (optional): asset filename for ON state
               - 'off_image' (optional): asset filename for OFF state
        size: Optional (width, height) bounding box of the group.
        skins_dir: Optional directory to write generated button PNG assets.
        title_rect: Bounding rect for the title label (x, y, w, h).
        title_handle: Handle name for dynamic title if title is None (default: 'Caption').
        title_font_size: Font size for title label (default: font.label / 14.0).
        font_size: Font size for button text (default: font.selector / 19.0).
        actions: Definition actions list (must be empty or None for passive group).
        asset_prefix: Prefix for generated asset filenames (default: name).
    """
    if not items:
        raise ValueError("items must not be empty")

    if actions:
        raise ValueError("Titled ActiveText group is passive and does not accept actions")

    min_hit = int(_style_size("height.high"))
    seen_handles = set()

    for i, item in enumerate(items):
        child_rect = item["rect"]
        if len(child_rect) != 4:
            raise ValueError(f"Button {i} rect must have 4 elements: (x, y, w, h)")
        child_x, child_y, child_w, child_h = child_rect
        if child_x < 0 or child_y < 0:
            raise ValueError(f"Button '{item.get('label', i)}' coordinates ({child_x}, {child_y}) must be non-negative")
        if child_w < min_hit or child_h < min_hit:
            raise ValueError(f"Button '{item.get('label', i)}' dimensions ({child_w}x{child_h}) must be >= {min_hit}px (height.high)")
        if size is not None:
            group_w, group_h = size
            if child_x + child_w > group_w or child_y + child_h > group_h:
                raise ValueError(f"Button '{item.get('label', i)}' bounds {child_rect} exceed group size {size}")

        child_handle = item.get("handle", f"Active{i}")
        if child_handle in seen_handles:
            raise ValueError(f"Duplicate handle '{child_handle}': each child button must have a unique independent handle")
        seen_handles.add(child_handle)

    def _rects_overlap(r1, r2):
        x1, y1, w1, h1 = r1
        x2, y2, w2, h2 = r2
        return not (x1 + w1 <= x2 or x2 + w2 <= x1 or y1 + h1 <= y2 or y2 + h2 <= y1)

    for i in range(len(items)):
        r1 = items[i]["rect"]
        for j in range(i + 1, len(items)):
            r2 = items[j]["rect"]
            if _rects_overlap(r1, r2):
                raise ValueError(f"Button rects for '{items[i].get('label', i)}' ({r1}) and '{items[j].get('label', j)}' ({r2}) overlap")

    if title_rect is not None:
        t_rect = title_rect
    elif size is not None:
        t_rect = (0, 0, size[0], _style_size("active_text.default.height"))
    else:
        max_w = max(item["rect"][0] + item["rect"][2] for item in items)
        t_rect = (0, 0, max_w, _style_size("active_text.default.height"))

    if t_rect[2] <= 0 or t_rect[3] <= 0:
        raise ValueError(f"Title rect dimensions ({t_rect[2]}, {t_rect[3]}) must be positive")

    for i, item in enumerate(items):
        if _rects_overlap(item["rect"], t_rect):
            raise ValueError(f"Button '{item.get('label', i)}' rect ({item['rect']}) overlaps with title rect ({t_rect})")

    children = []
    prefix = asset_prefix or name
    skins = Path(skins_dir) if skins_dir is not None else None
    if skins is not None:
        skins.mkdir(parents=True, exist_ok=True)

    btn_font = font_size if font_size is not None else _style_size("font.selector")

    for i, item in enumerate(items):
        child_rect = item["rect"]
        child_w, child_h = child_rect[2], child_rect[3]
        label = item.get("label", item.get("caption", ""))
        child_handle = item.get("handle", f"Active{i}")
        child_name = item.get("name", f"button_{i}")

        on_img = item.get("on_image")
        off_img = item.get("off_image")
        if not on_img or not off_img:
            if skins is None:
                raise ValueError(f"skins_dir is required to generate missing assets for item '{label}'")
            clean_stem = label.lower().replace(" ", "_").replace("/", "_").replace(".", "_")
            digest = hashlib.sha1(f"{label}:{child_w}x{child_h}:{btn_font}".encode("utf-8")).hexdigest()[:8]
            if not on_img:
                on_img = f"{prefix}_{clean_stem}_{digest}_on.png"
                render_active_text_art(label, True, child_w, child_h, font_size=btn_font).save(skins / on_img)
            if not off_img:
                off_img = f"{prefix}_{clean_stem}_{digest}_off.png"
                render_active_text_art(label, False, child_w, child_h, font_size=btn_font).save(skins / off_img)

        child_button = active_text_child(child_name, on_img, off_img, child_rect, handle=child_handle)
        children.append(child_button)

    t_font = float(title_font_size) if title_font_size is not None else float(_style_size("font.label"))
    title_component = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "title",
            "type": "Label",
            "data": {
                "version": 1,
                "textStyle": {
                    "version": 1,
                    "font": {
                        "version": 1,
                        "name": _style_size("font.family"),
                        "style": _style_size("font.style.semibold"),
                        "height": t_font,
                    },
                    "colour": _style_argb("text.caption"),
                    "justification": "horizontallyCentred verticallyCentred",
                    "case": "Original",
                },
                "type": "Text" if title is not None else "Name",
                "handleName": title_handle,
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % tuple(t_rect),
        },
    }
    if title is not None:
        title_component["componentData"]["data"]["text"] = title

    bg = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": bg, "unfocussed": bg},
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": [title_component] + children,
    }


titled_active_text_group_definition = build_titled_active_text_group_definition


def titled_active_text_group_entry(
    name,
    definition_type,
    rect,
    mappings=(),
    gates=(),
    focus="Yes",
):
    """Top-level MPC page entry for a titled ActiveText group instance.

    Args:
        name: Instance name on the page/tab (e.g. 'Voice Target Scope').
        definition_type: Registered component definition name (e.g. 'voiceNativeTargets').
        rect: (x, y, w, h) bounding box on the tab.
        mappings: Iterable of (handle_key, parameter_target) tuples,
                  e.g. [('ActiveA', 'Parameter 2676'), ('ActiveB', 'Parameter 2677'), ...].
        gates: Additional invalidating handles (e.g. root_gate).
        focus: Accepts HW focus ('Yes' or 'No').
    """
    bounds = {
        "version": 2 if gates else 1,
        "acceptsHWFocus": focus,
        "showWhenDataModelInvalid": "Hide" if focus == "Yes" else "Show",
        "whenVisible": "Always",
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % tuple(rect),
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": definition_type,
            "data": {"version": 1, "handleName": "Data"},
        },
        "handle remapping": {
            "version": 1,
            "map": [{"key": k, "value": v} for k, v in mappings],
        },
        "bounds": bounds,
    }


__all__ = [
    "render_active_text_art",
    "render_active_text_preview",
    "write_active_text_assets",
    "active_text_data",
    "active_text_child",
    "active_text_entry",
    "active_text_definition",
    "build_titled_active_text_group_definition",
    "titled_active_text_group_definition",
    "titled_active_text_group_entry",
]
