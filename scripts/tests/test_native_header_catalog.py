# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The HEADER catalogue entry: geometry of every example, zones, tokens and the generated HTML."""
import json
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.header import header

DIRECTORY = ROOT / "components/native/header"
HTML = ROOT / "docs/ui-components/kepes-katalogus.html"
EXAMPLES = header.header_examples(json.loads((DIRECTORY / "catalog.json").read_text(encoding="utf-8"))["header"])


class HeaderCatalogTests(unittest.TestCase):
    def test_tokens(self):
        self.assertEqual((style.size("header.height"), style.size("header.padding"), style.size("header.gap")), (56, 4, 8))
        m = header.metrics()
        self.assertEqual(m["content"], style.size("height.medium"))
        self.assertEqual(style.size("header.page.width"), style.size("screen.width"))

    def test_eight_examples_with_one_picture_each(self):
        self.assertEqual([e["id"] for e in EXAMPLES],
                         ["osc-a", "lfo-9", "env-1", "filter-1", "master", "play-panel", "osc-a-off", "lfo-9-full"])
        for example in EXAMPLES:
            with Image.open(DIRECTORY / "img" / example["file"]) as image:
                self.assertEqual(image.size, (header.header_width(example["width"]), 56), example["id"])
                self.assertEqual(image.convert("RGBA").tobytes(), header.render_header(example).tobytes(), example["id"])

    def test_content_is_48_high_with_4_px_padding_dropdowns_40_centred(self):
        m = header.metrics()
        for example in EXAMPLES:
            _, placed = header.layout(example)
            for item in placed:
                x, y, w, h = item.rect
                if item.kind == "dropdown":   # 40 px (the proposal), centred in the 48 px content: 8 px margin
                    self.assertEqual((y, h), (m["padding"] + (m["content"] - 40) // 2, 40), example["id"])
                else:
                    self.assertEqual((y, h), (m["padding"], m["content"]), f"{example['id']} {item.kind}")
                self.assertEqual(y + h + (y - m["padding"]) + m["padding"], m["height"])

    def test_zones_are_ordered_do_not_overlap_and_fit_the_room(self):
        m = header.metrics()
        for example in EXAMPLES:
            width, placed = header.layout(example)
            rects = sorted(p.rect for p in placed)
            self.assertGreaterEqual(rects[0][0], m["side"], example["id"])
            self.assertLessEqual(max(x + w for x, _, w, _ in rects), width - m["side"], example["id"])
            for a, b in zip(rects, rects[1:]):
                self.assertGreaterEqual(b[0] - (a[0] + a[2]), m["gap"], f"{example['id']}: components closer than the gap")
            zones = {z: [p for p in placed if p.zone == z] for z in header.ZONES}
            order = [z for z in header.ZONES if zones[z]]
            spans = [(min(p.x for p in zones[z]), max(p.x + p.face.width for p in zones[z])) for z in order]
            for a, b in zip(spans, spans[1:]):
                self.assertLess(a[1], b[0], f"{example['id']}: zones overlap")
            for p in placed:
                self.assertLessEqual(p.face.width, width - 2 * m["side"], f"{example['id']} {p.kind}")

    def test_modulators_have_no_power_slot_and_power_is_48_square(self):
        m = header.metrics()
        for example in EXAMPLES:
            _, placed = header.layout(example)
            kinds = [p.kind for p in placed if p.zone == "left"]
            if example["id"] in ("lfo-9", "env-1", "master", "lfo-9-full"):
                self.assertNotIn("power", kinds)
                self.assertEqual(placed[0].x, m["side"])
            else:
                power = next(p for p in placed if p.kind == "power")
                self.assertEqual(power.face.size, (m["content"], m["content"]))
                self.assertEqual(power.x, m["side"])

    def test_right_zone_and_states(self):
        by_id = {e["id"]: header.layout(e)[1] for e in EXAMPLES}
        self.assertEqual([p.kind for p in by_id["lfo-9"] if p.zone == "right"], ["expand"])
        self.assertEqual([p.kind for p in by_id["lfo-9-full"] if p.zone == "right"], ["close"])
        self.assertEqual([p.kind for p in by_id["master"]], ["title"])
        on = {p.kind: p.face.tobytes() for p in by_id["osc-a"]}
        off = {p.kind: p.face.tobytes() for p in by_id["osc-a-off"]}
        self.assertNotEqual(on["power"], off["power"])
        self.assertNotEqual(on["dropdown"], off["dropdown"])
        self.assertEqual(on["title"], off["title"])
        self.assertEqual(on["more"], off["more"])

    def test_invalid_examples_are_refused(self):
        with self.assertRaises(ValueError):
            header.header_examples({"examples": [dict(EXAMPLES[0], width="huge")]})
        with self.assertRaises(ValueError):
            header.header_examples({"examples": [dict(EXAMPLES[0], right=["nope"])]})

    def test_html_lists_every_example_with_caption_outside_the_image(self):
        html = HTML.read_text(encoding="utf-8")
        self.assertIn('<article class="wide">', html)
        for example in EXAMPLES:
            self.assertIn(f"components/native/header/img/{example['file']}", html)
        self.assertIn("400 px", html)


if __name__ == "__main__":
    unittest.main()
