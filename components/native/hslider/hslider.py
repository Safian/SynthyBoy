# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HSLIDER: a small horizontal (left-right) slider: title and value text above, slot with the contrasty fader thumb below.

Unipolar (0..100 %, a send level) or bipolar (a pan: -100 L .. +100 R, with a centre tick at 50 % of the travel). The
thumb is the contrasty fader thumb (`thumb_face`, colours fader.thumb.*); the vertical fader of the MixerStrip uses
the same face. Geometry only from components/sizes.json (hslider.*), colours from colors.json. Disabled = the
disabled look of style.dim_artwork. Docs: docs/ui-components/hslider.md.
"""
from pathlib import Path

from components.native.button.button import semibold
from components.style import dim_artwork, native_font_em, rgb as _rgb, size as _style_size

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

STATES = ("enabled", "disabled")
EXAMPLE_KEYS = {"id", "file", "alt", "caption", "title", "value", "text", "bipolar", "state"}


def thumb_face(width, height, index, *, radius, edge, line, line_inset):
    """The contrasty fader thumb: light fill (fader.thumb.fill), light keyline (fader.thumb.edge), dark index line.

    index: "horizontal" (the vertical fader's thumb) or "vertical" (the horizontal slider's thumb).
    """
    if index not in ("horizontal", "vertical"):
        raise ValueError(f"index must be horizontal or vertical: {index!r}")
    ss = _style_size("knob.supersample")
    big = Image.new("RGBA", (width * ss, height * ss))
    pen = ImageDraw.Draw(big)
    pen.rounded_rectangle((0, 0, big.width - 1, big.height - 1), radius=radius * ss, fill=_rgb("fader.thumb.fill"),
                          outline=_rgb("fader.thumb.edge"), width=edge * ss)
    inset, half = line_inset * ss, line * ss / 2
    if index == "horizontal":
        mid = big.height / 2
        pen.rectangle((inset, mid - half, big.width - inset, mid + half), fill=_rgb("fader.thumb.line"))
    else:
        mid = big.width / 2
        pen.rectangle((mid - half, inset, mid + half, big.height - inset), fill=_rgb("fader.thumb.line"))
    return big.resize((width, height), Image.Resampling.LANCZOS)


def geometry(width):
    """Where things are: slot rect, thumb size, the thumb travel (centre x of the thumb at 0 and at 1) and the centre x."""
    height, row = _style_size("hslider.height"), _style_size("hslider.text_row")
    thumb, slot = _style_size("hslider.thumb"), _style_size("hslider.slot")
    centre_y = row + (height - row) // 2
    left, right = thumb["width"] / 2, width - thumb["width"] / 2
    return {"height": height, "text_row": row, "centre_y": centre_y, "thumb": (thumb["width"], thumb["height"]),
            "slot": (0, centre_y - slot["height"] // 2, width, slot["height"]), "travel": (left, right),
            "centre_x": (left + right) / 2}


def thumb_centre_x(width, value):
    left, right = geometry(width)["travel"]
    return left + max(0.0, min(1.0, value)) * (right - left)


def render_hslider(width, title, value, text, *, bipolar=False, state="enabled"):
    """One slider on a transparent picture: width x hslider.height. value is the normalised position 0..1."""
    if Image is None:
        raise RuntimeError("Pillow is required for HSlider")
    if state not in STATES:
        raise ValueError(f"state must be one of {STATES}: {state!r}")
    g = geometry(width)
    image = Image.new("RGBA", (width, g["height"]))
    pen = ImageDraw.Draw(image)
    caption_font = semibold(round(native_font_em(_style_size("font.caption"))))
    value_font = semibold(round(native_font_em(_style_size("font.body"))))
    pen.text((0, g["text_row"] // 2), title.upper(), font=caption_font, fill=_rgb("text.caption"), anchor="lm")
    pen.text((width, g["text_row"] // 2), text, font=value_font, fill=_rgb("text.value"), anchor="rm")
    x, y, w, h = g["slot"]
    slot = _style_size("hslider.slot")
    pen.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=slot["radius"], fill=_rgb("surface.control"),
                          outline=_rgb("field.border"), width=1)
    if bipolar:
        tick = _style_size("hslider.tick")
        cx = round(g["centre_x"] - tick["width"] / 2)
        pen.rectangle((cx, g["centre_y"] - tick["height"] // 2, cx + tick["width"] - 1, g["centre_y"] + tick["height"] // 2 - 1),
                      fill=_rgb("text.muted"))
    t = _style_size("hslider.thumb")
    face = thumb_face(t["width"], t["height"], "vertical", radius=t["radius"], edge=t["edge"], line=t["line"],
                      line_inset=t["line_inset"])
    image.alpha_composite(face, (round(thumb_centre_x(width, value) - t["width"] / 2), g["centre_y"] - t["height"] // 2))
    return dim_artwork(image) if state == "disabled" else image


def render_example(example):
    return render_hslider(_style_size("hslider.width"), example["title"], example["value"], example["text"],
                          bipolar=example["bipolar"], state=example["state"])


def hslider_examples(entry):
    if not isinstance(entry, dict) or set(entry) != {"examples"} or not entry["examples"]:
        raise ValueError("hslider entry needs a nonempty examples list")
    for e in entry["examples"]:
        if set(e) != EXAMPLE_KEYS or e["state"] not in STATES or not 0 <= e["value"] <= 1:
            raise ValueError(f"hslider example needs exactly {sorted(EXAMPLE_KEYS)}, state in {STATES}, value 0..1")
    if len({e["id"] for e in entry["examples"]}) != len(entry["examples"]):
        raise ValueError("hslider example ids must be unique")
    return entry["examples"]


def render_catalog():
    import json
    directory = Path(__file__).resolve().parent
    for e in hslider_examples(json.loads((directory / "catalog.json").read_text(encoding="utf-8"))["hslider"]):
        render_example(e).save(directory / "img" / e["file"])


def catalog_sections(entry):
    return [{"heading": "Példák (a fader kontrasztos húzogatójával, 144 px széles)", "images": [
        {"presentation": "sample", "file": e["file"], "alt": e["alt"], "caption": e["caption"]}
        for e in hslider_examples(entry)]}]
