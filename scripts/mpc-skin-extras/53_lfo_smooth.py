# SynthyBoy - native standalone LFO and RND controls.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import json
from pathlib import Path

from native_ui_params import LFO_OFFSET, LFO_TARGETS_ACTION, native_base_index
from components import style as _ui_style
from components.native.button.button import button as native_button
from components.native.selector.selector import labelled_choice_drawer

CONTRACT = Path(__file__).with_name("lfo-native-contract.json")
PREFIX = "LFO 1 Native "
DEFINITION = "lfoNative"
SELECT, SHAPE, STYLE, SMOOTH, SMOOTH_TIME, ACTION, LAYOUT = range(7)   # SELECT: RESERVED (the page has no selector: the 14 hardware sub-tabs name the LFO)
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


def _label(kind, rect, size=_ui_style.size("lfo.dial.caption_font"), colour=_ui_style.argb('text.icon'), text=None, proportional=False):
    data = {"version": 1, "textStyle": {"version": 1,
            "font": {"version": 1, "name": _ui_style.size("font.family"), "style": _ui_style.size("font.style.semibold"), "height": float(size)},
            "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": "Original"},
            "type": kind, "handleName": "Data"}
    if text is not None:
        data["text"] = text
    return _entry(kind.lower(), "Label", data, rect, proportional=proportional)


def _definition(children, popup=None, inactive=False):
    background = {"version": 1, "colour": _ui_style.argb('screen.lfo_smooth.colour'), "image": ""}
    actions = [{"version": 1, "onAction": trigger, "handler": "Show Overlay",
                "handleName": "Data", "additionalData": popup}
               for trigger in ("Mouse Down", "Enter Pressed")] if popup is not None else []
    return {"version": 2, "actions": actions,
            "backgroundData": {"version": 1, "focussed": dict(background, colour=_ui_style.argb('screen.lfo_smooth.focussed')), "unfocussed": background},
            "ignoreMousePresses": inactive, "disableCoarseDataWheel": inactive, "componentsData": children}


# Every definition below is built from the current shared native primitives.
# Geometry comes from the C++ skin contract, never from proportional dial scaling.
FIELD_CAPTIONS = {
    0: "SOURCE", 1: "WAVEFORM", 2: "STYLE", 3: "SMOOTH", 4: "TIME",
    7: "RATE", 8: "DIVISION", 9: "RISE", 10: "SYNC", 11: "MODE", 12: "MODE",
    13: "PHASE", 14: "DELAY", 15: "HOST", 16: "DIRECTION", 17: "SWING",
    18: "STEREO", 19: "MONO", 20: "STEREO", 21: "TRANSPOSE", 22: "TUNE",
}


def _dial(polarity, ctx, width=_ui_style.size("lfo.dial.default_width"), height=_ui_style.size("lfo.dial.default_height"), *, active=True, smooth=False):
    from components.native.level.level import (
        compact_dial_definition, level_definition,
        build_native_smooth_dial_with_indicator_definition, synthyboy_dial_data,
    )
    from components.native.pan.pan import synthyboy_dial_data as pan_data

    # Current compact LEVEL variants: side-by-side in wide cells, stacked in
    # narrow cells. The physical knob always remains a square >= 56 px.
    side = width >= _ui_style.size("lfo.dial.side_min_width") and height <= _ui_style.size("lfo.dial.side_max_height")
    size = _ui_style.size("lfo.dial.small_size") if side or height < _ui_style.size("lfo.dial.small_max_height") else _ui_style.size("lfo.dial.large_size")
    if smooth:
        # The knob sits at the left edge of its cell, next to the right-aligned SMOOTH caption of the cell before it; the value stands to its right, large.
        value_h = _ui_style.size("lfo.dial.value_height")
        rects = {"focus": (0, 0, width, height), "knob": (_ui_style.size("lfo.dial.side_left"), (height-size)//2, size, size),
                 "name": (0, 0, width, 1),
                 "value": (_ui_style.size("lfo.dial.side_left") + size + _ui_style.size("lfo.dial.smooth_value_gap"), (height-value_h)//2,
                           width-size-_ui_style.size("lfo.dial.side_left")-_ui_style.size("lfo.dial.smooth_value_gap")-2, value_h)}
        definition = build_native_smooth_dial_with_indicator_definition(
            rects=rects, skins_dir=ctx.skins_dir, active=active, handle_name="Active")
        for child in definition["componentsData"]:
            if child["componentData"]["name"] == "value":
                child["componentData"]["data"]["textStyle"]["font"]["height"] = float(_ui_style.size("lfo.dial.value_font"))
        # The adjacent exported kSmooth cell owns the On/Off label caption.
        definition["componentsData"] = [c for c in definition["componentsData"]
                                         if c["componentData"]["name"] != "name"]
        return definition
    if side:
        rects = {"focus": (0, 0, width, height), "knob": (_ui_style.size("lfo.dial.side_left"), (height-size)//2, size, size),
                 "name": (_ui_style.size("lfo.dial.side_text_left"), height//2-_ui_style.size("lfo.dial.side_caption_offset"), width-_ui_style.size("lfo.dial.side_text_margin"), _ui_style.size("lfo.dial.caption_height")),
                 "value": (_ui_style.size("lfo.dial.side_text_left"), height//2, width-_ui_style.size("lfo.dial.side_text_margin"), _ui_style.size("lfo.dial.value_height"))}
    else:
        name_y = min(size+_ui_style.size("lfo.dial.caption_gap"), height-_ui_style.size("lfo.dial.caption_bottom"))
        rects = {"focus": (0, 0, width, height), "knob": ((width-size)//2, _ui_style.size("lfo.dial.top"), size, size),
                 "name": (2, name_y, width-_ui_style.size("lfo.dial.top"), _ui_style.size("lfo.dial.caption_height")), "value": (2, name_y+_ui_style.size("lfo.dial.value_offset"), width-_ui_style.size("lfo.dial.top"), _ui_style.size("lfo.dial.value_height"))}
    definition = compact_dial_definition(level_definition(skins_dir=ctx.skins_dir), rects=rects, polarity=polarity)
    for child in definition["componentsData"]:
        if child["componentData"]["name"] == "knob":
            child["componentData"]["data"] = (pan_data if polarity == "BiPolar" else synthyboy_dial_data)(
                ctx.skins_dir, frame_size=size, bipolar=polarity == "BiPolar")
        elif child["componentData"]["name"] in ("name", "value"):
            data = child["componentData"]["data"]
            data["textStyle"]["font"]["height"] = _ui_style.size("lfo.dial.caption_font") if child["componentData"]["name"] == "name" else _ui_style.size("lfo.dial.value_font")
            data["textStyle"]["justification"] = "left verticallyCentred" if side else "horizontallyCentred verticallyCentred"
    return definition


def _on_off_label(ctx, text, width, height, anchor="rm"):
    from components.native.on_off_label.on_off_label import on_off_label_definition
    return on_off_label_definition(text, (width, height), skins_dir=ctx.skins_dir,
                                  anchor=anchor, asset_prefix="lfo_smooth")


def _active_text(ctx, text, width, height, anchor="mm"):
    from components.native.active_text.active_text import (
        active_text_definition, render_active_text_art,
    )
    tag = text.lower().replace(" ", "_") + f"_{width}x{height}" + ("" if anchor == "mm" else "_" + anchor)
    on, off = f"lfo_{tag}_on.png", f"lfo_{tag}_off.png"
    from components.style import size
    for active, filename in ((True, on), (False, off)):
        render_active_text_art(text, active, width, height, font_size=size("font.caption"), anchor=anchor).save(ctx.skins_dir / filename)
    return active_text_definition(text, on, off, (width, height), handle="Active")


def _choice(ctx, width, height, caption=None, number=False, popup=None):
    from components.native.selector.selector import selector_definition
    return selector_definition(None, popup or ("fx editor number menu" if number else "fx editor choice menu"),
                               (width, height), ctx.skins_dir, title="Data",
                               caption_text=caption, label_height=_ui_style.size('screen.lfo_smooth.choice.label_height'))


def _action(ctx, width, height, *, text=None, icon=None, action=1, count=1):
    from components.native.button.button import action_button
    child = action_button(ctx, text or icon, (0, 0, width, height), "Data", text=text, icon=icon)
    child["componentData"]["data"].update(buttonId=action, numButtonsInGroup=count)
    return _definition([child])


def _sync_radio(ctx, width, height, handle="Data"):
    from components.native.radio_button.radio_button import create_segmented_radio_button_group
    vertical = width < 3 * _ui_style.size("lfo.dial.option_min_width")
    children = create_segmented_radio_button_group("LFO Sync", (0, 0, width, height),
                                                   ("HZ", "BPM", "KEY"), handle, ctx.skins_dir, vertical=vertical, grid=True)
    # The existing append-only Sync handle is the engine's FIVE-state value:
    # Hz=0, Tempo=1, dotted=2, triplet=3, Key=4. Keep that API unchanged.
    for child, index in zip(children, (0, 1, 4)):
        child["componentData"]["data"].update(buttonId=index, numButtonsInGroup=5)
    bpm = children.pop(1)
    for low, high, index in ((0, 1, 1), (2, 2, 2), (3, 3, 3), (4, 4, 1)):
        copy_bpm = copy.deepcopy(bpm)
        copy_bpm["componentData"]["data"]["buttonId"] = index
        copy_bpm["bounds"].update(version=2, additionalInvalidatingHandles=[f"RangedEnabling/{low}/{high}/5/{handle}"])
        children.append(copy_bpm)
    return children


def _definitions(ctx):
    from components.native.button.button import icon_button
    icon_button("expand", _ui_style.color('text.icon')).save(ctx.skins_dir / "lfo_native_expand.png")
    # Generic definitions still used by the existing nested shape editor.
    return {
        "Uni": _dial("UniPolar", ctx), "Bi": _dial("BiPolar", ctx),
        "Choice": _choice(ctx, 300, 62), "Number": _choice(ctx, 236, 56, number=True),
    }


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == "LFO"), None)
    if tab is None:
        return

    # Regenerate with lfo_ui_host --skin-contract from the standalone layout.
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    base = native_base_index(ctx.param_count) + LFO_OFFSET
    params = {p["key"]: p for p in ctx.params}
    choices = {field: list(params[key]["choices"]) for field, key in CHOICE_FIELDS.items()}
    choices[SHAPE][-1] = "Drawn"
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    for field, names in choices.items():
        replacements.append({"key": f"Parameter {base + field}", "value": names})

    for field, names in choices.items():
        if field != 10:  # SYNC has its own native radio group.
            ctx.add_component_definition(f"lfoNativeChoiceDrawer{field}",
                                        labelled_choice_drawer(f"LFO {FIELD_CAPTIONS[field]}", names,
                                                               columns=2 if len(names) > 8 else 1))

    definitions = _definitions(ctx)

    items = tab["componentDefinition"]["componentsData"]
    items[:] = [c for c in items if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
    # Group captions are real native Labels, never baked into a background.
    from native_static_captions import caption_entries
    for layout in range(1, LAYOUT_STEPS):
        caption_layout = (1, 1, 3, 4, 4, 6)[layout - 1]
        gates = (f"IndexedEnabling/{layout}/{LAYOUT_STEPS}/Parameter {base + LAYOUT}",)
        items.extend(caption_entries(f"lfo{caption_layout}", prefix=PREFIX + f"Group {layout}", gates=gates))
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
    # Bind the library's native leaf Buttons directly to their host handles;
    # no opaque wrapper or intermediate Data remapping is needed.
    if field == ACTION or field == 10:
        from components.native.button.button import action_button
        if field == ACTION:
            if action == 1:
                handle = f"Parameter {native_base_index(ctx.param_count) + LFO_TARGETS_ACTION}"
            child = action_button(ctx, PREFIX + f"Action {action}", tuple(rect),
                                  "FromGesture/" + handle,
                                  text="TARGETS" if action == 1 else None,
                                  icon="expand" if action == 2 else None, gates=gates)
            items.append(child)
        else:
            for child in _sync_radio(ctx, width, height, handle):
                x, y, w, h = map(int, child["bounds"]["bounds"].split())
                child["bounds"].update(version=2, bounds=f"{rect[0]+x} {rect[1]+y} {w} {h}")
                child["bounds"].setdefault("additionalInvalidatingHandles", []).extend(gates)
                child["componentData"]["name"] = PREFIX + child["componentData"]["name"]
                items.append(child)
        return
    label = FIELD_CAPTIONS.get(field)
    widget = f"Field{field}_{width}x{height}_{action}"
    if widget not in definitions:
        if field == SMOOTH:
            definitions[widget] = _on_off_label(ctx, label, width, height, anchor="rm")
        elif field in TOGGLE_FIELDS:
            definitions[widget] = _active_text(ctx, label, width, height, anchor="mm")
        elif field in choices or field in NUMBER_FIELDS:
            definitions[widget] = _choice(ctx, width, height, caption=label, number=field in NUMBER_FIELDS,
                                          popup=f"lfoNativeChoiceDrawer{field}" if field in choices else None)
        else:
            definitions[widget] = _dial("BiPolar" if field == 18 else "UniPolar", ctx, width, height,
                                         smooth=field == SMOOTH_TIME)
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
    from native_ui_params import (
        LFO_EDITOR_AVAILABILITY, LFO_EDITOR_SCALE, LFO_EDITOR_VALUES, LFO_EDITOR_VIEW,
    )

    contract = json.loads(Path(__file__).with_name("lfo-shape-editor-contract.json").read_text(encoding="utf-8"))
    base = native_base_index(ctx.param_count)
    gate = f"IndexedEnabling/1/2/Parameter {base + LFO_EDITOR_VIEW}"
    optional = {field: base + LFO_EDITOR_AVAILABILITY + i for i, field in enumerate(contract["availability"])}
    curve_gate = f"Parameter {base + LFO_EDITOR_SCALE}"
    prefix = PREFIX + "Editor "
    definition = "lfoEdit"
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    mode = next(p["choices"] for p in ctx.params if p["key"] == "lfo1.sync_type")

    def text_label(text=None, colour=_ui_style.argb('text.icon'), size=_ui_style.size("lfo.dial.choice_font")):
        return _label("Name" if text is None else "Text", (0, 0, 1, 1), size, colour, text, proportional=True)

    generic_action = _action(ctx, *_ui_style.size("lfo.editor.action_size"), text=" ")
    generic_action["componentsData"][0]["bounds"].update(boundsType="Proportional", bounds="0 0 1 1")
    generic_action["componentsData"].append(text_label())
    defs = {
        "Action": generic_action,
    }

    for display in ("CURVE", "SCALE"):
        defs[display + "Active"] = _definition([
            _label("Text", (0.02, 0.03, 0.96, 0.42), text=display, proportional=True),
            _label("Value", (0.02, 0.43, 0.96, 0.54),
                   _ui_style.size("lfo.editor.curve_value_font"), proportional=True),
        ], popup="fx editor number menu")

    def widget(name, kind, rect, handle, gates, available=None):
        remap = [("Data", handle)]
        if "Toggle" in kind:
            remap.append(("Active", handle))
        focus = True
        if available is not None:
            remap.extend((f"NativeOptionalGate{value}", f"IndexedEnabling/{value}/2/Parameter {available}")
                         for value in (0, 1))
            focus = "Children"
        item = _entry(prefix + name, kind, {"version": 1, "handleName": "Data"}, rect, remap, gates, focus=focus)
        if focus == "Children":
            item["bounds"]["acceptsHWFocus"] = focus
        items.append(item)

    def optional_definition(name, active):
        # An unavailable field retains its own native face and value binding.
        # Do not replace selectors/buttons with a generic text-only rectangle.
        available_defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        available_defs.update({definition + key: value for key, value in defs.items()})
        inactive_name = "Inactive" + name
        defs[inactive_name] = _ui_style.apply_control_state(available_defs[active], ctx.skins_dir, active=False)
        passive = definition + inactive_name
        children = []
        for i, child_type in enumerate((passive, active)):
            children.append(_entry("state", child_type, {"version": 1, "handleName": "Data"},
                                   (0, 0, 1, 1), gates=(f"NativeOptionalGate{i}",),
                                   focus=i == 1, proportional=True))
        container = _definition(children, inactive=True)
        for style in ("focussed", "unfocussed"):
            container["backgroundData"][style]["colour"] = "0"
        defs[name] = container

    optional_definition("OptionalAction", definition + "Action")

    optional_definition("OptionalNumber", DEFINITION + "Number")
    for caption in ("CURVE", "SCALE"):
        optional_definition("Optional" + caption, definition + caption + "Active")

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
                item = native_button(prefix + "Tool " + tool["name"], tool["rect"], *art_pair,
                                     index=i, count=len(contract["tools"]), remaps=(("Data", parameter),), gates=(gate,))
                items.append(item)
            continue
        if kind == "Choice":
            replacements.append({"key": parameter, "value": list(mode)})
        if kind in ("Choice", "Number") and name != "CURVE":
            local_kind = kind + str(field)
            defs[local_kind] = _choice(ctx, rect[2], rect[3], caption=name, number=kind == "Number")
            if field in optional:
                optional_definition("Optional" + local_kind, definition + local_kind)
                kind = local_kind
            else:
                widget(name, definition + local_kind, rect, parameter, (gate,))
                continue
        if kind == "Toggle":
            toggle_name = "Toggle" + str(field)
            defs[toggle_name] = _active_text(ctx, name, rect[2], rect[3])
            if field in optional:
                optional_definition("Optional" + toggle_name, definition + toggle_name)
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
                optional_definition("Optional" + kind, definition + kind)
            widget(name, definition + "Optional" + kind, rect, parameter, (gate,), available)
        else:
            widget(name, (definition if kind.startswith("Action") else DEFINITION) + kind, rect, parameter, (gate,))
    defs["Action"]["disableCoarseDataWheel"] = True
    for name, data in defs.items():
        ctx.add_component_definition(definition + name, data)


def _apply_shape_detail(tui, items, ctx):
    from native_ui_params import LFO_DETAIL

    rects = json.loads(Path(__file__).with_name("lfo-shape-detail-contract.json").read_text(encoding="utf-8"))
    base = native_base_index(ctx.param_count) + LFO_DETAIL
    prefix = PREFIX + "Detail "
    point_view = f"IndexedEnabling/1/4/Parameter {base}"
    list_view = f"RangedEnabling/2/3/4/Parameter {base}"
    point, header_back, rows = (f"Parameter {base + offset}" for offset in (5, 6, 7))
    payload = {"version": 1, "handleName": "Data"}

    detail_definitions = set()
    def add(name, kind, rect, handle, gates, focus=True, active=True):
        if kind in ("lfoNativeChoice", "lfoNativeNumber"):
            variant = kind + f"_{rect[2]}x{rect[3]}"
            if variant not in detail_definitions:
                ctx.add_component_definition(variant, _choice(ctx, rect[2], rect[3], caption=name,
                                                              number=kind == "lfoNativeNumber"))
                detail_definitions.add(variant)
            kind = variant
        if not active:
            inactive_kind = kind + "Inactive"
            if inactive_kind not in detail_definitions:
                definitions = {entry["key"]: entry["value"] for entry in
                               tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
                ctx.add_component_definition(inactive_kind, _ui_style.apply_control_state(
                    definitions[kind], ctx.skins_dir, active=False))
                detail_definitions.add(inactive_kind)
            kind = inactive_kind
        items.append(_entry(prefix + name, kind, payload, rect, (("Data", handle),), gates, focus=focus))

    name = _label("Name", (0.025, 0, 0.95, 1), _ui_style.size("lfo.detail.header_font"), proportional=True)
    name["componentData"]["data"]["textStyle"]["justification"] = "left verticallyCentred"
    action = _action(ctx, *_ui_style.size("lfo.detail.row_size"), text=" ")
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
            add(title + f" unavailable {low}", kind, rects[field], handle,
                (point_view, gate), focus=False, active=False)

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
    __import__("sys").path.insert(0, str(Path(__file__).parent)); __import__("94_editor_commands").apply_pointmod_source_controls(tui, items, ctx, base, point_view)
