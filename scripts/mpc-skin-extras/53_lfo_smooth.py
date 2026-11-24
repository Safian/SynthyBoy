# SynthyBoy - native standalone LFO and RND controls.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
from pathlib import Path

from native_ui_params import LFO_OFFSET, mixer_peek_index

CONTRACT = Path(__file__).with_name("lfo-native-contract.json")
PREFIX = "LFO 1 Native "
DEFINITION = "lfoNative"
SELECT, SHAPE, STYLE, SMOOTH, SMOOTH_TIME, ACTION, LAYOUT = range(7)
CHOICE_FIELDS = {
    SHAPE: "lfo1.shape", STYLE: "rlfo1.style", 8: "lfo1.division", 10: "lfo1.sync",
    11: "lfo1.sync_type", 12: "rlfo1.sync_type", 16: "lfo1.direction",
}
TOGGLE_FIELDS = {SMOOTH, 15, 19, 20}
NUMBER_FIELDS = {21, 22}
LAYOUT_STEPS = 7


def _entry(name, kind, data, rect, handles=(), gates=(), focus=False, proportional=False):
    bounds = {"version": 2 if gates else 1, "acceptsHWFocus": "Yes" if focus else "No",
              "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Proportional" if proportional else "Absolute",
              "bounds": " ".join(str(x) for x in rect)}
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": key, "value": value} for key, value in handles]},
            "bounds": bounds}


def _label(kind, rect, size=14, colour="ffe6e6e6", text=None, proportional=False):
    data = {"version": 1, "textStyle": {"version": 1,
            "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(size)},
            "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": "Original"},
            "type": kind, "handleName": "Data"}
    if text is not None:
        data["text"] = text
    return _entry(kind.lower(), "Label", data, rect, proportional=proportional)


def _button(rect, index, count, art):
    data = {"version": 2, "onImage": art[0], "offImage": art[1], "buttonId": index,
            "numButtonsInGroup": count, "handleName": "Data", "gestureBehaviour": "Instant"}
    return _entry("button", "Button", data, rect, proportional=True)


def _definition(children, popup=None, inactive=False):
    background = {"version": 1, "colour": "ff171d32", "image": ""}
    actions = [{"version": 1, "onAction": trigger, "handler": "Show Overlay",
                "handleName": "Data", "additionalData": popup}
               for trigger in ("Mouse Down", "Enter Pressed")] if popup is not None else []
    return {"version": 2, "actions": actions,
            "backgroundData": {"version": 1, "focussed": dict(background, colour="ff2a3651"), "unfocussed": background},
            "ignoreMousePresses": inactive, "disableCoarseDataWheel": inactive, "componentsData": children}


# Every definition below is built from the current shared native primitives.
# Geometry comes from the C++ skin contract, never from proportional dial scaling.
FIELD_CAPTIONS = {
    0: "SOURCE", 1: "WAVEFORM", 2: "STYLE", 3: "SMOOTH", 4: "TIME",
    7: "RATE", 8: "DIVISION", 9: "RISE", 10: "SYNC", 11: "MODE", 12: "MODE",
    13: "PHASE", 14: "DELAY", 15: "HOST", 16: "DIRECTION", 17: "SWING",
    18: "STEREO", 19: "MONO", 20: "STEREO", 21: "TRANSPOSE", 22: "TUNE",
}


def _dial(polarity, ctx, width=162, height=168, *, active=True, smooth=False):
    from components.native.level.level import (
        compact_dial_definition, level_definition,
        build_native_smooth_dial_with_indicator_definition, synthyboy_dial_data,
    )
    from components.native.pan.pan import synthyboy_dial_data as pan_data

    # Current compact LEVEL variants: side-by-side in wide cells, stacked in
    # narrow cells. The physical knob always remains a square >= 56 px.
    side = width >= 190 and height <= 120
    size = 60 if side or height < 150 else 90
    if smooth:
        rects = {"focus": (0, 0, width, height), "knob": ((width-size)//2, 4, size, size),
                 "name": (0, 0, width, 1), "value": (2, height-34, width-4, 28)}
        definition = build_native_smooth_dial_with_indicator_definition(
            rects=rects, skins_dir=ctx.skins_dir, active=active, handle_name="Active")
        # The adjacent exported kSmooth cell owns the one ActiveText caption.
        definition["componentsData"] = [c for c in definition["componentsData"]
                                         if c["componentData"]["name"] != "name"]
        return definition
    if side:
        rects = {"focus": (0, 0, width, height), "knob": (6, (height-size)//2, size, size),
                 "name": (74, height//2-26, width-78, 22),
                 "value": (74, height//2, width-78, 28)}
    else:
        name_y = min(size+10, height-52)
        rects = {"focus": (0, 0, width, height), "knob": ((width-size)//2, 4, size, size),
                 "name": (2, name_y, width-4, 22), "value": (2, name_y+24, width-4, 28)}
    definition = compact_dial_definition(level_definition(skins_dir=ctx.skins_dir), rects=rects, polarity=polarity)
    for child in definition["componentsData"]:
        if child["componentData"]["name"] == "knob":
            child["componentData"]["data"] = (pan_data if polarity == "BiPolar" else synthyboy_dial_data)(
                ctx.skins_dir, frame_size=size, bipolar=polarity == "BiPolar")
        elif child["componentData"]["name"] in ("name", "value"):
            data = child["componentData"]["data"]
            data["textStyle"]["font"]["height"] = 14 if child["componentData"]["name"] == "name" else 20
            data["textStyle"]["justification"] = "left verticallyCentred" if side else "horizontallyCentred verticallyCentred"
    return definition


def _active_text(ctx, text, width, height):
    from components.native.active_text.active_text import (
        active_text_definition, render_active_text_art,
    )
    tag = text.lower().replace(" ", "_") + f"_{width}x{height}"
    on, off = f"lfo_{tag}_on.png", f"lfo_{tag}_off.png"
    from components.style import size
    for active, filename in ((True, on), (False, off)):
        render_active_text_art(text, active, width, height, font_size=size("font.caption"), anchor="mm").save(ctx.skins_dir / filename)
    return active_text_definition(text, on, off, (width, height), handle="Active")


def _choice(ctx, width, height, caption=None, number=False):
    from components.native.selector.selector import selector_definition
    return selector_definition(None, "fx editor number menu" if number else "fx editor choice menu",
                               (width, height), ctx.skins_dir, title="Data",
                               caption_text=caption, label_height=17)


def _action(ctx, width, height, *, text=None, icon=None, action=1, count=1):
    from components.native.button.button import action_button
    child = action_button(ctx, text or icon, (0, 0, width, height), "Data", text=text, icon=icon)
    child["componentData"]["data"].update(buttonId=action, numButtonsInGroup=count)
    return _definition([child])


def _sync_radio(ctx, width, height):
    from components.native.radio_button.radio_button import build_radio_button_entries, VARIANT_RADIO_DOT
    vertical = width < 3*56
    cell_w, cell_h = (width, height//3) if vertical else (width//3, height)
    children = build_radio_button_entries("LFO Sync", (0, 0, cell_w*3, cell_h),
                                          ("HZ", "BPM", "KEY"), VARIANT_RADIO_DOT, "Data", ctx.skins_dir)
    if vertical:
        for i, child in enumerate(children):
            child["bounds"]["bounds"] = f"0 {i*cell_h} {cell_w} {cell_h}"
    # The existing append-only Sync handle is the engine's FIVE-state value:
    # Hz=0, Tempo=1, dotted=2, triplet=3, Key=4. Keep that API unchanged.
    for child, index in zip(children, (0, 1, 4)):
        child["componentData"]["data"].update(buttonId=index, numButtonsInGroup=5)
    bpm = children.pop(1)
    for low, high, index in ((0, 1, 1), (2, 2, 2), (3, 3, 3), (4, 4, 1)):
        copy_bpm = copy.deepcopy(bpm)
        copy_bpm["componentData"]["data"]["buttonId"] = index
        copy_bpm["bounds"].update(version=2, additionalInvalidatingHandles=[f"RangedEnabling/{low}/{high}/5/Data"])
        children.append(copy_bpm)
    return _definition(children)


def _definitions(ctx):
    from components.native.button.button import icon_button
    icon_button("expand", "#e6e6e6").save(ctx.skins_dir / "lfo_native_expand.png")
    # Generic definitions still used by the existing nested shape editor.
    return {
        "Uni": _dial("UniPolar", ctx), "Bi": _dial("BiPolar", ctx),
        # The compact Play page binds its RND stereo handle to this shared control.
        "Toggle": _definition([_label("Name", (0.02, 0.03, 0.96, 0.36), 13, proportional=True),
                               _button((0.12, 0.38, 0.76, 0.58), 1, 1, ("toggle_on.png", "toggle_off.png"))]),
        "Choice": _choice(ctx, 300, 62), "Number": _choice(ctx, 236, 56, number=True),
        "Disabled": _definition([_label("Name", (0, 0, 1, .45), colour="ff9699a4", proportional=True),
                                  _label("Value", (0, .45, 1, .55), 17, "ff9699a4", proportional=True)], inactive=True),
    }


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == "LFO"), None)
    if tab is None:
        return

    # Regenerate with lfo_ui_host --skin-contract from the standalone layout.
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    base = mixer_peek_index(ctx.param_count) + LFO_OFFSET
    params = {p["key"]: p for p in ctx.params}
    choices = {field: list(params[key]["choices"]) for field, key in CHOICE_FIELDS.items()}
    choices[SELECT] = [f"LFO {i}" for i in range(1, 11)] + [f"RND {i}" for i in range(1, 5)]
    choices[SHAPE][-1] = "Drawn"
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    for field, names in choices.items():
        replacements.append({"key": f"Parameter {base + field}", "value": names})

    definitions = _definitions(ctx)

    items = tab["componentDefinition"]["componentsData"]
    items[:] = [c for c in items if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
    ranges = {}
    for layout, field, action, *rect in sorted(contract):
        spans = ranges.setdefault((field, action, tuple(rect)), [])
        if spans and spans[-1][1] + 1 == layout:
            spans[-1][1] = layout
        else:
            spans.append([layout, layout])
    for (field, action, rect), spans in ranges.items():
        for first, last in spans:
            _widget(items, base, choices, field, action, rect, first, last, ctx, definitions)
    for name, value in definitions.items():
        ctx.add_component_definition(DEFINITION + name, value)
    _apply_shape_editor(tui, items, ctx)
    _apply_shape_detail(tui, items, ctx)


def _widget(items, base, choices, field, action, rect, first, last, ctx, definitions):
    layout_handle = f"Parameter {base + LAYOUT}"
    if first == last:
        gates = (f"IndexedEnabling/{first}/{LAYOUT_STEPS}/{layout_handle}",)
    else:
        gates = (f"RangedEnabling/{first}/{last}/{LAYOUT_STEPS}/{layout_handle}",)
    handle = f"Parameter {base + field}"
    width, height = rect[2:]
    label = FIELD_CAPTIONS.get(field)
    widget = f"Field{field}_{width}x{height}_{action}"
    if widget not in definitions:
        if field == ACTION:
            definitions[widget] = _action(ctx, width, height, text="TARGETS" if action == 1 else None,
                                           icon="expand" if action == 2 else None, action=action, count=3)
        elif field == 10:
            definitions[widget] = _sync_radio(ctx, width, height)
        elif field in TOGGLE_FIELDS:
            definitions[widget] = _active_text(ctx, label, width, height)
        elif field in choices or field in NUMBER_FIELDS:
            definitions[widget] = _choice(ctx, width, height, caption=label, number=field in NUMBER_FIELDS)
        else:
            definitions[widget] = _dial("BiPolar" if field == 18 else "UniPolar", ctx, width, height,
                                         smooth=field == SMOOTH_TIME)
    if field == ACTION:
        handle = "FromGesture/" + handle
    name = PREFIX + f"{first}-{last}.{field}.{action}"
    payload = {"version": 1, "handleName": "Data"}
    handles = [("Data", handle)]
    if label is not None:
        handles.append(("Caption", f"WithName[{label}]/"+handle))
    if field in TOGGLE_FIELDS:
        handles.append(("Active", handle))
    if field == SMOOTH_TIME:
        handles.append(("Active", f"Parameter {base + SMOOTH}"))
        off = f"IndexedEnabling/0/2/Parameter {base + SMOOTH}"
        on = f"IndexedEnabling/1/2/Parameter {base + SMOOTH}"
        inactive_widget = widget + "Off"
        definitions.setdefault(inactive_widget, _dial("UniPolar", ctx, width, height, active=False, smooth=True))
        items.append(_entry(name + " inactive", DEFINITION + inactive_widget, payload, rect, handles, (*gates, off)))
        gates = (*gates, on)
    items.append(_entry(name, DEFINITION + widget, payload, rect, handles, gates, focus=True))


# The stock components own only the cells exported by ShapeEditorPage. The
# transparent overlay keeps the canvas, annotations and Q-Link framing.
def _apply_shape_editor(tui, items, ctx):
    from PIL import Image, ImageDraw, ImageFont
    from native_ui_params import (
        LFO_EDITOR_AVAILABILITY, LFO_EDITOR_SCALE, LFO_EDITOR_VALUES, LFO_EDITOR_VIEW,
    )

    contract = json.loads(Path(__file__).with_name("lfo-shape-editor-contract.json").read_text(encoding="utf-8"))
    base = mixer_peek_index(ctx.param_count)
    gate = f"IndexedEnabling/1/2/Parameter {base + LFO_EDITOR_VIEW}"
    optional = {field: base + LFO_EDITOR_AVAILABILITY + i for i, field in enumerate(contract["availability"])}
    curve_gate = f"Parameter {base + LFO_EDITOR_SCALE}"
    prefix = PREFIX + "Editor "
    definition = "lfoEdit"
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    mode = next(p["choices"] for p in ctx.params if p["key"] == "lfo1.sync_type")

    def text_label(text=None, colour="ffe6e6e6", size=17):
        return _label("Name" if text is None else "Text", (0, 0, 1, 1), size, colour, text, proportional=True)

    generic_action = _action(ctx, 150, 56, text=" ")
    generic_action["componentsData"][0]["bounds"].update(boundsType="Proportional", bounds="0 0 1 1")
    generic_action["componentsData"].append(text_label())
    defs = {
        "Action": generic_action,
        "DisabledAction": _definition([text_label(colour="ff9699a4")], inactive=True),
        "Disabled": _definition([text_label(colour="ff9699a4")], inactive=True),
    }

    for display in ("CURVE", "SCALE"):
        for enabled in (False, True):
            colour = "ffe6e6e6" if enabled else "ff9699a4"
            defs[display + ("Active" if enabled else "Disabled")] = _definition([
                _label("Text", (0.02, 0.03, 0.96, 0.42), colour=colour, text=display, proportional=True),
                _label("Value", (0.02, 0.43, 0.96, 0.54), 17, colour, proportional=True),
            ], popup="fx editor number menu" if enabled else None, inactive=not enabled)

    def widget(name, kind, rect, handle, gates, available=None):
        remap = [("Data", handle)]
        if "Toggle" in kind:
            remap.append(("Active", handle))
        focus = True
        if available is not None:
            remap.append(("Enabled", f"Parameter {available}"))
            focus = "Children"
        item = _entry(prefix + name, kind, {"version": 1, "handleName": "Data"}, rect, remap, gates, focus=focus)
        if focus == "Children":
            item["bounds"]["acceptsHWFocus"] = focus
        items.append(item)

    def optional_definition(name, active, passive):
        children = []
        for i, child_type in enumerate((passive, active)):
            children.append(_entry("state", child_type, {"version": 1, "handleName": "Data"},
                                   (0, 0, 1, 1), gates=(f"IndexedEnabling/{i}/2/Enabled",),
                                   focus=i == 1, proportional=True))
        container = _definition(children, inactive=True)
        for style in ("focussed", "unfocussed"):
            container["backgroundData"][style]["colour"] = "0"
        defs[name] = container

    optional_definition("OptionalAction", definition + "Action", definition + "DisabledAction")

    optional_definition("OptionalNumber", DEFINITION + "Number", definition + "Disabled")
    for caption in ("CURVE", "SCALE"):
        optional_definition("Optional" + caption, definition + caption + "Active", definition + caption + "Disabled")

    def tool_art(i, tool, active):
        from components.native.radio_button.radio_button import render_segmented_button_art
        filename = f"lfo_edit_tool_{i}_{'on' if active else 'off'}.png"
        _, _, w, h = tool["rect"]
        render_segmented_button_art(tool["name"], active, w, h).save(ctx.skins_dir / filename)
        return filename

    for field, spec in enumerate(contract["fields"]):
        name, kind, rect = spec["name"], spec["type"], spec["rect"]
        parameter = f"Parameter {base + LFO_EDITOR_VALUES + field}"
        if kind == "Tool":
            for i, tool in enumerate(contract["tools"]):
                art_pair = (tool_art(i, tool, True), tool_art(i, tool, False))
                item = _button(tool["rect"], i, len(contract["tools"]), art_pair)
                item["componentData"]["name"] = prefix + "Tool " + tool["name"]
                item["handle remapping"]["map"] = [{"key": "Data", "value": parameter}]
                item["bounds"].update(version=2, acceptsHWFocus="Yes", boundsType="Absolute",
                                      additionalInvalidatingHandles=[gate])
                items.append(item)
            continue
        if kind == "Choice":
            replacements.append({"key": parameter, "value": list(mode)})
        if kind in ("Choice", "Number") and name != "CURVE":
            local_kind = kind + str(field)
            defs[local_kind] = _choice(ctx, rect[2], rect[3], caption=name, number=kind == "Number")
            if field in optional:
                optional_definition("Optional" + local_kind, definition + local_kind, definition + "Disabled")
                kind = local_kind
            else:
                widget(name, definition + local_kind, rect, parameter, (gate,))
                continue
        if kind == "Toggle":
            toggle_name = "Toggle" + str(field)
            defs[toggle_name] = _active_text(ctx, name, rect[2], rect[3])
            if field in optional:
                optional_definition("Optional" + toggle_name, definition + toggle_name, definition + "Disabled")
                kind = toggle_name
            else:
                widget(name, definition + toggle_name, rect, parameter, (gate,))
                continue
        if kind == "Action":
            action_name = "Action" + str(field)
            defs[action_name] = _action(ctx, rect[2], rect[3], text=name)
            kind = action_name
            parameter = "FromGesture/" + parameter
        available = optional.get(field)
        if name == "CURVE":
            for scale, caption in enumerate(("CURVE", "SCALE")):
                widget(caption, definition + "Optional" + caption, rect, parameter,
                       (gate, f"IndexedEnabling/{scale}/2/{curve_gate}"), available)
        elif available is not None:
            if kind.startswith("Action"):
                optional_definition("Optional" + kind, definition + kind, definition + "DisabledAction")
            widget(name, definition + "Optional" + kind, rect, parameter, (gate,), available)
        else:
            widget(name, (definition if kind.startswith("Action") else DEFINITION) + kind, rect, parameter, (gate,))
    defs["Action"]["disableCoarseDataWheel"] = True
    for name, data in defs.items():
        ctx.add_component_definition(definition + name, data)


def _apply_shape_detail(tui, items, ctx):
    from native_ui_params import LFO_DETAIL

    rects = json.loads(Path(__file__).with_name("lfo-shape-detail-contract.json").read_text(encoding="utf-8"))
    base = mixer_peek_index(ctx.param_count) + LFO_DETAIL
    prefix = PREFIX + "Detail "
    point_view = f"IndexedEnabling/1/4/Parameter {base}"
    list_view = f"RangedEnabling/2/3/4/Parameter {base}"
    point, header_back, rows = (f"Parameter {base + offset}" for offset in (5, 6, 7))
    payload = {"version": 1, "handleName": "Data"}

    detail_definitions = set()
    def add(name, kind, rect, handle, gates, focus=True):
        if kind in ("lfoNativeChoice", "lfoNativeNumber"):
            variant = kind + f"_{rect[2]}x{rect[3]}"
            if variant not in detail_definitions:
                ctx.add_component_definition(variant, _choice(ctx, rect[2], rect[3], caption=name,
                                                              number=kind == "lfoNativeNumber"))
                detail_definitions.add(variant)
            kind = variant
        items.append(_entry(prefix + name, kind, payload, rect, (("Data", handle),), gates, focus=focus))

    name = _label("Name", (0.025, 0, 0.95, 1), 19, proportional=True)
    name["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
    action = _action(ctx, 300, 56, text=" ")
    action["componentsData"][0]["bounds"].update(boundsType="Proportional", bounds="0 0 1 1")
    action["componentsData"].append(name)
    action["disableCoarseDataWheel"] = True
    ctx.add_component_definition("lfoDetailRow", action)
    ctx.add_component_definition("lfoDetailHeader", _definition([name], inactive=True))

    for field, (title, kind) in enumerate((
        ("BACK", "lfoEditAction"), ("MODULATE", "lfoNativeChoice"),
        ("AMOUNT", "lfoNativeNumber"), ("REMOVE", "lfoEditAction"),
    )):
        parameter = f"Parameter {base + field + 1}"
        if field in (0, 3):
            handle = "FromGesture/" + parameter
        else:
            handle = parameter
        if field == 0:
            add(title, kind, rects[field], handle, (point_view,))
            continue
        active = {1: (1, 4), 2: (2, 3), 3: (2, 4)}[field]
        gate = f"RangedEnabling/{active[0]}/{active[1]}/5/{point}"
        add(title, kind, rects[field], handle, (point_view, gate))
        disabled = ((0, 0),) if field == 1 else ((0, 1), (4, 4)) if field == 2 else ((0, 1),)
        for low, high in disabled:
            gate = (f"IndexedEnabling/{low}/5/{point}" if low == high
                    else f"RangedEnabling/{low}/{high}/5/{point}")
            passive = "lfoEditDisabledAction" if field == 3 else "lfoNativeDisabled"
            add(title + f" unavailable {low}", passive, rects[field], handle, (point_view, gate), focus=False)

    replacements = tui["pageData"].setdefault("replacementStrings", [])
    replacements.append({"key": f"Parameter {base + 2}", "value": ["TIME", "LEVEL", "CURVE"]})

    for active in (False, True):
        gate = f"IndexedEnabling/{int(active)}/2/{header_back}"
        handle = ("FromGesture/" if active else "") + f"Parameter {base + 8}"
        add("Header" if active else "Heading", "lfoDetailRow" if active else "lfoDetailHeader",
            rects[4], handle, (list_view, gate), focus=active)
    for row, rect in enumerate(rects[5:]):
        limit = len(rects) - 5
        gate = f"RangedEnabling/{row + 1}/{limit}/{limit + 1}/{rows}"
        add(f"Row {row + 1}", "lfoDetailRow", rect, f"FromGesture/Parameter {base + 9 + row}", (list_view, gate))
