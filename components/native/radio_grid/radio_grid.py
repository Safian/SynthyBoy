# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""RADIO GRID: a group of text buttons in rows x columns, exactly one selected, with an optional group title.

Every cell is the Button recipe (`button.action_art(text=..., look=...)`): unselected = the legacy gradient face
(`control.gradient`, `button.gradient.*`), selected = look "active" (accent fill `accent.cyan` to `radio.active.bottom`,
keyline `radio.active.border`, text `radio.active.text`). Buttons are 32 / 40 / 48 px high and all equally wide; the grid
fills `width`, with `radio_grid.gap` (= `header.gap`, 8 px) between cells. The title is the Label block-title style
(`panel.title` font, `text.caption`), left aligned above the grid. Disabled: `style.dim_artwork`. A grid of 1 x N is a
horizontal row, N x 1 a vertical list. Docs: docs/ui-components/radio_grid.md.
"""
from components.native.button.button import action_art
from components.native.label.label import render_label
from components.style import color as _style_color, dim_artwork, size as _style_size

try:
    from PIL import Image
except ImportError:
    Image = None

STATES = ("enabled", "disabled")
HEIGHTS = (32, 40, 48)
SAMPLE_KEYS = {"id", "file", "alt", "caption", "rows", "cols", "items", "selected", "height", "width", "title", "state"}
TITLE_FONT = "panel.title:font"


def _height(token):
    value = token if isinstance(token, int) else _style_size(token)
    if value not in HEIGHTS:
        raise ValueError(f"button height must be one of {HEIGHTS}: {token!r}")
    return value


def layout(sample):
    """Geometry in final pixels: picture size, equal button width, cell origins (row-major), title height."""
    gap, height = _style_size("radio_grid")["gap"], _height(sample["height"])
    rows, cols, width = sample["rows"], sample["cols"], sample["width"]
    room = width - (cols - 1) * gap
    if room % cols:
        raise ValueError(f"width {width} does not divide into {cols} equal buttons with {gap} px gaps")
    title_h = 0 if not sample["title"] else render_label(sample["title"], TITLE_FONT, "text.caption").height + _style_size("radio_grid")["title_gap"]
    button = room // cols
    origins = [((i % cols) * (button + gap), title_h + (i // cols) * (height + gap)) for i in range(rows * cols)]
    return {"size": (width, title_h + rows * height + (rows - 1) * gap), "button": (button, height), "origins": origins, "title_h": title_h}


def render_sample(sample):
    geo = layout(sample)
    image = Image.new("RGBA", geo["size"])
    if sample["title"]:
        image.alpha_composite(render_label(sample["title"], TITLE_FONT, "text.caption"), (0, 0))
    button_width, height = geo["button"]
    for index, (text, origin) in enumerate(zip(sample["items"], geo["origins"])):
        if index == sample["selected"]:
            face = action_art(text=text, width=button_width, height=height, look="active", colour=_style_color("radio.active.text"))
        else:
            face = action_art(text=text, width=button_width, height=height, look="legacy")
        image.alpha_composite(face, origin)
    return dim_artwork(image) if sample["state"] == "disabled" else image


def sample_size(sample):
    return layout(sample)["size"]


def grid_samples(entry):
    """The samples of a catalogue "radio_grid" entry, validated; a new example is one more entry."""
    if not isinstance(entry, dict) or set(entry) != {"samples"} or not entry["samples"]:
        raise ValueError("radio_grid entry needs a nonempty samples list")
    for item in entry["samples"]:
        if (set(item) != SAMPLE_KEYS or item["state"] not in STATES or type(item["rows"]) is not int or type(item["cols"]) is not int
                or item["rows"] < 1 or item["cols"] < 1 or not isinstance(item["items"], list)
                or len(item["items"]) != item["rows"] * item["cols"] or not 2 <= len(item["items"]) <= 8
                or type(item["selected"]) is not int or not 0 <= item["selected"] < len(item["items"])
                or type(item["width"]) is not int or not (item["title"] is None or isinstance(item["title"], str))):
            raise ValueError(f"radio grid sample needs exactly {sorted(SAMPLE_KEYS)}, state in {STATES}, 2 to 8 items filling rows x cols "
                             "and a selected index among them")
        _height(item["height"])
        layout(item)
    if len({i["id"] for i in entry["samples"]}) != len(entry["samples"]):
        raise ValueError("radio grid sample ids must be unique")
    return entry["samples"]


def catalog_sections(entry):
    samples = grid_samples(entry)
    sections = [{"heading": "Rács", "images": [
        {"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if i["state"] == "enabled"]}]
    disabled = [{"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]} for i in samples if i["state"] == "disabled"]
    if disabled:
        sections.append({"heading": "Disabled", "images": disabled})
    sections.append({"heading": "Helyigény", "table": {"head": ["Példa", "Méret (px)"], "rows": [
        [i["caption"], "{} × {}".format(*sample_size(i))] for i in samples]}})
    return sections
