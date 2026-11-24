# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native compact Play ENV and LFO widgets from the C++ layout contract."""

import copy
import json
import sys
from pathlib import Path

from PIL import Image

_ROOT = str(Path(__file__).resolve().parents[2])
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from components.native.active_text.active_text import (
    active_text_child,
    write_active_text_assets,
)
from components.native.level.level import (
    PLAY_SMOOTH_RECTS,
    build_native_smooth_dial_with_indicator_definition,
    synthyboy_dial_data,
)
from components.native.selector.selector import build_shape_selector, selector_definition
from components.native.radio_button.radio_button import create_segmented_radio_button_group
from components.style import argb as _style_argb, opacity as _style_opacity, size as _style_size, apply_control_state
from native_ui_params import PLAY_ENV_LFO, PLAY_LFO_STANDARD_SYNC, mixer_peek_index

ENV_EXPAND, LFO_EXPAND, SHAPE, RATE, DIVISION, STYLE, SYNC, STEREO, CHOOSE, STANDARD_SYNC = range(10)
POPUPS = {
    CHOOSE: (88, "459be9"), SHAPE: (100, "aee33c"), DIVISION: (112, "9659df"),
    STYLE: (124, "f0bc77"), SYNC: (136, "6ef2a0"),
}
LABELS = ("ENV Expand", "LFO Expand", "SHAPE", "RATE", "DIVISION", "STYLE", "SYNC", "STEREO", "Select", "SYNC")


def _entry(name, kind, data, rect, bindings=(), gates=(), focus=True):
    bounds = {
        "version": 2 if gates else 1, "acceptsHWFocus": "Yes" if focus else "No",
        "showWhenDataModelInvalid": "Hide" if bindings else "Show",
        "whenVisible": "Always", "boundsType": "Absolute", "bounds": " ".join(map(str, rect)),
    }
    if gates:
        bounds["additionalInvalidatingHandles"] = list(gates)
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": kind, "data": data},
        "handle remapping": {"version": 1, "map": [{"key": k, "value": v} for k, v in bindings]},
        "bounds": bounds,
    }


def _blue_dial_definition(skins_dir, width, height, frame_size, knob_rect, name_rect, value_rect, *,
                          caption_text=None, caption_height=14.0, value_height=20.0):
    """Build a Play compact dial definition backed by the approved blue LEVEL FilmStrip."""
    bg = {"version": 1, "colour": _style_argb("surface.control"), "image": ""}
    focus = {
        "version": 2,
        "componentData": {
            "version": 1, "name": "focus", "type": "Focus",
            "data": {
                "version": 1,
                "backgroundColour": _style_argb("text.white", _style_opacity("focus")),
                "outlineColour": _style_argb("focus.cyan"),
                "backgroundInset": 4.0,
                "outlineThickness": 1.0,
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
            "whenVisible": "WhenFocussed", "boundsType": "Absolute", "bounds": f"0 0 {width} {height}",
        },
    }
    knob = {
        "version": 2,
        "componentData": {
            "version": 1, "name": "knob", "type": "Knob",
            "data": synthyboy_dial_data(skins_dir, frame_size=frame_size, bipolar=False),
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % knob_rect,
        },
    }
    name_data = {
        "version": 1,
        "textStyle": {
            "version": 1,
            "font": {"version": 1, "name": _style_size("font.family"), "style": _style_size("font.style.semibold"), "height": float(caption_height)},
            "colour": _style_argb("text.caption"),
            "justification": "left verticallyCentred",
            "case": "Original",
        },
        "type": "Text" if caption_text is not None else "Name",
        "handleName": "Data" if caption_text is not None else "Caption",
    }
    if caption_text is not None:
        name_data["text"] = caption_text
    name_comp = {
        "version": 2,
        "componentData": {"version": 1, "name": "name", "type": "Label", "data": name_data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % name_rect,
        },
    }
    value_comp = {
        "version": 2,
        "componentData": {
            "version": 1, "name": "value", "type": "Label",
            "data": {
                "version": 1,
                "textStyle": {
                    "version": 1,
                    "font": {"version": 1, "name": _style_size("font.family"), "style": _style_size("font.style.semibold"), "height": float(value_height)},
                    "colour": _style_argb("text.value"),
                    "justification": "left verticallyCentred",
                    "case": "Original",
                },
                "type": "Value",
                "handleName": "Data",
            },
        },
        "handle remapping": {"version": 1, "map": []},
        "bounds": {
            "version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % value_rect,
        },
    }
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {"version": 1, "focussed": dict(bg, colour=_style_argb("surface.focus")), "unfocussed": bg},
        "ignoreMousePresses": False,
        "disableCoarseDataWheel": False,
        "componentsData": [focus, knob, name_comp, value_comp],
    }


def apply(tui, items, ctx, contract):
    native_base = mixer_peek_index(ctx.param_count)
    base = native_base + PLAY_ENV_LFO
    modal_gate = f"RangedEnabling/0/18/33/Parameter {ctx.param_count + 38}"
    choice_data = {"version": 1, "handleName": "Data"}
    local_defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    definitions = {item["key"]: item["value"] for item in local_defs}
    choices = {p["key"]: list(p.get("choices", [])) for p in ctx.params}
    shapes = choices["lfo1.shape"]
    shapes[-1] = "Drawn"
    domains = {CHOOSE: [f"LFO {i}" for i in range(1, 11)] + [f"RND {i}" for i in range(1, 5)],
               SHAPE: shapes, DIVISION: choices["lfo1.division"], STYLE: choices["rlfo1.style"], SYNC: choices["rlfo1.sync"]}
    replacements = tui["pageData"].setdefault("replacementStrings", [])
    selector = next(item for item in items if item["componentData"]["name"] == "Play LFO Select")
    selector["bounds"].setdefault("additionalInvalidatingHandles", []).append(f"IndexedEnabling/0/7/Parameter {base}")
    selectors = tuple(map(int, selector["bounds"]["bounds"].split()))

    on_img, off_img = write_active_text_assets(
        ctx.skins_dir,
        "SMOOTH",
        width=PLAY_SMOOTH_RECTS["name"][2],
        height=PLAY_SMOOTH_RECTS["name"][3],
    )
    smooth_active_def = build_native_smooth_dial_with_indicator_definition(
        rects=PLAY_SMOOTH_RECTS,
        on_image=on_img,
        off_image=off_img,
        interactive_toggle=True,
        handle_name="Active",
        skins_dir=ctx.skins_dir,
        active=True,
    )
    for comp in smooth_active_def["componentsData"]:
        if comp["componentData"]["name"] == "name":
            comp.update(active_text_child("name", on_img, off_img, PLAY_SMOOTH_RECTS["name"], handle="Active"))

    smooth_passive_def = build_native_smooth_dial_with_indicator_definition(
        rects=PLAY_SMOOTH_RECTS,
        on_image=on_img,
        off_image=off_img,
        interactive_toggle=True,
        handle_name="Active",
        skins_dir=ctx.skins_dir,
        active=False,
    )
    for comp in smooth_passive_def["componentsData"]:
        if comp["componentData"]["name"] == "name":
            comp.update(active_text_child("name", on_img, off_img, PLAY_SMOOTH_RECTS["name"], handle="Active"))

    # Upgrade compact Play ENV/LFO dials from GenericLegacy to the approved blue LEVEL component.
    blue_dials = {
        "compactNativeDial": _blue_dial_definition(
            ctx.skins_dir, 192, 108, 90, (2, 9, 90, 90), (96, 32, 92, 22), (96, 54, 92, 32),
            caption_height=15.0, value_height=22.0,
        ),
        "compactNativeRiseDial": _blue_dial_definition(
            ctx.skins_dir, 196, 72, 60, (2, 6, 60, 60), (68, 12, 124, 22), (68, 34, 124, 28),
            caption_height=14.0, value_height=20.0,
        ),
        "compactNativeSmoothWithIndicator": smooth_active_def,
        "compactNativeSmoothTime": smooth_active_def,
        "compactNativeSmoothReadout": smooth_passive_def,
    }
    for entry in local_defs:
        if entry["key"] in blue_dials:
            entry["value"] = blue_dials[entry["key"]]
            definitions[entry["key"]] = entry["value"]
        elif entry["key"].startswith("NativeInactive "):
            # OSC state bindings are registered before this shared dial upgrade.
            # Keep their inactive state on the exact same final native component.
            original = entry["key"][len("NativeInactive "):]
            if original in blue_dials:
                entry["value"] = apply_control_state(blue_dials[original], ctx.skins_dir, active=False)
                definitions[entry["key"]] = entry["value"]
    for k, v in blue_dials.items():
        if k not in definitions:
            local_defs.append({"key": k, "value": v})
            definitions[k] = v
    local_defs[:] = [e for e in local_defs if e["key"] != "compactNativeSmoothToggle"]
    definitions.pop("compactNativeSmoothToggle", None)

    rate_def = _blue_dial_definition(
        ctx.skins_dir, 182, 72, 60, (2, 6, 60, 60), (68, 12, 110, 22), (68, 34, 110, 28),
        caption_text="RATE", caption_height=14.0, value_height=19.0,
    )
    ctx.add_component_definition("playEnvLfoRate", rate_def)

    kinds = {RATE: "playEnvLfoRate", STEREO: "lfoNativeToggle"}
    for field, (x, rgb) in POPUPS.items():
        caption = LABELS[field]
        menu = "playEnvLfoMenu " + caption
        filename = f"play_env_lfo_menu_{field}.png"
        Image.new("RGB", (6, 6), "#" + rgb).save(ctx.skins_dir / filename)
        popup = copy.deepcopy(definitions["fx editor choice menu"])
        popup["componentsData"].append(_entry("Play ENV LFO Menu Marker", "Image",
                                               {"version": 2, "imageType": "Regular", "colour": "0", "image": filename},
                                               (x, 120, 6, 6), focus=False))
        ctx.add_component_definition(menu, popup)
        if field == SHAPE:
            control = build_shape_selector(
                definitions.get("sliderWithFocusMenu", selector),
                menu,
                (396, 56),
                ctx.skins_dir,
                title="Data",
                caption="SHAPE",
                stepper=True,
                arrows=True,
            )
        elif field == CHOOSE:
            control = selector_definition(
                definitions.get("sliderWithFocusMenu", selector),
                menu,
                selectors[2:],
                ctx.skins_dir,
                title="Data",
                frame_name="Play selector frame",
                filename="play_lfo_selector_frame.png",
                neutral=False,
                arrows=True,
                label_height=24.0,
                justification="horizontallyCentred verticallyCentred",
            )
        else:
            control = copy.deepcopy(definitions["lfoNativeChoice"])
            for action in control["actions"]:
                if action["handler"] == "Show Overlay":
                    action["additionalData"] = menu
        kinds[field] = "playEnvLfoChoice " + caption
        ctx.add_component_definition(kinds[field], control)
        replacements.append({"key": f"Parameter {base + 1 + field}", "value": domains[field]})

    groups = {}
    for view, field, *rect in sorted(json.loads(Path(contract).read_text(encoding="utf-8"))):
        if field == CHOOSE and tuple(rect) != selectors:
            raise ValueError("the compact Play LFO selector geometry must match the C++ hit rect")
        spans = groups.setdefault((field, tuple(rect)), [])
        if spans and spans[-1][1] + 1 == view:
            spans[-1][1] = view
        else:
            spans.append([view, view])
    for (field, rect), spans in groups.items():
        for first, last in spans:
            name = f"Play Native EnvLfo {LABELS[field]} {first}-{last}"
            visible = (f"IndexedEnabling/{first}/7/Parameter {base}" if first == last
                       else f"RangedEnabling/{first}/{last}/7/Parameter {base}")
            handle = f"Parameter {base + 1 + field}"
            if field == STANDARD_SYNC:
                handle = f"Parameter {native_base + PLAY_LFO_STANDARD_SYNC}"
                items.extend(create_segmented_radio_button_group(
                    f"Play Native EnvLfo SYNC {first}-{last}", rect, ("HZ", "BPM", "KEY"),
                    handle, ctx.skins_dir, gates=(modal_gate, visible)))
            elif field in (ENV_EXPAND, LFO_EXPAND):
                data = {"version": 2, "onImage": "lfo_native_expand.png", "offImage": "lfo_native_expand.png",
                        "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}
                items.append(_entry(name, "Button", data, rect, (("Data", "FromGesture/" + handle),), (modal_gate, visible)))
            else:
                items.append(_entry(name, kinds[field], choice_data, rect, (("Data", handle),), (modal_gate, visible)))
