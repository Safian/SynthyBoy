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
from components.native.button import button
from components.native.header import header
from components.native.icon_button import icon_button

DIRECTORY = ROOT / "components/native/header"
HTML = ROOT / "docs/ui-components/kepes-katalogus.html"
EXAMPLES = header.header_examples(json.loads((DIRECTORY / "catalog.json").read_text(encoding="utf-8"))["header"])


class HeaderCatalogTests(unittest.TestCase):
    def test_tokens(self):
        self.assertEqual((style.size("header.height"), style.size("header.padding"), style.size("header.gap")), (56, 8, 8))
        m = header.metrics()
        self.assertEqual(m["content"], style.size("height.intermediate"))   # 56 - 2 x 8 = 40
        with self.assertRaises(ValueError):
            style.size("header.side")                                      # one margin token: header.padding
        self.assertEqual(style.size("header.page.width"), style.size("screen.width"))

    def test_eight_examples_with_one_picture_each(self):
        self.assertEqual([e["id"] for e in EXAMPLES],
                         ["osc-a", "lfo-9", "env-1", "filter-1", "master", "play-panel", "osc-a-off", "lfo-9-full"])
        for example in EXAMPLES:
            with Image.open(DIRECTORY / "img" / example["file"]) as image:
                self.assertEqual(image.size, (header.header_width(example["width"]), 56), example["id"])
                self.assertEqual(image.convert("RGBA").tobytes(), header.render_header(example).tobytes(), example["id"])

    def test_content_is_40_high_with_the_same_8_px_all_around(self):
        m = header.metrics()
        self.assertEqual((m["padding"], m["content"], m["side"]), (8, 40, 8))
        for example in EXAMPLES:
            width, placed = header.layout(example)
            for item in placed:
                x, y, w, h = item.rect
                if item.kind == "title":      # the Label screenTitle, centred, width from the text
                    ink = item.face.getbbox()      # the line box may be a pixel taller than the row; the letters fit
                    self.assertLessEqual(ink[3] - ink[1], m["content"])
                    self.assertEqual(y, m["padding"] + (m["content"] - h) // 2, example["id"])
                else:                         # every button and dropdown is the content height: 8 px above and below
                    self.assertEqual((y, h), (m["padding"], m["content"]), f"{example['id']} {item.kind}")
                    self.assertEqual(m["height"] - (y + h), m["padding"], f"{example['id']} {item.kind}")
            real = [p for p in placed if p.kind != "title"]
            if real:
                self.assertEqual(min(p.y for p in real), m["padding"])                       # top edge 8 px from the header edge
                self.assertEqual(m["height"] - max(p.y + p.face.height for p in real), m["padding"])   # bottom edge 8 px
            self.assertEqual(min(p.x for p in placed), m["padding"], example["id"])           # left edge 8 px
            if example.get("right") or any(i.get("fill") for i in example.get("centre", [])):
                self.assertEqual(width - max(p.x + p.face.width for p in placed), m["padding"], example["id"])   # right edge 8 px
            if example["width"] == "panel":
                self.assertEqual(width, 400)

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

    def test_title_first_at_the_left_edge_then_power_a_gap_after_it(self):
        m = header.metrics()
        from components.native.label import label
        for example in EXAMPLES:
            _, placed = header.layout(example)
            left = [p for p in placed if p.zone == "left"]
            self.assertEqual(left[0].x, m["side"], example["id"])           # title (or the Play panel's selector) first
            if example.get("title"):
                self.assertEqual(left[0].kind, "title")
                text = label.render_label(example["title"], "font.page_title", "text.value", unit="em")
                self.assertEqual(left[0].face.width, text.width)               # the width comes from the Label component
            if example.get("power"):
                power = next(p for p in left if p.kind == "power")
                self.assertEqual(power.face.size, (m["content"], m["content"]))
                if example.get("title"):
                    self.assertEqual(power.x, left[0].x + left[0].face.width + m["gap"], example["id"])   # fixed gap after the title
                else:
                    self.assertEqual(power.x, m["side"], example["id"])    # the Play panel starts with POWER
            elif example.get("title"):
                self.assertEqual([p.kind for p in left], ["title"], example["id"])   # a modulator or Master: no POWER slot
        play = next(e for e in EXAMPLES if e["id"] == "play-panel")
        self.assertEqual([p.kind for p in header.layout(play)[1] if p.zone == "left"], ["power"])

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

    def test_play_panel_dropdown_fills_between_power_and_expand_with_gaps(self):
        m = header.metrics()
        play = next(e for e in EXAMPLES if e["id"] == "play-panel")
        width, placed = header.layout(play)
        self.assertEqual(width, style.size("header.panel.width"))
        by_kind = {p.kind: p for p in placed}
        power, drop, expand = by_kind["power"], by_kind["dropdown"], by_kind["expand"]
        self.assertEqual(power.x, m["side"])
        self.assertEqual(drop.x, power.x + power.face.width + m["gap"])                 # 8 px after POWER
        self.assertEqual(drop.x + drop.face.width + m["gap"], expand.x)                 # 8 px before EXPAND
        self.assertEqual(expand.x + expand.face.width + m["side"], width)
        self.assertGreaterEqual(drop.face.width, style.size("header.dropdown.min_width"))

    def test_page_headers_fill_their_flexible_control_up_to_the_right_group(self):
        m = header.metrics()
        for example in EXAMPLES:
            fills = [i for i in example.get("centre", []) if i.get("fill")]
            self.assertLessEqual(len(fills), 1, example["id"])
            width, placed = header.layout(example)
            centre = [p for p in placed if p.zone == "centre"]
            right = [p for p in placed if p.zone == "right"]
            if fills and right:
                self.assertEqual(centre[-1].x + centre[-1].face.width + m["gap"] if fills[0] is example["centre"][-1] else
                                 max(p.x + p.face.width for p in centre) + m["gap"] * 0, right[0].x if fills[0] is example["centre"][-1] else
                                 max(p.x + p.face.width for p in centre), example["id"])
            for p in centre:
                self.assertGreaterEqual(p.face.width, 1)

    def test_buttons_are_the_button_and_icon_button_recipes_never_flat(self):
        flat = style.rgb("button.surface")
        samples = {"power": lambda state: icon_button.render_sample({"icon": "power", "height": header.metrics()["content"], "state": state, "look": "filled"}),
                   "expand": lambda state: icon_button.render_sample({"icon": "expand", "height": header.metrics()["content"], "state": "off", "look": "filled"})}
        for example in EXAMPLES:
            _, placed = header.layout(example)
            for item in placed:
                if item.kind == "power":
                    self.assertEqual(item.face.tobytes(), samples["power"](example["power"]).tobytes(), example["id"])
                elif item.kind == "expand":
                    self.assertEqual(item.face.tobytes(), samples["expand"]("off").tobytes(), example["id"])
                elif item.kind == "close":
                    self.assertEqual(item.face.tobytes(), icon_button.render_sample({"icon": "close", "height": header.metrics()["content"], "state": "off", "look": "filled"}).tobytes())
                elif item.kind == "more":
                    self.assertEqual(item.face.tobytes(), button.render_sample({"text": "MORE", "height": header.metrics()["content"], "state": "enabled"}).tobytes())
            for item in placed:                # no flat (21,26,45) fill inside any button face
                if item.kind in ("power", "expand", "close", "more", "button"):
                    flat_pixels = sum(1 for p in item.face.getdata() if p[3] == 255 and p[:3] == flat)
                    self.assertLessEqual(flat_pixels, 2, (example["id"], item.kind))   # a flat fill would be hundreds of pixels (an edge blend can hit it once)
            with Image.open(DIRECTORY / "img" / example["file"]) as image:
                self.assertEqual(image.size[1], 56)

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
