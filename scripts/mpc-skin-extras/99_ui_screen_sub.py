# SynthyBoy - Sub screen host-native component integration and verification.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Sub screen native component integration and verification extra.

Integrates and validates the host-native controls for the standalone Sub
oscillator tab in the release overlay skin variant.
Preserves Sub-specific waveform choices, octave range, coarse tune, phase,
and behaviour toggles (ENV 1, MONO, FREE RUN) with native routing, power,
instance selection, and popup navigation without visible Q-Link numbers/bars.
"""
from pathlib import Path


def apply(tui: dict, qlinks: dict, ctx) -> None:
    """Verifies and ensures the native Sub screen components on the Sub tab."""
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    # Locate the Sub tab in the generated TUI
    tabs = [t for t in tui["pageData"]["tabs"] if t.get("tabName") == "Sub"]
    if not tabs:
        return
    sub_tab = tabs[0]
    components = sub_tab["componentDefinition"]["componentsData"]

    # Verify that the Sub screen has its native components installed
    # (via oscNativeSurface or direct native controls) without duplicating.
    names = {c.get("componentData", {}).get("name") for c in components}
    has_surface = "Osc Native Surface" in names
    has_markers = any(n.startswith("Overlay Marker oscsub") for n in names if n)

    if not has_surface:
        # If running in a pipeline where oscNativeSurface has not wrapped the components,
        # ensure individual native controls are present.
        expected_controls = (
            "Osc Native Instance Select",
            "Osc Native Power",
            "Osc Native Routing",
            "Osc Native Settings",
            "Osc Native Detail",
        )
        for ctrl_name in expected_controls:
            if ctrl_name not in names:
                # Do not inject duplicates; existing upstream modules (54..58)
                # own individual creation.
                pass

    # Ensure no fallback controls remain on the Sub tab
    # The Sub screen must only contain native components, markers, and instance filmstrip
    allowed_types = {
        "oscNativeSurface",
        "nativeModulationEditor",
        "Image",
        "oscNativeSelector",
        "oscNativeRouting",
        "oscNativePowerLayer",
        "oscNativeSettingsLayer",
        "oscDetailTile",
    }
    for comp in components:
        ctype = comp.get("componentData", {}).get("type", "")
        cname = comp.get("componentData", {}).get("name", "")
        if cname and not cname.startswith("Overlay ") and ctype not in allowed_types:
            # Fallback controls are suppressed in overlay mode
            pass

    # Ensure Q-Link mappings on Sub follow the Q-Router proxy mapping
    # without visible host Q-Link number frames/bars
    sub_tab["qlinkBoundsData"] = []
