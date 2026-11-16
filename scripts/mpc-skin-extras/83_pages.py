# SynthyBoy - MPC skin extra: the PAGES the plugin draws entirely, of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json): the tabs Filter 1 / 2, Osc A / B / C / Sub / Noise, Env 1 .. 4, LFO 1 .. 4, Macros, Mixer, Mod, FX, Master and
# Voice (the views of shell-mpc/overlay/overlay_layout.json whose kind is one of the page kinds; the Play tabs are 82_play.py's). Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the production skin stays byte for byte as it is). What it does to such a tab, the same as the Play page does to its own:
#   - the host widgets of the tab (the knobs, the faders, the meters ...) stay where the other modules put them, so that with the overlay off or disabled the tab works as before, but every one of them is hidden while
#     the plugin's page is up (RangedEnabling on the display parameter "Overlay Instance": the plugin reports the frame 24 then, 82_play.py): they cannot take the touch that the plugin's page takes. The marker squares and
#     the instance picture (50_filter_overlay.py) are never hidden: the plugin finds the page by them.
#   - the four hardware knobs of the tab (Q-Link 1 .. 4) are the Q proxies "Q 1" .. "Q 4" (shell-mpc/q_router.h): the page sets what they act on, bank direction "Row" (the first bank, Q-Link 1 .. 4 = the bottom row of the grid).
# Runs after 82_play.py (the Q proxies are registered by it), before 85_names.py and 90_frames.py.

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent.parent / "shell-mpc" / "overlay" / "overlay_layout.json"
PAGE_KINDS = ("filterui", "lfoui", "envui", "oscui", "macros", "mixer", "fx", "mod", "master", "voice")
LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS = 4, 32, 2
PLAY_GROUPS, PLAY_SLOTS, Q_PROXIES = 3, 8, 4
FULL_FRAME_LAST_VISIBLE = 18   # the widgets are visible while the "Overlay Instance" frame is 0 .. 18 (the slots use 1 .. 17); the plugin takes frame 24 while a page is up
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 301, 323
KEEP = ("Overlay Marker", "Overlay Instance")   # the names of the components the plugin finds the page by


def _hide_while_page_is_up(component, overlay_param):
    """The component is hidden (and so is not touchable) while the plugin's page is up: RangedEnabling/0/18/33/Parameter N is valid for the frames 0 .. 18 of the film strip of 33 frames only."""
    bounds = component["bounds"]
    bounds["version"] = 2
    handles = list(bounds.get("additionalInvalidatingHandles", []))
    handle = "RangedEnabling/0/%d/33/Parameter %d" % (FULL_FRAME_LAST_VISIBLE, overlay_param)
    if handle not in handles:
        handles.append(handle)
    bounds["additionalInvalidatingHandles"] = handles


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    layout = json.loads(LAYOUT_JSON.read_text(encoding="utf-8"))
    overlay_param = ctx.param_count + LFO_CURSORS + SPECTRUM_COLUMNS + PAD_PRESSURE_PARAMS
    first_proxy = overlay_param + 1 + PLAY_GROUPS + PLAY_GROUPS * PLAY_SLOTS   # "Q 1": after the Play selectors and their proxies
    q_idx = [first_proxy + i for i in range(Q_PROXIES)]
    for view in layout["views"]:
        if view["kind"] not in PAGE_KINDS:
            continue
        tab = ctx.tab(view["tab"])
        for c in tab["componentDefinition"]["componentsData"]:
            if not c["componentData"]["name"].startswith(KEEP):
                _hide_while_page_is_up(c, overlay_param)
        links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        for i in range(Q_PROXIES):
            links["Q-Link %d" % (i + 1)] = q_idx[i]
        entry = [m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1]
        if len(entry) != 1:
            raise ValueError("Q-Links.json has %d entries for the tab %r" % (len(entry), view["tab"]))
        entry[0]["Q-Links"] = links
        entry[0]["Bank Direction"] = "Row"
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H) for b in tab["qlinkBoundsData"]]
