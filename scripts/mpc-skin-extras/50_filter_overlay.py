# SynthyBoy - MPC skin extra: the MARKER pictures of the OVERLAY skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json), made for the plugin built with SYNTHYBOY_MPC_OVERLAY.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Inactive unless skin-meta.json says "variant": "overlay" (the layout file of that variant sets it; the production layout does not, and this module then changes nothing).
# The overlay draws over tabs (shell-mpc/overlay/overlay_layout.json: the VIEWS): one cycle of Oscillator 1 .. 3 over a region of its tab, and the PAGES it draws entirely (the Play tabs, Filter 1 / 2, Env 1 .. 4, LFO 1 .. 4,
# Macros, Mixer, Mod, FX, Master, Voice: 82_play.py and 83_pages.py hide their host widgets while the plugin's page is up and give them the Q proxies). Every view has a region and a pair of marker squares of
# colours that no other view uses; this module draws them (and the instance picture) on the tab of every view. It does not move any control: the tabs keep what the other modules made of them, which is what
# the player sees when the overlay is off or disabled.
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
# Runs after 40_spectrum.py and before 80_layout.py, which places the slots, 85_names.py and 90_frames.py.

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
LFO_CURSORS, SPECTRUM_COLUMNS, PAD_PRESSURE_PARAMS = 4, 32, 2   # the parameters before "Overlay Instance" (shell-mpc/lfo_live.h, shell-mpc/spectrum.h, shell-mpc/pad_pressure.h; 45_pad_pressure.py)
FILTERS = (1, 2)
# the kinds of the views of overlay_layout.json (scripts/gen-overlay-layout-header.py KINDS): the pictures over a region of a host tab (osc) and the pages the plugin draws entirely (83_pages.py, 82_play.py)
KNOWN_KINDS = ("osc", "play", "filterui", "lfoui", "envui", "macros", "mixer", "fx", "mod", "master", "voice")


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
        if view["kind"] not in KNOWN_KINDS:
            raise ValueError("view %r: unknown kind %r" % (view["name"], view["kind"]))
        if view["kind"] == "filterui" and view["index"] not in FILTERS:
            raise ValueError("filter view %r: no such filter" % view["name"])
        items = ctx.tab(view["tab"])["componentDefinition"]["componentsData"]
        _add_markers(view, ctx, items)
        _add_instance(view, ctx, items, layout, param_index)
