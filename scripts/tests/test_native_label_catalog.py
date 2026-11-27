# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Label, ON/OFF label and the Selector dropdown layouts of the component catalogue, and the catalogue order."""
import json
import re
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.label import label
from components.native.level.level import dial_text_variants
from components.native.on_off_label import on_off_label
from components.native.selector import selector

NATIVE = ROOT / "components/native"
HTML = ROOT / "docs/ui-components/kepes-katalogus.html"


def entry(component, key):
    return json.loads((NATIVE / component / "catalog.json").read_text(encoding="utf-8"))[key]


def same(path, image):
    with Image.open(path) as stored:
        return stored.convert("RGBA").tobytes() == image.convert("RGBA").tobytes() and stored.size == image.size


class LabelTests(unittest.TestCase):
    def test_every_variant_is_the_recipe_at_its_token_size(self):
        variants = label.label_variants(entry("label", "label"))
        self.assertEqual([v["id"] for v in variants],
                         ["screen-title", "block-title", "block-title-tune", "dial-caption", "dial-value", "dial-value-pan"])
        for v in variants:
            image = label.render_label(v["text"], v["font"], v["colour"], unit=v["unit"], case=v["case"])
            self.assertTrue(same(NATIVE / "label/img" / v["file"], image), v["id"])
        self.assertEqual(variants[0]["size"], style.size("font.page_title"))

    def test_dial_size_table_keeps_the_tokens(self):
        sizes = label.dial_sizes(entry("label", "label"))
        self.assertEqual([s["token"] for s in sizes], [f"label.dial.{d}" for d in (36, 40, 48, 56, 90)])
        self.assertEqual([s["diameter"] for s in sizes], [36, 40, 48, 56, 90])
        self.assertEqual([s["proposal"] for s in sizes], [True, False, True, True, False])
        for s in sizes:
            self.assertLess(s["caption"], s["value"])
            self.assertFalse((NATIVE / "label/img" / f"label-dial-{s['id']}.png").exists())

    def test_dial_examples_three_layouts_enabled_and_disabled(self):
        examples = label.dial_examples(entry("label", "label"))
        self.assertEqual([(e["layout"], e["state"]) for e in examples],
                         [(lay, st) for lay in ("bottom", "top", "right") for st in ("enabled", "disabled")])
        images = {}
        for e in examples:
            image = label.render_dial_example(e)
            images[e["id"]] = image
            self.assertTrue(same(NATIVE / "label/img" / e["file"], image), e["id"])
            self.assertGreaterEqual(min(image.size), 48)
        for lay in ("lent", "fent-lent", "jobbra"):
            self.assertNotEqual(images[f"{lay}-enabled"].tobytes(), images[f"{lay}-disabled"].tobytes())
            self.assertEqual(images[f"{lay}-enabled"].size, images[f"{lay}-disabled"].size)

    def test_disabled_option_is_off_by_default_and_pan_level_images_unchanged(self):
        from components.native.level.level import render_dial_labelled
        for name in ("pan", "level"):
            dial = entry(name, "dial")
            text, variants = dial_text_variants(dial)
            for v in variants:
                image = render_dial_labelled(text["diameter_px"], text["state"], text["caption"], text["value"], v["layout"],
                                             bipolar=dial["bipolar"], gap=style.size(dial["state_gap"]))
                self.assertTrue(same(NATIVE / name / "img" / v["file"], image), v["file"])

    def test_invalid_label_input_is_refused(self):
        with self.assertRaises(ValueError):
            label.label_variants({"variants": [{"id": "x"}]})
        with self.assertRaises(ValueError):
            label.render_label("A", "font.small", "text.value", unit="px")


class OnOffLabelTests(unittest.TestCase):
    def test_examples_are_rendered_by_the_recipe(self):
        items = on_off_label.examples(entry("on_off_label", "on_off"))
        self.assertEqual(len(items), 10)
        for item in items:
            self.assertTrue(same(NATIVE / "on_off_label/img" / item["file"], on_off_label.render_example(item)), item["id"])

    def test_states_differ_and_layouts_place_the_dial(self):
        on, off, disabled = (on_off_label.label_face("SMOOTH", s).tobytes() for s in on_off_label.STATES)
        self.assertEqual(len({on, off, disabled}), 3)
        diameter = style.size("dial.diameter")
        right = on_off_label.render_example({"state": "on", "dial": "level", "layout": "right"})
        bottom = on_off_label.render_example({"state": "on", "dial": "pan", "layout": "bottom"})
        self.assertEqual((right.height, bottom.width), (diameter, diameter))
        self.assertGreater(right.width, diameter)
        self.assertGreater(bottom.height, diameter)

    def test_invalid_input_is_refused(self):
        with self.assertRaises(ValueError):
            on_off_label.label_face("X", "half")
        with self.assertRaises(ValueError):
            on_off_label.examples({"examples": [{"id": "a", "file": "a", "alt": "a", "caption": "a", "state": "on", "dial": "pan"}]})


class DropdownTests(unittest.TestCase):
    def test_layouts_same_recipe_value_centred_at_three_heights(self):
        variants = selector.dropdown_variants(entry("selector", "dropdown"))
        self.assertEqual([(v["placement"], v["align"], v.get("height", "height.medium")) for v in variants],
                         [("none", "center", "height.small"), ("none", "center", "height.intermediate"),
                          ("none", "center", "height.medium"), ("none", "center", "height.intermediate"),
                          ("above", "center", "height.intermediate"), ("above", "center", "height.intermediate"),
                          ("inside", "left", "height.medium")])
        self.assertTrue(variants[-1]["caption"].startswith("KIVEZETÉSRE"))   # the small label inside the box is being phased out
        width = style.size("header.dropdown.min_width")
        for v in variants:
            box = style.size(v.get("height", "height.medium"))
            image = selector.render_dropdown_variant(width, box, v["value"], label=v.get("label"),
                                                     placement=v["placement"], align=v["align"])
            self.assertTrue(same(NATIVE / "selector/img" / v["file"], image), v["id"])
            self.assertEqual(image.width, width)
            self.assertGreaterEqual(image.height, box)
            if v["placement"] != "above":
                self.assertEqual(image.height, box)

    def test_size_tokens_follow_the_box_height(self):
        for height in (32, 40, 48):
            token = style.size(f"selector.dropdown.size.{height}")
            self.assertEqual(token["height"], height)
            self.assertAlmostEqual(token["font_em"] / height, 0.5, delta=0.07)   # value em about half the box
            self.assertLess(token["chevron_width"], token["pad_x"] * 2 + 8)
        ems = [style.size(f"selector.dropdown.size.{h}")["font_em"] for h in (32, 40, 48)]
        self.assertEqual(ems, [18, 22, 24])
        self.assertGreater(ems[0], style.native_font_em(style.size("selector.dropdown.preview_font")))

    def test_value_text_is_larger_and_clear_of_the_box_edges(self):
        for height in (32, 40, 48):
            image = selector.render_dropdown_variant(220, height, "Env 1")
            ink = [(x, y) for y in range(height) for x in range(20, 180) if image.getpixel((x, y))[3] and
                   sum(image.getpixel((x, y))[:3]) > 600]
            ys = [y for _, y in ink]
            self.assertTrue(ink)
            self.assertGreaterEqual(min(ys), 4)
            self.assertLessEqual(max(ys), height - 4)
            self.assertAlmostEqual((min(ys) + max(ys)) / 2, height / 2, delta=2.5)

    def test_long_value_is_cut_left_aligned_and_keeps_chevron_gap(self):
        gap, width = style.size("selector.dropdown.chevron_gap"), style.size("header.dropdown.min_width")
        token = style.size("selector.dropdown.size.40")
        self.assertEqual(gap, 4)                                       # the minimum space between text and arrow
        chevron_left = width - style.size("selector.dropdown")["chevron_lane"] / 2 - token["chevron_width"] / 2
        for value, short in (("Env 1", True), ("Filter 1 Cutoff Modulation Amount", False)):
            image = selector.render_dropdown_variant(width, 40, value)
            ink = [x for y in range(40) for x in range(2, width - 2) if sum(image.getpixel((x, y))[:3]) > 650 and image.getpixel((x, y))[3] > 200]
            self.assertLessEqual(max(ink) + gap, chevron_left, value)    # never closer to the arrow than chevron_gap
            if short:
                self.assertAlmostEqual((min(ink) + max(ink)) / 2, (token["pad_x"] + width - 28 - gap) / 2, delta=3)  # centred
            else:
                self.assertLessEqual(min(ink), token["pad_x"] + 2)       # left-aligned
                self.assertGreater(max(ink), width - 28 - gap - 40)      # runs to the end of the available room (cut with an ellipsis)

    def test_title_above_is_left_aligned_with_the_box(self):
        image = selector.render_dropdown_variant(220, 40, "Percent", label="LFO MODE", placement="above")
        top = [x for y in range(0, 10) for x in range(220) if image.getpixel((x, y))[3] > 120]
        self.assertLessEqual(min(top), 2)

    def test_header_dropdowns_are_the_value_only_centred_40_px_variant(self):
        plain = selector.render_dropdown_variant(220, 40, "Env 1")
        from components.native.header import header
        face = header.centre_face({"kind": "dropdown", "value": "Env 1", "width": "header.dropdown.min_width"}, 48)
        self.assertEqual(face.tobytes(), plain.tobytes())
        self.assertEqual(face.height, 40)

    def test_legacy_fill_has_the_voice_selector_contrast_and_neon_is_untouched(self):
        face = selector.render_dropdown_face(220, 48, look="legacy", size="selector.dropdown.size.48").convert("RGB")
        self.assertEqual(face.getpixel((20, 8)), (20, 26, 56))    # constant upper part: #141a38
        self.assertEqual(face.getpixel((20, 38)), (11, 15, 36))   # constant lower part: #0b0f24
        mid = face.getpixel((20, 26))                              # darkening between the two constant parts
        self.assertTrue(all(lo <= v <= hi for v, lo, hi in zip(mid, (11, 15, 36), (20, 26, 56))) and mid not in ((20, 26, 56), (11, 15, 36)))
        self.assertEqual(face.getpixel((20, 0)), (38, 46, 103))   # the thin bright keyline on top, #27306a
        neon = selector.render_dropdown_face(220, 48)
        self.assertEqual(neon.convert("RGB").getpixel((20, 10)), (20, 26, 56))  # theme.neon.panel0 as before
        self.assertEqual(style.color("selector.dropdown.top"), "#141a38")
        self.assertEqual(style.color("selector.dropdown.bottom"), "#0b0f24")
        self.assertEqual(style.size("selector.dropdown.gradient.legacy"), style.size("selector.dropdown.gradient.neon"))

    def test_invalid_combinations_are_refused(self):
        with self.assertRaises(ValueError):
            selector.render_dropdown_variant(220, 48, "x", placement="above")
        with self.assertRaises(ValueError):
            selector.render_dropdown_variant(220, 48, "x", label="L", placement="none")
        with self.assertRaises(ValueError):
            selector.render_dropdown_variant(220, 48, "x", placement="side")


class OrderTests(unittest.TestCase):
    def test_catalogue_order_comes_from_the_order_list(self):
        titles = re.findall(r"<h3>([^<]*)</h3>", HTML.read_text(encoding="utf-8"))
        order = json.loads((NATIVE / "catalog-order.json").read_text(encoding="utf-8"))
        self.assertEqual(titles[:6], ["Label", "On/Off label", "Pan", "Level", "Button", "IconButton"])
        self.assertEqual(titles[-1], "Header")
        self.assertEqual(order["last"], ["header"])


if __name__ == "__main__":
    unittest.main()
