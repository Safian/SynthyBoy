# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""RotarySelector catalogue entry: pointer on evenly spaced angles, N positions = N frames, pictograms clear, disabled."""
import json
import math
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.level.level import dial_geometry
from components.native.rotary_selector import rotary_selector

NATIVE = ROOT / "components/native"
SAMPLES = rotary_selector.rotary_samples(json.loads((NATIVE / "rotary_selector/catalog.json").read_text(encoding="utf-8"))["rotary_selector"])
BY_ID = {s["id"]: s for s in SAMPLES}


def stored(file):
    with Image.open(NATIVE / "rotary_selector/img" / file) as image:
        return image.convert("RGBA")


class RotarySelectorTests(unittest.TestCase):
    def test_images_equal_renderer_output(self):
        for sample in SAMPLES:
            image = stored(sample["file"])
            self.assertEqual(image.tobytes(), rotary_selector.render_sample(sample).convert("RGBA").tobytes(), sample["id"])
            self.assertEqual(image.size, rotary_selector.sample_size(sample), sample["id"])

    def test_pointer_angles_are_even_over_the_270_degree_sweep(self):
        for count in range(3, 7):
            angles = [rotary_selector.stop_angle(i, count) for i in range(count)]
            self.assertEqual(angles[0], style.size("knob.start.degrees"))
            self.assertAlmostEqual(angles[-1] - angles[0], style.size("knob.sweep.degrees"))
            self.assertEqual(len({round(b - a, 6) for a, b in zip(angles, angles[1:])}), 1)

    def test_thumb_sits_on_the_stop_angle_in_the_picture(self):
        text = tuple(int(style.theme_colors("neon")["text"][i:i + 2], 16) for i in (1, 3, 5))
        for sample in (s for s in SAMPLES if s["state"] == "enabled"):
            geo = rotary_selector.layout(sample)
            radius, _, _ = dial_geometry(geo["diameter"])
            a = math.radians(rotary_selector.stop_angle(sample["position"], len(sample["icons"])))
            x = round(geo["centre"][0] - geo["diameter"] / 2) + geo["diameter"] / 2 + radius * math.cos(a)
            y = round(geo["centre"][1] - geo["diameter"] / 2) + geo["diameter"] / 2 + radius * math.sin(a)
            pixel = stored(sample["file"]).getpixel((int(x), int(y)))
            self.assertTrue(all(abs(p - t) <= 25 for p, t in zip(pixel[:3], text)), (sample["id"], pixel))

    def test_n_positions_are_n_different_frames(self):
        for count in (3, 4, 6):
            sample = dict(BY_ID["four-2"], icons=["saw"] * count)
            frames = {rotary_selector.render_sample(dict(sample, position=i)).tobytes() for i in range(count)}
            self.assertEqual(len(frames), count)

    def test_pictograms_do_not_cover_the_dial_or_each_other_or_leave_the_picture(self):
        for sample in SAMPLES:
            geo = rotary_selector.layout(sample)
            cx, cy = geo["centre"]
            boxes, width_height = geo["icon_boxes"], geo["size"]
            for left, top, right, bottom in boxes:
                nearest = math.hypot(min(max(cx, left), right) - cx, min(max(cy, top), bottom) - cy)
                self.assertGreaterEqual(nearest, geo["diameter"] / 2, sample["id"])
                self.assertGreaterEqual(left, 0, sample["id"])
                self.assertLessEqual(right, width_height[0], sample["id"])
                self.assertLessEqual(bottom, width_height[1], sample["id"])
                self.assertGreaterEqual(top, geo["title_h"], sample["id"])
            for i, a in enumerate(boxes):
                for b in boxes[i + 1:]:
                    self.assertTrue(a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1], sample["id"])

    def test_disabled_differs_and_invalid_samples_refused(self):
        enabled = rotary_selector.render_sample(dict(BY_ID["disabled"], state="enabled"))
        self.assertNotEqual(enabled.tobytes(), stored("rotary-selector-disabled.png").tobytes())
        base = BY_ID["four-2"]
        for bad in (dict(base, icons=["saw"] * 2), dict(base, icons=["saw"] * 7), dict(base, icons=["blob"] * 4),
                    dict(base, position=4), dict(base, diameter="height.small")):
            with self.assertRaises(ValueError):
                rotary_selector.rotary_samples({"samples": [bad]})


if __name__ == "__main__":
    unittest.main()
