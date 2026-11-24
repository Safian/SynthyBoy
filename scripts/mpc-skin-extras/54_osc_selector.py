# Host-native instance selector for standalone oscillator tabs.
import copy
from pathlib import Path

import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))
_SCRIPTS = _REPO_ROOT / "scripts"
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_POWER_PARAM_KEYS,
    OSC_SOURCE_OVERLAY_NAME,
    STANDALONE_OSC_SELECTOR_RECT,
    build_standalone_osc_selector_definition,
    build_standalone_osc_source_overlay,
    sanitize_osc_tab_navigation,
)
from native_ui_params import OSC_SOURCE, OSC_VISIBILITY, mixer_peek_index


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tabs = [tab for tab in tui["pageData"]["tabs"] if tab.get("tabName") in {"Osc", *OSC_INSTANCE_NAMES}]
    if not tabs:
        return
    base = mixer_peek_index(ctx.param_count)
    selector_idx = base + OSC_SOURCE
    state_idx = base + OSC_VISIBILITY
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    source_def = next((d["value"] for d in defs if d["key"] == "sliderWithFocusMenu"), None)
    source = next((c for t in tui["pageData"]["tabs"] for c in t["componentDefinition"]["componentsData"]
                   if c["componentData"]["type"] == "sliderWithFocusMenu"), None)
    if source_def is None:
        raise ValueError("no sliderWithFocusMenu template for standalone OSC selector")
    key = "oscNativeSelector"
    if not any(d["key"] == key for d in defs):
        defs.append({
            "key": key,
            "value": build_standalone_osc_selector_definition(
                source_def,
                component_name=key,
                overlay_name=OSC_SOURCE_OVERLAY_NAME,
                skins_dir=getattr(ctx, "skins_dir", None),
            ),
        })
    power_params = {k: ctx.param_index(k) for k in OSC_POWER_PARAM_KEYS}
    if not any(d["key"] == OSC_SOURCE_OVERLAY_NAME for d in defs):
        defs.append({"key": OSC_SOURCE_OVERLAY_NAME, "value": build_standalone_osc_source_overlay(power_params.values())})
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    pkey = "Parameter %d" % selector_idx
    entry = next((r for r in replacement if r.get("key") == pkey), None)
    choices = list(OSC_INSTANCE_NAMES)
    if entry is None:
        replacement.append({"key": pkey, "value": choices})
    else:
        entry["value"] = choices

    for tab in tabs:
        items = tab["componentDefinition"]["componentsData"]
        if not any(c["componentData"]["name"] == "Osc Native Instance Select" for c in items):
            if source is not None:
                c = copy.deepcopy(source)
            else:
                c = {
                    "version": 2,
                    "componentData": {"version": 1, "name": "Osc Native Instance Select", "type": key, "data": {"version": 1, "handleName": "Data"}},
                    "handle remapping": {"version": 1, "map": []},
                    "bounds": {"version": 1, "acceptsHWFocus": "Yes", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "8 33 200 56", "additionalInvalidatingHandles": []}
                }
            c["componentData"]["name"] = "Osc Native Instance Select"
            c["componentData"]["type"] = key
            c["handle remapping"]["map"] = [{"key": "Data", "value": pkey}]
            c["bounds"]["bounds"] = "%d %d %d %d" % STANDALONE_OSC_SELECTOR_RECT
            c["bounds"]["version"] = 2
            c["bounds"]["showWhenDataModelInvalid"] = "Hide"
            c["bounds"]["whenVisible"] = "Always"
            c["bounds"]["additionalInvalidatingHandles"] = [f"NonZero/Parameter {state_idx}"]
            items.append(c)
        sanitize_osc_tab_navigation(tab, power_params)
