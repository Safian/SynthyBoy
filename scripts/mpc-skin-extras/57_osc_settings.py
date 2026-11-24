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
from PIL import Image, ImageDraw

from components import style
from components.native.level.level import synthyboy_dial_data as _level_dial_data
from components.native.pan.pan import synthyboy_dial_data as _pan_dial_data
from components.native.selector.selector import OSC_TAB_NAMES, passive_layer, save_png_filter0 as _save_png_filter0
from native_ui_params import (
    OSC_ABC_SETTINGS, OSC_ABC_SETTINGS_FIELDS, OSC_SETTINGS, OSC_SETTINGS_FIELDS, OSC_SETTINGS_LAYOUTS,
    OSC_SETTINGS_VIEW, OSC_SOURCE, OSC_WARP_LAYOUT, mixer_peek_index,
)

CONTRACT = Path(__file__).with_name("osc-native-settings.json")
LAYER = "oscNativeSettingsLayer"
CHOICE_MENU = "fx editor choice menu"
NUMBER_MENU = "fx editor number menu"
TABLE_ART = ("osc_native_table_prev.png", "osc_native_table_hit.png", "osc_native_table_next.png")
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
                "name": "Titillium Web",
                "style": "SemiBold",
                "height": height if height is not None else (14 if caption else 18),
            },
            "justification": align,
            "case": "Original",
        },
    }
    if text is not None:
        data["type"] = "Text"
        data["text"] = text
        data["handleName"] = "Invalid"
    return _entry(kind.lower(), "Label", data, rect, relative=True)


def _definition(children, handler=None, popup="", transparent=False):
    bg = {"version": 1, "colour": "0" if transparent else style.argb("button.surface"), "image": ""}
    focused_bg = bg if transparent else dict(bg, colour=style.argb("field.focus"))
    actions = [{"version": 1, "onAction": trigger, "handler": handler, "handleName": "Data", "additionalData": popup}
               for trigger in ("Mouse Down", "Enter Pressed")] if handler else []
    return {"version": 2, "actions": actions,
            "backgroundData": {"version": 1, "focussed": focused_bg, "unfocussed": bg},
            "ignoreMousePresses": transparent, "disableCoarseDataWheel": False, "componentsData": children}


def _ensure_field_box_art(skins_dir):
    box_file = "osc_native_field_box.png"
    box_path = skins_dir / box_file
    if not box_path.is_file():
        w, h = 160, 56
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle(
            (1, 1, w - 2, h - 2),
            radius=style.size("radius.field"),
            fill=style.color("field.surface"),
            outline=style.color("field.border"),
            width=2,
        )
        _save_png_filter0(im, box_path)
    return _entry("box", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": box_file},
                  (0.08, 0.46, 0.84, 0.42), relative=True)


def _ensure_toggle_led_art(skins_dir):
    on_file, off_file = "osc_native_toggle_on.png", "osc_native_toggle_off.png"
    for filename, color_token in ((on_file, "accent.cyan"), (off_file, "tune.track.off")):
        path = skins_dir / filename
        if not path.is_file():
            sz = 32
            im = Image.new("RGBA", (sz, sz), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            d.ellipse((6, 6, sz - 7, sz - 7), fill=style.color(color_token))
            _save_png_filter0(im.resize((16, 16), Image.Resampling.LANCZOS), path)
    return _entry("led", "Indicator", {
        "version": 1, "onImage": on_file, "offImage": off_file,
        "indicatorId": 1, "numIndicatorsInGroup": 1, "handleName": "Data",
    }, (0.13, 0.58, 0.11, 0.18), relative=True)


def _control(kind, side, skins_dir=None):
    if kind.startswith("dial_"):
        # Keep the dial's hit rectangle >=56 px in each generated cell.
        rect = (0.04, 0.07, 0.35, 0.86) if side else (0.16, 0.03, 0.68, 0.60)
        is_bi = "bi" in kind
        knob_data = _pan_dial_data(skins_dir, frame_size=90, bipolar=True) if is_bi else _level_dial_data(
            skins_dir, frame_size=90, bipolar=False
        )
        knob = _entry("knob", "Knob", knob_data, rect, focus=True, relative=True)
        name, value = ((0.42, 0.08, 0.55, 0.42), (0.42, 0.50, 0.55, 0.42)) if side else (
            (0.02, 0.64, 0.96, 0.16), (0.02, 0.80, 0.96, 0.18))
        return _definition([knob, _label("Name", name), _label("Value", value)])

    if kind == "table":
        parts = []
        for i, (art, x, width) in enumerate(zip(TABLE_ART, (0, 0.14, 0.86), (0.14, 0.72, 0.14)), 1):
            parts.append(_entry(f"Source action {i}", "Button", {
                "version": 2, "onImage": art, "offImage": art, "buttonId": i, "numButtonsInGroup": 4,
                "handleName": "Data", "gestureBehaviour": "Instant",
            }, (x, 0.05, width, 0.90), focus=True, relative=True))
        return _definition([
            *parts,
            _label("Text", (0.16, 0.06, 0.68, 0.32), text="SOURCE", height=12),
            _label("Value", (0.16, 0.38, 0.68, 0.56), height=17),
        ])
    if kind == "source":
        return _definition([
            _label("Text", (0.04, 0.06, 0.92, 0.32), text="SOURCE", height=12),
            _label("Value", (0.04, 0.38, 0.92, 0.56), height=17),
        ], "Toggle Switch")
    if kind == "action":
        return _definition([_label("Name", (0.04, 0.04, 0.92, 0.92), height=13)], "Toggle Switch")
    if kind == "toggle":
        kids = [_label("Name", (0.04, 0.06, 0.92, 0.36))]
        if skins_dir is not None:
            kids.append(_ensure_field_box_art(skins_dir))
            kids.append(_ensure_toggle_led_art(skins_dir))
            kids.append(_label("Value", (0.26, 0.46, 0.62, 0.42), height=16))
        else:
            kids.append(_label("Value", (0.04, 0.48, 0.92, 0.42), height=16))
        return _definition(kids, "Toggle Switch")
    if kind == "number":
        kids = [_label("Name", (0.04, 0.06, 0.92, 0.36))]
        if skins_dir is not None:
            kids.append(_ensure_field_box_art(skins_dir))
        kids.append(_label("Value", (0.10, 0.46, 0.80, 0.42), height=18))
        return _definition(kids, "Show Overlay", NUMBER_MENU)
    children = [_label("Name", (0.02, 0.06, 0.96, 0.38)), _label("Value", (0.02, 0.48, 0.86, 0.44))]
    if kind == "menu" and skins_dir is not None:
        chevron_file = "osc_native_menu_chevron.png"
        chevron_path = skins_dir / chevron_file
        if not chevron_path.is_file():
            im_ch = Image.new("RGBA", (16 * 4, 16 * 4), (0, 0, 0, 0))
            d_ch = ImageDraw.Draw(im_ch)
            d_ch.line(((3 * 4, 6 * 4), (8 * 4, 11 * 4), (13 * 4, 6 * 4)), fill=style.color("text.muted"), width=2 * 4)
            _save_png_filter0(im_ch.resize((16, 16), Image.Resampling.LANCZOS), chevron_path)
        chevron = _entry(
            "chevron",
            "Image",
            {"version": 2, "imageType": "Regular", "colour": "0", "image": chevron_file},
            (0.90, 0.58, 0.06, 0.28),
            relative=True,
        )
        return _definition([*children, chevron], "Show Overlay", CHOICE_MENU)
    return _definition(children, "Show Overlay", CHOICE_MENU if kind == "menu" else NUMBER_MENU)


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
    definitions, menus, cells, abc_cells = set(), {}, [], []
    variants_by_layout = defaultdict(set)
    geometry = {}

    def add(field, gates, abc):
        slot, kind, rect = field["slot"], field["kind"], field["rect"]
        if abc and field["id"] in (110, 112):  # OCT and FIN live in the native TUNE card
            return
        limit = OSC_ABC_SETTINGS_FIELDS if abc else OSC_SETTINGS_FIELDS
        if not 0 <= slot < limit or kind not in ("dial_uni", "dial_bi", "toggle", "menu", "number", "source", "table", "action"):
            raise ValueError(f"Unexpected OSC settings field: {field}")
        side = kind.startswith("dial_") and rect[3] < 94
        definition = f"oscNativeSetting_{kind}{'_side' if side else ''}"
        if definition not in definitions:
            ctx.add_component_definition(definition, _control(kind, side, getattr(ctx, "skins_dir", None)))
            definitions.add(definition)
        data = f"OscAbc{slot}" if abc else f"Parameter {base + OSC_SETTINGS + slot}"
        remap = [("Data", data), ("Caption", f"WithName[{field['label']}]/{data}")]
        (abc_cells if abc else cells).append(
            _entry(field["key"] or field["label"], definition, {"version": 1, "handleName": "Data"}, rect, remap, gates, kind != "table")
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
            for field in variant["fields"]:
                add(field, [gate], False)
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
        for i, label in enumerate(("Osc A", "Osc B", "Osc C")):
            remaps = [("Data", view), *((f"OscAbc{slot}", f"Parameter {base + OSC_ABC_SETTINGS + i * OSC_ABC_SETTINGS_FIELDS + slot}")
                                         for slot in range(OSC_ABC_SETTINGS_FIELDS))]
            cells.append(_entry(label, ABC_LAYER, {"version": 1, "handleName": "Data"}, (0, 0, 1280, 628), remaps,
                                [f"IndexedEnabling/{i + 1}/5/Parameter {base + OSC_SOURCE}"]))

    if "oscNativeSetting_table" in definitions:
        for i, filename in enumerate(TABLE_ART):
            art = Image.new("RGBA", (56, 56))
            if i != 1:
                tip, side = (19, 35) if i == 0 else (37, 21)
                ImageDraw.Draw(art).line((side, 15, tip, 28, side, 41), fill=style.color("text.icon"), width=3)
            art.save(ctx.skins_dir / filename)

    ctx.add_component_definition(LAYER, passive_layer(cells))
    tui["pageData"].setdefault("replacementStrings", []).extend(
        {"key": key, "value": choices} for key, choices in menus.items()
    )
    for tab in tabs:
        tab["componentDefinition"]["componentsData"].append(
            _entry("Osc Native Settings", LAYER, {"version": 1, "handleName": "Data"}, (0, 0, 1280, 628),
                   [("Data", view)], [f"NonZero/{view}"])
        )
