# SynthyBoy - MPC skin extras: oscillator wave pictures (GATE version, placeholder pictures).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Gate (plan 2-oszcillator.md points 1-2), to be replaced by the real strips of gen-mpc-wave-strips.py:
#   (a) tab "Osc 1": a 15-frame Image film strip bound to osc1.wavetable, in the free slot (983 497 278 107);
#   (b) tab "Osc 2": per-table film strips bound to osc2.frame, only the strip of the selected osc2.wavetable
#       value is visible (bounds v2 additionalInvalidatingHandles, ui-reference 7.4), in the free slot too
#       (Osc 1 has no second free slot, so (b) is on Osc 2 and uses tables 0 and 1).
# Pictures here are simple labelled placeholders at the final size (278x107 per frame), drawn with PIL.

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent.parent
BG, RED, GREY = (30, 29, 30, 255), (211, 17, 69, 255), (110, 110, 112, 255)
FRAME_W, FRAME_H = 278, 107
SLOT = "983 497 278 107"
FRAME_STEPS = 11  # frame strips: 0, 10 ... 100 %


def _font(size):
    return ImageFont.load_default(size)


def _table_names():
    params = json.loads((ROOT / "docs" / "params.json").read_text(encoding="utf-8"))["params"]
    return next(p for p in params if p["key"] == "osc1.wavetable")["choices"]


def _strip(frames_text, extra=None):
    img = Image.new("RGBA", (FRAME_W, FRAME_H * len(frames_text)), BG)
    d = ImageDraw.Draw(img)
    for i, text in enumerate(frames_text):
        y = i * FRAME_H
        d.rectangle([0, y, FRAME_W - 1, y + FRAME_H - 1], outline=GREY)
        d.text((8, y + 6), text, font=_font(22), fill=RED)
        d.text((FRAME_W - 40, y + FRAME_H - 24), str(i), font=_font(16), fill=GREY)
        if extra:
            extra(d, y, i)
    return img


def _entry(name, image, frames, param, enabling=None):
    version = 2 if enabling else 1
    bounds = {"version": version, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
              "boundsType": "Absolute", "bounds": SLOT}
    if enabling:
        bounds["additionalInvalidatingHandles"] = [enabling]
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": "Image", "data": {
            "version": 2, "imageType": "FilmStrip", "colour": "0", "image": image, "numFrames": frames, "handleName": "Data"}},
        "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % param}]},
        "bounds": bounds,
    }


def apply(tui, qlinks, ctx):
    names = _table_names()
    n = len(names)
    # (a) table picture, one frame per table
    _strip(["%d/%d %s" % (i + 1, n, names[i]) for i in range(n)]).save(ctx.skins_dir / "osc1_wavetable_strip.png")
    ctx.tab("Osc 1")["componentDefinition"]["componentsData"].append(
        _entry("Osc 1 Wave Picture", "osc1_wavetable_strip.png", n, ctx.param_index("osc1.wavetable")))
    # (b) per-table frame strips, only the selected table's strip is valid/visible
    tab2 = ctx.tab("Osc 2")["componentDefinition"]["componentsData"]
    wt = ctx.param_index("osc2.wavetable")
    for t in (0, 1):
        def line(d, y, i, t=t):
            x = 8 + i * 25
            d.line([(8, y + 90), (x, y + 90)], fill=RED, width=3)
        fn = "osc2_t%d_frame_strip.png" % t
        _strip(["T%d %s F%d%%" % (t + 1, names[t], i * 100 // (FRAME_STEPS - 1)) for i in range(FRAME_STEPS)], line).save(ctx.skins_dir / fn)
        tab2.append(_entry("Osc 2 Frame Picture T%d" % (t + 1), fn, FRAME_STEPS, ctx.param_index("osc2.frame"),
                           "IndexedEnabling/%d/%d/Parameter %d" % (t, n, wt)))
