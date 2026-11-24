# SynthyBoy - MPC skin extra: the PAGES the plugin draws entirely, of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json): the tabs Filter 1 / 2, Osc A / B / C / Sub / Noise, Env 1 .. 4, LFO 1 .. 4, Macros, Mixer, Mod, FX, Master and
# Voice (the views of shell-mpc/overlay/overlay_layout.json whose kind is one of the page kinds; the Play tabs are 82_play.py's). Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the production skin stays byte for byte as it is). What it does to such a tab, the same as the Play page does to its own:
#   - the host widgets of the tab (the knobs, the faders, the meters ...) stay where the other modules put them, so that with the overlay off or disabled the tab works as before. Overlay-owned widgets are hidden while
#     the plugin's page is up (RangedEnabling on the display parameter "Overlay Instance": the plugin reports the frame 24 then, 82_play.py); explicitly native controls are exempt and have transparent holes/touch pass-through in their C++ page. The marker squares and
#     the instance picture (50_filter_overlay.py) are never hidden: the plugin finds the page by them.
#   - the hardware knobs of the tab (Q-Link 1 .. 16) are the Q proxies "Q 1" .. "Q 16" (shell-mpc/q_router.h): the page sets what they act on (the active group on Q 1 .. 4, the next groups on 5 .. 16), bank direction "Row"
#     (bank 1 = Q-Link 1 .. 4 = the bottom row of the grid, then 5 .. 8, 9 .. 12, 13 .. 16; the MPC XL has all 16 knobs at once).
# Runs after 82_play.py (the Q proxies are registered by it), before 85_names.py and 90_frames.py.

import json
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent.parent / "shell-mpc" / "overlay" / "overlay_layout.json"
PAGE_KINDS = ("filterui", "lfoui", "envui", "oscui", "macros", "mixer", "fx", "mod", "master", "voice", "page")   # "page": the generic kind of another plugin (template-mpc)
LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS = 4, 32, 2
PLAY_GROUPS, PLAY_SLOTS, Q_PROXIES = 3, 8, 16
# Another plugin uses this module with its own layout: the file from skin-meta ("layout_path") or the environment (MPC_OVERLAY_LAYOUT); the layout's "paramsBefore" and "paramsAfterInstance" (the parameters between
# "Overlay Instance" and "Q 1"; SynthyBoy's default: the Play selectors and proxies) place the parameters (same keys as 50_filter_overlay.py).
FULL_FRAME_LAST_VISIBLE = 18   # the widgets are visible while the "Overlay Instance" frame is 0 .. 18 (the slots use 1 .. 17); the plugin takes frame 24 while a page is up
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 301, 323
KEEP = ("Overlay Marker", "Overlay Instance")   # the names of the components the plugin finds the page by

# These controls deliberately remain host-owned while the page renderer is
# live.  The corresponding C++ pages carve transparent holes and/or pass the
# touch through to the host.  Keep this list explicit: retaining an unrelated
# fallback control would create a second writer behind the overlay.
NATIVE_COMPONENTS = {
    "Env": {"Env 1 Attack", "Env 1 Attack name", "Env 1 Decay", "Env 1 Decay name",
             "Env 1 Sustain", "Env 1 Sustain name", "Env 1 Release", "Env 1 Release name",
             "Env 1 Delay", "Env 1 Delay name", "Env 1 Hold", "Env 1 Hold name",
             "Env 1 Attack Curve", "Env 1 Attack Curve name", "Env 1 Decay Curve", "Env 1 Decay Curve name",
             "Env 1 Release Curve", "Env 1 Release Curve name", "Env 1 Beats", "Env 1 Beats name",
             "Env 1 Retrig", "Env 1 Retrig name", "Env 1 Select", "Env 1 Targets"},
    "Filter": {"Filter 1 Power", "Filter 2 Power"},
    "Mix": {"Mixer F1 Dest", "Mixer F2 Dest", "Mixer Bus 1 Bypass", "Mixer Bus 1 Bypass name",
            "Mixer Bus 1 Dest", "Mixer Bus 1 Level", "Mixer Bus 1 Level name", "Mixer Bus 2 Bypass",
            "Mixer Bus 2 Bypass name", "Mixer Bus 2 Dest", "Mixer Bus 2 Level", "Mixer Bus 2 Level name",
            "Mixer Main Bypass", "Mixer Main Bypass name"},
}


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
    layout = json.loads(Path((ctx.meta or {}).get("layout_path") or os.environ.get("MPC_OVERLAY_LAYOUT") or LAYOUT_JSON).read_text(encoding="utf-8"))
    overlay_param = ctx.param_count + int(layout.get("paramsBefore", LFO_CURSORS + SPECTRUM_COLUMNS + PAD_PRESSURE_PARAMS))
    first_proxy = overlay_param + 1 + int(layout.get("paramsAfterInstance", PLAY_GROUPS + PLAY_GROUPS * PLAY_SLOTS))   # "Q 1": after the Play selectors and their proxies (SynthyBoy)
    q_idx = [first_proxy + i for i in range(Q_PROXIES)]
    for view in layout["views"]:
        if view["kind"] not in PAGE_KINDS:
            continue
        tab = ctx.tab(view["tab"])
        native = NATIVE_COMPONENTS.get(tab["tabName"], set())
        for c in tab["componentDefinition"]["componentsData"]:
            cname = c["componentData"]["name"]
            if tab["tabName"] == "Mod" and (cname.startswith("Mod Native ") or cname in ("Mod Matrix Background", "Mod Detail Background")):
                continue
            ctype = c.get("componentData", {}).get("type", "")
            if cname.startswith("LFO 1 Native "):
                continue  # these native cells have their own visibility handles
            full_native = cname.startswith(("Master ", "Voice ", "Mod ", "Macro ", "Macros ", "FX ", "Mixer ", "Osc Native ")) \
                or cname.endswith((" Power", " Smooth Native")) \
                or ctype.startswith(("masterNative", "voiceNative", "modNative", "fxNative", "oscNative"))
            if full_native:
                handles = c.get("bounds", {}).get("additionalInvalidatingHandles", [])
                c["bounds"]["additionalInvalidatingHandles"] = [h for h in handles if not h.startswith("RangedEnabling/")]
            elif cname not in native and not cname.startswith(KEEP):
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
