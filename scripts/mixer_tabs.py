# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared Mixer bottom 6-tab navigation bar geometry and rendering.

Single-sources the 6-tab bottom navigation layout and artwork between the
production MPC TUI filmstrip generation (84_mixer.py) and the offline
preview renderer (render_mixer_preview.py).
"""
from pathlib import Path
from PIL import Image, ImageDraw

from components.native.mixer_strip.mixer_strip import semibold
from components.style import color as _style_color, rgb as _style_rgb, size as _style_size

MIXER_TABS = 6
MIXER_TAB_LABELS = ("MIXER", "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")


def mixer_tab_rect(index: int) -> tuple[int, int, int, int]:
    """Returns the (x, y, width, height) bounding box for tab `index` (0..5)."""
    start_x = _style_size("mixer_strip.tab.start_x")
    start_y = _style_size("mixer_strip.start_y")
    strip_h = _style_size("mixer_strip.height")
    gap_y = _style_size("mixer_strip.tab.gap_y")
    gap_x = _style_size("mixer_strip.tab.gap_x")
    bw = _style_size("mixer_strip.tab.width")
    bh = _style_size("mixer_strip.tab.height")

    x = start_x + index * (bw + gap_x)
    y = start_y + strip_h + gap_y
    return (x, y, bw, bh)


def render_mixer_tab_face(caption: str, active: bool, width: int = None, height: int = None) -> Image.Image:
    """Renders a single tab button face image (width x height) in active or inactive state."""
    bw = width if width is not None else _style_size("mixer_strip.tab.width")
    bh = height if height is not None else _style_size("mixer_strip.tab.height")
    radius = _style_size("radius.field")
    font = semibold(_style_size("mixer_strip.font.sm"))

    img = Image.new("RGBA", (bw, bh), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    fill = _style_rgb("dial.value") if active else _style_rgb("button.surface")
    outline = _style_rgb("accent.cyan") if active else _style_rgb("button.border")
    draw.rounded_rectangle([0, 0, bw - 1, bh - 1], radius=radius, fill=fill, outline=outline, width=1)
    text_fill = _style_color("text.white") if active else _style_color("text.icon")
    draw.text((bw // 2, bh // 2), caption, fill=text_fill, font=font, anchor="mm")
    return img


def render_mixer_tab_bar(canvas: Image.Image, active_tab: int = 0) -> None:
    """Renders the persistent 6-tab bottom navigation bar directly onto `canvas`."""
    for k, caption in enumerate(MIXER_TAB_LABELS):
        rx, ry, rw, rh = mixer_tab_rect(k)
        face = render_mixer_tab_face(caption, active=(k == active_tab), width=rw, height=rh)
        canvas.alpha_composite(face, (rx, ry))


def render_mixer_tab_filmstrip(skins_dir: Path, caption: str, tab_index: int) -> str:
    """Generates a 6-frame vertical FilmStrip PNG highlighting tab `tab_index` when active."""
    bw = _style_size("mixer_strip.tab.width")
    bh = _style_size("mixer_strip.tab.height")
    sheet = Image.new("RGBA", (bw, bh * MIXER_TABS), (0, 0, 0, 0))
    for frame in range(MIXER_TABS):
        face = render_mixer_tab_face(caption, active=(frame == tab_index), width=bw, height=bh)
        sheet.alpha_composite(face, (0, frame * bh))
    slug = caption.lower().replace(" ", "_")
    filename = f"mixer_tab_{slug}_states.png"
    sheet.save(skins_dir / filename)
    return filename


def render_mixer_tab_hit(skins_dir: Path) -> str:
    """Generates a transparent hit-box PNG matching tab dimensions."""
    bw = _style_size("mixer_strip.tab.width")
    bh = _style_size("mixer_strip.tab.height")
    filename = "mixer_tab_hit.png"
    Image.new("RGBA", (bw, bh), (0, 0, 0, 0)).save(skins_dir / filename)
    return filename
