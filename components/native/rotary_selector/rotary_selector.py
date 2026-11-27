# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ROTARY SELECTOR: a stepped rotary dial with pictograms around it, N = 3..6 positions, catalogue pictures.

A composite: the SteppedDial (our dial face without the gradient value arc: dark track, a dot per stop, white thumb and
pointer; stepped_dial.py) at 48 or 56 px, the pictograms and the title. The pointer jumps to N evenly spaced angles of the
270 degree sweep (`knob.start.degrees`, `knob.sweep.degrees`): discrete frames. The pictograms sit on a ring around the dial
at those same angles, `rotary_selector.ring_gap` clear of the dial, in the pictogram recipes (`selector.render_wave_icon`
shapes sine, saw, square, pulse, triangle, sh, noise; `selector.render_pipa_icon`: the check mark) scaled to
`rotary_selector.icon`; the selected one is `accent.cyan`, the others `selector.wave`. The title is the Label dial-caption
style (`label.render_label`, `pan.sub_dial_definition.style.font.height`, `text.caption`, upper case), centred on top.
Disabled: inactive dial and `style.dim_artwork` of the whole picture.
Docs: docs/ui-components/rotary_selector.md.
"""
import math

from components.native.label.label import render_label
from components.native.selector.selector import WAVE_SHAPES, render_pipa_icon, render_wave_icon
from components.native.stepped_dial.stepped_dial import render_frame, stop_angle
from components.style import color as _style_color, dim_artwork, size as _style_size

try:
    from PIL import Image
except ImportError:
    Image = None

STATES = ("enabled", "disabled")
DIAMETERS = (48, 56)
ICONS = {shape: (lambda w, h, colour, shape=shape: render_wave_icon(w, h, colour, shape=shape)) for shape in WAVE_SHAPES}
ICONS["pipa"] = lambda w, h, colour: render_pipa_icon(min(w, h), colour)
SAMPLE_KEYS = {"id", "file", "alt", "caption", "icons", "position", "diameter", "title", "state"}


def _diameter(token):
    value = token if isinstance(token, int) else _style_size(token)
    if value not in DIAMETERS:
        raise ValueError(f"dial diameter must be one of {DIAMETERS}: {token!r}")
    return value


def layout(sample):
    """Geometry in final pixels: picture size, dial box, dial centre, pictogram boxes (left, top, right, bottom), title origin."""
    tokens, diameter = _style_size("rotary_selector"), _diameter(sample["diameter"])
    count, icon = len(sample["icons"]), tokens["icon"]
    ring = diameter / 2 + tokens["ring_gap"] + math.hypot(icon["width"], icon["height"]) / 2
    side = math.ceil(2 * (ring + icon["width"] / 2 + tokens["margin"]))
    title_h = render_label(sample["title"], tokens["title_font"], "text.caption", unit="tui", case="Upper Case").height + tokens["title_gap"]
    centre = (side / 2, title_h + side / 2)
    boxes = []
    for index in range(count):
        a = math.radians(stop_angle(index, count))
        cx, cy = centre[0] + ring * math.cos(a), centre[1] + ring * math.sin(a)
        left, top = round(cx - icon["width"] / 2), round(cy - icon["height"] / 2)
        boxes.append((left, top, left + icon["width"], top + icon["height"]))
    return {"size": (side, title_h + side), "centre": centre, "diameter": diameter, "ring": ring, "icon_boxes": boxes,
            "title_h": title_h}


def render_sample(sample):
    """The selector at one position: static pictograms and title, the dial frame with the pointer at the position."""
    tokens, geo = _style_size("rotary_selector"), layout(sample)
    count, active = len(sample["icons"]), sample["state"] == "enabled"
    image = Image.new("RGBA", geo["size"])
    title = render_label(sample["title"], tokens["title_font"], "text.caption", unit="tui", case="Upper Case")
    image.alpha_composite(title, ((geo["size"][0] - title.width) // 2, 0))
    dial = render_frame(geo["diameter"], sample["position"], count, active=active)
    image.alpha_composite(dial, (round(geo["centre"][0] - geo["diameter"] / 2), round(geo["centre"][1] - geo["diameter"] / 2)))
    for index, (name, box) in enumerate(zip(sample["icons"], geo["icon_boxes"])):
        colour = _style_color("accent.cyan" if index == sample["position"] else "selector.wave")
        image.alpha_composite(ICONS[name](tokens["icon"]["width"], tokens["icon"]["height"], colour), box[:2])
    return dim_artwork(image) if not active else image


def sample_size(sample):
    return layout(sample)["size"]


def rotary_samples(entry):
    """The samples of a catalogue "rotary_selector" entry, validated; a new example is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("rotary_selector entry needs a nonempty samples list")
    for item in entry["samples"]:
        if (set(item) != SAMPLE_KEYS or item["state"] not in STATES or not isinstance(item["icons"], list)
                or not 3 <= len(item["icons"]) <= 6 or any(i not in ICONS for i in item["icons"])
                or type(item["position"]) is not int or not 0 <= item["position"] < len(item["icons"])
                or not isinstance(item["title"], str) or not item["title"]):
            raise ValueError(f"rotary selector sample needs exactly {sorted(SAMPLE_KEYS)}, state in {STATES}, 3 to 6 icons from "
                             f"{sorted(ICONS)}, a position among them and a title")
        _diameter(item["diameter"])
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("rotary selector sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = rotary_samples(entry)
    groups = (("Állások", lambda i: i["state"] == "enabled"), ("Disabled", lambda i: i["state"] == "disabled"))
    sections = []
    for heading, pick in groups:
        images = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if pick(i)]
        if images:
            sections.append({"heading": heading, "images": images})
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{} × {}".format(*sample_size(i))] for i in samples]}})
    return sections
