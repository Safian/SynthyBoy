# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
# Host-native routing destination, compact pan, and level controls for standalone oscillator tabs.
from pathlib import Path

import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))
_SCRIPTS = _REPO_ROOT / "scripts"
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from components.style import bind_control_active
from components.native.level.level import compact_level_definition
from components.native.pan.pan import compact_pan_definition
from components.native.selector.selector import (
    OSC_COMPACT_LEVEL_COMPONENT_TYPE,
    OSC_COMPACT_PAN_COMPONENT_TYPE,
    OSC_COMPACT_ROUTING_COMPONENT_TYPE,
    OSC_LEVEL_CONTROL_NAME,
    OSC_LEVEL_LAYER,
    OSC_LEVEL_PARAM_KEYS,
    OSC_PAN_CONTROL_NAME,
    OSC_PAN_LAYER,
    OSC_PAN_PARAM_KEYS,
    STANDALONE_COMPACT_OSC_LEVEL_RECT,
    STANDALONE_COMPACT_OSC_PAN_RECT,
    STANDALONE_COMPACT_OSC_ROUTING_RECT,
    build_compact_osc_routing_component,
    OSC_INSTANCE_NAMES,
    OSC_ROUTING_CHOICES,
    OSC_ROUTING_CONTROL_NAME,
    OSC_ROUTING_OVERLAY_NAME,
    OSC_ROUTING_TAB_SPECS,
    build_compact_osc_routing_definition,
    build_standalone_osc_routing_choice_overlay,
    _entry,
    ensure_routing_replacement_strings,
    passive_layer,
    write_routing_assets,
)
from native_ui_params import OSC_ROUTING_BY_SOURCE, OSC_SOURCE, OSC_VISIBILITY, mixer_peek_index


def apply(tui, qlinks, ctx):
    _ = qlinks
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    tabs = [tab for tab in tui["pageData"]["tabs"] if tab.get("tabName") in OSC_ROUTING_TAB_SPECS]
    if not tabs:
        return
    base = mixer_peek_index(ctx.param_count)
    routing_indices = range(base + OSC_ROUTING_BY_SOURCE, base + OSC_ROUTING_BY_SOURCE + len(OSC_INSTANCE_NAMES))
    visibility_idx = base + OSC_VISIBILITY
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    source_def = next((d["value"] for d in defs if d["key"] == "sliderWithFocusMenu"), None)
    if source_def is None:
        raise ValueError("no sliderWithFocusMenu template for standalone OSC routing")

    source_comp = next(
        (
            c
            for t in tui["pageData"]["tabs"]
            for c in t["componentDefinition"]["componentsData"]
            if c.get("componentData", {}).get("type") == "sliderWithFocusMenu"
        ),
        None,
    )

    if not any(d["key"] == OSC_COMPACT_ROUTING_COMPONENT_TYPE for d in defs):
        defs.append({
            "key": OSC_COMPACT_ROUTING_COMPONENT_TYPE,
            "value": build_compact_osc_routing_definition(source_def, ctx.skins_dir),
        })
    if not any(d["key"] == OSC_COMPACT_PAN_COMPONENT_TYPE for d in defs):
        defs.append({
            "key": OSC_COMPACT_PAN_COMPONENT_TYPE,
            "value": compact_pan_definition(None, getattr(ctx, "skins_dir", None)),
        })
    if not any(d["key"] == OSC_COMPACT_LEVEL_COMPONENT_TYPE for d in defs):
        defs.append({
            "key": OSC_COMPACT_LEVEL_COMPONENT_TYPE,
            "value": compact_level_definition(None, getattr(ctx, "skins_dir", None)),
        })
    if not any(d["key"] == OSC_ROUTING_OVERLAY_NAME for d in defs):
        defs.append({
            "key": OSC_ROUTING_OVERLAY_NAME,
            "value": build_standalone_osc_routing_choice_overlay(
                overlay_name=OSC_ROUTING_OVERLAY_NAME,
            ),
        })

    write_routing_assets(ctx.skins_dir)

    ensure_routing_replacement_strings(tui, routing_indices, choices=OSC_ROUTING_CHOICES)

    visibility = f"Parameter {visibility_idx}"

    def show_root_states(layer):
        # Hidden=0, RootOff=1, RootOn=2. OFF keeps the actual readouts and
        # removes the route drawer, dial gestures and focus from the surface.
        layer["bounds"]["additionalInvalidatingHandles"] = [f"NonZero/{visibility}"]
        return bind_control_active(layer, visibility, ctx, defs,
                                   frames=((1, False), (2, True)), frame_count=3)

    dial_layers = []
    for name, layer, kind, keys, rect, caption in (
        (OSC_PAN_CONTROL_NAME, OSC_PAN_LAYER, OSC_COMPACT_PAN_COMPONENT_TYPE, OSC_PAN_PARAM_KEYS, STANDALONE_COMPACT_OSC_PAN_RECT, "PAN"),
        (OSC_LEVEL_CONTROL_NAME, OSC_LEVEL_LAYER, OSC_COMPACT_LEVEL_COMPONENT_TYPE, OSC_LEVEL_PARAM_KEYS, STANDALONE_COMPACT_OSC_LEVEL_RECT, "LEVEL"),
    ):
        children = []
        for source, (label, key) in enumerate(zip(OSC_INSTANCE_NAMES, keys)):
            parameter = f"Parameter {ctx.param_index(key)}"
            children.append(_entry(
                f"{name} {label}", kind, {"version": 1, "handleName": "Data"}, (0, 0, *rect[2:]),
                remaps=[("Data", parameter), ("Caption", f"WithName[{caption}]/{parameter}")],
                gates=[f"IndexedEnabling/{source}/{len(keys)}/Parameter {base + OSC_SOURCE}"],
            ))
        defs.append({"key": layer, "value": passive_layer(children)})
        dial_layers.append(show_root_states(_entry(
            name, layer, {"version": 1, "handleName": "Data"}, rect,
            remaps=[("Data", f"Parameter {visibility_idx}")],
            gates=[f"NonZero/{visibility}"], focus="Children",
        )))

    routing_children = []
    for source, (label, index) in enumerate(zip(OSC_INSTANCE_NAMES, routing_indices)):
        item = build_compact_osc_routing_component(
            dest_param_idx=index,
            power_param_idx=None,
            visibility_param_idx=visibility_idx,
            source_component=source_comp,
        )
        item["componentData"]["name"] = f"{OSC_ROUTING_CONTROL_NAME} {label}"
        _, _, width, height = item["bounds"]["bounds"].split()
        item["bounds"]["bounds"] = f"0 0 {width} {height}"
        item["bounds"]["additionalInvalidatingHandles"] = [
            f"IndexedEnabling/{source}/{len(OSC_INSTANCE_NAMES)}/Parameter {base + OSC_SOURCE}"
        ]
        routing_children.append(item)
    defs.append({"key": "oscNativeRoutingLayer", "value": passive_layer(routing_children)})

    for tab in tabs:
        routing_layer_item = build_compact_osc_routing_component(
            dest_param_idx=visibility_idx,
            power_param_idx=None,
            visibility_param_idx=visibility_idx,
            source_component=source_comp,
        )
        routing_layer_item["componentData"]["type"] = "oscNativeRoutingLayer"
        routing_layer_item["bounds"]["acceptsHWFocus"] = "No"

        tab["componentDefinition"]["componentsData"].extend([
            *dial_layers,
            show_root_states(routing_layer_item),
        ])
