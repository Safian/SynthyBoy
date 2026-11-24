# SynthyBoy - MPC skin extra: native FX screen contract assurance and hook order verification.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Ensures native FX screen components are in place without duplicating definitions.

The FX screen (tab FX) uses the standalone native chain bar, header, and parameter
cell definitions according to the live release overlay skin contract.
The primary widget injection is performed earlier in the autoload pipeline by
20_fx.py. This module validates the hook discovery order, verifies that the
native editor surface and its required definitions are present, and applies
them if not yet registered, avoiding any duplicate definition clashes.
"""

from pathlib import Path


def apply(tui: dict, qlinks: dict, ctx) -> None:
    """Validate or apply the native FX screen controls without duplicating definitions."""
    try:
        tab = ctx.tab("FX")
    except Exception:
        return

    # In non-overlay variant, 20_fx.py has already set the 16 aperture controls.
    # In overlay variant, 20_fx.py applies _apply_native_editor.
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    defs = tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])
    def_keys = {d.get("key") for d in defs}

    # Verify whether 20_fx.py already registered the native editor surface.
    if "fxNativeEditorSurface" not in def_keys:
        # If 20_fx.py did not run earlier in the sequence, apply the native editor here.
        here = Path(__file__).resolve().parent
        fx_module_path = here / "20_fx.py"
        if fx_module_path.is_file():
            import importlib.util
            spec = importlib.util.spec_from_file_location("mpc_skin_extra_20_fx", fx_module_path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            if hasattr(mod, "_apply_native_editor"):
                mod._apply_native_editor(tui, tab, ctx)
        return

    # When already registered by 20_fx.py, ensure no duplicate registration occurs,
    # and verify that the tab contains the native editor surface.
    components = tab.get("componentDefinition", {}).get("componentsData", [])
    has_surface = any(
        c.get("componentData", {}).get("name") == "FX Native Surface"
        or c.get("componentData", {}).get("type") == "fxNativeEditorSurface"
        for c in components
    )
    if not has_surface:
        # Ensure the surface component entry is linked to the tab
        base = ctx.param_count + 64  # standard native param offset
        from native_ui_params import FX_CHAIN, mixer_peek_index
        base = mixer_peek_index(ctx.param_count)
        surface_entry = {
            "version": 2,
            "componentData": {
                "version": 1,
                "name": "FX Native Surface",
                "type": "fxNativeEditorSurface",
                "data": {"version": 1, "handleName": "Data"},
            },
            "handle remapping": {
                "version": 1,
                "map": [{"key": "Data", "value": f"Parameter {base + FX_CHAIN}"}],
            },
            "bounds": {
                "version": 1,
                "acceptsHWFocus": "Children",
                "showWhenDataModelInvalid": "Hide",
                "whenVisible": "Always",
                "boundsType": "Absolute",
                "bounds": "0 0 1280 628",
            },
        }
        components.append(surface_entry)
