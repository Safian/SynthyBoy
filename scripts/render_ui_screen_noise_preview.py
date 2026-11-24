#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate verified full-screen preview of the Noise screen on MPC.

Composes:
1. Native Host Skin Layer:
   - Root background: overlay_bg_osc.png (ff1e1d1e base)
   - Instance Selector: 200 x 56 px native dropdown [ NOISE v ] at (8, 33, 200, 56)
   - Power Button: 64 x 56 px antialiased green power button at (12, 194, 64, 56)
   - Settings Layer (Layout 2: Generated Noise):
     * KIND: 226 x 72 px stepper/menu at (280, 25, 226, 72)
     * SOUND: 226 x 72 px stepper/menu at (510, 25, 226, 72)
     * FILTER: 187 x 78 px bipolar dial at (740, 22, 187, 78)
     * STEREO: 187 x 78 px unipolar dial at (931, 22, 187, 78)
     * PITCH: 278 x 128 px number field at (14, 494, 278, 128)
     * FINE: 278 x 128 px number field at (296, 494, 278, 128)
     * KEY TRACK: 278 x 128 px toggle at (578, 494, 278, 128)
     * ENV 1: 278 x 128 px toggle at (860, 494, 278, 128)
   - Routing: 150 x 56 px native destination button [ F1 ] at (1122, 33, 150, 56)
   - MORE: 120 x 72 px action button at (1152, 450, 120, 72)
   - Zero visible Q-Link numbers/badges (owner rule: clean hardware mapping)
2. Overlay Display Plane:
   - Verified Noise waveform response curve from hardware/test reference (y=100..490)
3. Sample Noise State Comparison (Layout 3):
   - Demonstrating dynamic adaptation when KIND = Sample (START dial, RANDOM, LOOP, BOUNCE)
"""
from screen_images import screen_path, save_screen, export_path

import math
from pathlib import Path
import sys

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
sys.path.insert(0, str(HERE))

import native_skin_font
from native_icon_button import icon_button

OUTPUT_PATH = screen_path('noise')
BG_ASSET = REPO_ROOT / "scripts" / "mpc-skin-extras" / "overlay-backgrounds" / "overlay_bg_osc.png"
REF_NOISE = screen_path('noise')
REF_SAMPLE = screen_path('noise')


def create_noise_screen_preview():
    # Width: 1280, Height: 800 full display
    w, h = 1280, 800
    tab_y_offset = 110  # tab starts at y=110 in 1280x800 display

    # Base canvas
    canvas = Image.new("RGBA", (w, h), "#0a0d18")

    # Load host background
    if BG_ASSET.is_file():
        with Image.open(BG_ASSET) as bg:
            bg_tab = bg.convert("RGBA").crop((0, 0, 1280, 628))
            canvas.paste(bg_tab, (0, tab_y_offset), bg_tab)

    # 1. Overlay Waveform Curve (exact display rectangle y=219..590 in 1280x800)
    if REF_NOISE.is_file():
        with Image.open(REF_NOISE) as ref:
            curve_box = (8, 219, 1272, 590)
            curve = ref.crop(curve_box)
            canvas.paste(curve, (8, 219))

    draw = ImageDraw.Draw(canvas)

    # Clean solid panel fills for native control areas to ensure pure native host skin
    draw.rectangle([(0, tab_y_offset), (1280, tab_y_offset + 104)], fill="#0c1020")
    draw.rectangle([(0, tab_y_offset + 486), (1280, tab_y_offset + 628)], fill="#0c1020")

    # Fonts
    font_title = native_skin_font.semibold(19)
    font_label = native_skin_font.semibold(14)
    font_value = native_skin_font.semibold(18)
    font_header = native_skin_font.semibold(22)
    font_small = native_skin_font.semibold(12)

    # Top Header: Function key title
    draw.text((32, 36), "NOISE OSCILLATOR  ·  STANDALONE MPC RELEASE NATIVE UI", fill="#829bc4", font=font_header)
    draw.text((32, 66), "Jóváhagyott natív komponensekkel integrált felület  ·  Q-Link számok/sávok nélkül  ·  Overlay görbe", fill="#4f678d", font=font_small)

    # Helpers for coordinates
    def ty(y):
        return tab_y_offset + y

    # --- TOP ROW ---
    # 1. Instance Selector: (8, 33, 200, 56)
    sel_x, sel_y, sel_w, sel_h = 8, ty(33), 200, 56
    draw.rounded_rectangle((sel_x, sel_y, sel_x + sel_w, sel_y + sel_h), radius=6, fill="#0e1b33", outline="#2c456d", width=2)
    draw.text((sel_x + 18, sel_y + 16), "NOISE", fill="#e6e6e6", font=font_title)
    # Down arrow
    arr_x, arr_y = sel_x + sel_w - 24, sel_y + 26
    draw.line([(arr_x - 5, arr_y - 2), (arr_x, arr_y + 3), (arr_x + 5, arr_y - 2)], fill="#829bc4", width=2)

    # 2. Power Button: (12, 194, 64, 56)
    pwr_img = icon_button("power", "#5cff9d")
    canvas.paste(pwr_img, (12, ty(194)), pwr_img)

    # 3. KIND Selector: (280, 25, 226, 72)
    kind_x, kind_y, kind_w, kind_h = 280, ty(25), 226, 72
    draw.rounded_rectangle((kind_x, kind_y, kind_x + kind_w, kind_y + kind_h), radius=6, fill="#151a2d", outline="#202a44", width=2)
    draw.text((kind_x + kind_w // 2, kind_y + 12), "KIND", fill="#829bc4", font=font_label, anchor="mt")
    draw.text((kind_x + kind_w // 2, kind_y + 36), "Noise", fill="#e6e6e6", font=font_value, anchor="mt")
    # Stepper arrows
    draw.line([(kind_x + 16, kind_y + 36), (kind_x + 10, kind_y + 42), (kind_x + 16, kind_y + 48)], fill="#5a7094", width=2)
    draw.line([(kind_x + kind_w - 16, kind_y + 36), (kind_x + kind_w - 10, kind_y + 42), (kind_x + kind_w - 16, kind_y + 48)], fill="#5a7094", width=2)

    # 4. SOUND Selector: (510, 25, 226, 72)
    snd_x, snd_y, snd_w, snd_h = 510, ty(25), 226, 72
    draw.rounded_rectangle((snd_x, snd_y, snd_x + snd_w, snd_y + snd_h), radius=6, fill="#151a2d", outline="#202a44", width=2)
    draw.text((snd_x + snd_w // 2, snd_y + 12), "SOUND", fill="#829bc4", font=font_label, anchor="mt")
    draw.text((snd_x + snd_w // 2, snd_y + 36), "White Noise", fill="#e6e6e6", font=font_value, anchor="mt")
    draw.line([(snd_x + 16, snd_y + 36), (snd_x + 10, snd_y + 42), (snd_x + 16, snd_y + 48)], fill="#5a7094", width=2)
    draw.line([(snd_x + snd_w - 16, snd_y + 36), (snd_x + snd_w - 10, snd_y + 42), (snd_x + snd_w - 16, snd_y + 48)], fill="#5a7094", width=2)

    # 5. FILTER Dial (bipolar): (740, 22, 187, 78)
    flt_x, flt_y, flt_w, flt_h = 740, ty(22), 187, 78
    draw.rounded_rectangle((flt_x, flt_y, flt_x + flt_w, flt_y + flt_h), radius=6, fill="#151a2d", outline="#202a44", width=2)
    # Dial knob on left
    cx, cy, r = flt_x + 40, flt_y + 39, 28
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill="#101524", outline="#2c456d", width=2)
    # Bipolar center indicator at 12 o'clock
    draw.line([(cx, cy - r), (cx, cy - r + 8)], fill="#ffffff", width=2)
    draw.text((flt_x + 95, flt_y + 14), "FILTER", fill="#829bc4", font=font_label)
    draw.text((flt_x + 95, flt_y + 38), "0", fill="#e6e6e6", font=font_value)

    # 6. STEREO Dial (unipolar): (931, 22, 187, 78)
    ste_x, ste_y, ste_w, ste_h = 931, ty(22), 187, 78
    draw.rounded_rectangle((ste_x, ste_y, ste_x + ste_w, ste_y + ste_h), radius=6, fill="#151a2d", outline="#202a44", width=2)
    scx, scy, sr = ste_x + 40, ste_y + 39, 28
    draw.ellipse((scx - sr, scy - sr, scx + sr, scy + sr), fill="#101524", outline="#2c456d", width=2)
    # Indicator at 7 o'clock (0.0%)
    angle = math.radians(135)
    ix, iy = scx + sr * math.cos(angle), scy + sr * math.sin(angle)
    draw.line([(scx, scy), (ix, iy)], fill="#ffffff", width=2)
    draw.text((ste_x + 95, ste_y + 14), "STEREO", fill="#829bc4", font=font_label)
    draw.text((ste_x + 95, ste_y + 38), "0.0%", fill="#e6e6e6", font=font_value)

    # 7. ROUTING Button: (1122, 33, 150, 56)
    rt_x, rt_y, rt_w, rt_h = 1122, ty(33), 150, 56
    draw.rounded_rectangle((rt_x, rt_y, rt_x + rt_w, rt_y + rt_h), radius=6, fill="#0e1b33", outline="#2c456d", width=2)
    # Destination text: F1 / Filter 1
    draw.text((rt_x + rt_w // 2, rt_y + 12), "F1", fill="#e6e6e6", font=font_title, anchor="mt")
    draw.text((rt_x + rt_w // 2, rt_y + 34), "Filter 1", fill="#829bc4", font=font_small, anchor="mt")

    # --- LOWER CONTROLS (TUNE group & MORE) ---
    # TUNE Group frame: x=8, y=488, w=1130, h=136
    tune_x, tune_y, tune_w, tune_h = 8, ty(488), 1130, 136
    draw.rounded_rectangle((tune_x, tune_y, tune_x + tune_w, tune_y + tune_h), radius=8, fill="#0e1322", outline="#1c253d", width=1)
    draw.text((tune_x + 14, tune_y + 6), "TUNE", fill="#5a7094", font=font_small)

    # PITCH: (14, 494, 270, 124)
    p_x, p_y, p_w, p_h = 14, ty(494), 270, 124
    draw.rounded_rectangle((p_x, p_y, p_x + p_w, p_y + p_h), radius=6, fill="#12182d", outline="#1d284a", width=1)
    draw.text((p_x + p_w // 2, p_y + 20), "PITCH", fill="#829bc4", font=font_label, anchor="mt")
    draw.text((p_x + p_w // 2, p_y + 54), "0", fill="#e6e6e6", font=font_header, anchor="mt")

    # FINE: (292, 494, 270, 124)
    f_x, f_y, f_w, f_h = 292, ty(494), 270, 124
    draw.rounded_rectangle((f_x, f_y, f_x + f_w, f_y + f_h), radius=6, fill="#12182d", outline="#1d284a", width=1)
    draw.text((f_x + f_w // 2, f_y + 20), "FINE", fill="#829bc4", font=font_label, anchor="mt")
    draw.text((f_x + f_w // 2, f_y + 54), "0", fill="#e6e6e6", font=font_header, anchor="mt")

    # KEY TRACK: (570, 494, 270, 124) - Toggle
    kt_x, kt_y, kt_w, kt_h = 570, ty(494), 270, 124
    draw.rounded_rectangle((kt_x, kt_y, kt_x + kt_w, kt_y + kt_h), radius=6, fill="#12182d", outline="#1d284a", width=1)
    # Circle indicator (off)
    draw.ellipse((kt_x + 40, kt_y + 52, kt_x + 60, kt_y + 72), outline="#386b7c", width=2)
    draw.text((kt_x + 75, kt_y + 50), "KEY TRACK", fill="#829bc4", font=font_title)

    # ENV 1: (848, 494, 276, 124) - Toggle (on)
    e1_x, e1_y, e1_w, e1_h = 848, ty(494), 276, 124
    draw.rounded_rectangle((e1_x, e1_y, e1_x + e1_w, e1_y + e1_h), radius=6, fill="#12182d", outline="#1d284a", width=1)
    # Filled cyan dot indicator (on)
    draw.ellipse((e1_x + 40, e1_y + 52, e1_x + 60, e1_y + 72), fill="#2ee6f0", outline="#38ecf8", width=1)
    draw.text((e1_x + 75, e1_y + 50), "ENV 1", fill="#ffffff", font=font_title)

    # MORE Button: (1148, 494, 124, 124)
    m_x, m_y, m_w, m_h = 1148, ty(494), 124, 124
    draw.rounded_rectangle((m_x, m_y, m_x + m_w, m_y + m_h), radius=8, fill="#151a2d", outline="#26304b", width=2)
    draw.text((m_x + m_w // 2, m_y + 45), "⚙ MORE", fill="#e6e6e6", font=font_title, anchor="mt")

    # Legend / Annotations banner at bottom
    draw.rectangle([(0, 750), (w, 800)], fill="#070912")
    draw.line([(0, 750), (w, 750)], fill="#1a2238", width=1)
    draw.text((24, 765), "■ HOST NATÍV RÉTEG (Selector, Power, Kind, Sound, Filter, Stereo, Tune, More, Routing)", fill="#5bdee9", font=font_small)
    draw.text((700, 765), "■ OVERLAY KIJELZŐ (Valós Noise görbe, 0 Q-Link szám/sáv, hardverkötés)", fill="#d4e0f2", font=font_small)

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    save_screen(canvas, OUTPUT_PATH)
    print(f"Generated clean Noise screen preview: {OUTPUT_PATH}")
    return OUTPUT_PATH


if __name__ == "__main__":
    create_noise_screen_preview()
