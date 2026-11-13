# SynthyBoy - MPC skin extra: the LFO pictures of LFO 1-4 (selected shape with a live cursor).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# On the tabs "LFO 1-2" and "LFO 3-4", above the "LFO N Shape" selector, one picture per LFO. It is ONE element per shape choice of lfoN.shape (12 film strips
# and the CUSTOM marker), each visible only while lfoN.shape has that choice (bounds version 2, IndexedEnabling, ui-reference 7.4). The film strip of a shape has
# frame 0 = the shape alone and frames 1 .. 32 = the shape with the cursor in one cell of the cycle; the frame follows the display-only plugin parameter
# "LFO N Cursor" (index = number of entries of docs/params.json + N - 1, shell-mpc/lfo_live.h), so the same picture is the static shape picture
# and the live display. The plugin must be built with SYNTHYBOY_MPC_LFO_LIVE (the default of shell-mpc): without those parameters the host hides the pictures.
# The strips are rendered from the engine's own LFO code by scripts/gen-mpc-lfo-strips.py and committed in lfo-strips/ (the build copies them and needs no compiler). The existing controls and Q-Links.json are not touched.
# Place: 208 x 38 directly above the Shape selector; 80_layout.py (runs later) centres it over the column of the selector (x 56, the axis of the first Q-Link frame)
# and moves it with the selector.

import importlib.util
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPTS = HERE.parent
ASSETS = HERE / "lfo-strips"
PARAMS_JSON = SCRIPTS.parent / "docs" / "params.json"
TABS = {"LFO 1-2": (1, 2), "LFO 3-4": (3, 4)}
PICTURE_X = 86
GAP = 2  # pixels between the picture and the selector cell


def _load_generator():
    spec = importlib.util.spec_from_file_location("gen_mpc_lfo_strips", SCRIPTS / "gen-mpc-lfo-strips.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _rect(component):
    x, y, w, h = (int(v) for v in component["bounds"]["bounds"].split())
    return x, y, w, h


def _entry(name, data, handle_map, rect, enabling):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": "Image", "data": data},
        "handle remapping": {"version": 1, "map": handle_map},
        "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
                   "boundsType": "Absolute", "bounds": "%d %d %d %d" % rect, "additionalInvalidatingHandles": [enabling]},
    }


def apply(tui, qlinks, ctx):
    gen = _load_generator()
    params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
    first_display = len(params)
    shape_choices = len(next(p for p in params if p["key"] == "lfo1.shape")["choices"])
    if shape_choices != gen.NUM_SHAPES + 1:
        raise ValueError("lfo1.shape has %d choices, the pictures cover %d + Custom" % (shape_choices, gen.NUM_SHAPES))
    names = [gen.strip_name(i) for i in range(gen.NUM_SHAPES)] + [gen.CUSTOM_NAME]
    for name in names:
        if not (ASSETS / name).is_file():
            raise FileNotFoundError("%s is missing (run python3 scripts/gen-mpc-lfo-strips.py)" % (ASSETS / name))
        shutil.copyfile(ASSETS / name, ctx.skins_dir / name)
    for title, lfos in TABS.items():
        entries = ctx.tab(title)["componentDefinition"]["componentsData"]
        existing = [(c["componentData"]["name"], _rect(c)) for c in entries]
        added = []
        for n in lfos:
            name = "LFO %d Shape" % n
            shape = [r for nm, r in existing if nm == name]
            if len(shape) != 1:
                raise ValueError("tab %r has %d controls named %r, expected 1" % (title, len(shape), name))
            rect = (PICTURE_X, shape[0][1] - gen.FRAME_H - GAP, gen.FRAME_W, gen.FRAME_H)
            x, y, w, h = rect
            for nm, (ox, oy, ow, oh) in existing:
                if x < ox + ow and ox < x + w and y < oy + oh and oy < y + h:
                    raise ValueError("the LFO %d picture at %d %d %d %d overlaps %r" % (n, x, y, w, h, nm))
            if y < 0:
                raise ValueError("the LFO %d picture at %d %d %d %d leaves the tab" % (n, x, y, w, h))
            shape_param = ctx.param_index("lfo%d.shape" % n)
            cursor = [{"key": "Data", "value": "Parameter %d" % (first_display + n - 1)}]
            for choice in range(gen.NUM_SHAPES):
                enabling = "IndexedEnabling/%d/%d/Parameter %d" % (choice, shape_choices, shape_param)
                added.append(_entry("LFO %d Picture %d" % (n, choice), {
                    "version": 2, "imageType": "FilmStrip", "colour": "0", "image": gen.strip_name(choice),
                    "numFrames": gen.NUM_FRAMES, "handleName": "Data"}, cursor, rect, enabling))
            enabling = "IndexedEnabling/%d/%d/Parameter %d" % (gen.NUM_SHAPES, shape_choices, shape_param)
            added.append(_entry("LFO %d Picture Custom" % n, {
                "version": 2, "imageType": "Regular", "colour": "0", "image": gen.CUSTOM_NAME}, [], rect, enabling))
        entries.extend(added)
