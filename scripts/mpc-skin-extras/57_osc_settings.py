import sys as _token_sys
from pathlib import Path as _TokenPath
_token_root = str(_TokenPath(__file__).resolve().parents[2])
if _token_root not in _token_sys.path:
    _token_sys.path.insert(0, _token_root)
from components import style as _ui_style

# SynthyBoy - MPC controls generated from the OSC root bindings.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from collections import defaultdict
import json
from pathlib import Path
import sys

SCRIPTS = Path(__file__).resolve().parent.parent
_REPO_ROOT = SCRIPTS.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from components import style
from components.native.button.button import TRANSPARENT_HIT
from components.native.level.level import COMPACT_48_RECTS, DEFAULT_DIAL_RECTS, compact_level_definition, level_definition
from components.native.pan.pan import compact_pan_definition, pan_definition
from components.native.name_value.name_value import DEFAULT_BOX_WIDTH, DEFAULT_HEIGHT, build_name_value_definition
from components.native.active_text.active_text import active_text_definition, write_active_text_assets
from components.native.selector.selector import OSC_TAB_NAMES, labelled_choice_drawer, passive_layer, selector_definition, write_osc_table_navigation_assets
import native_warp_picker
from native_ui_params import (
    OSC_ABC_SETTINGS, OSC_ABC_SETTINGS_FIELDS, OSC_POWER, OSC_SETTINGS, OSC_SETTINGS_FIELDS, OSC_SETTINGS_LAYOUTS,
    OSC_SETTINGS_VIEW, OSC_SOURCE, OSC_WARP_LAYOUT, mixer_peek_index,
)

CONTRACT = Path(__file__).with_name("osc-native-settings.json")
LAYER = "oscNativeSettingsLayer"
CHOICE_MENU = "fx editor choice menu"
NUMBER_MENU = "fx editor number menu"
TABLE_ART = ("osc_native_table_prev.png", TRANSPARENT_HIT, "osc_native_table_next.png")
ABC_LAYER = "oscNativeAbcSettings"


def _entry(name, kind, data, rect, remap=(), gates=(), focus=False, relative=False):
    bounds = {"version": 2 if gates else 1, "acceptsHWFocus": "Yes" if focus else "No",
              "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Proportional" if relative else "Absolute", "bounds": " ".join(map(str, rect))}
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": kind, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in remap]}, "bounds": bounds}


def _label(kind, rect, *, text=None, height=None, align="horizontallyCentred verticallyCentred"):
    caption = kind == "Name" or text is not None
    data = {
        "version": 1,
        "type": kind,
        "handleName": "Caption" if kind == "Name" else "Data",
        "textStyle": {
            "version": 1,
            "colour": style.argb("text.caption") if caption else style.argb("text.icon"),
            "font": {
                "version": 1,
                "name": _ui_style.size("font.family"),
                "style": _ui_style.size("font.style.semibold"),
                "height": height if height is not None else (14 if caption else 18),
            },
            "justification": align,
            "case": "Original",
        },
    }
    if text is not None:
        data["type"] = "Text"
        data["text"] = text
    return _entry(kind.lower(), "Label", data, rect, relative=True)


def _definition(children, handler=None, popup=""):
    bg = {"version": 1, "colour": "0", "image": ""}
    actions = [{"version": 1, "onAction": trigger, "handler": handler, "handleName": "Data", "additionalData": popup}
               for trigger in ("Mouse Down", "Enter Pressed")] if handler else []
    return {"version": 2, "actions": actions,
            "backgroundData": {"version": 1, "focussed": bg, "unfocussed": bg},
            "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": children}


def _fit_catalog_dial(kind, size, skins_dir):
    width, height = size
    # Use the two catalog sizes. Keep every filmstrip frame square, including in the
    # narrow OSC detail grid. Only the name and value fields use the remaining width.
    compact = width < DEFAULT_DIAL_RECTS["knob"][2] + style.size("height.high") or height < DEFAULT_DIAL_RECTS["focus"][3]
    if compact:
        build = compact_pan_definition if kind == "dial_bi" else compact_level_definition
        rects = COMPACT_48_RECTS
    else:
        build = pan_definition if kind == "dial_bi" else level_definition
        rects = DEFAULT_DIAL_RECTS
    result = build(skins_dir=skins_dir)
    face_height = rects["focus"][3]
    top = (height - face_height) // 2
    for child in result["componentsData"]:
        bounds = child["bounds"]
        x, y, w, h = map(int, bounds["bounds"].split())
        native = child["componentData"]
        if native["type"] == "Label":
            w = width - x
        elif native["type"] == "Focus":
            w = width
        elif native["type"] == "Knob":
            bounds["acceptsHWFocus"] = "Yes"
        bounds["bounds"] = f"{x} {y+top} {w} {h}"
    return result


def _control(kind, side, skins_dir=None, size=None, caption=""):
    # The base names are also embedded in the OSC detail tiles. Their narrowest
    # catalog-sized variant fits the small detail cell without clipping text or art.
    width, height = size or (style.size("name_value.width"), style.size("height.high"))
    if kind.startswith("dial_"):
        return _fit_catalog_dial(kind, (width, height), skins_dir)
    if kind == "number":
        definition = build_name_value_definition(rect=(0,0,width,DEFAULT_HEIGHT), skins_dir=skins_dir,
                                                 box_width=min(DEFAULT_BOX_WIDTH,width//2), popup=NUMBER_MENU)
        for child in definition["componentsData"]:
            bounds = child["bounds"]
            x, y, w, h = map(int, bounds["bounds"].split())
            bounds["bounds"] = f"{x} {y+(height-DEFAULT_HEIGHT)//2} {w} {h}"
        return definition
    if kind in ("menu","source"):
        control = selector_definition(None, CHOICE_MENU, (width,height), skins_dir, name="Caption")
        if kind == "source":
            for action in control["actions"]:
                action.update(handler="Toggle Switch", additionalData="")
        return control
    if kind == "toggle":
        h = style.size("height.high")
        text = caption or "ON"
        suffix = "".join(c.lower() if c.isalnum() else "_" for c in text)
        on, off = write_active_text_assets(skins_dir, text, width=width, height=h,
                                          on_name=f"osc_active_{suffix}_{width}_on.png",
                                          off_name=f"osc_active_{suffix}_{width}_off.png")
        result = active_text_definition(text, on, off, (width,h), handle="Data")
        button = result["componentsData"][0]
        button["bounds"]["bounds"] = f"0 {(height-h)//2} {width} {h}"
        button["bounds"]["acceptsHWFocus"] = "Yes"
        if not caption:
            result["componentsData"].append(_label("Name", (0,0,1,.25)))
        return result

    if kind == "table":
        parts = []
        for i, (art, x, width) in enumerate(zip(TABLE_ART, (0, 0.14, 0.86), (0.14, 0.72, 0.14)), 1):
            parts.append(_entry(f"Source action {i}", "Button", {
                "version": 2, "onImage": art, "offImage": art, "buttonId": i, "numButtonsInGroup": 4,
                "handleName": "Data", "gestureBehaviour": "Instant",
            }, (x, 0.05, width, 0.90), focus=True, relative=True))
        return _definition([
            *parts,
            _label("Text", (0.16, 0.06, 0.68, 0.32), text="SOURCE", height=_ui_style.size('screen.osc_settings.control.height')),
            _label("Value", (0.16, 0.38, 0.68, 0.56), height=_ui_style.size('screen.osc_settings.control.height.2')),
        ])
    if kind == "action":
        return _definition([_label("Name", (0.04, 0.04, 0.92, 0.92), height=_ui_style.size('screen.osc_settings.control.height.3'))], "Toggle Switch")
    raise ValueError("Unknown OSC control " + kind)


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    tabs = [tab for tab in tui["pageData"]["tabs"] if tab["tabName"] in {"Osc", *OSC_TAB_NAMES}]
    if not tabs:
        return
    variants = json.loads(CONTRACT.read_text(encoding="utf-8"))
    base = mixer_peek_index(ctx.param_count)
    view = f"Parameter {base + OSC_SETTINGS_VIEW}"
    warp_layout = f"Parameter {base + OSC_WARP_LAYOUT}"
    definitions, drawers, menus, cells, abc_cells, abc_cells_off = set(), set(), {}, [], [], []
    variants_by_layout = defaultdict(set)
    geometry = {}
    abc_captions = {}

    skins_dir = getattr(ctx, "skins_dir", None)
    if skins_dir is not None:
        write_osc_table_navigation_assets(skins_dir, TABLE_ART)

    # These shared names are referenced by the standalone detail grid and expanded views.
    for kind in ("dial_uni", "dial_bi", "menu", "toggle", "number", "dial_uni_side"):
        key = "oscNativeSetting_" + kind
        ctx.add_component_definition(key, _control(kind.removesuffix("_side"), kind.endswith("_side"), skins_dir))
        definitions.add(key)

    # Detail tiles use the existing generic-sized Selector; root cells get
    # geometry-specific instances of the same native Warp drawer contract.
    warp_control = _control("menu", False, skins_dir)
    for action in warp_control["actions"]:
        action["additionalData"] = native_warp_picker.DRAWER
    ctx.add_component_definition(native_warp_picker.CONTROL, warp_control)
    ctx.add_component_definition(native_warp_picker.CONTROL + "Off",
                                 style.apply_control_state(warp_control, skins_dir, active=False))
    definitions.add(native_warp_picker.CONTROL)

    def add(field, gates, abc, power_gates=()):
        slot, kind, rect = field["slot"], field["kind"], field["rect"]
        if abc and field["id"] in tuple(_ui_style.size('screen.osc_settings.add.size')):  # OCT and FIN live in the native TUNE card
            return
        limit = OSC_ABC_SETTINGS_FIELDS if abc else OSC_SETTINGS_FIELDS
        if not 0 <= slot < limit or kind not in ("dial_uni", "dial_bi", "toggle", "menu", "number", "source", "table", "action"):
            raise ValueError(f"Unexpected OSC settings field: {field}")
        side = kind.startswith("dial_") and rect[3] < 94
        definition = f"oscNativeSetting_{kind}{'_side' if side else ''}"
        choices = field.get("choices", [])
        drawer = None
        warp = kind == "menu" and field["key"] in ("osc1.warp.type", "osc1.warp2.type")
        if warp:
            definition, drawer = native_warp_picker.CONTROL, native_warp_picker.DRAWER
        elif kind == "menu" and 2 <= len(choices) <= 9:
            definition += "_" + field["label"]
            drawer = definition + "Choices"
        if kind not in ("table", "action"):
            if kind == "toggle":
                definition += "_" + field["label"]
            definition += f"_{rect[2]}x{rect[3]}"
        definition_off = f"{definition}Off"
        if definition not in definitions:
            ctrl_def = _control(kind, side, skins_dir, tuple(rect[2:]), field["label"])
            if drawer:
                if drawer not in drawers:
                    overlay = native_warp_picker.warp_picker(choices, skins_dir) if warp else labelled_choice_drawer(drawer, choices)
                    ctx.add_component_definition(drawer, overlay)
                    drawers.add(drawer)
                for action in ctrl_def["actions"]:
                    action["additionalData"] = drawer
            ctx.add_component_definition(definition, ctrl_def)
            ctx.add_component_definition(
                definition_off,
                style.apply_control_state(ctrl_def, skins_dir, active=False),
            )
            definitions.add(definition)
        data = f"OscAbc{slot}" if abc else f"Parameter {base + OSC_SETTINGS + slot}"
        caption = field["label"]
        caption_handle = f"WithName[{caption}]/{data}"
        if abc:
            caption_handle = f"OscCaption{slot} {caption}"
            abc_captions[caption_handle] = (slot, caption)
        remap = [("Data", data), ("Caption", caption_handle)]
        if abc:
            abc_cells.append(
                _entry(field["key"] or field["label"], definition, {"version": 1, "handleName": "Data"}, rect, remap, gates, kind != "table")
            )
            abc_cells_off.append(
                _entry(f"{field['key'] or field['label']} Off", definition_off, {"version": 1, "handleName": "Data"}, rect, remap, gates, False)
            )
        else:
            power_on_gate, power_off_gate = power_gates
            cells.append(
                _entry(field["key"] or field["label"], definition, {"version": 1, "handleName": "Data"}, rect, remap, [*gates, power_on_gate], kind != "table")
            )
            cells.append(
                _entry(f"{field['key'] or field['label']} Off", definition_off, {"version": 1, "handleName": "Data"}, rect, remap, [*gates, power_off_gate], False)
            )
        if kind in ("menu", "toggle") and field.get("choices"):
            targets = [f"Parameter {base + OSC_ABC_SETTINGS + source * OSC_ABC_SETTINGS_FIELDS + slot}" for source in range(3)] if abc else [data]
            for target in targets:
                choices = menus.setdefault(target, field["choices"])
                if not choices or choices != field["choices"]:
                    raise ValueError(f"OSC choices mismatch on handle {target}")

    for variant in variants:
        layout = variant["layout"]
        if not 0 < layout < OSC_SETTINGS_LAYOUTS:
            raise ValueError(f"Unexpected OSC settings layout: {layout}")
        gate = f"IndexedEnabling/{layout}/{OSC_SETTINGS_LAYOUTS}/{view}"
        if "warp" not in variant:
            power_idx = base + OSC_POWER + (0 if layout == 1 else 4)
            power_gates = (
                f"IndexedEnabling/1/2/Parameter {power_idx}",
                f"IndexedEnabling/0/2/Parameter {power_idx}",
            )
            for field in variant["fields"]:
                add(field, [gate], False, power_gates)
            continue
        mask = variant["warp"]
        variants_by_layout[layout].add(mask)
        for field in variant["fields"]:
            signature = json.dumps(field, sort_keys=True)
            _, by_layout = geometry.setdefault(signature, (field, defaultdict(set)))
            by_layout[layout].add(mask)

    for field, by_layout in geometry.values():
        layouts_for_warp = defaultdict(set)
        for layout, masks in by_layout.items():
            if masks == variants_by_layout[layout]:
                layouts_for_warp[None].add(layout)
            else:
                for mask in masks:
                    layouts_for_warp[mask].add(layout)
        for mask, layouts in layouts_for_warp.items():
            series = sorted(layouts)
            first = last = series[0]
            # Emit one cell for each contiguous range with the same geometry and Warp requirement.
            for layout in [*series[1:], OSC_SETTINGS_LAYOUTS + 1]:
                if layout == last + 1:
                    last = layout
                    continue
                gate = (f"IndexedEnabling/{first}/{OSC_SETTINGS_LAYOUTS}/{view}" if first == last
                        else f"RangedEnabling/{first}/{last}/{OSC_SETTINGS_LAYOUTS}/{view}")
                gates = [gate] if mask is None else [gate, f"IndexedEnabling/{mask}/4/{warp_layout}"]
                add(field, gates, True)
                first = last = layout

    if abc_cells:
        ctx.add_component_definition(ABC_LAYER, passive_layer(abc_cells))
        ctx.add_component_definition(f"{ABC_LAYER}Off", passive_layer(abc_cells_off))
        for i, label in enumerate(("Osc A", "Osc B", "Osc C")):
            source_idx = i + 1
            power_idx = base + OSC_POWER + source_idx
            remaps = [("Data", view), *((f"OscAbc{slot}", f"Parameter {base + OSC_ABC_SETTINGS + i * OSC_ABC_SETTINGS_FIELDS + slot}")
                                         for slot in range(OSC_ABC_SETTINGS_FIELDS))]
            for key, (slot, caption) in abc_captions.items():
                param = base + OSC_ABC_SETTINGS + i * OSC_ABC_SETTINGS_FIELDS + slot
                remaps.append((key, f"WithName[{caption}]/Parameter {param}"))
            cells.append(_entry(
                label, ABC_LAYER, {"version": 1, "handleName": "Data"}, tuple(_ui_style.size('screen.osc_settings.apply.rect')), remaps,
                [f"IndexedEnabling/{source_idx}/5/Parameter {base + OSC_SOURCE}",
                 f"IndexedEnabling/1/2/Parameter {power_idx}"],
                focus="Children",
            ))
            cells.append(_entry(
                f"{label} Off", f"{ABC_LAYER}Off", {"version": 1, "handleName": "Data"}, tuple(_ui_style.size('screen.osc_settings.apply.rect')), remaps,
                [f"IndexedEnabling/{source_idx}/5/Parameter {base + OSC_SOURCE}",
                 f"IndexedEnabling/0/2/Parameter {power_idx}"],
                focus=False,
            ))

    ctx.add_component_definition(LAYER, passive_layer(cells))
    tui["pageData"].setdefault("replacementStrings", []).extend(
        {"key": key, "value": choices} for key, choices in menus.items()
    )
    for tab in tabs:
        tab["componentDefinition"]["componentsData"].append(
            _entry("Osc Native Settings", LAYER, {"version": 1, "handleName": "Data"}, tuple(_ui_style.size('screen.osc_settings.apply.rect')),
                   [("Data", view)], [f"NonZero/{view}"])
        )
