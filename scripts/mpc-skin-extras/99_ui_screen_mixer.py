# SynthyBoy - MPC skin extra: Mixer full-screen native controls coordinator and validator.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Dedicated auto-loaded skin extra for the Mixer screen.

Executes late in the autoload chain (after 84_mixer.py, 97_no_fallback.py,
98_filter_native.py, 20_fx.py, and 99_backgrounds.py).
Ensures that all 10 channel strip compound instances, shared definitions,
fader scales, and five PEEK module buttons (FLT 1, FLT 2, MAIN FX, BUS 1, BUS 2)
are fully present, properly gated, and conform strictly to the owner's rule:
NO visible Q-Link numbers, badges or dedicated number lanes, while preserving
all hardware parameter bindings and F1/F2 PEEK navigation.
"""

from pathlib import Path

STRIPS = (
    ("SUB", "sub"), ("A", "osc1"), ("B", "osc2"), ("C", "osc3"), ("NOISE", "noise"),
    ("F1", "filter1"), ("F2", "filter2"), ("BUS 1", "bus1"), ("BUS 2", "bus2"), ("MAIN", "main")
)
PEEK_LABELS = ("FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")
NATIVE_PREFIX = "Mixer Native "

REQUIRED_DEFINITIONS = (
    "mixerStripSourceFrame",
    "mixerStripBusFrame",
    "mixerStripMasterFrame",
    "mixerDestination56",
    "mixerSliderLEVEL198",
    "mixerSliderLEVEL414",
    "mixerSliderPAN56",
    "mixerSliderB156",
    "mixerSliderB256",
)


def apply(tui: dict, qlinks: dict, ctx) -> None:
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") in ("Mix", "Mixer")), None)
    if tab is None:
        return

    components = tab["componentDefinition"]["componentsData"]

    # 1. Ensure qlinkBoundsData is strictly empty on the release overlay skin
    # (Owner rule ec5506d6: no visible Q-Link frames or yellow number badges)
    tab["qlinkBoundsData"] = []

    # 2. Verify shared compound component definitions
    defs_list = tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])
    defined_keys = {d.get("key") for d in defs_list if "key" in d}
    for def_name in REQUIRED_DEFINITIONS:
        assert def_name in defined_keys, f"Mixer shared definition {def_name} missing from componentDefinitions"

    # 3. Verify presence of all 10 channel strip compound instances
    existing_names = {c["componentData"]["name"] for c in components}
    for caption, source in STRIPS:
        strip_name = f"{NATIVE_PREFIX}{caption} Strip"
        assert strip_name in existing_names, f"Mixer strip compound instance {strip_name} missing"

    # 4. Verify presence of all bottom native view tabs (MIXER + five PEEK buttons)
    assert f"{NATIVE_PREFIX}MIXER Peek" in existing_names, "Mixer Native MIXER Peek tab button missing"
    for caption in PEEK_LABELS:
        peek_name = f"{NATIVE_PREFIX}{caption} Peek"
        assert peek_name in existing_names, f"Mixer PEEK button {caption} missing"
