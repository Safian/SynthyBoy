# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native BandLane: one band of an FX splitter (tile, effect cards, ADD card), one definition for every lane.

A lane is the band tile (a selectable 3-line gradient face in the colour of its band: name, range, effect
count), then up to `band_lane.max_cards` effect cards (the FX chain card recipe: power, name, the same faces as
the chain bar, plus a MOVE grip) and the ADD card (the chain's add face). One definition (`fxNativeBandLane`)
serves every lane of every splitter mode (L/H 2 lanes, L/M/H 3, M/S 2): each lane is an entry of it with its
own handles, so the skin carries one copy of the lane subtree. The chain bar uses the same card recipe
(`fx_card_definition`) and add face (`add_face`) with its own sizes.
"""
from pathlib import Path

from components.native.button.button import (
    action_art, button as native_button, gradient_face, semibold, in_slot, write_transparent_hit)
from components.native.name_value.name_value import _label_component
from components.native.radio_button.radio_button import render_segmented_button_art
from components.style import (
    argb as _argb, color as _color, native_color as _native_color, rgb as _rgb, size as _size)

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

DEFINITION = "fxNativeBandLane"
FLOW_TOKENS = tuple(f"theme.neon.flow.{index}" for index in range(4))   # the band colours: LOW, MID, HIGH, SIDE look
STATES = _size("band_lane.states")      # State handle: 0 hidden, then 1 + colour * 2 + dim
CARDS = _size("band_lane.max_cards")
LANES = _size("band_lane.max_lanes")
CARD_KEYS = ("Present", "Select", "Selected", "Unselected", "Power", "Powered", "Bypassed")
LANE_KEYS = ("State", "Open", "Add", "AddAvail", "Shown") + tuple(f"Face{state}" for state in range(1, STATES))


# ---- the small JSON builders every FX recipe shares ---------------------------------------------------------------------------------------------------

def _bounds(rect, accepts_focus="No", gates=()):
    bounds = {"version": 2 if gates else 1, "acceptsHWFocus": accepts_focus, "showWhenDataModelInvalid": "Hide",
              "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % tuple(rect)}
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return bounds


def native_entry(name, kind, data, rect, param=None, gates=(), focus=False):
    value = [] if param is None else [{"key": "Data", "value": f"Parameter {param}"}]
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": value}, "bounds": _bounds(rect, "Yes" if focus else "No", gates)}


def native_label(kind, rect, height, colour=None):
    child = _label_component(kind.lower(), kind, "Data", rect, height, colour or _argb("text.icon"),
                             "horizontallyCentred verticallyCentred")
    return native_entry(kind.lower(), "Label", child["componentData"]["data"], rect)


def empty_background():
    nocolour = {"version": 1, "colour": _native_color("transparent"), "image": ""}
    return {"version": 1, "focussed": dict(nocolour), "unfocussed": dict(nocolour)}


def transparent_definition(children):
    """A read-only, background-less group: its children carry the interaction."""
    return {"version": 2, "actions": [], "backgroundData": empty_background(), "ignoreMousePresses": True,
            "disableCoarseDataWheel": True, "componentsData": children}


def image_entry(name, filename, rect, gates=()):
    return native_entry(name, "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": filename}, rect, gates=gates)


# ---- the FX card recipe (the chain bar's card and the lane's card) ------------------------------------------------------------------------------------

def _card_art(skins_dir, prefix, name, size, glyph="", active=False, inactive=False, power_height=None):
    filename = f"{prefix}_{name}.png"
    if glyph == "power":
        face = action_art(icon="power", colour=_color("text.status.off" if inactive else "text.status.on"), height=power_height)
        image = in_slot(face, size)
    elif glyph:
        image = render_segmented_button_art("" if glyph == "box" else glyph, active and not inactive, *size)
    else:
        image = Image.new("RGBA", size)
    image.save(Path(skins_dir) / filename)
    return filename


def grip_face(skins_dir, prefix, size):
    """The MOVE grip of a card: the button face with three bars (the drag handle the plugin reads a drag on)."""
    filename = f"{prefix}_move.png"
    image = render_segmented_button_art("", False, *size)
    pen = ImageDraw.Draw(image)
    bar = _size("band_lane.grip_bars")
    for index in range(bar["count"]):
        y = (size[1] - (bar["count"] * bar["height"] + (bar["count"] - 1) * bar["gap"])) // 2 + index * (bar["height"] + bar["gap"])
        pen.rectangle((bar["inset"], y, size[0] - bar["inset"] - 1, y + bar["height"] - 1), fill=_color("text.muted"))
    image.save(Path(skins_dir) / filename)
    return filename


def add_face(skins_dir, prefix, size, label="+ ADD"):
    """The add card's face, the one the chain bar uses (`<prefix>_add.png`)."""
    return _card_art(skins_dir, prefix, "add", size, label)


def fx_card_definition(skins_dir, prefix, power_size, select_size, down, labels, power_height=None):
    """The definition of one FX card: a power toggle and a select button (key `Select`) with the faces of the chain bar.

    The caller's entry binds the keys Present, Select, Selected, Unselected, Power, Powered, Bypassed. `labels` is a list of
    (handle key, (left, top, reserved width, height), font height) laid on the select face."""
    power_on = _card_art(skins_dir, prefix, "on", power_size, "power", power_height=power_height)
    power_off = _card_art(skins_dir, prefix, "off", power_size, "power", inactive=True, power_height=power_height)
    faces = {"selected": _card_art(skins_dir, prefix, "selected", select_size, "box", active=True),
             "unselected": _card_art(skins_dir, prefix, "unselected", select_size, "box"),
             "bypassed": _card_art(skins_dir, prefix, "bypassed", select_size, "box", inactive=True),
             "bypassed_selected": _card_art(skins_dir, prefix, "bypassed_selected", select_size, "box", active=True, inactive=True)}
    toggle = native_button("power", (0, 0) + tuple(power_size), power_on, power_off, index=1, count=1)
    toggle["handle remapping"]["map"] = [{"key": "Data", "value": "Power"}]
    select_rect = (0, down) + tuple(select_size)
    widgets = [toggle]
    for enabled, idle, active in ((0, faces["bypassed"], faces["bypassed_selected"]), (1, faces["unselected"], faces["selected"])):
        gates = ("Powered" if enabled else "Bypassed",)
        for selection, image in (("Unselected", idle), ("Selected", active)):
            widgets.append(native_entry("Selection face", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": image},
                                        select_rect, gates=(*gates, selection)))
        for handle, offsets, height in labels:
            left, top, reserved, label_height = offsets
            label = native_label("Value", (left, down + top, select_size[0] - reserved, label_height), height,
                                 _argb("text.icon") if enabled else _argb("fx.grey.959595"))
            label["handle remapping"]["map"] = [{"key": "Data", "value": handle}]
            label["bounds"]["version"] = 2
            label["bounds"]["additionalInvalidatingHandles"] = list(gates)
            widgets.append(label)
    # The single foreground hit target receives presses anywhere on either caption.
    widgets.append(native_button("select", select_rect, write_transparent_hit(skins_dir), index=1, count=2,
                                 remaps=(("Data", "Select"),)))
    return transparent_definition(widgets)


# ---- the band tile (the selectable 3-line face) ------------------------------------------------------------------------------------------------------

def _mix(foreground, background, alpha):
    return tuple(round(b + alpha * (f - b)) for f, b in zip(foreground, background))


def tile_art(skins_dir, colour, dim, size=None):
    """The tile face of band colour `colour` (0 .. 3): the old gradient box tinted with the band colour, dimmed for a bypassed band
    or an idle splitter. Returns the file name."""
    size = tuple(size or _size("band_lane.tile")[2:])
    filename = f"band_lane_tile_{colour}_{'dim' if dim else 'lit'}.png"
    scale = _size("button.art")["scale"]
    ground, flow = _rgb("surface.canvas"), _rgb(FLOW_TOKENS[colour])
    alpha = _size("band_lane.tile_fill_alpha")[1 if dim else 0]
    spread = _size("band_lane.tile_fill_spread")
    border = _mix(flow, ground, _size("band_lane.tile_border_alpha")[1 if dim else 0])
    face, _ = gradient_face((size[0] * scale, size[1] * scale), inset=0, radius=_size("band_lane.tile_radius") * scale, frame=scale,
                            span=_size("control.gradient"), fill=(_mix(flow, ground, alpha + spread), _mix(flow, ground, max(0.0, alpha - spread))),
                            line=(border, border))
    face.resize(size, Image.Resampling.LANCZOS).save(Path(skins_dir) / filename)
    return filename


# ---- the lane definition ---------------------------------------------------------------------------------------------------------------------------

def lane_card_x(index):
    return _size("band_lane.card_x") + index * _size("band_lane.card_pitch")


def build_band_lane_definition(skins_dir):
    """`fxNativeBandLane`: the tile, the 13 cards with their MOVE grips and the ADD card of one lane. The lane entry binds LANE_KEYS and CARD_KEYS + index."""
    skins_dir = Path(skins_dir)
    height = _size("band_lane.height")
    tile = tuple(_size("band_lane.tile"))
    children = []
    for state in range(1, STATES):
        children.append(image_entry(f"Tile face {state}", tile_art(skins_dir, (state - 1) // 2, (state - 1) % 2 == 1), tile, gates=(f"Face{state}",)))
    for handle, rect, font, colour in _size("band_lane.tile_labels"):
        label = native_label("Value", tuple(rect), font, _argb(colour))
        label["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
        label["handle remapping"]["map"] = [{"key": "Data", "value": handle}]
        children.append(label)
    children.append(native_button("open", tile, write_transparent_hit(skins_dir), index=1, count=2, remaps=(("Data", "Open"),)))
    card_size = tuple(_size("band_lane.card"))
    down = _size("band_lane.card_power")[1]
    grip_image = grip_face(skins_dir, "band_lane", tuple(_size("band_lane.grip")[2:]))
    card = fx_card_definition(skins_dir, "band_lane", tuple(_size("band_lane.card_power")), tuple(_size("band_lane.card_select")), down,
                              [(handle, tuple(offsets), font) for handle, offsets, font in _size("band_lane.card_labels")], power_height=_size("band_lane.card_power")[1])
    for index in range(CARDS):
        entry = native_entry(f"Card {index + 1}", "fxNativeBandCard", {"version": 1, "handleName": "Present"},
                             (lane_card_x(index), 0) + card_size, gates=(f"Present{index}",))
        entry["bounds"]["acceptsHWFocus"] = "Children"
        entry["handle remapping"]["map"] = [{"key": key, "value": f"{key}{index}"} for key in CARD_KEYS]
        children.append(entry)
        grip = _size("band_lane.grip")
        children.append(image_entry(f"Move {index + 1}", grip_image, (lane_card_x(index) + grip[0], grip[1], grip[2], grip[3]),
                                    gates=(f"Present{index}", f"Powered{index}")))
    add = tuple(_size("band_lane.add"))
    add_button = native_button("add", add, add_face(skins_dir, "band_lane", add[2:]), index=1, count=2,
                               remaps=(("Data", "Add"),), gates=("AddAvail",))
    children.append(add_button)
    definition = transparent_definition(children)
    return {DEFINITION: definition, "fxNativeBandCard": card}


def lane_handles(base_parameter, lane):
    """The remap of lane `lane` (0 ..): `base_parameter` is the host index of the first band-lane handle (fxbands field 0)."""
    from_native = _size("band_lane.handles")
    first = base_parameter + lane * from_native["lane"]
    fields = from_native["fields"]
    parameter = lambda field: f"Parameter {first + field}"
    values = {"State": parameter(fields["State"]), "Open": parameter(fields["Open"]), "Add": parameter(fields["Add"]),
              "AddAvail": f"IndexedEnabling/1/2/{parameter(fields['AddAvail'])}",
              "Shown": f"RangedEnabling/1/{STATES - 1}/{STATES}/{parameter(fields['State'])}"}
    for state in range(1, STATES):
        values[f"Face{state}"] = f"IndexedEnabling/{state}/{STATES}/{parameter(fields['State'])}"
    count = parameter(fields["Count"])
    for index in range(CARDS):
        select, power = first + fields["Select"] + index, first + fields["Power"] + index
        values.update({f"Present{index}": f"RangedEnabling/{index + 1}/{CARDS}/{CARDS + 1}/{count}",
                       f"Select{index}": f"Parameter {select}", f"Selected{index}": f"IndexedEnabling/1/2/Parameter {select}",
                       f"Unselected{index}": f"IndexedEnabling/0/2/Parameter {select}",
                       f"Power{index}": f"Parameter {power}", f"Powered{index}": f"IndexedEnabling/1/2/Parameter {power}",
                       f"Bypassed{index}": f"IndexedEnabling/0/2/Parameter {power}"})
    values.update({"Range": parameter(fields["Range"]), "Info": parameter(fields["Info"])})
    return values


def build_band_lane_instance(name, rect, base_parameter, lane):
    """One lane of the skin: an entry of `fxNativeBandLane` bound to lane `lane`'s handles; hidden while its State is 0."""
    values = lane_handles(base_parameter, lane)
    entry = native_entry(name, DEFINITION, {"version": 1, "handleName": "Data"}, rect, gates=(values["Shown"],))
    entry["bounds"]["acceptsHWFocus"] = "Children"
    entry["handle remapping"]["map"] = [{"key": key, "value": value} for key, value in values.items()]
    return entry


def lane_rect(lane):
    return (_size("band_lane.x"), _size("band_lane.y") + lane * _size("band_lane.pitch"),
            _size("band_lane.add")[0] + _size("band_lane.add")[2], _size("band_lane.height"))


# ---- the catalogue picture ----------------------------------------------------------------------------------------------------------------------------

def render_band_lane(path, skins_dir=None):
    """Three lanes of an L/M/H splitter as the skin draws them (faces from the recipes above, sample texts)."""
    import tempfile
    samples = (("LOW", "below 220 Hz", "1 effect", 0, False, [("Distortion", True)]),
               ("MID", "220 Hz to 2.5 kHz", "2 effects", 1, False, [("Utility", True), ("Chorus", False)]),
               ("HIGH", "above 2.5 kHz", "BYPASS", 2, True, []))
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(skins_dir) if skins_dir else Path(tmp)
        definitions = build_band_lane_definition(out)
        width, height = lane_rect(0)[2:]
        pad, pitch = 8, _size("band_lane.pitch")
        canvas = Image.new("RGBA", (width + 2 * pad, pitch * len(samples) + 2 * pad), _color("surface.canvas"))
        draw = ImageDraw.Draw(canvas)
        for row, (name, span, info, colour, dim, cards) in enumerate(samples):
            top = pad + row * pitch
            canvas.alpha_composite(Image.open(out / tile_art(out, colour, dim)), (pad, top))
            for text, rect, font, token in _size("band_lane.tile_labels"):
                value = {"State": name, "Range": span, "Info": info}[text]
                draw.text((pad + rect[0], top + rect[1] + rect[3] / 2), value, font=semibold(font), fill=_color(token), anchor="lm")
            for index, (label, on) in enumerate(cards):
                x = pad + lane_card_x(index)
                power = _card_art(out, "band_lane", "on" if on else "off", tuple(_size("band_lane.card_power")), "power",
                                  inactive=not on, power_height=_size("band_lane.card_power")[1])
                canvas.alpha_composite(Image.open(out / power), (x, top))
                grip = _size("band_lane.grip")
                canvas.alpha_composite(Image.open(out / "band_lane_move.png"), (x + grip[0], top + grip[1]))
                select = Image.open(out / ("band_lane_unselected.png" if on else "band_lane_bypassed.png"))
                canvas.alpha_composite(select, (x, top + _size("band_lane.card_power")[1]))
                draw.text((x + select.width / 2, top + _size("band_lane.card_power")[1] + select.height / 2), label, font=semibold(12),
                          fill=_color("text.icon" if on else "fx.grey.959595"), anchor="mm")
            add = _size("band_lane.add")
            canvas.alpha_composite(Image.open(out / "band_lane_add.png"), (pad + add[0], top + add[1]))
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        canvas.save(path)
    return str(path)


class BandLane:
    """Component namespace of the BandLane."""
    build_definitions = staticmethod(build_band_lane_definition)
    build_instance = staticmethod(build_band_lane_instance)
    render_preview = staticmethod(render_band_lane)
