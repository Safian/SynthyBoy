# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HEADER: the full-width top row of a page or a Play panel (design proposal v0, catalogue only).

Three zones in one row: LEFT = POWER (only for modules that can be switched off) and the page title, CENTRE =
page-specific controls, RIGHT = the common buttons (MORE). Every face is rendered by an existing
recipe at the shared content height (header.height - 2 x header.padding: 40 px with padding 8, 48 with padding 4); this module only places them. All sizes come from components/sizes.json
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
CENTRE_KINDS = ("dropdown", "button", "field", "segment", "label")
RIGHT_KINDS = ("more", "dropdown", "button", "label")
SLOT_KINDS = {"left": ("title", "power"), "centre": CENTRE_KINDS, "right": RIGHT_KINDS}
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
    """The header tokens as plain numbers: height, padding (the one margin on all four sides), gap and the content height."""
    height, padding = _style_size("header.height"), _style_size("header.padding")
    return {"height": height, "padding": padding, "gap": _style_size("header.gap"),
            "side": padding, "content": height - 2 * padding}


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


def power_face(state, height):
    """POWER 48 x 48: lit (accent.green, as the Filter power button) when on, the plain icon colour when off."""
    if state not in POWER_STATES:
        raise ValueError(f"power must be one of {POWER_STATES}: {state!r}")
    return _icon_button.render_sample({"icon": "power", "height": height, "state": state, "look": "filled"})


def _label_slot(item, height):
    """A `label` slot (a live value the host draws, e.g. the Mod slot count): only room in the layout, no picture."""
    return Image.new("RGBA", (item["width_px"], height))


def segment_face(item, height, *, selected=False):
    """One button of a `segment` group (e.g. the Mod ALL / BY SOURCE / BY DEST switch): the Button recipe; the selected one
    takes the RadioGrid selected look (action_art look "active")."""
    if not selected:
        return _button.render_sample({"text": item["text"], "height": height, "state": "enabled"})
    return _button.action_art(text=item["text"], height=height, look="active", colour=_style_color("radio.active.text"))


def _right_item(item):
    """A right-zone entry is the string "more" or {"kind": "dropdown", "value", "width_px"} (the oscillator's Filter routing)."""
    return {"kind": item} if isinstance(item, str) else item


def right_face(item, height):
    item = _right_item(item)
    kind = item["kind"]
    if kind == "more":
        return _button.render_sample({"text": "MORE", "height": height, "state": "enabled"})
    if kind == "dropdown":
        return render_dropdown_variant(item["width_px"], height, item.get("value", ""))
    if kind == "button":       # a page button at the right edge (TARGETS of the modulator screens, the Mod ADD)
        return _button.render_sample({"text": item["text"], "height": height, "state": "enabled"})
    if kind == "label":
        return _label_slot(item, height)
    raise ValueError(f"right button must be one of {RIGHT_KINDS}: {kind!r}")


def centre_face(item, content, *, enabled=True):
    """One page-specific control: dropdown (selector recipe; there is no left/right stepper) or a text button (button recipe)."""
    kind = item.get("kind")
    if kind == "button":
        if item.get("width_px"):    # a fixed-width text button (the Tuning scale input: both state texts share one width)
            face = _button.action_art(text=item["text"], width=item["width_px"], height=content, look="shaded")
        else:
            face = _button.render_sample({"text": item["text"], "height": content, "state": "enabled"})
    elif kind == "dropdown":
        # The old Play-panel look: value only, centred, no label (render_dropdown_variant placement none, align center).
        # As high as the content row (40 px with padding 8; sizes 32 / 40 / 48 exist), centred in the row.
        face = render_dropdown_variant(item.get("width_px") or _style_size(item["width"]), _style_size(item["height"]) if "height" in item else content,
                                       item.get("value", ""))
    elif kind == "segment":
        face = segment_face(item, content, selected=bool(item.get("selected")))      # "selected": true draws the catalogue picture of the current value
    elif kind == "label":
        face = _label_slot(item, content)
    elif kind == "field":
        # A named number field (the Sub OCT): the name left outside the frame, the value box right (name_value recipe).
        from components.native.name_value.name_value import render_name_value_preview
        face = render_name_value_preview(item["name"], item.get("value", ""), item["width_px"], content,
                                         box_width=item.get("box_px", 68))
    else:
        raise ValueError(f"centre control must be one of {CENTRE_KINDS}: {kind!r}")
    if face.height > content or (kind != "dropdown" and face.height != content):
        raise ValueError(f"a header component is at most {content} px high ({content} for all but dropdowns): {face.height}")
    return face if enabled else dim_artwork(face)


def flat(spec):
    """The flat view of a spec: {width, title, title_slot, power, centre, right} from its zones.

    A spec is {"id", "file", "alt", "caption", "width", "zones": {"left": [...], "centre": [...], "right": [...]}}; every zone is
    a list of slot dicts ({"kind": title|power|dropdown|field|button|more, ...}, see docs/ui-components/header.md). The layout and the
    renderers below work on this view."""
    zones = spec.get("zones", {})
    left = zones.get("left", [])
    out = {"id": spec.get("id"), "width": spec["width"], "centre": list(zones.get("centre", [])), "right": list(zones.get("right", []))}
    for slot in left:
        kind = slot.get("kind")
        if kind == "title":
            out["title"] = slot["text"]
            if slot.get("slot"):
                out["title_slot"] = slot["slot"]
        elif kind == "power":
            out["power"] = slot.get("state", "on")
        else:
            raise ValueError(f"left slot must be title or power: {kind!r}")
    return out


def layout(spec):
    """Place the faces of one example; returns (header width, [Placed]) with every face inside the padded row.

    LEFT is the title first (at the side padding), then POWER one gap after the title's width. CENTRE follows after
    one gap, RIGHT ends at the side padding. Faces inside a zone are one gap apart. At most one centre control is
    flexible (`"fill": true`): it takes all the room between the left and the right group (its width token is the
    minimum); every other face keeps its size. A header without POWER (a modulator) is just the title.
    """
    example = flat(spec)
    m = metrics()
    width = header_width(example["width"])
    y, gap = m["padding"], m["gap"]
    placed = []

    def run(zone, items, x):
        for kind, face in items:
            # A face lower than the content row (the title) is centred in the row.
            placed.append(Placed(zone, kind, x, y + (m["content"] - face.height) // 2, face))
            x += face.width + gap
        return x - gap if items else x

    left = []
    if example.get("title"):
        title = title_face(example["title"])
        slot = example.get("title_slot")   # a family of pages with different titles (OSC A/B/C) reserves the widest title's width, so POWER and the rest never move
        if slot:
            if slot < title.width:
                raise ValueError(f"title_slot {slot} is narrower than the title ({title.width} px)")
            wide = Image.new("RGBA", (slot, title.height))
            wide.alpha_composite(title)
            title = wide
        left.append(("title", title))
    if example.get("power"):
        left.append(("power", power_face(example["power"], m["content"])))
    end = run("left", left, m["side"])
    enabled = example.get("power") != "off"
    right = [(_right_item(item)["kind"], _content_face(right_face(item, m["content"]), m["content"])) for item in example.get("right", [])]
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


def render_header(spec):
    """The header row as one transparent picture: header.page.width / header.panel.width by header.height."""
    width, placed = layout(spec)
    row = Image.new("RGBA", (width, metrics()["height"]))
    for item in placed:
        row.alpha_composite(item.face, (item.x, item.y))
    return row


def header_examples(entry):
    """The examples (= specs) of a catalogue "header" entry, validated: id, file, alt, caption, width, zones."""
    required = {"id", "file", "alt", "caption", "width", "zones"}
    optional = set()
    if not isinstance(entry, dict) or set(entry) != {"examples"} or not entry["examples"]:
        raise ValueError("header entry needs a nonempty examples list")
    ids = [e.get("id") for e in entry["examples"]]
    if len(set(ids)) != len(ids):
        raise ValueError("header example ids must be unique")
    for example in entry["examples"]:
        if not required <= set(example) or not set(example) <= required | optional:
            raise ValueError(f"header example {example.get('id')!r}: fields must be {sorted(required)} plus any of {sorted(optional)}")
        header_width(example["width"])
        zones = example["zones"]
        if not isinstance(zones, dict) or not set(zones) <= set(ZONES):
            raise ValueError(f"header example {example['id']!r}: zones must be a dict of {ZONES}")
        for zone, slots in zones.items():
            for slot in slots:
                if slot.get("kind") not in SLOT_KINDS[zone]:
                    raise ValueError(f"header example {example['id']!r}: {zone} slot kind must be one of {SLOT_KINDS[zone]}: {slot.get('kind')!r}")
        view = flat(example)
        if view.get("power") is not None and view["power"] not in POWER_STATES:
            raise ValueError(f"header example {example['id']!r}: power must be one of {POWER_STATES}")
        if not (view.get("title") or view.get("power") or view.get("centre")):
            raise ValueError(f"header example {example['id']!r}: needs a title, a power button or centre controls")
        if sum(1 for item in view.get("centre", []) if item.get("fill")) > 1:
            raise ValueError(f"header example {example['id']!r}: at most one centre control may fill")
    return entry["examples"]


def catalog_sections(entry):
    """Catalogue sections of a "header" entry: one natural-size picture per example, the caption from the entry."""
    return [{"heading": "Példák (az oldalspecifikus középső tartalom csak példa)", "images": [
        {"presentation": "sample", "file": e["file"], "alt": e["alt"], "caption": e["caption"]}
        for e in header_examples(entry)]}]
