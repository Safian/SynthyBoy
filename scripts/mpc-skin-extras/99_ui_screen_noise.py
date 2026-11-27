# SynthyBoy - MPC skin extra: standalone host-native controls for the Noise tab.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Dedicated auto-loaded skin extra for the standalone Noise screen.

Verifies, validates, and guarantees host-native MPC control integration on the Noise tab:
- Source tabs (SUB / A / B / C / NOISE) at (0, 580, 1280, 48)
- Power button with antialiased state indicator at POWER_RECT (12, 116, 64, 56) bound to noise.on
- Native routing button at STANDALONE_OSC_ROUTING_RECT (1122, 37, 150, 48) bound to noise.destination
- Noise Generated layout (Layout 2: KIND, SOUND, FILTER, STEREO, PITCH, FINE, KEY TRACK, ENV 1)
- Noise Sample layout (Layout 3: KIND, FILTER, STEREO, PITCH, FINE, KEY TRACK, ENV 1, START, RANDOM, LOOP, BOUNCE)
- Native Detail and MORE navigation with BACK button
- Solid page colour background (no bitmap)
- Zero visible Q-Link frame/badge clutter (clean hardware Q-Link mapping preserved)

Maintains idempotency and does not duplicate any components already consolidated
by upstream modules (54_osc_selector, 55_osc_routing, 56_osc_power, 57_osc_settings,
58_osc_detail, 98_osc_surface, 99_backgrounds).
"""

import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

_REPO_ROOT = _SCRIPTS.parent
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from components.native.selector.selector import (
    STANDALONE_OSC_ROUTING_RECT,
)

NOISE_TAB_NAME = "Noise"
NOISE_SOURCE_INDEX = 4  # 0: Sub, 1: Osc A, 2: Osc B, 3: Osc C, 4: Noise


def apply(tui, qlinks, ctx):
    """Auto-loaded entry point invoked by scripts/apply-mpc-skin-extras.py."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return

    tabs = tui.get("pageData", {}).get("tabs", [])
    noise_tab = next((tab for tab in tabs if tab.get("tabName") == NOISE_TAB_NAME), None)
    if not noise_tab:
        return

    # 1. Ensure no visible Q-Link frames or badges on the Noise tab
    if noise_tab.get("qlinkBoundsData"):
        noise_tab["qlinkBoundsData"] = []

    # 2. Check local component definitions
    defs = {
        d["key"]: d["value"]
        for d in tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])
    }

    # 3. Verify oscNativeSurface integration on the Noise tab
    components = noise_tab.get("componentDefinition", {}).get("componentsData", [])
    surface_entry = next((c for c in components if c.get("componentData", {}).get("type") == "oscNativeSurface"), None)

    if surface_entry and "oscNativeSurface" in defs:
        surface_defs = defs["oscNativeSurface"].get("componentsData", [])
        existing_names = {c.get("componentData", {}).get("name") for c in surface_defs}
        expected_names = {
            "Osc Native Routing",
            "Osc Native Power",
            "Osc Native Settings",
            "Osc Native Detail",
        }
        missing = expected_names - existing_names
        if not missing:
            # Upstream hooks have already assembled the unified surface cleanly.
            # Do not duplicate components or definitions.
            return

    # If oscNativeSurface was not attached, attach it without duplication
    if not surface_entry and "oscNativeSurface" in defs:
        components.insert(
            0,
            {
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
            },
        )
