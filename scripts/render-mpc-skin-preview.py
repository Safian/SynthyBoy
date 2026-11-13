#!/usr/bin/env python3
# SynthyBoy - draws a sketch of one tab of a generated MPC skin (a development aid, needs Pillow; not a rendering of the host).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/render-mpc-skin-preview.py --skins "<Plugin Skins>" --params docs/params.json --tab "Osc 1" --out preview.png [--tab ... ]
#   With several --tab options --out is a folder and the files are preview-<tab>.png.
# What is drawn: the tab background, the controls of the shared component definitions (film strip frame 0 of knobs and faders, the buttons, the labels with
# the parameter name, a sample value and the fixed texts), the images of the tab (film strip frame 0; elements that are valid for one choice of another parameter
# only when that choice is 0, the default of every such parameter on the skin), the Q-Link frames, a box for envelopes. The fonts are the closest ones of
# this machine (Arial, or Titillium Web from the folder of the environment variable MPC_PREVIEW_FONTS), so the text widths are indicative only.

import argparse
import json
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TAB_W, TAB_H = 1280, 628
FONT_DIR = os.environ.get("MPC_PREVIEW_FONTS")   # a folder with TitilliumWeb-Bold.ttf and TitilliumWeb-Regular.ttf (the skin font); else Arial
FONTS = ({"Bold": os.path.join(FONT_DIR, "TitilliumWeb-Bold.ttf"), "Regular": os.path.join(FONT_DIR, "TitilliumWeb-Regular.ttf")} if FONT_DIR else
         {"Bold": "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "Regular": "/System/Library/Fonts/Supplemental/Arial.ttf"})


def argb(text):
    if text in ("0", "", None):
        return None
    text = text.rjust(8, "f") if len(text) == 6 else text
    a, r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4, 6))
    return (r, g, b, a)


def font(style, px):
    path = FONTS["Bold" if "Bold" in style else "Regular"]
    try:
        return ImageFont.truetype(path, int(round(px)))
    except OSError:
        return ImageFont.load_default(int(round(px)))


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=lambda pairs: dict(pairs))


def rect(text):
    return tuple(int(float(v)) for v in text.split())


def visible_by_default(bounds):
    for h in bounds.get("additionalInvalidatingHandles", []):
        parts = h.split("/")
        if parts[0] == "IndexedEnabling" and int(parts[1]) != 0:
            return False
    return True


class Sketch:
    def __init__(self, skins, params):
        self.skins = Path(skins)
        self.tui = load_json(self.skins / "TUI.json")
        self.params = params
        self.defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.repl = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}

    def image(self, name, frames=1):
        path = self.skins / name
        if not path.is_file():
            return None
        im = Image.open(path).convert("RGBA")
        if frames > 1:
            im = im.crop((0, 0, im.width, im.height // frames))
        return im

    def text(self, img, box, txt, style, px, colour, justification, case):
        if case == "Upper Case":
            txt = txt.upper()
        d = ImageDraw.Draw(img)
        f = font(style, px)
        x, y, w, h = box
        bb = d.textbbox((0, 0), txt, font=f)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        tx = x + (w - tw) / 2 if "horizontallyCentred" in justification else (x + w - tw if "right" in justification else x)
        ty = y + (h - th) / 2 if "verticallyCentred" in justification else y
        d.text((tx - bb[0], ty - bb[1]), txt, font=f, fill=colour)
        if tw > w:
            d.rectangle([x, y, x + w, y + h], outline=(255, 0, 0, 255))   # does not fit

    def draw_entry(self, img, entry, origin, param_name=None):
        cd = entry["componentData"]
        b = entry["bounds"]
        if not visible_by_default(b):
            return
        x0, y0, w, h = rect(b["bounds"])
        x0, y0 = x0 + origin[0], y0 + origin[1]
        typ, data = cd["type"], cd["data"]
        remap = {m["key"]: m["value"] for m in entry["handle remapping"]["map"]}
        d = ImageDraw.Draw(img)
        if typ in self.defs:
            pname = self.params[int(remap["Data"].split()[1])]["name"] if "Data" in remap else None
            for child in self.defs[typ]["componentsData"]:
                self.draw_entry(img, child, (x0, y0), pname)
        elif typ == "Label":
            kind = data["type"]
            ts = data["textStyle"]
            txt = data.get("text") if kind == "Text" else (param_name or "NAME") if kind == "Name" else "0.0"
            colour = argb(ts["colour"]) or (255, 255, 255, 255)
            self.text(img, (x0, y0, w, h), txt, ts["font"]["style"], ts["font"]["height"], colour, ts["justification"], ts["case"])
        elif typ in ("Knob", "Image"):
            key = "filmStrip" if typ == "Knob" else "image"
            name = data.get(key)
            frames = data.get("numFrames", 1) if data.get("knobType", data.get("imageType")) in ("FilmStrip",) else 1
            im = self.image(name, frames) if name else None
            if im is not None:
                img.alpha_composite(im, (x0, y0))
        elif typ == "Button":
            im = self.image(data.get("offImage"))
            if im is not None:
                img.alpha_composite(im, (x0, y0))
        elif typ == "Slider":
            d.rounded_rectangle([x0, y0, x0 + w, y0 + h], 5, outline=(106, 106, 108, 255), fill=(38, 37, 40, 255))
            name = (param_name or "").upper()
            d.text((x0 + 8, y0 + 4), name, font=font("Bold", 22), fill=(180, 180, 184, 255))
        elif typ in ("Envelope",):
            d.rectangle([x0, y0, x0 + w, y0 + h], outline=(211, 17, 69, 255))
            d.text((x0 + 8, y0 + 4), "ENVELOPE", font=font("Bold", 20), fill=(211, 17, 69, 255))

    def render(self, tab_name):
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == tab_name)
        img = Image.new("RGBA", (TAB_W, TAB_H), (30, 29, 30, 255))
        for entry in tab["componentDefinition"]["componentsData"]:
            self.draw_entry(img, entry, (0, 0))
        d = ImageDraw.Draw(img)
        for q in tab["qlinkBoundsData"]:
            x, y, w, h = rect(q)
            d.rectangle([x, y, x + w, y + h], outline=(242, 120, 0, 255))
        return img


def main(argv=None):
    ap = argparse.ArgumentParser(description="Sketch of tabs of a generated MPC skin.")
    ap.add_argument("--skins", required=True)
    ap.add_argument("--params", required=True)
    ap.add_argument("--tab", action="append", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    sketch = Sketch(args.skins, json.loads(Path(args.params).read_text(encoding="utf-8"))["params"])
    for tab in args.tab:
        if len(args.tab) == 1 and args.out.endswith(".png"):
            out = Path(args.out)
        else:
            out = Path(args.out) / ("preview-%s.png" % "".join(c if c.isalnum() else "-" for c in tab).strip("-"))
        out.parent.mkdir(parents=True, exist_ok=True)
        sketch.render(tab).convert("RGB").save(out)
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
