# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Keep the unified Filter tab selectable while either filter is switched off."""
from components.native.button.button import filter_power_param_key, sanitize_tab_navigation


def apply(tui, qlinks, ctx):
    if (ctx.meta or {}).get("variant") != "overlay":
        return
    for tab in tui["pageData"]["tabs"]:
        if tab["tabName"] in ("Filter", "Filter 2", "Note", "Velo"):
            sanitize_tab_navigation(tab, {num: ctx.param_index(filter_power_param_key(num)) for num in (1, 2)})
