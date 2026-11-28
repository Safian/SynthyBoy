# SynthyBoy - native MPC Macro cards and their target-navigation handles.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from components.native.macro_card.macro_card import (
    CARD_HEIGHT, CARD_WIDTH, MACRO_CARD_POSITIONS, build_macro_card_definitions, build_macro_card_instance,
)
from components.native.button.button import action_button
from components.native.mod_row.mod_row import build_editor_cell_definition, editor_label
from components.style import size as style_size
import native_ui_params as handles
from native_ui_params import MACRO_TARGET, native_base_index

CHOICE_MENU = "fx editor choice menu"   # the choice menu name the Mod tab's cell definitions are built with


def _entry(name, kind, data, rect, remap, gates, focus=False):
    bounds = {"version": 2, "acceptsHWFocus": "Yes" if focus else "No", "showWhenDataModelInvalid": "Hide",
              "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % tuple(rect),
              "additionalInvalidatingHandles": list(gates)}
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


def destination_picker(ctx, defs):
    """The add-target list of a macro: the same native picker the Mod tab shows (rows, BACK, GROUPS, PREVIOUS, NEXT, title), bound to the same modboard
    handles and shown while the plugin publishes the list layout; the rows are the display strings of the choice handles (two levels: groups, then parameters)."""
    base = native_base_index(ctx.param_count)
    gate = f"IndexedEnabling/1/2/Parameter {base + handles.MOD_LIST_GATE}"
    row_rect = style_size("mod_picker.row")
    definition = f"modNative_list_{row_rect[2]}x{row_rect[3]}"
    if definition not in defs:   # normally the Mod tab's hook has built it already (same recipe, same key)
        ctx.add_component_definition(definition, build_editor_cell_definition(
            "list", row_rect[2], row_rect[3], choice_menu=CHOICE_MENU, skins_dir=ctx.skins_dir))
    items = []
    for row in range(handles.MOD_LIST_ROWS):
        rect = [row_rect[0], row_rect[1] + row * style_size("mod_picker.row_step"), *row_rect[2:]]
        data = f"Parameter {base + handles.MOD_LIST_ROW + row}"
        items.append(_entry(f"Macro Native Picker Row {row + 1}", definition, {"version": 1, "handleName": "Data"}, rect,
                            [("Data", data), ("Caption", f"WithName[]/{data}")],
                            [gate, f"IndexedEnabling/1/2/Parameter {base + handles.MOD_LIST_ROW_GATE + row}"], True))
    for caption, key, field in (("BACK", "back", handles.MOD_LIST_BACK), ("GROUPS", "groups", handles.MOD_LIST_GROUPS),
                                ("PREVIOUS", "prev", handles.MOD_LIST_PREV), ("NEXT", "next", handles.MOD_LIST_NEXT)):
        rect = style_size("mod_picker." + key)
        items.append(action_button(ctx, "Macro Native Picker " + caption, rect, f"FromGesture/Parameter {base + field}",
                                   text=caption, width=rect[2], gates=[gate]))
    title = editor_label("Value", style_size("mod_picker.title"), just="horizontallyCentred verticallyCentred")
    title["componentData"]["name"] = "Macro Native Picker Title"
    title["handle remapping"] = {"version": 1, "map": [{"key": "Data", "value": f"Parameter {base + handles.MOD_LIST_TITLE}"}]}
    title["bounds"]["version"] = 2
    title["bounds"]["additionalInvalidatingHandles"] = [gate]
    items.append(title)
    return items


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tab = next((t for t in tui["pageData"]["tabs"] if t["tabName"] == "Macros"), None)
    if tab is None:
        return
    components = tab["componentDefinition"]["componentsData"]
    markers = [c for c in components if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
    modal = [c for c in components if c["componentData"]["name"] == "Native Modulation Editor"]
    instance = next(c for c in markers if c["componentData"]["name"] == "Overlay Instance macros")
    root_gate = "RangedEnabling/0/18/33/" + instance["handle remapping"]["map"][0]["value"]
    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    card_defs = build_macro_card_definitions(
        ctx.skins_dir, action_text="TARGETS", include_name=False
    )
    for key, val in card_defs.items():
        if key not in defs:
            ctx.add_component_definition(key, val)

    first_target = native_base_index(ctx.param_count) + MACRO_TARGET
    cards = []
    # All 8 compact MacroCards in one bottom row across 1280x628 (Bank 1: 0..3, Bank 2: 4..7)
    for i, (x, y) in enumerate(MACRO_CARD_POSITIONS):
        caption = f"Macro {i + 1}"
        cards.append(build_macro_card_instance(
            caption + " Card", ctx.param_index(f"macro.{i + 1}"),
            (x, y, CARD_WIDTH, CARD_HEIGHT),
            target_handle=f"FromGesture/Parameter {first_target + i}", caption=caption, root_gate=root_gate,
        ))
    components[:] = modal + cards + destination_picker(ctx, defs) + markers
