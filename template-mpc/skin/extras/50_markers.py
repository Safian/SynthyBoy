# MpcKit Demo - skin extra: the MARKERS of the three tabs, the instance picture, the hiding of the host widgets while the plugin's page is up, and the Q-Link mapping to the Q proxies.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run by scripts/apply-mpc-skin-extras.py --extras-dir template-mpc/skin/extras (tools/gen_skin.py does it). It runs the REAL modules of SynthyBoy's skin (scripts/mpc-skin-extras/50_filter_overlay.py:
# the marker and instance picture components; 83_pages.py: the RangedEnabling that hides a widget, the Q-Link mapping, the Q-Link bounds) on THIS plugin's layout (skin/overlay_layout.json): the modules take the
# layout file from the skin meta ("layout_path") and the parameter numbers from the layout's "paramsBefore" and "paramsAfterInstance" (migration step 9 of docs/ui-components/TEMPLATE.md). The parameter list is
#     parameter list = [ params.json entries ] [ "Overlay Instance" ] [ "Q 1" .. "Q 16" ]     (template-mpc/src/demo_plugin.cpp)
# so "Overlay Instance" is ctx.param_count (paramsBefore 0) and "Q 1" is ctx.param_count + 1 (paramsAfterInstance 0).

import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent / "overlay_layout.json"
SCRIPTS = HERE.parent.parent.parent / "scripts" / "mpc-skin-extras"


def _load(name):
    spec = importlib.util.spec_from_file_location("mpckit_" + name.replace(".py", ""), SCRIPTS / name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def apply(tui, qlinks, ctx):
    # the modules are inactive without the overlay variant in the skin meta: this plugin IS the overlay variant, its meta is the layout file and nothing else
    ctx.meta = {"variant": "overlay", "layout_path": str(LAYOUT_JSON)}
    _load("50_filter_overlay.py").apply(tui, qlinks, ctx)   # the markers and the instance picture of every view
    _load("83_pages.py").apply(tui, qlinks, ctx)            # the widgets hidden while a page is up, the Q-Links on the Q proxies, the Q-Link bounds
