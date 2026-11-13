# SynthyBoy - MPC skin extra: the live spectrum, 32 columns in the empty bottom row of the Macros tab.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The Macros tab has three rows of controls (12 slots of the 4 x 4 grid); the fourth row is empty. Its place, y 480 .. 616 of the 628 pixel high tab, holds 32 native `Meter`
# widgets side by side, one per band of the spectrum, drawn upwards (direction "Up", the one of the output meters of the stock Filterbank skin): COLUMN_W = 30 px wide, GAP = 8 px
# apart, 32 * 30 + 31 * 8 = 1208 px, centred on the 1280 px screen (x 36 .. 1244). They cross the four columns of the Q-Link frames, so the frames of the tab end above them:
# their height is FRAME_H = 474 (they cover rows 1 to 3, row 3 ends at y 470), the tab becomes a tab with a picture at the bottom, as the Env / Osc / LFO tabs are tabs with a
# picture at the top.
#
# Why a Meter and not a film strip: measured on the Key 61 (2026-11-13), a Meter bound to a plugin parameter follows the value and is redrawn when the plugin calls
# audioMasterAutomate for the parameter, and it needs only two small pictures for any number of levels (a film strip of 17 frames per column would be 32 strips). The keys of
# the element are those of the `envfollower_meter1` element of the stock Multiband Filterbank skin, in the same order; only the images, the direction and the handles are ours.
# The two pictures (spectrum_off.png dark, spectrum_on.png accent colour with a vertical gradient, both 30 x 136, scripts/gen-mpc-spectrum-images.py) are shared by all 32.
# peakHandle "Parameter N" (and "Hold/Parameter N") with the element's own remap Data -> Parameter N, N = the display parameter "Spectrum k" (index = number of entries of
# docs/params.json + 4 LFO cursors + k - 1, shell-mpc/synthyboy_mpc.cpp). The values are 0 .. 1 in steps of 1/64.
# The plugin must be built with SYNTHYBOY_MPC_SPECTRUM (the default of shell-mpc), otherwise the parameters do not exist.

import json
import shutil
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "spectrum-images"
PARAMS_JSON = HERE.parent.parent / "docs" / "params.json"

TAB = "Macros"
COLUMNS = 32
COLUMN_W, GAP = 30, 8
X0 = (1280 - (COLUMNS * COLUMN_W + (COLUMNS - 1) * GAP)) // 2   # 36
Y, COLUMN_H = 480, 136                                           # y 480 .. 616 (the tab is 628 high)
FRAME_H = 474                                                    # the Q-Link frames end here (the 3rd row of controls ends at y 470 on the plain tabs)
LFO_CURSORS = 4                                                  # display parameters before the spectrum (shell-mpc/lfo_live.h kLfoLiveCount)
FILE_OFF, FILE_ON = "spectrum_off.png", "spectrum_on.png"
FIRST_FREE_ROW_Y = 400                                           # every control of the tab is above this at the time this module runs (generator grid: rows at 45 / 196 / 346)


def column_x(k):
    return X0 + k * (COLUMN_W + GAP)


def png_size(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    return struct.unpack(">II", data[16:24])


def meter(name, handle):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": "Meter",
                              "data": {"version": 3, "direction": "Up", "invert": False, "inactiveImage": FILE_OFF, "rmsImage": "", "rmsHandle": "Invalid",
                                       "peakImage": FILE_ON, "peakHandle": handle, "peakHoldHandle": "Hold/" + handle, "peakHoldSize": 0, "peakHoldColour": "00ffffff"}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": handle}]},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": ""}}


def apply(tui, qlinks, ctx):
    params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
    first_spectrum = len(params) + LFO_CURSORS
    for name in (FILE_OFF, FILE_ON):
        path = ASSETS / name
        if not path.is_file():
            raise FileNotFoundError("%s is missing (run python3 scripts/gen-mpc-spectrum-images.py)" % path)
        if png_size(path) != (COLUMN_W, COLUMN_H):
            raise ValueError("%s is %dx%d, not %dx%d" % (name, *png_size(path), COLUMN_W, COLUMN_H))
        shutil.copyfile(path, ctx.skins_dir / name)
    tab = ctx.tab(TAB)
    items = tab["componentDefinition"]["componentsData"]
    for c in items:
        x, y, w, h = (int(v) for v in c["bounds"]["bounds"].split())
        if y >= FIRST_FREE_ROW_Y:
            raise ValueError("tab %r: %r at y %d is not above the row of the spectrum" % (TAB, c["componentData"]["name"], y))
    for k in range(COLUMNS):
        m = meter("Spectrum %d" % (k + 1), "Parameter %d" % (first_spectrum + k))
        m["bounds"]["bounds"] = "%d %d %d %d" % (column_x(k), Y, COLUMN_W, COLUMN_H)
        items.append(m)
    tab["qlinkBoundsData"] = ["%s 0 %s %d" % (b.split()[0], b.split()[2], FRAME_H) for b in tab["qlinkBoundsData"]]
