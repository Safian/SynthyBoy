#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Renders comprehensive full-page before/after preview artwork for SynthyBoy MPC spectrum redesign.

Generates full 1280 x 628 canvases on identical coordinate systems with real controls:
  1. macros-layout-before-active.png (Current 140 px bottom strip, 1264x140 outer, 1244x98 inner plot, aspect 12.69:1)
  2. macros-layout-before-silent.png (Current 140 px bottom strip, quiet baseline at -60 dBFS)
  3. macros-layout-after-opt1-active.png (Option 1: Top-Right Rectangular Spectrum 628x312, aspect 2.01:1 / 2.31:1 inner plot, active)
  4. macros-layout-after-opt1-silent.png (Option 1: Top-Right Rectangular Spectrum 628x312, quiet state)
  5. macros-layout-after-opt2-active.png (Option 2: Center Pro Rectangular Spectrum 628x548, aspect 1.15:1 / 1.21:1 inner plot, active)
  6. macros-layout-after-opt2-silent.png (Option 2: Center Pro Rectangular Spectrum 628x548, quiet state)
  7. master-layout-before-active.png (Master output page before: Output with small peak meters, no spectrum)
  8. master-layout-after-active.png (Master output page after: Output paired with 628x260 Rectangular Master Analyzer, active)
  9. master-layout-after-silent.png (Master output page after: Rectangular Master Analyzer quiet state)
 10. spectrum-rect-628x312-active.png & silent.png (Standalone 628x312 rectangular panel)
 11. spectrum-rect-628x548-active.png & silent.png (Standalone 628x548 rectangular panel)
 12. spectrum-rect-628x260-active.png & silent.png (Standalone 628x260 rectangular panel)
 13. macros-spectrum-redesign-comparison.png (Unified multi-tier comparison board)
 14. master-spectrum-redesign-comparison.png (Master page comparison board)
"""

import json
import math
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_PATH = ROOT / "shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf"
OUTPUT_DIR = Path(tempfile.mkdtemp(prefix="synthyboy-screen-research-"))
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Theme Palette (shell-mpc/ui/theme.h Look 1 "neon")
COLOR_BG_SCREEN_TOP = (11, 16, 38)     # bg0 (#0B1026)
COLOR_BG_SCREEN_BOT = (5, 7, 15)       # bg1 (#05070F)
COLOR_PANEL_TOP = (20, 26, 56)         # panel0 (#141A38)
COLOR_PANEL_BOT = (12, 16, 38)         # panel1 (#0C1026)
COLOR_KEYLINE = (39, 48, 106)          # keyline (#27306A)
COLOR_KEYLINE_HI = (74, 90, 168)       # keylineHi (#4A5AA8)
COLOR_TRACK = (28, 35, 80)             # track (#1C2350)
COLOR_GRID_MAJOR = (39, 48, 106, 220)  # grid major
COLOR_GRID_MINOR = (26, 34, 80, 130)   # grid minor
COLOR_TEXT_TITLE = (238, 241, 255)     # text (#EEF1FF)
COLOR_TEXT_DIM = (170, 178, 216)       # textDim (#AAB2D8)
COLOR_TEXT_FAINT = (108, 118, 168)     # textFaint (#6C76A8)
COLOR_CYAN = (46, 230, 240)            # flow[0] (#2EE6F0)
COLOR_YELLOW_Q = (255, 194, 61)        # q focus (#FFC23D)
COLOR_CHIP_BG = (16, 22, 48)           # chip background
COLOR_CHIP_BORDER = (35, 44, 95)       # chip outline

# 256-point Chord spectrum data (220, 440, 880, 3500 Hz)
LEVELS_JSON = Path("/private/tmp/spectrum_chord_levels.json")
if LEVELS_JSON.exists():
    with open(LEVELS_JSON) as f:
        CHORD_LEVELS = json.load(f)
else:
    CHORD_LEVELS = [0.0] * 256
    for i in range(256):
        f = 20.0 * (1000.0 ** (i / 255.0))
        val = 0.0
        for peak_f, amp, q in [(220.0, 0.88, 18.0), (440.0, 0.74, 22.0), (880.0, 0.65, 25.0), (3500.0, 0.58, 28.0)]:
            dist = math.log2(f / peak_f)
            val = max(val, amp * math.exp(-0.5 * (dist * q) ** 2))
        CHORD_LEVELS[i] = val


def create_canvas(w=1280, h=628, scale=2):
    """Creates a background canvas with vertical gradient."""
    W = w * scale
    H = h * scale
    img = Image.new("RGBA", (W, H))
    draw = ImageDraw.Draw(img)
    for y in range(H):
        t = y / max(1, H - 1)
        r = int(COLOR_BG_SCREEN_TOP[0] * (1 - t) + COLOR_BG_SCREEN_BOT[0] * t)
        g = int(COLOR_BG_SCREEN_TOP[1] * (1 - t) + COLOR_BG_SCREEN_BOT[1] * t)
        b = int(COLOR_BG_SCREEN_TOP[2] * (1 - t) + COLOR_BG_SCREEN_BOT[2] * t)
        draw.line([(0, y), (W - 1, y)], fill=(r, g, b, 255))
    return img


def draw_panel_box(draw, img, rect, radius=10, scale=2, fill_grad=True, outline=COLOR_KEYLINE, outline_w=1):
    """Draws a themed rounded panel block with gradient fill."""
    rx, ry, rw, rh = [int(v * scale) for v in rect]
    if rw <= 0 or rh <= 0:
        return
    rad = int(radius * scale)
    box = (rx, ry, rx + rw - 1, ry + rh - 1)

    if fill_grad:
        grad = Image.new("RGBA", (rw, rh))
        gdraw = ImageDraw.Draw(grad)
        for y in range(rh):
            t = y / max(1, rh - 1)
            r = int(COLOR_PANEL_TOP[0] * (1 - t) + COLOR_PANEL_BOT[0] * t)
            g = int(COLOR_PANEL_TOP[1] * (1 - t) + COLOR_PANEL_BOT[1] * t)
            b = int(COLOR_PANEL_TOP[2] * (1 - t) + COLOR_PANEL_BOT[2] * t)
            gdraw.line([(0, y), (rw - 1, y)], fill=(r, g, b, 255))
        mask = Image.new("L", (rw, rh), 0)
        mdraw = ImageDraw.Draw(mask)
        mdraw.rounded_rectangle((0, 0, rw - 1, rh - 1), radius=rad, fill=255)
        img.paste(grad, (rx, ry), mask)

    if outline:
        draw.rounded_rectangle(box, radius=rad, outline=outline, width=max(1, int(outline_w * scale)))


def draw_header_bar(draw, scale, title="MACROS", active_bank=0, subtitle=None):
    """Draws the top 64px header bar."""
    bar_h = int(64 * scale)
    W = int(1280 * scale)
    # Bottom keyline
    draw.line([(0, bar_h - 1), (W - 1, bar_h - 1)], fill=COLOR_KEYLINE, width=max(1, int(1.2 * scale)))

    font_title = ImageFont.truetype(str(FONT_PATH), int(22 * scale))
    font_tabs = ImageFont.truetype(str(FONT_PATH), int(14 * scale))

    # Title
    draw.text((int(20 * scale), int(18 * scale)), title, font=font_title, fill=COLOR_TEXT_TITLE)

    if subtitle:
        draw.text((int(140 * scale), int(24 * scale)), subtitle, font=font_tabs, fill=COLOR_TEXT_DIM)

    # Bank toggles
    bx1 = int(320 * scale)
    by = int(14 * scale)
    bw = int(180 * scale)
    bh = int(38 * scale)

    # Bank 1
    col1 = COLOR_YELLOW_Q if active_bank == 0 else COLOR_TEXT_DIM
    bg1 = (COLOR_YELLOW_Q[0], COLOR_YELLOW_Q[1], COLOR_YELLOW_Q[2], 40) if active_bank == 0 else None
    if bg1:
        draw.rounded_rectangle((bx1, by, bx1 + bw, by + bh), radius=int(6 * scale), fill=bg1, outline=COLOR_YELLOW_Q, width=max(1, int(1.2 * scale)))
    else:
        draw.rounded_rectangle((bx1, by, bx1 + bw, by + bh), radius=int(6 * scale), outline=COLOR_KEYLINE, width=1)
    draw.text((bx1 + int(24 * scale), by + int(8 * scale)), "BANK 1:  1 - 4", font=font_tabs, fill=col1)
    # LED dot
    dot_col = COLOR_YELLOW_Q if active_bank == 0 else (60, 70, 110)
    draw.ellipse((bx1 + int(10 * scale), by + int(15 * scale), bx1 + int(18 * scale), by + int(23 * scale)), fill=dot_col)

    # Bank 2
    bx2 = bx1 + bw + int(16 * scale)
    col2 = COLOR_YELLOW_Q if active_bank == 1 else COLOR_TEXT_DIM
    bg2 = (COLOR_YELLOW_Q[0], COLOR_YELLOW_Q[1], COLOR_YELLOW_Q[2], 40) if active_bank == 1 else None
    if bg2:
        draw.rounded_rectangle((bx2, by, bx2 + bw, by + bh), radius=int(6 * scale), fill=bg2, outline=COLOR_YELLOW_Q, width=max(1, int(1.2 * scale)))
    else:
        draw.rounded_rectangle((bx2, by, bx2 + bw, by + bh), radius=int(6 * scale), outline=COLOR_KEYLINE, width=1)
    draw.text((bx2 + int(24 * scale), by + int(8 * scale)), "BANK 2:  5 - 8", font=font_tabs, fill=col2)
    dot_col2 = COLOR_YELLOW_Q if active_bank == 1 else (60, 70, 110)
    draw.ellipse((bx2 + int(10 * scale), by + int(15 * scale), bx2 + int(18 * scale), by + int(23 * scale)), fill=dot_col2)


def draw_macro_card(draw, img, rect, macro_num, name, val_str, val_pct, targets, q_num=None, scale=2, compact_vertical=False):
    """Draws a real MacroCard with dial, label, value, and target chips."""
    draw_panel_box(draw, img, rect, radius=10, scale=scale, fill_grad=True, outline=COLOR_KEYLINE, outline_w=1)

    rx, ry, rw, rh = [int(v * scale) for v in rect]
    font_name = ImageFont.truetype(str(FONT_PATH), int(13 * scale))
    font_val = ImageFont.truetype(str(FONT_PATH), int(14 * scale))
    font_chip = ImageFont.truetype(str(FONT_PATH), int(10 * scale))
    font_q = ImageFont.truetype(str(FONT_PATH), int(11 * scale))

    # Q chip badge
    if q_num:
        qx = rx + int(10 * scale)
        qy = ry + int(8 * scale)
        qw = int(18 * scale)
        qh = int(18 * scale)
        draw.rounded_rectangle((qx, qy, qx + qw, qy + qh), radius=int(4 * scale), fill=COLOR_YELLOW_Q)
        draw.text((qx + int(5 * scale), qy + int(1 * scale)), str(q_num), font=font_q, fill=(10, 13, 25))

    # Layout for knob & labels
    if not compact_vertical:
        # Standard layout: Dial at left/center, text beside it, chips below
        dial_r = int(24 * scale)
        cx = rx + int(48 * scale)
        cy = ry + int(52 * scale)

        # Track arc
        bbox = (cx - dial_r, cy - dial_r, cx + dial_r, cy + dial_r)
        draw.arc(bbox, 135, 405, fill=COLOR_TRACK, width=max(2, int(4 * scale)))
        # Value arc
        sweep = val_pct * 270.0
        if sweep > 0:
            draw.arc(bbox, 135, 135 + sweep, fill=COLOR_CYAN, width=max(2, int(4 * scale)))
        # Knob inner face
        inner_r = dial_r - int(4 * scale)
        draw.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r), fill=(16, 21, 46))
        # Indicator line
        ang_rad = math.radians(135 + sweep)
        ix = cx + (inner_r - int(4 * scale)) * math.cos(ang_rad)
        iy = cy + (inner_r - int(4 * scale)) * math.sin(ang_rad)
        draw.line([(cx, cy), (ix, iy)], fill=COLOR_TEXT_TITLE, width=max(1, int(2 * scale)))

        # Name and Value labels
        draw.text((cx + dial_r + int(14 * scale), cy - int(16 * scale)), name, font=font_name, fill=COLOR_TEXT_TITLE)
        draw.text((cx + dial_r + int(14 * scale), cy + int(4 * scale)), val_str, font=font_val, fill=COLOR_CYAN)

        # Chips area at bottom
        chip_y = ry + rh - int(56 * scale)
        chip_h = int(46 * scale)
        chip_w = rw - int(20 * scale)
        chip_x = rx + int(10 * scale)

        draw.rounded_rectangle((chip_x, chip_y, chip_x + chip_w, chip_y + chip_h), radius=int(6 * scale), fill=COLOR_CHIP_BG, outline=COLOR_CHIP_BORDER, width=1)
        if targets:
            draw.text((chip_x + int(10 * scale), chip_y + int(8 * scale)), targets[0], font=font_chip, fill=COLOR_TEXT_DIM)
            if len(targets) > 1:
                draw.text((chip_x + int(10 * scale), chip_y + int(24 * scale)), targets[1], font=font_chip, fill=COLOR_TEXT_FAINT)
        else:
            draw.text((chip_x + int(14 * scale), chip_y + int(15 * scale)), "NO TARGET - TAP TO ADD", font=font_chip, fill=COLOR_TEXT_FAINT)

    else:
        # Compact side-by-side card layout (for 4-vertical stack)
        dial_r = int(20 * scale)
        cx = rx + int(46 * scale)
        cy = ry + rh // 2

        bbox = (cx - dial_r, cy - dial_r, cx + dial_r, cy + dial_r)
        draw.arc(bbox, 135, 405, fill=COLOR_TRACK, width=max(2, int(3 * scale)))
        sweep = val_pct * 270.0
        if sweep > 0:
            draw.arc(bbox, 135, 135 + sweep, fill=COLOR_CYAN, width=max(2, int(3 * scale)))
        inner_r = dial_r - int(3 * scale)
        draw.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r), fill=(16, 21, 46))
        ang_rad = math.radians(135 + sweep)
        ix = cx + (inner_r - int(3 * scale)) * math.cos(ang_rad)
        iy = cy + (inner_r - int(3 * scale)) * math.sin(ang_rad)
        draw.line([(cx, cy), (ix, iy)], fill=COLOR_TEXT_TITLE, width=max(1, int(1.5 * scale)))

        # Name & value
        lx = cx + dial_r + int(10 * scale)
        draw.text((lx, cy - int(16 * scale)), name, font=font_name, fill=COLOR_TEXT_TITLE)
        draw.text((lx, cy + int(2 * scale)), val_str, font=font_val, fill=COLOR_CYAN)

        # Chips on the right side
        chip_x = rx + int(170 * scale)
        chip_y = ry + int(10 * scale)
        chip_w = rw - int(180 * scale)
        chip_h = rh - int(20 * scale)
        draw.rounded_rectangle((chip_x, chip_y, chip_x + chip_w, chip_y + chip_h), radius=int(6 * scale), fill=COLOR_CHIP_BG, outline=COLOR_CHIP_BORDER, width=1)
        if targets:
            draw.text((chip_x + int(8 * scale), chip_y + int(8 * scale)), targets[0], font=font_chip, fill=COLOR_TEXT_DIM)
            if len(targets) > 1:
                draw.text((chip_x + int(8 * scale), chip_y + int(24 * scale)), targets[1], font=font_chip, fill=COLOR_TEXT_FAINT)
        else:
            draw.text((chip_x + int(10 * scale), chip_y + int(14 * scale)), "NO TARGET - TAP", font=font_chip, fill=COLOR_TEXT_FAINT)


def draw_spectrum_panel(draw, img, rect, levels=None, is_tall=True, scale=2, title="SPECTRUM"):
    """Draws a rectangular spectrum analyzer panel inside `rect`."""
    draw_panel_box(draw, img, rect, radius=10, scale=scale, fill_grad=True, outline=COLOR_KEYLINE, outline_w=1.2)

    rx, ry, rw, rh = [int(v * scale) for v in rect]
    pad_x = int(12 * scale)
    header_h = int(24 * scale)
    scale_lane_h = int(24 * scale)

    in_x = rx + pad_x
    in_w = rw - 2 * pad_x
    in_y = ry + header_h
    in_h = rh - header_h - scale_lane_h
    in_bottom = in_y + in_h
    in_right = in_x + in_w

    font_title = ImageFont.truetype(str(FONT_PATH), int(13 * scale))
    font_db = ImageFont.truetype(str(FONT_PATH), int(10 * scale))
    font_hz = ImageFont.truetype(str(FONT_PATH), int(10 * scale))

    # Header: SPECTRUM title & dB unit
    draw.text((in_x + int(4 * scale), ry + int(5 * scale)), title, font=font_title, fill=COLOR_TEXT_TITLE)
    draw.text((in_right - int(26 * scale), ry + int(6 * scale)), "dB", font=font_db, fill=COLOR_TEXT_DIM)

    def hz_to_x(hz):
        return in_x + (math.log10(hz / 20.0) / 3.0) * in_w

    # Vertical frequency lines
    major_hz = [(100.0, "100"), (1000.0, "1k"), (10000.0, "10k")]
    minor_hz = [30.0, 50.0, 70.0, 200.0, 300.0, 500.0, 700.0, 2000.0, 3000.0, 5000.0, 7000.0]

    grid_layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    grid_draw = ImageDraw.Draw(grid_layer)

    for hz in minor_hz:
        gx = int(round(hz_to_x(hz)))
        if in_x <= gx <= in_right:
            grid_draw.line([(gx, in_y), (gx, in_bottom)], fill=COLOR_GRID_MINOR, width=1)

    for hz, _ in major_hz:
        gx = int(round(hz_to_x(hz)))
        if in_x <= gx <= in_right:
            grid_draw.line([(gx, in_y), (gx, in_bottom)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.0 * scale)))
            grid_draw.line([(gx, in_bottom), (gx, in_bottom + int(4 * scale))], fill=COLOR_GRID_MAJOR, width=max(1, int(1.0 * scale)))

    # Horizontal dB grid lines
    plot_h = in_h - int(4 * scale)
    db_steps = [(-48, 0.25), (-36, 0.50), (-24, 0.75), (-12, 1.00)]
    if in_h > int(180 * scale):
        # Add intermediate grid lines for tall rectangular graphs
        for frac in [0.125, 0.375, 0.625, 0.875]:
            gy = int(round(in_bottom - frac * plot_h))
            grid_draw.line([(in_x, gy), (in_right, gy)], fill=COLOR_GRID_MINOR, width=1)

    for db_val, frac in db_steps:
        gy = int(round(in_bottom - frac * plot_h))
        grid_draw.line([(in_x, gy), (in_right, gy)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.0 * scale)))

    # Baseline at -60 dBFS
    grid_draw.line([(in_x, in_bottom), (in_right, in_bottom)], fill=COLOR_GRID_MAJOR, width=max(1, int(1.2 * scale)))
    img.alpha_composite(grid_layer)

    # dB labels with dark halos
    for db_val, frac in db_steps:
        gy = int(round(in_bottom - frac * plot_h))
        text_str = f"{db_val}"
        bbox = font_db.getbbox(text_str)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = in_x + int(4 * scale)
        ty = gy - th // 2 - int(1 * scale)
        draw.rectangle((tx - int(2 * scale), ty - int(1 * scale), tx + tw + int(2 * scale), ty + th + int(2 * scale)), fill=(14, 19, 42, 235))
        draw.text((tx, ty), text_str, font=font_db, fill=COLOR_TEXT_DIM)

    # Floor label (-60)
    bbox_60 = font_db.getbbox("-60")
    draw.rectangle((in_x + int(2 * scale), in_bottom - (bbox_60[3] - bbox_60[1]) - int(2 * scale), in_x + int(6 * scale) + (bbox_60[2] - bbox_60[0]), in_bottom), fill=(14, 19, 42, 235))
    draw.text((in_x + int(4 * scale), in_bottom - (bbox_60[3] - bbox_60[1]) - int(2 * scale)), "-60", font=font_db, fill=COLOR_TEXT_FAINT)

    # Frequency labels
    hz_labels = [
        (20.0, "20 Hz", "left"),
        (100.0, "100", "center"),
        (1000.0, "1k", "center"),
        (10000.0, "10k", "center"),
        (20000.0, "20 kHz", "right"),
    ]
    if in_w > int(500 * scale):
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
        ty = in_bottom + int(4 * scale)
        if align == "left":
            tx = in_x
        elif align == "right":
            tx = in_right - tw
        else:
            tx = int(round(gx - tw / 2))
        fill_col = COLOR_TEXT_DIM if hz in (20.0, 100.0, 1000.0, 10000.0, 20000.0) else COLOR_TEXT_FAINT
        draw.text((tx, ty), name, font=font_hz, fill=fill_col)

    # Curve and Fill
    curve_layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
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
        fill_mask = Image.new("L", img.size, 0)
        fill_mask_draw = ImageDraw.Draw(fill_mask)
        fill_mask_draw.polygon(poly_pts, fill=255)

        grad_fill = Image.new("RGBA", img.size, (0, 0, 0, 0))
        for y in range(int(in_y), int(in_bottom) + 1):
            t = (y - in_y) / max(1, in_bottom - in_y)
            alpha = int(58 * (1 - t) + 12 * t)
            row_col = (COLOR_CYAN[0], COLOR_CYAN[1], COLOR_CYAN[2], alpha)
            for x in range(int(in_x), int(in_right) + 1):
                grad_fill.putpixel((x, y), row_col)
        curve_layer.paste(grad_fill, (0, 0), fill_mask)

        # Cyan polyline stroke
        stroke_w = max(2, int(2.0 * scale))
        curve_draw.line(pts, fill=(COLOR_CYAN[0], COLOR_CYAN[1], COLOR_CYAN[2], 255), width=stroke_w, joint="curve")
    else:
        # Quiet state: resting baseline at floor
        curve_draw.line([(in_x, in_bottom), (in_right, in_bottom)],
                        fill=(COLOR_CYAN[0], COLOR_CYAN[1], COLOR_CYAN[2], 85),
                        width=max(1, int(1.2 * scale)))

    img.alpha_composite(curve_layer)


# =================================================================================================
# PAGE RENDERERS
# =================================================================================================

def render_macros_before(active=True):
    """Renders the current MacroView with 140px bottom spectrum strip."""
    scale = 2
    img = create_canvas(1280, 628, scale)
    draw = ImageDraw.Draw(img)
    draw_header_bar(draw, scale, title="MACROS", active_bank=0)

    # 8 cards in 2 rows x 4 columns
    # gridY = 72, colW = 310, rowH = 196, gap = 8, m = 8
    m = 8
    gap = 8
    gridY = 72
    colW = 310
    rowH = 196

    macro_names = ["MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"]
    macro_vals = [("0.0%", 0.0), ("35.0%", 0.35), ("62.0%", 0.62), ("100.0%", 1.0),
                  ("15.0%", 0.15), ("0.0%", 0.0), ("80.0%", 0.8), ("45.0%", 0.45)]

    for r in range(2):
        for c in range(4):
            idx = r * 4 + c
            cx = m + c * (colW + gap)
            cy = gridY + r * (rowH + gap)
            cw = colW if c < 3 else (1280 - m - cx)
            q_num = (idx % 4) + 1 if (idx < 4) else None
            targets = ["F1 CUTOFF +40%", "WARP DRIVE +20%"] if idx in (1, 2) else None
            draw_macro_card(draw, img, (cx, cy, cw, rowH), idx + 1, macro_names[idx], macro_vals[idx][0], macro_vals[idx][1], targets, q_num=q_num, scale=scale)

    # Q-group frame around Bank 1
    q_rect = (m - 3, gridY - 3, 1280 - 2 * m + 6, rowH + 6)
    draw.rounded_rectangle((int(q_rect[0] * scale), int(q_rect[1] * scale), int((q_rect[0] + q_rect[2]) * scale), int((q_rect[1] + q_rect[3]) * scale)),
                           radius=int(12 * scale), outline=COLOR_YELLOW_Q, width=max(1, int(1.5 * scale)))

    # Spectrum strip at bottom: { 8, 480, 1264, 140 }
    spectrum_rect = (8, 480, 1264, 140)
    levels = CHORD_LEVELS if active else None
    draw_spectrum_panel(draw, img, spectrum_rect, levels=levels, is_tall=False, scale=scale, title="SPECTRUM")

    final_img = img.resize((1280, 628), Image.Resampling.LANCZOS)
    return final_img


def render_macros_after_option1(active=True):
    """Renders Option 1: Top-Right Rectangular Spectrum (628 x 312 px) + Bank 1 (Left 2x2) + Bank 2 (Bottom 1x4)."""
    scale = 2
    img = create_canvas(1280, 628, scale)
    draw = ImageDraw.Draw(img)
    draw_header_bar(draw, scale, title="MACROS", active_bank=0, subtitle="[ PLAN A: ASYMMETRIC RECTANGULAR SPECTRUM ]")

    m = 8
    gap = 8

    # Top Left: Bank 1 (Macros 1, 2, 3, 4) in 2 rows x 2 cols
    b1_w = 628
    b1_h = 312
    card_w = (b1_w - gap) // 2
    card_h = (b1_h - gap) // 2

    macro_names = ["MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4"]
    macro_vals = [("0.0%", 0.0), ("35.0%", 0.35), ("62.0%", 0.62), ("100.0%", 1.0)]

    for r in range(2):
        for c in range(2):
            idx = r * 2 + c
            cx = m + c * (card_w + gap)
            cy = 72 + r * (card_h + gap)
            targets = ["F1 CUTOFF +40%", "WARP DRIVE +20%"] if idx in (1, 2) else None
            draw_macro_card(draw, img, (cx, cy, card_w, card_h), idx + 1, macro_names[idx], macro_vals[idx][0], macro_vals[idx][1], targets, q_num=idx + 1, scale=scale)

    # Q-group frame around Bank 1
    q_rect = (m - 3, 72 - 3, b1_w + 6, b1_h + 6)
    draw.rounded_rectangle((int(q_rect[0] * scale), int(q_rect[1] * scale), int((q_rect[0] + q_rect[2]) * scale), int((q_rect[1] + q_rect[3]) * scale)),
                           radius=int(12 * scale), outline=COLOR_YELLOW_Q, width=max(1, int(1.5 * scale)))

    # Top Right: Rectangular Spectrum Panel { 644, 72, 628, 312 }
    # Outer aspect ratio: 628 / 312 = 2.01:1 (approx 18:9 / 2:1)
    # Inner plot: 604 x 262 px, aspect 2.31:1, height 262 px (vs 98 px originally: +167% vertical gain!)
    spec_rect = (644, 72, 628, 312)
    levels = CHORD_LEVELS if active else None
    draw_spectrum_panel(draw, img, spec_rect, levels=levels, is_tall=True, scale=scale, title="STUDIO SPECTRUM ANALYZER (2.0 : 1)")

    # Bottom: Bank 2 (Macros 5, 6, 7, 8) in 1 row x 4 cols { 8, 392, 1264, 228 }
    b2_y = 392
    b2_h = 228
    b2_col_w = (1280 - 2 * m - 3 * gap) // 4
    b2_names = ["MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"]
    b2_vals = [("15.0%", 0.15), ("0.0%", 0.0), ("80.0%", 0.8), ("45.0%", 0.45)]

    for c in range(4):
        cx = m + c * (b2_col_w + gap)
        cw = b2_col_w if c < 3 else (1280 - m - cx)
        targets = ["REVERB MIX +50%"] if c == 2 else None
        draw_macro_card(draw, img, (cx, b2_y, cw, b2_h), c + 5, b2_names[c], b2_vals[c][0], b2_vals[c][1], targets, q_num=None, scale=scale)

    final_img = img.resize((1280, 628), Image.Resampling.LANCZOS)
    return final_img


def render_macros_after_option2(active=True):
    """Renders Option 2: Center Pro Studio Rectangular Spectrum (628 x 548 px) flanked by Bank 1 (Left) & Bank 2 (Right)."""
    scale = 2
    img = create_canvas(1280, 628, scale)
    draw = ImageDraw.Draw(img)
    draw_header_bar(draw, scale, title="MACROS", active_bank=0, subtitle="[ PLAN B: FLANKED CENTER PRO ANALYZER ]")

    m = 8
    gap = 8
    gridY = 72
    total_h = 548

    # Left Column: Bank 1 (Macros 1..4) in 4 rows x 1 col { 8, 72, 310, 548 }
    col_w = 310
    card_h = (total_h - 3 * gap) // 4
    b1_names = ["MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4"]
    b1_vals = [("0.0%", 0.0), ("35.0%", 0.35), ("62.0%", 0.62), ("100.0%", 1.0)]

    for r in range(4):
        cy = gridY + r * (card_h + gap)
        targets = ["F1 CUTOFF +40%"] if r == 1 else None
        draw_macro_card(draw, img, (m, cy, col_w, card_h), r + 1, b1_names[r], b1_vals[r][0], b1_vals[r][1], targets, q_num=r + 1, scale=scale, compact_vertical=True)

    # Q-group frame around Bank 1 (Left column)
    q_rect = (m - 3, gridY - 3, col_w + 6, total_h + 6)
    draw.rounded_rectangle((int(q_rect[0] * scale), int(q_rect[1] * scale), int((q_rect[0] + q_rect[2]) * scale), int((q_rect[1] + q_rect[3]) * scale)),
                           radius=int(12 * scale), outline=COLOR_YELLOW_Q, width=max(1, int(1.5 * scale)))

    # Center Panel: Pro Studio Rectangular Spectrum { 326, 72, 628, 548 }
    # Outer aspect ratio: 628 / 548 = 1.15 : 1 (approx 4:3 / golden rectangle)
    # Inner plot: 604 x 498 px, aspect 1.21 : 1, height 498 px (vs 98 px originally: +408% vertical gain!)
    spec_rect = (326, 72, 628, 548)
    levels = CHORD_LEVELS if active else None
    draw_spectrum_panel(draw, img, spec_rect, levels=levels, is_tall=True, scale=scale, title="PRO STUDIO SPECTRUM ANALYZER (4:3)")

    # Right Column: Bank 2 (Macros 5..8) in 4 rows x 1 col { 962, 72, 310, 548 }
    b2_x = 962
    b2_names = ["MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"]
    b2_vals = [("15.0%", 0.15), ("0.0%", 0.0), ("80.0%", 0.8), ("45.0%", 0.45)]

    for r in range(4):
        cy = gridY + r * (card_h + gap)
        targets = ["REVERB +50%"] if r == 2 else None
        draw_macro_card(draw, img, (b2_x, cy, col_w, card_h), r + 5, b2_names[r], b2_vals[r][0], b2_vals[r][1], targets, q_num=None, scale=scale, compact_vertical=True)

    final_img = img.resize((1280, 628), Image.Resampling.LANCZOS)
    return final_img


def render_master_before():
    """Renders the current Master Main page before spectrum integration."""
    scale = 2
    img = create_canvas(1280, 628, scale)
    draw = ImageDraw.Draw(img)
    draw_header_bar(draw, scale, title="MASTER", active_bank=0, subtitle="[ SIDE: MAIN ]")

    font_sec = ImageFont.truetype(str(FONT_PATH), int(12 * scale))

    # Top Left: OUTPUT block { 8, 72, 640, 228 }
    draw_panel_box(draw, img, (8, 72, 640, 228), radius=10, scale=scale)
    draw.text((int(20 * scale), int(80 * scale)), "OUTPUT", font=font_sec, fill=COLOR_TEXT_DIM)

    # Output Knobs: Volume, Spread, Stereo
    draw_macro_card(draw, img, (18, 102, 190, 186), 1, "VOLUME", "0.0 dB", 0.8, None, q_num=1, scale=scale)
    draw_macro_card(draw, img, (216, 102, 190, 186), 2, "SPREAD", "100%", 1.0, None, q_num=2, scale=scale)
    draw_macro_card(draw, img, (414, 102, 190, 186), 3, "STEREO", "100%", 1.0, None, q_num=3, scale=scale)

    # Vertical meter bars { 610, 102, 30, 186 }
    draw_panel_box(draw, img, (612, 102, 28, 186), radius=4, scale=scale, fill_grad=False, outline=COLOR_KEYLINE)
    draw.line([(int(620 * scale), int(270 * scale)), (int(620 * scale), int(140 * scale))], fill=COLOR_CYAN, width=int(4 * scale))
    draw.line([(int(628 * scale), int(270 * scale)), (int(628 * scale), int(145 * scale))], fill=COLOR_CYAN, width=int(4 * scale))

    # Top Right: PITCH block { 656, 72, 616, 228 }
    draw_panel_box(draw, img, (656, 72, 616, 228), radius=10, scale=scale)
    draw.text((int(668 * scale), int(80 * scale)), "PITCH", font=font_sec, fill=COLOR_TEXT_DIM)
    draw_macro_card(draw, img, (666, 102, 180, 186), 4, "TUNE", "0.0 st", 0.5, None, q_num=4, scale=scale)
    draw_macro_card(draw, img, (854, 102, 180, 186), 5, "TRANSPOSE", "+0 st", 0.5, None, scale=scale)
    draw_macro_card(draw, img, (1042, 102, 220, 186), 6, "BEND UP/DN", "2 / 2 st", 0.2, None, scale=scale)

    # Bottom Left: VOICES block { 8, 308, 780, 312 }
    draw_panel_box(draw, img, (8, 308, 780, 312), radius=10, scale=scale)
    draw.text((int(20 * scale), int(316 * scale)), "VOICES", font=font_sec, fill=COLOR_TEXT_DIM)
    draw_macro_card(draw, img, (18, 338, 240, 270), 7, "POLYPHONY", "16 v", 1.0, None, scale=scale)
    draw_macro_card(draw, img, (266, 338, 240, 270), 8, "VEL TRACK", "+0.0%", 0.5, None, scale=scale)
    draw_macro_card(draw, img, (514, 338, 264, 270), 9, "STEAL / PRIO", "OLDEST", 0.5, None, scale=scale)

    # Bottom Right: GLIDE block { 796, 308, 476, 312 }
    draw_panel_box(draw, img, (796, 308, 476, 312), radius=10, scale=scale)
    draw.text((int(808 * scale), int(316 * scale)), "GLIDE", font=font_sec, fill=COLOR_TEXT_DIM)
    draw_macro_card(draw, img, (806, 338, 220, 270), 10, "PORTA TIME", "120 ms", 0.3, None, scale=scale)
    draw_macro_card(draw, img, (1034, 338, 228, 270), 11, "SLOPE", "0.0 lin", 0.5, None, scale=scale)

    final_img = img.resize((1280, 628), Image.Resampling.LANCZOS)
    return final_img


def render_master_after(active=True):
    """Renders the Master Main page with integrated Rectangular Master Spectrum Analyzer (628 x 260 px)."""
    scale = 2
    img = create_canvas(1280, 628, scale)
    draw = ImageDraw.Draw(img)
    draw_header_bar(draw, scale, title="MASTER", active_bank=0, subtitle="[ INTEGRATED MASTER SPECTRUM ANALYZER ]")

    font_sec = ImageFont.truetype(str(FONT_PATH), int(12 * scale))

    # Top Left: OUTPUT controls { 8, 72, 628, 260 }
    draw_panel_box(draw, img, (8, 72, 628, 260), radius=10, scale=scale)
    draw.text((int(20 * scale), int(80 * scale)), "MASTER OUTPUT & MONITORING", font=font_sec, fill=COLOR_TEXT_DIM)

    # Output Knobs
    draw_macro_card(draw, img, (18, 102, 180, 218), 1, "VOLUME", "0.0 dB", 0.8, None, q_num=1, scale=scale)
    draw_macro_card(draw, img, (206, 102, 180, 218), 2, "SPREAD", "100%", 1.0, None, q_num=2, scale=scale)
    draw_macro_card(draw, img, (394, 102, 180, 218), 3, "STEREO", "100%", 1.0, None, q_num=3, scale=scale)

    # Peak meters beside knobs { 582, 102, 44, 218 }
    draw_panel_box(draw, img, (582, 102, 44, 218), radius=6, scale=scale, fill_grad=False, outline=COLOR_KEYLINE)
    draw.line([(int(594 * scale), int(305 * scale)), (int(594 * scale), int(135 * scale))], fill=COLOR_CYAN, width=int(5 * scale))
    draw.line([(int(612 * scale), int(305 * scale)), (int(612 * scale), int(142 * scale))], fill=COLOR_CYAN, width=int(5 * scale))

    # Top Right: Rectangular Master Spectrum Analyzer { 644, 72, 628, 260 }
    # Outer aspect ratio: 628 / 260 = 2.42 : 1
    # Inner plot: 604 x 210 px, aspect 2.88 : 1, height 210 px
    spec_rect = (644, 72, 628, 260)
    levels = CHORD_LEVELS if active else None
    draw_spectrum_panel(draw, img, spec_rect, levels=levels, is_tall=True, scale=scale, title="MASTER OUTPUT SPECTRUM ANALYZER")

    # Bottom Left: PITCH & VOICES unified block { 8, 340, 780, 280 }
    draw_panel_box(draw, img, (8, 340, 780, 280), radius=10, scale=scale)
    draw.text((int(20 * scale), int(348 * scale)), "PITCH & VOICE ARCHITECTURE", font=font_sec, fill=COLOR_TEXT_DIM)
    draw_macro_card(draw, img, (18, 372, 180, 238), 4, "TUNE", "0.0 st", 0.5, None, q_num=4, scale=scale)
    draw_macro_card(draw, img, (206, 372, 180, 238), 5, "TRANSPOSE", "+0 st", 0.5, None, scale=scale)
    draw_macro_card(draw, img, (394, 372, 180, 238), 6, "POLYPHONY", "16 v", 1.0, None, scale=scale)
    draw_macro_card(draw, img, (582, 372, 196, 238), 7, "STEAL", "OLDEST", 0.5, None, scale=scale)

    # Bottom Right: GLIDE block { 796, 340, 476, 280 }
    draw_panel_box(draw, img, (796, 340, 476, 280), radius=10, scale=scale)
    draw.text((int(808 * scale), int(348 * scale)), "GLIDE / PORTAMENTO", font=font_sec, fill=COLOR_TEXT_DIM)
    draw_macro_card(draw, img, (806, 372, 220, 238), 8, "PORTA TIME", "120 ms", 0.3, None, scale=scale)
    draw_macro_card(draw, img, (1034, 372, 228, 238), 9, "SLOPE", "0.0 lin", 0.5, None, scale=scale)

    final_img = img.resize((1280, 628), Image.Resampling.LANCZOS)
    return final_img


def render_standalone_panel(w, h, levels=None, title="SPECTRUM"):
    """Renders a standalone rectangular spectrum panel."""
    scale = 2
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw_spectrum_panel(draw, img, (0, 0, w, h), levels=levels, is_tall=True, scale=scale, title=title)
    final_img = img.resize((w, h), Image.Resampling.LANCZOS)
    return final_img


# =================================================================================================
# MASTER COMPARISON BOARDS
# =================================================================================================

def render_macros_comparison_board(before_img, opt1_img, opt2_img):
    """Combines Before, Option 1, and Option 2 into an authoritative 1440px wide comparison sheet."""
    scale = 1
    board_w = 1440
    # Header 140px, 3 sections of 680px + gaps = 2300px height
    sec_h = 670
    board_h = 160 + 3 * sec_h
    board = Image.new("RGB", (board_w, board_h), (11, 14, 25))
    draw = ImageDraw.Draw(board)

    font_title = ImageFont.truetype(str(FONT_PATH), 28)
    font_sub = ImageFont.truetype(str(FONT_PATH), 15)
    font_sec = ImageFont.truetype(str(FONT_PATH), 20)
    font_desc = ImageFont.truetype(str(FONT_PATH), 13)

    # Master title
    draw.text((40, 30), "SYNTHYBOY MPC - SPECTRUM PANEL & ENVIRONMENT REDESIGN", font=font_title, fill=COLOR_TEXT_TITLE)
    draw.text((40, 75), "Owner decision alignment: Eliminating the flat 142px strip (12.7:1) in favor of usable rectangular studio graphs", font=font_sub, fill=COLOR_CYAN)
    draw.text((40, 100), "Preserving all 8 macro controls, touch targets >= 56 px, calibrated Hz/dB scales, active and silent baselines", font=font_sub, fill=COLOR_TEXT_DIM)
    draw.line([(40, 135), (board_w - 40, 135)], fill=COLOR_KEYLINE, width=2)

    sections = [
        ("1. BEFORE: CURRENT 140 PX FLAT BOTTOM STRIP (UNACCEPTABLE SLIT)",
         "Outer: 1264 x 140 px (9.0 : 1)  |  Inner Plot: 1244 x 98 px (12.7 : 1)  |  Vertical Resolution: only 98 px! (~1.6 px / dB). Severe vertical compression.",
         before_img,
         (240, 80, 80)),
        ("2. AFTER - PROPOSAL A: ASYMMETRIC RECTANGULAR ANALYZER (2.0 : 1 / 18:9)",
         "Outer: 628 x 312 px (2.01 : 1)  |  Inner Plot: 604 x 262 px (2.31 : 1)  |  Vertical Resolution: 262 px (+167% vertical gain!). Bank 1 (2x2) + Bank 2 (1x4).",
         opt1_img,
         COLOR_YELLOW_Q),
        ("3. AFTER - PROPOSAL B: SYMMETRIC PRO STUDIO FLANKED ANALYZER (4:3)",
         "Outer: 628 x 548 px (1.15 : 1)  |  Inner Plot: 604 x 498 px (1.21 : 1)  |  Vertical Resolution: 498 px (+408% vertical gain!). Centerpiece flanked by Bank 1 & 2.",
         opt2_img,
         COLOR_CYAN),
    ]

    for i, (stitle, sdesc, simg, tag_col) in enumerate(sections):
        sy = 155 + i * sec_h
        # Section header
        draw.rectangle((40, sy, 46, sy + 38), fill=tag_col)
        draw.text((56, sy), stitle, font=font_sec, fill=COLOR_TEXT_TITLE)
        draw.text((56, sy + 24), sdesc, font=font_desc, fill=tag_col)

        # Image border & canvas paste
        canvas_x = 80
        canvas_y = sy + 50
        draw.rectangle((canvas_x - 2, canvas_y - 2, canvas_x + 1280 + 1, canvas_y + 628 + 1), outline=COLOR_KEYLINE, width=1)
        board.paste(simg, (canvas_x, canvas_y))

    return board


def render_master_comparison_board(before_img, after_img):
    """Combines Master Before and After into an authoritative presentation sheet."""
    board_w = 1440
    sec_h = 670
    board_h = 160 + 2 * sec_h
    board = Image.new("RGB", (board_w, board_h), (11, 14, 25))
    draw = ImageDraw.Draw(board)

    font_title = ImageFont.truetype(str(FONT_PATH), 28)
    font_sub = ImageFont.truetype(str(FONT_PATH), 15)
    font_sec = ImageFont.truetype(str(FONT_PATH), 20)
    font_desc = ImageFont.truetype(str(FONT_PATH), 13)

    # Master title
    draw.text((40, 30), "SYNTHYBOY MPC - MASTER OUTPUT REUSED SPECTRUM INTEGRATION", font=font_title, fill=COLOR_TEXT_TITLE)
    draw.text((40, 75), "Master sum monitoring: Upgrading from simple peak meters to an integrated 628 x 260 px rectangular studio analyzer", font=font_sub, fill=COLOR_CYAN)
    draw.text((40, 100), "Calibrated -60 dBFS to 0 dBFS headroom, log 20 Hz to 20 kHz axis, active signal tracking & silent baseline", font=font_sub, fill=COLOR_TEXT_DIM)
    draw.line([(40, 135), (board_w - 40, 135)], fill=COLOR_KEYLINE, width=2)

    sections = [
        ("1. BEFORE: MASTER OUTPUT TAB WITHOUT FREQUENCY MONITORING",
         "Output quadrant has only narrow 40px peak meters. No frequency domain analysis for final mix bus.",
         before_img,
         (240, 80, 80)),
        ("2. AFTER: INTEGRATED RECTANGULAR MASTER SPECTRUM ANALYZER (2.4 : 1)",
         "Outer: 628 x 260 px (2.42 : 1)  |  Inner Plot: 604 x 210 px (2.88 : 1)  |  Full mastering-grade frequency feedback.",
         after_img,
         COLOR_CYAN),
    ]

    for i, (stitle, sdesc, simg, tag_col) in enumerate(sections):
        sy = 155 + i * sec_h
        draw.rectangle((40, sy, 46, sy + 38), fill=tag_col)
        draw.text((56, sy), stitle, font=font_sec, fill=COLOR_TEXT_TITLE)
        draw.text((56, sy + 24), sdesc, font=font_desc, fill=tag_col)

        canvas_x = 80
        canvas_y = sy + 50
        draw.rectangle((canvas_x - 2, canvas_y - 2, canvas_x + 1280 + 1, canvas_y + 628 + 1), outline=COLOR_KEYLINE, width=1)
        board.paste(simg, (canvas_x, canvas_y))

    return board


def main():
    print("Generating comprehensive spectrum redesign artwork...")

    # 1. Full-page layouts
    print("  -> macros-layout-before-active.png")
    mac_bef_act = render_macros_before(active=True)
    mac_bef_act.save(OUTPUT_DIR / "macros-layout-before-active.png")

    print("  -> macros-layout-before-silent.png")
    mac_bef_sil = render_macros_before(active=False)
    mac_bef_sil.save(OUTPUT_DIR / "macros-layout-before-silent.png")

    print("  -> macros-layout-after-opt1-active.png")
    mac_opt1_act = render_macros_after_option1(active=True)
    mac_opt1_act.save(OUTPUT_DIR / "macros-layout-after-opt1-active.png")

    print("  -> macros-layout-after-opt1-silent.png")
    mac_opt1_sil = render_macros_after_option1(active=False)
    mac_opt1_sil.save(OUTPUT_DIR / "macros-layout-after-opt1-silent.png")

    print("  -> macros-layout-after-opt2-active.png")
    mac_opt2_act = render_macros_after_option2(active=True)
    mac_opt2_act.save(OUTPUT_DIR / "macros-layout-after-opt2-active.png")

    print("  -> macros-layout-after-opt2-silent.png")
    mac_opt2_sil = render_macros_after_option2(active=False)
    mac_opt2_sil.save(OUTPUT_DIR / "macros-layout-after-opt2-silent.png")

    print("  -> master-layout-before-active.png")
    mas_bef = render_master_before()
    mas_bef.save(OUTPUT_DIR / "master-layout-before-active.png")

    print("  -> master-layout-after-active.png")
    mas_aft_act = render_master_after(active=True)
    mas_aft_act.save(OUTPUT_DIR / "master-layout-after-active.png")

    print("  -> master-layout-after-silent.png")
    mas_aft_sil = render_master_after(active=False)
    mas_aft_sil.save(OUTPUT_DIR / "master-layout-after-silent.png")

    # 2. Standalone rectangular panels
    print("  -> Standalone rectangular panels...")
    render_standalone_panel(628, 312, levels=CHORD_LEVELS, title="RECTANGULAR SPECTRUM ANALYZER (2.0 : 1)").save(OUTPUT_DIR / "spectrum-rect-628x312-active.png")
    render_standalone_panel(628, 312, levels=None, title="RECTANGULAR SPECTRUM ANALYZER (2.0 : 1)").save(OUTPUT_DIR / "spectrum-rect-628x312-silent.png")

    render_standalone_panel(628, 548, levels=CHORD_LEVELS, title="PRO STUDIO SPECTRUM ANALYZER (4:3)").save(OUTPUT_DIR / "spectrum-rect-628x548-active.png")
    render_standalone_panel(628, 548, levels=None, title="PRO STUDIO SPECTRUM ANALYZER (4:3)").save(OUTPUT_DIR / "spectrum-rect-628x548-silent.png")

    render_standalone_panel(628, 260, levels=CHORD_LEVELS, title="MASTER OUTPUT SPECTRUM ANALYZER").save(OUTPUT_DIR / "spectrum-rect-628x260-active.png")
    render_standalone_panel(628, 260, levels=None, title="MASTER OUTPUT SPECTRUM ANALYZER").save(OUTPUT_DIR / "spectrum-rect-628x260-silent.png")

    # 3. Master presentation boards
    print("  -> macros-spectrum-redesign-comparison.png")
    board_macros = render_macros_comparison_board(mac_bef_act, mac_opt1_act, mac_opt2_act)
    board_macros.save(OUTPUT_DIR / "macros-spectrum-redesign-comparison.png")

    print("  -> master-spectrum-redesign-comparison.png")
    board_master = render_master_comparison_board(mas_bef, mas_aft_act)
    board_master.save(OUTPUT_DIR / "master-spectrum-redesign-comparison.png")

    print("All preview artwork successfully generated in docs/ui-components/komponensek/!")


if __name__ == "__main__":
    main()
