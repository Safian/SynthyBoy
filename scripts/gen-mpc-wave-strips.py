#!/usr/bin/env python3
# SynthyBoy - renders the wavetable pictures of the MPC skin (one PNG film strip per built-in wavetable of oscN.wavetable).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 scripts/gen-mpc-wave-strips.py [--out DIR] [--dump FILE]
#   --out   where the PNGs go (default scripts/mpc-skin-extras/osc-wave-strips, the committed assets copied by 20_osc.py)
#   --dump  the output of tools/mpc-wave-dump/wt_dump.cpp; without it the tool is compiled with the host C++ compiler and run
#
# The waves are the frames of the own engine's built-in tables (core/engine-own/osc/wavetable_bank.cpp, the engine of the MPC build),
# every harmonic kept (level 0), as the tool wt_dump writes them. Nothing is drawn by hand. Needs numpy and Pillow (not needed to build the plugin:
# the committed PNGs are copied by scripts/mpc-skin-extras/20_osc.py). Output is deterministic (same input, same bytes).
#
# Picture: WIDTH x HEIGHT pixels per frame, a strip is the frames stacked vertically (frame 0 on top). Frames per strip: 1 for the
# single cycle tables (the engine has one frame), STRIP_FRAMES for the morphing tables, 1 for the "Preset table" (neutral picture,
# the table of a preset is its own and is not known to the skin).
#
# Frame positions. The MPC shows frame floor(v * (n - 1)) of an n frame strip for the parameter value v (0 .. 1), measured on a Key 61:
# frame 0 for [0, 1/(n-1)), ..., the last frame only at v = 1. So frame i < n - 1 shows the table at the middle of its interval,
# (i + 0.5) / (n - 1), and the last frame shows the table at 1.0; this covers the range evenly (error at most half a step).
# The picture between two engine frames is the linear mix of the two cycles.

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "scripts" / "mpc-skin-extras" / "osc-wave-strips"
DUMP_SRC = ROOT / "tools" / "mpc-wave-dump" / "wt_dump.cpp"
ENGINE_DIR = ROOT / "core" / "engine-own"
PARAMS_JSON = ROOT / "docs" / "params.json"

WIDTH, HEIGHT = 512, 240
STRIP_FRAMES = 16          # morphing tables: 15 intervals of the frame parameter + the exact end
PRESET_TABLE = 13          # oscN.wavetable choice "Preset table"
CYCLE = 2048
SUPERSAMPLE = 4
BG, GRID, RED, FILL, TEXT = (30, 29, 30, 255), (66, 66, 68, 255), (211, 17, 69, 255), (211, 17, 69, 70), (130, 130, 134, 255)
MARGIN = 30                # vertical space above and below the +-1 level


def file_name(choice):
    return "osc_wt_%02d.png" % choice


def table_names():
    import json
    params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
    return next(p for p in params if p["key"] == "osc1.wavetable")["choices"]


def build_dump():
    """Compiles tools/mpc-wave-dump/wt_dump.cpp with the host C++ compiler, runs it, returns its output."""
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if cxx is None:
        raise SystemExit("gen-mpc-wave-strips.py: no host C++ compiler (c++/clang++/g++) found; pass --dump FILE")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "wt_dump"
        cmd = [cxx, "-std=c++17", "-O2", "-pthread", "-I", str(ROOT / "core" / "include"), "-I", str(ENGINE_DIR), str(DUMP_SRC),
               str(ENGINE_DIR / "osc" / "wavetable_bank.cpp"), "-o", str(exe)]
        subprocess.run(cmd, check=True)
        return subprocess.run([str(exe)], check=True, capture_output=True).stdout


def read_dump(data):
    """-> list per choice: float32 array (frames, 2048), or None for a choice without a built-in table."""
    tables, pos = [], 0
    while pos < len(data):
        (frames,) = struct.unpack_from("<i", data, pos)
        pos += 4
        if frames == 0:
            tables.append(None)
            continue
        n = frames * CYCLE
        tables.append(np.frombuffer(data, dtype="<f4", count=n, offset=pos).reshape(frames, CYCLE).copy())
        pos += n * 4
    return tables


def frame_position(i, n):
    """Position 0 .. 1 in the table that frame i of an n frame strip shows (see the header)."""
    if n == 1:
        return 0.0
    return 1.0 if i == n - 1 else (i + 0.5) / (n - 1)


def cycle_at(frames, t):
    """The cycle at position t (0 .. 1) of a table of several frames: the linear mix of the two neighbouring frames."""
    if len(frames) == 1:
        return frames[0]
    x = t * (len(frames) - 1)
    lo = min(int(np.floor(x)), len(frames) - 2)
    frac = x - lo
    return frames[lo] * (1.0 - frac) + frames[lo + 1] * frac


def _font(size):
    return ImageFont.load_default(size)


def render_frame(cycle, caption, width=WIDTH, height=HEIGHT):
    """One picture: the cycle (2048 floats, -1 .. 1) as a red line with a faint fill to the zero line, the caption small at the top left."""
    s = SUPERSAMPLE
    w, h = width * s, height * s
    top, bottom = MARGIN * s, h - MARGIN * s
    mid = (top + bottom) / 2.0
    half = (bottom - top) / 2.0
    xs = np.arange(CYCLE + 1) * (w / CYCLE)
    ys = mid - np.append(cycle, cycle[0]).astype(np.float64) * half
    pts = list(zip(xs.tolist(), ys.tolist()))

    img = Image.new("RGBA", (w, h), BG)
    d = ImageDraw.Draw(img)
    for level in (-1.0, 0.0, 1.0):
        y = mid - level * half
        d.line([(0, y), (w, y)], fill=GRID, width=s)
    for k in range(1, 4):
        x = w * k / 4.0
        d.line([(x, top), (x, bottom)], fill=GRID, width=s)
    fill = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(fill).polygon(pts + [(w, mid), (0, mid)], fill=FILL)
    img = Image.alpha_composite(img, fill)
    d = ImageDraw.Draw(img)
    d.line(pts, fill=RED, width=3 * s, joint="curve")
    img = img.resize((width, height), Image.LANCZOS)
    ImageDraw.Draw(img).text((10, 6), caption, font=_font(16), fill=TEXT)
    return img


def render_preset(width=WIDTH, height=HEIGHT):
    """Neutral picture of the "Preset table": the skin cannot know the table of the preset."""
    img = Image.new("RGBA", (width, height), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, width - 1, height - 1], outline=GRID)
    for text, size, y, colour in (("PRESET", 56, height // 2 - 50, RED), ("the wavetable of the loaded preset", 18, height // 2 + 20, TEXT)):
        font = _font(size)
        box = d.textbbox((0, 0), text, font=font)
        d.text(((width - (box[2] - box[0])) / 2 - box[0], y), text, font=font, fill=colour)
    return img


def render_strip(frames, caption, n_out):
    """A vertical strip of n_out pictures of the table `frames` (float array (frames, 2048))."""
    strip = Image.new("RGBA", (WIDTH, HEIGHT * n_out), BG)
    for i in range(n_out):
        strip.paste(render_frame(cycle_at(frames, frame_position(i, n_out)), caption), (0, HEIGHT * i))
    return strip


def strips(dump):
    """{choice: PIL strip image} for choices 0 .. len(dump) - 1, plus {choice: frames}."""
    names = table_names()
    tables = read_dump(dump)
    if len(tables) != len(names):
        raise SystemExit("gen-mpc-wave-strips.py: the dump has %d tables, params.json has %d choices" % (len(tables), len(names)))
    out = {}
    for choice, frames in enumerate(tables):
        if frames is None:
            if choice != PRESET_TABLE:
                raise SystemExit("gen-mpc-wave-strips.py: choice %d (%s) has no built-in table" % (choice, names[choice]))
            out[choice] = render_preset()
        else:
            n = STRIP_FRAMES if len(frames) > 1 else 1
            out[choice] = render_strip(frames, names[choice].upper(), n)
    return out


def write_all(out_dir, dump=None):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("osc_wt_*.png"):
        old.unlink()
    result = strips(dump if dump is not None else build_dump())
    for choice, img in result.items():
        img.save(out_dir / file_name(choice), optimize=False, compress_level=9)
    return result


def main(argv=None):
    ap = argparse.ArgumentParser(description="Render the oscillator wavetable film strips of the MPC skin.")
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--dump", default=None, help="output of wt_dump (default: compile and run tools/mpc-wave-dump/wt_dump.cpp)")
    args = ap.parse_args(argv)
    dump = Path(args.dump).read_bytes() if args.dump else None
    result = write_all(args.out, dump)
    disk = decoded = 0
    for choice, img in result.items():
        size = (Path(args.out) / file_name(choice)).stat().st_size
        disk += size
        decoded += img.width * img.height * 4
        print("%s %dx%d frames=%d disk=%d" % (file_name(choice), img.width, img.height, img.height // HEIGHT, size))
    print("total: disk %d bytes, decoded %d bytes (%.1f MiB)" % (disk, decoded, decoded / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
