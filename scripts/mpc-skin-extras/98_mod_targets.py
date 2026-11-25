import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style

# SynthyBoy - shared native controls of the MPC modulation route editor.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
from pathlib import Path


import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.button.button import TRANSPARENT_HIT, button as native_button, write_transparent_hit
from components.native.selector.selector import OSC_TAB_NAMES, passive_layer
from components.native.mod_row.mod_row import build_cell_definition, write_route_button_art, write_route_pagination_art
from native_ui_params import (MOD_TARGETS, MOD_TARGETS_PAGE_GATE, MOD_TARGETS_ROW_FIELDS,
                              MOD_TARGETS_ROW_GATE, MOD_TARGETS_ROWS, MOD_TARGETS_VIEW,
                              mixer_peek_index)

CONTRACT = Path(__file__).with_name("mod-targets-native-contract.json")


def _entry(name, kind, rect, data, remap=(), gate=None, focus="No"):
    bounds = {"version": 2 if gate else 1, "acceptsHWFocus": focus,
              "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Absolute", "bounds": " ".join(map(str, rect))}
    if gate:
        bounds["additionalInvalidatingHandles"] = [gate]
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


def _custom(name, kind, rect, remap, gate):
    return _entry(name, kind, rect, {"version": 1, "handleName": "Data"}, remap, gate, "Children")


def _label(name, rect, kind, handle, gate=None, align="horizontallyCentred"):
    data = {"version": 1, "type": kind, "handleName": handle, "textStyle": {
        "version": 1, "font": {"version": 1, "name": _ui_style.size("font.family"), "style": _ui_style.size("font.style.semibold"), "height": _ui_style.size('screen.mod_targets.height')},
        "colour": _ui_style.argb('text.icon'), "justification": f"{align} verticallyCentred", "case": "Original"}}
    return _entry(name, "Label", rect, data, gate=gate)


def _button(name, rect, images, handle, gate=None, index=1, count=1):
    return native_button(name, rect, *images, index=index, count=count,
                         remaps=(("Data", handle),), gates=(gate,) if gate else ())


def _rows(ctx, geometry):
    """One reusable row owns its dynamic route name and four independent controls."""
    y = geometry[0][1]
    name, amount, polarity, active, remove = [(x, top - y, w, h) for x, top, w, h in geometry]
    w, h = amount[2:]
    kind = f"modNative_bar_{w}x{h}"
    ctx.add_component_definition(kind, build_cell_definition("bar", w, h, skins_dir=ctx.skins_dir))
    items = [_label("Target", name, "Name", "Amount", "IndexedEnabling/2/3/Present", "leftJustified"),
             _custom("Amount", kind, amount, (("Data", "Amount"),), None)]
    for key, rect, captions in (("Polarity", polarity, ("BI", "UNI")), ("Active", active, ("ON", "OFF")),
                                ("Remove", remove, ("×", "×"))):
        on = write_route_button_art(ctx.skins_dir, key.lower() + "_on", captions[0], rect, key != "Remove")
        off = on if key == "Remove" else write_route_button_art(ctx.skins_dir, key.lower() + "_off", captions[1], rect, False)
        items.append(_button(key, rect, (on, off), "FromGesture/" + key))
    return items


def _pagination(ctx, rect, parameter, availability):
    x, y, w, h = rect
    half = w // 2
    image = write_route_pagination_art(ctx.skins_dir, w, h)
    gate = f"NonZero/{availability}"
    art = {"version": 2, "imageType": "FilmStrip", "image": image, "colour": "0", "numFrames": 4,
           "handleName": availability}
    hit = (TRANSPARENT_HIT,) * 2
    return [_entry("Page arrows", "Image", rect, art, gate=gate),
            _label("Page", (x + 40, y, w - 80, h), "Value", parameter, gate),
            _button("Previous", (x, y, half, h), hit, f"WithName[Previous Page]/FromGesture/{parameter}",
                    f"RangedEnabling/1/2/4/{availability}", 1, 4),
            _button("Next", (x + half, y, w - half, h), hit, f"WithName[Next Page]/FromGesture/{parameter}",
                    f"RangedEnabling/2/3/4/{availability}", 2, 4)]


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    base = mixer_peek_index(ctx.param_count)
    view = f"Parameter {base + MOD_TARGETS_VIEW}"
    row_type = "nativeModulationRoute"
    ctx.add_component_definition(row_type, passive_layer(_rows(ctx, contract["row"])))
    write_transparent_hit(ctx.skins_dir)
    back = contract["back"]
    art = write_route_button_art(ctx.skins_dir, "back", "BACK", back, False)
    add = f"Parameter {base + MOD_TARGETS + 1}"
    items = [_button("BACK", back, (art, art), f"FromGesture/Parameter {base + MOD_TARGETS}"),
             _entry("Add", "oscNativeSetting_action", contract["add"], {"version": 1, "handleName": "Data"},
                    (("Data", "FromGesture/" + add), ("Caption", add)), focus="Yes")]
    items.extend(_pagination(ctx, contract["page"], f"Parameter {base + MOD_TARGETS + 2}",
                             f"Parameter {base + MOD_TARGETS_PAGE_GATE}"))
    _, row_top, _, row_height = contract["row"][0]
    for row in range(MOD_TARGETS_ROWS):
        start = base + MOD_TARGETS + 3 + row * MOD_TARGETS_ROW_FIELDS
        present = f"Parameter {base + MOD_TARGETS_ROW_GATE + row}"
        remap = [("Data", present), ("Present", present)]
        remap.extend((name, f"Parameter {start + j}") for j, name in enumerate(("Amount", "Polarity", "Active", "Remove")))
        rect = (0, row_top + row * contract["stride"], 1280, row_height)
        items.append(_custom(f"Route {row + 1}", row_type, rect, remap, f"NonZero/{present}"))

    kind = "nativeModulationEditor"
    ctx.add_component_definition(kind, passive_layer(items))
    pages = {"Macros": 1, "Env": 2, "LFO": 3, "Filter": 4, "Mod": 5, "Play": 6,
             **dict.fromkeys((*OSC_TAB_NAMES, "Osc"), 7), **dict.fromkeys(("Mix", "Mixer"), 8)}
    for tab in tui["pageData"]["tabs"]:
        if (frame := pages.get(tab["tabName"])) is not None:
            entries = tab["componentDefinition"]["componentsData"]
            entries.insert(0, _custom("Native Modulation Editor", kind, tuple(_ui_style.size('screen.mod_targets.apply.rect')),
                                      (("Data", view),), f"IndexedEnabling/{frame}/9/{view}"))
