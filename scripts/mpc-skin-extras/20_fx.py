# SynthyBoy - MPC skin extra: native controls throughout standalone FX (tab FX).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The overlay host renders the live editor cells according to the runtime
# layout and per-slot kind. The non-overlay skin still has the order and
# enable grid, bound directly to the original parameters.

import copy
import json
import tempfile
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components import style
from components.native.level.level import compact_dial_definition, default_level_template, synthyboy_dial_data
from components.native.active_text.active_text import write_active_text_assets, active_text_child
from components.native.radio_button.radio_button import create_segmented_radio_button_group, render_segmented_button_art
from components.native.button.button import action_button, action_art, in_slot, button as native_button

from native_ui_params import (FX_CELL_COUNT, FX_DOMAIN_COUNT, FX_DOMAIN_OFFSET,
                              FX_BAND_BASE, FX_BAND_MODE, FX_HEADER, FX_HEADER_FIELDS, FX_HEADER_ITEMS,
                              FX_KIND_OFFSET, FX_LAYOUT_OFFSET, FX_VALUE_OFFSET,
                              FX_CHAIN, MIXER_PEEK_VIEW,
                              mixer_peek_index)

CONTRACT = Path(__file__).with_name("fx-native-contract.json")
FX_DIAL, FX_TOGGLE, FX_FIRST_DOMAIN = 1, 2, 3
FX_NUMBER_DOMAINS = {18, 19}  # Chorus Voices and Hyperdimension Unison
FX_CHOICE_MENU = "fx editor choice menu"
FX_NUMBER_MENU = "fx editor number menu"
FX_RACK_MENU = "fx editor rack menu"
FX_ADD_MENU = "fx chain add menu"
# Full-screen popup marker coordinates and colours match the overlay detector.
FX_MENU_MARKERS = ((16, "fe356c"), (28, "24c7ff"), (40, "90ed43"))

COLS = (16, 173, 330, 487, 644, 801, 958, 1115)
ROWS = (426, 526)
KNOB_W, KNOB_H = 149, 92
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 301, 323

SELECTOR_DEF = "fxNativeSelector"
TOGGLE_DEF = "fxNativeToggle"

# Controls for FX tab: (name suffix, params.json key, widget kind, row, col)
FX_CONTROLS = (
    # Row 0 (y=426): FX Order 1..8
    ("Order 1", "fx.order.1", "selector", 0, 0),
    ("Order 2", "fx.order.2", "selector", 0, 1),
    ("Order 3", "fx.order.3", "selector", 0, 2),
    ("Order 4", "fx.order.4", "selector", 0, 3),
    ("Order 5", "fx.order.5", "selector", 0, 4),
    ("Order 6", "fx.order.6", "selector", 0, 5),
    ("Order 7", "fx.order.7", "selector", 0, 6),
    ("Order 8", "fx.order.8", "selector", 0, 7),
    # Row 1 (y=526): FX Order 9 and the 7 module On toggles
    ("Order 9", "fx.order.9", "selector", 1, 0),
    ("Filter On", "fxfilter.on", "toggle", 1, 1),
    ("Distortion On", "distortion.on", "toggle", 1, 2),
    ("Compressor On", "compressor.on", "toggle", 1, 3),
    ("EQ On", "eq.on", "toggle", 1, 4),
    ("Chorus On", "chorus.on", "toggle", 1, 5),
    ("Delay On", "delay.on", "toggle", 1, 6),
    ("Reverb On", "reverb.on", "toggle", 1, 7),
)


def _qlink(row, col):
    return "Q-Link %d" % (8 * (1 - row) + col + 1)


def _bounds(rect, accepts_focus="No", when_visible="Always"):
    return {
        "version": 1,
        "acceptsHWFocus": accepts_focus,
        "showWhenDataModelInvalid": "Show",
        "whenVisible": when_visible,
        "boundsType": "Absolute",
        "bounds": "%d %d %d %d" % tuple(rect),
    }


def _empty_bg():
    nocolour = {"version": 1, "colour": "00000000", "image": ""}
    return {"version": 1, "focussed": dict(nocolour), "unfocussed": dict(nocolour)}


def _focus_child(w, h):
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "focus",
            "type": "Focus",
            "data": {
                "version": 1,
                "backgroundColour": "19ffffff",
                "outlineColour": "ffd31145",
                "backgroundInset": 4.0,
                "outlineThickness": 1.0,
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds((0, 0, w, h), "No", "WhenFocussed"),
    }


def _build_selector_definition(base_def):
    if base_def is not None:
        defn = copy.deepcopy(base_def)
    else:
        defn = {
            "version": 2,
            "actions": [
                {
                    "version": 1,
                    "onAction": "Mouse Down",
                    "handler": "Q-Link",
                    "handleName": "Data",
                    "additionalData": "",
                },
                {
                    "version": 1,
                    "onAction": "Double Click",
                    "handler": "Show Overlay",
                    "handleName": "Data",
                    "additionalData": "menu overlay",
                },
                {
                    "version": 1,
                    "onAction": "Enter Pressed",
                    "handler": "Show Overlay",
                    "handleName": "Data",
                    "additionalData": "menu overlay",
                },
            ],
            "backgroundData": _empty_bg(),
            "ignoreMousePresses": False,
            "disableCoarseDataWheel": False,
            "componentsData": [
                _focus_child(KNOB_W, KNOB_H),
                {
                    "version": 2,
                    "componentData": {
                        "version": 1,
                        "name": "slider",
                        "type": "Slider",
                        "data": {"version": 1, "handleName": "Data"},
                    },
                    "handle remapping": {"version": 1, "map": []},
                    "bounds": _bounds((6, 7, KNOB_W - 12, KNOB_H - 14)),
                },
            ],
        }
    for child in defn.get("componentsData", []):
        cname = child.get("componentData", {}).get("name")
        if cname == "focus":
            child["bounds"]["bounds"] = "0 0 %d %d" % (KNOB_W, KNOB_H)
        elif cname == "slider":
            child["bounds"]["bounds"] = "6 7 %d %d" % (KNOB_W - 12, KNOB_H - 14)
    return defn


def _build_toggle_definition(base_def):
    if base_def is not None:
        defn = copy.deepcopy(base_def)
    else:
        defn = {
            "version": 2,
            "actions": [
                {
                    "version": 1,
                    "onAction": "Mouse Down",
                    "handler": "Q-Link",
                    "handleName": "Data",
                    "additionalData": "",
                },
                {
                    "version": 1,
                    "onAction": "Enter Pressed",
                    "handler": "Show Overlay",
                    "handleName": "Data",
                    "additionalData": "knob overlay",
                },
            ],
            "backgroundData": _empty_bg(),
            "ignoreMousePresses": False,
            "disableCoarseDataWheel": False,
            "componentsData": [],
        }

    name_label = {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "name",
            "type": "Label",
            "data": {
                "version": 1,
                "textStyle": {
                    "version": 1,
                    "font": {
                        "version": 1,
                        "name": "Titillium Web",
                        "style": "SemiBold",
                        "height": 16.0,
                    },
                    "colour": "ff6a6a6c",
                    "justification": "horizontallyCentred verticallyCentred",
                    "case": "Upper Case",
                },
                "type": "Name",
                "handleName": "Data",
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds((4, 2, KNOB_W - 8, 22)),
    }

    toggle_btn = None
    for child in defn.get("componentsData", []):
        if child.get("componentData", {}).get("name") == "toggle":
            toggle_btn = copy.deepcopy(child)
            break
    if toggle_btn is None:
        toggle_btn = {
            "version": 2,
            "componentData": {
                "version": 1,
                "name": "toggle",
                "type": "Button",
                "data": {
                    "version": 2,
                    "onImage": "toggle_on.png",
                    "offImage": "toggle_off.png",
                    "buttonId": 1,
                    "numButtonsInGroup": 1,
                    "handleName": "Data",
                    "gestureBehaviour": "Instant",
                },
            },
            "handle remapping": {"version": 1, "map": []},
            "bounds": _bounds((32, 24, 84, 68)),
        }
    else:
        toggle_btn["bounds"]["bounds"] = "%d 24 84 68" % ((KNOB_W - 84) // 2)

    defn["componentsData"] = [
        _focus_child(KNOB_W, KNOB_H),
        name_label,
        toggle_btn,
    ]
    return defn


def _ensure_definitions(tui):
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    by_key = {d["key"]: d["value"] for d in defs}
    if SELECTOR_DEF not in by_key:
        sel = _build_selector_definition(by_key.get("sliderWithFocusMenu"))
        defs.append({"key": SELECTOR_DEF, "value": sel})
        by_key[SELECTOR_DEF] = sel
    if TOGGLE_DEF not in by_key:
        tog = _build_toggle_definition(by_key.get("toggle"))
        defs.append({"key": TOGGLE_DEF, "value": tog})
        by_key[TOGGLE_DEF] = tog


def _native_entry(name, kind, data, rect, param=None, gates=(), focus=False):
    value = [] if param is None else [{"key": "Data", "value": f"Parameter {param}"}]
    bounds = _bounds(rect, "Yes" if focus else "No")
    bounds["showWhenDataModelInvalid"] = "Hide"
    if gates:
        bounds["version"] = 2
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": value},
        "bounds": bounds,
    }


def _native_label(kind, rect, height):
    return _native_entry(kind.lower(), "Label", {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {"version": 1, "name": style.size("font.family"), "style": style.size("font.style.semibold"), "height": float(height)},
            "colour": style.argb("text.icon"),
            "justification": "horizontallyCentred verticallyCentred",
            "case": "Original",
        },
        "type": kind,
        "handleName": "Data",
    }, rect)


def _native_definition(children, popup=None, read_only=False):
    background = {"version": 1, "colour": style.argb("surface.control"), "image": ""}
    actions = [{"version": 1, "onAction": trigger, "handler": "Show Overlay",
                "handleName": "Data", "additionalData": popup}
               for trigger in ("Mouse Down", "Enter Pressed")] if popup is not None else []
    return {
        "version": 2,
        "actions": actions,
        "backgroundData": {"version": 1, "focussed": dict(background, colour=style.argb("field.focus")), "unfocussed": background},
        "ignoreMousePresses": read_only,
        "disableCoarseDataWheel": read_only,
        "componentsData": children,
    }


def _native_dial(ctx, bipolar=False):
    rects = {"focus": (0, 0, 310, 72), "knob": (12, 4, 64, 64),
             "name": (88, 0, 214, 32), "value": (88, 32, 214, 40)}
    definition = compact_dial_definition(default_level_template(), rects=rects)
    if bipolar:
        # The Level API uses one filename per size: isolate and rename bipolar
        # output so it cannot replace the unipolar filmstrip in the same skin.
        with tempfile.TemporaryDirectory() as folder:
            data = synthyboy_dial_data(folder, frame_size=64, bipolar=True)
            filename = "fx_level_bipolar_64.png"
            (ctx.skins_dir / filename).write_bytes((Path(folder) / data["filmStrip"]).read_bytes())
            data["filmStrip"] = filename
    else:
        data = synthyboy_dial_data(ctx.skins_dir, frame_size=64)
    for child in definition["componentsData"]:
        model = child["componentData"]
        if model["name"] == "knob":
            model["data"] = data
        elif model["name"] == "name":
            model["data"]["handleName"] = "Data"
    value = _native_label("Value", rects["value"], style.size("font.value"))
    value["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
    definition["componentsData"].append(value)
    return definition


def _native_editor_definitions(ctx):
    choices = [_native_label("Name", (8, 0, 294, 28), 16),
               _native_label("Value", (8, 28, 294, 44), 20)]
    on, off = write_active_text_assets(ctx.skins_dir, "ON", width=76, height=56,
                                      on_name="fx_active_on.png", off_name="fx_active_off.png")
    return {
        "fxNativeCellUni": _native_dial(ctx),
        "fxNativeCellBi": _native_dial(ctx, bipolar=True),
        "fxNativeCellSwitch": _native_definition([
            active_text_child("active", on, off, (12, 8, 76, 56), handle="Data"),
            _native_label("Name", (96, 4, 206, 64), 19),
        ]),
        "fxNativeCellChoice": _native_definition(choices, popup=FX_CHOICE_MENU),
        "fxNativeCellNumber": _native_definition([
            _native_label("Value", (12, 4, 80, 64), 24),
            _native_label("Name", (92, 4, 210, 64), 19),
        ], popup=FX_NUMBER_MENU),
        "fxNativeCellReadOnly": _native_definition(copy.deepcopy(choices), read_only=True),
    }


def _add_native_popups(tui, ctx):
    from PIL import Image

    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    stock = next(d["value"] for d in defs if d["key"] == "menu overlay")
    markers = []
    for i, (x, rgb) in enumerate(FX_MENU_MARKERS, 1):
        filename = f"fx_editor_modal_marker_{i}.png"
        Image.new("RGB", (6, 6), "#" + rgb).save(ctx.skins_dir / filename)
        marker = _native_entry(f"FX Native Popup Marker {i}", "Image",
                               {"version": 2, "imageType": "Regular", "colour": "0", "image": filename},
                               (x, 120, 6, 6))
        marker["bounds"]["showWhenDataModelInvalid"] = "Show"
        markers.append(marker)
    for name, kind in ((FX_CHOICE_MENU, "MenuOverlay"), (FX_NUMBER_MENU, "NumericOverlay"),
                       (FX_RACK_MENU, "MenuOverlay"), (FX_ADD_MENU, "MenuOverlay")):
        popup = copy.deepcopy(stock)
        overlay = next(c["componentData"] for c in popup["componentsData"] if c["componentData"]["type"] == "MenuOverlay")
        overlay["type"] = kind
        if kind == "NumericOverlay":
            overlay["data"].pop("withNumbers")
        popup["componentsData"].extend(copy.deepcopy(markers))
        if name in (FX_RACK_MENU, FX_ADD_MENU):
            rack = name == FX_RACK_MENU
            control = "Rack" if rack else "Add"
            filename = f"fx_{control.lower()}_popup_marker.png"
            Image.new("RGB", (6, 6), "#f1ae47" if rack else "#ef72bf").save(ctx.skins_dir / filename)
            action = _native_entry(f"FX {control} Popup Marker", "Image",
                                   {"version": 2, "imageType": "Regular", "colour": "0", "image": filename},
                                   (52 if rack else 64, 120, 6, 6))
            action["bounds"]["showWhenDataModelInvalid"] = "Show"
            popup["componentsData"].append(action)
        ctx.add_component_definition(name, popup)


def _add_native_chain(tui, tab, ctx, contract, base):
    view, count, select = range(base, base + 3)
    slots = len(contract["cards"])
    power = select + slots
    gallery = power + slots
    menu = gallery + len(contract["gallery"])
    choose, back, page = menu + 1, menu + 2, menu + 3

    def art(name, size, glyph="", colour=None, active=False, inactive=False):
        from PIL import Image
        filename = f"fx_chain_{name}.png"
        label = "⏻" if glyph == "power" else "" if glyph == "box" else glyph
        image = (in_slot(action_art(icon="power", colour=style.color("text.status.off" if inactive else "text.status.on")), size)
                 if glyph == "power" else render_segmented_button_art(label, active and not inactive, *size)
                 if glyph else Image.new("RGBA", size))
        image.save(ctx.skins_dir / filename)
        return filename

    def button(name, param, bounds, gates, off, on=None, index=1, domain=1):
        return _native_entry(name, "Button", {"version": 2, "onImage": on or off, "offImage": off,
                             "buttonId": index, "numButtonsInGroup": domain, "handleName": "Data",
                             "gestureBehaviour": "Instant"}, bounds, param, gates, True)

    def labelled_button(name, kind, size, button_art):
        w, h = size
        label = _native_label(kind, (2, h - 38, w - 4, 30), 15)
        target = button("button", None, (0, 0, w, h), (), button_art)
        definition = _native_definition([target, label])
        definition["backgroundData"] = _empty_bg()
        ctx.add_component_definition(name, definition)

    def gate(lo, hi=None):
        if hi is None:
            return f"IndexedEnabling/{lo}/7/Parameter {view}"
        return f"RangedEnabling/{lo}/{hi}/7/Parameter {view}"

    items = tab["componentDefinition"]["componentsData"]

    def transparent(children, read_only=False):
        definition = _native_definition(children, read_only=read_only)
        definition["backgroundData"] = _empty_bg()
        return definition

    def repeated(name, tile_type, rect, rows, stride, remap, visible):
        x, y, _, h = rect
        tile = _native_entry("tile", tile_type, {"version": 1, "handleName": "Data"},
                             (0, 0, stride, h), gates=(visible,))
        tile["bounds"]["acceptsHWFocus"] = "Children"
        tile["handle remapping"]["map"] = [{"key": key, "value": handle} for key, handle in remap.items()]
        layer = transparent([tile], read_only=True)
        layer["version"] = 3
        children = layer.pop("componentsData")
        layer["repeats"] = rows
        layer["componentsData"] = children
        ctx.add_component_definition(name, layer)
        entry = _native_entry(f"FX {name}", name, {"version": 1, "handleName": "Data"},
                              (x, y, stride * rows, h), view)
        entry["bounds"]["acceptsHWFocus"] = "Children"
        items.append(entry)

    def increment(offset):
        return f"{{Increment:Base={offset}}}"

    power_rect, select_rect = contract["cards"][0]
    w, h = power_rect[2:]
    down = select_rect[1] - power_rect[1]
    size = (w, h)
    power_on = art("on", size, "power", "#5cff9d")
    power_off = art("off", size, "power", "#ff4f4f", inactive=True)
    select_on = art("selected", size, "box", active=True)
    select_off = art("unselected", size, "box")
    select_bypassed = art("bypassed", size, "box", inactive=True)
    select_bypassed_on = art("bypassed_selected", size, "box", active=True, inactive=True)
    toggle_power = button("power", None, (0, 0, w, h), (), power_off, power_on)
    toggle_power["handle remapping"]["map"] = [{"key": "Data", "value": "Power"}]
    card_widgets = [toggle_power]
    for enabled, idle, active in ((0, select_bypassed, select_bypassed_on), (1, select_off, select_on)):
        gates = (f"IndexedEnabling/{enabled}/2/Power",)
        select_card = button("select", None, (0, down, w, h), gates, idle, active, domain=2)
        select_card["handle remapping"]["map"] = [{"key": "Data", "value": "Select"}]
        card_widgets.append(select_card)
        for handle, label_bounds, height in (("Select", (2, down + 3, w - 4, 27), 13),
                                             ("Power", (2, down + 30, w - 4, 23), 12)):
            label = _native_label("Value", label_bounds, height)
            label["handle remapping"]["map"] = [{"key": "Data", "value": handle}]
            label["componentData"]["data"]["textStyle"]["colour"] = "ffe6e6e6" if enabled else "ff959595"
            label["bounds"]["version"] = 2
            label["bounds"]["additionalInvalidatingHandles"] = list(gates)
            card_widgets.append(label)
    ctx.add_component_definition("fxNativeChainCard", transparent(card_widgets, True))
    repeated("fxNativeChainRow", "fxNativeChainCard", (power_rect[0], power_rect[1], w, down + h), slots,
             contract["cards"][1][0][0] - power_rect[0],
             {"Data": f"Parameter {increment(select)}", "Power": f"Parameter {increment(power)}",
              "Select": f"Parameter {increment(select)}"},
             f"RangedEnabling/{increment(1)}/{slots}/{slots + 1}/Parameter {count}")

    add = contract["add"]
    caption = _native_label("Text", (0, 0, add[2], add[3]), 16)
    caption["componentData"]["data"].update(text="+ ADD", handleName="Invalid")
    caption["bounds"]["showWhenDataModelInvalid"] = "Show"
    definition = _native_definition([caption], popup=FX_ADD_MENU)
    definition["disableCoarseDataWheel"] = True
    ctx.add_component_definition("fxNativeChainAdd", definition)
    items.append(_native_entry("FX Chain Add Menu", "fxNativeChainAdd", {"version": 1, "handleName": "Data"},
                              add, menu, (gate(2, 3),), True))
    items.append(button("FX Chain Filtered Add", choose, add, (gate(5),), art("add", add[2:], "+ ADD")))
    back_rect = contract["back"]
    items.append(button("FX Chain Back", back, back_rect, (f"IndexedEnabling/1/2/Parameter {back}",),
                        art("back", back_rect[2:], "BACK")))
    page_rect = contract["page"]
    for current, target in ((1, 2), (2, 1)):
        items.append(button(f"FX Chain Page {current}", page, page_rect,
                            (f"IndexedEnabling/{current}/3/Parameter {page}",),
                            art(f"page_{current}", page_rect[2:], f"{current} / 2"), index=target, domain=3))
    drag_rect = contract["drag"]
    move_comp = button("FX Chain Move", None, drag_rect,
                       (f"RangedEnabling/1/{slots}/{slots + 1}/Parameter {count}",),
                       art("move", drag_rect[2:], "MOVE"))
    move_comp["bounds"]["acceptsHWFocus"] = "No"
    items.append(move_comp)

    cards = contract["gallery"]
    gallery_size = tuple(cards[0][1][2:])
    labelled_button("fxNativeGalleryCard", "Name", gallery_size, art("gallery_card", gallery_size, "box"))
    columns = len(cards) // 2
    for row in range(2):
        param = gallery + row * columns
        repeated(f"fxNativeGalleryRow{row + 1}", "fxNativeGalleryCard",
                 cards[row * columns][1], columns, cards[1][1][0] - cards[0][1][0],
                 {"Data": f"Parameter {increment(param)}"},
                 f"IndexedEnabling/1/2/Parameter {increment(param)}")
    tui["pageData"].setdefault("replacementStrings", []).append(
        {"key": f"Parameter {menu}", "value": ["Select an effect"] + [name for name, _ in contract["gallery"]]})


def _add_native_header(tui, tab, ctx, headers, band, base, panels):
    rack_names = ("MAIN", "BUS 1", "BUS 2", "L/H LOW", "L/H HIGH", "L/M/H LOW",
                  "L/M/H MID", "L/M/H HIGH", "M/S MID", "M/S SIDE")
    single, multi = headers
    y, height = single["power"][1], single["power"][3]
    layout_parameter = f"Parameter {base + FX_HEADER + 1}"
    frame_count = 7 + 2 * len(panels)
    handles = ("Power", "Rack", "Page", "Remove")
    children = []
    power_images = []
    for name, colour in (("on", "text.status.on"), ("off", "text.status.off")):
        filename = f"fx_header_power_{name}.png"
        in_slot(action_art(icon="power", colour=style.color(colour)), (56, 56)).save(ctx.skins_dir / filename)
        power_images.append(filename)
    children.append(native_button("power", (single["power"][0], 0, 56, 56), *power_images,
                                  remaps=(("Data", "Power"),), gates=(f"NonZero/{layout_parameter}",)))
    rack_definition = _native_definition([
        _native_label("Name", (0, 0, 230, 24), 16),
        _native_label("Value", (0, 24, 230, 32), 20)], popup=FX_RACK_MENU)
    rack_definition["componentsData"][0]["componentData"]["data"].update(type="Text", text="RACK", handleName="")
    rack_definition["disableCoarseDataWheel"] = True
    ctx.add_component_definition("fxNativeHeaderRack", rack_definition)
    x, yy, w, h = single["rack"]
    rack = _native_entry("rack", "fxNativeHeaderRack", {"version": 1, "handleName": "Data"},
                         (x, yy-y, w, h), gates=("NonZero/Power",), focus=True)
    rack["handle remapping"]["map"] = [{"key": "Data", "value": "Rack"}]
    children.append(rack)
    x, yy, w, h = single["remove"]
    children.append(action_button(ctx, "remove", (x, yy-y, w, h), "Remove", text="REMOVE", gates=("NonZero/Power",)))

    # Header path: shows current rack name (e.g. "MAIN", "BUS 1", etc.)
    path_lbl = _native_label("Value", (500, 0, 390, 56), 16)
    path_lbl["componentData"]["name"] = "fx_header_path"
    path_lbl["handle remapping"]["map"] = [{"key": "Data", "value": "Rack"}]
    path_lbl["componentData"]["data"]["textStyle"]["justification"] = "right verticallyCentred"
    path_lbl["componentData"]["data"]["textStyle"]["colour"] = style.argb("text.label")
    path_lbl["bounds"]["additionalInvalidatingHandles"] = [f"NonZero/{layout_parameter}"]
    path_lbl["bounds"]["version"] = 2
    children.append(path_lbl)

    # Header effect name for all 16 effect kinds
    kind_names = [
        "Chorus", "Compressor", "Delay", "Distortion", "EQ",
        "FX Filter", "Flanger", "Phaser", "Reverb", "Hyper/Dimension",
        "Utility", "Bode", "Convolve", "Splitter L/H", "Splitter L/M/H", "Splitter M/S"
    ]
    for kind_idx, kname in enumerate(kind_names, 1):
        off_frame = 7 + 2 * (kind_idx - 1)
        on_frame = off_frame + 1
        # Active title (when effect is ON)
        lbl_on = _native_label("Text", (78, 0, 420, 56), 22)
        lbl_on["componentData"]["name"] = f"fx_title_on_{kind_idx}"
        lbl_on["componentData"]["data"].update(type="Text", text=kname.upper(), handleName="")
        lbl_on["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
        lbl_on["componentData"]["data"]["textStyle"]["colour"] = style.argb("text.white")
        lbl_on["bounds"]["additionalInvalidatingHandles"] = [f"IndexedEnabling/{on_frame}/{frame_count}/{layout_parameter}"]
        lbl_on["bounds"]["version"] = 2
        children.append(lbl_on)

        # Inactive title (when effect is OFF)
        lbl_off = _native_label("Text", (78, 0, 420, 56), 22)
        lbl_off["componentData"]["name"] = f"fx_title_off_{kind_idx}"
        lbl_off["componentData"]["data"].update(type="Text", text=kname.upper(), handleName="")
        lbl_off["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
        lbl_off["componentData"]["data"]["textStyle"]["colour"] = style.argb("text.muted")
        lbl_off["bounds"]["additionalInvalidatingHandles"] = [f"IndexedEnabling/{off_frame}/{frame_count}/{layout_parameter}"]
        lbl_off["bounds"]["version"] = 2
        children.append(lbl_off)
    for panel in panels:
        kind, names = panel["kind"], panel["names"]
        off_frame, on_frame = 7 + 2 * (kind - 1), 8 + 2 * (kind - 1)
        if len(names) > 1:
            x, yy, w, h = multi["tabs"]
            # Fixed 3-choice normalization (0, .5, 1) on the existing Page handle.
            # A 2-tab effect still emits .5 for its second tab, not 1.0.
            gate = f"RangedEnabling/{off_frame}/{on_frame}/{frame_count}/{layout_parameter}"
            group = create_segmented_radio_button_group(f"FX panel {kind}", (x, yy-y, w, h), names,
                                                         "Page", ctx.skins_dir, gates=(gate,))
            for entry in group:
                entry["componentData"]["data"]["numButtonsInGroup"] = 3
            children.extend(group)

    x, top, width, h = band
    mode = f"Parameter {base + FX_BAND_MODE}"
    group = create_segmented_radio_button_group("Band", (x, top-y, width, 56),
                                               ("LOW", "MID", "HIGH"), "Band", ctx.skins_dir,
                                               gates=(f"NonZero/{mode}",))
    for i, entry in enumerate(group):
        entry["componentData"]["data"].update(buttonId=i+1, numButtonsInGroup=4)
        entry["handle remapping"]["map"][0]["value"] = f"WithName[{('LOW', 'MID', 'HIGH')[i]}]/FromGesture/Band"
        gate = f"RangedEnabling/1/2/4/{mode}" if i == 0 else f"RangedEnabling/2/3/4/{mode}" if i == 2 else f"NonZero/{mode}"
        entry["bounds"]["additionalInvalidatingHandles"] = [gate]
    children.extend(group)
    for i in (0, 2):
        button = group[i]
        passive = copy.deepcopy(button)
        passive["componentData"].update(name=button["componentData"]["name"] + " disabled", type="Image",
                                        data={"version": 2, "imageType": "Regular", "colour": "0",
                                              "image": button["componentData"]["data"]["offImage"]})
        passive["bounds"]["additionalInvalidatingHandles"] = [f"IndexedEnabling/{3 if i == 0 else 1}/4/{mode}"]
        passive["bounds"]["acceptsHWFocus"] = "No"
        children.append(passive)
    layer = "fxNativeHeader"
    ctx.add_component_definition(layer, {"version": 2, "actions": [], "backgroundData": _empty_bg(),
                                         "ignoreMousePresses": True, "disableCoarseDataWheel": True,
                                         "componentsData": children})
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    legacy_modules = next(p["choices"] for p in ctx.params if p["key"] == "fx.order.1")
    compressor = legacy_modules.index("Compressor")
    for item in range(FX_HEADER_ITEMS):
        parameter = base + FX_HEADER + 2 + item * FX_HEADER_FIELDS
        entry = _native_entry(f"FX Native Header {item + 1}", layer, {"version": 1, "handleName": "Data"}, (0, y, 1280, 628-y),
                             gates=(f"IndexedEnabling/{item+1}/{FX_HEADER_ITEMS+1}/Parameter {base+FX_HEADER}",))
        entry["bounds"]["acceptsHWFocus"] = "Children"
        entry["handle remapping"]["map"] = [{"key": key, "value": f"Parameter {parameter+field}"}
                                              for field, key in enumerate(handles)]
        if item == compressor or item >= len(legacy_modules):
            entry["handle remapping"]["map"].append({"key": "Band", "value": f"Parameter {base+FX_BAND_BASE+item}"})
        tab["componentDefinition"]["componentsData"].append(entry)
        replacements.append({"key": f"Parameter {parameter+1}", "value": list(rack_names if item < 13 else rack_names[:3])})


def _apply_native_editor(tui, tab, ctx):
    # Regenerate from the repository root with:
    # fx_native_ui_host --skin-contract > scripts/mpc-skin-extras/fx-native-contract.json
    # The exporter walks both pages and all conditional states using the C++ geometry.
    manifest = json.loads(CONTRACT.read_text(encoding="utf-8"))
    layouts = manifest["layouts"]
    domains = manifest["domains"]
    reserves = manifest["kind_last"] + 1  # kind zero is hidden
    if manifest["kind_last"] != FX_FIRST_DOMAIN + len(domains) or len(domains) != FX_DOMAIN_COUNT:
        raise ValueError("FX cell manifest does not match the shell domain range")

    for name, definition in _native_editor_definitions(ctx).items():
        ctx.add_component_definition(name, definition)
    _add_native_popups(tui, ctx)

    base = mixer_peek_index(ctx.param_count)
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    for i, choices in enumerate(domains):
        replacements.append({"key": f"Parameter {base + FX_DOMAIN_OFFSET + i}", "value": choices})

    items = tab["componentDefinition"]["componentsData"]
    items.clear()
    _add_native_header(tui, tab, ctx, manifest["editor_headers"], manifest["chain"]["band"], base, manifest["panels"])
    _add_native_chain(tui, tab, ctx, manifest["chain"], base + FX_CHAIN)
    scalars = {FX_DIAL: "fxNativeCellUni", FX_TOGGLE: "fxNativeCellSwitch", manifest["kind_last"]: "fxNativeCellBi"}
    for layout, slot, kind in manifest["widgets"]:
        if not 1 <= layout <= len(layouts) or not 1 <= kind < reserves:
            raise ValueError(f"unknown FX layout or cell kind: {layout}, {kind}")
        top, rows = layouts[layout - 1]
        row, col = divmod(slot, 4)
        if not 0 <= slot < min(FX_CELL_COUNT, rows * 4):
            raise ValueError(f"FX cell {slot} is outside layout {layout}")
        row_h = (620 - top) // rows
        rect = (8 + 318 * col, top + 16 + row_h * row, 310, max(72, row_h - 16))
        target = base + FX_VALUE_OFFSET + slot
        focus = True
        if kind in scalars:
            # The MPC resolves the same direct parameter handles for dials and
            # choices. A repeated wrapper with locally incremented handles left
            # only the choices visible on the XL.
            widget = scalars[kind]
        else:
            domain = kind - FX_FIRST_DOMAIN
            target = base + FX_DOMAIN_OFFSET + domain
            if len(domains[domain]) == 1:
                widget, focus = "fxNativeCellReadOnly", False
            elif domain in FX_NUMBER_DOMAINS:
                widget = "fxNativeCellNumber"
            else:
                widget = "fxNativeCellChoice"
        gates = (
            f"IndexedEnabling/{layout}/{len(layouts) + 1}/Parameter {base + FX_LAYOUT_OFFSET}",
            f"IndexedEnabling/{kind}/{reserves}/Parameter {base + FX_KIND_OFFSET + slot}",
        )
        items.append(_native_entry(f"FX Cell {layout}.{slot + 1}.{kind}", widget,
                                   {"version": 1, "handleName": "Data"}, rect, target, gates, focus))


    # Both surfaces place the same 1280 x 628 editor, with its own chain,
    # header and cell bindings. The Mixer's PEEK state gates the whole tree.
    kind = "fxNativeEditorSurface"
    definition = _native_definition(list(items), read_only=True)
    definition["backgroundData"] = _empty_bg()
    ctx.add_component_definition(kind, definition)
    data = {"version": 1, "handleName": "Data"}
    rect = (0, 0, 1280, 628)
    surface = _native_entry("FX Native Surface", kind, data, rect, base + FX_CHAIN)
    surface["bounds"]["acceptsHWFocus"] = "Children"
    items[:] = [surface]
    mixer = next((t for t in tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer")), None)
    if mixer is not None:
        # Docked Mixer PEEK FX editor occupies (0, 0, 1280, 564) above the persistent bottom 6-tab bar.
        docked_h = 564
        docked_bottom = docked_h - 8  # 556
        dock_items = []
        for comp in items:
            if comp["componentData"]["name"] == "FX Native Surface":
                pass
        # Build docked copy of surface children fitted cleanly into 0..564 while preserving FX cell identity
        import copy as _copy
        dock_children = _copy.deepcopy(definition["componentsData"])
        dock_cell_rects = {}
        for layout_id, slot, cell_kind in manifest["widgets"]:
            top, rows = layouts[layout_id - 1]
            row, col = divmod(slot, 4)
            dock_top = top - 64 if top >= 344 else top
            row_h = (docked_bottom - dock_top) // rows
            cell_top = dock_top + 16 + row_h * row
            cell_h = min(max(72, row_h - 16), max(56, docked_bottom - cell_top))
            dock_cell_rects[f"FX Cell {layout_id}.{slot + 1}.{cell_kind}"] = (
                8 + 318 * col, cell_top, 310, cell_h
            )
        # Fit gallery rows cleanly in docked 564px space reusing the same fxNativeGalleryCard
        cards = manifest["chain"]["gallery"]
        columns = len(cards) // 2
        if columns > 0:
            for r_idx in (0, 1):
                row_y = 182 if r_idx == 0 else 372
                row_def = _copy.deepcopy(tui["pageData"]["componentDefinitions"]["localComponentDefinitions"])
                row_key = f"fxNativeGalleryRow{r_idx + 1}"
                src_row_def = next((d["value"] for d in row_def if d["key"] == row_key), None)
                if src_row_def is not None:
                    dock_row_key = f"mixerPeekGalleryRow{r_idx + 1}"
                    dock_row_def = _copy.deepcopy(src_row_def)
                    for child in dock_row_def.get("componentsData", []):
                        cx, cy, cw, _ = map(int, child["bounds"]["bounds"].split())
                        child["bounds"]["bounds"] = f"{cx} {cy} {cw} 182"
                    ctx.add_component_definition(dock_row_key, dock_row_def)
                    for c in dock_children:
                        if c["componentData"]["type"] == row_key:
                            cx, _, cw, _ = map(int, c["bounds"]["bounds"].split())
                            c["componentData"]["type"] = dock_row_key
                            c["bounds"]["bounds"] = f"{cx} {row_y} {cw} 182"

        band_dock = manifest["chain"]["band"]
        dock_band_rect = (band_dock[0], band_dock[1] - 64, band_dock[2], band_dock[3])
        for c in dock_children:
            cname = c["componentData"]["name"]
            if cname in dock_cell_rects:
                c["bounds"]["bounds"] = "%d %d %d %d" % dock_cell_rects[cname]
            elif c["componentData"]["type"] == "fxNativeChainBand":
                c["bounds"]["bounds"] = "%d %d %d %d" % dock_band_rect
            elif cname.startswith("FX Native Header "):
                hx, hy, hw, hh = map(int, c["bounds"]["bounds"].split())
                c["bounds"]["bounds"] = f"{hx} {hy} {hw} {docked_h - hy}"

        dock_kind = "mixerPeekFxEditorSurface"
        dock_def = _native_definition(dock_children, read_only=True)
        dock_def["backgroundData"] = _empty_bg()
        ctx.add_component_definition(dock_kind, dock_def)
        view = base + MIXER_PEEK_VIEW
        peek = _native_entry("Mixer Native Peek FX", dock_kind, data, (0, 0, 1280, docked_h), view,
                             (f"IndexedEnabling/2/3/Parameter {view}",))
        peek["bounds"]["acceptsHWFocus"] = "Children"
        mixer["componentDefinition"]["componentsData"].append(peek)


def apply(tui: dict, qlinks: dict, ctx) -> None:
    try:
        tab = ctx.tab("FX")
    except Exception:
        return

    if ctx.meta is not None and ctx.meta.get("variant") == "overlay":
        _apply_native_editor(tui, tab, ctx)
        return

    _ensure_definitions(tui)

    items = tab["componentDefinition"]["componentsData"]
    by_name = {c["componentData"]["name"]: c for c in items}

    # Find row map in Q-Links.json
    row_map = None
    for m in qlinks.get("Screen Mode Q-Links", {}).get("map", []):
        if m.get("Tab") == tab["fnKeyIndex"] + 1 and m.get("SubTab") == 1:
            row_map = m
            break

    links = {"Q-Link %d" % i: -1 for i in range(1, 17)}

    # Place all 16 native controls strictly in the lower aperture rows (y=426, 526)
    for suffix, key, widget_kind, row, col in FX_CONTROLS:
        name = "FX " + suffix
        param_idx = ctx.param_index(key)
        links[_qlink(row, col)] = param_idx
        def_type = SELECTOR_DEF if widget_kind == "selector" else TOGGLE_DEF
        rect_str = "%d %d %d %d" % (COLS[col], ROWS[row], KNOB_W, KNOB_H)

        if name in by_name:
            comp = by_name[name]
        else:
            comp = {
                "version": 2,
                "componentData": {
                    "version": 1,
                    "name": name,
                    "type": def_type,
                    "data": {"version": 1, "handleName": "Data"},
                },
                "handle remapping": {
                    "version": 1,
                    "map": [{"key": "Data", "value": "Parameter %d" % param_idx}],
                },
                "bounds": {
                    "version": 1,
                    "acceptsHWFocus": "No",
                    "showWhenDataModelInvalid": "Show",
                    "whenVisible": "Always",
                    "boundsType": "Absolute",
                    "bounds": rect_str,
                },
            }
            items.append(comp)
            by_name[name] = comp

        comp["componentData"]["type"] = def_type
        comp["componentData"]["data"] = {"version": 1, "handleName": "Data"}
        comp["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter %d" % param_idx}]
        comp["bounds"]["bounds"] = rect_str
        comp["bounds"]["acceptsHWFocus"] = "No"
        comp["bounds"]["showWhenDataModelInvalid"] = "Show"
        comp["bounds"]["whenVisible"] = "Always"
        handles = [
            h for h in comp["bounds"].get("additionalInvalidatingHandles", [])
            if not h.startswith("RangedEnabling/")
        ]
        if handles:
            comp["bounds"]["additionalInvalidatingHandles"] = handles
        elif "additionalInvalidatingHandles" in comp["bounds"]:
            comp["bounds"]["additionalInvalidatingHandles"] = []

    if row_map is not None:
        row_map["Q-Links"] = links

    # Set Q-Link bounds data if present
    if tab.get("qlinkBoundsData"):
        tab["qlinkBoundsData"] = [
            "%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H)
            for b in tab["qlinkBoundsData"]
        ]
