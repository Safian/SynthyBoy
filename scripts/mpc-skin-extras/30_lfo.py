# SynthyBoy - MPC skin extra: the LFO tabs LFO 1 .. LFO 4 with a large picture of the shape and a live cursor.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Per tab "LFO N" (sub-tabs of the function key LFO, scripts/mpc-ui-pages.json): the picture PICTURE (600 x 240, centred in the upper two rows of the 4 x 4 grid, above the
# frames of the lower two rows) and the 8 controls of lfoN in rows 3-4 (Shape, Sync, Rate, Division / Trigger, Phase, Fade In, Delay). The Q-Link map of the tab is rewritten
# so that those controls are Q-Link 1-8 in the rows seen on the screen (Bank Direction "Column": Q-Link 4*(3-row)+col+1, row 0 = top), as on the tabs Env 1-4 and Osc 1-3.
#
# The picture has TWO layers (the host keeps pictures decoded, width x height x 4 bytes per frame: one film strip of 33 frames of this size per shape would be 13 * 19 MB):
#   (a) the static picture of the selected shape: 13 single frame PNGs (12 shapes of the engine's lfoShape() and CUSTOM), one Image each, valid only while lfoN.shape has that
#       choice (bounds version 2, IndexedEnabling, ui-reference 7.4);
#   (b) above it ONE transparent film strip, shared by the four LFOs and every shape (lfo_cursor.png): frame 0 empty, frames 1 .. 32 a vertical line in one cell of the cycle,
#       bound to the display parameter "LFO N Cursor" (index = number of entries of docs/params.json + N - 1, shell-mpc/lfo_live.h; the shell shows the step / 32 of the
#       voice played last). The host redraws it when the plugin calls audioMasterAutomate for that parameter. The strip is always shown (also on Custom).
# The PNGs are rendered by scripts/gen-mpc-lfo-pictures.py and committed in lfo-pictures/. The plugin must be built with SYNTHYBOY_MPC_LFO_LIVE (the default of shell-mpc).
#
# Sync (lfoN.sync: 0 Free (Hz), 1 Tempo, 2 Tempo dotted, 3 Tempo triplet, 4 Key follow; core/engine-own/env_lfo/lfo.h lfoRateHz): the Rate (Hz) matters only for Free, the
# Division only for the three tempo modes, Key follow uses neither. A Q-Link is one parameter, so the two controls keep their two places and the one that does not apply is hidden:
# Rate is valid for sync 0 (IndexedEnabling/0/5), the Division exists three times at one place, each copy valid for one of sync 1, 2, 3. The Q-Link map is not changed by this.
# The names of the controls (85_names.py) follow their control. 80_layout.py (runs later) places the slots; the pictures stay where they are.

import copy
import json
import shutil
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "lfo-pictures"
PARAMS_JSON = HERE.parent.parent / "docs" / "params.json"

COLS = (23, 343, 663, 983)
ROWS = (45, 196, 346, 497)
PICTURE = (340, 30, 600, 240)   # x y w h: centred on the screen, y 30 .. 270, above the frames of the lower two rows (they start at y 301)
FRAME_H = 240
NUM_SHAPES = 12                 # shapes of the engine; choice 12 is Custom
CURSOR_FRAMES = 33
QLINK_BOUNDS_Y, QLINK_BOUNDS_H = 301, 323   # x and width of the frames: 90_frames.py
SYNC_CHOICES = 5
TEMPO_SYNC = (1, 2, 3)          # sync choices in which the Division applies
# the controls of the tab in the order of the grid cells (row 3, then row 4): (name suffix, params.json suffix)
CONTROLS = (("Shape", "shape"), ("Sync", "sync"), ("Rate", "rate"), ("Division", "division"),
            ("Trigger", "sync_type"), ("Phase", "phase"), ("Fade In", "fade_in"), ("Delay", "delay"))


def shape_file(choice):
    return "lfo_shape_%02d.png" % choice


CURSOR_FILE = "lfo_cursor.png"


def png_size(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    return struct.unpack(">II", data[16:24])


def _picture_entry(name, data, remap, enabling):
    bounds = {"version": 2 if enabling else 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Absolute", "bounds": "%d %d %d %d" % PICTURE}
    if enabling:
        bounds["additionalInvalidatingHandles"] = [enabling]
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": "Image", "data": data},
            "handle remapping": {"version": 1, "map": remap}, "bounds": bounds}


def _enable(control, handle):
    """The control is valid only where `handle` (IndexedEnabling/...) says so: bounds version 2 with the extra key."""
    control["bounds"]["version"] = 2
    control["bounds"]["additionalInvalidatingHandles"] = [handle]


def apply(tui, qlinks, ctx):
    params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
    shape = next(p for p in params if p["key"] == "lfo1.shape")
    sync = next(p for p in params if p["key"] == "lfo1.sync")
    if len(shape["choices"]) != NUM_SHAPES + 1:
        raise ValueError("lfo1.shape has %d choices, the pictures cover %d + Custom" % (len(shape["choices"]), NUM_SHAPES))
    if len(sync["choices"]) != SYNC_CHOICES or [c.split()[0] for c in sync["choices"][:4]] != ["Free", "Tempo", "Tempo", "Tempo"]:
        raise ValueError("lfo1.sync is not Free, Tempo, Tempo dotted, Tempo triplet, Key follow: %r" % (sync["choices"],))
    first_display = len(params)
    for choice in range(NUM_SHAPES + 1):
        path = ASSETS / shape_file(choice)
        if not path.is_file():
            raise FileNotFoundError("%s is missing (run python3 scripts/gen-mpc-lfo-pictures.py)" % path)
        if png_size(path) != PICTURE[2:]:
            raise ValueError("%s is %dx%d, not %dx%d" % (path.name, *png_size(path), *PICTURE[2:]))
        shutil.copyfile(path, ctx.skins_dir / path.name)
    cursor = ASSETS / CURSOR_FILE
    if not cursor.is_file():
        raise FileNotFoundError("%s is missing (run python3 scripts/gen-mpc-lfo-pictures.py)" % cursor)
    w, h = png_size(cursor)
    if (w, h) != (PICTURE[2], PICTURE[3] * CURSOR_FRAMES):
        raise ValueError("%s is %dx%d, not %dx%d" % (cursor.name, w, h, PICTURE[2], PICTURE[3] * CURSOR_FRAMES))
    shutil.copyfile(cursor, ctx.skins_dir / CURSOR_FILE)

    for n in (1, 2, 3, 4):
        tab = ctx.tab("LFO %d" % n)
        items = tab["componentDefinition"]["componentsData"]
        prefix = "LFO %d " % n
        by_name = {c["componentData"]["name"]: c for c in items}
        extra = set(by_name) - {prefix + s for s, _ in CONTROLS}
        if extra or len(by_name) != len(items):
            raise ValueError("unexpected components on tab 'LFO %d': %s" % (n, sorted(extra)))
        row_map = next(m for m in qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1)
        links = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        sync_param = ctx.param_index("lfo%d.sync" % n)
        shape_param = ctx.param_index("lfo%d.shape" % n)
        out = []
        for i, (suffix, key) in enumerate(CONTROLS):
            row, col = 2 + i // 4, i % 4
            c = by_name[prefix + suffix]
            param = ctx.param_index("lfo%d.%s" % (n, key))
            if c["handle remapping"]["map"][0]["value"] != "Parameter %d" % param:
                raise ValueError("%s is bound to %s, not lfo%d.%s" % (c["componentData"]["name"], c["handle remapping"]["map"][0]["value"], n, key))
            c["bounds"]["bounds"] = "%d %d %d %d" % (COLS[col], ROWS[row], 278, 107)
            links["Q-Link %d" % (4 * (3 - row) + col + 1)] = param
            if key == "rate":
                _enable(c, "IndexedEnabling/0/%d/Parameter %d" % (SYNC_CHOICES, sync_param))
                out.append(c)
            elif key == "division":
                for k, s in enumerate(TEMPO_SYNC):
                    copy_ = c if k == 0 else copy.deepcopy(c)
                    copy_["componentData"]["name"] = prefix + "Division" + ("" if k == 0 else " %d" % (k + 1))
                    _enable(copy_, "IndexedEnabling/%d/%d/Parameter %d" % (s, SYNC_CHOICES, sync_param))
                    out.append(copy_)
            else:
                out.append(c)
        items[:] = out
        row_map["Q-Links"] = links
        tab["qlinkBoundsData"] = ["%s %d %s %d" % (b.split()[0], QLINK_BOUNDS_Y, b.split()[2], QLINK_BOUNDS_H) for b in tab["qlinkBoundsData"]]
        # layer (a): the picture of the shape, one element per choice; layer (b): the cursor strip on top
        for choice in range(NUM_SHAPES + 1):
            name = "LFO %d Picture %s" % (n, "Custom" if choice == NUM_SHAPES else choice)
            items.append(_picture_entry(name, {"version": 2, "imageType": "Regular", "colour": "0", "image": shape_file(choice)}, [],
                                        "IndexedEnabling/%d/%d/Parameter %d" % (choice, NUM_SHAPES + 1, shape_param)))
        items.append(_picture_entry("LFO %d Cursor" % n, {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": CURSOR_FILE,
                                                            "numFrames": CURSOR_FRAMES, "handleName": "Data"},
                                    [{"key": "Data", "value": "Parameter %d" % (first_display + n - 1)}], None))
