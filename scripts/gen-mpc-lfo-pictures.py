#!/usr/bin/env python3
# SynthyBoy - renders the large LFO pictures of the MPC skin (the shape pictures and the cursor strip). Deterministic; needs numpy and Pillow.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/gen-mpc-lfo-pictures.py [--out DIR] [--cxx c++]   (default DIR: scripts/mpc-skin-extras/lfo-pictures, the committed assets copied by 30_lfo.py)
#
# Writes into DIR (WIDTH x HEIGHT pixels, 600 x 240):
#   lfo_shape_00.png .. lfo_shape_11.png   one picture of each engine shape (lfoN.shape choices 0 .. 11): the curve of one cycle, a faint fill to the zero line, a grid, the
#                                          name of the shape at the top left. Nothing is drawn by hand: the curves are the engine's own, scripts/lfo-shape-dump.cpp is
#                                          compiled on this machine against core/engine-own/env_lfo/lfo.h (lfoShape()) and its samples are plotted.
#   lfo_shape_12.png                       the picture of the Custom shape (choice 12): no fixed curve, a CUSTOM marker
#   lfo_cursor.png                         the cursor film strip, NUM_FRAMES frames of WIDTH x HEIGHT stacked vertically on a TRANSPARENT background: frame 0 is empty (no voice
#                                          sounds), frame k (1 .. CURSOR_STEPS) has only a vertical line at the centre of cell k - 1 of the cycle. One file for every LFO and
#                                          every shape: 30_lfo.py lays it over the shape picture and binds it to the display parameter "LFO N Cursor".
# Decoded size (the host unpacks film strips and pictures, width x height x 4 bytes per frame): (13 + 33) * 600 * 240 * 4 = 26 496 000 bytes (docs/MPC.md).
# Only this development tool needs numpy, Pillow and a C++ compiler ($CXX or `c++`; --cxx overrides); the build copies the committed PNGs.
# Exit 0 ok, 1 on error (message on stderr).

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
TOOL_SOURCE = ROOT / "scripts" / "lfo-shape-dump.cpp"
PARAMS_JSON = ROOT / "docs" / "params.json"
DEFAULT_OUT = ROOT / "scripts" / "mpc-skin-extras" / "lfo-pictures"

WIDTH, HEIGHT = 600, 240
NUM_SHAPES = 12          # engine shapes (kNumLfoShapes); shape choice 12 is the Custom marker
CUSTOM_CHOICE = 12
CURSOR_STEPS = 32        # cursor cells per cycle; must equal kLfoLiveSteps of shell-mpc/lfo_live.h
NUM_FRAMES = CURSOR_STEPS + 1
CURSOR_NAME = "lfo_cursor.png"
SAMPLES = 480            # samples of one cycle from the engine; a multiple of CURSOR_STEPS: a cell is SAMPLES / CURSOR_STEPS samples wide
SUPERSAMPLE = 4
BG, GRID, RED, FILL, TEXT = (30, 29, 30, 255), (66, 66, 68, 255), (211, 17, 69, 255), (211, 17, 69, 70), (130, 130, 134, 255)
CURSOR = (242, 242, 242, 235)
PLOT_X0, PLOT_X1 = 24, WIDTH - 24     # the cycle spans these columns (the edge of the cell grid)
PLOT_TOP, PLOT_BOTTOM = 34, HEIGHT - 26   # +1 is the top row, -1 the bottom row of the curve
CURSOR_OVERSHOOT = 8     # the cursor line reaches this far above and below the plot
CURSOR_WIDTH = 3


def shape_name(choice):
    return "lfo_shape_%02d.png" % choice


class GenError(Exception):
    pass


def shape_choices():
    params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
    return next(p for p in params if p["key"] == "lfo1.shape")["choices"]


def dump_engine_shapes(cxx=None, samples=SAMPLES):
    """Compile scripts/lfo-shape-dump.cpp against the engine header and return {shape: [values]} for shapes 0 .. 11."""
    cxx = cxx or os.environ.get("CXX") or "c++"
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "lfo-shape-dump"
        cmd = [cxx, "-std=c++17", "-O0", "-I" + str(ROOT / "core" / "engine-own"), "-I" + str(ROOT / "core" / "include"), str(TOOL_SOURCE), "-o", str(exe)]
        try:
            res = subprocess.run(cmd, capture_output=True, text=True)
        except OSError as e:
            raise GenError("cannot run the C++ compiler '%s': %s" % (cxx, e))
        if res.returncode != 0:
            raise GenError("compiling %s failed:\n%s" % (TOOL_SOURCE.name, res.stderr))
        res = subprocess.run([str(exe), str(samples)], capture_output=True, text=True)
        if res.returncode != 0:
            raise GenError("lfo-shape-dump failed: " + res.stderr)
    shapes = {}
    for line in res.stdout.splitlines():
        parts = line.split()
        shapes[int(parts[0])] = [float(v) for v in parts[1:]]
    if sorted(shapes) != list(range(NUM_SHAPES)) or any(len(v) != samples for v in shapes.values()):
        raise GenError("lfo-shape-dump gave an unexpected table (shapes %s)" % sorted(shapes))
    return shapes


def x_of(p):
    """Column (pixel units, float) of the cycle position p in 0 .. 1."""
    return PLOT_X0 + p * (PLOT_X1 - PLOT_X0)


def y_of(v):
    return PLOT_TOP + (1.0 - v) * 0.5 * (PLOT_BOTTOM - PLOT_TOP)


def cell_centre(step):
    """Column of the centre of cell step - 1 (step 1 .. CURSOR_STEPS)."""
    return x_of((step - 0.5) / CURSOR_STEPS)


def _font(size):
    return ImageFont.load_default(size)


def _canvas(bg):
    return Image.new("RGBA", (WIDTH * SUPERSAMPLE, HEIGHT * SUPERSAMPLE), bg)


def _grid(d):
    s = SUPERSAMPLE
    for level in (-1.0, 0.0, 1.0):
        y = y_of(level) * s
        d.line([(PLOT_X0 * s, y), (PLOT_X1 * s, y)], fill=GRID, width=s)
    for k in range(5):
        x = x_of(k / 4.0) * s
        d.line([(x, PLOT_TOP * s), (x, PLOT_BOTTOM * s)], fill=GRID, width=s)


def render_shape(values, caption):
    """The picture of one shape: the engine's samples of one cycle as a red line over a faint fill, grid, caption."""
    s = SUPERSAMPLE
    n = len(values)
    pts = [(x_of((i + 0.5) / n) * s, y_of(v) * s) for i, v in enumerate(values)]
    img = _canvas(BG)
    d = ImageDraw.Draw(img)
    _grid(d)
    fill = Image.new("RGBA", img.size, (0, 0, 0, 0))
    zero = y_of(0.0) * s
    ImageDraw.Draw(fill).polygon(pts + [(pts[-1][0], zero), (pts[0][0], zero)], fill=FILL)
    img = Image.alpha_composite(img, fill)
    ImageDraw.Draw(img).line(pts, fill=RED, width=4 * s, joint="curve")
    img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
    ImageDraw.Draw(img).text((10, 6), caption.upper(), font=_font(16), fill=TEXT)
    return img


def render_custom():
    """The picture of the Custom shape: no fixed curve, the grid and a marker."""
    s = SUPERSAMPLE
    img = _canvas(BG)
    _grid(ImageDraw.Draw(img))
    img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
    d = ImageDraw.Draw(img)
    for text, size, y, colour in (("CUSTOM", 56, HEIGHT // 2 - 50, RED), ("the shape is drawn in the LFO editor", 18, HEIGHT // 2 + 20, TEXT)):
        font = _font(size)
        box = d.textbbox((0, 0), text, font=font)
        d.text(((WIDTH - (box[2] - box[0])) / 2 - box[0], y), text, font=font, fill=colour)
    return img


def render_cursor_frame(step):
    """One frame of the cursor strip: transparent, with a vertical line at the centre of cell step - 1 (step 0: nothing)."""
    s = SUPERSAMPLE
    img = Image.new("RGBA", (WIDTH * s, HEIGHT * s), (0, 0, 0, 0))
    if step:
        x = cell_centre(step) * s
        top, bottom = (PLOT_TOP - CURSOR_OVERSHOOT) * s, (PLOT_BOTTOM + CURSOR_OVERSHOOT) * s
        ImageDraw.Draw(img).line([(x, top), (x, bottom)], fill=CURSOR, width=CURSOR_WIDTH * s)
    # resample the colour only where the line is: a transparent pixel keeps the colour of the line, so no dark fringe appears on the dark panel
    arr = np.asarray(img).astype(np.float64)
    alpha = arr[..., 3:4] / 255.0
    small_a = alpha.reshape(HEIGHT, s, WIDTH, s, 1).mean(axis=(1, 3))
    small_rgb = (arr[..., :3] * alpha).reshape(HEIGHT, s, WIDTH, s, 3).sum(axis=(1, 3)) / np.maximum(alpha.reshape(HEIGHT, s, WIDTH, s, 1).sum(axis=(1, 3)), 1e-9)
    out = np.concatenate([np.rint(small_rgb), np.rint(small_a * 255.0)], axis=-1).astype(np.uint8)
    out[out[..., 3] == 0, :3] = 0
    return Image.fromarray(out, "RGBA")


def render_cursor_strip():
    strip = Image.new("RGBA", (WIDTH, HEIGHT * NUM_FRAMES), (0, 0, 0, 0))
    for step in range(NUM_FRAMES):
        strip.paste(render_cursor_frame(step), (0, HEIGHT * step))
    return strip


def pictures(shapes):
    """{file name: PIL image}: 13 shape pictures and the cursor strip."""
    names = shape_choices()
    if len(names) != NUM_SHAPES + 1:
        raise GenError("lfo1.shape has %d choices, the pictures cover %d + Custom" % (len(names), NUM_SHAPES))
    out = {}
    for choice in range(NUM_SHAPES):
        out[shape_name(choice)] = render_shape(shapes[choice], names[choice])
    out[shape_name(CUSTOM_CHOICE)] = render_custom()
    out[CURSOR_NAME] = render_cursor_strip()
    return out


def generate(out_dir, cxx=None, shapes=None):
    """Writes the pictures into out_dir; returns the list of the paths."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for old in list(out.glob("lfo_shape_*.png")) + list(out.glob("lfo_cursor.png")):
        old.unlink()
    paths = []
    for name, img in pictures(shapes if shapes is not None else dump_engine_shapes(cxx)).items():
        img.save(out / name, optimize=False, compress_level=9)
        paths.append(out / name)
    return paths


def decoded_bytes():
    return (NUM_SHAPES + 1 + NUM_FRAMES) * WIDTH * HEIGHT * 4


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render the large LFO pictures of the MPC skin from the engine's LFO code.")
    ap.add_argument("--out", default=str(DEFAULT_OUT), help="output directory (default: the committed assets)")
    ap.add_argument("--cxx", help="C++ compiler (default $CXX or c++)")
    args = ap.parse_args(argv)
    try:
        paths = generate(args.out, args.cxx)
    except GenError as e:
        print("gen-mpc-lfo-pictures.py: " + str(e), file=sys.stderr)
        return 1
    for path in paths:
        print("%s %d bytes" % (path, path.stat().st_size))
    print("decoded size: %d bytes (%.1f MiB)" % (decoded_bytes(), decoded_bytes() / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
