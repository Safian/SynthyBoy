#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The native dial and fader art follow the drawn ui::Knob / ui::Fader from the same tokens.

docs/review/NATIVE-COMPONENT-FIDELITY-2026-11-26.md
"""
import math
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from PIL import ImageFont

from components import style
from components.native.fader.fader import render_fader_cap
from components.native.level.level import FRAMES, dial_geometry, render_dial_frame


def rgb(hex_colour):
    return tuple(int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))


def close(a, b, tolerance=24):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


class DialFidelityTests(unittest.TestCase):
    palette = style.theme_colors("neon")

    def point(self, diameter, value, offset=0.0):
        radius, _, _ = dial_geometry(diameter)
        start = math.radians(style.size("knob.start.degrees"))
        angle = start + value * math.radians(style.size("knob.sweep.degrees"))
        c = diameter / 2
        return int(c + (radius + offset) * math.cos(angle)), int(c + (radius + offset) * math.sin(angle))

    def test_geometry_is_the_knob_geometry(self):
        metrics = style.size("theme.neon")
        for d in (40, 60, 90):
            radius, stroke, track = dial_geometry(d)
            self.assertAlmostEqual(stroke, max(style.size("knob.stroke.min"), d * metrics["arcValue"]))
            self.assertAlmostEqual(track, max(style.size("knob.track.min"), d * metrics["arcTrack"]))
            self.assertAlmostEqual(radius, d / 2 - stroke / 2 - style.size("knob.radius.inset"))

    def test_track_value_arc_and_thumb_colours(self):
        d = 90
        frame = render_dial_frame(d, 0.5).convert("RGBA")
        # unfilled part of the sweep: the theme track colour, opaque
        x, y = self.point(d, 0.85)
        pixel = frame.getpixel((x, y))
        self.assertEqual(pixel[3], 255)
        self.assertTrue(close(pixel[:3], rgb(self.palette["track"])), pixel)
        # the value arc near its start is the first flow stop, near its end the last
        self.assertTrue(close(frame.getpixel(self.point(d, 0.02))[:3], rgb(self.palette["flow"][0]), 40))
        self.assertTrue(close(frame.getpixel(self.point(d, 0.40))[:3], rgb(self.palette["flow"][3]), 60))
        # the thumb on the arc at the value is the text colour
        self.assertTrue(close(frame.getpixel(self.point(d, 0.5))[:3], rgb(self.palette["text"]), 8))

    def test_inactive_is_the_drawn_grey(self):
        frame = render_dial_frame(90, 0.5, active=False).convert("RGB")
        for value in (0.2, 0.5, 0.85):
            r, g, b = frame.getpixel(self.point(90, value))
            self.assertTrue(abs(r - g) <= 2 and abs(g - b) <= 2, (value, (r, g, b)))

    def test_every_frame_stays_inside_its_square(self):
        """Nothing of the drawn dial is cut by the frame: the thumb and the arc caps fit the square."""
        for d in (40, 60, 90):
            radius, stroke, track = dial_geometry(d)
            thumb = max(style.size("knob.thumb.min"), stroke * style.size("knob.thumb.ratio"))
            self.assertLessEqual(radius + max(thumb, stroke / 2, track / 2), d / 2, d)
            self.assertEqual(render_dial_frame(d, 0.5).size, (d, d))
        self.assertEqual(FRAMES, 127)

    def test_fader_cap_is_the_drawn_thumb(self):
        cap = render_fader_cap().convert("RGBA")
        w, h = cap.size
        self.assertEqual((w, h), (style.size("mixer_strip.fader.cap_width"), style.size("mixer_strip.fader.cap_height")))
        self.assertTrue(close(cap.getpixel((w // 2, h // 2))[:3], rgb(self.palette["text"]), 30))
        body = cap.getpixel((w // 2, h // 4))[:3]
        self.assertLess(sum(body), 3 * 60, body)   # a dark panel block, not the old light slab

    def test_host_font_height_per_em_matches_the_font(self):
        font = ImageFont.truetype(str(ROOT / "shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf"), 1000)
        ascent, descent = font.getmetrics()
        self.assertAlmostEqual(style.size("font.host.height.per.em"), (ascent + descent) / 1000, places=2)
        self.assertAlmostEqual(style.native_font_em(15), 15 / style.size("font.host.height.per.em"))

    def test_theme_header_is_generated_from_the_tokens(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/gen-ui-theme.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
