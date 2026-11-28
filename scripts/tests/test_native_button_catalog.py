# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Button and IconButton catalogue entries: shaded look (measured colours), sizes, ghost, disabled, order."""
import json
import re
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.button import button
from components.native.icon_button import icon_button

NATIVE = ROOT / "components/native"
HTML = ROOT / "docs/ui-components/kepes-katalogus.html"
BUTTONS = button.button_samples(json.loads((NATIVE / "button/catalog.json").read_text(encoding="utf-8"))["button"])
ICONS = icon_button.icon_samples(json.loads((NATIVE / "icon_button/catalog.json").read_text(encoding="utf-8"))["icon_button"])


def stored(component, file):
    with Image.open(NATIVE / component / "img" / file) as image:
        return image.convert("RGBA")


def near(actual, expected, delta=2):
    return all(abs(a - b) <= delta for a, b in zip(actual[:3], expected))


class ButtonTests(unittest.TestCase):
    def test_one_sample_per_size_and_one_disabled_at_48(self):
        self.assertEqual([(s["height"], s["state"]) for s in BUTTONS],
                         [("height.small", "enabled"), ("height.intermediate", "enabled"), ("height.medium", "enabled"),
                          ("height.high", "enabled"), ("height.medium", "disabled")])
        for sample in BUTTONS:
            image = stored("button", sample["file"])
            self.assertEqual(image.height, style.size(sample["height"]), sample["id"])
            self.assertEqual(image.tobytes(), button.render_sample(sample).tobytes(), sample["id"])
        self.assertNotEqual(stored("button", "button-48.png").tobytes(), stored("button", "button-48-disabled.png").tobytes())

    def test_measured_old_colours(self):
        """Owner's '+ ADD' crop: keyline on top, fill (19,25,48) down to a third, then darkening to (13,17,34)."""
        top_line = style.rgb("button.gradient.border")
        for height in (32, 40, 48, 56):
            face = button.action_art(text="+ ADD", height=height, look="shaded")
            x = 8                              # in the padding, left of the text
            self.assertTrue(near(face.getpixel((x, 0)), top_line, 6), height)
            self.assertTrue(near(face.getpixel((x, 3)), (19, 25, 48)), height)
            self.assertTrue(near(face.getpixel((x, height * 3 // 4)), (13, 17, 34), 3), height)
            mid = face.getpixel((x, height // 2))
            self.assertTrue(all(lo <= v <= hi for v, lo, hi in zip(mid, (13, 17, 34), (19, 25, 48))), height)
            self.assertLessEqual(face.getpixel((0, 0))[3], 8)                   # rounded corner is (almost) transparent
        self.assertEqual(style.color("button.gradient.top"), "#131930")
        self.assertEqual(style.color("button.gradient.bottom"), "#0d1122")

    def test_gradient_shape_is_shared_with_the_dropdown(self):
        for look in ("neon", "shaded"):
            self.assertEqual(style.size(f"selector.dropdown.gradient.{look}"), style.size("control.gradient"))
        from components.native.selector import selector
        self.assertIs(selector.gradient_face, button.gradient_face)

    def test_default_look_follows_control_look_and_flat_stays_available(self):
        self.assertEqual(style.control_look(), "shaded")
        face = button.action_art(icon="power").convert("RGB")
        self.assertEqual(face.size, (48, 48))
        self.assertEqual(face, button.action_art(icon="power", look="shaded").convert("RGB"))
        flat = button.action_art(icon="power", look="flat").convert("RGB")
        self.assertEqual(flat.getpixel((6, 24)), style.rgb("button.surface"))
        self.assertNotEqual(face.getpixel((6, 24)), flat.getpixel((6, 24)))

    def test_text_fits_and_look_is_validated(self):
        for height in (32, 40, 48, 56):
            face = button.action_art(text="+ ADD", height=height, look="shaded")
            self.assertEqual(face.height, height)
            self.assertGreater(face.width, height)
        with self.assertRaises(ValueError):
            button.action_art(text="X", look="shiny")
        with self.assertRaises(ValueError):
            button.action_art(text="X", icon="power")


class IconButtonTests(unittest.TestCase):
    def test_samples_are_square_at_32_40_48_56(self):
        sizes = {style.size(s["height"]) for s in ICONS}
        self.assertEqual(sizes, {32, 40, 48, 56})
        for sample in ICONS:
            image = stored("icon_button", sample["file"])
            self.assertEqual(image.size, (style.size(sample["height"]),) * 2, sample["id"])
            self.assertEqual(image.tobytes(), icon_button.render_sample(sample).tobytes(), sample["id"])

    def test_states_listed_in_the_catalogue_data(self):
        states = {(s["icon"], s["state"], s["look"]) for s in ICONS}
        for wanted in (("power", "on", "filled"), ("power", "off", "filled"), ("power", "disabled", "filled"),
                       ("power", "on", "ghost"), ("power", "off", "ghost")):
            self.assertIn(wanted, states)
        self.assertEqual({style.size(s["height"]) for s in ICONS if s["look"] == "ghost"}, {40, 48})
        for wanted in (("close", "off", "filled"), ("close", "disabled", "filled"), ("close", "off", "ghost")):
            self.assertIn(wanted, states)

    def test_close_is_an_x_at_40_percent_of_the_height(self):
        art = style.size("button.art")
        self.assertAlmostEqual(2 * art["close"]["half"] / art["height"], 0.4, places=2)
        face = button.action_art(icon="close", look="ghost", height=48)
        ink = [(x, y) for y in range(48) for x in range(48) if face.getpixel((x, y))[3] > 128]
        xs, ys = [x for x, _ in ink], [y for _, y in ink]
        self.assertAlmostEqual((max(xs) - min(xs)) / 48, 0.4 + art["close"]["stroke"] / 48, delta=0.06)
        self.assertAlmostEqual((min(xs) + max(xs)) / 2, 23.5, delta=1)
        self.assertAlmostEqual((min(ys) + max(ys)) / 2, 23.5, delta=1)
        self.assertEqual(face.getpixel((24, 24))[3] > 128, True)            # the diagonals cross in the middle
        self.assertEqual(face.getpixel((24, 10))[3], 0)                     # nothing above the X on the axis

    def test_ghost_has_no_fill_and_no_keyline(self):
        background = (12, 17, 31, 255)
        for sample in (s for s in ICONS if s["look"] == "ghost"):
            image = stored("icon_button", sample["file"])
            clear = sum(1 for p in image.getdata() if p[3] == 0)
            self.assertGreater(clear, 0.6 * image.width * image.height, sample["id"])   # only the icon has ink
            for point in ((0, 0), (image.width // 2, 0), (0, image.height // 2), (image.width - 1, image.height - 1)):
                self.assertEqual(image.getpixel(point)[3], 0, (sample["id"], point))   # no edge, no corner
            flat = Image.new("RGBA", image.size, background)
            composed = flat.copy()
            composed.alpha_composite(image)
            same = sum(1 for a, b in zip(flat.getdata(), composed.getdata()) if a == b)
            self.assertGreaterEqual(same, clear)                                       # background shows through exactly
            filled = icon_button.render_sample(dict(sample, look="filled"))
            self.assertGreater(filled.getpixel((filled.width // 2, 0))[3], 0)          # the filled face has a keyline

    def test_on_is_green_off_is_plain_and_disabled_differs(self):
        by_id = {s["id"]: s for s in ICONS}
        on = icon_button.render_sample(by_id["power-on-filled-48"])
        off = icon_button.render_sample(by_id["power-off-filled-48"])
        dim = icon_button.render_sample(by_id["power-disabled-filled-48"])
        self.assertEqual(len({on.tobytes(), off.tobytes(), dim.tobytes()}), 3)
        brightest = lambda im: max(im.getdata(), key=lambda p: p[3] and p[1] - p[0])
        self.assertTrue(near(brightest(on), style.rgb("accent.green"), 40))

    def test_invalid_samples_are_refused(self):
        with self.assertRaises(ValueError):
            icon_button.icon_samples({"samples": [dict(ICONS[0], look="glass")]})
        with self.assertRaises(ValueError):
            icon_button.icon_samples({"samples": [dict(ICONS[0], icon="more")]})    # no MORE icon recipe


class OrderTests(unittest.TestCase):
    def test_button_pages_in_order_and_flat_examples_are_gone(self):
        titles = re.findall(r"<h3>([^<]*)</h3>", HTML.read_text(encoding="utf-8"))
        self.assertEqual(titles[titles.index("Button") + 1], "IconButton")
        html = HTML.read_text(encoding="utf-8")
        self.assertNotIn("catalog-button-48", html)
        self.assertNotIn("filter-catalog-active", html)
        for component in ("button", "icon_button"):
            self.assertIn(f"components/native/{component}/img/", html)


if __name__ == "__main__":
    unittest.main()
