# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Waveform icons: inside the icon box, pairwise different, same stroke as the sine, sheets equal the renderer."""
import json
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.selector.selector import WAVE_SHAPES, render_wave_icon
from components.native.wave_icons import wave_icons

NATIVE = ROOT / "components/native"
SHEETS = wave_icons.sheet_samples(json.loads((NATIVE / "wave_icons/catalog.json").read_text(encoding="utf-8"))["wave_icons"])


def stored(file):
    with Image.open(NATIVE / "wave_icons/img" / file) as image:
        return image.convert("RGBA")


class WaveIconTests(unittest.TestCase):
    def test_all_shapes_exist_and_stay_inside_the_icon_box(self):
        self.assertEqual(set(WAVE_SHAPES), {"sine", "saw", "square", "pulse", "triangle", "sh", "noise"})
        width, height = wave_icons.icon_box()
        for shape in WAVE_SHAPES:
            icon = wave_icons.render_icon(shape)
            self.assertEqual(icon.size, (width, height), shape)
            left, top, right, bottom = icon.getchannel("A").point(lambda a: 255 if a > 32 else 0).getbbox()
            self.assertTrue(left >= 1 and top >= 0 and right <= width - 1 and bottom <= height, (shape, (left, top, right, bottom)))

    def test_icons_differ_pairwise_and_sine_is_the_original(self):
        images = [wave_icons.render_icon(shape).tobytes() for shape in WAVE_SHAPES]
        self.assertEqual(len(set(images)), len(WAVE_SHAPES))
        self.assertEqual(render_wave_icon().tobytes(), render_wave_icon(shape="sine").tobytes())

    def test_stroke_matches_the_sine(self):
        """The ink in a column of a flat stretch (sine peak, square plateau, pulse plateau, sh step) is the stroke width."""
        width, height = wave_icons.icon_box()
        stroke = style.size("stroke.icon")

        def column(shape, x):
            icon = wave_icons.render_icon(shape)
            return sum(icon.getpixel((x, y))[3] for y in range(height)) / 255

        self.assertAlmostEqual(column("sine", width // 4), stroke, delta=0.8)       # the sine's peak (a quarter along)
        self.assertAlmostEqual(column("square", width // 4), stroke, delta=0.4)
        self.assertAlmostEqual(column("pulse", 6), stroke, delta=0.4)
        self.assertAlmostEqual(column("sh", 5), stroke, delta=0.8)

    def test_sheets_equal_renderer_output_and_zoom_is_exact(self):
        self.assertEqual({s["zoom"] for s in SHEETS}, {1, 4})
        for sheet in SHEETS:
            image = stored(sheet["file"])
            self.assertEqual(image.size, wave_icons.sheet_size(sheet), sheet["id"])
            self.assertEqual(image.tobytes(), wave_icons.render_sheet(sheet).convert("RGBA").tobytes(), sheet["id"])
        one = stored("wave-icons-1x.png")
        four = stored("wave-icons-4x.png")
        self.assertEqual(four.resize(one.size, Image.Resampling.NEAREST).tobytes(), one.tobytes())

    def test_selected_icon_is_cyan_the_others_blue_grey(self):
        sheet = next(s for s in SHEETS if s["zoom"] == 1)
        image = stored(sheet["file"])
        width, height = wave_icons.icon_box()
        cyan, wave = style.rgb("accent.cyan"), style.rgb("selector.wave")
        for index, left in enumerate(wave_icons.sheet_origins()):
            ink = [p for p in image.crop((left, 0, left + width, height)).getdata() if p[3] > 200]
            target = cyan if index == sheet["selected"] else wave
            self.assertTrue(all(abs(a - b) <= 40 for a, b in zip(ink[0][:3], target)), index)


if __name__ == "__main__":
    unittest.main()
