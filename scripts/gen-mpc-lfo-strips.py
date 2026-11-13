#!/usr/bin/env python3
# SynthyBoy - renders the LFO shape film strip of the MPC skin (stdlib only, deterministic).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/gen-mpc-lfo-strips.py [--out DIR] [--cxx c++]   (default DIR: scripts/mpc-skin-extras/lfo-strips, the committed assets copied by 30_lfo.py)
#
# Writes into DIR the pictures of the LFO display of the skin (scripts/mpc-skin-extras/30_lfo.py):
#   lfo_live_00.png .. lfo_live_11.png   one film strip per engine shape (lfoN.shape choices 0 .. 11): FRAME_W x (FRAME_H * NUM_FRAMES), frame 0 = the
#                                        shape without a cursor (idle), frame k (1 .. CURSOR_STEPS) = the shape with the cursor in cell k - 1 of the cycle
#   lfo_custom.png                       one FRAME_W x FRAME_H picture with a "CUSTOM" marker (lfoN.shape choice 12, no fixed shape, no cursor)
# The curves are the engine's: scripts/lfo-shape-dump.cpp is compiled on this machine against core/engine-own/env_lfo/lfo.h (the engine's own lfoShape())
# and its samples are plotted; nothing is approximated here. Decoded size of the 12 strips: 12 * NUM_FRAMES * FRAME_W * FRAME_H * 4 bytes (see docs/MPC.md).
# Only this development tool needs a compiler ($CXX or `c++`; --cxx overrides); the build copies the committed PNGs. Exit 0 ok, 1 on error (message on stderr).

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL_SOURCE = ROOT / "scripts" / "lfo-shape-dump.cpp"
CUSTOM_NAME = "lfo_custom.png"

FRAME_W, FRAME_H = 208, 38
NUM_SHAPES = 12          # engine shapes (kNumLfoShapes); shape choice 12 is the Custom marker
CURSOR_STEPS = 32        # cursor cells per cycle; must equal kLfoLiveSteps of shell-mpc/lfo_live.h
NUM_FRAMES = CURSOR_STEPS + 1
SAMPLES = 480            # a multiple of 2 * CURSOR_STEPS: the cursor cell centres lie between two samples


def strip_name(shape):
    return "lfo_live_%02d.png" % shape

# colours (r, g, b); page background is ff1e1d1e, accent ffd31145 as in the SDK kit
PANEL = (0x26, 0x25, 0x28)
BORDER = (0x43, 0x43, 0x47)
GRID = (0x3c, 0x3c, 0x40)
CURVE = (0xd3, 0x11, 0x45)
TEXT = (0xb9, 0xb9, 0xbd)
CURSOR = (0xf2, 0xf2, 0xf2)
PLOT_X0, PLOT_X1 = 6, FRAME_W - 7        # curve spans these columns (inclusive pixel centres)
PLOT_Y_TOP, PLOT_Y_BOT = 5, FRAME_H - 6  # +1 maps to the top row, -1 to the bottom row
HALF_WIDTH = 1.0                         # curve line is 2 px wide

FONT = {  # 5 x 7
    "C": ("01110", "10001", "10000", "10000", "10000", "10001", "01110"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
}


class GenError(Exception):
    pass


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


def _seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    qx, qy = ax + t * dx, ay + t * dy
    return ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5


def _blend(dst, src, a):
    return tuple(int(round(d + (s - d) * a)) for d, s in zip(dst, src))


def _panel():
    """FRAME_H rows of FRAME_W RGB tuples: panel, border, dashed zero line."""
    px = [[PANEL] * FRAME_W for _ in range(FRAME_H)]
    for x in range(FRAME_W):
        px[0][x] = px[FRAME_H - 1][x] = BORDER
    for y in range(FRAME_H):
        px[y][0] = px[y][FRAME_W - 1] = BORDER
    mid = (PLOT_Y_TOP + PLOT_Y_BOT) // 2
    for x in range(PLOT_X0, PLOT_X1 + 1):
        if (x // 3) % 2 == 0:
            px[mid][x] = GRID
    return px


def _x_of(p):
    return PLOT_X0 + p * (PLOT_X1 - PLOT_X0)


def _y_of(v):
    return PLOT_Y_TOP + (1.0 - v) * 0.5 * (PLOT_Y_BOT - PLOT_Y_TOP)


def render_shape_frame(values):
    px = _panel()
    n = len(values)
    pts = [(_x_of(i / (n - 1)), _y_of(v)) for i, v in enumerate(values)]
    segs = list(zip(pts, pts[1:]))
    reach = HALF_WIDTH + 1.5
    for y in range(1, FRAME_H - 1):
        for x in range(1, FRAME_W - 1):
            cx, cy = x + 0.5, y + 0.5
            best = None
            for (ax, ay), (bx, by) in segs:
                if max(ax, bx) < cx - reach or min(ax, bx) > cx + reach:
                    continue
                if max(ay, by) < cy - reach or min(ay, by) > cy + reach:
                    continue
                d = _seg_dist(cx, cy, ax + 0.5, ay + 0.5, bx + 0.5, by + 0.5)
                if best is None or d < best:
                    best = d
            if best is not None:
                cov = max(0.0, min(1.0, HALF_WIDTH + 0.5 - best))
                if cov > 0:
                    px[y][x] = _blend(px[y][x], CURVE, cov)
    return px


def with_cursor(base, values, step):
    """Copy of the frame `base` with the cursor of cell step - 1: a thin vertical line over the plot and a dot on the curve."""
    px = [list(row) for row in base]
    p = (step - 0.5) / CURSOR_STEPS                      # centre of the cell
    f = p * len(values) - 0.5                            # between two engine samples (the cell centre is never on a jump)
    i = int(f)
    v = values[i] + (values[i + 1] - values[i]) * (f - i)
    cx, cy = _x_of(p) + 0.5, _y_of(v) + 0.5
    for y in range(1, FRAME_H - 1):
        for x in range(1, FRAME_W - 1):
            line = max(0.0, min(1.0, 0.75 - abs(x + 0.5 - cx))) * 0.55
            dot = max(0.0, min(1.0, 3.5 - ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2) ** 0.5))
            if dot > 0:
                px[y][x] = _blend(px[y][x], CURSOR, dot)
            elif line > 0:
                px[y][x] = _blend(px[y][x], CURSOR, line)
    return px


def render_custom_frame():
    px = _panel()
    scale, gap, word = 3, 3, "CUSTOM"
    width = len(word) * 5 * scale + (len(word) - 1) * gap
    height = 7 * scale
    x0, y0 = (FRAME_W - width) // 2, (FRAME_H - height) // 2
    for k, ch in enumerate(word):
        for ry, row in enumerate(FONT[ch]):
            for rx, bit in enumerate(row):
                if bit == "1":
                    for sy in range(scale):
                        for sx in range(scale):
                            px[y0 + ry * scale + sy][x0 + k * (5 * scale + gap) + rx * scale + sx] = TEXT
    return px


def _rows(px):
    return [b"".join(bytes(c) + b"\xff" for c in line) for line in px]


def render_strip(values):
    """PNG rows of the film strip of one shape: NUM_FRAMES frames (idle, then the cursor cells) stacked vertically."""
    base = render_shape_frame(values)
    rows = _rows(base)
    for step in range(1, CURSOR_STEPS + 1):
        rows.extend(_rows(with_cursor(base, values, step)))
    return rows


def write_png(path, width, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, len(rows), 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(png)


def generate(out_dir, cxx=None):
    """Writes the 12 strips and the Custom picture into out_dir; returns the list of the paths."""
    out = Path(out_dir)
    shapes = dump_engine_shapes(cxx)
    paths = []
    for shape in range(NUM_SHAPES):
        path = out / strip_name(shape)
        write_png(path, FRAME_W, render_strip(shapes[shape]))
        paths.append(path)
    write_png(out / CUSTOM_NAME, FRAME_W, _rows(render_custom_frame()))
    paths.append(out / CUSTOM_NAME)
    return paths


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render the MPC skin's LFO shape film strip from the engine's LFO code.")
    ap.add_argument("--out", default=str(ROOT / "scripts" / "mpc-skin-extras" / "lfo-strips"), help="output directory (default: the committed assets)")
    ap.add_argument("--cxx", help="C++ compiler (default $CXX or c++)")
    args = ap.parse_args(argv)
    try:
        for path in generate(args.out, args.cxx):
            print(path)
    except GenError as e:
        print("gen-mpc-lfo-strips.py: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
