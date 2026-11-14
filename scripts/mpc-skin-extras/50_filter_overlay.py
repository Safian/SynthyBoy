# SynthyBoy - MPC skin extra: the "Filter 1" tab of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json), made for the plugin built with SYNTHYBOY_MPC_OVERLAY.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the layout file of that variant sets it; the production layout does not, and this module then changes nothing).
# The overlay draws live pictures over regions of tabs (shell-mpc/overlay/overlay_layout.json: the VIEWS): the response curve of Filter 1 and Filter 2, the shape of LFO 1 .. 4
# with its cursor, one cycle of Oscillator 1 .. 3. Every view has a region and a pair of marker squares of colours that no other view uses.
#
# Filter 1 / Filter 2 tabs: the curve area is at the top of the tab (a dark picture of the skin shows there and nothing else), the controls are the two rows below it, in the
# lower two rows of the 4 x 4 grid like the tabs with a picture above (Env, Osc, LFO: the Q-Link frames cover those two rows only):
#     row 3:  On, Model, Style, Drive          (Q-Link 5 .. 8)
#     row 4:  Cutoff, Resonance, Mix, Key Tracking      (Q-Link 1 .. 4)
# Filter 1: Cutoff and Resonance are not on the production tab (they are on Macros): the controls are copies of the Drive knob bound to filter1.cutoff / filter1.resonance
# (ctx.param_index); Filter 2 has them already. Everything else the production tab has stays reachable: Blend and the six controls of the model / formant page go to the sub-tab
# "F1 Model" / "F2 Model" of the same function key (the layout file gives it the Kick page filterN_model; Blend takes the place of Key Tracking, which moved to the main tab).
#
# LFO and Osc tabs keep their pictures (the shape / wavetable strips of 30_lfo.py / 20_osc.py): the overlay covers them while it runs, they are what the player sees when it
# is off. Only the marker pictures are added on top of them.
#
# The marker pictures (overlay_marker_<view>_<left|right>.png, solid squares of the colours in shell-mpc/overlay/overlay_layout.json at the positions given there, drawn 1:1) are
# what the plugin's page detector looks for in the primary framebuffer; the plugin paints the dark picture over them, so the player sees only the dark area. The numbers are in
# that JSON only (the plugin includes the header scripts/gen-overlay-layout-header.py makes of it).
#
# INSTANCE picture (several plugin instances): next to the two markers of every view the skin draws a tiny film strip image (`instance` of the view in the JSON, 6 x 6 px, the envelope
# tabs 4 x 4) bound to the display-only parameter "Overlay Instance" (the LAST parameter of the overlay build: number of entries of docs/params.json + 4 LFO cursors + 32 spectrum
# columns + 2 pad pressure parameters). Its 33 frames are solid colours (blank, slot 0 .. 15, none, blank ...; `instance` in the JSON): the host draws the frame of THIS instance's value, so the colour the plugin
# reads in the primary framebuffer says which instance's page is on screen. The strips are RGB PNGs with the settings of the marker pictures (no alpha), width = the picture, height =
# 33 frames one under the other (like the cursor strip of 30_lfo.py).
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
BACKGROUND_FILE = "overlay_curve_bg.png"
LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS = 4, 32, 2   # the parameters before "Overlay Instance" (shell-mpc/lfo_live.h, shell-mpc/spectrum.h, shell-mpc/pad_pressure.h; 45_pad_pressure.py)
FILTERS = (1, 2)


def png_solid(width, height, rgb):
    """An opaque RGB PNG of one colour (stdlib only)."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height, 9)) + chunk(b"IEND", b""))


def png_filmstrip(width, frame_height, colours):
    """An opaque RGB PNG of len(colours) frames one under the other, every frame one solid colour (stdlib only; the settings of png_solid)."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join((b"\x00" + bytes(c) * width) * frame_height for c in colours)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, frame_height * len(colours), 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def instance_frames(layout):
    """The colour of every frame of the instance strip: 0 blank, 1 .. slots the slot colours, then none, the rest blank."""
    inst = layout["instance"]
    frames = [rgb_of(inst["blank"])] * inst["frames"]
    for k, c in enumerate(inst["slotColours"]):
        frames[k + 1] = rgb_of(c)
    frames[inst["slots"] + 1] = rgb_of(inst["none"])
    return frames


def instance_file(w, h):
    return "overlay_instance_%dx%d.png" % (w, h)


def rgb_of(text):
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))


def marker_file(view, side):
    return "overlay_marker_%s_%s.png" % (view["name"], side)


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


def _arrange_filter(n, view, ctx, qlinks, layout):
    """The two rows of controls of the tab "Filter n" and the sub-tab "Fn Model"; the dark curve picture and the markers of its view."""
    main_tab, model_tab = "Filter %d" % n, "F%d Model" % n
    if view["tab"] != main_tab:
        raise ValueError("overlay_layout.json names the tab %r for %s, this module builds %r" % (view["tab"], view["name"], main_tab))
    main, model = ctx.tab(main_tab), ctx.tab(model_tab)
    items, model_items = main["componentDefinition"]["componentsData"], model["componentDefinition"]["componentsData"]
    keys = ("on", "model", "style", "cutoff", "resonance", "drive", "blend", "mix", "keytrack")
    idx = {k: ctx.param_index("filter%d.%s" % (n, k)) for k in keys}
    by_param = {_param_of(c): c for c in items}
    for key in ("on", "model", "style", "drive", "blend", "mix"):
        if "Parameter %d" % idx[key] not in by_param:
            raise ValueError("tab %r has no control for filter%d.%s" % (main_tab, n, key))
    keytrack = _take(model_items, "Filter %d Key Tracking" % n)
    if _param_of(keytrack) != "Parameter %d" % idx["keytrack"]:
        raise ValueError("'Filter %d Key Tracking' is not bound to filter%d.keytrack" % (n, n))

    def control(key):
        return by_param["Parameter %d" % idx[key]]

    def knob(name, key):
        existing = by_param.get("Parameter %d" % idx[key])
        if existing is not None:
            return existing
        c = copy.deepcopy(control("drive"))
        c["componentData"]["name"] = name
        c["handle remapping"]["map"][0]["value"] = "Parameter %d" % idx[key]
        return c

    cutoff, resonance = knob("Filter %d Cutoff" % n, "cutoff"), knob("Filter %d Resonance" % n, "resonance")
    grid = [[control("on"), control("model"), control("style"), control("drive")],
            [cutoff, resonance, control("mix"), keytrack]]
    blend = control("blend")

    # Blend takes the place (and the Q-Link) of Key Tracking on the sub-tab
    blend["bounds"]["bounds"] = keytrack["bounds"]["bounds"]
    model_row = _row_map(qlinks, model)
    for slot, value in model_row["Q-Links"].items():
        if value == idx["keytrack"]:
            model_row["Q-Links"][slot] = idx["blend"]
    model_items.insert(0, blend)
    model_items[:] = [c for c in model_items if c is not keytrack]

    # the two rows of the main tab and its Q-Link map
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
    names = _meta_tab(ctx, main_tab)["names"]
    names.update({"filter%d.cutoff" % n: "Cutoff", "filter%d.resonance" % n: "Resonance", "filter%d.keytrack" % n: "Key Tracking"})
    _meta_tab(ctx, model_tab)["names"].update({"filter%d.blend" % n: "Blend"})
    placed = _meta_tab(ctx, main_tab)["placed"]
    placed[:] = sorted((set(placed) - {"filter%d.blend" % n}) | {"filter%d.cutoff" % n, "filter%d.resonance" % n, "filter%d.keytrack" % n})
    mplaced = _meta_tab(ctx, model_tab)["placed"]
    mplaced[:] = sorted((set(mplaced) - {"filter%d.keytrack" % n}) | {"filter%d.blend" % n})

    # the dark picture of the curve area (under the live curve; what the player sees when the overlay is off)
    r = view["region"]
    (ctx.skins_dir / BACKGROUND_FILE).write_bytes(png_solid(r["w"], r["h"], rgb_of(layout["background"])))
    items.append(_image("Filter %d Curve Background" % n, BACKGROUND_FILE, (r["x"], r["y"], r["w"], r["h"])))


def _add_markers(view, ctx, items):
    for side, m in zip(("left", "right"), view["markers"]):
        (ctx.skins_dir / marker_file(view, side)).write_bytes(png_solid(m["w"], m["h"], rgb_of(m["rgb"])))
        items.append(_image("Overlay Marker %s %s" % (view["name"], side), marker_file(view, side), (m["x"], m["y"], m["w"], m["h"])))


def _add_instance(view, ctx, items, layout, param_index):
    inst, r = layout["instance"], view["instance"]
    frames = instance_frames(layout)
    name = instance_file(r["w"], r["h"])
    (ctx.skins_dir / name).write_bytes(png_filmstrip(r["w"], r["h"], frames))
    items.append({"version": 2,
                  "componentData": {"version": 1, "name": "Overlay Instance %s" % view["name"], "type": "Image",
                                    "data": {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": name, "numFrames": inst["frames"], "handleName": "Data"}},
                  "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % param_index}]},
                  "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always", "boundsType": "Absolute",
                             "bounds": "%d %d %d %d" % (r["x"], r["y"], r["w"], r["h"])}})


def apply(tui, qlinks, ctx):
    if ctx.meta is None or ctx.meta.get("variant") != "overlay":
        return
    layout = json.loads(LAYOUT_JSON.read_text(encoding="utf-8"))
    # "Overlay Instance" is the LAST parameter of the overlay build (docs/MPC.md "Parameter order"): after the engine's list, the four LFO cursors, the 32 spectrum columns and the two pad
    # pressure parameters (the indexes of those do not move in any build)
    param_index = ctx.param_count + LFO_CURSORS + SPECTRUM_COLUMNS + PAD_PRESSURE_PARAMS
    for view in layout["views"]:
        if view["kind"] == "filter":
            if view["index"] not in FILTERS:
                raise ValueError("filter view %r: no such filter" % view["name"])
            _arrange_filter(view["index"], view, ctx, qlinks, layout)
        elif view["kind"] not in ("lfo", "osc", "env"):
            raise ValueError("view %r: unknown kind %r" % (view["name"], view["kind"]))
        items = ctx.tab(view["tab"])["componentDefinition"]["componentsData"]
        _add_markers(view, ctx, items)
        _add_instance(view, ctx, items, layout, param_index)
