#!/usr/bin/env python3
# SynthyBoy - Generate verified factory MPC radio selector comparison assets.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# Setup paths
HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
sys.path.append(str(HERE))
sys.path.append("/Users/szabi/Library/Python/3.9/lib/python/site-packages")

from PIL import Image, ImageDraw, ImageFont
import native_skin_font
from native_choice_group import render_segmented_button_art

OUT_DIR = REPO_ROOT / "docs" / "ui-components" / "komponensek"
OUT_DIR.mkdir(parents=True, exist_ok=True)

# Fonts
font_title = native_skin_font.semibold(24)
font_subtitle = native_skin_font.semibold(18)
font_label = native_skin_font.semibold(16)
font_small = native_skin_font.semibold(13)
font_tiny = native_skin_font.semibold(11)

print("Building verified factory crops and SynthyBoy HZ/BPM/KEY comparison...")

PDF_PATH = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/manuals-extended/MPC Standalone OS - User Guide - v3.10.1.pdf")
PDFTOPPM = "/Users/szabi/.cache/codex-runtimes/codex-primary-runtime/dependencies/bin/override/pdftoppm"
if not Path(PDFTOPPM).exists():
    PDFTOPPM = "pdftoppm"

with tempfile.TemporaryDirectory() as tmpdir:
    tmp_path = Path(tmpdir)
    
    # -----------------------------------------------------------------------------
    # Render PDF pages 41 and 80 at 300 DPI
    # -----------------------------------------------------------------------------
    subprocess.run([PDFTOPPM, "-png", "-f", "41", "-l", "41", "-r", "300", str(PDF_PATH), str(tmp_path / "p41_300")], check=True)
    subprocess.run([PDFTOPPM, "-png", "-f", "80", "-l", "80", "-r", "300", str(PDF_PATH), str(tmp_path / "p80_300")], check=True)
    
    p41_path = tmp_path / "p41_300-041.png"
    p80_path = tmp_path / "p80_300-080.png"
    
    p41_im = Image.open(p41_path)
    p80_im = Image.open(p80_path)
    
    # -----------------------------------------------------------------------------
    # 1. Factory Manual Page 41 Selector Crop
    # -----------------------------------------------------------------------------
    p41_crop = p41_im.crop((150, 240, 2350, 660))
    # Exact waveform selector
    p41_exact = p41_im.crop((1578, 307, 2329, 432))
    
    card_p41_w = 1200
    card_p41_h = 320
    card_p41 = Image.new("RGB", (card_p41_w, card_p41_h), "#10131a")
    draw_p41 = ImageDraw.Draw(card_p41)
    
    draw_p41.rectangle([(0, 0), (card_p41_w, 40)], fill="#1b202b")
    draw_p41.text((16, 10), "HIVATALOS GYÁRI FORRÁS: Akai MPC Standalone OS User Guide v3.10.1, 41. oldal", font=font_small, fill="#eef1ff")
    
    scaled_crop = p41_crop.resize((1168, int(p41_crop.height * (1168 / p41_crop.width))), Image.Resampling.LANCZOS)
    card_p41.paste(scaled_crop, (16, 52))
    draw_p41.text((16, 290), "Forrásfájl: Safian/.hw-local/mpc-knowledge/manuals-extended/MPC Standalone OS - User Guide - v3.10.1.pdf (ug310.pdf: 41. o.)", font=font_tiny, fill="#829bc4")
    card_p41.save(OUT_DIR / "radio-gyari-manual-p41.png")
    print("Saved radio-gyari-manual-p41.png")

    # -----------------------------------------------------------------------------
    # 2. Factory Pad Perform Page 80 Selector Crop
    # -----------------------------------------------------------------------------
    s1_row = p80_im.crop((330 + 30, 220 + 60, 330 + 320, 220 + 140))
    s2_row = p80_im.crop((330 + 30, 910 + 60, 330 + 320, 910 + 140))
    
    card_p80_w = 800
    card_p80_h = 240
    card_p80 = Image.new("RGB", (card_p80_w, card_p80_h), "#10131a")
    draw_p80 = ImageDraw.Draw(card_p80)
    draw_p80.rectangle([(0, 0), (card_p80_w, 36)], fill="#1b202b")
    draw_p80.text((16, 8), "GYÁRI MPC KÉPERNYŐKIVÁGÁS: Pad Perform módválasztó (3 állapotú kizáró sor)", font=font_small, fill="#eef1ff")
    
    card_p80.paste(s1_row, (20, 50))
    draw_p80.text((330, 75), "1. állapot: NOTES aktív (kiemelt háttér, sötét felirat)", font=font_small, fill="#bec8d9")
    card_p80.paste(s2_row, (20, 140))
    draw_p80.text((330, 165), "2. állapot: CHORDS aktív (sötét NOTES, kiemelt CHORDS)", font=font_small, fill="#bec8d9")
    draw_p80.text((16, 215), "Forrás: Akai MPC Standalone OS User Guide v3.10.1, 80. oldal (Pad Perform Mode)", font=font_tiny, fill="#829bc4")
    card_p80.save(OUT_DIR / "radio-gyari-padperform-p80.png")
    print("Saved radio-gyari-padperform-p80.png")

    # -----------------------------------------------------------------------------
    # 3. Akai MPC SDK Sample UI Kit (mpc-red seg3)
    # -----------------------------------------------------------------------------
    sdk_red_dir = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk/ui-kit/mpc-red")
    seg3_on = Image.open(sdk_red_dir / "seg3_on.png")
    seg3_off = Image.open(sdk_red_dir / "seg3_off.png")
    
    card_sdk_w = 700
    card_sdk_h = 220
    card_sdk = Image.new("RGB", (card_sdk_w, card_sdk_h), "#10131a")
    draw_sdk = ImageDraw.Draw(card_sdk)
    draw_sdk.rectangle([(0, 0), (card_sdk_w, 36)], fill="#1b202b")
    draw_sdk.text((16, 8), "AKAI MPC SDK MINTA SKIN (mpc-red UI-kit) — NEM GYÁRI FIRMWARE!", font=font_small, fill="#ff8282")
    
    labels = ["HZ", "BPM", "KEY"]
    for state_idx in range(3):
        y = 50 + state_idx * 50
        for btn_idx in range(3):
            x = 30 + btn_idx * 80
            is_on = (btn_idx == state_idx)
            img = seg3_on if is_on else seg3_off
            card_sdk.paste(img, (x, y), img)
            txt = labels[btn_idx]
            txt_fill = "#ffffff" if is_on else "#aab2d8"
            draw_sdk.text((x + 40, y + 20), txt, anchor="mm", font=font_small, fill=txt_fill)
        state_desc = f"{labels[state_idx]} kiválasztva (numButtonsInGroup: 3, buttonId: {state_idx})"
        draw_sdk.text((290, y + 12), state_desc, font=font_tiny, fill="#bec8d9")
    
    draw_sdk.text((16, 198), "Forrás: /Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk", font=font_tiny, fill="#829bc4")
    card_sdk.save(OUT_DIR / "radio-sdk-seg3.png")
    print("Saved radio-sdk-seg3.png")

    # -----------------------------------------------------------------------------
    # 4. SynthyBoy Native Segmented HZ / BPM / KEY Rows (Approved Variant 1)
    # -----------------------------------------------------------------------------
    def draw_native_segmented_row(active_index, width=216, height=48):
        im = Image.new("RGBA", (width, height))
        for i in range(3):
            left = width * i // 3
            right = width * (i + 1) // 3
            btn = render_segmented_button_art(labels[i], i == active_index, right - left, height)
            im.paste(btn, (left, 0), btn)
        return im
    
    card_native_w = 720
    card_native_h = 330
    card_native = Image.new("RGB", (card_native_w, card_native_h), "#10131a")
    draw_nat = ImageDraw.Draw(card_native)
    draw_nat.rectangle([(0, 0), (card_native_w, 36)], fill="#1b202b")
    draw_nat.text((16, 8), "SYNTHYBOY SAJÁT NATÍV HZ / BPM / KEY KIZÁRÓ SOROK (48 px magasság)", font=font_small, fill="#eef1ff")
    
    for i in range(3):
        y = 52 + i * 85
        row_img = draw_native_segmented_row(i, width=216, height=48)
        card_native.paste(row_img, (30, y), row_img)
        draw_nat.text((265, y + 14), f"Állapot {i+1}: {labels[i]} aktív (Jóváhagyott 1-es paletta: cián #2EE6F0 / sötét)", font=font_small, fill="#eef1ff")
        draw_nat.text((265, y + 32), f"Méret: 216 × 48 px (3 × 72 px szegmens), érintőcél >= 56 px", font=font_tiny, fill="#829bc4")
    
    draw_nat.text((16, 305), "Definíció: shell-mpc/ui/AGENTS.md & scripts/native_choice_group.py (VARIANT_SEGMENTED)", font=font_tiny, fill="#829bc4")

    # -----------------------------------------------------------------------------
    # 5. SynthyBoy LedList UI Component (HZ / BPM / KEY)
    # -----------------------------------------------------------------------------
    def draw_led_list_row(active_index, width=220, height=48):
        scale = 2
        sw, sh = width * scale, height * scale
        im = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
        draw = ImageDraw.Draw(im)
        seg_w = sw // 3
        font_led = native_skin_font.semibold(14 * scale)
        
        for i in range(3):
            is_active = (i == active_index)
            cx = i * seg_w + 18 * scale
            cy = sh // 2
            if is_active:
                draw.ellipse([(cx - 4*scale, cy - 4*scale), (cx + 4*scale, cy + 4*scale)], fill="#ffc23d")
            else:
                draw.ellipse([(cx - 4*scale, cy - 4*scale), (cx + 4*scale, cy + 4*scale)], outline="#465064", width=scale)
                
            txt = labels[i]
            col = "#eef1ff" if is_active else "#768299"
            draw.text((cx + 12 * scale, cy), txt, anchor="lm", font=font_led, fill=col)
            
        return im.resize((width, height), Image.Resampling.LANCZOS)
    
    card_led_w = 720
    card_led_h = 240
    card_led = Image.new("RGB", (card_led_w, card_led_h), "#10131a")
    draw_led = ImageDraw.Draw(card_led)
    draw_led.rectangle([(0, 0), (card_led_w, 36)], fill="#1b202b")
    draw_led.text((16, 8), "SYNTHYBOY OVERLAY UI-KOMPONENS: LedList (SyncSwitch: HZ, BPM, KEY)", font=font_small, fill="#eef1ff")
    
    for i in range(3):
        y = 50 + i * 55
        led_img = draw_led_list_row(i, width=220, height=44)
        card_led.paste(led_img, (30, y), led_img)
        draw_led.text((270, y + 12), f"LedList {labels[i]} aktív: keret nélküli LED-pont + felirat (shell-mpc/ui/led_list.h)", font=font_small, fill="#eef1ff")
    
    draw_led.text((16, 215), "Forrás: shell-mpc/ui/led_list.h & lfo_parts.h (SyncSwitch::mode, RateControl::modeCells)", font=font_tiny, fill="#829bc4")
    card_led.save(OUT_DIR / "radio-sajat-hz-bpm-key-ledlist.png")
    print("Saved radio-sajat-hz-bpm-key-ledlist.png")

    # -----------------------------------------------------------------------------
    # 6. Master Side-by-Side Comparison Graphic (radio-gyari-vs-sajat-osszehasonlitas.png)
    # -----------------------------------------------------------------------------
    comp_w = 1380
    comp_h = 880
    comp = Image.new("RGB", (comp_w, comp_h), "#0b1026")
    draw_c = ImageDraw.Draw(comp)
    
    draw_c.rectangle([(0, 0), (comp_w, 64)], fill="#141a38")
    draw_c.line([(0, 64), (comp_w, 64)], fill="#27306a", width=2)
    draw_c.text((24, 16), "IGAZOLT GYÁRI AKAI MPC KIZÁRÓ VÁLASZTÓ (SELECTOR) ÉS A SYNTHYBOY HZ / BPM / KEY SOR", font=font_title, fill="#eef1ff")
    
    col_w = (comp_w - 72) // 2
    
    # LEFT COLUMN: Factory Sources
    left_x = 24
    draw_c.rounded_rectangle([(left_x, 80), (left_x + col_w, comp_h - 24)], radius=8, fill="#151a2d", outline="#27306a", width=2)
    draw_c.rectangle([(left_x, 80), (left_x + col_w, 120)], fill="#1f2742")
    draw_c.text((left_x + 16, 92), "1. IGAZOLT GYÁRI AKAI MPC FORRÁSOK (FIRMWARE / KÉZIKÖNYV)", font=font_subtitle, fill="#ffc23d")
    
    draw_c.text((left_x + 16, 134), "A) Hivatalos Selector definíció (Akai MPC OS User Guide v3.10.1, 41. o.)", font=font_label, fill="#eef1ff")
    draw_c.text((left_x + 16, 158), '„Selectors show all available options. One of them is always selected.\nThe selected option is highlighted while the others are dark. To select an option, tap it."', font=font_tiny, fill="#aab2d8")
    
    target_w = col_w - 32
    target_h = int(p41_exact.height * (target_w / p41_exact.width))
    scaled_p41 = p41_exact.resize((target_w, target_h), Image.Resampling.LANCZOS)
    draw_c.rectangle([(left_x + 14, 202), (left_x + 18 + target_w, 206 + target_h)], fill="#000000", outline="#465064")
    comp.paste(scaled_p41, (left_x + 16, 204))
    draw_c.text((left_x + 16, 214 + target_h), "7-tagú horizontális rádiósor (LFO hullámformák, 2. tag [Triangle] fehérrel kiemelve)", font=font_tiny, fill="#829bc4")
    
    pp_y = 360
    draw_c.text((left_x + 16, pp_y), "B) Valódi MPC képernyőkivágás: Pad Perform módválasztó (80. o.)", font=font_label, fill="#eef1ff")
    draw_c.text((left_x + 16, pp_y + 24), "Háromállapotú kizáró mező: NOTES / CHORDS / PROGRESSIONS", font=font_tiny, fill="#aab2d8")
    
    comp.paste(s1_row, (left_x + 16, pp_y + 50))
    draw_c.text((left_x + 325, pp_y + 70), "• NOTES aktív: világos szürke háttér,\nsötét felirat (a többi sötét marad)", font=font_tiny, fill="#bec8d9")
    
    comp.paste(s2_row, (left_x + 16, pp_y + 140))
    draw_c.text((left_x + 325, pp_y + 160), "• CHORDS aktív: azonnali kizáró\nátbillenés az érintett gombon", font=font_tiny, fill="#bec8d9")
    
    draw_c.text((left_x + 16, comp_h - 60), "Hiteles forrás: .hw-local/mpc-knowledge/manuals-extended/ (Standalone OS v3.10.1)", font=font_tiny, fill="#768299")
    
    # RIGHT COLUMN: SynthyBoy Solutions & SDK distinction
    right_x = left_x + col_w + 24
    draw_c.rounded_rectangle([(right_x, 80), (right_x + col_w, comp_h - 24)], radius=8, fill="#151a2d", outline="#27306a", width=2)
    draw_c.rectangle([(right_x, 80), (right_x + col_w, 120)], fill="#1f2742")
    draw_c.text((right_x + 16, 92), "2. SAJÁT HZ / BPM / KEY SOROK ÉS AZ SDK-MEGKÜLÖNBÖZTETÉS", font=font_subtitle, fill="#5cff9d")
    
    draw_c.text((right_x + 16, 134), "A) SynthyBoy natív szegmentált rádiósor (48 px gombmagasság)", font=font_label, fill="#eef1ff")
    draw_c.text((right_x + 16, 158), "Szabványos 3-tagú Button-csoport (numButtonsInGroup: 3, buttonId: 0..2)", font=font_tiny, fill="#aab2d8")
    
    for idx, label_name in enumerate(["HZ", "BPM", "KEY"]):
        y_row = 186 + idx * 58
        nat_row = draw_native_segmented_row(idx, width=202, height=44)
        comp.paste(nat_row, (right_x + 16, y_row), nat_row)
        draw_c.text((right_x + 235, y_row + 12), f"• {label_name} kiválasztva (aktív: #2EE6F0, inaktív: #1A2238)", font=font_small, fill="#eef1ff")
    
    led_y = 380
    draw_c.text((right_x + 16, led_y), "B) SynthyBoy UI-komponens: LedList (shell-mpc/ui/led_list.h)", font=font_label, fill="#eef1ff")
    draw_c.text((right_x + 16, led_y + 24), "Keret nélküli LED-pontos kizáró választó (SyncSwitch LFO szinkronhoz)", font=font_tiny, fill="#aab2d8")
    
    for idx, label_name in enumerate(["HZ", "BPM", "KEY"]):
        y_led = led_y + 50 + idx * 46
        led_row = draw_led_list_row(idx, width=202, height=40)
        comp.paste(led_row, (right_x + 16, y_led), led_row)
        draw_c.text((right_x + 235, y_led + 10), f"• {label_name} aktív: megvilágított pont + #EEF1FF felirat", font=font_small, fill="#bec8d9")
    
    sdk_y = 540
    draw_c.rounded_rectangle([(right_x + 16, sdk_y), (right_x + col_w - 16, comp_h - 40)], radius=6, fill="#0c1026", outline="#ff4f4f", width=1)
    draw_c.text((right_x + 26, sdk_y + 10), "FONTOS: AKAI MPC SDK MINTA SKIN (mpc-red) — NEM GYÁRI FIRMWARE!", font=font_small, fill="#ff4f4f")
    draw_c.text((right_x + 26, sdk_y + 32), "Útvonal: /Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk", font=font_tiny, fill="#bec8d9")
    draw_c.text((right_x + 26, sdk_y + 50), "Az SDK-ban lévő seg3_on.png / seg3_off.png példabőr VST beépülőknek, a standalone", font=font_tiny, fill="#bec8d9")
    draw_c.text((right_x + 26, sdk_y + 68), "MPC firmware UI a fent balra látható natív szürke/fehér és sötét stílust alkalmazza.", font=font_tiny, fill="#bec8d9")
    
    for b in range(3):
        img = seg3_on if b == 0 else seg3_off
        comp.paste(img, (right_x + 26 + b * 82, sdk_y + 92), img)
        txt_col = "#ffffff" if b == 0 else "#aab2d8"
        draw_c.text((right_x + 26 + b * 82 + 40, sdk_y + 112), labels[b], anchor="mm", font=font_small, fill=txt_col)
    
    draw_c.text((right_x + 290, sdk_y + 104), "SDK seg3 minta (80×40 px)\npiros aktív állapottal", font=font_tiny, fill="#829bc4")
    
    comp.save(OUT_DIR / "radio-gyari-vs-sajat-osszehasonlitas.png")
    print("Saved master comparison: radio-gyari-vs-sajat-osszehasonlitas.png")

    from render_native_catalog_cards import render_radio_cards
    render_radio_cards()

print("All radio preview assets successfully generated!")

