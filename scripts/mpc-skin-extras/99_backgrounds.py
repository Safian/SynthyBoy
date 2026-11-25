# SynthyBoy - MPC skin extra: fixed plain canvas assets for the OVERLAY skin variant.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The manifest selects tabs whose static PNG is the root background in both
# focus states. Markers, instance filmstrips and native controls sit above it.
# Only generated overlay skins use this module.

import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS_DIR = HERE / "overlay-backgrounds"

# The backgrounds the generator can make: view -> (tab name, committed asset, sentinel x, y, RRGGBB). The SENTINEL (tab coordinates, a 5 x 5 square) is the spot where the background has a flat colour the
# host's own picture of the tab (its native widgets on the host's tab colour 1e1d1e, or the tab before the switch) does not have, outside the three marker spots, the host's left-edge handle tab (x 0 .. 9) and the host's
# Q-Link panel (x >= 1020). The plugin reads it from the host's primary framebuffer (shell-mpc/overlay/page_detector.h) and keeps its own plane OPAQUE until it matches (shell-mpc/overlay/host_background.h).
# The detector colour lives in a separate fixed native Image widget; background PNGs are plain canvas.
OSC_ASSET = "overlay_bg_osc.png"
OSC_SENTINEL = (900, 8, "0f253f")

KNOWN = {
    "osca": ("Osc", OSC_ASSET, *OSC_SENTINEL),
    "oscb": ("Osc B", OSC_ASSET, *OSC_SENTINEL),
    "oscc": ("Osc C", OSC_ASSET, *OSC_SENTINEL),
    "oscsub": ("Sub", OSC_ASSET, *OSC_SENTINEL),
    "oscnoise": ("Noise", OSC_ASSET, *OSC_SENTINEL),
    "env1": ("Env", "overlay_bg_env.png", 900, 2, "0b1023"),
    "lfo1": ("LFO", "overlay_bg_lfo.png", 900, 2, "0b1029"),
    "mod": ("Mod", "overlay_bg_mod.png", 900, 20, "0b1022"),
    "filter1": ("Filter", "overlay_bg_filter.png", 900, 95, "171c35"),
    "master": ("Master", "overlay_bg_master.png", 960, 386, "141a38"),
    "voice": ("Voice", "overlay_bg_voice.png", 792, 574, "0c1023"),
    "mixer": ("Mix", "overlay_bg_mixer.png", 900, 18, "141a35"),
    "fx": ("FX", "overlay_bg_fx.png", 900, 2, "0c1032"),
    "macros": ("Macros", "overlay_bg_macros.png", 900, 20, "0b1028"),
    "play1": ("Play", "overlay_bg_play.png", 900, 20, "141a3b"),
}
SENTINELS = {view: (v[2], v[3], v[4]) for view, v in KNOWN.items()}

# The root background stays below the three named Play selector instances;
# their frame-24 gate (82_play.py) hides them only while the plugin owns a
# full-screen/modal view. The skin and plugin sentinel header use the same manifest.
MANIFEST = ASSETS_DIR / "manifest.json"


def load_manifest(path=MANIFEST):
    """[(view_name, tab_name, asset_filename)] of the views the file lists, in the file's order."""
    names = json.loads(Path(path).read_text(encoding="utf-8")).get("tabs", [])
    unknown = [n for n in names if n not in KNOWN]
    if unknown:
        raise ValueError("%s lists views without a background asset: %s (known: %s)" % (path, unknown, sorted(KNOWN)))
    return [(n, KNOWN[n][0], KNOWN[n][1]) for n in names]


BACKGROUND_TABS = load_manifest()


def install_background(source, destination, sentinel):
    """Copy the owner-approved static asset verbatim. Never redraw or patch it."""
    shutil.copyfile(source, destination)


def sentinel_entry(view, filename, sentinel):
    x, y, _ = sentinel
    return {
        "version": 2,
        "componentData": {"version": 1, "name": "Overlay Background Sentinel " + view,
                          "type": "Image", "data": {"version": 2, "imageType": "Regular",
                          "colour": "0", "image": filename}},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
                   "whenVisible": "Always", "boundsType": "Absolute", "bounds": f"{x} {y} 5 5"},
    }


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return

    copied = set()
    for view, tab_name, asset_name in BACKGROUND_TABS:
        asset_src = ASSETS_DIR / asset_name
        if not asset_src.is_file():
            raise FileNotFoundError("Missing committed background asset: %s" % asset_src)

        # Copy asset to Plugin Skins folder
        if asset_name not in copied and hasattr(ctx, "skins_dir") and ctx.skins_dir:
            install_background(asset_src, Path(ctx.skins_dir) / asset_name, SENTINELS[view])
            copied.add(asset_name)

        tab = ctx.tab(tab_name)
        root = tab["componentDefinition"]
        marker = "sentinel_" + view + ".png"
        if getattr(ctx, "skins_dir", None):
            shutil.copyfile(ASSETS_DIR / marker, Path(ctx.skins_dir) / marker)
        children = root["componentsData"]
        children[:] = [entry for entry in children if not entry["componentData"]["name"].startswith(
            ("Overlay Background Sentinel ", "Native Caption " + view))]
        children.append(sentinel_entry(view, marker, SENTINELS[view]))
        # LFO captions belong to its layout-gated native hook.
        if view != "lfo1":
            from native_static_captions import caption_entries
            gates = ()
            if view == "master":
                import native_ui_params
                master_view = native_ui_params.mixer_peek_index(ctx.param_count) + native_ui_params.MASTER_VIEW
                children.extend(caption_entries("master_header", prefix="Native Caption master header"))
                children.extend(caption_entries("master_tuning", prefix="Native Caption master tuning",
                                                gates=(f"IndexedEnabling/2/3/Parameter {master_view}",)))
                gates = (f"IndexedEnabling/1/3/Parameter {master_view}",)
            children.extend(caption_entries(view, prefix="Native Caption " + view, gates=gates))
        bg = root["backgroundData"]
        for state in ("focussed", "unfocussed"):
            bg[state] = dict(bg[state], image=asset_name)
