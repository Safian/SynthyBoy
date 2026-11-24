#!/usr/bin/env python3
"""Generates shell-mpc/overlay/sdf_font_data.h: the signed distance field atlas of the overlay UI font.

Font: Titillium Web SemiBold (SIL OFL 1.1), the official release of the Google Fonts repository, kept with its licence in shell-mpc/overlay/fonts/.
One 8-bit atlas, glyphs in a shelf packing, each glyph padded by SPREAD pixels. A value v of the atlas means the signed distance
d = (v / 255 - 0.5) * 2 * SPREAD atlas pixels to the outline (positive inside). A renderer at `size` pixels per em makes the coverage
clamp (0.5 + d * size / EM, 0, 1): the same formula on the GPU (the helper's fragment shader) and on the CPU (scene_cpu.h).

Characters: ASCII 32 .. 126, the Hungarian letters, the degree sign, a few typographic signs and the arrows / triangles the UI uses (drawn here, the font has none).
Usage: scripts/gen-overlay-font.py [--check]   (--check: exit 1 when the committed header differs from what this script makes)
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(ROOT, "shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf")
OUT = os.path.join(ROOT, "shell-mpc/overlay/sdf_font_data.h")

EM = 40            # atlas pixels per em
SPREAD = 5         # distance range, atlas pixels each side of the outline
SS = 8             # supersampling of the outline before the distance transform
ATLAS_W = 512

CHARS = [chr(c) for c in range(32, 127)] + list("áéíóöőúüűÁÉÍÓÖŐÚÜŰ") + list("°±×·–—…")
ARROWS = {"←": "left", "→": "right", "↑": "up", "↓": "down", "▲": "tri_up", "▼": "tri_down", "◀": "tri_left", "▶": "tri_right"}


def draw_symbol(kind, big):
    """A symbol on a transparent supersampled canvas; returns (image, advance in em). Sizes in em: cap height 0.685."""
    s = SS * EM
    img = Image.new("L", (int(s * 1.2), int(s * 1.2)), 0)
    d = ImageDraw.Draw(img)
    ox, base = int(0.1 * s), int(0.95 * s)       # the pen origin and the baseline inside the canvas
    cap = 0.685 * s
    mid = base - cap * 0.5
    w = 0.075 * s                                # stroke of the arrows
    if kind in ("left", "right", "up", "down"):
        half = 0.30 * s
        head = 0.19 * s
        cx = ox + 0.36 * s
        pts = {"right": [(-half, 0), (half, 0)], "left": [(half, 0), (-half, 0)], "up": [(0, half), (0, -half)], "down": [(0, -half), (0, half)]}[kind]
        (x0, y0), (x1, y1) = pts
        # shaft + head as a polygon
        dx, dy = (x1 - x0), (y1 - y0)
        l = (dx * dx + dy * dy) ** 0.5
        ux, uy = dx / l, dy / l
        px, py = -uy, ux
        def P(a, b):
            return (cx + x0 + ux * a + px * b, mid + y0 + uy * a + py * b)
        d.polygon([P(0, -w / 2), P(l - head * 0.7, -w / 2), P(l - head * 0.7, -head * 0.62), P(l + 0.0, 0), P(l - head * 0.7, head * 0.62), P(l - head * 0.7, w / 2), P(0, w / 2)], fill=255)
        adv = 0.72
    else:
        r = 0.27 * s
        cx = ox + 0.30 * s
        tri = {"tri_up": [(0, -r), (r * 0.95, r * 0.8), (-r * 0.95, r * 0.8)], "tri_down": [(0, r), (r * 0.95, -r * 0.8), (-r * 0.95, -r * 0.8)],
               "tri_left": [(-r, 0), (r * 0.8, -r * 0.95), (r * 0.8, r * 0.95)], "tri_right": [(r, 0), (-r * 0.8, -r * 0.95), (-r * 0.8, r * 0.95)]}[kind]
        d.polygon([(cx + x, mid + y) for x, y in tri], fill=255)
        adv = 0.62
    return img, ox, base, adv


def sdf_of(mask_big):
    """mask_big: boolean array at SS x. Returns the distance (in atlas pixels, positive inside) sampled at the atlas pixel centres."""
    inside = ndimage.distance_transform_edt(mask_big)
    outside = ndimage.distance_transform_edt(~mask_big)
    sd = (inside - outside + (np.where(mask_big, -0.5, 0.5))) / SS      # half a supersample pixel: the edge is between pixels
    return sd


def make_glyph(ch, font):
    """Returns (bitmap uint8 h x w, left offset from the pen origin, top offset from the baseline (up positive), advance in em)."""
    if ch in ARROWS:
        img, ox, base, adv = draw_symbol(ARROWS[ch], True)
        mask = np.array(img) > 127
    else:
        s = SS * EM
        bigfont = font.font_variant(size=s)
        adv = bigfont.getlength(ch) / s
        img = Image.new("L", (int(s * 2.0), int(s * 1.8)), 0)
        ox, base = int(0.3 * s), int(1.2 * s)
        ImageDraw.Draw(img).text((ox, base), ch, font=bigfont, fill=255, anchor="ls")
        mask = np.array(img) > 127
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None, 0, 0, adv
    pad = SPREAD * SS
    x0 = (int(xs.min()) - pad) // SS * SS
    y0 = (int(ys.min()) - pad) // SS * SS
    x1 = -(-(int(xs.max()) + 1 + pad) // SS) * SS
    y1 = -(-(int(ys.max()) + 1 + pad) // SS) * SS
    big = np.zeros((y1 - y0, x1 - x0), dtype=bool)
    sy0, sx0 = max(y0, 0), max(x0, 0)
    sy1, sx1 = min(y1, mask.shape[0]), min(x1, mask.shape[1])
    big[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0] = mask[sy0:sy1, sx0:sx1]
    sd = sdf_of(big)
    h, w = big.shape[0] // SS, big.shape[1] // SS
    # distance at the atlas pixel centres: the mean of the central 2x2 supersamples
    c = SS // 2
    cen = sd.reshape(h, SS, w, SS)[:, c - 1:c + 1, :, c - 1:c + 1].mean(axis=(1, 3))
    v = np.clip(0.5 + cen / (2 * SPREAD), 0.0, 1.0)
    bitmap = np.round(v * 255).astype(np.uint8)
    left = (x0 - ox) / SS            # atlas pixels from the pen origin to the bitmap's left edge
    top = (base - y0) / SS           # atlas pixels from the baseline up to the bitmap's top edge
    return bitmap, left, top, adv


def build():
    font = ImageFont.truetype(FONT, 100)
    glyphs = []
    for ch in CHARS + list(ARROWS):
        bm, left, top, adv = make_glyph(ch, font)
        glyphs.append((ord(ch), bm, left, top, adv))
    glyphs.sort(key=lambda g: g[0])
    # shelf packing, tallest first inside a row is not needed: rows by sorted height keeps the atlas compact
    order = sorted(range(len(glyphs)), key=lambda i: -(glyphs[i][1].shape[0] if glyphs[i][1] is not None else 0))
    pos = {}
    x = y = rowh = 0
    for i in order:
        bm = glyphs[i][1]
        if bm is None:
            pos[i] = (0, 0)
            continue
        h, w = bm.shape
        if x + w + 1 > ATLAS_W:
            x, y, rowh = 0, y + rowh + 1, 0
        pos[i] = (x, y)
        x += w + 1
        rowh = max(rowh, h)
    atlas_h = y + rowh + 1
    atlas = np.zeros((atlas_h, ATLAS_W), dtype=np.uint8)
    atlas[:] = 0
    for i, (cp, bm, left, top, adv) in enumerate(glyphs):
        if bm is not None:
            gx, gy = pos[i]
            atlas[gy:gy + bm.shape[0], gx:gx + bm.shape[1]] = bm
    # metrics of the font
    from fontTools.ttLib import TTFont
    f = TTFont(FONT)
    upm = f["head"].unitsPerEm
    cap = f["OS/2"].sCapHeight / upm
    xh = f["OS/2"].sxHeight / upm
    asc = f["hhea"].ascent / upm
    desc = -f["hhea"].descent / upm
    # kerning is not used (the UI's strings are short labels): advances only
    lines = []
    lines.append("// SynthyBoy - MPC overlay: the signed distance field atlas of the UI font. GENERATED by scripts/gen-overlay-font.py from Titillium Web SemiBold (SIL OFL 1.1, shell-mpc/overlay/fonts/OFL.txt): do not edit.")
    lines.append("// Copyright (C) 2026 Safian Szabolcs (the generated tables); the font software is (c) 2009-2011 the Accademia di Belle Arti di Urbino and students of MA course of Visual design (OFL 1.1).")
    lines.append("// SPDX-License-Identifier: AGPL-3.0-or-later AND OFL-1.1")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("namespace synthyboy_mpc { namespace overlay { namespace sdf {")
    lines.append(f"constexpr int kAtlasW = {ATLAS_W}, kAtlasH = {atlas_h}, kEm = {EM}, kSpread = {SPREAD};   // atlas pixels; kEm atlas pixels = 1 em; the distance range is +- kSpread atlas pixels")
    lines.append(f"constexpr float kCapHeight = {cap:.4f}f, kXHeight = {xh:.4f}f, kAscent = {asc:.4f}f, kDescent = {desc:.4f}f;   // em")
    lines.append("struct GlyphData { uint32_t cp; uint16_t x, y, w, h; float left, top, advance; };   // x, y, w, h: the bitmap in the atlas (w = 0: no outline, a space); left: from the pen origin to the bitmap's left edge, top: from the baseline up to its top edge (atlas pixels); advance in em")
    lines.append(f"inline constexpr int kNumGlyphs = {len(glyphs)};")
    lines.append("inline constexpr GlyphData kGlyphs[kNumGlyphs] = {")
    for i, (cp, bm, left, top, adv) in enumerate(glyphs):
        gx, gy = pos[i]
        w = 0 if bm is None else bm.shape[1]
        h = 0 if bm is None else bm.shape[0]
        lines.append(f"    {{ 0x{cp:x}, {gx}, {gy}, {w}, {h}, {left:.3f}f, {top:.3f}f, {adv:.4f}f }},")
    lines.append("};")
    lines.append("inline const uint8_t kAtlas[kAtlasW * kAtlasH] = {")
    flat = atlas.reshape(-1)
    for k in range(0, len(flat), 64):
        lines.append(",".join(str(int(v)) for v in flat[k:k + 64]) + ",")
    lines.append("};")
    lines.append("}}} // namespace synthyboy_mpc::overlay::sdf")
    return "\n".join(lines) + "\n", atlas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--png", help="also write the atlas as a PNG")
    a = ap.parse_args()
    text, atlas = build()
    if a.png:
        Image.fromarray(atlas).save(a.png)
    if a.check:
        with open(OUT) as f:
            same = f.read() == text
        print("sdf_font_data.h is up to date" if same else "sdf_font_data.h DIFFERS from the generator output")
        return 0 if same else 1
    with open(OUT, "w") as f:
        f.write(text)
    print(f"wrote {OUT} ({len(text)} bytes, atlas {atlas.shape[1]}x{atlas.shape[0]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
