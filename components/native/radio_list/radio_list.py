# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""RADIO LIST: rows of a small round LED plus a label, exactly one row selected (lit), catalogue pictures.

Every row is the Button's shaded gradient face (`button.gradient_face`, `control.gradient`, `button.gradient.*`) at
32 / 40 / 48 px with that size's radius; the LED is `radio_list.led_ratio` (40%) of the row height, centred vertically,
`radio_list.pad_x` from the left edge; the label is the Label component (`label.render_label`, `text.value`, the font
of `button.size.<px>` as em) `radio_list.led_gap` right of the LED. Lit LED: `accent.cyan` with the `radio.active.border`
rim and a soft glow (a blurred disc of the same colour); dark LED: `surface.control` with the `control.border` rim. The
selected row carries a faint `accent.cyan` highlight. Disabled: `style.dim_artwork` of the whole list. Docs:
docs/ui-components/radio_list.md.
"""
from components.native.button.button import gradient_face
from components.native.label.label import render_label
from components.style import color as _style_color, dim_artwork, rgb as _style_rgb, size as _style_size

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    Image = ImageDraw = ImageFilter = None

ORIENTATIONS = ("vertical", "horizontal")
STATES = ("enabled", "disabled")
HEIGHTS = (32, 40, 48)      # the size set of the button / dropdown
SAMPLE_KEYS ={"id", "file", "alt", "caption", "orientation", "items", "selected", "height", "width", "state"}


def _height(token):
    height = token if isinstance(token, int) else _style_size(token)
    if height not in HEIGHTS:
        raise ValueError(f"row height must be one of {HEIGHTS}: {height!r}")
    return height


def row_layout(height, width):
    """The row geometry in final pixels: LED diameter and centre, label x (its left ink edge), row radius and font."""
    tokens, measures = _style_size("radio_list"), _style_size(f"button.size.{height}")
    diameter = round(tokens["led_ratio"] * height)
    left = tokens["pad_x"]
    return {"diameter": diameter, "led_x": left, "led_cx": left + diameter / 2, "led_cy": height / 2,
            "label_x": left + diameter + tokens["led_gap"], "radius": measures["radius"], "font": measures["font"],
            "width": width, "height": height}


def _led(size, layout, lit, scale):
    """The LED (and its glow when lit) of one row as a supersampled RGBA layer."""
    tokens = _style_size("radio_list")
    layer = Image.new("RGBA", size)
    centre = (layout["led_cx"] * scale, layout["led_cy"] * scale)
    radius = layout["diameter"] * scale / 2
    if lit:
        glow = tokens["glow"]
        unit = layout["diameter"] * scale
        spread = radius + glow["spread_ratio"] * unit
        mask = Image.new("L", size)
        ImageDraw.Draw(mask).ellipse((centre[0] - spread, centre[1] - spread, centre[0] + spread, centre[1] + spread), fill=255)
        mask = mask.filter(ImageFilter.GaussianBlur(glow["blur_ratio"] * unit)).point(lambda v: v * glow["alpha"] // 255)
        layer = Image.new("RGBA", size, _style_rgb("accent.cyan") + (0,))
        layer.putalpha(mask)
    ImageDraw.Draw(layer).ellipse(
        (centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius),
        fill=_style_color("accent.cyan" if lit else "surface.control"),
        outline=_style_color("radio.active.border" if lit else "control.border"), width=tokens["rim"] * scale)
    return layer


def render_row(text, selected, height, width):
    """One row: gradient face, highlight when selected, LED with glow, label; exactly `width` x `height`."""
    tokens = _style_size("radio_list")
    scale = tokens["scale"]
    layout = row_layout(height, width)
    size = (width * scale, height * scale)
    face, _ = gradient_face(
        size, inset=0, radius=layout["radius"] * scale, frame=scale, span=_style_size("control.gradient"),
        fill=(_style_rgb("button.gradient.top"), _style_rgb("button.gradient.bottom")),
        line=(_style_rgb("button.gradient.border"), _style_rgb("button.gradient.border.bottom")))
    if selected:
        tint = Image.new("RGBA", size, _style_rgb("accent.cyan") + (0,))
        tint.putalpha(face.getchannel("A").point(lambda v: v * tokens["highlight_alpha"] // 255))
        face.alpha_composite(tint)
    face.alpha_composite(_led(size, layout, selected, scale))
    row = face.resize((width, height), Image.Resampling.LANCZOS)
    label = render_label(text, layout["font"], "text.value", unit="em")
    ink = label.getchannel("A").getbbox()
    row.alpha_composite(label, (layout["label_x"], round(height / 2 - (ink[1] + ink[3]) / 2)))
    return row


def row_origins(sample):
    """The top-left corner of every row in the sample picture."""
    height, gap, width = _height(sample["height"]), _style_size("radio_list")["gap"], sample["width"]
    vertical = sample["orientation"] == "vertical"
    return [(0, i * (height + gap)) if vertical else (i * (width + gap), 0) for i in range(len(sample["items"]))]


def sample_size(sample):
    """The footprint (width, height) in px of a sample's picture."""
    height, gap = _height(sample["height"]), _style_size("radio_list")["gap"]
    count, width = len(sample["items"]), sample["width"]
    return (width, count * height + (count - 1) * gap) if sample["orientation"] == "vertical" else (count * width + (count - 1) * gap, height)


def render_sample(sample):
    """The whole list of a catalogue sample; rows are `width` wide, `radio_list.gap` apart; disabled is dimmed."""
    height = _height(sample["height"])
    items, width = sample["items"], sample["width"]
    image = Image.new("RGBA", sample_size(sample))
    for index, (text, origin) in enumerate(zip(items, row_origins(sample))):
        image.alpha_composite(render_row(text, index == sample["selected"], height, width), origin)
    return dim_artwork(image) if sample["state"] == "disabled" else image


def radio_samples(entry):
    """The samples of a catalogue "radio_list" entry, validated; a new example is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("radio_list entry needs a nonempty samples list")
    for item in entry["samples"]:
        if (set(item) != SAMPLE_KEYS or item["orientation"] not in ORIENTATIONS or item["state"] not in STATES
                or not isinstance(item["items"], list) or len(item["items"]) < 2
                or type(item["selected"]) is not int or not 0 <= item["selected"] < len(item["items"])
                or type(item["width"]) is not int or item["width"] <= 0):
            raise ValueError(f"radio list sample needs exactly {sorted(SAMPLE_KEYS)}, orientation in {ORIENTATIONS}, state in {STATES}, "
                             "at least two items and a selected index among them")
        _height(item["height"])
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("radio list sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = radio_samples(entry)
    groups = (("Függőleges lista", lambda i: i["orientation"] == "vertical" and i["state"] == "enabled"),
              ("Vízszintes sor", lambda i: i["orientation"] == "horizontal" and i["state"] == "enabled"),
              ("Disabled", lambda i: i["state"] == "disabled"))
    sections = []
    for heading, pick in groups:
        images = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if pick(i)]
        if images:
            sections.append({"heading": heading, "images": images})
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{} × {}".format(*sample_size(i))] for i in samples]}})
    return sections
