# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Host-native TUNE block extra for standalone oscillator tabs (Osc, Osc A, Osc B, Osc C).

Autoloaded by scripts/apply-mpc-skin-extras.py in sorted name order.
Runs after 98_osc_surface.py and installs the approved 278 x 154 px OSC TUNE card
with actual numeric host parameter bindings, transposition route, switched A/B/C
gating via OSC_SOURCE, and existing OFF protections via OSC_SETTINGS_VIEW.
"""

from pathlib import Path
import sys

_SCRIPTS = Path(__file__).resolve().parent.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from components.native.tune.tune import (
    build_standalone_osc_tune,
    build_standalone_osc_tune_definitions,
)
from components.style import bind_control_active
from native_ui_params import (
    OSC_POWER,
    OSC_SETTINGS_VIEW,
    OSC_SOURCE,
    native_base_index,
)

OSC_SOURCES = (
    ("Osc A", 1, "A"),
    ("Osc B", 2, "B"),
    ("Osc C", 3, "C"),
)


def apply(tui, qlinks, ctx):
    """Auto-loaded hook invoked by scripts/apply-mpc-skin-extras.py."""
    if (ctx.meta or {}).get("variant") != "overlay":
        return

    local_defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    surface = next((d["value"] for d in local_defs if d["key"] == "oscNativeSurface"), None)
    if surface is None:
        return
    existing_keys = {d.get("key") for d in local_defs}
    defs = build_standalone_osc_tune_definitions(skins_dir=ctx.skins_dir)
    for name, definition in defs.items():
        if name not in existing_keys:
            ctx.add_component_definition(name, definition)
            existing_keys.add(name)

    base = native_base_index(ctx.param_count)
    view_gate = f"NonZero/Parameter {base + OSC_SETTINGS_VIEW}"
    source_param = f"Parameter {base + OSC_SOURCE}"

    items = surface["componentsData"]
    for src_name, src_idx, src_label in OSC_SOURCES:
        comp_name = f"Osc Native Tune {src_label}"
        if not any(c.get("componentData", {}).get("name") == comp_name for c in items):
            gates = [
                f"IndexedEnabling/{src_idx}/5/{source_param}",
                view_gate,
            ]
            tune_entry = build_standalone_osc_tune(
                src_name,
                base=base,
                name=comp_name,
                gates=gates,
            )
            bind_control_active(tune_entry, f"Parameter {base + OSC_POWER + src_idx}", ctx, local_defs)
            items.append(tune_entry)
