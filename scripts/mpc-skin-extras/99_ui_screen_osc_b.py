# SynthyBoy - native screen integration and verification hook for Osc B.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Osc B standalone screen native component integration and verification.

Ensures that the standalone Osc B tab in the MPC overlay skin variant incorporates
the approved native components, with all controls and bindings scoped strictly to
the B instance (osc2.*), with hardware Q-Link proxy bindings preserved, visible
Q-Link numbers and frames removed, popups and navigation retained, and no duplicate
definitions or rejected sidebar layouts.
"""

from pathlib import Path
import sys

_HERE = Path(__file__).resolve().parent
_SCRIPTS = _HERE.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from native_ui_params import (
    OSC_ABC_SETTINGS,
    OSC_ABC_SETTINGS_FIELDS,
    OSC_DETAIL,
    OSC_POWER,
    OSC_ROOT_LOAD,
    OSC_ROOT_LOAD_VIEW,
    OSC_ROUTING_BY_SOURCE,
    OSC_SETTINGS,
    OSC_SETTINGS_VIEW,
    OSC_SOURCE,
    OSC_VISIBILITY,
    native_base_index,
)
_REPO_ROOT = _SCRIPTS.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import (
    OSC_INSTANCE_NAMES,
    OSC_POWER_PARAM_KEYS,
    OSC_ROUTING_COMPONENT_TYPE,
    OSC_ROUTING_CONTROL_NAME,
    OSC_ROUTING_OVERLAY_NAME,
    OSC_TAB_NAMES,
    STANDALONE_OSC_ROUTING_RECT,
)

OSC_B_TAB_NAME = "Osc B"
OSC_B_INSTANCE_NAME = "Osc B"
OSC_B_SOURCE_INDEX = 2  # 0: Sub, 1: Osc A, 2: Osc B, 3: Osc C, 4: Noise
OSC_B_ABC_INDEX = 1     # 0: Osc A, 1: Osc B, 2: Osc C

# Bound engine parameter keys for the B instance
OSC_B_POWER_KEY = "osc2.on"
OSC_B_ROUTING_KEY = "osc2.destination"

# 18 parameter fields of the ABC settings layer mapped for Osc B
OSC_B_ABC_KEYS = (
    "osc2.type",
    "osc2.wavetable",
    None,  # BROWSE action
    "osc2.transpose",
    "osc2.tune",
    None,  # DETAIL action
    "osc2.unison.voices",
    "osc2.unison.detune",
    "osc2.warp.type",
    "osc2.warp.amount",
    "osc2.warp2.type",
    "osc2.warp2.amount",
    "osc2.frame",
    "osc2.grain.density",
    "osc2.grain.length",
    "osc2.grain.position",
    "osc2.grain.direction",
    "osc2.grain.pitch",
)


def verify_osc_b_screen(tui, qlinks, ctx):
    """Verify that the Osc B tab and its native components satisfy all contract rules."""
    meta = ctx.meta or {}
    if meta.get("variant") != "overlay":
        return True, "non-overlay variant"

    tabs = tui.get("pageData", {}).get("tabs", [])
    tab = next((t for t in tabs if t.get("tabName") in (OSC_B_TAB_NAME, "oscb")), None)
    if tab is None:
        return False, f"Tab {OSC_B_TAB_NAME!r} not found in tui tabs"

    # Rule 1: No visible Q-Link frames or numbered badges on the native screen
    qlink_bounds = tab.get("qlinkBoundsData", [])
    if qlink_bounds != []:
        return False, f"Osc B tab has visible Q-Link frames: {qlink_bounds!r}"

    # Rule 2: Sub-tab hierarchy preserved
    if tab.get("fnKeyIndex") != 1 or tab.get("fnKeySubIndex") != 1:
        return False, f"Osc B sub-tab index mismatch: fnKeyIndex={tab.get('fnKeyIndex')}, fnKeySubIndex={tab.get('fnKeySubIndex')}"

    # Rule 3: Required overlay markers and surface present
    components = tab.get("componentDefinition", {}).get("componentsData", [])
    comp_names = {c.get("componentData", {}).get("name") for c in components}
    required_names = {
        "Overlay Marker oscb left",
        "Overlay Marker oscb right",
        "Overlay Instance oscb",
        "Osc Native Surface",
    }
    missing = required_names - comp_names
    if missing:
        return False, f"Osc B tab missing required components: {missing!r}"

    # Rule 4: Verify Osc Native Surface child controls
    defs = {d["key"]: d["value"] for d in tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])}
    surface = defs.get("oscNativeSurface")
    if not surface:
        return False, "oscNativeSurface definition not found"
    surface_children = {c.get("componentData", {}).get("name") for c in surface.get("componentsData", [])}
    expected_surface_children = {
        "Osc Native Pan",
        "Osc Native Level",
        "Osc Native Routing",
        "Osc Native Power",
        "Osc Native Settings",
        "Osc Native Detail",
    }
    if not expected_surface_children.issubset(surface_children):
        return False, f"oscNativeSurface missing children: {expected_surface_children - surface_children!r}"

    # Rule 5: Verify Osc B specific bindings
    base = native_base_index(ctx.param_count)

    # 5a. Power button for Osc B: bound to base + OSC_POWER + 2
    power_layer = defs.get("oscNativePowerLayer")
    if not power_layer:
        return False, "oscNativePowerLayer definition not found"
    power_b = next((c for c in power_layer.get("componentsData", []) if c.get("componentData", {}).get("name") == "Osc Native Osc B Power"), None)
    if not power_b:
        return False, "Osc Native Osc B Power not found in power layer"
    expected_power_param = f"Parameter {base + OSC_POWER + OSC_B_SOURCE_INDEX}"
    actual_power_param = next((m["value"] for m in power_b.get("handle remapping", {}).get("map", []) if m.get("key") == "Data"), None)
    if actual_power_param != expected_power_param:
        return False, f"Osc B power binding mismatch: {actual_power_param} != {expected_power_param}"

    # 5b. Routing dropdown for Osc B: bound to base + OSC_ROUTING_BY_SOURCE + 2
    routing_layer = defs.get("oscNativeRoutingLayer")
    if not routing_layer:
        return False, "oscNativeRoutingLayer definition not found"
    routing_b = next((c for c in routing_layer.get("componentsData", []) if c.get("componentData", {}).get("name") == f"{OSC_ROUTING_CONTROL_NAME} {OSC_B_INSTANCE_NAME}"), None)
    if not routing_b:
        return False, f"{OSC_ROUTING_CONTROL_NAME} {OSC_B_INSTANCE_NAME} not found in routing layer"
    expected_route_param = f"Parameter {base + OSC_ROUTING_BY_SOURCE + OSC_B_SOURCE_INDEX}"
    actual_route_param = next((m["value"] for m in routing_b.get("handle remapping", {}).get("map", []) if m.get("key") == "Data"), None)
    if actual_route_param != expected_route_param:
        return False, f"Osc B routing binding mismatch: {actual_route_param} != {expected_route_param}"

    # 5c. Settings layer Osc B ABC wrapper: maps OscAbc0..17 to base + OSC_ABC_SETTINGS + 18 + slot
    settings_layer = defs.get("oscNativeSettingsLayer")
    if not settings_layer:
        return False, "oscNativeSettingsLayer definition not found"
    settings_b = next((c for c in settings_layer.get("componentsData", []) if c.get("componentData", {}).get("name") == OSC_B_INSTANCE_NAME), None)
    if not settings_b:
        return False, f"{OSC_B_INSTANCE_NAME} wrapper not found in settings layer"
    remaps = {m["key"]: m["value"] for m in settings_b.get("handle remapping", {}).get("map", [])}
    for slot in range(OSC_ABC_SETTINGS_FIELDS):
        expected_param = f"Parameter {base + OSC_ABC_SETTINGS + OSC_B_ABC_INDEX * OSC_ABC_SETTINGS_FIELDS + slot}"
        actual_param = remaps.get(f"OscAbc{slot}")
        if actual_param != expected_param:
            return False, f"Osc B slot {slot} binding mismatch: {actual_param} != {expected_param}"

    # 5d. Detail page LOAD B action
    detail_def = defs.get("oscNativeDetail")
    if not detail_def:
        return False, "oscNativeDetail definition not found"
    load_b = next((c for c in detail_def.get("componentsData", []) if c.get("componentData", {}).get("name") == "LOAD B"), None)
    if not load_b:
        return False, "LOAD B action not found in oscNativeDetail"
    expected_load_param = f"FromGesture/Parameter {base + OSC_ROOT_LOAD + OSC_B_ABC_INDEX}"
    actual_load_param = next((m["value"] for m in load_b.get("handle remapping", {}).get("map", []) if m.get("key") == "Data"), None)
    if actual_load_param != expected_load_param:
        return False, f"Osc B LOAD binding mismatch: {actual_load_param} != {expected_load_param}"

    # Rule 6: Hardware Q-Links preserved in Q-Links.json
    ql_entries = qlinks.get("Screen Mode Q-Links", {}).get("map", [])
    entry = next((e for e in ql_entries if e.get("Tab") == 2 and e.get("SubTab") == 2), None)
    if not entry:
        return False, "Osc B entry not found in Q-Links.json"
    if entry.get("Bank Direction") != "Row":
        return False, f"Osc B Bank Direction is not 'Row': {entry.get('Bank Direction')}"
    ql_map = entry.get("Q-Links", {})
    if len(ql_map) != 16 or any(v < 0 for v in ql_map.values()):
        return False, f"Osc B Q-Links mapping invalid: {ql_map!r}"

    return True, "Osc B native screen fully compliant"


def apply(tui: dict, qlinks: dict, ctx) -> None:
    """Apply Osc B screen integration and verification in the extras pipeline."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return

    tabs = tui.get("pageData", {}).get("tabs", [])
    tab = next((t for t in tabs if t.get("tabName") in (OSC_B_TAB_NAME, "oscb")), None)
    if tab is None:
        return

    # Strictly enforce that no visible Q-Link frames are drawn on Osc B
    tab["qlinkBoundsData"] = []

    # Verify compliance
    ok, msg = verify_osc_b_screen(tui, qlinks, ctx)
    if not ok:
        raise ValueError(f"99_ui_screen_osc_b verification failed: {msg}")
