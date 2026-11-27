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

from components.native.button.button import action_art, semibold
from components.native.selector.selector import render_selector_face
from components.style import color as _style_color, dim_artwork, size as _style_size

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

ZONES = ("left", "centre", "right")
CENTRE_KINDS = ("dropdown", "stepper", "button")
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


def title_face(text, content):
    """The page title: font.page_title in text.value, the style of the page captions (title_entry in native_static_captions)."""
    font = semibold(_style_size("font.page_title"))
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    left, _, right, _ = probe.textbbox((0, 0), text, font=font, anchor="lm")
    face = Image.new("RGBA", (math.ceil(right) - min(0, math.floor(left)), content))
    ImageDraw.Draw(face).text((-min(0, math.floor(left)), content / 2), text, font=font,
                              fill=_style_color("text.value"), anchor="lm")
    return face


def power_face(state):
    """POWER 48 x 48: lit (accent.green, as the Filter power button) when on, the plain icon colour when off."""
    if state not in POWER_STATES:
        raise ValueError(f"power must be one of {POWER_STATES}: {state!r}")
    return action_art(icon="power", colour=_style_color("accent.green" if state == "on" else "text.icon"))


def right_face(kind):
    if kind == "expand":
        return action_art(icon="expand")
    if kind in ("close", "more"):
        return action_art(text=kind.upper())
    raise ValueError(f"right button must be one of {RIGHT_KINDS}: {kind!r}")


def centre_face(item, content, *, enabled=True):
    """One page-specific control: dropdown / stepper (selector recipe) or a text button (button recipe)."""
    kind = item.get("kind")
    if kind == "button":
        face = action_art(text=item["text"])
    elif kind in ("dropdown", "stepper"):
        face = render_selector_face(_style_size(item["width"]), content, name=item.get("name"),
                                    value=item.get("value", ""), stepper=(kind == "stepper"))
    else:
        raise ValueError(f"centre control must be one of {CENTRE_KINDS}: {kind!r}")
    face = _content_face(face, content)
    return face if enabled else dim_artwork(face)


def layout(example):
    """Place the faces of one example; returns (header width, [Placed]) with every face inside the padded row.

    LEFT starts at the side padding, CENTRE follows the left zone after one gap, RIGHT ends at the side padding.
    Faces inside a zone are one gap apart. A header without POWER (a modulator) starts with the title at the side
    padding: no empty power slot.
    """
    m = metrics()
    width = header_width(example["width"])
    y, gap = m["padding"], m["gap"]
    placed = []

    def run(zone, items, x):
        for kind, face in items:
            placed.append(Placed(zone, kind, x, y, face))
            x += face.width + gap
        return x - gap if items else x

    left = []
    if example.get("power"):
        left.append(("power", power_face(example["power"])))
    if example.get("title"):
        left.append(("title", title_face(example["title"], m["content"])))
    end = run("left", left, m["side"])
    enabled = example.get("power") != "off"
    centre = [(item["kind"], centre_face(item, m["content"], enabled=enabled)) for item in example.get("centre", [])]
    if centre:
        run("centre", centre, end + gap)
    right = [(kind, _content_face(right_face(kind), m["content"])) for kind in example.get("right", [])]
    if right:
        run("right", right, width - m["side"] - sum(f.width for _, f in right) - gap * (len(right) - 1))
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
        if not example.get("title") and not example.get("power") and not example.get("centre"):
            raise ValueError(f"header example {example['id']!r}: needs a title, a power button or centre controls")
        for kind in example.get("right", []):
            if kind not in RIGHT_KINDS:
                raise ValueError(f"header example {example['id']!r}: right button must be one of {RIGHT_KINDS}")
    return entry["examples"]
