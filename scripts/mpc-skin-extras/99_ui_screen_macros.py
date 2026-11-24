# SynthyBoy - native MPC Macro cards and their target-navigation handles.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from components.native.macro_card.macro_card import (
    CARD_HEIGHT, CARD_WIDTH, MACRO_CARD_POSITIONS, build_macro_card_definitions, build_macro_card_instance,
)
from native_ui_params import MACRO_TARGET, mixer_peek_index


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    tab = next((t for t in tui["pageData"]["tabs"] if t["tabName"] == "Macros"), None)
    if tab is None:
        return
    components = tab["componentDefinition"]["componentsData"]
    markers = [c for c in components if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
    modal = [c for c in components if c["componentData"]["name"] == "Native Modulation Editor"]
    instance = next(c for c in markers if c["componentData"]["name"] == "Overlay Instance macros")
    root_gate = "RangedEnabling/0/18/33/" + instance["handle remapping"]["map"][0]["value"]
    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    card_defs = build_macro_card_definitions(
        ctx.skins_dir, action_text="TARGETS", dial_template=defs["knobUni"], include_name=False
    )
    for key, val in card_defs.items():
        if key not in defs:
            ctx.add_component_definition(key, val)

    first_target = mixer_peek_index(ctx.param_count) + MACRO_TARGET
    cards = []
    # All 8 compact MacroCards in one bottom row across 1280x628 (Bank 1: 0..3, Bank 2: 4..7)
    for i, (x, y) in enumerate(MACRO_CARD_POSITIONS):
        caption = f"Macro {i + 1}"
        cards.append(build_macro_card_instance(
            caption + " Card", ctx.param_index(f"macro.{i + 1}"),
            (x, y, CARD_WIDTH, CARD_HEIGHT),
            target_handle=f"FromGesture/Parameter {first_target + i}", caption=caption, root_gate=root_gate,
        ))
    components[:] = modal + cards + markers
