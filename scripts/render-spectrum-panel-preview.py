#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Renders compact and 1.5x taller spectrum panel preview artwork for SynthyBoy MPC.

Generates:
  1. docs/ui-components/komponensek/spectrum-panel-preview.png (unified comparison board, 1380 px width)
  2. docs/ui-components/komponensek/spectrum-compact-active.png (1316 x 174 px, matching reference)
  3. docs/ui-components/komponensek/spectrum-compact-silent.png (1316 x 174 px, matching reference)
  4. docs/ui-components/komponensek/spectrum-tall-active.png (1316 x 245 px, 1.5x height)
  5. docs/ui-components/komponensek/spectrum-tall-silent.png (1316 x 245 px, 1.5x height)

Features:
  - Exact 256-point illustrative chord signal (220, 440, 880, 3500 Hz) via genuine SpectrumCurve
  - Vibrant cyan curve (#2EE6F0) with delicate translucent gradient fill (alpha 50 -> 10)
  - Dedicated quiet / silent state with faint resting baseline at floor (-60 dBFS)
  - Fully legible logarithmic frequency scale (20 Hz .. 20 kHz) with major decade lines and minor ticks
  - Fully legible linear dB scale (-60 dBFS .. -12 dBFS / 0 dBFS) with clear labels and contrast halos
  - Native MPC dark theme palette: #0B1026 ground, #141A38 / #0C1026 panel, #27306A frame
"""

import json
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# Root directory of worktree
ROOT = Path(__file__).resolve().parent.parent
FONT_PATH = ROOT / "shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf"
OUTPUT_DIR = ROOT / "docs/ui-components/komponensek"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Theme Palette (shell-mpc/ui/theme.h look 1 "neon")
COLOR_BG_OUTER = (23, 23, 23)         # Reference outer border (#171717)
COLOR_OUTER_FRAME = (42, 42, 42)      # Reference outer frame line (#2A2A2A)
COLOR_BG_CANVAS = (10, 13, 21)        # Reference tab canvas background (#0A0D15)
COLOR_PANEL_TOP = (20, 26, 56)        # Panel gradient top (#141A38)
COLOR_PANEL_BOT = (12, 16, 38)        # Panel gradient bottom (#0C1026)
COLOR_FRAME = (39, 48, 106)           # Panel keyline (#27306A)
COLOR_GRID_MAJOR = (39, 48, 106, 220) # Major grid line
COLOR_GRID_MINOR = (26, 34, 80, 130)  # Minor grid line / ticks
COLOR_TEXT_TITLE = (238, 241, 255)    # Primary text (#EEF1FF)
COLOR_TEXT_DIM = (170, 178, 216)      # Secondary text (#AAB2D8)
COLOR_TEXT_FAINT = (118, 128, 175)    # Faint text (#7680AF - enhanced legibility)
COLOR_CYAN = (46, 230, 240)           # Main curve cyan (#2EE6F0)

# Load illustrative 256 chord levels
LEVELS_JSON = Path("/private/tmp/spectrum_chord_levels.json")
if LEVELS_JSON.exists():
    with open(LEVELS_JSON) as f:
        CHORD_LEVELS = json.load(f)
else:
    CHORD_LEVELS = [0.0] * 256
    for i in range(256):
        f = 20.0 * (1000.0 ** (i / 255.0))
        val = 0.0
        for peak_f, amp, q in [(220.0, 0.85, 18.0), (440.0, 0.73, 22.0), (880.0, 0.64, 25.0), (3500.0, 0.60, 28.0)]:
            dist = math.log2(f / peak_f)
            val = max(val, amp * math.exp(-0.5 * (dist * q) ** 2))
        CHORD_LEVELS[i] = val


def draw_panel(width=1294, height=142, levels=None, is_tall=False, scale=2):
    """Draws a single spectrum panel with supersampling."""
    W = width * scale
    H = height * scale
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    r_corner = 10 * scale
    pad_x = 10 * scale
    header_h = 22 * scale
    scale_lane_h = 22 * scale

    panel_rect = (0, 0, W - 1, H - 1)
    
    # Vertical gradient fill for panel body
    grad_img = Image.new("RGBA", (W, H))
    for y in range(H):
        t = y / max(1, H - 1)
        r = int(COLOR_PANEL_TOP[0] * (1 - t) + COLOR_PANEL_BOT[0] * t)
        g = int(COLOR_PANEL_TOP[1] * (1 - t) + COLOR_PANEL_BOT[1] * t)
        b = int(COLOR_PANEL_TOP[2] * (1 - t) + COLOR_PANEL_BOT[2] * t)
        for x in range(W):
            grad_img.putpixel((x, y), (r, g, b, 255))
    
    mask = Image.new("L", (W, H), 0)
    mask_draw = ImageDraw.Draw(mask)
    mask_draw.rounded_rectangle(panel_rect, radius=r_corner, fill=255)
    img.paste(grad_img, (0, 0), mask)

    # Frame stroke
    draw.rounded_rectangle(panel_rect, radius=r_corner, outline=COLOR_FRAME, width=max(1, int(1.2 * scale)))

    # Inner plotting region
    in_x = pad_x
    in_w = W - 2 * pad_x
    in_y = header_h
    in_h = H - header_h - scale_lane_h
    in_bottom = in_y + in_h
    in_right = in_x + in_w

    # Fonts
    font_title = ImageFont.truetype(str(FONT_PATH), 13 * scale)
    font_db = ImageFont.truetype(str(FONT_PATH), 11 * scale)
    font_hz = ImageFont.truetype(str(FONT_PATH), 11 * scale)

    # 1. Header: "SPECTRUM" title and "dB" unit
    draw.text((in_x + 4 * scale, int(4 * scale)), "SPECTRUM", font=font_title, fill=COLOR_TEXT_TITLE)
    draw.text((in_right - 26 * scale, in_y - 18 * scale), "dB", font=font_db, fill=COLOR_TEXT_DIM)

    # Vertical frequency mapping:
    # 20 Hz to 20 kHz = 3 decades
    def hz_to_x(hz):
        return in_x + (math.log10(hz / 20.0) / 3.0) * in_w

    # 2. Vertical Grid lines (Frequency)
    major_hz = [(100.0, "100"), (1000.0, "1k"), (10000.0, "10k")]
    minor_hz = [30.0, 50.0, 70.0, 200.0, 300.0, 500.0, 700.0, 2000.0, 3000.0, 5000.0, 7000.0]

    grid_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    grid_draw = ImageDraw.Draw(grid_layer)
    for hz in minor_hz:
        gx = int(round(hz_to_x(hz)))
        if in_x <= gx <= in_right:
            grid_draw.line([(gx, in_y), (gx, in_bottom)], fill=COLOR_GRID_MINOR, width=1)
            grid_draw.line([(gx, in_bottom), (gx, in_bottom + 3 * scale)], fill=COLOR_GRID_MINOR, width=1)

    for hz, _ in major_hz:
        gx = int(round(hz_to_x(hz)))
        if in_x <= gx <= in_right:
            grid_draw.line([(gx, in_y), (gx, in_bottom)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.0 * scale)))
            grid_draw.line([(gx, in_bottom), (gx, in_bottom + 4 * scale)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.0 * scale)))

    # 3. Horizontal Grid lines (dB scale)
    plot_h = in_h - 4 * scale
    
    db_steps = [
        (-48, 0.25),
        (-36, 0.50),
        (-24, 0.75),
        (-12, 1.00),
    ]

    for db_val, frac in db_steps:
        gy = int(round(in_bottom - frac * plot_h))
        grid_draw.line([(in_x, gy), (in_right, gy)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.0 * scale)))

    if is_tall:
        for frac in [0.125, 0.375, 0.625, 0.875]:
            gy = int(round(in_bottom - frac * plot_h))
            grid_draw.line([(in_x, gy), (in_right, gy)], fill=COLOR_GRID_MINOR, width=1)

    # Baseline at -60 dBFS
    grid_draw.line([(in_x, in_bottom), (in_right, in_bottom)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.2 * scale)))
    img.alpha_composite(grid_layer)

    # 4. dB scale labels
    for db_val, frac in db_steps:
        gy = int(round(in_bottom - frac * plot_h))
        text_str = f"{db_val}"
        bbox = font_db.getbbox(text_str)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = in_x + 4 * scale
        ty = gy - th // 2 - 1 * scale
        draw.rectangle((tx - 2 * scale, ty - 1 * scale, tx + tw + 2 * scale, ty + th + 2 * scale), fill=(14, 19, 42, 235))
        draw.text((tx, ty), text_str, font=font_db, fill=COLOR_TEXT_DIM)

    # Floor label (-60)
    bbox_60 = font_db.getbbox("-60")
    draw.rectangle((in_x + 2 * scale, in_bottom - (bbox_60[3]-bbox_60[1]) - 2 * scale, in_x + 6 * scale + (bbox_60[2]-bbox_60[0]), in_bottom), fill=(14, 19, 42, 235))
    draw.text((in_x + 4 * scale, in_bottom - (bbox_60[3]-bbox_60[1]) - 2 * scale), "-60", font=font_db, fill=COLOR_TEXT_FAINT)

    # 5. Frequency scale labels
    hz_labels = [
        (20.0, "20 Hz", "left"),
        (100.0, "100", "center"),
        (1000.0, "1k", "center"),
        (10000.0, "10k", "center"),
        (20000.0, "20 kHz", "right"),
    ]
    if is_tall:
        hz_labels = [
            (20.0, "20 Hz", "left"),
            (50.0, "50", "center"),
            (100.0, "100", "center"),
            (200.0, "200", "center"),
            (500.0, "500", "center"),
            (1000.0, "1k", "center"),
            (2000.0, "2k", "center"),
            (5000.0, "5k", "center"),
            (10000.0, "10k", "center"),
            (20000.0, "20 kHz", "right"),
        ]

    for hz, name, align in hz_labels:
        gx = hz_to_x(hz)
        bbox = font_hz.getbbox(name)
        tw = bbox[2] - bbox[0]
        ty = in_bottom + 4 * scale
        if align == "left":
            tx = in_x
        elif align == "right":
            tx = in_right - tw
        else:
            tx = int(round(gx - tw / 2))
        fill_col = COLOR_TEXT_DIM if hz in (20.0, 100.0, 1000.0, 10000.0, 20000.0) else COLOR_TEXT_FAINT
        draw.text((tx, ty), name, font=font_hz, fill=fill_col)

    # 6. Curve and Fill
    curve_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    curve_draw = ImageDraw.Draw(curve_layer)

    sound_active = (levels is not None) and any(v > 0.005 for v in levels)

    if sound_active:
        n_pts = len(levels)
        pts = []
        for i in range(n_pts):
            px = in_x + (i / float(n_pts - 1)) * in_w
            lvl = max(0.0, min(1.0, float(levels[i])))
            py = in_bottom - lvl * plot_h
            pts.append((px, py))

        # Translucent gradient fill under curve
        poly_pts = [(in_x, in_bottom)] + pts + [(in_right, in_bottom)]
        fill_mask = Image.new("L", (W, H), 0)
        fill_mask_draw = ImageDraw.Draw(fill_mask)
        fill_mask_draw.polygon(poly_pts, fill=255)

        grad_fill = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        for y in range(int(in_y), int(in_bottom) + 1):
            t = (y - in_y) / max(1, in_bottom - in_y)
            alpha = int(52 * (1 - t) + 10 * t)
            row_col = (COLOR_CYAN[0], COLOR_CYAN[1], COLOR_CYAN[2], alpha)
            for x in range(int(in_x), int(in_right) + 1):
                grad_fill.putpixel((x, y), row_col)
        
        curve_layer.paste(grad_fill, (0, 0), fill_mask)

        # Cyan polyline stroke
        stroke_w = max(2, int(1.8 * scale))
        curve_draw.line(pts, fill=(COLOR_CYAN[0], COLOR_CYAN[1], COLOR_CYAN[2], 255), width=stroke_w, joint="curve")

    else:
        # Csendes állapot (Quiet state):
        # Resting baseline at floor line in subtle cyan (#2EE6F0 with alpha ~85)
        curve_draw.line([(in_x, in_bottom), (in_right, in_bottom)],
                        fill=(COLOR_CYAN[0], COLOR_CYAN[1], COLOR_CYAN[2], 85),
                        width=max(1, int(1.0 * scale)))

    img.alpha_composite(curve_layer)
    final_img = img.resize((width, height), Image.Resampling.LANCZOS)
    return final_img


def frame_in_reference_canvas(panel_img, target_w=1316, is_tall=False):
    """Frames the panel inside the exact reference layout canvas.
    Compact reference canvas: 1316 x 174 (panel at x=14, y=15, w=1294, h=142).
    Tall reference canvas: 1316 x 245 (panel at x=14, y=15, w=1294, h=213).
    """
    pw, ph = panel_img.size
    th = 15 + ph + 17  # 15 px top margin, 17 px bottom margin
    canvas = Image.new("RGB", (target_w, th), COLOR_BG_CANVAS)
    draw = ImageDraw.Draw(canvas)

    # Outer border framing matching reference
    # Columns 0..3: [23, 23, 23], 4..5: [42, 42, 42]
    # Rows 0..3: [23, 23, 23], 4..5: [42, 42, 42]
    # Bottom rows: 158..165: [9, 11, 16], 166..173: [16, 16, 16]
    draw.rectangle((0, 0, 3, th - 1), fill=COLOR_BG_OUTER)
    draw.rectangle((4, 0, 5, th - 1), fill=COLOR_OUTER_FRAME)
    draw.rectangle((target_w - 2, 0, target_w - 1, th - 1), fill=COLOR_OUTER_FRAME)
    draw.rectangle((0, 0, target_w - 1, 3), fill=COLOR_BG_OUTER)
    draw.rectangle((0, 4, target_w - 1, 5), fill=COLOR_OUTER_FRAME)
    draw.rectangle((0, th - 8, target_w - 1, th - 1), fill=(16, 16, 16))

    # Paste panel at (14, 15)
    canvas.paste(panel_img, (14, 15), panel_img)
    return canvas


def build_preview_board():
    """Generates all standalone previews and the master comparison sheet."""
    print("Generating panels...")

    W_PANEL = 1294
    H_COMPACT = 142
    H_TALL = 213  # 1.5x of 142

    # 1. Raw panels
    raw_compact_act = draw_panel(W_PANEL, H_COMPACT, levels=CHORD_LEVELS, is_tall=False, scale=2)
    raw_compact_sil = draw_panel(W_PANEL, H_COMPACT, levels=None, is_tall=False, scale=2)
    raw_tall_act = draw_panel(W_PANEL, H_TALL, levels=CHORD_LEVELS, is_tall=True, scale=2)
    raw_tall_sil = draw_panel(W_PANEL, H_TALL, levels=None, is_tall=True, scale=2)

    # 2. Standalone framed previews (1:1 drop-in matching reference)
    frame_in_reference_canvas(raw_compact_act, target_w=1316, is_tall=False).save(OUTPUT_DIR / "spectrum-compact-active.png")
    frame_in_reference_canvas(raw_compact_sil, target_w=1316, is_tall=False).save(OUTPUT_DIR / "spectrum-compact-silent.png")
    frame_in_reference_canvas(raw_tall_act, target_w=1316, is_tall=True).save(OUTPUT_DIR / "spectrum-tall-active.png")
    frame_in_reference_canvas(raw_tall_sil, target_w=1316, is_tall=True).save(OUTPUT_DIR / "spectrum-tall-silent.png")

    print("Standalone framed panels saved. Assembling master comparison board...")

    # 3. Master Board Assembly (1380 px width, matching kepes-katalogus standard)
    BOARD_W = 1380
    BOARD_PAD_X = 43  # 43 + 1294 + 43 = 1380
    
    HEADER_H = 150
    SEC1_H = 35 + 20 + H_COMPACT + 24 + H_COMPACT + 40
    SEC2_H = 35 + 20 + H_TALL + 24 + H_TALL + 40
    FOOTER_H = 170
    BOARD_H = HEADER_H + SEC1_H + SEC2_H + FOOTER_H

    board = Image.new("RGBA", (BOARD_W, BOARD_H), (16, 20, 27, 255))
    draw = ImageDraw.Draw(board)

    f_title = ImageFont.truetype(str(FONT_PATH), 30)
    f_sub = ImageFont.truetype(str(FONT_PATH), 15)
    f_sec = ImageFont.truetype(str(FONT_PATH), 18)
    f_item = ImageFont.truetype(str(FONT_PATH), 13)
    f_badge = ImageFont.truetype(str(FONT_PATH), 12)

    # Header section
    draw.text((43, 35), "SynthyBoy — Spektrum panel látványterv", font=f_title, fill=COLOR_TEXT_TITLE)
    draw.text((43, 80), "Kompakt (140 px) és 1.5× magasabb (210 px) natív MPC kialakítás · Cián görbe finom kitöltéssel · Olvasható log Hz és dB skálák",
              font=f_sub, fill=COLOR_TEXT_DIM)

    # Badges
    badges = [
        ("CIÁN GÖRBE", "#2EE6F0", (46, 230, 240)),
        ("ILLUSZTRATÍV MINTAJEL", "220 / 440 / 880 / 3500 Hz", (170, 178, 216)),
        ("FREKVENCIASÁV", "20 Hz … 20 kHz (log)", (170, 178, 216)),
        ("DINAMIKA", "-60 dBFS … -12 dBFS", (170, 178, 216)),
    ]
    bx = 43
    by = 110
    for label, val, val_col in badges:
        text = f"{label}: {val}"
        bbox = f_badge.getbbox(text)
        bw = bbox[2] - bbox[0] + 16
        draw.rounded_rectangle((bx, by, bx + bw, by + 24), radius=4, fill=(24, 30, 48), outline=(45, 56, 88), width=1)
        draw.text((bx + 8, by + 4), text, font=f_badge, fill=val_col)
        bx += bw + 12

    y_cursor = HEADER_H

    # SECTION 1: KOMPAKT VÁLTOZAT
    draw.line([(43, y_cursor), (BOARD_W - 43, y_cursor)], fill=(45, 56, 88), width=1)
    y_cursor += 15
    draw.text((43, y_cursor), "1. KOMPAKT VÁLTOZAT — 1294 × 142 px (98 px aktív rajzterület, 24 px / 12 dB lépés)", font=f_sec, fill=COLOR_TEXT_TITLE)
    y_cursor += 30

    # 1A: Aktív
    draw.text((43, y_cursor), "Aktív állapot · 4-tagú akkord illusztratív mintajel · #2EE6F0 cián görbe + finom kitöltés", font=f_item, fill=(92, 255, 157))
    y_cursor += 20
    board.paste(raw_compact_act, (BOARD_PAD_X, y_cursor), raw_compact_act)
    y_cursor += H_COMPACT + 15

    # 1B: Csendes
    draw.text((43, y_cursor), "Csendes állapot · Néma bemenet (csendes gyári alapvonal, olvasható mérőhálózat és skálák)", font=f_item, fill=COLOR_TEXT_DIM)
    y_cursor += 20
    board.paste(raw_compact_sil, (BOARD_PAD_X, y_cursor), raw_compact_sil)
    y_cursor += H_COMPACT + 35

    # SECTION 2: 1.5x MAGASABB VÁLTOZAT
    draw.line([(43, y_cursor), (BOARD_W - 43, y_cursor)], fill=(45, 56, 88), width=1)
    y_cursor += 15
    draw.text((43, y_cursor), "2. 1.5× MAGASABB VÁLTOZAT — 1294 × 213 px (169 px aktív rajzterület, 42 px / 12 dB lépés, +71% felbontás)", font=f_sec, fill=COLOR_TEXT_TITLE)
    y_cursor += 30

    # 2A: Aktív
    draw.text((43, y_cursor), "Aktív állapot · Ugyanaz a mintajel · Kiemelt dinamikai és harmonikus felbontás, DAW-szintű leolvashatóság", font=f_item, fill=(92, 255, 157))
    y_cursor += 20
    board.paste(raw_tall_act, (BOARD_PAD_X, y_cursor), raw_tall_act)
    y_cursor += H_TALL + 15

    # 2B: Csendes
    draw.text((43, y_cursor), "Csendes állapot · Néma bemenet (1.5× magasabb mérőhálózat, 6 dB-es finom osztásokkal)", font=f_item, fill=COLOR_TEXT_DIM)
    y_cursor += 20
    board.paste(raw_tall_sil, (BOARD_PAD_X, y_cursor), raw_tall_sil)
    y_cursor += H_TALL + 35

    # Footer summary box
    draw.line([(43, y_cursor), (BOARD_W - 43, y_cursor)], fill=(45, 56, 88), width=1)
    y_cursor += 15
    draw.text((43, y_cursor), "Összehasonlítás és tervezési szempontok", font=f_sec, fill=COLOR_TEXT_TITLE)
    y_cursor += 28

    notes = [
        "• Kompakt változat (142 px): Helytakarékos, ideális a Macros fül 2×4-es forgótárcsa-rácsa alá (shell-mpc/ui/mod_macros.h:335).",
        "• 1.5× magasabb változat (213 px): 71%-kal nagyobb vertikális rajzterület (169 px vs 98 px), 42 px magas 12 dB-es sávok 6 dB-es köztes osztásokkal.",
        "• Olvasható skálák: A bal szélen kiemelt -12/-24/-36/-48/-60 dBFS feliratok; alul a 20 Hz .. 20 kHz logaritmikus frekvenciaosztások (100, 1k, 10k fősávok és segédjelek).",
        "• Natív illeszkedés: #141A38/#0C1026 panel, #27306A keret, #2EE6F0 cián görbe és akusztikus finom kitöltés; külön csendes nyugalmi alapvonal.",
    ]
    for note in notes:
        draw.text((43, y_cursor), note, font=f_item, fill=COLOR_TEXT_DIM)
        y_cursor += 20

    master_path = OUTPUT_DIR / "spectrum-panel-preview.png"
    board.save(master_path)
    print(f"Master preview board saved to {master_path} (size={board.size})")


if __name__ == "__main__":
    build_preview_board()
