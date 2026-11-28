# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The text-free Pan/Level dial previews of the component catalogue: pair, sizes, recipe, captions, skin strips."""
import json
import subprocess
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.level.level import (
    DIAL_TEXT_STYLE, FRAMES, dial_state_images, dial_text_variants, dial_variants, render_dial_frame,
    render_dial_labelled, render_dial_preview,
)

HTML = ROOT / "docs/ui-components/kepes-katalogus.html"


def entry(name):
    return json.loads((ROOT / f"components/native/{name}/catalog.json").read_text(encoding="utf-8"))["dial"]


class DialCatalogTests(unittest.TestCase):
    def test_normal_and_compact_pair_at_real_size_from_the_dial_recipe(self):
        for name in ("pan", "level"):
            dial = entry(name)
            variants = dial_variants(dial)
            self.assertEqual([v["id"] for v in variants], ["normal", "compact"])
            gap = style.size(dial["state_gap"])
            for variant in variants:
                d = variant["diameter"]
                with Image.open(ROOT / f"components/native/{name}/img/{variant['file']}") as image:
                    image = image.convert("RGBA")
                self.assertEqual(image.size, (len(dial["states"]) * d + (len(dial["states"]) - 1) * gap, d))
                for i, state in enumerate(dial["states"]):
                    cell = image.crop((i * (d + gap), 0, i * (d + gap) + d, d))
                    expected = render_dial_frame(d, state, bipolar=dial["bipolar"])
                    self.assertEqual(cell.tobytes(), expected.tobytes(), f"{name} {variant['id']} state {state}")

    def test_one_text_free_picture_per_state_at_normal_size(self):
        for name in ("pan", "level"):
            dial = entry(name)
            images = dial_state_images(dial)
            self.assertEqual([i["id"] for i in images], ["min", "mid", "max"])
            for item in images:
                with Image.open(ROOT / f"components/native/{name}/img/{item['file']}") as image:
                    expected = render_dial_frame(style.size("dial.diameter"), item["state"], bipolar=dial["bipolar"])
                    self.assertEqual(image.convert("RGBA").tobytes(), expected.tobytes())

    def test_labelled_layouts_share_one_text_style_and_the_dial_recipe(self):
        self.assertEqual(DIAL_TEXT_STYLE, {"font": "font.value", "colour": "text.value"})
        for name in ("pan", "level"):
            dial = entry(name)
            text, variants = dial_text_variants(dial)
            self.assertEqual([v["layout"] for v in variants], ["right", "bottom", "top"])
            d = text["diameter_px"]
            for variant in variants:
                expected = render_dial_labelled(d, text["state"], text["caption"], text["value"], variant["layout"],
                                                bipolar=dial["bipolar"], gap=style.size(dial["state_gap"]))
                with Image.open(ROOT / f"components/native/{name}/img/{variant['file']}") as image:
                    self.assertEqual(image.convert("RGBA").tobytes(), expected.tobytes(), variant["file"])
                self.assertGreaterEqual(expected.width, d)
            # the dial itself is the recipe's frame: the bottom layout keeps it at the top, uncropped
            bottom = render_dial_labelled(d, text["state"], text["caption"], text["value"], "bottom",
                                          bipolar=dial["bipolar"], gap=2)
            dial_frame = render_dial_frame(d, text["state"], bipolar=dial["bipolar"])
            x = (bottom.width - d) // 2
            self.assertEqual(bottom.crop((x, 0, x + d, d)).tobytes(), dial_frame.tobytes())
        with self.assertRaises(ValueError):
            render_dial_labelled(90, 0.5, "PAN", "L 100", "left")

    def test_picture_is_rendered_text_free_by_parameter(self):
        picture = render_dial_preview(40, [0.5], bipolar=False)
        self.assertEqual(picture.tobytes(), render_dial_frame(40, 0.5).tobytes())
        for kwargs in ({"caption": True}, {"value_text": "50%"}):
            with self.assertRaises(ValueError):
                render_dial_preview(40, [0.5], **kwargs)

    def test_html_captions_the_sizes_outside_the_image(self):
        content = HTML.read_text(encoding="utf-8")
        for name in ("pan", "level"):
            for kind, token, ident in (("Normál", "dial.diameter", "normal"), ("Kompakt", "dial.compact.diameter", "compact")):
                d = style.size(token)
                self.assertIn(f"<figcaption>{kind} · {d} × {d} px</figcaption>", content)
                self.assertIn(f"{name}-dial-{ident}.png?v=", content)
        for name in ("pan", "level"):
            for ident in ("min", "mid", "max"):
                self.assertIn(f"{name}-dial-{ident}.png?v=", content)
            for ident in ("right", "bottom", "top"):
                self.assertIn(f"{name}-dial-text-{ident}.png?v=", content)
        self.assertNotIn("pan-90.png", content)
        self.assertNotIn("level-native-40.png", content)

    def test_variant_entries_are_validated(self):
        dial = entry("pan")
        with self.assertRaises(ValueError):
            dial_variants(dict(dial, variants=dial["variants"] + [dict(dial["variants"][0])]))
        with self.assertRaises(ValueError):
            dial_variants(dict(dial, variants=[dict(dial["variants"][0], diameter="nonexistent.token")]))

    def test_skin_filmstrips_still_hold_all_frames(self):
        for strip in ("pan/img/play_sub_pan_40.png", "pan/img/play_sub_pan_90.png",
                      "level/img/play_sub_level_40.png", "level/img/play_sub_level_90.png"):
            with Image.open(ROOT / "components/native" / strip) as image:
                self.assertEqual(image.height, image.width * FRAMES, strip)


if __name__ == "__main__":
    unittest.main()
