# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""RadioList catalogue entry: row heights, LED placement, one lit LED, disabled, label clear of the LED, pictures, order."""
import json
import re
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.radio_list import radio_list

NATIVE = ROOT / "components/native"
HTML = ROOT / "docs/ui-components/kepes-katalogus.html"
SAMPLES = radio_list.radio_samples(json.loads((NATIVE / "radio_list/catalog.json").read_text(encoding="utf-8"))["radio_list"])
BY_ID = {s["id"]: s for s in SAMPLES}


def stored(file):
    with Image.open(NATIVE / "radio_list/img" / file) as image:
        return image.convert("RGBA")


def near(actual, expected, delta=40):
    return all(abs(a - b) <= delta for a, b in zip(actual[:3], expected))


def led_centres(sample):
    height = style.size(sample["height"])
    layout = radio_list.row_layout(height, sample["width"])
    return [(ox + int(layout["led_cx"]), oy + int(layout["led_cy"])) for ox, oy in radio_list.row_origins(sample)]


class RadioListTests(unittest.TestCase):
    def test_images_equal_renderer_output_and_row_heights(self):
        self.assertEqual({style.size(s["height"]) for s in SAMPLES}, {32, 40, 48})
        for sample in SAMPLES:
            image = stored(sample["file"])
            self.assertEqual(image.tobytes(), radio_list.render_sample(sample).convert("RGBA").tobytes(), sample["id"])
            height, gap, count = style.size(sample["height"]), style.size("radio_list")["gap"], len(sample["items"])
            if sample["orientation"] == "vertical":
                self.assertEqual(image.size, (sample["width"], count * height + (count - 1) * gap), sample["id"])
            else:
                self.assertEqual(image.size, (count * sample["width"] + (count - 1) * gap, height), sample["id"])

    def test_led_is_forty_percent_and_centred_in_the_row(self):
        for height in (32, 40, 48):
            layout = radio_list.row_layout(height, 120)
            self.assertAlmostEqual(layout["diameter"] / height, 0.4, delta=0.02)
            self.assertEqual(layout["led_cy"], height / 2)
        sample = BY_ID["six-48"]
        image = stored(sample["file"])
        layout = radio_list.row_layout(48, sample["width"])
        ox, oy = radio_list.row_origins(sample)[1]
        dark = [(x, y) for y in range(48) for x in range(int(layout["led_x"]) - 1, int(layout["led_x"] + layout["diameter"]) + 2)
                if near(image.getpixel((ox + x, oy + y)), style.rgb("surface.control"), 6)]
        ys = [y for _, y in dark]
        self.assertAlmostEqual((min(ys) + max(ys)) / 2, 23.5, delta=1)

    def test_exactly_one_lit_led_in_every_enabled_list(self):
        cyan = style.rgb("accent.cyan")
        for sample in SAMPLES:
            if sample["state"] != "enabled":
                continue
            image = stored(sample["file"])
            lit = [i for i, c in enumerate(led_centres(sample)) if near(image.getpixel(c), cyan, 12)]
            self.assertEqual(lit, [sample["selected"]], sample["id"])

    def test_disabled_differs_and_has_no_cyan(self):
        enabled = radio_list.render_sample(dict(BY_ID["disabled-40"], state="enabled"))
        disabled = stored("radio-list-disabled-40.png")
        self.assertNotEqual(enabled.tobytes(), disabled.tobytes())
        cyan = style.rgb("accent.cyan")
        self.assertFalse(near(disabled.getpixel(led_centres(BY_ID["disabled-40"])[0]), cyan, 40))

    def test_label_does_not_overlap_the_led_and_fits_the_row(self):
        for sample in SAMPLES:
            height = style.size(sample["height"])
            layout = radio_list.row_layout(height, sample["width"])
            self.assertGreaterEqual(layout["label_x"] - (layout["led_x"] + layout["diameter"]), style.size("radio_list")["led_gap"])
            for item in sample["items"]:
                from components.native.label.label import render_label
                label = render_label(item, layout["font"], "text.value", unit="em")
                self.assertLessEqual(layout["label_x"] + label.width, sample["width"] - style.size("radio_list")["led_gap"] // 2, (sample["id"], item))
                self.assertLessEqual(label.height, height)

    def test_selected_row_is_highlighted_and_invalid_samples_refused(self):
        sample = BY_ID["six-40"]
        selected = radio_list.render_row("TRI", True, 40, 120)
        plain = radio_list.render_row("TRI", False, 40, 120)
        self.assertNotEqual(selected.getpixel((100, 20)), plain.getpixel((100, 20)))
        with self.assertRaises(ValueError):
            radio_list.radio_samples({"samples": [dict(sample, selected=9)]})
        with self.assertRaises(ValueError):
            radio_list.radio_samples({"samples": [dict(sample, height="height.high")]})

    def test_radio_family_order_and_old_radiobutton_marked_for_removal(self):
        html = HTML.read_text(encoding="utf-8")
        titles = re.findall(r"<h3>([^<]*)</h3>", html)
        first = titles.index("RadioList")
        self.assertEqual(titles[first:first + 6], ["RadioList", "RadioGrid", "SlideSwitch", "SteppedDial", "RotarySelector", "RadioButton (KIVEZETÉSRE)"])
        self.assertLess(titles.index("FilterSelector") if "FilterSelector" in titles else first - 1, first)
        self.assertIn("components/native/radio_list/img/", html)


if __name__ == "__main__":
    unittest.main()
