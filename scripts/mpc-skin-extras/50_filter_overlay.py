# SynthyBoy - MPC skin extra: the "Filter 1" tab of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json), made for the plugin built with SYNTHYBOY_MPC_OVERLAY.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the layout file of that variant sets it; the production layout does not, and this module then changes nothing).
# The overlay draws the live response curve of Filter 1 over the area at the top of the tab (a picture of the skin shows the dark background there and nothing else); the controls
# are the two rows below it, in the lower two rows of the 4 x 4 grid like the tabs with a picture above (Env, Osc, LFO: the Q-Link frames cover those two rows only):
#     row 3:  On, Model, Style, Drive          (Q-Link 5 .. 8)
#     row 4:  Cutoff, Resonance, Mix, Key Tracking      (Q-Link 1 .. 4)
# Cutoff and Resonance are not on the production tab (they are on Macros): the controls are copies of the Drive knob bound to filter1.cutoff / filter1.resonance (ctx.param_index).
# Everything else the production tab has stays reachable: Blend and the six controls of the model / formant page go to the sub-tab "F1 Model" of the same function key
# (the layout file gives it the Kick page filter1_model; Blend takes the place of Key Tracking, which moved to the Filter 1 tab) - 35 tabs in all.
#
# The two marker pictures (overlay_marker_<name>.png, solid squares of the colours in shell-mpc/overlay/overlay_layout.json at the positions given there, drawn 1:1) are what the
# plugin's page detector looks for in the primary framebuffer; the plugin paints the background colour over them, so the player sees only the dark curve area. The numbers are
# in that JSON only (the plugin includes the header scripts/gen-overlay-layout-header.py makes of it).
#
# Runs after 40_spectrum.py and before 80_layout.py, which places the slots, 85_names.py (names: this module adds them to the meta) and 90_frames.py.

import copy
import json
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent.parent / "shell-mpc" / "overlay" / "overlay_layout.json"
COLS = (23, 343, 663, 983)
ROWS = (45, 196, 346, 497)
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 301, 323   # x and width of the frames: 90_frames.py
MAIN_TAB, MODEL_TAB = "Filter 1", "F1 Model"
BACKGROUND_FILE = "overlay_curve_bg.png"


def png_solid(width, height, rgb):
    """An opaque RGB PNG of one colour (stdlib only)."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height, 9)) + chunk(b"IEND", b""))


def rgb_of(text):
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))


def marker_file(marker):
    return "overlay_marker_%s.png" % marker["name"]


def _image(name, file, rect):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": "Image",
                              "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": file}},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute",
                       "bounds": "%d %d %d %d" % rect}}


def _param_of(component):
    return component["handle remapping"]["map"][0]["value"]


def _place(component, row, col):
    component["bounds"]["bounds"] = "%d %d 278 107" % (COLS[col], ROWS[row])


def _qlink(row, col):
    return "Q-Link %d" % (4 * (3 - row) + col + 1)


def _row_map(qlinks, tab):
    return next(m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1)


def _take(items, name):
    found = [c for c in items if c["componentData"]["name"] == name]
    if len(found) != 1:
        raise ValueError("tab has %d components named %r" % (len(found), name))
    return found[0]


def _meta_tab(ctx, name):
    return next(t for t in ctx.meta["tabs"] if t["name"] == name)


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    layout = json.loads(LAYOUT_JSON.read_text(encoding="utf-8"))
    if layout["tab"] != MAIN_TAB:
        raise ValueError("overlay_layout.json names the tab %r, this module builds %r" % (layout["tab"], MAIN_TAB))
    main, model = ctx.tab(MAIN_TAB), ctx.tab(MODEL_TAB)
    items, model_items = main["componentDefinition"]["componentsData"], model["componentDefinition"]["componentsData"]
    idx = {k: ctx.param_index(k) for k in ("filter1.on", "filter1.model", "filter1.style", "filter1.cutoff", "filter1.resonance", "filter1.drive", "filter1.blend",
                                           "filter1.mix", "filter1.keytrack")}
    by_param = {_param_of(c): c for c in items}
    for key in ("filter1.on", "filter1.model", "filter1.style", "filter1.drive", "filter1.blend", "filter1.mix"):
        if "Parameter %d" % idx[key] not in by_param:
            raise ValueError("tab %r has no control for %s" % (MAIN_TAB, key))
    if "Parameter %d" % idx["filter1.cutoff"] in by_param or "Parameter %d" % idx["filter1.resonance"] in by_param:
        raise ValueError("tab %r already has Cutoff or Resonance" % MAIN_TAB)
    keytrack = _take(model_items, "Filter 1 Key Tracking")
    if _param_of(keytrack) != "Parameter %d" % idx["filter1.keytrack"]:
        raise ValueError("'Filter 1 Key Tracking' is not bound to filter1.keytrack")

    def control(key):
        return by_param["Parameter %d" % idx[key]]

    def knob(name, key):
        c = copy.deepcopy(control("filter1.drive"))
        c["componentData"]["name"] = name
        c["handle remapping"]["map"][0]["value"] = "Parameter %d" % idx[key]
        return c

    cutoff, resonance = knob("Filter 1 Cutoff", "filter1.cutoff"), knob("Filter 1 Resonance", "filter1.resonance")
    grid = [[control("filter1.on"), control("filter1.model"), control("filter1.style"), control("filter1.drive")],
            [cutoff, resonance, control("filter1.mix"), keytrack]]
    blend = control("filter1.blend")

    # Blend takes the place (and the Q-Link) of Key Tracking on the sub-tab
    blend["bounds"]["bounds"] = keytrack["bounds"]["bounds"]
    model_row = _row_map(qlinks, model)
    for slot, value in model_row["Q-Links"].items():
        if value == idx["filter1.keytrack"]:
            model_row["Q-Links"][slot] = idx["filter1.blend"]
    model_items.insert(0, blend)
    model_items[:] = [c for c in model_items if c is not keytrack]

    # the two rows of the Filter 1 tab and its Q-Link map
    links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
    items[:] = []
    for r, row in enumerate(grid):
        for col, c in enumerate(row):
            _place(c, 2 + r, col)
            links[_qlink(2 + r, col)] = int(_param_of(c).split()[1])
            items.append(c)
    _row_map(qlinks, main)["Q-Links"] = links
    main["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H) for b in main["qlinkBoundsData"]]

    # names over the controls (85_names.py takes them from the meta): the short names of this tab
    names = _meta_tab(ctx, MAIN_TAB)["names"]
    names.update({"filter1.cutoff": "Cutoff", "filter1.resonance": "Resonance", "filter1.keytrack": "Key Tracking"})
    _meta_tab(ctx, MODEL_TAB)["names"].update({"filter1.blend": "Blend"})
    placed = _meta_tab(ctx, MAIN_TAB)["placed"]
    placed[:] = sorted((set(placed) - {"filter1.blend"}) | {"filter1.cutoff", "filter1.resonance", "filter1.keytrack"})
    mplaced = _meta_tab(ctx, MODEL_TAB)["placed"]
    mplaced[:] = sorted((set(mplaced) - {"filter1.keytrack"}) | {"filter1.blend"})

    # the curve area: dark background picture and the two marker squares
    c = layout["curve"]
    (ctx.skins_dir / BACKGROUND_FILE).write_bytes(png_solid(c["w"], c["h"], rgb_of(layout["background"])))
    items.append(_image("Filter 1 Curve Background", BACKGROUND_FILE, (c["x"], c["y"], c["w"], c["h"])))
    for m in layout["markers"]:
        (ctx.skins_dir / marker_file(m)).write_bytes(png_solid(m["w"], m["h"], rgb_of(m["rgb"])))
        items.append(_image("Filter 1 Overlay Marker " + m["name"], marker_file(m), (m["x"], m["y"], m["w"], m["h"])))
