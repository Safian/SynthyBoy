# SynthyBoy - the host-native POWER button of the standalone oscillator views.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import sys

SCRIPTS = Path(__file__).resolve().parent.parent
_REPO_ROOT = SCRIPTS.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
from components import style
from components.native.selector.selector import OSC_TAB_NAMES, passive_layer
from components.native.button.button import button, icon_button
from native_ui_params import OSC_POWER, OSC_SOURCE, OSC_VISIBILITY, native_base_index

# OscillatorFullScreen::layout, tab-local coordinates.
POWER_RECT = (12, 8, 106, 164)
POWER_ON, POWER_OFF = "osc_native_power_on.png", "osc_native_power_off.png"


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    tabs = [tab for tab in tui["pageData"]["tabs"] if tab["tabName"] in {"Osc", *OSC_TAB_NAMES}]
    if not tabs:
        return

    for filename, colour in ((POWER_OFF, style.color("accent.red")), (POWER_ON, style.color("accent.green"))):
        icon_button("power", colour).save(ctx.skins_dir / filename)

    base = native_base_index(ctx.param_count)
    view = f"Parameter {base + OSC_VISIBILITY}"
    children = []
    for source, label in enumerate(OSC_TAB_NAMES):
        # A/B/C reserve the TUNE card to the left of the waveform.
        x, y = (42, 0) if source in (1, 2, 3) else (0, 108)
        children.append(button(
            f"Osc Native {label} Power", (x, y, 64, 56), POWER_ON, POWER_OFF,
            remaps=(("Data", f"Parameter {base + OSC_POWER + source}"),),
            gates=(f"IndexedEnabling/{source}/{len(OSC_TAB_NAMES)}/Parameter {base + OSC_SOURCE}",),
        ))
    layer = "oscNativePowerLayer"
    ctx.add_component_definition(layer, passive_layer(children))
    for tab in tabs:
        tab["componentDefinition"]["componentsData"].append({
            "version": 2,
            "componentData": {"version": 1, "name": "Osc Native Power", "type": layer,
                              "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": view}]},
            "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide",
                       "whenVisible": "Always", "boundsType": "Absolute", "bounds": "%d %d %d %d" % POWER_RECT,
                       "additionalInvalidatingHandles": [f"NonZero/{view}"]},
        })
