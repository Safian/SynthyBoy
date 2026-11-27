# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""ON/OFF LABEL: a label that is a switch, placed next to a dial (catalogue only; Smooth becomes an instance of it).

ON is the shared active colour (active_text.on, cyan), OFF the dim one (active_text.off), DISABLED the OFF face in the
disabled look (style.dim_artwork). The faces are drawn by the ActiveText recipe; the dial is the Pan/Level dial graphic.
Docs: docs/ui-components/on_off_label.md.
"""
from pathlib import Path

from components.native.active_text.active_text import render_active_text_art
from components.native.level.level import render_dial_frame
from components.style import dim_artwork, native_color as _style_native_color, size as _style_size

try:
    from PIL import Image
except ImportError:
    Image = None

STATES = ("on", "off", "disabled")
LAYOUTS = ("right", "bottom")
DIALS = {"level": False, "pan": True}  # name -> bipolar
EXAMPLE_KEYS = {"id", "file", "alt", "caption", "state"}
EXAMPLE_OPTIONAL = {"dial", "layout", "text"}
DIAL_STATE = 0.5


def label_face(text, state, *, width=None, height=None, font_size=None, anchor=None):
    """One switch label: ON bright, OFF dim, DISABLED = the OFF face dimmed."""
    if state not in STATES:
        raise ValueError(f"state must be one of {STATES}: {state!r}")
    face = render_active_text_art(text, state == "on", width, height, font_size=font_size, anchor=anchor)
    return dim_artwork(face) if state == "disabled" else face


def on_off_label_child(name, on_image, off_image, rect, handle="Active"):
    """MPC Button child component for an On/Off label."""
    bounds_str = "%d %d %d %d" % tuple(rect) if isinstance(rect, (tuple, list)) else str(rect)
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": name,
            "type": "Button",
            "data": {
                "version": 2,
                "onImage": on_image,
                "offImage": off_image,
                "buttonId": 1,
                "numButtonsInGroup": 1,
                "handleName": handle,
                "gestureBehaviour": "Instant",
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "No",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": bounds_str,
        },
    }


def on_off_label_definition(
    text,
    size,
    *,
    skins_dir=None,
    handle="Active",
    anchor="mm",
    active=True,
    font_size=None,
    asset_prefix=None,
    actions=None,
):
    """Build the skin definition of the On/Off label component.

    Renders ON and OFF states using the On/Off label component recipe (label_face)
    and returns a native component definition containing the toggle Button.
    """
    w, h = size
    clean = text.lower().replace(" ", "_")
    prefix = asset_prefix or f"on_off_{clean}"
    tag = f"{prefix}_{w}x{h}" + ("" if anchor == "mm" else "_" + anchor)
    on_name = f"{tag}_on.png"
    off_name = f"{tag}_off.png"

    if skins_dir is not None:
        skins_path = Path(skins_dir)
        skins_path.mkdir(parents=True, exist_ok=True)
        im_on = label_face(text, "on" if active else "disabled", width=w, height=h, font_size=font_size, anchor=anchor)
        im_off = label_face(text, "off" if active else "disabled", width=w, height=h, font_size=font_size, anchor=anchor)
        im_on.save(skins_path / on_name)
        im_off.save(skins_path / off_name)

    child = on_off_label_child("button", on_name, off_name, (0, 0, w, h), handle=handle)
    bg = {"version": 1, "colour": _style_native_color("transparent"), "image": ""}
    return {
        "version": 2,
        "actions": list(actions or []),
        "backgroundData": {"version": 1, "focussed": bg, "unfocussed": bg},
        "ignoreMousePresses": not active,
        "disableCoarseDataWheel": True,
        "componentsData": [child],
    }


def render_example(example):
    """Alone (no dial) or next to the text-free dial: layout right (label centred beside) or bottom (centred below)."""
    text = example.get("text", "SMOOTH")
    state = example["state"]
    if "dial" not in example:
        return label_face(text, state)
    dial, layout = example["dial"], example["layout"]
    if dial not in DIALS or layout not in LAYOUTS:
        raise ValueError(f"dial must be one of {sorted(DIALS)} and layout one of {LAYOUTS}")
    diameter, gap = _style_size("dial.diameter"), _style_size("gap.column")
    graphic = render_dial_frame(diameter, DIAL_STATE, bipolar=DIALS[dial], active=state != "disabled")
    if layout == "right":
        face = label_face(text, state)
        canvas = Image.new("RGBA", (diameter + gap + face.width, max(diameter, face.height)))
        canvas.alpha_composite(graphic, (0, (canvas.height - diameter) // 2))
        canvas.alpha_composite(face, (diameter + gap, (canvas.height - face.height) // 2))
        return canvas
    face = label_face(text, state, width=diameter, anchor="mm")
    canvas = Image.new("RGBA", (diameter, diameter + gap + face.height))
    canvas.alpha_composite(graphic, (0, 0))
    canvas.alpha_composite(face, (0, diameter + gap))
    return canvas


def examples(entry):
    """The examples of a catalogue "on_off" entry, validated."""
    if not isinstance(entry, dict) or set(entry) != {"examples"} or not entry["examples"]:
        raise ValueError("on_off entry needs a nonempty examples list")
    for item in entry["examples"]:
        if not EXAMPLE_KEYS <= set(item) <= EXAMPLE_KEYS | EXAMPLE_OPTIONAL or item["state"] not in STATES:
            raise ValueError(f"on_off example needs {sorted(EXAMPLE_KEYS)} (+ optional {sorted(EXAMPLE_OPTIONAL)}), state in {STATES}")
        if ("dial" in item) != ("layout" in item):
            raise ValueError("an on_off example gives dial and layout together")
    if len({i["id"] for i in entry["examples"]}) != len(entry["examples"]):
        raise ValueError("on_off example ids must be unique")
    return entry["examples"]


def catalog_sections(entry):
    items = examples(entry)
    alone = [i for i in items if "dial" not in i]
    beside = [i for i in items if "dial" in i]

    def image(i):
        return {"presentation": "sample", "file": i["file"], "alt": i["alt"], "caption": i["caption"]}

    sections = [{"images": [image(i) for i in alone]}] if alone else []
    if beside:
        sections.append({"heading": "Tárcsa mellett (szöveg nélküli tárcsa)", "images": [image(i) for i in beside]})
    return sections
