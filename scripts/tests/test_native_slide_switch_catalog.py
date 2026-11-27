# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SlideSwitch catalogue entry: thumb on the position, one picture per position, disabled, labels clear of the track."""
import json
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.slide_switch import slide_switch

NATIVE = ROOT / "components/native"
SAMPLES = slide_switch.slide_samples(json.loads((NATIVE / "slide_switch/catalog.json").read_text(encoding="utf-8"))["slide_switch"])
BY_ID = {s["id"]: s for s in SAMPLES}


def stored(file):
    with Image.open(NATIVE / "slide_switch/img" / file) as image:
        return image.convert("RGBA")


def cyan_centre(image):
    """The centroid of the accent.cyan pixels: the thumb's centre line."""
    target = style.rgb("accent.cyan")
    points = [(x, y) for y in range(image.height) for x in range(image.width)
              if all(abs(a - b) <= 25 for a, b in zip(image.getpixel((x, y))[:3], target)) and image.getpixel((x, y))[3] > 200]
    return sum(x for x, _ in points) / len(points), sum(y for _, y in points) / len(points)


class SlideSwitchTests(unittest.TestCase):
    def test_images_equal_renderer_output(self):
        for sample in SAMPLES:
            self.assertEqual(stored(sample["file"]).tobytes(), slide_switch.render_sample(sample).convert("RGBA").tobytes(), sample["id"])
            self.assertEqual(stored(sample["file"]).size, slide_switch.sample_size(sample), sample["id"])

    def test_thumb_centre_is_on_the_position(self):
        for sample in SAMPLES:
            if sample["state"] != "enabled":
                continue
            geo = slide_switch.layout(sample)
            cx, cy = geo["centres"][sample["position"]]
            found = cyan_centre(stored(sample["file"]))
            self.assertAlmostEqual(found[0], cx, delta=1.0, msg=sample["id"])
            self.assertAlmostEqual(found[1], cy, delta=1.0, msg=sample["id"])

    def test_positions_are_even_and_stay_in_the_track(self):
        for sample in SAMPLES:
            geo = slide_switch.layout(sample)
            axis = 0 if sample["orientation"] == "horizontal" else 1
            values = [c[axis] for c in geo["centres"]]
            steps = {round(b - a, 3) for a, b in zip(values, values[1:])}
            self.assertEqual(len(steps), 1, sample["id"])
            left, top, right, bottom = geo["track"]
            self.assertAlmostEqual(values[0] - geo["diameter"] / 2, (left, top)[axis] + style.size("slide_switch")["inset"], delta=0.01)
            self.assertAlmostEqual(values[-1] + geo["diameter"] / 2, (right, bottom)[axis] - style.size("slide_switch")["inset"], delta=0.01)

    def test_n_positions_are_n_different_pictures(self):
        for group, count in (("lmh", 3), ("pwm", 3)):
            ids = [s for s in SAMPLES if s["id"].startswith(group) and s["state"] == "enabled"]
            self.assertEqual(len(ids), count)
            self.assertEqual(len({stored(s["file"]).tobytes() for s in ids}), count)
        self.assertNotEqual(stored("slide-switch-mono.png").tobytes(), stored("slide-switch-poly.png").tobytes())
        self.assertEqual({len(s["labels"]) for s in SAMPLES}, {2, 3, 4})

    def test_disabled_differs_from_enabled(self):
        for disabled, state in (("disabled-h", "enabled"), ("disabled-v", "enabled")):
            enabled = slide_switch.render_sample(dict(BY_ID[disabled], state=state))
            self.assertNotEqual(enabled.tobytes(), stored(BY_ID[disabled]["file"]).tobytes())

    def test_labels_inside_the_picture_and_clear_of_the_track(self):
        for sample in SAMPLES:
            geo = slide_switch.layout(sample)
            left, top, right, bottom = geo["track"]
            width, height = geo["size"]
            for label, (x, y) in zip(geo["labels"], geo["label_boxes"]):
                self.assertGreaterEqual(x, 0, sample["id"])
                self.assertGreaterEqual(y, 0, sample["id"])
                self.assertLessEqual(x + label.width, width, sample["id"])
                self.assertLessEqual(y + label.height, height, sample["id"])
                if sample["orientation"] == "vertical":
                    self.assertGreaterEqual(x, right + style.size("slide_switch")["label_gap"], sample["id"])
                else:
                    self.assertLessEqual(y + label.height, top, sample["id"])

    def test_invalid_samples_refused(self):
        base = BY_ID["lmh-l"]
        for bad in (dict(base, labels=["A"]), dict(base, labels=list("ABCDE")), dict(base, position=3),
                    dict(base, size="height.high"), dict(base, orientation="diagonal")):
            with self.assertRaises(ValueError):
                slide_switch.slide_samples({"samples": [bad]})


if __name__ == "__main__":
    unittest.main()
