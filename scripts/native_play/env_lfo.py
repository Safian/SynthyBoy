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
    active_text_definition,
    write_active_text_assets,
)
from components.native.button.button import button
from components.native.level.level import (
    PLAY_SMOOTH_RECTS,
    build_native_smooth_dial_with_indicator_definition,
    blue_dial_definition,
)
from components.native.selector.selector import build_shape_selector, selector_definition
from components.native.radio_button.radio_button import create_segmented_radio_button_group
from components.style import argb as _style_argb, opacity as _style_opacity, size as _style_size, apply_control_state
from native_ui_params import PLAY_ENV_LFO, PLAY_LFO_STANDARD_SYNC, native_base_index

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


def apply(tui, items, ctx, contract):
    native_base = native_base_index(ctx.param_count)
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
        "compactNativeDial": blue_dial_definition(
            ctx.skins_dir, *_style_size('play.env.dial.geometry'),
            caption_height=_style_size('play.font.15'), value_height=_style_size('play.font.22'),
        ),
        "compactNativeRiseDial": blue_dial_definition(
            ctx.skins_dir, *_style_size('play.rise.dial.geometry'),
            caption_height=_style_size('play.font.14'), value_height=_style_size('play.font.20'),
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

    rate_def = blue_dial_definition(
        ctx.skins_dir, *_style_size('play.rate.dial.geometry'),
        caption_text="RATE", caption_height=_style_size('play.font.14'), value_height=_style_size('play.font.19'),
    )
    ctx.add_component_definition("playEnvLfoRate", rate_def)

    cells = json.loads(Path(contract).read_text(encoding="utf-8"))
    stereo_sizes = {(width, height) for _, field, _, _, width, height in cells if field == STEREO}
    stereo_size, = stereo_sizes
    stereo_on, stereo_off = write_active_text_assets(
        ctx.skins_dir, "STEREO", *stereo_size,
        on_name="play_stereo_on.png", off_name="play_stereo_off.png",
    )
    ctx.add_component_definition("playRndStereo", active_text_definition(
        "STEREO", stereo_on, stereo_off, stereo_size, handle="Data"))
    kinds = {RATE: "playEnvLfoRate", STEREO: "playRndStereo"}
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
                tuple(_style_size("play.lfo.selector.size")),
                ctx.skins_dir,
                title="Data",
                caption="SHAPE",
                stepper=False,
                arrows=False,
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
                arrows=False,
                label_height=_style_size("play.font.24"),
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
    for view, field, *rect in sorted(cells):
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
                    handle, ctx.skins_dir, gates=(modal_gate, visible), grid=True))
            elif field in (ENV_EXPAND, LFO_EXPAND):
                items.append(button(name, rect, "lfo_native_expand.png",
                                    remaps=(("Data", "FromGesture/" + handle),), gates=(modal_gate, visible)))
            else:
                items.append(_entry(name, kinds[field], choice_data, rect, (("Data", handle),), (modal_gate, visible)))
