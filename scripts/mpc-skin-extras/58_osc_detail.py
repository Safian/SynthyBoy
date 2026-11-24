# SynthyBoy - native control grid of the standalone OSC detail pages.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import json
import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components import style
from components.native.selector.selector import OSC_TAB_NAMES, passive_layer
from native_warp_picker import CONTROL as WARP_CONTROL
from native_ui_params import (OSC_ABC_SETTINGS, OSC_DETAIL, OSC_DETAIL_CELLS, OSC_ROOT_LOAD,
                              OSC_ROOT_LOAD_VIEW, OSC_SETTINGS_VIEW, OSC_SETTINGS_LAYOUTS, mixer_peek_index)

CONTRACT = Path(__file__).with_name("osc-native-detail.json")


def entry(name, kind, rect, data, bindings=(), gate=None, focus="Yes", proportional=False):
    bounds = {"version": 2 if gate else 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Hide",
              "whenVisible": "Always", "boundsType": "Proportional" if proportional else "Absolute",
              "bounds": " ".join(map(str, rect))}
    if gate:
        bounds["additionalInvalidatingHandles"] = [gate]
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": key, "value": value} for key, value in bindings]},
            "bounds": bounds}


def custom(name, kind, rect, bindings=(), gate=None, focus="Children", proportional=False):
    return entry(name, kind, rect, {"version": 1, "handleName": "Data"}, bindings, gate, focus, proportional)


def stepper():
    kids = []
    for i, (name, image, x) in enumerate((("Previous", "osc_native_table_prev.png", 0),
                                          ("Next", "osc_native_table_next.png", 0.61))):
        data = {"version": 2, "onImage": image, "offImage": image, "buttonId": 1 if i == 0 else 3,
                "numButtonsInGroup": 4, "handleName": "Step", "gestureBehaviour": "Instant"}
        kids.append(entry(name, "Button", (x, 0.39, 0.39, 0.61), data, proportional=True))
    for name, y, size, colour in (("Name", 0, 14, style.argb("text.caption")), ("Value", 0.18, 17, style.argb("text.icon"))):
        data = {"version": 1, "type": name, "handleName": "Data", "textStyle": {"version": 1,
                "colour": colour, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": size},
                "justification": "horizontallyCentred verticallyCentred", "case": "Original"}}
        kids.append(entry(name, "Label", (0.02, y, 0.96, 0.21), data, focus="No", proportional=True))
    return passive_layer(kids)


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    tabs = [tab for tab in tui["pageData"]["tabs"] if tab["tabName"] in (*OSC_TAB_NAMES, "Osc")]
    if not tabs:
        return
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    native_base = mixer_peek_index(ctx.param_count)
    base = native_base + OSC_DETAIL
    count = OSC_DETAIL_CELLS
    view = f"Parameter {base}"
    visual_steps = 7 + len(contract["menus"])
    ctx.add_component_definition("oscDetailStepper", stepper())
    tile = []
    for i, definition in enumerate(("oscNativeSetting_dial_uni", "oscNativeSetting_dial_bi", "oscNativeSetting_toggle",
                                    "oscNativeSetting_dial_uni", "oscDetailStepper", "oscNativeSetting_action"), 1):
        bind = (("Caption", "Data"),)
        if i == 6:
            bind += (("Data", "FromGesture/Data"),)
        tile.append(custom(str(i), definition, (0, 0, 1, 1), bind,
                           f"IndexedEnabling/{i}/{visual_steps}/Kind", "Children" if i == 5 else "Yes", True))
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    existing = {row["key"]: row["value"] for row in replacements}
    params = {info["key"]: info for info in ctx.params}
    for i, suffix in enumerate(contract["menus"]):
        parameter = f"Parameter {base + 4 + 3 * count + i}"
        control = WARP_CONTROL if suffix in ("warp.type", "warp2.type") else "oscNativeSetting_menu"
        tile.append(custom(str(i + 7), control, (0, 0, 1, 1),
                           (("Caption", "Data"), ("Data", parameter)), f"IndexedEnabling/{i + 7}/{visual_steps}/Kind", "Yes", True))
        # The root's Warp2 menu already has the release display names for this domain.
        warp2 = f"Parameter {native_base + OSC_ABC_SETTINGS + 10}"
        choices = existing[warp2] if suffix == "warp2.type" else params["osc1." + suffix]["choices"]
        replacements.append({"key": parameter, "value": choices})
    ctx.add_component_definition("oscDetailTile", passive_layer(tile))

    page = []
    for frame, geometry in (("Plain", contract["plain"]), ("Graph", contract["graph"])):
        x, y, w, h, step, rows = geometry
        row_items = []
        for row in range(rows):
            value = f"Parameter {{Increment:Base={base + 1 + row},Step=4}}"
            adjust = f"FromGesture/Parameter {{Increment:Base={base + 1 + count + row},Step=4}}"
            kind = f"Parameter {{Increment:Base={base + 1 + 2 * count + row},Step=4}}"
            row_items.append(custom(str(row), "oscDetailTile", (0, row * step, w, h),
                                    (("Data", value), ("Step", adjust), ("Kind", kind)), "NonZero/Kind"))
        definition = passive_layer(row_items)
        definition["version"] = 3
        definition.pop("componentsData")
        definition["repeats"] = 8
        definition["componentsData"] = row_items
        name = f"oscDetail{frame}Grid"
        ctx.add_component_definition(name, definition)
        gate = f"IndexedEnabling/3/6/{view}" if frame == "Plain" else f"RangedEnabling/4/5/6/{view}"
        page.append(custom(frame, name, (x, y, 8 * w, h + (rows - 1) * step), (("Data", view),), gate))
    # Sub and Noise expose BLEND at the root; neither has a MORE sheet.
    settings_view = f"Parameter {native_base + OSC_SETTINGS_VIEW}"
    for name, layout in (("sub_blend", 1), ("noise_blend", 2), ("noise_sample_blend", 3)):
        control = custom(name, "oscNativeSetting_dial_uni", contract[name],
                         (("Data", f"Parameter {base + 1}"), ("Caption", "WithName[BLEND]/" + f"Parameter {base + 1}")),
                         f"IndexedEnabling/2/6/{view}")
        control["bounds"]["additionalInvalidatingHandles"].append(f"IndexedEnabling/{layout}/{OSC_SETTINGS_LAYOUTS}/{settings_view}")
        page.append(control)
    for name, i, gate in (("back", 1, f"RangedEnabling/3/5/6/{view}"), ("load", 2, f"IndexedEnabling/5/6/{view}")):
        data = f"Parameter {base + 1 + 3 * count + i}"
        page.append(custom(name, "oscNativeSetting_action", contract[name],
                           (("Data", "FromGesture/" + data), ("Caption", data)), gate, "Yes"))
    load_view = native_base + OSC_ROOT_LOAD_VIEW
    for i, source in enumerate("ABC"):
        data = f"Parameter {native_base + OSC_ROOT_LOAD + i}"
        page.append(custom(f"LOAD {source}", "oscNativeSetting_action", contract["root_load"],
                           (("Data", "FromGesture/" + data), ("Caption", data)),
                           f"IndexedEnabling/{i + 1}/4/Parameter {load_view}", "Yes"))
    ctx.add_component_definition("oscNativeDetail", passive_layer(page))
    for tab in tabs:
        tab["componentDefinition"]["componentsData"].append(
            custom("Osc Native Detail", "oscNativeDetail", (0, 0, 1280, 628), (("Data", view),), f"NonZero/{view}"))
