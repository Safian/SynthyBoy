# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SteppedDial: N positions = N frames, pointer on the stop angle, no value arc, Pan/Level pictures unchanged."""
import json
import math
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.level.level import dial_geometry, render_dial_frame
from components.native.stepped_dial import stepped_dial

NATIVE = ROOT / "components/native"
SAMPLES = stepped_dial.stepped_samples(json.loads((NATIVE / "stepped_dial/catalog.json").read_text(encoding="utf-8"))["stepped_dial"])
BY_ID = {s["id"]: s for s in SAMPLES}
NEON = style.theme_colors("neon")


def rgb(hex_colour):
    return tuple(int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))


def stored(file):
    with Image.open(NATIVE / "stepped_dial/img" / file) as image:
        return image.convert("RGBA")


def ring_pixel(diameter, degrees):
    radius, _, _ = dial_geometry(diameter)
    a = math.radians(degrees)
    return int(diameter / 2 + radius * math.cos(a)), int(diameter / 2 + radius * math.sin(a))


class SteppedDialTests(unittest.TestCase):
    def test_images_equal_renderer_output_and_cover_the_four_diameters(self):
        self.assertEqual({s["diameter"] for s in SAMPLES}, {36, 40, 48, 56})
        self.assertEqual({s["stops"] for s in SAMPLES if s["state"] == "enabled"}, {3, 4, 6, 8})
        for sample in SAMPLES:
            image = stored(sample["file"])
            self.assertEqual(image.size, (sample["diameter"],) * 2, sample["id"])
            self.assertEqual(image.tobytes(), stepped_dial.render_sample(sample).convert("RGBA").tobytes(), sample["id"])

    def test_n_positions_are_n_different_frames(self):
        for count in range(2, 9):
            frames = {stepped_dial.render_frame(48, i, count).tobytes() for i in range(count)}
            self.assertEqual(len(frames), count)

    def test_thumb_is_on_the_stop_angle(self):
        text = rgb(NEON["text"])
        for sample in (s for s in SAMPLES if s["state"] == "enabled"):
            x, y = ring_pixel(sample["diameter"], stepped_dial.stop_angle(sample["position"], sample["stops"]))
            pixel = stored(sample["file"]).getpixel((x, y))
            self.assertTrue(all(abs(p - t) <= 25 for p, t in zip(pixel[:3], text)), (sample["id"], pixel))

    def test_no_value_arc_the_ring_between_stops_is_the_track_colour_everywhere(self):
        track, flow = rgb(NEON["track"]), [rgb(c) for c in NEON["flow"]]
        for sample in (s for s in SAMPLES if s["state"] == "enabled" and s["diameter"] >= 40):
            image, count = stored(sample["file"]), sample["stops"]
            for index in range(count - 1):
                mid = (stepped_dial.stop_angle(index, count) + stepped_dial.stop_angle(index + 1, count)) / 2
                pixel = image.getpixel(ring_pixel(sample["diameter"], mid))
                self.assertTrue(all(abs(p - t) <= 6 for p, t in zip(pixel[:3], track)), (sample["id"], index, pixel))
                for colour in flow:
                    self.assertFalse(all(abs(p - c) <= 20 for p, c in zip(pixel[:3], colour)), (sample["id"], index))

    def test_stops_are_marked_and_the_selected_one_is_lighter(self):
        sample = BY_ID["six-4-48"]
        image = stored(sample["file"])
        dim = image.getpixel(ring_pixel(48, stepped_dial.stop_angle(0, 6)))
        track = image.getpixel(ring_pixel(48, (stepped_dial.stop_angle(0, 6) + stepped_dial.stop_angle(1, 6)) / 2))
        self.assertGreater(sum(dim[:3]), sum(track[:3]) + 60)
        chosen = image.getpixel(ring_pixel(48, stepped_dial.stop_angle(3, 6)))
        self.assertGreater(sum(chosen[:3]), sum(dim[:3]))

    def test_pan_and_level_pictures_are_byte_identical_by_default(self):
        explicit = render_dial_frame(48, 0.4, value_arc=True, stops=0)
        self.assertEqual(explicit.tobytes(), render_dial_frame(48, 0.4).tobytes())
        self.assertNotEqual(render_dial_frame(48, 0.4, value_arc=False).tobytes(), explicit.tobytes())

    def test_disabled_differs_and_invalid_samples_refused(self):
        enabled = stepped_dial.render_sample(dict(BY_ID["disabled-48"], state="enabled"))
        self.assertNotEqual(enabled.tobytes(), stored("stepped-dial-disabled-48.png").tobytes())
        base = BY_ID["four-2-48"]
        for bad in (dict(base, diameter=32), dict(base, stops=9), dict(base, stops=1), dict(base, position=4), dict(base, state="x")):
            with self.assertRaises(ValueError):
                stepped_dial.stepped_samples({"samples": [bad]})


if __name__ == "__main__":
    unittest.main()
