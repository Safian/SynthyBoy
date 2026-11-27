# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HEADER: the full-width top row of a page or a Play panel (design proposal v0, catalogue only).

Three zones in one row: LEFT = POWER (only for modules that can be switched off) and the page title, CENTRE =
page-specific controls, RIGHT = the common buttons (EXPAND / CLOSE, MORE). Every face is rendered by an existing
recipe at the shared 48 px content height; this module only places them. All sizes come from components/sizes.json
(header.*), the examples from components/native/header/catalog.json. Docs: docs/ui-components/header.md.
"""
import math
from dataclasses import dataclass

from components.native.button import button as _button
from components.native.icon_button import icon_button as _icon_button
from components.native.selector.selector import render_dropdown_variant
from components.style import color as _style_color, dim_artwork, size as _style_size

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

ZONES = ("left", "centre", "right")
CENTRE_KINDS = ("dropdown", "button")
RIGHT_KINDS = ("expand", "close", "more")
POWER_STATES = ("on", "off")
WIDTHS = ("page", "panel")


@dataclass(frozen=True)
class Placed:
    zone: str
    kind: str
    x: int
    y: int
    face: "Image.Image"

    @property
    def rect(self):
        return (self.x, self.y, self.face.width, self.face.height)


def metrics():
    """The header tokens as plain numbers: height, padding, gap, side padding and the content height."""
    height, padding = _style_size("header.height"), _style_size("header.padding")
    return {"height": height, "padding": padding, "gap": _style_size("header.gap"),
            "side": _style_size("header.side"), "content": height - 2 * padding}


def header_width(width_id):
    if width_id not in WIDTHS:
        raise ValueError(f"header width must be one of {WIDTHS}: {width_id!r}")
    return _style_size(f"header.{width_id}.width")


def _content_face(face, content):
    if face.height != content:
        raise ValueError(f"a header component must be {content} px high, got {face.height}")
    return face


def title_face(text):
    """The page title: the Label component's screenTitle (font.page_title em, text.value); its width comes from the text."""
    from components.native.label.label import render_label
    return render_label(text, "font.page_title", "text.value", unit="em")


def power_face(state):
    """POWER 48 x 48: lit (accent.green, as the Filter power button) when on, the plain icon colour when off."""
    if state not in POWER_STATES:
        raise ValueError(f"power must be one of {POWER_STATES}: {state!r}")
    return _icon_button.render_sample({"icon": "power", "height": "height.medium", "state": state, "look": "filled"})


def right_face(kind):
    if kind == "expand":
        return _icon_button.render_sample({"icon": "expand", "height": "height.medium", "state": "off", "look": "filled"})
    if kind == "close":
        return _icon_button.render_sample({"icon": "close", "height": "height.medium", "state": "off", "look": "filled"})
    if kind == "more":
        return _button.render_sample({"text": "MORE", "height": "height.medium", "state": "enabled"})
    raise ValueError(f"right button must be one of {RIGHT_KINDS}: {kind!r}")


def centre_face(item, content, *, enabled=True):
    """One page-specific control: dropdown (selector recipe; there is no left/right stepper) or a text button (button recipe)."""
    kind = item.get("kind")
    if kind == "button":
        face = _button.render_sample({"text": item["text"], "height": "height.medium", "state": "enabled"})
    elif kind == "dropdown":
        # The old Play-panel look: value only, centred, no label (render_dropdown_variant placement none, align center).
        # 40 px by default (header.dropdown.height proposal: the owner chooses from the catalogue), centred in the row.
        face = render_dropdown_variant(item.get("width_px") or _style_size(item["width"]), _style_size(item.get("height", "height.intermediate")),
                                       item.get("value", ""))
    else:
        raise ValueError(f"centre control must be one of {CENTRE_KINDS}: {kind!r}")
    if face.height > content or (kind != "dropdown" and face.height != content):
        raise ValueError(f"a header component is at most {content} px high ({content} for all but dropdowns): {face.height}")
    return face if enabled else dim_artwork(face)


def layout(example):
    """Place the faces of one example; returns (header width, [Placed]) with every face inside the padded row.

    LEFT is the title first (at the side padding), then POWER one gap after the title's width. CENTRE follows after
    one gap, RIGHT ends at the side padding. Faces inside a zone are one gap apart. At most one centre control is
    flexible (`"fill": true`): it takes all the room between the left and the right group (its width token is the
    minimum); every other face keeps its size. A header without POWER (a modulator) is just the title.
    """
    m = metrics()
    width = header_width(example["width"])
    y, gap = m["padding"], m["gap"]
    placed = []

    def run(zone, items, x):
        for kind, face in items:
            # A face lower than the 48 px content (a 40 px dropdown) is centred in the row.
            placed.append(Placed(zone, kind, x, y + (m["content"] - face.height) // 2, face))
            x += face.width + gap
        return x - gap if items else x

    left = []
    if example.get("title"):
        left.append(("title", title_face(example["title"])))
    if example.get("power"):
        left.append(("power", power_face(example["power"])))
    end = run("left", left, m["side"])
    enabled = example.get("power") != "off"
    right = [(kind, _content_face(right_face(kind), m["content"])) for kind in example.get("right", [])]
    right_x = width - m["side"] - sum(f.width for _, f in right) - gap * (len(right) - 1) if right else width - m["side"] + gap
    items = example.get("centre", [])
    centre_x = end + gap if left else m["side"]
    fixed = [centre_face(item, m["content"], enabled=enabled) for item in items if not item.get("fill")]
    fills = [item for item in items if item.get("fill")]
    if len(fills) > 1:
        raise ValueError("at most one flexible (fill) header control")
    room = right_x - gap - centre_x - sum(f.width for f in fixed) - gap * (len(items) - 1)
    faces = []
    for item in items:
        if item.get("fill"):
            if room < _style_size(item["width"]):
                raise ValueError(f"no room for the flexible header control: {room} px < minimum {_style_size(item['width'])}")
            faces.append((item["kind"], centre_face(dict(item, width_px=room), m["content"], enabled=enabled)))
        else:
            faces.append((item["kind"], fixed.pop(0)))
    if faces:
        run("centre", faces, centre_x)
    if right:
        run("right", right, right_x)
    return width, placed


def render_header(example):
    """The header row as one transparent picture: header.page.width / header.panel.width by header.height."""
    width, placed = layout(example)
    row = Image.new("RGBA", (width, metrics()["height"]))
    for item in placed:
        row.alpha_composite(item.face, (item.x, item.y))
    return row


def header_examples(entry):
    """The examples of a catalogue "header" entry, validated: id, file, alt, caption, width, title/power/centre/right."""
    required = {"id", "file", "alt", "caption", "width"}
    optional = {"title", "power", "centre", "right"}
    if not isinstance(entry, dict) or set(entry) != {"examples"} or not entry["examples"]:
        raise ValueError("header entry needs a nonempty examples list")
    ids = [e.get("id") for e in entry["examples"]]
    if len(set(ids)) != len(ids):
        raise ValueError("header example ids must be unique")
    for example in entry["examples"]:
        if not required <= set(example) or not set(example) <= required | optional:
            raise ValueError(f"header example {example.get('id')!r}: fields must be {sorted(required)} plus any of {sorted(optional)}")
        header_width(example["width"])
        if example.get("power") is not None and example["power"] not in POWER_STATES:
            raise ValueError(f"header example {example['id']!r}: power must be one of {POWER_STATES}")
        if not (example.get("title") or example.get("power") or example.get("centre")):
            raise ValueError(f"header example {example['id']!r}: needs a title, a power button or centre controls")
        if sum(1 for item in example.get("centre", []) if item.get("fill")) > 1:
            raise ValueError(f"header example {example['id']!r}: at most one centre control may fill")
        for kind in example.get("right", []):
            if kind not in RIGHT_KINDS:
                raise ValueError(f"header example {example['id']!r}: right button must be one of {RIGHT_KINDS}")
    return entry["examples"]


def catalog_sections(entry):
    """Catalogue sections of a "header" entry: one natural-size picture per example, the caption from the entry."""
    return [{"heading": "Példák (az oldalspecifikus középső tartalom csak példa)", "images": [
        {"presentation": "sample", "file": e["file"], "alt": e["alt"], "caption": e["caption"]}
        for e in header_examples(entry)]}]
