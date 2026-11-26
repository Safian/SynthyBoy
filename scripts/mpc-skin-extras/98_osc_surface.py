# SynthyBoy - share the standalone oscillator controls after the release filter.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import OSC_TAB_NAMES, passive_layer

CONTROL_NAMES = (
    "Osc Native Pan",
    "Osc Native Level",
    "Osc Native Routing",
    "Osc Native Power",
    "Osc Native Settings",
    "Osc Native Detail",
)


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    surfaces = []
    for tab in tui["pageData"]["tabs"]:
        if tab["tabName"] not in (*OSC_TAB_NAMES, "Osc"):
            continue
        components = tab["componentDefinition"]["componentsData"]
        positions = [i for i, child in enumerate(components) if child["componentData"]["name"] in CONTROL_NAMES]
        names = tuple(components[i]["componentData"]["name"] for i in positions)
        if names != CONTROL_NAMES or positions != list(range(positions[0], positions[-1] + 1)):
            raise ValueError(f"Unexpected standalone OSC controls on {tab['tabName']}")
        surfaces.append((components, slice(positions[0], positions[-1] + 1)))
    if not surfaces:
        return
    first, span = surfaces[0]
    controls = first[span]
    ctx.add_component_definition("oscNativeSurface", passive_layer(controls))
    for components, span in surfaces:
        if components[span] != controls:
            raise ValueError("Standalone OSC tabs have different native surfaces")
        components[span] = [{"version": 2, "componentData": {"version": 1, "name": "Osc Native Surface",
                             "type": "oscNativeSurface", "data": {"version": 1, "handleName": "Data"}},
                             "handle remapping": {"version": 1, "map": []},
                             "bounds": {"version": 1, "acceptsHWFocus": "Children", "showWhenDataModelInvalid": "Show",
                                        "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1280 628"}}]
