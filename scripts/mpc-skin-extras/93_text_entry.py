# SynthyBoy - native MPC TextEditOverlay component definitions and hooks.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Production autoload hook for native host text entry (TextEditOverlay).

Discovered and executed by scripts/apply-mpc-skin-extras.py in alphabetical order.
Runs at index 93 to ensure the native TextEditOverlay component definition is
registered and available before screens (Macros at 99, LFO at 99) wire into it.

References:
- MPC SDK docs: docs/ui-reference.md section 6.12 (NumericOverlay and TextEditOverlay)
- Official factory definition: /usr/share/Akai/SME0/Resources/Pages/Generic/Generic Text Overlay.json
- Preserved binary/disassembly evidence: docs/dsp-knowledge/native-text-entry.md
"""

from pathlib import Path
import sys

TEXT_EDIT_OVERLAY_NAME = "text edit overlay"


def build_text_edit_overlay_definition():
    """Build the factory-canonical TextEditOverlay component definition.

    Matches Akai factory /usr/share/Akai/SME0/Resources/Pages/Generic/Generic Text Overlay.json
    and SDK ui-reference.md 6.12.
    Uses duplicate 'version' key (represented as 'version\\x001' for apply-mpc-skin-extras loader).
    """
    background = {"version": 1, "colour": "0", "image": ""}
    return {
        "version": 2,
        "actions": [],
        "backgroundData": {
            "version": 1,
            "focussed": background.copy(),
            "unfocussed": background.copy(),
        },
        "ignoreMousePresses": True,
        "disableCoarseDataWheel": False,
        "componentsData": [
            {
                "version": 2,
                "componentData": {
                    "version": 1,
                    "name": "Text Edit Overlay",
                    "type": "TextEditOverlay",
                    "data": {
                        "version": 1,
                        "version\x001": 1,
                        "actions": [
                            {
                                "version": 1,
                                "onAction": "Focus Lost",
                                "handler": "Focus Lost",
                                "handleName": "Focus Lost",
                                "additionalData": "",
                            }
                        ],
                        "withNumbers": False,
                        "handleName": "Data",
                    },
                },
                "handle remapping": {"version": 1, "map": []},
                "bounds": {
                    "version": 1,
                    "acceptsHWFocus": "No",
                    "showWhenDataModelInvalid": "Show",
                    "whenVisible": "Always",
                    "boundsType": "Absolute",
                    "bounds": "0 0 1280 800",
                },
            }
        ],
    }


def text_edit_actions(handle_name="Data", overlay_name=TEXT_EDIT_OVERLAY_NAME):
    """Return the standard host action list to show the text edit overlay.

    Attached to Double Click and Enter Pressed (standard overlay actions).
    """
    return [
        {
            "version": 1,
            "onAction": "Double Click",
            "handler": "Show Overlay",
            "handleName": handle_name,
            "additionalData": overlay_name,
        },
        {
            "version": 1,
            "onAction": "Enter Pressed",
            "handler": "Show Overlay",
            "handleName": handle_name,
            "additionalData": overlay_name,
        },
    ]


def add_text_edit_overlay_definition(tui, ctx=None):
    """Register the text edit overlay definition in localComponentDefinitions if missing."""
    defs = tui.setdefault("pageData", {}).setdefault(
        "componentDefinitions", {}
    ).setdefault("localComponentDefinitions", [])
    if not any(d.get("key") == TEXT_EDIT_OVERLAY_NAME for d in defs):
        comp = build_text_edit_overlay_definition()
        if ctx is not None and hasattr(ctx, "add_component_definition"):
            ctx.add_component_definition(TEXT_EDIT_OVERLAY_NAME, comp)
        else:
            defs.append({"key": TEXT_EDIT_OVERLAY_NAME, "value": comp})


def wire_macros(tui, ctx=None):
    """One-line hook for Macros page: ensures text edit overlay is registered and wired."""
    add_text_edit_overlay_definition(tui, ctx)
    defs = {d["key"]: d["value"] for d in tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])}
    card_def = defs.get("macroCard")
    if card_def is not None and not card_def.get("actions"):
        # Wire Show Overlay to Caption so double-clicking or pressing Enter on the card triggers text edit overlay
        card_def["actions"] = text_edit_actions(handle_name="Caption", overlay_name=TEXT_EDIT_OVERLAY_NAME)


def wire_lfo(tui, ctx=None):
    """One-line hook for LFO page: ensures text edit overlay is registered."""
    add_text_edit_overlay_definition(tui, ctx)


def apply(tui, qlinks, ctx):
    """Production entry point for apply-mpc-skin-extras.py."""
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    add_text_edit_overlay_definition(tui, ctx)
