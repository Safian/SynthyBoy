# SynthyBoy - native controls for the Mod matrix and route editor.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
REPO_ROOT = SCRIPTS.parent
for p in (str(REPO_ROOT), str(SCRIPTS)):
    if p not in sys.path:
        sys.path.insert(0, p)
import native_ui_params as handles
from components.style import (
    argb as _style_argb,
    native_color as _style_native_color,
    color as _style_color,
    size as _style_size,
)
from components.native.button.button import action_button
from components.native.mod_row.mod_row import (
    build_editor_cell_definition,
    editor_label as _label,
    build_mod_row_definitions,
    build_mod_row_instance,
)

CHOICE_MENU = "fx editor choice menu"
CONTRACT = Path(__file__).with_name("mod-native-contract.json").resolve()
MODE_LABELS = ("ALL", "BY SOURCE", "BY DEST")
SOURCE_GROUPS = ("ALL SOURCES", "ENVELOPES", "LFOS", "MACROS", "PLAYER", "SOUND")
TARGET_GROUPS = ("ALL MODULES", "OSC A", "OSC B", "OSC C", "SUB / NOISE", "FILTER 1", "FILTER 2",
                 "ENVELOPES", "LFOS", "FX", "MASTER")
NAVIGATION = (("SOURCE", "list"), ("TARGET", "list"), ("MORE", "action"))
ROW = (("AMOUNT", "bar"), ("POLARITY", "toggle"), ("ROUTE", "toggle"))
DETAIL = ROW + (
    ("SCALE", "dial_uni"), ("INVERT", "toggle"), ("AUX CURVE", "dial_bi"),
    ("SHAPE CURVE", "dial_bi"), ("STEREO", "toggle"), ("RISE", "dial_uni"),
    ("FALL", "dial_uni"), ("DELAY", "dial_uni"), ("SYNC", "toggle"),
)


def _entry(name, kind, data, rect, remap=(), gates=(), focus=False):
    bounds = {"version": 2 if gates else 1, "acceptsHWFocus": "Yes" if focus else "No",
              "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute",
              "bounds": "%d %d %d %d" % tuple(rect)}
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]},
            "bounds": bounds}


def _definition(kind, w, h, skins_dir):
    return build_editor_cell_definition(kind, w, h, choice_menu=CHOICE_MENU, skins_dir=skins_dir)


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay" or not any(tab["tabName"] == "Mod" for tab in tui["pageData"]["tabs"]):
        return
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    if (contract["rows"], len(ROW), len(DETAIL)) != (handles.MOD_WINDOW_ROWS, handles.MOD_ROW_FIELDS, handles.MOD_DETAIL_FIELDS):
        raise ValueError("Mod skin geometry and parameter layout disagree")
    items = ctx.tab("Mod")["componentDefinition"]["componentsData"]
    items[:] = [c for c in items if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
    base = handles.native_base_index(ctx.param_count)
    view, row_base, field_base = (base + offset for offset in (handles.MOD_VIEW, handles.MOD_ROWS, handles.MOD_FIELDS))
    nav_base = base + handles.MOD_NAVIGATION
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    definitions = set()
    toggles = {}

    # Register canonical shared ModRow definitions
    mod_row_defs = build_mod_row_definitions(ctx.skins_dir)
    for def_key, def_val in mod_row_defs.items():
        if def_key not in definitions:
            ctx.add_component_definition(def_key, def_val)
            definitions.add(def_key)

    def cell_definition(kind, rect):
        w, h = rect[2:]
        definition = f"modNative_{kind}_{w}x{h}"
        if definition not in definitions:
            ctx.add_component_definition(definition, _definition(kind, w, h, ctx.skins_dir))
            definitions.add(definition)
        return definition

    def add(name, caption, kind, rect, parameter, gates):
        data = f"Parameter {parameter}"
        if kind == "action":
            items.append(action_button(ctx, "Mod Native " + name, rect, "FromGesture/" + data,
                                       text=caption, width=rect[2], gates=gates))
            return
        definition = cell_definition(kind, rect)
        items.append(_entry("Mod Native " + name, definition, {"version": 1, "handleName": "Data"}, rect,
                            [("Data", data), ("Caption", f"WithName[{caption}]/{data}")], gates, True))
        if kind == "toggle":
            toggles[data] = {"POLARITY": ["UNI", "BI"], "ROUTE": ["ON", "OFF"], "INVERT": ["OFF", "ON"], "STEREO": ["OFF", "ON"], "SYNC": ["OFF", "ON"]}.get(caption, ["OFF", "ON"])

    tabs = [f"RangedEnabling/1/3/4/Parameter {nav_base}"]
    for i, caption in enumerate(MODE_LABELS, 1):
        add("Mode " + caption, caption, f"mode{i}", contract["header"][i - 1], nav_base, tabs)
    add("ADD", "+ ADD", "action", contract["header"][3], nav_base + 3, tabs)
    for i, (caption, groups) in enumerate((("Source", SOURCE_GROUPS), ("Target", TARGET_GROUPS)), 1):
        add(caption + " Group", "", "group", contract["header"][i + 3], nav_base + i,
            [f"IndexedEnabling/{i + 1}/4/Parameter {nav_base}"])
        replacement.append({"key": f"Parameter {nav_base + i}", "value": list(groups)})
    replacement.append({"key": f"Parameter {nav_base}", "value": [""] + list(MODE_LABELS)})

    # Seven independent instances for each frame layout (normal and scroll)
    for frame, layout in enumerate(("normal", "scroll"), 1):
        geom = contract[layout]
        fields = list(zip(ROW, geom["first"])) + list(zip(NAVIGATION, geom["navigation"]))
        x0 = min(rect[0] for _, rect in fields)
        y0 = min(rect[1] for _, rect in fields)
        width = max(x + w for _, (x, y, w, h) in fields) - x0
        height = max(y + h for _, (x, y, w, h) in fields) - y0
        row_type = f"modNativeRow{layout.title()}"

        for row in range(contract["rows"]):
            gates = [
                f"IndexedEnabling/{frame}/5/Parameter {view}",
                f"RangedEnabling/1/64/65/Parameter {row_base + row}",
            ]
            amt_param = field_base + row * len(ROW) + 0
            pol_param = field_base + row * len(ROW) + 1
            rt_param = field_base + row * len(ROW) + 2
            src_param = nav_base + handles.MOD_NAV_HEADER_FIELDS + row * len(NAVIGATION) + 0
            dst_param = nav_base + handles.MOD_NAV_HEADER_FIELDS + row * len(NAVIGATION) + 1
            mr_param = nav_base + handles.MOD_NAV_HEADER_FIELDS + row * len(NAVIGATION) + 2

            toggles[f"Parameter {pol_param}"] = ["UNI", "BI"]
            toggles[f"Parameter {rt_param}"] = ["ON", "OFF"]

            entry = build_mod_row_instance(
                f"Mod Native {layout.title()} Row {row + 1}",
                row,
                (x0, y0 + row * geom["rowStep"], width, height),
                row_type=row_type,
                slot_handle=row_base + row,
                amount_handle=amt_param,
                polarity_handle=pol_param,
                route_handle=rt_param,
                source_handle=src_param,
                target_handle=dst_param,
                more_handle=mr_param,
                gates=gates,
                caption=f"{row + 1:02d}",
            )
            items.append(entry)

    detail_gate = f"RangedEnabling/3/4/5/Parameter {view}"
    for field, ((caption, kind), rect) in enumerate(zip(DETAIL, contract["detail"])):
        param = field_base + contract["rows"] * len(ROW) + field
        if caption == "DELAY":
            for frame, time_caption in ((3, "DELAY"), (4, "BEATS")):
                add("Detail " + time_caption, time_caption, kind, rect, param, [f"IndexedEnabling/{frame}/5/Parameter {view}"])
        else:
            add("Detail " + caption, caption, kind, rect, param, [detail_gate])

    det_nav = contract.get("detail_navigation", {})
    det_base = base + handles.MOD_DETAIL_CONTROL
    if "back" in det_nav:
        add("Detail Back", "BACK", "action", det_nav["back"], det_base + 0, [detail_gate])
    if "delete" in det_nav:
        add("Detail Delete", "DELETE", "delete", det_nav["delete"], det_base + 1, [detail_gate])
        items[-1]["handle remapping"]["map"].append({"key": "Confirm", "value": f"Parameter {det_base + 5}"})
    if "source" in det_nav:
        add("Detail Source", "SOURCE", "list", det_nav["source"], det_base + 2, [detail_gate])
    if "target" in det_nav:
        add("Detail Target", "TARGET", "list", det_nav["target"], det_base + 3, [detail_gate])
    if "aux_source" in det_nav:
        add("Detail Aux Source", "SOURCE", "list", det_nav["aux_source"], det_base + 4, [detail_gate])

    replacement.append({"key": f"Parameter {det_base + 1}", "value": ["DELETE", "CONFIRM?"]})
    replacement.append({"key": f"Parameter {det_base + 5}", "value": ["", "CONFIRM?"]})

    # Matrix rows, quick-add board and picker all occupy the same rectangle.
    # Gate their host cells by both the current screen and their own published state.
    hidden_matrix = f"IndexedEnabling/0/5/Parameter {view}"
    board_gates = [hidden_matrix, *tabs, f"IndexedEnabling/1/2/Parameter {base + handles.MOD_BOARD_GATE}"]
    board_chips = (
        ("ENV 1", 1, 160, 116), ("ENV 2", 2, 272, 116), ("ENV 3", 3, 384, 116),
        ("LFO 1", 7, 160, 200), ("LFO 2", 8, 272, 200), ("LFO 3", 9, 384, 200), ("LFO 4", 10, 496, 200),
        ("MACRO 1", 19, 160, 284), ("MACRO 2", 20, 272, 284), ("MACRO 3", 21, 384, 284), ("MACRO 4", 22, 496, 284),
        ("WHEEL", 27, 160, 368), ("VELO", 23, 272, 368), ("AT", 25, 384, 368), ("KEY", 24, 496, 368),
        ("PITCH", 26, 160, 452), ("FIXED", 57, 272, 452),
    )
    for label, src_id, cx, cy in board_chips:
        rect = (cx, cy, *_style_size("screen.ui_screen_mod.chip_art.size"))
        child = action_button(ctx, f"Mod Native Board Chip {label}", rect,
                              f"Parameter {base + handles.MOD_BOARD_SOURCE}", text=label,
                              width=_style_size("screen.ui_screen_mod.chip_art.width"), gates=board_gates)
        child["componentData"]["data"].update(
            buttonId=src_id, numButtonsInGroup=int(ctx.params[ctx.param_index("mod01.source")]["max"]) + 1)
        items.append(child)

    add("Board All Sources", "ALL SOURCES", "action", _style_size("mod_picker.all_sources"),
        base + handles.MOD_BOARD_GROUP, board_gates)
    list_gates = [hidden_matrix, f"IndexedEnabling/0/4/Parameter {nav_base}",
                  f"IndexedEnabling/1/2/Parameter {base + handles.MOD_LIST_GATE}"]
    list_row = _style_size("mod_picker.row")
    for row in range(handles.MOD_LIST_ROWS):
        rect = [list_row[0], list_row[1] + row * _style_size("mod_picker.row_step"), *list_row[2:]]
        add(f"Picker Row {row + 1}", "", "list", rect, base + handles.MOD_LIST_ROW + row,
            [*list_gates, f"IndexedEnabling/1/2/Parameter {base + handles.MOD_LIST_ROW_GATE + row}"])
    for caption, key, field in (("BACK", "back", handles.MOD_LIST_BACK),
                                ("GROUPS", "groups", handles.MOD_LIST_GROUPS),
                                ("PREVIOUS", "prev", handles.MOD_LIST_PREV),
                                ("NEXT", "next", handles.MOD_LIST_NEXT)):
        add("Picker " + caption, caption, "action", _style_size("mod_picker." + key), base + field, list_gates)
    title = _label("Value", _style_size("mod_picker.title"), just="horizontallyCentred verticallyCentred")
    title["componentData"]["name"] = "Mod Native Picker Title"
    title["handle remapping"] = {"version": 1, "map": [{"key": "Data", "value": f"Parameter {base + handles.MOD_LIST_TITLE}"}]}
    title["bounds"]["version"] = 2
    title["bounds"]["additionalInvalidatingHandles"] = list_gates
    items.append(title)

    # Matrix, detail and quick-add share the fixed root canvas. No redundant
    # per-view background Images or generated demo artwork belongs below them.
    replacement.extend({"key": param, "value": choices} for param, choices in toggles.items())

