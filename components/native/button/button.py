# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC button construction, action-button components and Filter tab navigation."""
import hashlib
import math
from pathlib import Path

from components.style import (
    color as _style_color,
    size as _style_size,
)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

FONT_FILE = Path(__file__).resolve().parent / "img" / _style_size('font.file.semibold')


TRANSPARENT_HIT = "native_button_hit.png"


def write_transparent_hit(directory):
    """A native Button takes its hit area from bounds; one transparent pixel is sufficient."""
    path = Path(directory) / TRANSPARENT_HIT
    if not path.is_file():
        path.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", (1, 1)).save(path)
    return TRANSPARENT_HIT


def semibold(size):
    if ImageFont is None or not FONT_FILE.is_file():
        return None
    return ImageFont.truetype(str(FONT_FILE), size)


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


def _store_action_face(ctx, rect, *, icon=None, text=None, width=None, colour=None):
    ink = {} if colour is None else {"colour": colour}
    image = in_slot(action_art(icon=icon, text=text, width=width, **ink), tuple(rect[2:]))
    digest = hashlib.sha256(str(image.size).encode() + image.tobytes()).hexdigest()[:20]
    filename = f"native_action_{digest}.png"
    skins_dir = getattr(ctx, "skins_dir", ctx)
    image.save(Path(skins_dir) / filename)
    return filename


def action_button(ctx, name, rect, action, *, icon=None, text=None, width=None, gates=(), colour=None):
    """Bind one immediate MPC action to a centered icon or label."""
    if not action:
        raise ValueError("An action handle is required")
    filename = _store_action_face(ctx, rect, icon=icon, text=text, width=width, colour=colour)
    remaps = () if action == "Data" else (("Data", action),)
    return button(name, rect, filename, remaps=remaps, gates=gates)


def mode_button(ctx, name, rect, handle, *, on_text, off_text, gates=()):
    width = rect[2]
    on = _store_action_face(ctx, rect, text=on_text, width=width, colour=_style_color("accent.cyan"))
    off = _store_action_face(ctx, rect, text=off_text, width=width)
    return button(name, rect, on, off, remaps=(("Data", handle),), gates=gates)


def icon_button(symbol, colour):
    return in_slot(action_art(icon=symbol, colour=colour), (_style_size('button.icon_button.in_slot'), _style_size('button.icon_button.in_slot.offset1')))


# -------------------------------------------------------------------------
# FilterPower variant API
# -------------------------------------------------------------------------

def filter_power_param_key(filter_num):
    """The engine parameter key for filter power: 'filter1.on' / 'filter2.on'.

    Note: In current source the parameter key is 'filterN.on' (docs/params.json,
    core/include/synthyboy/params.h), NOT 'filterN.power'.
    """
    return "filter%d.on" % filter_num


def sanitize_tab_navigation(tab, filter_power_params):
    """Ensures standalone filter tab navigation is not locked out when power is OFF.

    Strips any IndexedEnabling handle pointing to filter power parameters from the tab
    or its function key definitions.
    """
    for param_idx in filter_power_params.values():
        if param_idx < 0:
            continue
        target_suffix = "Parameter %d" % param_idx
        # Strip from tab invalidating handles
        handles = tab.get("additionalInvalidatingHandles", [])
        if handles:
            tab["additionalInvalidatingHandles"] = [
                h for h in handles if not h.endswith(target_suffix)
            ]
        # Also check componentDefinition if present
        comp_def = tab.get("componentDefinition", {})
        cd_handles = comp_def.get("additionalInvalidatingHandles", [])
        if cd_handles:
            comp_def["additionalInvalidatingHandles"] = [
                h for h in cd_handles if not h.endswith(target_suffix)
            ]


__all__ = [
    "FONT_FILE",
    "TRANSPARENT_HIT",
    "write_transparent_hit",
    "semibold",
    "action_art",
    "in_slot",
    "entry",
    "button",
    "action_button",
    "icon_button",
    "filter_power_param_key",
    "sanitize_tab_navigation",
]


def text_face(label, size, *, geometry, surface, border, text):
    """Token-configured native action face for compact fixed-layout toolbars."""
    metrics = _style_size(geometry)
    width, height = size
    image = Image.new("RGBA", size)
    pen = ImageDraw.Draw(image)
    pen.rounded_rectangle((metrics["inset"], metrics["inset"],
                           width - metrics["end_inset"], height - metrics["end_inset"]),
                          radius=metrics["radius"], fill=_style_color(surface),
                          outline=_style_color(border), width=metrics["stroke"])
    pen.text((width / 2, height / 2), label, anchor="mm", font=semibold(metrics["font"]),
             fill=_style_color(text))
    return image


def write_switch_asset(path, active):
    """Shared native switch face used only when an SDK switch asset is absent."""
    geometry = _style_size("button.switch.face")
    width, height = geometry["size"]
    image = Image.new("RGBA", (width, height))
    pen = ImageDraw.Draw(image)
    pen.rounded_rectangle((0, 0, width - 1, height - 1), radius=geometry["radius"],
                           fill=_style_color("accent.cyan" if active else "button.surface"),
                           outline=_style_color("button.switch.border"), width=geometry["stroke"])
    x = geometry["thumb_on_x" if active else "thumb_off_x"]
    y, size = geometry["thumb_y"], geometry["thumb_size"]
    pen.ellipse((x, y, x + size, y + size), fill=_style_color("text.icon"))
    image.save(path)
