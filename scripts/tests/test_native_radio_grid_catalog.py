# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""RadioGrid catalogue entry: one selected, equal button size, the grid fills the width, selected differs, disabled."""
import json
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.radio_grid import radio_grid

NATIVE = ROOT / "components/native"
SAMPLES = radio_grid.grid_samples(json.loads((NATIVE / "radio_grid/catalog.json").read_text(encoding="utf-8"))["radio_grid"])
BY_ID = {s["id"]: s for s in SAMPLES}


def stored(file):
    with Image.open(NATIVE / "radio_grid/img" / file) as image:
        return image.convert("RGBA")


def cell(image, sample, index):
    geo = radio_grid.layout(sample)
    x, y = geo["origins"][index]
    return image.crop((x, y, x + geo["button"][0], y + geo["button"][1]))


def is_accent(pixel):
    return all(abs(a - b) <= 30 for a, b in zip(pixel[:3], style.rgb("accent.cyan")))


class RadioGridTests(unittest.TestCase):
    def test_images_equal_renderer_output(self):
        for sample in SAMPLES:
            image = stored(sample["file"])
            self.assertEqual(image.tobytes(), radio_grid.render_sample(sample).convert("RGBA").tobytes(), sample["id"])
            self.assertEqual(image.size, radio_grid.sample_size(sample), sample["id"])

    def test_buttons_equal_in_size_and_the_grid_fills_the_width(self):
        gap = style.size("header.gap")
        self.assertEqual(style.size("radio_grid")["gap"], gap)
        for sample in SAMPLES:
            geo = radio_grid.layout(sample)
            width, height = geo["button"]
            self.assertEqual(height, style.size(sample["height"]), sample["id"])
            self.assertEqual(sample["cols"] * width + (sample["cols"] - 1) * gap, sample["width"], sample["id"])
            self.assertEqual(geo["size"][0], sample["width"])
            right = max(x for x, _ in geo["origins"]) + width
            self.assertEqual(right, sample["width"], sample["id"])
            self.assertEqual(len(geo["origins"]), sample["rows"] * sample["cols"])
            self.assertEqual(geo["size"][1], geo["title_h"] + sample["rows"] * height + (sample["rows"] - 1) * gap)

    def test_exactly_one_selected_and_it_differs_from_the_others(self):
        for sample in (s for s in SAMPLES if s["state"] == "enabled"):
            image = stored(sample["file"])
            accent = [i for i in range(len(sample["items"]))
                      if is_accent(cell(image, sample, i).getpixel((cell(image, sample, i).width // 2, 4)))]
            self.assertEqual(accent, [sample["selected"]], sample["id"])
            cells = [cell(image, sample, i).tobytes() for i in range(len(sample["items"]))]
            self.assertEqual(cells.count(cells[sample["selected"]]), 1, sample["id"])

    def test_unselected_buttons_are_the_button_shaded_face(self):
        from components.native.button.button import action_art
        sample = BY_ID["row-1x4"]
        width, height = radio_grid.layout(sample)["button"]
        face = action_art(text="HZ", width=width, height=height, look="shaded")
        self.assertEqual(cell(stored(sample["file"]), sample, 0).tobytes(), face.tobytes())

    def test_disabled_differs_and_has_no_accent(self):
        sample = BY_ID["disabled"]
        enabled = radio_grid.render_sample(dict(sample, state="enabled"))
        disabled = stored(sample["file"])
        self.assertNotEqual(enabled.tobytes(), disabled.tobytes())
        self.assertFalse(is_accent(cell(disabled, sample, sample["selected"]).getpixel((cell(disabled, sample, 0).width // 2, 4))))

    def test_title_is_left_aligned_above_the_grid_and_invalid_samples_refused(self):
        sample = BY_ID["type-2x3"]
        geo = radio_grid.layout(sample)
        self.assertGreater(geo["title_h"], 0)
        ink = stored(sample["file"]).crop((0, 0, sample["width"], geo["title_h"])).getchannel("A").getbbox()
        self.assertLessEqual(ink[0], 1)
        base = BY_ID["row-1x4"]
        for bad in (dict(base, width=311), dict(base, rows=2), dict(base, selected=4), dict(base, height="height.high"),
                    dict(base, items=["HZ"] * 9, cols=9)):
            with self.assertRaises(ValueError):
                radio_grid.grid_samples({"samples": [bad]})


if __name__ == "__main__":
    unittest.main()
