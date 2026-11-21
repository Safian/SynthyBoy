#!/usr/bin/env python3
# Draw the MPC overlay function key and sub-tab strip showing the updated order.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "release" / "tabstrip-new-order.png"

# MPC Landscape resolution
WIDTH = 1280
HEIGHT = 160

BG_COLOR = (24, 25, 28, 255)
KEY_BG = (38, 40, 44, 255)
KEY_BORDER = (65, 68, 75, 255)
KEY_TEXT = (220, 224, 230, 255)
KEY_SUB_TEXT = (140, 145, 155, 255)
DOT_ACTIVE = (230, 90, 40, 255)
DOT_INACTIVE = (90, 95, 105, 255)
HEADER_TEXT = (180, 185, 195, 255)

FN_KEYS = [
    {"label": "Play", "subtabs": ["Play", "Macros"]},
    {"label": "Osc", "subtabs": ["Osc A", "Osc B", "Osc C", "Sub", "Noise"]},
    {"label": "Env", "subtabs": ["Env 1", "Env 2", "Env 3", "Env 4"]},
    {"label": "LFO", "subtabs": ["LFO 1", "LFO 2", "LFO 3", "LFO 4"]},
    {"label": "FX", "subtabs": ["FX"]},
    {"label": "Mix", "subtabs": ["Mixer"]},
    {"label": "Filter", "subtabs": ["Filter 1", "Filter 2"]},
    {"label": "Mod", "subtabs": ["Mod"]},
    {"label": "Master", "subtabs": ["Master", "Voice"]},
]

img = Image.new("RGBA", (WIDTH, HEIGHT), BG_COLOR)
draw = ImageDraw.Draw(img)

# Try loading system font, fallback to default
try:
    font_title = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 14)
    font_key = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
    font_sub = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 10)
except Exception:
    font_title = ImageFont.load_default()
    font_key = ImageFont.load_default()
    font_sub = ImageFont.load_default()

# Header banner
draw.rectangle([(0, 0), (WIDTH, 26)], fill=(18, 19, 21, 255))
draw.text((16, 5), "SynthyBoy MPC Overlay Skin — Function Key Order (9 Keys, 22 Sub-tabs | Play + Macros on F1)", fill=HEADER_TEXT, font=font_title)

# Draw 9 function keys
margin_x = 8
spacing = 6
total_w = WIDTH - 2 * margin_x
key_w = (total_w - (len(FN_KEYS) - 1) * spacing) / len(FN_KEYS)

key_y = 34
key_h = 118

for i, key in enumerate(FN_KEYS):
    kx = margin_x + i * (key_w + spacing)
    ky = key_y
    kr = kx + key_w
    kb = ky + key_h
    
    # Key button background & border
    draw.rounded_rectangle([(kx, ky), (kr, kb)], radius=6, fill=KEY_BG, outline=KEY_BORDER, width=1)
    
    # Main function key label (F1..F9)
    f_num = f"F{i+1}: {key['label']}"
    bbox = draw.textbbox((0, 0), f_num, font=font_key)
    tw = bbox[2] - bbox[0]
    draw.text((kx + (key_w - tw) / 2, ky + 8), f_num, fill=KEY_TEXT, font=font_key)
    
    # Sub-tab count dots indicator
    num_subs = len(key["subtabs"])
    dot_r = 3
    dot_gap = 5
    dots_total_w = num_subs * (2 * dot_r) + (num_subs - 1) * dot_gap
    start_dot_x = kx + (key_w - dots_total_w) / 2
    dot_y = ky + 30
    for d in range(num_subs):
        dx = start_dot_x + d * (2 * dot_r + dot_gap)
        color = DOT_ACTIVE if d == 0 else DOT_INACTIVE
        draw.ellipse([(dx, dot_y), (dx + 2 * dot_r, dot_y + 2 * dot_r)], fill=color)
        
    # Divider line inside button
    draw.line([(kx + 6, ky + 42), (kr - 6, ky + 42)], fill=(50, 53, 58, 255), width=1)
    
    # Sub-tab names listing
    sub_y = ky + 46
    for sub in key["subtabs"]:
        s_bbox = draw.textbbox((0, 0), sub, font=font_sub)
        stw = s_bbox[2] - s_bbox[0]
        draw.text((kx + (key_w - stw) / 2, sub_y), sub, fill=KEY_SUB_TEXT, font=font_sub)
        sub_y += 13

img.save(OUT, "PNG")
print(f"Wrote {OUT}")
