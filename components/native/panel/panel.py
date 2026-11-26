# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared native PANEL: a titled card that groups the controls of one block.

The card is drawn by the host from flat colours only (no image asset, no texture
memory): the fill is `surface.panel`, the 1 px keyline `panel.border`, the title a
native Text label in `text.caption`. The panel takes no touch and no focus; the
controls of the block are siblings placed above it (the panel is listed first).
"""

from components.style import (
    argb as _style_argb,
    native_color as _style_native_color,
    size as _style_size,
)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = ImageDraw = ImageFont = None

LINE_KIND = "nativePanelKeyline"


def _fill(colour):
    background = {"version": 1, "colour": colour, "image": ""}
    return {"version": 1, "focussed": dict(background), "unfocussed": dict(background)}


def _child(name, kind, rect, data=None):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind,
                          "data": data if data is not None else {"version": 1, "handleName": "Data"}},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
                   "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % tuple(rect)},
    }


def keyline_definition():
    """The one-colour line the four edges of every panel share."""
    return {"version": 2, "actions": [], "backgroundData": _fill(_style_argb("panel.border")),
            "ignoreMousePresses": True, "disableCoarseDataWheel": True, "componentsData": []}


def title_rect(size):
    """The title strip of a panel of `size` (panel coordinates)."""
    geometry = _style_size("panel.title")
    width, _ = size
    return (geometry["x"], geometry["y"], max(0, width - 2 * geometry["x"]), geometry["height"])


def panel_definition(size, title=None, *, subtitle=None, keyline=LINE_KIND):
    """A flat card of `size` with a keyline and an optional static title (and right-aligned subtitle)."""
    from components.native.name_value.name_value import _label_component

    width, height = size
    if width < 8 or height < 8:
        raise ValueError("A panel needs at least 8 x 8 px")
    line = _style_size("panel.keyline")
    children = [
        _child("top", keyline, (0, 0, width, line)),
        _child("bottom", keyline, (0, height - line, width, line)),
        _child("left", keyline, (0, 0, line, height)),
        _child("right", keyline, (width - line, 0, line, height)),
    ]
    font = _style_size("panel.title")["font"]
    for name, text, justification in (("title", title, "left verticallyCentred"),
                                      ("subtitle", subtitle, "right verticallyCentred")):
        if not text:
            continue
        label = _label_component(name, "Text", "Data", title_rect(size), font,
                                 _style_argb("text.caption"), justification)
        label["componentData"]["data"]["text"] = text
        children.append(label)
    return {"version": 2, "actions": [], "backgroundData": _fill(_style_argb("surface.panel")),
            "ignoreMousePresses": True, "disableCoarseDataWheel": True, "componentsData": children}


def panel_definitions(panels, *, keyline=LINE_KIND):
    """{kind: definition} for [(kind, size, title, subtitle)], plus the keyline they share (one per page:
    a skin registers each definition name once)."""
    definitions = {keyline: keyline_definition()}
    for kind, size, title, subtitle in panels:
        definitions[kind] = panel_definition(size, title, subtitle=subtitle, keyline=keyline)
    return definitions


def panel_entry(name, kind, rect, *, gates=(), model="Parameter 0"):
    """Place a panel on a tab. The Key 61 needs a valid model even for a passive component."""
    entry = _child(name, kind, rect)
    entry["handle remapping"]["map"] = [{"key": "Data", "value": model}]
    if gates:
        entry["bounds"].update(version=2, showWhenDataModelInvalid="Hide", additionalInvalidatingHandles=list(gates))
    return entry


def render_panel_preview(size=(360, 160), title="OUTPUT", subtitle=None, output_path=None):
    """Catalog picture: the card as the host draws it (fill, keyline, title)."""
    if Image is None:
        raise RuntimeError("Pillow is required for render_panel_preview")
    from pathlib import Path
    from components.style import color, rgb
    width, height = size
    line = _style_size("panel.keyline")
    image = Image.new("RGB", (width, height), color("surface.panel"))
    pen = ImageDraw.Draw(image)
    pen.rectangle((0, 0, width - 1, height - 1), outline=rgb("panel.border"), width=line)
    font_file = Path(__file__).resolve().parent / "img" / _style_size("font.file.semibold")
    if title and font_file.is_file():
        x, y, w, h = title_rect(size)
        font = ImageFont.truetype(str(font_file), _style_size("panel.title")["font"])
        pen.text((x, y + h / 2), title, font=font, fill=rgb("text.caption"), anchor="lm")
        if subtitle:
            pen.text((x + w, y + h / 2), subtitle, font=font, fill=rgb("text.caption"), anchor="rm")
    if output_path is not None:
        image.save(output_path)
    return image


__all__ = ["LINE_KIND", "keyline_definition", "panel_definition", "panel_definitions", "panel_entry",
           "render_panel_preview", "title_rect"]


if __name__ == "__main__":
    from pathlib import Path
    render_panel_preview(output_path=Path(__file__).resolve().parent / "img" / "catalog-panel.png")
