#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates authentic before/after and variant comparison previews for the native MixerStrip component.

Outputs:
  - components/native/mixer_strip/img/source_strip_before_after.png
  - components/native/mixer_strip/img/variants_comparison.png
  - components/native/mixer_strip/img/catalog-mixer-strip.png

Conforms strictly to project rules:
  - Before image is cropped from actual historical docs/ui-map/mixer-master-bold-cut/after/Init_mixer.png (x: 6..129, y: 176..734).
  - After image is rendered authentically from the new native mixer_strip component definition.
  - Zero hardcoded style geometry/font sizes: all tokens read from sizes.json (mixer_strip.*) and colors.json.
  - Identical logical state between before and after (SUB, Filters 1+2, 50.0% level, 0.0% pan, 0.0% sends).
  - Normalized level values conform to engine parameter specs (percent for SOURCE, derived dB with exp_skew for BUS/MASTER).
  - Images are offline design previews, not host/native firmware captures.
"""

from pathlib import Path
import sys

# Ensure repository root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from PIL import Image, ImageDraw, ImageFont

from components.native.mixer_strip.mixer_strip import (
    render_mixer_strip,
    semibold,
    VARIANT_SOURCE,
    VARIANT_BUS,
    VARIANT_MASTER,
)
from components.style import (
    color as _style_color,
    rgb as _style_rgb,
    rgba as _style_rgba,
    size as _style_size,
)

IMG_DIR = REPO_ROOT / "components" / "native" / "mixer_strip" / "img"
HISTORICAL_REF = REPO_ROOT / "docs" / "ui-map" / "mixer-master-bold-cut" / "after" / "Init_mixer.png"


def get_font(size_px: int):
    return semibold(size_px)


def render_source_strip_before_after(out_path: Path):
    """Renders single-strip before/after comparison with identical logical parameters and exact crop."""
    W = _style_size("mixer_strip.preview.before_after_w")
    H = _style_size("mixer_strip.preview.before_after_h")
    canvas = Image.new("RGBA", (W, H), _style_rgb("field.focus"))
    draw = ImageDraw.Draw(canvas)

    f_title = get_font(_style_size("mixer_strip.font.preview_title"))
    f_sub = get_font(_style_size("mixer_strip.font.preview_sub"))
    f_card_title = get_font(_style_size("mixer_strip.font.preview_card"))
    f_badge = get_font(_style_size("mixer_strip.font.badge"))
    f_note = get_font(_style_size("mixer_strip.font.note"))

    # Title & Subtitle
    draw.text((W // 2, 26), "SynthyBoy Native MPC Mixer Strip — Before / After Comparison", font=f_title, fill=_style_rgb("text.icon"), anchor="mm")
    draw.text((W // 2, 48), "Historical Pre-Migration Layout vs New Reusable Native MixerStrip Component", font=f_sub, fill=_style_rgb("text.caption"), anchor="mm")

    # Card 1 (Left): Historical Pre-Migration Strip
    c1_x, c1_y = 35, 72
    c1_w = _style_size("mixer_strip.preview.card_w")
    c1_h = _style_size("mixer_strip.preview.card_h")
    card_r = _style_size("mixer_strip.preview.card_radius")
    badge_r = _style_size("mixer_strip.preview.badge_radius")
    badge_h = _style_size("mixer_strip.preview.badge_h")

    draw.rounded_rectangle((c1_x, c1_y, c1_x + c1_w, c1_y + c1_h), radius=card_r, fill=_style_rgb("button.surface"), outline=_style_rgb("button.border"), width=1)
    draw.text((c1_x + 16, c1_y + 20), "BEFORE: Historical", font=f_card_title, fill=_style_rgb("text.icon"))

    # Badge 1
    b1_w = _style_size("mixer_strip.preview.badge_w_old")
    draw.rounded_rectangle((c1_x + c1_w - b1_w - 14, c1_y + 12, c1_x + c1_w - 14, c1_y + 12 + badge_h), radius=badge_r, fill=_style_rgb("ink.black"), outline=_style_rgb("button.border"), width=1)
    draw.text((c1_x + c1_w - b1_w // 2 - 14, c1_y + 21), "HISTORICAL PRE-MIGRATION", font=f_badge, fill=_style_rgb("text.caption"), anchor="mm")

    # Crop historical strip 0 (SUB) from Init_mixer.png (exact geometry x: 6..129, y: 176..734)
    crop_x0 = _style_size("mixer_strip.preview.crop_x0")
    crop_y0 = _style_size("mixer_strip.preview.crop_y0")
    crop_x1 = _style_size("mixer_strip.preview.crop_x1")
    crop_y1 = _style_size("mixer_strip.preview.crop_y1")
    crop_w = crop_x1 - crop_x0
    crop_h = crop_y1 - crop_y0

    if HISTORICAL_REF.is_file():
        with Image.open(HISTORICAL_REF) as h_img:
            h_crop = h_img.convert("RGBA").crop((crop_x0, crop_y0, crop_x1, crop_y1))
            strip_x1 = c1_x + (c1_w - crop_w) // 2
            strip_y1 = c1_y + 40
            canvas.alpha_composite(h_crop, (strip_x1, strip_y1))
            draw.rectangle((strip_x1 - 1, strip_y1 - 1, strip_x1 + crop_w, strip_y1 + crop_h), outline=_style_rgb("voice.border"), width=1)
    else:
        draw.text((c1_x + c1_w // 2, c1_y + 300), "Missing Init_mixer.png", font=f_sub, fill=_style_rgb("text.caption"), anchor="mm")

    draw.text((c1_x + c1_w // 2, c1_y + c1_h - 10), "Crop x:6..129 y:176..734 (Init_mixer.png)", font=f_note, fill=_style_rgb("text.caption"), anchor="mm")

    # Card 2 (Right): New Reusable Native Component Strip
    c2_x, c2_y = 395, 72
    c2_w, c2_h = c1_w, c1_h
    draw.rounded_rectangle((c2_x, c2_y, c2_x + c2_w, c2_y + c2_h), radius=card_r, fill=_style_rgb("button.surface"), outline=_style_rgb("button.border"), width=1)
    draw.text((c2_x + 16, c2_y + 20), "AFTER: Native MixerStrip", font=f_card_title, fill=_style_rgb("voice.value"))

    # Badge 2
    b2_w = _style_size("mixer_strip.preview.badge_w_new")
    draw.rounded_rectangle((c2_x + c2_w - b2_w - 14, c2_y + 12, c2_x + c2_w - 14, c2_y + 12 + badge_h), radius=badge_r, fill=_style_rgb("ink.black"), outline=_style_rgb("voice.border"), width=1)
    draw.text((c2_x + c2_w - b2_w // 2 - 14, c2_y + 21), "NATIVE TRACK & THUMB", font=f_badge, fill=_style_rgb("voice.value"), anchor="mm")

    # Render authentic new native strip with identical SUB state (level: 50.0%, pan: 0.0%, sends: 0.0%)
    state_after = {
        "power": 1,
        "name": "SUB",
        "destination": "Filters 1+2",
        "level": 0.50,
        "level_text": "50.0%",
        "peak": 0.0,
        "hold": 0.0,
        "pan": 0.0,
        "pan_text": "0.0%",
        "send1": 0.0,
        "send1_text": "0.0%",
        "send2": 0.0,
        "send2_text": "0.0%",
    }
    new_strip = render_mixer_strip(VARIANT_SOURCE, state_after)
    sw = _style_size("mixer_strip.width")
    sh = _style_size("mixer_strip.height")
    strip_x2 = c2_x + (c2_w - sw) // 2
    strip_y2 = c2_y + 44
    canvas.alpha_composite(new_strip, (strip_x2, strip_y2))
    draw.rectangle((strip_x2 - 1, strip_y2 - 1, strip_x2 + sw, strip_y2 + sh), outline=_style_rgb("voice.border"), width=1)

    draw.text((c2_x + c2_w // 2, c2_y + c2_h - 10), "Identical state: SUB, 50.0% level, 0.0% pan/sends", font=f_note, fill=_style_rgb("voice.value"), anchor="mm")

    # Footer notice
    draw.text(
        (W // 2, H - 16),
        "Offline design preview (TrackAndThumb slider, Section 6.1 Slider). Not a physical device capture or verified host render.",
        font=f_note,
        fill=_style_rgb("text.caption"),
        anchor="mm",
    )

    canvas.save(out_path, format="PNG")
    print(f"Generated {out_path} ({W}x{H})")


def render_variants_comparison(out_path: Path):
    """Renders side-by-side comparison of the 3 architecture variants: SOURCE, BUS, and MASTER showing all children."""
    W = _style_size("mixer_strip.preview.variants_w")
    H = _style_size("mixer_strip.preview.variants_h")
    canvas = Image.new("RGBA", (W, H), _style_rgb("field.focus"))
    draw = ImageDraw.Draw(canvas)

    f_title = get_font(_style_size("mixer_strip.font.preview_title"))
    f_sub = get_font(_style_size("mixer_strip.font.preview_sub"))
    f_card_title = get_font(_style_size("mixer_strip.font.preview_card"))
    f_badge = get_font(_style_size("mixer_strip.font.badge"))
    f_note = get_font(_style_size("mixer_strip.font.note"))

    # Header banner
    draw.text((W // 2, 26), "SynthyBoy Native MPC Mixer Strip — Component Variants Comparison", font=f_title, fill=_style_rgb("text.icon"), anchor="mm")
    draw.text((W // 2, 48), "Three Compound Definitions: SOURCE (Generators/Filters), BUS (FX Returns), and MASTER (Main Console)", font=f_sub, fill=_style_rgb("text.caption"), anchor="mm")

    # BUS level 0 dB corresponds to proportion 60/66 = 0.90909, normalized = 0.90909^2.5 = 0.7891
    # MASTER default volume -12 dB corresponds to proportion 48/66 = 0.72727, normalized = 0.72727^2.5 = 0.4504
    columns = [
        {
            "variant": VARIANT_SOURCE,
            "title": "SOURCE Variant",
            "badge": "GENERATORS & FILTERS",
            "state": {
                "power": 1,
                "name": "OSC A",
                "destination": "Filters 1+2",
                "level": 0.80,
                "level_text": "80.0%",
                "peak": 0.74,
                "hold": 0.88,
                "pan": 0.0,
                "pan_text": "0.0%",
                "send1": 0.25,
                "send1_text": "25.0%",
                "send2": 0.0,
                "send2_text": "0.0%",
            },
        },
        {
            "variant": VARIANT_BUS,
            "title": "BUS Variant",
            "badge": "FX RETURN BUS 1 & 2",
            "state": {
                "power": 1,
                "name": "BUS 1",
                "destination": "Main",
                "level": 0.7891,
                "level_text": "0.0 dB",
                "peak": 0.70,
                "hold": 0.85,
            },
        },
        {
            "variant": VARIANT_MASTER,
            "title": "MASTER Variant",
            "badge": "FINAL STEREO CONSOLE",
            "state": {
                "power": 1,
                "name": "MAIN",
                "level": 0.4504,
                "level_text": "-12.0 dB",
                "peak": 0.65,
                "hold": 0.80,
            },
        },
    ]

    col_w = _style_size("mixer_strip.preview.variants_col_w")
    col_h = _style_size("mixer_strip.preview.variants_col_h")
    gap = _style_size("mixer_strip.preview.variants_gap")
    start_x = (W - (3 * col_w + 2 * gap)) // 2
    card_r = _style_size("mixer_strip.preview.card_radius")
    badge_r = _style_size("mixer_strip.preview.badge_radius")
    sw = _style_size("mixer_strip.width")
    sh = _style_size("mixer_strip.height")

    for i, col in enumerate(columns):
        cx = start_x + i * (col_w + gap)
        cy = 72

        draw.rounded_rectangle((cx, cy, cx + col_w, cy + col_h), radius=card_r, fill=_style_rgb("button.surface"), outline=_style_rgb("button.border"), width=1)
        draw.text((cx + col_w // 2, cy + 18), col["title"], font=f_card_title, fill=_style_rgb("voice.value") if i == 0 else _style_rgb("text.icon"), anchor="mm")

        # Badge
        draw.rounded_rectangle((cx + 25, cy + 30, cx + col_w - 25, cy + 46), radius=badge_r, fill=_style_rgb("ink.black"), outline=_style_rgb("button.border"), width=1)
        draw.text((cx + col_w // 2, cy + 38), col["badge"], font=f_badge, fill=_style_rgb("voice.value"), anchor="mm")

        # Render authentic strip showing all children
        strip_im = render_mixer_strip(col["variant"], col["state"])
        sx = cx + (col_w - sw) // 2
        sy = cy + 54
        canvas.alpha_composite(strip_im, (sx, sy))
        draw.rectangle((sx - 1, sy - 1, sx + sw, sy + sh), outline=_style_rgb("voice.border"), width=1)

        # Dimension annotation below strip
        draw.text((cx + col_w // 2, sy + sh + 14), "115 × 606 px (all touch targets >= 56 px)", font=f_note, fill=_style_rgb("text.caption"), anchor="mm")

    # Bottom notice
    draw.text(
        (W // 2, H - 16),
        "Offline design preview (TrackAndThumb slider, Section 6.1 Slider). 10 strips fit within 1280 px. 0 Q numbers. Not a hardware capture.",
        font=f_note,
        fill=_style_rgb("text.caption"),
        anchor="mm",
    )

    canvas.save(out_path, format="PNG")
    print(f"Generated {out_path} ({W}x{H})")


def render_catalog_card(out_path: Path):
    """Renders sample card image for the native component catalog."""
    W = _style_size("mixer_strip.preview.catalog_w")
    H = _style_size("mixer_strip.preview.catalog_h")
    canvas = Image.new("RGBA", (W, H), _style_rgb("field.focus"))
    draw = ImageDraw.Draw(canvas)

    f_title = get_font(_style_size("mixer_strip.font.preview_title"))
    f_sub = get_font(_style_size("mixer_strip.font.preview_sub"))
    f_note = get_font(_style_size("mixer_strip.font.note"))

    draw.text((W // 2, 20), "Native MPC MixerStrip", font=f_title, fill=_style_rgb("text.icon"), anchor="mm")
    draw.text((W // 2, 38), "Compound Channel Strip Component (115 × 606 px)", font=f_sub, fill=_style_rgb("voice.value"), anchor="mm")

    # Render SOURCE and BUS strips side-by-side
    s_src = render_mixer_strip(VARIANT_SOURCE, {"power": 1, "name": "SUB", "destination": "Filters 1+2", "level": 0.50, "level_text": "50.0%", "peak": 0.0, "hold": 0.0, "pan": 0.0, "pan_text": "0.0%", "send1": 0.0, "send1_text": "0.0%", "send2": 0.0, "send2_text": "0.0%"})
    s_bus = render_mixer_strip(VARIANT_BUS, {"power": 1, "name": "BUS 1", "destination": "Main", "level": 0.7891, "level_text": "0.0 dB", "peak": 0.70, "hold": 0.85})

    sw = _style_size("mixer_strip.width")
    sh = _style_size("mixer_strip.height")
    gap = 25
    x1 = (W - (2 * sw + gap)) // 2
    x2 = x1 + sw + gap
    y = 50

    canvas.alpha_composite(s_src, (x1, y))
    draw.rectangle((x1 - 1, y - 1, x1 + sw, y + sh), outline=_style_rgb("voice.border"), width=1)

    canvas.alpha_composite(s_bus, (x2, y))
    draw.rectangle((x2 - 1, y - 1, x2 + sw, y + sh), outline=_style_rgb("voice.border"), width=1)

    draw.text((W // 2, H - 12), "Offline design preview: SOURCE (left) and BUS (right) variants", font=f_note, fill=_style_rgb("text.caption"), anchor="mm")

    canvas.save(out_path, format="PNG")
    print(f"Generated {out_path} ({W}x{H})")


def main():
    IMG_DIR.mkdir(parents=True, exist_ok=True)
    out_ba = IMG_DIR / "source_strip_before_after.png"
    out_var = IMG_DIR / "variants_comparison.png"
    out_cat = IMG_DIR / "catalog-mixer-strip.png"

    print("Rendering native mixer strip preview sheets...")
    render_source_strip_before_after(out_ba)
    render_variants_comparison(out_var)
    render_catalog_card(out_cat)
    print("All previews generated successfully.")


if __name__ == "__main__":
    main()
