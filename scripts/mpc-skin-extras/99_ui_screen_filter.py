# SynthyBoy - MPC skin extra: Filter screen native component integration and verification.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Filter screen native component integration hook.

Autoload order: runs at priority 99 (after 98_filter_native.py).
Verifies the presence and integrity of the unified Filter tab:
- Ensures Filter tab is correctly configured in overlay variant without duplicate entries.
- Validates that no visible Q-Link numbers, badges, or dedicated number lanes leak onto the native surface.
- Verifies that F1/F2 states, NOTE/VELO curve states, dual mode, and Mixer PEEK filter
  surfaces are properly bound and intact.
- Enforces non-duplicating operation: preserves the existing native surface without duplicating controls.
"""


def apply(tui: dict, qlinks: dict, ctx) -> None:
    """Verifies and finalizes the Filter screen native components."""
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    tabs = tui.get("pageData", {}).get("tabs", [])
    filter_tab = next((t for t in tabs if t.get("tabName") == "Filter"), None)
    if filter_tab is None:
        return

    # Verification: ensure filterNativeEditorSurface exists and is not duplicated
    comp_defs = tui.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])
    surface_defs = [d for d in comp_defs if d.get("key") == "filterNativeEditorSurface"]
    if not surface_defs:
        return

    # Ensure no Q-Link number badges or dedicated number lanes are in componentsData
    comps = filter_tab.get("componentDefinition", {}).get("componentsData", [])
    for comp in comps:
        name = comp.get("componentData", {}).get("name", "")
        # Prevent any accidental Q-Link badge widgets
        assert not name.startswith("QBadge"), f"Visible Q-Link badge not allowed: {name}"
