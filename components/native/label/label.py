# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""LABEL: the read-only text styles of the UI, one picture per variant (catalogue only, no screen uses this module).

Every variant is a font token (`font.*`, or `token:key` for a field of a dict token), a colour token (`text.*`) and an
optional case, drawn with the shared Titillium Web SemiBold exactly as the native Label components write them. Font
tokens are TUI heights (the host draws em = height / font.host.height.per.em); `font.page_title` is an em (the overlay's
page-title size), so a variant states its `unit`. Docs: docs/ui-components/label.md.
"""
import math
from pathlib import Path

from components.native.level.level import render_dial_frame
from components.style import color as _style_color, native_font_em, size as _style_size

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

UNITS = ("tui", "em")
CASES = ("Original", "Upper Case")
VARIANT_KEYS = {"id", "label", "text", "font", "unit", "colour", "case", "file", "alt"}
DIAL_SIZE_KEYS = {"id", "label", "token", "file", "alt"}
DIAL_LAYOUT_STATE = 0.5


def font_value(spec):
    """The number behind a font spec: a scalar size token, or `token:key` for a field of a dict token."""
    token, _, key = spec.partition(":")
    value = _style_size(token)
    if key:
        value = value[key]
    if type(value) not in (int, float):
        raise ValueError(f"font spec is not a number: {spec!r}")
    return value


def _font(em):
    path = Path(__file__).with_name("img") / _style_size("font.file.semibold")
    return ImageFont.truetype(str(path), max(1, round(em)))


def render_label(text, font, colour, *, unit="tui", case="Original"):
    """The text on a transparent picture of exactly its width (ink box) and line height (ascent + descent)."""
    if Image is None:
        raise RuntimeError("Pillow is required to render labels")
    if unit not in UNITS or case not in CASES:
        raise ValueError(f"label unit must be one of {UNITS} and case one of {CASES}")
    size = font_value(font) if isinstance(font, str) else font
    pen_font = _font(size if unit == "em" else native_font_em(size))
    text = text.upper() if case == "Upper Case" else text
    ascent, descent = pen_font.getmetrics()
    left, _, right, _ = ImageDraw.Draw(Image.new("RGBA", (1, 1))).textbbox((0, 0), text, font=pen_font, anchor="ls")
    image = Image.new("RGBA", (math.ceil(right - left), ascent + descent))
    ImageDraw.Draw(image).text((-left, ascent), text, font=pen_font, fill=_style_color(colour), anchor="ls")
    return image


def label_variants(entry):
    """The variants of a catalogue "label" entry, validated; each gains `size` (the resolved font number)."""
    if not isinstance(entry, dict) or not {"variants"} <= set(entry) <= {"variants", "dial_sizes"} or not entry["variants"]:
        raise ValueError("label entry needs variants and may add dial_sizes")
    result = []
    for variant in entry["variants"]:
        if set(variant) != VARIANT_KEYS or variant["unit"] not in UNITS or variant["case"] not in CASES:
            raise ValueError(f"label variant needs exactly {sorted(VARIANT_KEYS)}, unit in {UNITS}, case in {CASES}")
        result.append(dict(variant, size=font_value(variant["font"])))
    if len({v["id"] for v in result}) != len(result):
        raise ValueError("label variant ids must be unique")
    return result


def dial_sizes(entry):
    """The dial-size series of a catalogue "label" entry: the caption and value sizes (TUI heights) next to each dial.

    A token is {diameter, caption, value, proposal}; `proposal` marks sizes that are a design proposal, not in use yet.
    """
    result = []
    for item in entry.get("dial_sizes", []):
        if set(item) != DIAL_SIZE_KEYS:
            raise ValueError(f"label dial size needs exactly {sorted(DIAL_SIZE_KEYS)}")
        token = _style_size(item["token"])
        if set(token) != {"diameter", "caption", "value", "proposal"} or not isinstance(token["proposal"], bool):
            raise ValueError(f"dial size token needs diameter, caption, value, proposal: {item['token']}")
        result.append(dict(item, **token))
    return result


def render_dial_with_text(diameter, caption_height, value_height, caption="LEVEL", value="50%"):
    """The Level dial (render_dial_frame) with its caption and value to the right: caption on top, value below."""
    gap, line_gap = _style_size("gap.column"), _style_size("gap.field")
    top = render_label(caption, caption_height, "text.caption", case="Upper Case")
    bottom = render_label(value, value_height, "text.value")
    dial = render_dial_frame(diameter, DIAL_LAYOUT_STATE, bipolar=False)
    block = top.height + line_gap + bottom.height
    canvas = Image.new("RGBA", (diameter + gap + max(top.width, bottom.width), max(diameter, block)))
    canvas.alpha_composite(dial, (0, (canvas.height - diameter) // 2))
    y = (canvas.height - block) // 2
    canvas.alpha_composite(top, (diameter + gap, y))
    canvas.alpha_composite(bottom, (diameter + gap, y + top.height + line_gap))
    return canvas


def catalog_sections(entry):
    """The catalogue sections of a "label" entry: the variants, then the sizes next to a dial."""
    sections = [{"images": [
        {"presentation": "sample", "file": v["file"], "alt": v["alt"],
         "caption": f"{v['label']} · {v['font']} = {v['size']} ({v['unit'].upper()}) · {v['colour']}"
                    + (" · nagybetű" if v["case"] == "Upper Case" else "")}
        for v in label_variants(entry)]}]
    sizes = dial_sizes(entry)
    if sizes:
        sections.append({"heading": "Felirat- és értékméret a tárcsa mellett (jelölve: javaslat)", "images": [
            {"presentation": "sample", "file": s["file"], "alt": s["alt"],
             "caption": f"{s['label']} · tárcsa {s['diameter']} px · felirat {s['caption']} · érték {s['value']} (TUI)"
                        + (" · JAVASLAT" if s["proposal"] else " · ma használt")}
            for s in sizes]})
    return sections
