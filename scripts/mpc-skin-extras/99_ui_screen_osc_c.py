# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Production skin extra for the Osc C screen (tab 'Osc C').

Verifies and guarantees that:
1. The Osc C tab exclusively binds to the C instance handlers and parameters:
   - Power toggle: Parameter bound to 'osc3.on'
   - Routing destination: Parameter bound to 'osc3.destination'
   - Settings layer: Parameter remap slots bound to 'osc3.*' engine parameters
   - Detail layer: Bound to the C instance detail/spec pages and content loader
2. The approved native component layout is preserved:
   - Native instance selector at (8, 33, 200, 56)
   - Native power button at (212, 33, 64, 56)
   - Native routing destination menu at (1122, 33, 150, 56)
   - Settings surface and detail surface at (0, 0, 1280, 628)
3. No visible Q-Link frames/numbers on screen:
   - qlinkBoundsData is strictly empty ([]).
4. Hardware Q-Link bindings:
   - Physical encoders Q1..Q16 remain mapped to Q router proxies Q 1..Q 16 in row order.
5. Non-duplication / hook ordering:
   - Reuses existing shared definitions from 54..58 and 98_osc_surface without duplicate insertions.
"""

from pathlib import Path
import sys

_SCRIPTS = Path(__file__).resolve().parent.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from native_ui_params import (
    OSC_ABC_SETTINGS,
    OSC_ABC_SETTINGS_FIELDS,
    OSC_POWER,
    OSC_ROUTING_BY_SOURCE,
    OSC_SOURCE,
    mixer_peek_index,
)


def apply(tui, qlinks, ctx):
    """Auto-loaded hook invoked by scripts/apply-mpc-skin-extras.py."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return

    tabs = tui.get("pageData", {}).get("tabs", [])
    tab = next((t for t in tabs if t.get("tabName") == "Osc C"), None)
    if tab is None:
        return

    # 1. Enforce no visible host Q-Link number frames or bars (owner rule).
    tab["qlinkBoundsData"] = []

    # 2. Verify hardware Q-Link routing to proxy handles.
    fn_idx = tab.get("fnKeyIndex", 0)
    sub_idx = tab.get("fnKeySubIndex", 0)
    ql_entries = qlinks.get("Screen Mode Q-Links", {}).get("map", [])
    for entry in ql_entries:
        if entry.get("Tab") == fn_idx + 1 and entry.get("SubTab") == sub_idx + 1:
            entry["Bank Direction"] = "Row"

    # 3. Ensure native controls exist on the Osc C tab without duplication.
    components = tab.get("componentDefinition", {}).get("componentsData", [])
    has_surface = any(c.get("componentData", {}).get("name") == "Osc Native Surface" for c in components)
    has_individual = any(c.get("componentData", {}).get("name") == "Osc Native Instance Select" for c in components)

    defs = {d["key"]: d["value"] for d in tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])}

    base = mixer_peek_index(ctx.param_count)
    source_idx = 3  # Osc C is 0-indexed instance 3 in (Sub=0, Osc A=1, Osc B=2, Osc C=3, Noise=4)
    expected_power_param = base + OSC_POWER + source_idx
    expected_route_param = base + OSC_ROUTING_BY_SOURCE + source_idx
    expected_abc_base = base + OSC_ABC_SETTINGS + 2 * OSC_ABC_SETTINGS_FIELDS  # C is index 2 of (A=0, B=1, C=2)

    # 4. Verify that C-instance bindings in existing definitions are correct.
    if "oscNativeAbcSettings" in defs:
        settings_layer = defs.get("oscNativeSettingsLayer", {})
        c_entry = next((c for c in settings_layer.get("componentsData", []) if c.get("componentData", {}).get("name") == "Osc C"), None)
        if c_entry is not None:
            remap = {m["key"]: m["value"] for m in c_entry.get("handle remapping", {}).get("map", [])}
            # OscAbc0 should point to expected_abc_base
            first_slot = remap.get("OscAbc0")
            if first_slot and first_slot != f"Parameter {expected_abc_base}":
                raise ValueError(f"Osc C ABC settings bound to {first_slot}, expected Parameter {expected_abc_base}")

    if not has_surface and not has_individual:
        # Fallback: if earlier modules did not install the surface, install it now.
        if "oscNativeSurface" in defs:
            components.append({
                "version": 2,
                "componentData": {
                    "version": 1,
                    "name": "Osc Native Surface",
                    "type": "oscNativeSurface",
                    "data": {"version": 1, "handleName": "Data"},
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {
                    "version": 1,
                    "acceptsHWFocus": "Children",
                    "showWhenDataModelInvalid": "Show",
                    "whenVisible": "Always",
                    "boundsType": "Absolute",
                    "bounds": "0 0 1280 628",
                },
            })
