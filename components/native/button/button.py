# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native MPC button construction, action-button components and Filter tab navigation."""
import hashlib
import math
from pathlib import Path

from components.style import (
    color as _style_color,
    control_look as _control_look,
    rgb as _style_rgb,
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


def gradient_face(size, *, inset, radius, frame, fill, line, span):
    """The shared Play-panel control box: a rounded box with a vertical gradient fill and a keyline, drawn at the size given.

    fill and line are (top, bottom) RGB pairs: the fill is constant down to span["start"] (a fraction of the box
    height), darkens linearly to span["end"] and is constant below; the keyline follows the same ramp when its two
    colours differ. Used by the dropdown (selector.render_dropdown_face) and the button look "shaded" (action_art),
    so both share control.gradient. `size`, `inset`, `radius` and `frame` are in the caller's (supersampled) pixels.
    """
    mask = Image.new("L", size)
    box = (inset, inset, size[0] - inset - 1, size[1] - inset - 1)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius, fill=255)
    face = Image.new("RGBA", size)
    pen = ImageDraw.Draw(face)
    top, bottom = fill
    line_top, line_bottom = line

    def blend(y):
        return min(1., max(0., ((y - inset) / max(1, box[3] - inset) - span["start"]) / (span["end"] - span["start"])))

    for y in range(box[1], box[3] + 1):
        t = blend(y)
        pen.line((box[0], y, box[2], y), fill=tuple(round(lo + t * (hi - lo)) for lo, hi in zip(top, bottom)))
    face.putalpha(mask)
    if line_top == line_bottom:
        pen.rounded_rectangle(box, radius, outline="#%02x%02x%02x" % tuple(line_top), width=frame)
    else:
        # The keyline darkens down the box with the fill (bright top edge, dark bottom edge).
        outline = Image.new("L", size)
        ImageDraw.Draw(outline).rounded_rectangle(box, radius, outline=255, width=frame)
        ramp = Image.new("RGBA", size)
        ramp_pen = ImageDraw.Draw(ramp)
        for y in range(box[1], box[3] + 1):
            t = blend(y)
            ramp_pen.line((0, y, size[0], y), fill=tuple(round(lo + t * (hi - lo)) for lo, hi in zip(line_top, line_bottom)) + (255,))
        face.paste(ramp, (0, 0), outline)
    return face, pen


def semibold(size):
    if ImageFont is None or not FONT_FILE.is_file():
        return None
    return ImageFont.truetype(str(FONT_FILE), size)


LOOKS = ("flat", "shaded", "active", "ghost")


def action_art(*, icon=None, text=None, width=None, colour=_style_color("text.icon"), height=None, look=None):
    """A native action button face: a text label or an icon (power, expand, close) on its box.

    height: None keeps the 48 px face every skin button uses; 32 / 40 / 48 / 56 take their `button.size.<px>`
            tokens (font, radius, text padding) and scale the icon about the box centre.
    look:   None     the skin default: `control.look` (shaded; SYNTHYBOY_CONTROL_LOOK=flat gives the flat face back);
            "flat"   the flat face (button.surface / button.border);
            "shaded" the old Play-panel face: the control.gradient fill and keyline (button.gradient.* colours);
            "active" the shaded gradient shape filled with the accent (accent.cyan to radio.active.bottom, keyline
                     radio.active.border); the caller passes colour=radio.active.text (RadioGrid's selected button);
            "ghost"  no face at all (no fill, no keyline): the icon or text only, on a transparent picture.
    """
    if Image is None:
        raise RuntimeError("Pillow is required for action_art")
    if bool(icon) == bool(text):
        raise ValueError("Supply exactly one icon or text label")
    look = _control_look() if look is None else look
    if look not in LOOKS:
        raise ValueError(f"look must be one of {LOOKS}: {look!r}")
    art = _style_size('button.art')
    scale = art['scale']
    measures = dict(art) if height is None else dict(art, **_style_size(f"button.size.{height}"))
    box_height, factor = measures['height'], measures['height'] / art['height']
    font = semibold(measures['font'] * scale)
    minimum = box_height if icon else math.ceil(font.getlength(text) / scale) + measures['padding']
    width = minimum if width is None else width
    if width < minimum or (icon and width != box_height):
        raise ValueError("Button width does not fit its content")
    size = (width * scale, box_height * scale)
    if look in ("shaded", "active"):
        if look == "shaded":
            fill = (_style_rgb("button.gradient.top"), _style_rgb("button.gradient.bottom"))
            line = (_style_rgb("button.gradient.border"), _style_rgb("button.gradient.border.bottom"))
        else:   # the selected face of a group: the accent fill (the radio.active tokens) on the same gradient shape
            fill = (_style_rgb("accent.cyan"), _style_rgb("radio.active.bottom"))
            line = (_style_rgb("radio.active.border"),) * 2
        image, pen = gradient_face(
            size, inset=0, radius=measures['radius'] * scale, frame=art['outline'] * scale,
            span=_style_size("control.gradient"), fill=fill, line=line)
    else:
        image = Image.new("RGBA", size)
        pen = ImageDraw.Draw(image)
    if look == "flat":
        pen.rounded_rectangle(
            (0, 0, image.width - art['outline'], image.height - art['outline']),
            radius=measures['radius'] * scale,
            fill=_style_color("button.surface"), outline=_style_color("button.border"), width=art['outline'] * scale,
        )

    def stroke(points, stroke_width):
        ox, oy = art['origin']
        if height is None:
            points = [(round((x - ox) * scale), round((y - oy) * scale)) for x, y in points]
        else:
            # The 48 px icon geometry, scaled about the box centre.
            half, centre = art['height'] / 2, box_height / 2
            points = [(round(((x - ox - half) * factor + centre) * scale), round(((y - oy - half) * factor + centre) * scale))
                      for x, y in points]
            stroke_width = stroke_width * factor
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
    elif icon == "close":
        # An X: two diagonals, each `half` (40% of the 48 px button height, in all) around the centre; stroke as EXPAND.
        geometry = art['close']
        cx, cy = geometry['center']
        half = geometry['half']
        stroke([(cx - half, cy - half), (cx + half, cy + half)], geometry['stroke'])
        stroke([(cx + half, cy - half), (cx - half, cy + half)], geometry['stroke'])
    elif text:
        pen.text((image.width / 2, image.height / 2), text, font=font, fill=colour, anchor="mm")
    else:
        raise ValueError(icon)
    return image.resize((width, box_height), Image.Resampling.LANCZOS)


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


def text_face(label, size, *, geometry, surface, border, text, gradient=False, accent=None):
    """Token-configured native action face for compact fixed-layout toolbars.

    gradient: the face follows the skin's control look (`control.look`): "shaded" draws the shared button gradient
    (button.gradient.*) instead of the flat surface/border tokens; `accent` (a colour token) then overrides the
    gradient keyline, e.g. the active route button. Disabled faces pass gradient=False and stay flat grey."""
    metrics = _style_size(geometry)
    width, height = size
    if gradient and _control_look() == "shaded":
        scale = _style_size("sampling.standard")
        line = (_style_rgb("button.gradient.border"), _style_rgb("button.gradient.border.bottom"))
        if accent is not None:
            line = (_style_rgb(accent),) * 2
        image, pen = gradient_face(
            (width * scale, height * scale), inset=0, radius=metrics["radius"] * scale, frame=metrics["stroke"] * scale // 2,
            span=_style_size("control.gradient"), line=line,
            fill=(_style_rgb("button.gradient.top"), _style_rgb("button.gradient.bottom")))
        image = image.resize(size, Image.Resampling.LANCZOS)
        pen = ImageDraw.Draw(image)
    else:
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
    shaded = _control_look() == "shaded"
    pen.rounded_rectangle((0, 0, width - 1, height - 1), radius=geometry["radius"],
                           fill=_style_color("accent.cyan" if active else ("button.gradient.top" if shaded else "button.surface")),
                           outline=_style_color("button.gradient.border" if shaded else "button.switch.border"),
                           width=geometry["stroke"])
    x = geometry["thumb_on_x" if active else "thumb_off_x"]
    y, size = geometry["thumb_y"], geometry["thumb_size"]
    pen.ellipse((x, y, x + size, y + size), fill=_style_color("text.icon"))
    image.save(path)


SAMPLE_KEYS = {"id", "file", "alt", "caption", "height", "text", "state"}
SAMPLE_STATES = ("enabled", "disabled")


def render_sample(sample):
    """One catalogue picture: the text button in the shaded look at its height token; disabled = dim_artwork of it."""
    from components.style import dim_artwork
    height = sample["height"] if isinstance(sample["height"], int) else _style_size(sample["height"])
    face = action_art(text=sample["text"], height=height, look="shaded")
    return dim_artwork(face) if sample["state"] == "disabled" else face


def button_samples(entry):
    """The samples of a catalogue "button" entry, validated: id, file, alt, caption, height (a height token), text, state."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("button entry needs a nonempty samples list")
    for item in entry["samples"]:
        if set(item) != SAMPLE_KEYS or item["state"] not in SAMPLE_STATES:
            raise ValueError(f"button sample needs exactly {sorted(SAMPLE_KEYS)}, state in {SAMPLE_STATES}")
        _style_size(f"button.size.{_style_size(item['height'])}")
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("button sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    return [{"heading": "Méret szerint egy-egy minta (régi külső: átmenetes kitöltés, világosabb kék keret)", "images": [
        {"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]}
        for i in button_samples(entry)]}]
