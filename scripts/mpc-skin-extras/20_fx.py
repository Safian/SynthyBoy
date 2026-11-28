# SynthyBoy - MPC skin extra: native controls throughout standalone FX (tab FX).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The overlay host renders the live editor cells according to the runtime
# layout and per-slot kind. The non-overlay skin still has the order and
# enable grid, bound directly to the original parameters.

import copy
import json
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components import style
from components.native.name_value.name_value import _label_component, build_name_value_definition
from components.native.selector.selector import dropdown_definition, labelled_choice_drawer, write_routing_assets
from components.native.pan.pan import synthyboy_dial_data as pan_dial_data
from components.native.level.level import compact_dial_definition, default_level_template, synthyboy_dial_data
from components.native.active_text.active_text import write_active_text_assets, active_text_child
from components.native.radio_button.radio_button import create_segmented_radio_button_group, render_segmented_button_art
from components.native.band_lane.band_lane import (
    build_band_lane_definition, build_band_lane_instance, fx_card_definition, lane_rect, LANES)
from components.native.button.button import action_button, action_art, in_slot, button as native_button, write_transparent_hit

from native_ui_params import (FX_CELL_COUNT, FX_DOMAIN_COUNT, FX_DOMAIN_OFFSET,
                              FX_BAND_BASE, FX_BAND_MODE, FX_EDITOR_POWER, FX_HEADER, FX_HEADER_FIELDS, FX_HEADER_ITEMS,
                              FX_KIND_OFFSET, FX_LAYOUT_OFFSET, FX_VALUE_OFFSET,
                              FX_CHAIN, FX_BANDS,
                              native_base_index)

CONTRACT = Path(__file__).with_name("fx-native-contract.json")
FX_DIAL, FX_TOGGLE, FX_FIRST_DOMAIN = 1, 2, 3
FX_NUMBER_DOMAINS = {18, 19}  # Chorus Voices and Hyperdimension Unison
FX_CHOICE_MENU = "fx editor choice menu"
FX_NUMBER_MENU = "fx editor number menu"
FX_RACK_MENU = "fx editor rack menu"
FX_ADD_MENU = "fx chain add menu"
FX_POOL_RACK_MENU = "fx editor main bus rack menu"
RACK_NAMES = ("MAIN", "BUS 1", "BUS 2", "L/H LOW", "L/H HIGH", "L/M/H LOW",
              "L/M/H MID", "L/M/H HIGH", "M/S MID", "M/S SIDE")
# Full-screen popup marker coordinates and colours match the overlay detector.
FX_MENU_MARKERS = ((16, "fe356c"), (28, "24c7ff"), (40, "90ed43"))

COLS = style.size("fx.grid.columns")
ROWS = style.size("fx.grid.rows")
KNOB_W, KNOB_H = style.size("fx.grid.cell_size")
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = style.size("fx.grid.qlink_bounds")

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
    nocolour = {"version": 1, "colour": style.native_color("transparent"), "image": ""}
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
                "backgroundColour": style.argb("mod_detail.title", 25),
                "outlineColour": style.argb("focus.cyan"),
                "backgroundInset": style.size("fx.focus_child.inset"),
                "outlineThickness": style.size("fx.focus_child.outline"),
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
                    "bounds": _bounds((style.size("fx.build_selector_definition.bounds.6"), style.size("fx.build_selector_definition.bounds.7"), KNOB_W - style.size("fx.build_selector_definition.bounds.12"), KNOB_H - style.size("fx.build_selector_definition.bounds.14"))),
                },
            ],
        }
    for child in defn.get("componentsData", []):
        cname = child.get("componentData", {}).get("name")
        if cname == "focus":
            child["bounds"]["bounds"] = "0 0 %d %d" % (KNOB_W, KNOB_H)
        elif cname == "slider":
            x, y, right, bottom = style.size("fx.legacy.slider.insets")
            child["bounds"]["bounds"] = f"{x} {y} {KNOB_W - right} {KNOB_H - bottom}"
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
                        "name": style.size("font.family"),
                        "style": style.size("font.style.semibold"),
                        "height": style.size("fx.build_toggle_definition.height.16_0"),
                    },
                    "colour": style.argb("fx.legacy.6a6a6c"),
                    "justification": "horizontallyCentred verticallyCentred",
                    "case": "Upper Case",
                },
                "type": "Name",
                "handleName": "Data",
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": _bounds((style.size("fx.build_toggle_definition.bounds.4"), style.size("fx.build_toggle_definition.bounds.2"), KNOB_W - style.size("fx.build_toggle_definition.bounds.8"), style.size("fx.build_toggle_definition.bounds.22"))),
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
            "bounds": _bounds(style.size("fx.build_toggle_definition.rect.1")),
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
    child = _label_component(kind.lower(), kind, "Data", rect, height,
                             style.argb("text.icon"), "horizontallyCentred verticallyCentred")
    return _native_entry(kind.lower(), "Label", child["componentData"]["data"], rect)


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
    rects = {"focus": (0, 0, style.size("fx.native_dial.width"), style.size("fx.native_dial.height")), "knob": (style.size("fx.native_dial.knob_x"), style.size("fx.native_dial.knob_y"), style.size("fx.native_dial.knob_size"), style.size("fx.native_dial.knob_size")),
             "name": (style.size("fx.native_dial.label_x"), 0, style.size("fx.native_dial.label_width"), style.size("fx.native_dial.caption_height")), "value": (style.size("fx.native_dial.label_x"), style.size("fx.native_dial.caption_height"), style.size("fx.native_dial.label_width"), style.size("fx.native_dial.value_height"))}
    definition = compact_dial_definition(default_level_template(), rects=rects)
    dial_data = pan_dial_data if bipolar else synthyboy_dial_data
    data = dial_data(ctx.skins_dir, frame_size=style.size("fx.native_dial.knob_size"))
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
    choices = [_native_label("Name", (style.size("fx.native_editor_definitions.choice_padding"), 0, style.size("fx.native_editor_definitions.choice_width"), style.size("fx.native_editor_definitions.caption_height")), style.size("fx.native_editor_definitions.caption_font")),
               _native_label("Value", (style.size("fx.native_editor_definitions.choice_padding"), style.size("fx.native_editor_definitions.caption_height"), style.size("fx.native_editor_definitions.choice_width"), style.size("fx.native_editor_definitions.value_height")), style.size("fx.native_editor_definitions.value_font"))]
    on, off = write_active_text_assets(ctx.skins_dir, "ON", width=style.size("fx.native_editor_definitions.toggle_width"), height=style.size("fx.native_editor_definitions.toggle_height"),
                                      on_name="fx_active_on.png", off_name="fx_active_off.png")
    choice = dropdown_definition(
        style.size("fx.native_editor_definitions.choice_width"), style.size("fx.native_editor_definitions.value_height"),
        actions=_native_definition([], popup=FX_CHOICE_MENU)["actions"], skins_dir=ctx.skins_dir)
    for child in choice["componentsData"]:
        x, y, w, h = map(int, child["bounds"]["bounds"].split())
        child["bounds"]["bounds"] = f"{x + style.size('fx.native_editor_definitions.choice_padding')} {y + style.size('fx.native_editor_definitions.caption_height')} {w} {h}"
    choice["componentsData"].append(choices[0])
    number = build_name_value_definition(
        rect=(0, 0, style.size("fx.native_dial.width"), style.size("fx.native_dial.height")),
        box_width=style.size("fx.native_editor_definitions.number_width"), box_height=style.size("height.medium"),
        skins_dir=ctx.skins_dir, caption_handle="Data", popup=FX_NUMBER_MENU)
    return {
        "fxNativeCellUni": _native_dial(ctx),
        "fxNativeCellBi": _native_dial(ctx, bipolar=True),
        "fxNativeCellSwitch": _native_definition([
            active_text_child("active", on, off, (style.size("fx.native_editor_definitions.toggle_x"), style.size("fx.native_editor_definitions.choice_padding"), style.size("fx.native_editor_definitions.toggle_width"), style.size("fx.native_editor_definitions.toggle_height")), handle="Data"),
            _native_label("Name", (style.size("fx.native_editor_definitions.switch_label_x"), style.size("fx.native_editor_definitions.label_y"), style.size("fx.native_editor_definitions.switch_label_width"), style.size("fx.native_editor_definitions.label_height")), style.size("fx.native_editor_definitions.label_font")),
        ]),
        "fxNativeCellChoice": choice,
        "fxNativeCellNumber": number,
        "fxNativeCellReadOnly": _native_definition(copy.deepcopy(choices), read_only=True),
    }


def _add_drawer(gallery):
    names = [name for name, _ in gallery]
    # The Menu handle reserves zero for cancellation. The 16 effect kinds use 1..16.
    # Keep the full native row height by laying out the gallery as two columns.
    per_column = (len(names) + 1) // 2
    drawer = labelled_choice_drawer(FX_ADD_MENU, names[:per_column])
    children = drawer["componentsData"]
    for column, offset in enumerate(range(0, len(names), per_column)):
        source = drawer if column == 0 else labelled_choice_drawer(FX_ADD_MENU, names[offset:offset+per_column])
        rows = [child for child in source["componentsData"] if child["componentData"]["type"] in ("Label","Button")]
        for child in rows:
            model = child["componentData"]
            if model["type"] == "Button":
                local = model["data"]["buttonId"]
                model["data"].update(buttonId=offset + local + 1, numButtonsInGroup=len(names)+1)
            if column:
                x, y, w, h = map(int, child["bounds"]["bounds"].split())
                width = style.size("selector.source.drawer_row")[2]
                child["bounds"]["bounds"] = f"{x + column * width} {y} {w} {h}"
                children.append(child)
    return drawer


def _add_native_popups(tui, ctx, gallery):
    from PIL import Image

    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    stock = next(d["value"] for d in defs if d["key"] == "menu overlay")
    markers = []
    for i, (x, rgb) in enumerate(FX_MENU_MARKERS, 1):
        filename = f"fx_editor_modal_marker_{i}.png"
        Image.new("RGB", (style.size("fx.add_native_popups.new.6"), style.size("fx.add_native_popups.new.6")), "#" + rgb).save(ctx.skins_dir / filename)
        marker = _native_entry(f"FX Native Popup Marker {i}", "Image",
                               {"version": 2, "imageType": "Regular", "colour": "0", "image": filename},
                               (x, style.size("fx.add_native_popups.native_entry.120"), style.size("fx.add_native_popups.native_entry.6"), style.size("fx.add_native_popups.native_entry.6")))
        marker["bounds"]["showWhenDataModelInvalid"] = "Show"
        markers.append(marker)
    write_routing_assets(ctx.skins_dir)
    for name in (FX_CHOICE_MENU, FX_NUMBER_MENU, FX_RACK_MENU, FX_POOL_RACK_MENU, FX_ADD_MENU):
        if name in (FX_RACK_MENU, FX_POOL_RACK_MENU, FX_ADD_MENU):
            rack = name != FX_ADD_MENU
            if rack:
                popup = labelled_choice_drawer(name, RACK_NAMES if name == FX_RACK_MENU else RACK_NAMES[:3])
            else:
                popup = _add_drawer(gallery)
            control = "Rack" if rack else "Add"
            filename = f"fx_{control.lower()}_popup_marker.png"
            Image.new("RGB", (style.size("fx.add_native_popups.new.6"), style.size("fx.add_native_popups.new.6")), "#f1ae47" if rack else "#ef72bf").save(ctx.skins_dir / filename)
            action = _native_entry(f"FX {control} Popup Marker", "Image",
                                   {"version": 2, "imageType": "Regular", "colour": "0", "image": filename},
                                   (style.size("fx.add_native_popups.native_entry.52") if rack else style.size("fx.add_native_popups.native_entry.64"), style.size("fx.add_native_popups.native_entry.120"), style.size("fx.add_native_popups.native_entry.6"), style.size("fx.add_native_popups.native_entry.6")))
            action["bounds"]["showWhenDataModelInvalid"] = "Show"
            popup["componentsData"].append(action)
        else:
            popup = copy.deepcopy(stock)
            if name == FX_NUMBER_MENU:
                overlay = next(c["componentData"] for c in popup["componentsData"] if c["componentData"]["type"] == "MenuOverlay")
                overlay["type"] = "NumericOverlay"
                overlay["data"].pop("withNumbers")
            popup["componentsData"].extend(copy.deepcopy(markers))
        ctx.add_component_definition(name, popup)


def _add_native_chain(tui, tab, ctx, contract, base):
    view, count, select = range(base, base + 3)
    slots = len(contract["cards"])
    power = select + slots
    gallery = power + slots
    menu = gallery + len(contract["gallery"])
    choose, back, page = menu + 1, menu + 2, menu + 3

    def art(name, size, glyph="", active=False, inactive=False):
        from PIL import Image
        filename = f"fx_chain_{name}.png"
        label = "⏻" if glyph == "power" else "" if glyph == "box" else glyph
        image = (in_slot(action_art(icon="power", colour=style.color("text.status.off" if inactive else "text.status.on")), size)
                 if glyph == "power" else render_segmented_button_art(label, active and not inactive, *size)
                 if glyph else Image.new("RGBA", size))
        image.save(ctx.skins_dir / filename)
        return filename

    def button(name, param, bounds, gates, off, on=None, index=1, domain=1):
        remaps = () if param is None else (("Data", f"Parameter {param}"),)
        return native_button(name, bounds, on or off, off, index=index, count=domain,
                             remaps=remaps, gates=gates)


    def labelled_button(name, kind, size, button_art):
        w, h = size
        label = _native_label(kind, (style.size("fx.labelled_button.native_label.2"), h - style.size("fx.labelled_button.native_label.38"), w - style.size("fx.labelled_button.native_label.4"), style.size("fx.labelled_button.native_label.30")), style.size("fx.labelled_button.native_label.15"))
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
    ctx.add_component_definition("fxNativeChainCard", fx_card_definition(
        ctx.skins_dir, "fx_chain", size, size, down, style.size("fx.chain.card.labels")))
    tiles = []
    for slot, (power_box, _) in enumerate(contract["cards"]):
        tile = _native_entry(f"Slot {slot + 1}", "fxNativeChainCard",
                             {"version": 1, "handleName": "Present"},
                             (power_box[0], power_box[1], w, down + h), gates=("Present",))
        tile["bounds"]["acceptsHWFocus"] = "Children"
        tile["handle remapping"]["map"] = [
            {"key": "Present", "value": f"RangedEnabling/{slot + 1}/{slots}/{slots + 1}/Parameter {count}"},
            {"key": "Data", "value": f"Parameter {select + slot}"},
            {"key": "Select", "value": f"Parameter {select + slot}"},
            {"key": "Selected", "value": f"IndexedEnabling/1/2/Parameter {select + slot}"},
            {"key": "Unselected", "value": f"IndexedEnabling/0/2/Parameter {select + slot}"},
            {"key": "Power", "value": f"Parameter {power + slot}"},
            {"key": "Powered", "value": f"IndexedEnabling/1/2/Parameter {power + slot}"},
            {"key": "Bypassed", "value": f"IndexedEnabling/0/2/Parameter {power + slot}"},
        ]
        tiles.append(tile)
    ctx.add_component_definition("fxNativeChainRow", transparent(tiles, True))
    row = _native_entry("FX fxNativeChainRow", "fxNativeChainRow", {"version": 1, "handleName": "Data"},
                        (0, 0, contract["cards"][-1][0][0] + w, select_rect[1] + h), view)
    row["bounds"]["acceptsHWFocus"] = "Children"
    items.append(row)

    add = contract["add"]
    caption = _native_label("Text", (0, 0, add[2], add[3]), style.size("fx.add_native_chain.native_label.16"))
    caption["componentData"]["data"]["text"] = "+ ADD"
    caption["bounds"]["showWhenDataModelInvalid"] = "Show"
    definition = _native_definition([caption], popup=FX_ADD_MENU)
    definition["disableCoarseDataWheel"] = True
    definition["backgroundData"]["unfocussed"] = _empty_bg()["unfocussed"]   # the "+ ADD" caption lies on the page background: no darker surface.control rectangle (68x56) behind it; the focus colour stays
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
    move_comp = _native_entry("FX Chain Move", "Image",
                              {"version": 2, "imageType": "Regular", "colour": "0",
                               "image": art("move", drag_rect[2:], "MOVE")}, drag_rect,
                              gates=(f"RangedEnabling/1/{slots}/{slots + 1}/Parameter {count}",))
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


def _add_native_bands(ctx, items, base):
    """The band lanes of a splitter page: one `fxNativeBandLane` definition, one entry per lane (components/native/band_lane)."""
    for name, definition in build_band_lane_definition(ctx.skins_dir).items():
        ctx.add_component_definition(name, definition)
    for lane in range(LANES):
        items.append(build_band_lane_instance(f"FX Band Lane {lane + 1}", lane_rect(lane), base, lane))


def _add_native_header(tui, tab, ctx, headers, band, base, panels, gallery):
    single, multi = headers
    y, height = single["power"][1], single["power"][3]
    layout_parameter = f"Parameter {base + FX_HEADER + 1}"
    frame_count = 7 + 2 * len(panels)
    handles = ("Power", "Rack", "Page", "Remove")
    children = []
    power_images = []
    for name, colour in (("on", "text.status.on"), ("off", "text.status.off")):
        filename = f"fx_header_power_{name}.png"
        in_slot(action_art(icon="power", colour=style.color(colour)), (style.size("fx.add_native_header.in_slot.56"), style.size("fx.add_native_header.in_slot.56"))).save(ctx.skins_dir / filename)
        power_images.append(filename)
    children.append(native_button("power", (single["power"][0], 0, style.size("fx.add_native_header.native_button.56"), style.size("fx.add_native_header.native_button.56")), *power_images,
                                  remaps=(("Data", "Power"),), gates=(f"NonZero/{layout_parameter}",)))
    rack_definition = _native_definition([
        _native_label("Name", style.size("fx.add_native_header.rect.1"), style.size("fx.add_native_header.native_label.16")),
        _native_label("Value", style.size("fx.add_native_header.rect.2"), style.size("fx.add_native_header.native_label.20"))], popup=FX_RACK_MENU)
    rack_definition["componentsData"][0]["componentData"]["data"].update(type="Text", text="RACK", handleName="")
    rack_definition["disableCoarseDataWheel"] = True
    ctx.add_component_definition("fxNativeHeaderRack", rack_definition)
    pool_rack = copy.deepcopy(rack_definition)
    for action in pool_rack["actions"]:
        action["additionalData"] = FX_POOL_RACK_MENU
    ctx.add_component_definition("fxNativeHeaderPoolRack", pool_rack)
    x, yy, w, h = single["rack"]
    # Choose the drawer while the native header is visible. The opened drawer holds its
    # own action parameter and domain, even if the host subsequently hides the tab.
    selected = f"Parameter {base + FX_HEADER}"
    for name, kind, first, last in (("rack", "fxNativeHeaderRack", 1, 13),
                                     ("rack pool", "fxNativeHeaderPoolRack", 14, FX_HEADER_ITEMS)):
        rack = _native_entry(name, kind, {"version": 1, "handleName": "Data"}, (x, yy-y, w, h),
                             gates=("IndexedEnabling/1/2/Power", f"RangedEnabling/{first}/{last}/{FX_HEADER_ITEMS+1}/{selected}"),
                             focus=True)
        rack["handle remapping"]["map"] = [{"key": "Data", "value": "Rack"}]
        children.append(rack)
    x, yy, w, h = single["remove"]
    children.append(action_button(ctx, "remove", (x, yy-y, w, h), "Remove", text="REMOVE", gates=("NonZero/Power",)))

    # All effects share the title and rack-path geometry. Only Convolve's
    # missing impulse can publish a notice (FxUi::pullEditor).
    convolve = next(panel for panel in panels if gallery[panel["kind"] - 1][0] == "Convolve")
    notice_frame = 7 + 2 * (convolve["kind"] - 1)
    for field, name, handle, colour, panel, gate in (
        (0, "path", "Rack", "text.label", panels[0], f"NonZero/{layout_parameter}"),
        (1, "title", "Power", "text.white", panels[0], f"NonZero/{layout_parameter}"),
        (2, "notice", "Remove", "text.status.off", convolve,
         f"RangedEnabling/{notice_frame}/{notice_frame+1}/{frame_count}/{layout_parameter}"),
    ):
        x, yy, w, h = panel["text"][field]
        label = _native_label("Name", (x, yy-y, w, h), panel["text_font"][field])
        label["componentData"]["name"] = f"fx_header_{name}"
        label["handle remapping"]["map"] = [{"key": "Data", "value": handle}]
        label["componentData"]["data"]["textStyle"].update(
            justification=("right" if name == "notice" else "left") + " verticallyCentred",
            colour=style.argb(colour))
        label["bounds"].update(version=2, additionalInvalidatingHandles=[gate])
        children.append(label)
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
        entry = _native_entry(f"FX Native Header {item + 1}", layer, {"version": 1, "handleName": "Data"}, (0, y, style.size("fx.add_native_header.native_entry.1280"), style.size("fx.add_native_header.native_entry.628")-y),
                             gates=(f"IndexedEnabling/{item+1}/{FX_HEADER_ITEMS+1}/Parameter {base+FX_HEADER}",))
        entry["bounds"]["acceptsHWFocus"] = "Children"
        entry["handle remapping"]["map"] = [{"key": key, "value": f"Parameter {parameter+field}"}
                                              for field, key in enumerate(handles)]
        if item == compressor or item >= len(legacy_modules):
            entry["handle remapping"]["map"].append({"key": "Band", "value": f"Parameter {base+FX_BAND_BASE+item}"})
        tab["componentDefinition"]["componentsData"].append(entry)
        replacements.append({"key": f"Parameter {parameter+1}", "value": list(RACK_NAMES if item < 13 else RACK_NAMES[:3])})


# DISPLAY LEFT, CONTROLS RIGHT (shell-mpc/native_fx_cells.h kFxSide*): two cells to a line at x 644, 72 px a line, a 16 px Q lane above every row.
FX_SIDE_X, FX_SIDE_STRIDE, FX_SIDE_LANE, FX_SIDE_LINE, FX_SIDE_CELL_W = 644, 318, 16, 72, 310


def _side_cell_rect(top, side, row, col):
    lines = side[1:5]
    y = top + sum(FX_SIDE_LANE + FX_SIDE_LINE * lines[r] for r in range(row)) + FX_SIDE_LANE
    return (FX_SIDE_X + FX_SIDE_STRIDE * (col % 2), y + FX_SIDE_LINE * (col // 2), FX_SIDE_CELL_W, FX_SIDE_LINE)


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
    _add_native_popups(tui, ctx, manifest["chain"]["gallery"])

    base = native_base_index(ctx.param_count)
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    for i, choices in enumerate(domains):
        replacements.append({"key": f"Parameter {base + FX_DOMAIN_OFFSET + i}", "value": choices})

    items = tab["componentDefinition"]["componentsData"]
    items.clear()
    _add_native_header(tui, tab, ctx, manifest["editor_headers"], manifest["chain"]["band"], base, manifest["panels"], manifest["chain"]["gallery"])
    _add_native_chain(tui, tab, ctx, manifest["chain"], base + FX_CHAIN)
    _add_native_bands(ctx, items, base + FX_BANDS)
    cells_start = len(items)
    scalars = {FX_DIAL: "fxNativeCellUni", FX_TOGGLE: "fxNativeCellSwitch", manifest["kind_last"]: "fxNativeCellBi"}
    for layout, slot, kind in manifest["widgets"]:
        if not 1 <= layout <= len(layouts) or not 1 <= kind < reserves:
            raise ValueError(f"unknown FX layout or cell kind: {layout}, {kind}")
        top, rows, *side = layouts[layout - 1]
        row, col = divmod(slot, 4)
        if not 0 <= slot < min(FX_CELL_COUNT, rows * 4):
            raise ValueError(f"FX cell {slot} is outside layout {layout}")
        row_h = (style.size("fx.apply_native_editor.row_h.620") - top) // rows
        # a stacked cell stands at the top of its row, at most cell_max_h high (shell-mpc/native_fx_cells.h fxStackedCellH)
        cell_h = min(max(style.size("fx.apply_native_editor.rect.72"), row_h - style.size("fx.apply_native_editor.rect.16")), manifest["cell_max_h"])
        rect = _side_cell_rect(top, side, row, col) if side and side[0] else (style.size("fx.apply_native_editor.rect.8") + style.size("fx.apply_native_editor.rect.318") * col, top + style.size("fx.apply_native_editor.rect.16") + row_h * row, style.size("fx.apply_native_editor.rect.310"), cell_h)
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


    # Keep the original native cells visible while bypassed. Header and chain
    # navigation remain independently operable; the shared family owns OFF.
    def cell_layer(children):
        rects = [tuple(map(int, c["bounds"]["bounds"].split())) for c in children]
        left = min(x for x, y, w, h in rects)
        top = min(y for x, y, w, h in rects)
        right = max(x + w for x, y, w, h in rects)
        bottom = max(y + h for x, y, w, h in rects)
        for child, (x, y, w, h) in zip(children, rects):
            child["bounds"]["bounds"] = f"{x-left} {y-top} {w} {h}"
        definition = _native_definition(children, read_only=True)
        definition["backgroundData"] = _empty_bg()
        return definition, (left, top, right-left, bottom-top)

    cells_kind = "fxNativeParameterCells"
    cells_definition, cells_rect = cell_layer(list(items[cells_start:]))
    ctx.add_component_definition(cells_kind, cells_definition)
    cells_entry = _native_entry("FX parameter cells", cells_kind,
                                {"version": 1, "handleName": "Data"}, cells_rect, base + FX_EDITOR_POWER)
    definitions = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    style.bind_control_active(cells_entry, f"Parameter {base + FX_EDITOR_POWER}", ctx, definitions)
    cells_entry["bounds"]["acceptsHWFocus"] = "Children"
    items[cells_start:] = [cells_entry]


    # Both surfaces place the same 1280 x 628 editor, with its own chain,
    # header and cell bindings. The Mixer's PEEK state gates the whole tree.
    kind = "fxNativeEditorSurface"
    definition = _native_definition(list(items), read_only=True)
    definition["backgroundData"] = _empty_bg()
    ctx.add_component_definition(kind, definition)
    data = {"version": 1, "handleName": "Data"}
    rect = style.size("fx.apply_native_editor.rect.1")
    surface = _native_entry("FX Native Surface", kind, data, rect, base + FX_CHAIN)
    surface["bounds"]["acceptsHWFocus"] = "Children"
    items[:] = [surface]


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
