# SynthyBoy - MPC skin extra: standalone Filter 1 and Filter 2 native power switches for overlay skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Provides standalone Filter 1 and Filter 2 native power switches with transparent host aperture support."""
import copy
from pathlib import Path

from components.native.button.button import build_filter_power, filter_power_param_key, sanitize_tab_navigation

FILTER_POWER_RECT = (12, 16, 56, 56)


def _default_toggle_template():
    return {
        "version": 2,
        "componentData": {
            "version": 1,
            "name": "Template Power",
            "type": "toggleIcon",
            "data": {
                "version": 1,
                "icon": "Power",
            },
        },
        "handle remapping": {
            "version": 1,
            "map": [
                {"key": "Value", "value": "Parameter 0"},
            ],
        },
        "bounds": {
            "version": 1,
            "acceptsHWFocus": "Yes",
            "showWhenDataModelInvalid": "Show",
            "whenVisible": "Always",
            "boundsType": "Absolute",
            "bounds": "%d %d %d %d" % FILTER_POWER_RECT,
            "additionalInvalidatingHandles": [],
        },
    }


def _clone_component(template, name, parameter_index, rect):
    c = copy.deepcopy(template)
    c["componentData"]["name"] = name
    c["bounds"]["bounds"] = "%d %d %d %d" % rect
    remapping = c.setdefault("handle remapping", {"version": 1, "map": []})
    mapped = False
    for m in remapping.get("map", []):
        if m.get("key") in ("Value", "Data") or m.get("from") in ("Value", "Data"):
            m["value"] = "Parameter %d" % parameter_index
            mapped = True
    if not mapped:
        remapping.setdefault("map", []).append({"key": "Data", "value": "Parameter %d" % parameter_index})
    return c


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    # Find an existing toggle template if present in any tab, else use default
    template = None
    for tab in tui.get("pageData", {}).get("tabs", []):
        for comp in tab.get("componentDefinition", {}).get("componentsData", []):
            if comp.get("componentData", {}).get("type") in ("toggle", "toggleIcon"):
                template = comp
                break
        if template:
            break
    if not template:
        template = _default_toggle_template()

    tabs = tui.get("pageData", {}).get("tabs", [])
    for tab in tabs:
        tname = tab.get("tabName", "")
        comps = tab.setdefault("componentDefinition", {}).setdefault("componentsData", [])

        if tname == "Filter 1":
            p1 = ctx.param_index(filter_power_param_key(1))
            entry1 = build_filter_power(_clone_component, template, 1, p1, FILTER_POWER_RECT, overlay_param=-1)
            entry1["componentData"]["type"] = "toggleIcon"
            comps.append(entry1)
            sanitize_tab_navigation(tab, {1: p1})
        elif tname == "Filter 2":
            p2 = ctx.param_index(filter_power_param_key(2))
            entry2 = build_filter_power(_clone_component, template, 2, p2, FILTER_POWER_RECT, overlay_param=-1)
            entry2["componentData"]["type"] = "toggleIcon"
            comps.append(entry2)
            sanitize_tab_navigation(tab, {2: p2})
        elif tname == "Filter":
            # The unified page ships the state-gated controls in 98_filter_native.py.
            sanitize_tab_navigation(tab, {num: ctx.param_index(filter_power_param_key(num)) for num in (1, 2)})
