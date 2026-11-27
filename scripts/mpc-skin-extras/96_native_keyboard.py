# SynthyBoy - native MPC keyboard component definition and skin hook.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Production autoload hook for native on-screen touch keyboard.

Discovered and executed by scripts/apply-mpc-skin-extras.py in alphabetical order.
Runs at index 96 to ensure the native keyboard overlay component definition is
registered in localComponentDefinitions.

Guarded by SYNTHYBOY_NATIVE_KEYBOARD=1 so the default skin remains byte-identical
until C++ backend parameters (TextEdit Context, TextEdit Key, TextEdit Text) are wired.

References:
- docs/review/TEXT-ENTRY-NATIVE-KEYBOARD-2026-11-27.md
- docs/ui-components/keyboard.md
- components/native/keyboard/keyboard.py
"""

import os
from pathlib import Path
import sys

# Ensure repository root is on sys.path for components.* imports
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from components.native.keyboard.keyboard import (
    NATIVE_KEYBOARD_OVERLAY_NAME,
    build_keyboard_overlay_definition,
)
from native_ui_params import LFO_TARGETS_ACTION, native_base_index

TEXT_EDIT_CONTEXT = LFO_TARGETS_ACTION + 1
TEXT_EDIT_KEY = TEXT_EDIT_CONTEXT + 1
TEXT_EDIT_TEXT = TEXT_EDIT_KEY + 1


def add_native_keyboard_definition(tui, ctx=None):
    """Register the native keyboard overlay definition in localComponentDefinitions."""
    skins_dir = getattr(ctx, "skins_dir", None)

    if ctx is not None and hasattr(ctx, "param_count"):
        base = native_base_index(ctx.param_count)
        ctx_idx = base + TEXT_EDIT_CONTEXT
        key_idx = base + TEXT_EDIT_KEY
        txt_idx = base + TEXT_EDIT_TEXT

        if hasattr(ctx, "add_param_key"):
            try:
                ctx.add_param_key(ctx_idx, "shell.text_edit_context")
                ctx.add_param_key(key_idx, "shell.text_edit_key")
                ctx.add_param_key(txt_idx, "shell.text_edit_text")
            except Exception:
                # Keys may already be registered
                pass

        context_param = f"Parameter {ctx_idx}"
        key_param = f"ResetOnGestureEnd/Parameter {key_idx}"
        text_param = f"Parameter {txt_idx}"
    else:
        context_param = "Parameter 963"
        key_param = "ResetOnGestureEnd/Parameter 964"
        text_param = "Parameter 965"

    comp_def = build_keyboard_overlay_definition(
        skins_dir=skins_dir,
        context_param=context_param,
        key_param=key_param,
        text_param=text_param,
    )

    defs = tui.setdefault("pageData", {}).setdefault(
        "componentDefinitions", {}
    ).setdefault("localComponentDefinitions", [])

    if not any(d.get("key") == NATIVE_KEYBOARD_OVERLAY_NAME for d in defs):
        if ctx is not None and hasattr(ctx, "add_component_definition"):
            ctx.add_component_definition(NATIVE_KEYBOARD_OVERLAY_NAME, comp_def)
        else:
            defs.append({"key": NATIVE_KEYBOARD_OVERLAY_NAME, "value": comp_def})


def apply(tui, qlinks, ctx):
    """Production entry point for apply-mpc-skin-extras.py.

    Guarded behind SYNTHYBOY_NATIVE_KEYBOARD=1 to keep default skin generation
    completely byte-identical to existing baseline.
    """
    if os.environ.get("SYNTHYBOY_NATIVE_KEYBOARD") != "1":
        return
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    add_native_keyboard_definition(tui, ctx)
