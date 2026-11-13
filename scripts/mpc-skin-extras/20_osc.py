# SynthyBoy - MPC skin extra: the wavetable picture of the oscillators on the tabs Osc 1 .. Osc 3.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Per tab "Osc N": a large picture of the selected wavetable in the upper two rows of the 4 x 4 grid (PICTURE below), the 8 knobs of
# osc N Main in rows 3-4, and the Q-Link map rewritten so that those knobs are Q-Link 1-8 (the lower two hardware rows): the rows seen on the
# screen are the rows of the hardware (Bank Direction "Column": Q-Link 4*(3-row)+col+1, row 0 = top, col 0 = left). The unison and the
# shaping knobs are on the tab "OscN Shp" (scripts/mpc-ui-pages.json).
#
# The picture: one Image film strip per wavetable choice (15), each valid only while oscN.wavetable has that choice (bounds version 2,
# IndexedEnabling, ui-reference 7.4); the strip frame follows oscN.frame. The 15 PNGs are shared by the three oscillators (same file names),
# they are rendered from the engine's tables by scripts/gen-mpc-wave-strips.py and committed in osc-wave-strips/. Morph and warp are not drawn
# (the skin has no live curve): the picture is the table itself. Memory and frame count: docs/MPC.md "Oscillator wave pictures".

import json
import shutil
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "osc-wave-strips"
PARAMS_JSON = HERE.parent.parent / "docs" / "params.json"

COLS = (23, 343, 663, 983)
ROWS = (45, 196, 346, 497)
PICTURE = (384, 70, 512, 240)   # x y w h: centred in the 1238 x 290 area of rows 1-2 (the PNG frames are exactly 512 x 240)
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 336, 288


def file_name(choice):
    return "osc_wt_%02d.png" % choice


def png_size(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    return struct.unpack(">II", data[16:24])


def _choices():
    params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
    return next(p for p in params if p["key"] == "osc1.wavetable")["choices"]


def _picture_entry(name, choice, count, frames, image, frame_param, wavetable_param):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": "Image", "data": {
            "version": 2, "imageType": "FilmStrip", "colour": "0", "image": image, "numFrames": frames, "handleName": "Data"}},
        "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % frame_param}]},
        "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
                   "boundsType": "Absolute", "bounds": "%d %d %d %d" % PICTURE,
                   "additionalInvalidatingHandles": ["IndexedEnabling/%d/%d/Parameter %d" % (choice, count, wavetable_param)]},
    }


def _move_knobs_down(tab, qlinks, ctx, n):
    """Rows 1-2 -> rows 3-4 and Q-Link 1-8 for the 8 knobs of the tab; returns nothing."""
    items = tab["componentDefinition"]["componentsData"]
    row_map = next(m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1)
    links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
    for e in items:
        x, y, w, h = e["bounds"]["bounds"].split()
        row, col = ROWS.index(int(y)), COLS.index(int(x))
        if row > 1:
            raise ValueError("tab 'Osc %d': a knob is already in row %d" % (n, row + 1))
        row += 2
        e["bounds"]["bounds"] = "%d %d %s %s" % (COLS[col], ROWS[row], w, h)
        param = int(e["handle remapping"]["map"][0]["value"].split()[1])
        links["Q-Link %d" % (4 * (3 - row) + col + 1)] = param
    row_map["Q-Links"] = links
    tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H) for b in tab["qlinkBoundsData"]]


def apply(tui, qlinks, ctx):
    choices = _choices()
    count = len(choices)
    frames = {}
    for choice in range(count):
        path = ASSETS / file_name(choice)
        if not path.is_file():
            raise ValueError("missing %s (run scripts/gen-mpc-wave-strips.py)" % path)
        w, h = png_size(path)
        if w != PICTURE[2] or h % PICTURE[3]:
            raise ValueError("%s is %dx%d, not %d x a multiple of %d" % (path.name, w, h, PICTURE[2], PICTURE[3]))
        frames[choice] = h // PICTURE[3]
        shutil.copyfile(path, ctx.skins_dir / path.name)
    for n in (1, 2, 3):
        tab = ctx.tab("Osc %d" % n)
        _move_knobs_down(tab, qlinks, ctx, n)
        items = tab["componentDefinition"]["componentsData"]
        frame_param, wavetable_param = ctx.param_index("osc%d.frame" % n), ctx.param_index("osc%d.wavetable" % n)
        for choice in range(count):
            items.append(_picture_entry("Osc %d Wave %s" % (n, choices[choice]), choice, count, frames[choice], file_name(choice),
                                        frame_param, wavetable_param))
