#!/usr/bin/env python3
"""
Generate clean, authentic comparison graphic between legacy/before Macros layout
and the owner-approved Terv A production layout with compact MacroCard.

Ensures:
- Real authentic composite images (no hand-drawn mockups).
- Clear component details and touch hitbox table.
- Strict canvas margins to avoid right-side text clipping (fixing 597eaed6f3 defect).
- Compact target action button geometry (125x56 at x=100) aligned with reading column.
"""

import tempfile
from pathlib import Path
from screen_images import screen_path, require_external_output
import sys

SITE_PACKAGES = Path("/Users/szabi/Library/Python/3.9/lib/python/site-packages")
if SITE_PACKAGES.exists() and str(SITE_PACKAGES) not in sys.path:
    sys.path.insert(0, str(SITE_PACKAGES))

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from components.native.macro_card.macro_card import (
    CARD_HEIGHT,
    CARD_WIDTH,
    NAME_HIT_RECT,
    NAME_RECT,
    NATIVE_KNOB_RECT,
    TARGET_RECT,
    VALUE_RECT,
)
from components.style import size as _style_size

FONTS = {
    "Bold": "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "Regular": "/System/Library/Fonts/Supplemental/Arial.ttf",
}

def get_font(style: str, size: int):
    path = FONTS.get(style, FONTS["Regular"])
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()

def render_comparison(out_path: Path):
    W, H = 1280, 1140
    img = Image.new("RGB", (W, H), (11, 16, 37))
    draw = ImageDraw.Draw(img)

    f_title = get_font("Bold", 22)
    f_sub = get_font("Regular", 13)
    f_sec = get_font("Bold", 15)
    f_body = get_font("Regular", 12)
    f_body_bold = get_font("Bold", 12)
    f_small = get_font("Regular", 11)

    # 1. Header bar
    draw.rectangle([(0, 0), (W, 76)], fill=(14, 20, 48))
    draw.line([(0, 76), (W, 76)], fill=(39, 48, 106), width=1)
    draw.text((28, 16), "SYNTHYBOY MPC — MACROS KÉPERNYŐ ÉS KOMPONENS ÖSSZEVETÉS", fill=(255, 255, 255), font=f_title)
    draw.text((28, 44), "8 kompakt kártya alul egy sorban (152×238 px), felső nagy Stúdió Spektrum (1264×290 px) és tiszta fejléc • szintetikus fixture", fill=(143, 164, 200), font=f_sub)

    # 2. Top Screen Previews
    y_previews = 88
    draw.text((28, y_previews), "KORÁBBI ELRENDEZÉS (Legacy 2×4 rács + vékony alsó spektrum)", fill=(200, 210, 230), font=f_sec)
    draw.text((660, y_previews), "ÚJ 8-AS ALSÓ SOR KOMPOZIT (Natív Skin + C++ Overlay)", fill=(114, 209, 236), font=f_sec)

    before_path = screen_path("macros").with_name("macros_old.jpg")
    after_path = screen_path("macros")

    preview_w, preview_h = 592, 290
    if before_path.exists():
        with Image.open(before_path) as b_img:
            b_scaled = b_img.resize((preview_w, preview_h), Image.Resampling.LANCZOS)
            img.paste(b_scaled, (28, y_previews + 26))
            draw.rectangle([(28, y_previews + 26), (28 + preview_w, y_previews + 26 + preview_h)], outline=(39, 48, 106), width=1)

    if after_path.exists():
        with Image.open(after_path) as a_img:
            a_scaled = a_img.resize((preview_w, preview_h), Image.Resampling.LANCZOS)
            img.paste(a_scaled, (660, y_previews + 26))
            draw.rectangle([(660, y_previews + 26), (660 + preview_w, y_previews + 26 + preview_h)], outline=(53, 104, 192), width=2)

    # 3. Architecture summary banner
    y_arch = y_previews + preview_h + 36
    draw.rectangle([(28, y_arch), (W - 28, y_arch + 50)], fill=(16, 24, 58), outline=(39, 48, 106), width=1)
    
    spec_r = _style_size("macros.spectrum_rect")
    zone_top = f"• FELSŐ ZÓNA: Tiszta fejléc + teljes szélességű Stúdió Spektrum {{{spec_r[0]}, {spec_r[1]}, {spec_r[2]}, {spec_r[3]}}}"
    zone_bot = f"• ALSÓ SOR: Mind a 8 MacroCard egy sorban ({CARD_WIDTH}×{CARD_HEIGHT} px, y=382..620), fix Bank 1 (1-4) + Bank 2 (5-8)"
    draw.text((40, y_arch + 10), zone_top, fill=(114, 209, 236), font=f_body_bold)
    draw.text((40, y_arch + 28), zone_bot, fill=(255, 255, 255), font=f_body_bold)
    draw.text((760, y_arch + 10), "• Nincs felső bankválasztó vagy lapozás", fill=(143, 164, 200), font=f_body)
    draw.text((760, y_arch + 28), "• Key 61 sárga Q-Link bankkeret / XL keret nélkül", fill=(143, 164, 200), font=f_body)

    # 4. Lower section: Component details (Left) and Hitbox table (Right)
    y_lower = y_arch + 64
    
    # Left column: MacroCard from shared sizes.json contract
    draw.text((28, y_lower), f"NATÍV MACROCARD ({CARD_WIDTH} × {CARD_HEIGHT} px)", fill=(255, 255, 255), font=f_sec)
    catalog_path = ROOT / "components" / "native" / "macro_card" / "img" / "catalog-macro-card.png"
    if catalog_path.exists():
        with Image.open(catalog_path) as c_img:
            img.paste(c_img, (28, y_lower + 26))
            draw.rectangle([(28, y_lower + 26), (28 + c_img.width, y_lower + 26 + c_img.height)], outline=(53, 104, 192), width=1)
            
    y_card_desc = y_lower + 26 + CARD_HEIGHT + 14
    card_bullets = [
        ("Dial", "90×90 px unipolar kék (#3568c0), 127 frame"),
        ("Érték", f"y={VALUE_RECT[1]}..{VALUE_RECT[1]+VALUE_RECT[3]} középre igazítva (skin tulajdon)"),
        ("Név gomb", f"x={NAME_HIT_RECT[0]}, y={NAME_HIT_RECT[1]}..{NAME_HIT_RECT[1]+NAME_HIT_RECT[3]} ({NAME_HIT_RECT[3]} px), felirat y={NAME_RECT[1]}..{NAME_RECT[1]+NAME_RECT[3]}"),
        ("Target", f"x={TARGET_RECT[0]}, y={TARGET_RECT[1]}, {TARGET_RECT[2]}×{TARGET_RECT[3]} px hitbox (48 px gomb)"),
        ("Szerződés", "Közös MacroCard 152×238 (sizes.json -> C++/Py)"),
        ("Érintés", "Minden érintési zóna >= 56 px, átfedésmentes"),
    ]
    for label, desc in card_bullets:
        draw.text((28, y_card_desc), f"• {label}:", fill=(114, 209, 236), font=f_body_bold)
        draw.text((112, y_card_desc), desc, fill=(220, 230, 245), font=f_small)
        y_card_desc += 22

    # Right column: Hitbox & Ownership Table
    table_x = 450
    table_w = W - 28 - table_x  # 1280 - 28 - 450 = 802 px width
    draw.text((table_x, y_lower), "ÉRINTÉSI ÉS VEZÉRLŐI TULAJDONLÁSI TÁBLÁZAT", fill=(255, 255, 255), font=f_sec)

    cols = [
        ("Vezérlő elem", 140),
        ("Hitbox / Méret", 130),
        ("Paraméter / Funkció", 260),
        ("Réteg / Szabály", 190),
    ]
    
    t_y = y_lower + 26
    # Header row
    draw.rectangle([(table_x, t_y), (table_x + table_w, t_y + 26)], fill=(20, 30, 70), outline=(39, 48, 106), width=1)
    cur_x = table_x + 10
    for header, width in cols:
        draw.text((cur_x, t_y + 6), header, fill=(114, 209, 236), font=f_body_bold)
        cur_x += width

    rows = [
        ("Macro Dial 1..8", f"{NATIVE_KNOB_RECT[2]} × {NATIVE_KNOB_RECT[3]} px (≥56)", "macro.1 .. macro.8 (0..100%)", "Host Skin (macroSubLevel)"),
        ("Macro Név 1..8", f"{NAME_HIT_RECT[2]} × {NAME_HIT_RECT[3]} px", "TextEntry átnevezés (500+i)", "C++ Overlay (középre igazítva)"),
        ("Target Akció 1..8", f"{TARGET_RECT[2]} × {TARGET_RECT[3]} px", "kPageTargets (50) moduláció", "Host Skin (48 px gomb + 56 px hitbox)"),
        ("SpectrumStrip", f"{spec_r[2]} × {spec_r[3]} px", "20 Hz .. 20 kHz log frekvencia", "C++ Overlay (SpectrumStrip)"),
        ("Hardver Bank 1/2", "4×152 px / bank", "Fix Q1-4 = M1-4, Q5-8 = M5-8", "C++ QSlots + QGroup fókusz"),
        ("Hardware Q-Link", "XL: 16 enc / Key: 4", "XL: keret nélkül, Key61: sárga Q", "C++ QGroup (Gen1/Gen2 policy)"),
        ("Teljes Lap", "1280 × 628 px", "Lapváltás, modal védelem", "Host + Overlay kompozit"),
    ]

    for i, row in enumerate(rows):
        t_y += 26
        bg_col = (14, 22, 52) if i % 2 == 0 else (11, 16, 37)
        draw.rectangle([(table_x, t_y), (table_x + table_w, t_y + 26)], fill=bg_col, outline=(25, 34, 75), width=1)
        cur_x = table_x + 10
        for item, (header, width) in zip(row, cols):
            col_text = (255, 255, 255) if header == "Vezérlő elem" else (200, 215, 235)
            draw.text((cur_x, t_y + 6), item, fill=col_text, font=f_small)
            cur_x += width

    # Explanatory Notes Box below table
    t_y += 36
    draw.rectangle([(table_x, t_y), (table_x + table_w, t_y + 115)], fill=(16, 24, 58), outline=(39, 48, 106), width=1)
    notes = [
        "1. Nem levágott grafika: a jobb margó szigorúan garantált (28 px belső margó, 802 px táblaszélesség).",
        "2. Hardware Q-Link szabályok: MPC XL-en mind a 16 enkóder aktív, Key 61-en sárga keret és számozatlan 'Q'.",
        "3. Egyetlen érték elv: a host skin rendereli az értéket, az overlay tiszta apertúrát hagy, nincs szövegütközés.",
        "4. Tesztelési státusz: a fixture-ök tesztelési célúak, a linkek repo-relatívak (nem worktree URL-ek).",
    ]
    for j, note in enumerate(notes):
        draw.text((table_x + 12, t_y + 10 + j * 24), note, fill=(160, 180, 215), font=f_small)

    require_external_output(out_path.parent)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)
    print(f"Generated comparison graphic: {out_path} ({W}x{H})")

if __name__ == "__main__":
    out_file = Path(tempfile.mkdtemp(prefix="synthyboy-macros-comparison-")) / "comparison.png"
    render_comparison(out_file)
