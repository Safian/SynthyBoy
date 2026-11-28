# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ICON BUTTON: the Button recipe with an icon where the text is, square (width = height), catalogue pictures.

The face is `button.action_art(icon=..., height=..., look=...)`: look "filled" is the Button's shaded face, "ghost"
has no fill and no keyline (icon only, transparent). Icons: the recipes POWER and CLOSE (an X). States: power on
(accent.green) / off (text.icon), disabled (the disabled look of style.dim_artwork). Docs: docs/ui-components/icon_button.md.
"""
from PIL import Image, ImageDraw
from components.native.button.button import action_art
from components.style import color as _style_color, dim_artwork, size as _style_size

ICONS = ("power", "close")
GLYPHS = ("back_arrow", "row_arrow")
STATES = ("on", "off", "disabled")
LOOKS = ("filled", "ghost")
SAMPLE_KEYS = {"id", "file", "alt", "caption", "icon", "height", "state", "look"}
GLYPH_KEYS = {"id", "file", "skin_file", "alt", "caption", "glyph", "box", "glyph_size", "stroke"}


def render_glyph(entry):
    """One transparent list-arrow glyph drawn with the supersampled stroke style of button.action_art."""
    box = entry["box"]
    glyph = entry["glyph"]
    span = entry["glyph_size"]
    stroke_w = entry["stroke"]
    scale = _style_size("button.art")["scale"]
    colour = _style_color("text.icon")
    image = Image.new("RGBA", (box * scale, box * scale))
    pen = ImageDraw.Draw(image)
    cx = cy = (box / 2.0) * scale
    half = (span / 2.0) * scale
    arm = (span * 0.35) * scale
    w = round(stroke_w * scale)
    cap = w / 2.0

    def draw_seg(pts):
        pen.line(pts, fill=colour, width=w, joint="curve")
        for x, y in pts:
            pen.ellipse((x - cap, y - cap, x + cap, y + cap), fill=colour)

    if glyph == "back_arrow":
        tip = (cx - half, cy)
        draw_seg([(cx + half, cy), tip])
        draw_seg([(cx - half + arm, cy - arm), tip, (cx - half + arm, cy + arm)])
    elif glyph == "row_arrow":
        tip = (cx + half, cy)
        draw_seg([(cx - half, cy), tip])
        draw_seg([(cx + half - arm, cy - arm), tip, (cx + half - arm, cy + arm)])
    else:
        raise ValueError(f"unknown glyph: {glyph!r}")
    return image.resize((box, box), Image.Resampling.LANCZOS)


def glyph_samples(catalog):
    """The arrow glyph entries of a catalogue JSON, validated."""
    items = catalog.get("glyphs", [])
    for item in items:
        if set(item) != GLYPH_KEYS or item["glyph"] not in GLYPHS:
            raise ValueError(f"glyph sample needs {sorted(GLYPH_KEYS)}, glyph in {GLYPHS}")
    return items


def render_sample(sample):
    """One icon button: square at its height token; "on" lights POWER green, "disabled" dims the off face."""
    icon, state = sample["icon"], sample["state"]
    colour = _style_color("accent.green" if (icon == "power" and state == "on") else "text.icon")
    height = sample["height"] if isinstance(sample["height"], int) else _style_size(sample["height"])
    face = action_art(icon=icon, height=height, colour=colour,
                      look="shaded" if sample["look"] == "filled" else "ghost")
    return dim_artwork(face) if state == "disabled" else face


def icon_samples(entry):
    """The samples of a catalogue "icon_button" entry, validated; a new state or size is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("icon_button entry needs a nonempty samples list")
    for item in entry["samples"]:
        if set(item) != SAMPLE_KEYS or item["icon"] not in ICONS or item["state"] not in STATES or item["look"] not in LOOKS:
            raise ValueError(f"icon button sample needs exactly {sorted(SAMPLE_KEYS)}, icon in {ICONS}, state in {STATES}, look in {LOOKS}")
        _style_size(f"button.size.{_style_size(item['height'])}")
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("icon button sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = icon_samples(entry)
    groups = (("Teli (régi külső), méret szerint", lambda i: i["look"] == "filled" and i["state"] != "disabled"),
              ("Disabled", lambda i: i["state"] == "disabled"),
              ("Ghost: kitöltés és keret nélkül, átlátszó", lambda i: i["look"] == "ghost"))
    sections = []
    for heading, pick in groups:
        images = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if pick(i)]
        if images:
            sections.append({"heading": heading, "images": images})
    return sections


def glyph_section(catalog):
    items = glyph_samples(catalog)
    if not items:
        return []
    return [{"heading": "Lista-nyilak (átlátszó)",
             "images": [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in items]}]
