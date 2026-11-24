# SynthyBoy - MPC skin extra: Voice screen native component integration and verification.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Production autoload hook for the standalone Voice screen.

Discovered and executed by scripts/apply-mpc-skin-extras.py in alphabetical order.
Runs at index 99 to ensure the Voice tab has all 17 approved native controls
properly integrated, with zero visible Q-Link frames or numbers (Rule 5b),
correct sentinels, and valid SDK parameter mappings.
Safely checks existing definitions from 84_voice.py to prevent duplicate key errors.
"""
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from native_voice.voice_tab import (  # noqa: E402
    KEEP,
    VOICE_NATIVE_CONTROL_NAMES,
    VOICE_NATIVE_CONTROLS,
    VOICE_ROWS,
    VOICE_SEQUENCE_CONTROLS,
    VOICE_STEPS,
    VOICE_TAB_NAME,
    populate_voice_tab,
    sequence_base,
    voice_definitions,
)


def apply(tui, qlinks, ctx):
    """Integrate, verify and finalize host-native Voice screen controls."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return

    tab = next((t for t in tui["pageData"]["tabs"] if t.get("tabName") == VOICE_TAB_NAME), None)
    if tab is None:
        return

    components = tab["componentDefinition"]["componentsData"]
    instance = next((c for c in components if c["componentData"]["name"].startswith("Overlay Instance")), None)
    if instance is None:
        return
    instance_handle = next(h["value"] for h in instance["handle remapping"]["map"] if h["key"] == "Data")
    root_gate = "RangedEnabling/0/18/33/" + instance_handle

    # 1. Safely ensure all native component definitions exist without duplicate definition clash
    defs = tui["pageData"].setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
    existing_keys = {d.get("key") for d in defs}
    for kind, definition in voice_definitions(ctx.skins_dir).items():
        if kind not in existing_keys:
            ctx.add_component_definition(kind, definition)
            existing_keys.add(kind)

    # 2. Ensure replacement strings for mode, rows, and steps
    mode = "voicectl.scale.lfo_mode"
    mode_param = "Parameter %d" % ctx.param_index(mode)
    mode_names = next(p["choices"] for p in ctx.params if p["key"] == mode)
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    names = {
        mode_param: mode_names,
        f"Parameter {sequence_base(ctx)}": list(VOICE_ROWS),
        f"Parameter {sequence_base(ctx) + 1}": list(VOICE_STEPS),
    }
    replacement[:] = [r for r in replacement if r["key"] not in names]
    replacement.extend({"key": key, "value": value} for key, value in names.items())

    # 3. Check if native controls are already populated on the tab
    existing_names = {c["componentData"]["name"] for c in components}
    if not VOICE_NATIVE_CONTROL_NAMES.issubset(existing_names):
        populate_voice_tab(tab, ctx, root_gate)

    # 4. Enforce Rule 5b: NO visible Q-Link frames, badges or numbers on the host skin
    tab["qlinkBoundsData"] = []

    # 5. Verify integrity of converted native apertures and sentinels
    final_components = tab["componentDefinition"]["componentsData"]
    final_names = {c["componentData"]["name"] for c in final_components}
    for name in VOICE_NATIVE_CONTROL_NAMES:
        if name not in final_names:
            raise ValueError(f"Voice screen missing converted native control: {name}")

    sentinel_names = [c["componentData"]["name"] for c in final_components if c["componentData"]["name"].startswith(KEEP)]
    if not sentinel_names:
        raise ValueError("Voice screen missing overlay sentinel components")
