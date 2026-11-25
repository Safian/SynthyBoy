# SynthyBoy - MPC skin extra: standalone native Voice tab controls.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from native_voice.voice_tab import (VOICE_ROWS, VOICE_STEPS, VOICE_TAB_NAME,
                                    populate_voice_tab, sequence_base, voice_definitions)  # noqa: E402


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    tab = next((t for t in tui["pageData"]["tabs"] if t["tabName"] == VOICE_TAB_NAME), None)
    if tab is None:
        return

    components = tab["componentDefinition"]["componentsData"]
    instance = next(c for c in components if c["componentData"]["name"].startswith("Overlay Instance"))
    instance_handle = next(h["value"] for h in instance["handle remapping"]["map"] if h["key"] == "Data")
    # VoiceUi reports frame 24 while the overlay owns a full-screen panel.
    root_gate = "RangedEnabling/0/18/33/" + instance_handle

    for kind, definition in voice_definitions(ctx.skins_dir).items():
        ctx.add_component_definition(kind, definition)

    mode = "voicectl.scale.lfo_mode"
    mode_param = "Parameter %d" % ctx.param_index(mode)
    mode_names = next(p["choices"] for p in ctx.params if p["key"] == mode)
    replacement = tui["pageData"].setdefault("replacementStrings", [])
    names = {mode_param: mode_names,
             f"Parameter {sequence_base(ctx)}": list(VOICE_ROWS),
             f"Parameter {sequence_base(ctx) + 1}": list(VOICE_STEPS)}
    replacement[:] = [r for r in replacement if r["key"] not in names]
    replacement.extend({"key": key, "value": value} for key, value in names.items())

    populate_voice_tab(tab, ctx, root_gate)
    tab["qlinkBoundsData"] = []
    # The shared 83_pages extra already assigns the full sixteen Q proxies.
