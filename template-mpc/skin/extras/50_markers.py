# MpcKit Demo - skin extra: the MARKERS of the three tabs, the instance picture, the hiding of the host widgets while the plugin's page is up, and the Q-Link mapping to the Q proxies.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run by scripts/apply-mpc-skin-extras.py --extras-dir template-mpc/skin/extras (tools/gen_skin.py does it). It REUSES the helpers of SynthyBoy's modules (scripts/mpc-skin-extras/50_filter_overlay.py:
# the PNG writers, the marker and instance image components; 83_pages.py: the RangedEnabling that hides a widget) and adds nothing of its own but the parameter numbers of THIS plugin:
#     parameter list = [ params.json entries ] [ "Overlay Instance" ] [ "Q 1" .. "Q 16" ]     (template-mpc/src/demo_plugin.cpp)
# so "Overlay Instance" is ctx.param_count and "Q 1" is ctx.param_count + 1.

import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent / "overlay_layout.json"
SCRIPTS = HERE.parent.parent.parent / "scripts" / "mpc-skin-extras"
Q_PROXIES = 16
KEEP = ("Overlay Marker", "Overlay Instance")   # the components the plugin finds the page by: never hidden


def _load(name):
    spec = importlib.util.spec_from_file_location("mpckit_" + name.replace(".py", ""), SCRIPTS / name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def apply(tui, qlinks, ctx):
    overlay = _load("50_filter_overlay.py")
    pages = _load("83_pages.py")
    layout = json.loads(LAYOUT_JSON.read_text(encoding="utf-8"))
    overlay_param = ctx.param_count            # "Overlay Instance"
    first_q = overlay_param + 1                # "Q 1"
    for view in layout["views"]:
        tab = ctx.tab(view["tab"])
        items = tab["componentDefinition"]["componentsData"]
        overlay._add_markers(view, ctx, items)
        overlay._add_instance(view, ctx, items, layout, overlay_param)
        for c in items:
            if not c["componentData"]["name"].startswith(KEEP):
                pages._hide_while_page_is_up(c, overlay_param)
        links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        for i in range(Q_PROXIES):
            links["Q-Link %d" % (i + 1)] = first_q + i
        entry = [m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1]
        if len(entry) != 1:
            raise ValueError("Q-Links.json has %d entries for the tab %r" % (len(entry), view["tab"]))
        entry[0]["Q-Links"] = links
        entry[0]["Bank Direction"] = "Row"
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], pages.QLINK_BOUNDS_Y, b.split()[2], pages.QLINK_BOUNDS_H) for b in tab["qlinkBoundsData"]]
