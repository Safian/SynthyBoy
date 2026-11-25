# SynthyBoy - MPC skin extra: NO HOST FALLBACK in the OVERLAY skin variant (owner decision, 2026-11-21: the release ships the overlay skin only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay". What it does to every tab of such a skin, after the modules that make the host widgets (10_env .. 95_subtabs) and before 98_tile.py / 99_backgrounds.py:
#   - REMOVES every componentsData entry of the tab except the components the overlay needs from the skin: the marker squares ("Overlay Marker ...") and the instance picture ("Overlay Instance ...") by which the
#     plugin finds its page. The three compact Play selectors are deliberate exceptions, as are the ENV ADSR dials whose C++ page provides transparent holes/touch pass-through.
#     Other fallback controls (faders, meters, graphs and unrelated knobs) are gone: the plugin draws those parts.
#   - what STAYS besides those: the Q proxies "Q 1" .. "Q 16" (parameters, NOT widgets: Q-Links.json maps the hardware knobs to them, 82_play.py / 83_pages.py), the
#     hide rule's handle (nothing is left to hide, the rule is harmless), the optional host background (99_backgrounds.py, manifest empty by default: it runs after this module and adds its Image to the tabs it lists).
#   - SETS qlinkBoundsData to [] on every tab (owner photo, XL, 2026-11-21: the host drew its own Q-Link highlight, the frames of the old fallback grid with numbered badges 3 / 7 / 4 / 8, from the 4 frames
#     of 90_frames.py INSIDE the plugin page's Env and LFO graphs while a knob was turned). "Use [] to draw no highlight" (ui-reference.md 3.6, the key is required, [] is valid). The plugin page draws its own Q frames and badges.
#   - drops the component definitions that no remaining component uses (the Play selector's named slider definition and menu overlay are retained) and the PNG files of the skin folder that TUI.json no
#     longer refers to (the kit film strips, the LFO pictures and cursor strip, the spectrum pictures, the Play background and panel pictures): they would be unpacked into the host's picture cache at every start.

import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_SCRIPTS = _HERE.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from native_voice.voice_tab import VOICE_NATIVE_CONTROL_NAMES
from native_master.master_controls import MASTER_CONTROL_NAMES

KEEP = ("Overlay Marker", "Overlay Instance")   # the components the plugin finds its page by (50_filter_overlay.py)

# The page implementations carve transparent holes for exactly these host
# controls. Keep the list narrow so an unrelated fallback cannot become a
# second input/drawing owner behind the overlay.
NATIVE = {
    "Filter": {"Filter 1 Power", "Filter 2 Power"},
    "Mix": {"Mixer F1 Dest", "Mixer F2 Dest", "Mixer Bus 1 Bypass", "Mixer Bus 1 Bypass name",
            "Mixer Bus 1 Dest", "Mixer Bus 1 Level", "Mixer Bus 1 Level name", "Mixer Bus 2 Bypass",
            "Mixer Bus 2 Bypass name", "Mixer Bus 2 Dest", "Mixer Bus 2 Level", "Mixer Bus 2 Level name",
            "Mixer Main Bypass", "Mixer Main Bypass name"},
}


def _keep_component(tab, component):
    """Keep the three compact Play selectors as host-owned controls.

    The Play graph is still painted by the overlay, but these selectors are
    intentionally named component instances of the SDK's slider definition.
    They must remain in the final TUI so the normal compact page has a real
    host hit target; 82_play.py adds the frame-24 range gate that removes that
    target atomically while a full-screen/modal overlay is open.
    """
    name = component["componentData"]["name"]
    if name.startswith(KEEP):
        return True
    if tab.get("tabName") == "Macros":
        return False  # 99_ui_screen_macros installs the eight complete MacroCards.
    if tab.get("tabName") == "Env" and name.startswith("Env Native "):
        return True
    if tab.get("tabName") == "LFO" and name.startswith("LFO 1 Native "):
        return True
    if tab.get("tabName") == "Mix" and name.startswith("Mixer Native "):
        return True
    if tab.get("tabName") == "Master":
        return name in MASTER_CONTROL_NAMES
    if tab.get("tabName") == "Mod":
        return name.startswith("Mod Native ") or name in ("Mod Matrix Background", "Mod Detail Background")
    if tab.get("tabName") == "Voice":
        return name in VOICE_NATIVE_CONTROL_NAMES
    # Full-tab candidate generators use stable module prefixes and private
    # native widget types.  Keep those entries after 85_names has rewritten
    # the tab labels; exact-name tables alone lose them on the release TUI.
    native_generated = name.startswith(("Master ", "Mod ", "Macro ", "Macros ", "FX ", "Mixer ", "Osc Native ")) \
            or name.endswith((" Power", " Smooth Native")) \
            or component.get("componentData", {}).get("type", "").startswith(("masterNative", "voiceNative", "modNative", "fxNative", "oscNative"))
    if native_generated:
        component.setdefault("bounds", {})["additionalInvalidatingHandles"] = [
            h for h in component.get("bounds", {}).get("additionalInvalidatingHandles", []) if not h.startswith("RangedEnabling/")]
        return True
    if tab.get("tabName") == "Play" and name.startswith("Play Native EnvLfo "):
        return True
    if name in NATIVE.get(tab.get("tabName"), set()):
        return True
    return tab.get("tabName") == "Play" and (
        name.startswith("Play Native OSC ") or
        name in {"Play Osc Select", "Play Env Select", "Play LFO Select", "Play Env Attack",
                 "Play Env Decay", "Play Env Sustain", "Play Env Release", "Play LFO Rise",
                 "Play LFO Smooth", "Play LFO Smooth Off", "Play LFO Smooth Switch"})


def _used_definitions(tui):
    """Names of the component definitions the tabs use (directly or through another definition: its componentsData types and the popups it opens by name)."""
    defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    used, todo = set(), []

    def scan(node):
        if isinstance(node, dict):
            name = node.get("type") if "componentData" not in node else node["componentData"].get("type")
            if isinstance(name, str) and name in defs and name not in used:
                used.add(name)
                todo.append(name)
            extra = node.get("additionalData")
            if isinstance(extra, str) and extra in defs and extra not in used:
                used.add(extra)
                todo.append(extra)
            for v in node.values():
                scan(v)
        elif isinstance(node, list):
            for v in node:
                scan(v)

    for tab in tui["pageData"]["tabs"]:
        scan(tab["componentDefinition"])
    while todo:
        scan(defs[todo.pop()])
    return used


def _png_names(node, out):
    if isinstance(node, dict):
        for v in node.values():
            _png_names(v, out)
    elif isinstance(node, list):
        for v in node:
            _png_names(v, out)
    elif isinstance(node, str) and node.lower().endswith(".png"):
        out.add(node)


def _normalize_local_instances(tui):
    local_keys = {d["key"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
    # A local-component instance exposes only the generic Data handle.  Its
    # child-specific caption/buttonStyle/Value fields belong in the local
    # definition; leaving them on the instance violates SDK 4.4 and can make
    # the host reject the complete TUI.
    for tab in tui["pageData"]["tabs"]:
        for component in tab["componentDefinition"]["componentsData"]:
            data = component.get("componentData", {})
            if data.get("type") in local_keys:
                data["data"] = {"version": 1, "handleName": "Data"}


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    for tab in tui["pageData"]["tabs"]:
        items = tab["componentDefinition"]["componentsData"]
        kept = [c for c in items if _keep_component(tab, c)]
        # The marker/instance images are the plugin's page-discovery and
        # overlay-presence sentinels.  Native controls may occupy their
        # transparent aperture, but must never paint over those six pixels;
        # retain them last so the host's component painter puts them on top.
        sentinels = [c for c in kept if c["componentData"]["name"].startswith(KEEP)]
        controls = [c for c in kept if not c["componentData"]["name"].startswith(KEEP)]
        items[:] = controls + sentinels
        tab["qlinkBoundsData"] = []
    _normalize_local_instances(tui)
    used = _used_definitions(tui)
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    defs[:] = [d for d in defs if d["key"] in used]
    referenced = set()
    _png_names(tui, referenced)
    for png in sorted(Path(ctx.skins_dir).glob("*.png")):
        if png.name not in referenced:
            png.unlink()
