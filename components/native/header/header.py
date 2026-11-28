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
    if kind == "button":
        return _button.render_sample({"text": item["text"], "height": height, "state": "enabled"})
    if kind == "label":
        return _label_slot(item, height)
    raise ValueError(f"right button must be one of {RIGHT_KINDS}: {kind!r}")


def centre_face(item, content, *, enabled=True):
    """One page-specific control: dropdown (selector recipe; there is no left/right stepper) or a text button (button recipe)."""
    kind = item.get("kind")
    if kind == "button":
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

    A spec is {"id", "file", "alt", "caption", "width", "zones": {"left": [...], "centre": [...], "right": [...]}, "skin"?}; every zone is
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
    """The examples (= specs) of a catalogue "header" entry, validated: id, file, alt, caption, width, zones (+ optional skin)."""
    required = {"id", "file", "alt", "caption", "width", "zones"}
    optional = {"skin"}
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


def skin_parts(example):
    """The geometry and the textless faces the SKIN needs for one example (the Osc header forms): the same layout as the
    catalogue picture, but every dropdown / field value is blank (the host draws the live value in a Label) and every
    face also comes in its disabled look (power off). Returns [{zone, kind, rect, on, off}]; the title has no face
    (the skin draws it as a Label in the slot `rect`)."""
    view = flat(example)
    zones = {"left": example["zones"].get("left", []),
             "centre": [dict(i, value="") if i["kind"] in ("dropdown", "field") else dict(i, selected=False) if i["kind"] == "segment" else i for i in view["centre"]],
             "right": [dict(i, value="") if i["kind"] == "dropdown" else i for i in view["right"]]}
    blank = dict(example, zones=zones)
    parts = []
    specs = {z: list(zones[z]) for z in ZONES}
    for item in layout(blank)[1]:
        on = item.face
        slot = specs[item.zone].pop(0)
        extra = {}
        if item.kind == "power":
            off = power_face("off", item.face.height)
        elif item.kind in ("title", "label"):
            on = off = None
        else:
            off = dim_artwork(on)
            if item.kind == "segment":
                extra["selected"] = segment_face(slot, item.face.height, selected=True)
        parts.append({"zone": item.zone, "kind": item.kind, "rect": item.rect, "on": on, "off": off, **extra})
    return parts


# ---------------------------------------------------------------------------------------------------------------------
# Skin: ONE entry point turns a spec into the skin definitions (hand-run by scripts/header-skin.py; nothing at build time)
# ---------------------------------------------------------------------------------------------------------------------

DIM_TEXT = "ffb9bdc9"          # the value text of a disabled (POWER off) control
TEXT = "ffeef1ff"
FIELD_TEXT = "ffebeffe"
HIT_IMAGE = "native_button_hit.png"
EMPTY_BACKGROUND = {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""}, "unfocussed": {"version": 1, "colour": "0", "image": ""}}
FOCUS_DATA = {"version": 1, "backgroundColour": "19ffffff", "outlineColour": "ff19c6e0", "backgroundInset": 2.0, "outlineThickness": 1.0}


def _bounds(rect, gates=(), focus="No", show="Show", visible="Always", kind="Absolute"):
    b = {"version": 2 if gates else 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show,
         "whenVisible": visible, "boundsType": kind, "bounds": " ".join(map(str, rect))}
    if gates:
        b["additionalInvalidatingHandles"] = list(gates)
    return b


def _comp(name, typ, data, rect, remap=(), gates=(), focus="No", show="Show", visible="Always", kind="Absolute"):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]},
            "bounds": _bounds(rect, gates, focus, show, visible, kind)}


def _image(name, file, rect):
    return _comp(name, "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": file}, rect)


def _label(name, rect, size, colour, remap=(), kind="Value", justification="horizontallyCentred verticallyCentred"):
    data = {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": size},
                                        "colour": colour, "justification": justification, "case": "Original"},
            "type": kind, "handleName": "Data"}
    return _comp(name, "Label", data, rect, remap)


def _definition(children, actions=(), inert=False, ignore_presses=None):
    return {"version": 2, "actions": list(actions), "backgroundData": EMPTY_BACKGROUND,
            "ignoreMousePresses": inert if ignore_presses is None else ignore_presses,
            "disableCoarseDataWheel": inert, "componentsData": list(children)}


def _actions(cell):
    """Show Overlay (a dropdown or number list) or Toggle Switch on both triggers, or none (a Button is the control)."""
    if cell.get("overlay"):
        handler, extra = "Show Overlay", cell["overlay"]
    elif cell.get("toggle"):
        handler, extra = "Toggle Switch", ""
    else:
        return []
    return [{"version": 1, "onAction": trigger, "handler": handler, "handleName": "Data", "additionalData": extra}
            for trigger in ("Mouse Down", "Enter Pressed")]


def _handle(slot):
    role = slot["role"]
    return "Hdr" + role[0].upper() + role[1:]


def _face_names(slot, w, h):
    if slot["kind"] == "segment":      # (selected look, plain look)
        return f"hdr_{slot['role'].lower()}_{w}x{h}_sel.png", f"hdr_{slot['role'].lower()}_{w}x{h}.png"
    prefix = "hdr_dd" if slot["kind"] == "dropdown" else "hdr_" + slot["role"].lower()
    return f"{prefix}_{w}x{h}.png", f"{prefix}_{w}x{h}_off.png"


def _dropdown_geometry(h):
    size = _style_size("selector.dropdown.size.%d" % h)
    font = round(size["font_em"] * _style_size("font.host.height.per.em"), 1)
    return size["pad_x"], _style_size("selector.dropdown")["chevron_lane"], _style_size("selector.dropdown.chevron_gap"), font


def _cell_children(slot, cell, w, h, on):
    """The children of one cell in its enabled look (on) or the inert pictures of its disabled look (not on); coordinates inside the cell."""
    face_on, face_off = _face_names(slot, w, h)
    face = face_on if on else face_off
    kind = slot["kind"]
    out = []
    if kind == "dropdown":
        pad, lane, gap, font = _dropdown_geometry(h)
        if on and cell.get("focus"):
            out.append(_comp("focus", "Focus", FOCUS_DATA, (0, 0, w, h), visible="WhenFocussed"))
        out.append(_image("Selector face", face, (0, 0, w, h)))
        out.append(_label("Title", (pad, 0, w - pad - lane - gap, h), font, TEXT if on else DIM_TEXT))
        if on and cell.get("hit"):
            hit = cell["hit"]
            out.append(_comp("Open", "Button", {"version": 2, "onImage": HIT_IMAGE, "offImage": HIT_IMAGE, "buttonId": hit["id"],
                                                "numButtonsInGroup": hit["group"], "handleName": "Data", "gestureBehaviour": "Instant"},
                             (0, 0, w, h), focus="Yes", show="Hide"))
    elif kind == "field":
        box = slot.get("box_px", 68)
        if on:
            out.append(_comp("focus", "Focus", FOCUS_DATA, (w - box, 0, box, h), visible="WhenFocussed"))
        out.append(_image("Face", face, (0, 0, w, h)))
        out.append(_label("value", (w - box, 0, box, h), 16.0, FIELD_TEXT if on else DIM_TEXT))
    elif kind == "button":
        if on:
            out.append(_comp(slot["text"], "Button", {"version": 2, "onImage": face, "offImage": face, "buttonId": cell.get("id", 1),
                                                       "numButtonsInGroup": cell.get("group", 1),
                                                       "handleName": "Data", "gestureBehaviour": "Instant"}, (0, 0, w, h), focus="Yes", show="Hide"))
        else:
            out.append(_image(slot["text"], face, (0, 0, w, h)))
    elif kind == "segment":
        # one button of a switch group: the selected look while its value is current (on image), the plain look otherwise (off image)
        out.append(_comp(slot["text"], "Button", {"version": 2, "onImage": face_on, "offImage": face_off, "buttonId": cell["id"],
                                                   "numButtonsInGroup": cell["group"], "handleName": "Data", "gestureBehaviour": "Instant"},
                         (0, 0, w, h), focus="No", show="Hide"))
    return out


def header_definitions(spec):
    """The skin definitions of ONE header spec (a catalogue example with a "skin" block): returns
    {"form": key, "definitions": [(key, definition)], "images": {file: PIL image}}.

    * the form (spec["skin"]["def"]) is one 1280 x 56 definition holding the title Label, the POWER Button and, per slot,
      the cell instance(s) of the enabled look plus the inert pictures (Image + Label) of the disabled look. Both power
      states live in this ONE definition, switched by the handle names HdrOn / HdrOff (IndexedEnabling/1|0/2/<power>) that
      the instance remaps: there is no `...Off` twin of the form or of any cell.
    * a cell definition (slot["skin"]["cells"][i]["def"]) holds only the enabled look with its actions: one per role and
      width; every form and instance that names it shares it.
    * a cell with "wrapper" keeps the proven NativeActive / NativeInactive pattern (its disabled look follows another gate
      than POWER; see docs/ui-components/header.md).
    The instance supplies the names HdrTitle, HdrPower, HdrOn, HdrOff, the page gate names and one Hdr<Role> per slot."""
    skin = spec["skin"]
    form_key = skin["def"]
    page_gate = list(skin.get("page_gate", []))
    parts = skin_parts(spec)
    view = flat(spec)
    m = metrics()
    comps, definitions, images = [], {}, {}

    title = next(p for p in parts if p["kind"] == "title")
    title_label = _label("Title", (title["rect"][0], m["padding"], title["rect"][2], m["content"]), 40.0, TEXT, [("Data", "HdrTitle")],
                         kind="Name", justification="left verticallyCentred")
    if skin.get("title_gate"):      # the title alone follows its own gate (the instance remaps HdrTitleGate)
        title_label["bounds"] = _bounds(title["rect"][:1] + (m["padding"], title["rect"][2], m["content"]), ["HdrTitleGate"], show="Show")
    comps.append(title_label)
    power = next((p for p in parts if p["kind"] == "power"), None)   # a header without POWER (a screen of a modulator, the curve screens, Mod)
    if power:
        pw, ph = power["rect"][2:]
        on_name, off_name = f"hdr_power_on_{pw}x{ph}.png", f"hdr_power_off_{pw}x{ph}.png"
        images[on_name], images[off_name] = power["on"], power["off"]
        comps.append(_comp("Power", "Button", {"version": 2, "onImage": on_name, "offImage": off_name, "buttonId": 1, "numButtonsInGroup": 1,
                                               "handleName": "Data", "gestureBehaviour": "Instant"},
                           power["rect"], [("Data", "HdrPower")], page_gate, "Yes", "Hide"))

    for zone in ("centre", "right"):
        slots = [s for s in view[zone] if s["kind"] != "more"]
        placed = [p for p in parts if p["zone"] == zone and p["kind"] != "more"]
        for slot, part in zip(slots, placed):
            rect = part["rect"]
            w, h = rect[2:]
            handle = _handle(slot)
            if slot["kind"] == "label":      # a live value the host draws: a Label straight in the form, no picture, no cell
                look = slot["skin"]["label"]
                comps.append(_label(slot["role"].title(), rect, look["size"], look["colour"], [("Data", handle)], justification=look["justify"]))
                continue
            face_on, face_off = _face_names(slot, w, h)
            if slot["kind"] == "segment":
                images[face_on], images[face_off] = part["selected"], part["on"]
            elif power:
                images[face_on], images[face_off] = part["on"], part["off"]
            else:
                images[face_on] = part["on"]
            for cell in slot["skin"]["cells"]:
                handle = _handle(dict(slot, role=cell["role"])) if cell.get("role") else _handle(slot)    # alternatives of one slot may bind other parameters (Mod: source / target group)
                gate = list(cell.get("gate", []))
                page = page_gate if cell.get("page") else []
                show = cell.get("show", "Show")
                key = cell["def"]
                wrapper = cell.get("wrapper")
                definitions[key] = _definition(_cell_children(slot, cell, w, h, True), _actions(cell))
                if wrapper:
                    definitions["NativeInactive " + key] = _definition(_cell_children(slot, cell, w, h, False), inert=True)
                    definitions[wrapper + " " + key] = _definition([
                        _comp("State", "NativeInactive " + key, {"version": 1, "handleName": "Data"}, (0, 0, 1, 1),
                              gates=["NativeActiveGate1"], show="Hide", kind="Proportional"),
                        _comp("State", key, {"version": 1, "handleName": "Data"}, (0, 0, 1, 1),
                              gates=["NativeActiveGate2"], focus="Children", show="Hide", kind="Proportional")],
                        ignore_presses=True)       # the proven NativeActive wrapper: the wrapper ignores presses, its active child takes them
                    comps.append(_comp(cell["name"], wrapper + " " + key, {"version": 1, "handleName": "Data"}, rect,
                                       [("Data", handle), ("NativeActiveGate1", "HdrGate1"), ("NativeActiveGate2", "HdrGate2")],
                                       page + gate, "No", show))
                    continue
                comps.append(_comp(cell["name"], key, {"version": 1, "handleName": "Data"}, rect, [("Data", handle)],
                                   (["HdrOn"] if power else []) + page + gate, cell.get("hw_focus", "No"), show))
                for child in (_cell_children(slot, cell, w, h, False) if power and slot["kind"] != "segment" else []):       # the disabled look: inert pictures straight in the form
                    cx, cy, cw, ch = map(int, child["bounds"]["bounds"].split())
                    b = child["bounds"]
                    child["bounds"] = _bounds((rect[0] + cx, rect[1] + cy, cw, ch), ["HdrOff"] + page + gate, b["acceptsHWFocus"],
                                              b["showWhenDataModelInvalid"], b["whenVisible"])
                    child["componentData"]["name"] = cell["name"] + " off " + child["componentData"]["name"]
                    if child["componentData"]["type"] == "Label":
                        child["handle remapping"] = {"version": 1, "map": [{"key": "Data", "value": handle}]}
                    comps.append(child)
    definitions[form_key] = _definition(comps)
    ordered = [(k, v) for k, v in definitions.items() if k != form_key] + [(form_key, definitions[form_key])]
    return {"form": form_key, "definitions": ordered, "images": images}
