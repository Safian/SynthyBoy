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
CANVAS_ASSET = ""   # no bitmap: every tab has a solid page colour (PAGE_COLOUR)
PAGE_COLOUR = "ff090d19"
OSC_SENTINEL = (900, 8, "0f253f")

KNOWN = {
    "osca": ("Osc A", CANVAS_ASSET, *OSC_SENTINEL),
    "oscb": ("Osc B", CANVAS_ASSET, *OSC_SENTINEL),
    "oscc": ("Osc C", CANVAS_ASSET, *OSC_SENTINEL),
    "oscsub": ("Sub", CANVAS_ASSET, *OSC_SENTINEL),
    "oscnoise": ("Noise", CANVAS_ASSET, *OSC_SENTINEL),
    "env1": ("Env", CANVAS_ASSET, 900, 2, "0b1023"),
    "env2": ("Env 2", CANVAS_ASSET, 900, 2, "0b1023"),
    "env3": ("Env 3", CANVAS_ASSET, 900, 2, "0b1023"),
    "env4": ("Env 4", CANVAS_ASSET, 900, 2, "0b1023"),
    "env5": ("Env 5", CANVAS_ASSET, 900, 2, "0b1023"),
    "env6": ("Env 6", CANVAS_ASSET, 900, 2, "0b1023"),
    "lfo1": ("LFO", CANVAS_ASSET, 900, 2, "0b102d"),   # the 14 hardware sub-tabs LFO, LFO 2 .. LFO 10, RND 1 .. RND 4 (one sentinel colour)
    "lfo2": ("LFO 2", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo3": ("LFO 3", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo4": ("LFO 4", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo5": ("LFO 5", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo6": ("LFO 6", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo7": ("LFO 7", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo8": ("LFO 8", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo9": ("LFO 9", CANVAS_ASSET, 900, 2, "0b102d"),
    "lfo10": ("LFO 10", CANVAS_ASSET, 900, 2, "0b102d"),
    "rnd1": ("RND 1", CANVAS_ASSET, 900, 2, "0b102d"),
    "rnd2": ("RND 2", CANVAS_ASSET, 900, 2, "0b102d"),
    "rnd3": ("RND 3", CANVAS_ASSET, 900, 2, "0b102d"),
    "rnd4": ("RND 4", CANVAS_ASSET, 900, 2, "0b102d"),
    "mod": ("Mod", CANVAS_ASSET, 900, 2, "0b2017"),
    "filter1": ("Filter", CANVAS_ASSET, 900, 2, "171c35"),
    "filter2": ("Filter 2", CANVAS_ASSET, 900, 2, "171c35"),
    "filternote": ("Note", CANVAS_ASSET, 900, 2, "171c35"),
    "filtervelo": ("Velo", CANVAS_ASSET, 900, 2, "171c35"),
    # Master: in the title strip of a block panel right of its title, on both sides (the colour is close to the panel fill); Voice: in the gutter between RANDOM and SCALING
    # (close to the canvas). No control lies there (master_layout.json, voice_layout.json).
    "master": ("Master", CANVAS_ASSET, 900, 2, "24142d"),
    "tuning": ("Tuning", CANVAS_ASSET, 900, 2, "24142d"),   # the TUNING & MPE side: the title strip of its SCALE card is wide, the sentinel lies in it like on the Master side
    "voice": ("Voice", CANVAS_ASSET, 900, 2, "1a1010"),
    "mixer": ("Mixer", CANVAS_ASSET, 900, 2, "0f1a4b"),
    "fx": ("FX", CANVAS_ASSET, 900, 2, "0c1038"),
    "macros": ("Macros", CANVAS_ASSET, 900, 2, "0b1c5f"),
    "play1": ("Play", CANVAS_ASSET, 900, 2, "2a3a50"),
}
LFO_TITLE_RECT = (22, 91, 400, 44)   # the top left of the graph panel; shell-mpc/ui/lfo_display.h draws the page title at the same place
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

    for view, tab_name, asset_name in BACKGROUND_TABS:
        tab = ctx.tab(tab_name)
        root = tab["componentDefinition"]
        marker = "sentinel_" + view + ".png"
        if getattr(ctx, "skins_dir", None):
            shutil.copyfile(ASSETS_DIR / marker, Path(ctx.skins_dir) / marker)
        children = root["componentsData"]
        children[:] = [entry for entry in children if not entry["componentData"]["name"].startswith(
            ("Overlay Background Sentinel ", "Native Caption " + view))]
        sentinel = sentinel_entry(view, marker, SENTINELS[view])   # the LAST child: nothing is drawn over the detector pixels
        # LFO captions belong to its layout-gated native hook; the page title of a sub-tab is its own caption (the 14 tabs share one surface, so only
        # the tab knows which LFO or RND it is: "LFO 3", "RND 2"; the plugin's own drawing adds the shape, shell-mpc/ui/lfo_display.h).
        if view.startswith(("lfo", "rnd")):
            from native_static_captions import title_entry
            children.append(title_entry("Native Caption %s title" % view, view[:3].upper() + " " + view[3:], LFO_TITLE_RECT))
        else:
            from native_static_captions import caption_entries
            gates = ()
            if view == "tuning":
                children.extend(caption_entries("tuning_header", prefix="Native Caption tuning header"))
            if view == "master":
                import native_ui_params
                master_view = native_ui_params.native_base_index(ctx.param_count) + native_ui_params.MASTER_VIEW
                children.extend(caption_entries("master_header", prefix="Native Caption master header"))
                # The block titles of both sides are part of their native panels (native_master).
                gates = (f"IndexedEnabling/1/3/Parameter {master_view}",)
            if view == "mod":
                import native_ui_params
                # the title belongs to the matrix page (matrix, board, empty filter): the title line is 0 on the slot page, the targets page and the list (71_mod_texts.py)
                gates = (f"NonZero/Parameter {native_ui_params.native_base_index(ctx.param_count) + native_ui_params.NATIVE_TEXT_MOD_TITLE}",)
            children.extend(caption_entries(view, prefix="Native Caption " + view, gates=gates))
        children.append(sentinel)
        bg = root["backgroundData"]
        for state in ("focussed", "unfocussed"):
            bg[state] = dict(bg[state], colour=PAGE_COLOUR, image="")   # solid colour, no bitmap (owner 2026-11-27)
