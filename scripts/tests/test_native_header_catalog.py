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


def view(example):
    return header.flat(example)      # the flat view of a zones spec: title, title_slot, power, centre, right


class HeaderCatalogTests(unittest.TestCase):
    def test_tokens(self):
        self.assertEqual((style.size("header.height"), style.size("header.padding"), style.size("header.gap")), (56, 8, 8))
        m = header.metrics()
        self.assertEqual(m["content"], style.size("height.intermediate"))   # 56 - 2 x 8 = 40
        with self.assertRaises(ValueError):
            style.size("header.side")                                      # one margin token: header.padding
        self.assertEqual(style.size("header.page.width"), style.size("screen.width"))

    def test_every_example_has_one_picture(self):
        self.assertEqual([e["id"] for e in EXAMPLES],
                         ["osc-a", "lfo-9", "env-1", "filter-1", "master", "osc-a-off",
                          "osc-abc", "osc-abc-off", "osc-sub", "osc-noise", "osc-noise-sample", "env-page", "lfo-page", "curve", "mod", "filter-screen"])
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
            if view(example).get("right") or any(i.get("fill") for i in view(example).get("centre", [])):
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
            self.assertEqual(left[0].x, m["side"], example["id"])           # title first
            if view(example).get("title"):
                self.assertEqual(left[0].kind, "title")
                text = label.render_label(view(example)["title"], "font.page_title", "text.value", unit="em")
                self.assertEqual(left[0].face.width, text.width)               # the width comes from the Label component
            if view(example).get("power"):
                power = next(p for p in left if p.kind == "power")
                self.assertEqual(power.face.size, (m["content"], m["content"]))
                if view(example).get("title"):
                    self.assertEqual(power.x, left[0].x + left[0].face.width + m["gap"], example["id"])   # fixed gap after the title
                else:
                    self.assertEqual(power.x, m["side"], example["id"])    # a header without a title starts with POWER
            elif view(example).get("title"):
                self.assertEqual([p.kind for p in left], ["title"], example["id"])   # a modulator or Master: no POWER slot

    def test_right_zone_and_states(self):
        by_id = {e["id"]: header.layout(e)[1] for e in EXAMPLES}
        self.assertEqual([p.kind for p in by_id["lfo-9"] if p.zone == "right"], [])
        self.assertEqual([p.kind for p in by_id["osc-a"] if p.zone == "right"], ["more"])
        self.assertEqual([p.kind for p in by_id["master"]], ["title"])
        on = {p.kind: p.face.tobytes() for p in by_id["osc-a"]}
        off = {p.kind: p.face.tobytes() for p in by_id["osc-a-off"]}
        self.assertNotEqual(on["power"], off["power"])
        self.assertNotEqual(on["dropdown"], off["dropdown"])
        self.assertEqual(on["title"], off["title"])
        self.assertEqual(on["more"], off["more"])

    def test_page_headers_fill_their_flexible_control_up_to_the_right_group(self):
        m = header.metrics()
        for example in EXAMPLES:
            fills = [i for i in view(example).get("centre", []) if i.get("fill")]
            self.assertLessEqual(len(fills), 1, example["id"])
            width, placed = header.layout(example)
            centre = [p for p in placed if p.zone == "centre"]
            right = [p for p in placed if p.zone == "right"]
            if fills and right:
                self.assertEqual(centre[-1].x + centre[-1].face.width + m["gap"], right[0].x, example["id"])
            for p in centre:
                self.assertGreaterEqual(p.face.width, 1)

    def test_buttons_are_the_button_and_icon_button_recipes_never_flat(self):
        flat = style.rgb("button.surface")
        samples = {"power": lambda state: icon_button.render_sample({"icon": "power", "height": header.metrics()["content"], "state": state, "look": "filled"})}
        for example in EXAMPLES:
            _, placed = header.layout(example)
            for item in placed:
                if item.kind == "power":
                    self.assertEqual(item.face.tobytes(), samples["power"](view(example)["power"]).tobytes(), example["id"])
                elif item.kind == "more":
                    self.assertEqual(item.face.tobytes(), button.render_sample({"text": "MORE", "height": header.metrics()["content"], "state": "enabled"}).tobytes())
            for item in placed:                # no flat (21,26,45) fill inside any button face
                if item.kind in ("power", "more", "button"):
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


SKIN_SPEC = json.loads((DIRECTORY / "skin-osc.json").read_text(encoding="utf-8"))


class HeaderSkinSpecTests(unittest.TestCase):
    """header_definitions(spec): the skin definitions of a spec list; the committed skin is exactly what the tool writes."""

    @classmethod
    def setUpClass(cls):
        by_id = {e["id"]: e for e in EXAMPLES}
        cls.built = {form: header.header_definitions(by_id[form]) for form in SKIN_SPEC["forms"]}

    def test_no_power_state_twins(self):
        keys = [k for built in self.built.values() for k, _ in built["definitions"]]
        self.assertFalse([k for k in keys if k.endswith("Off")])
        self.assertEqual(len(set(keys)), 16)      # 4 forms + 9 cells + the 3 route definitions (the NativeActive pattern)

    def test_form_holds_both_power_states_behind_the_hdr_on_off_names(self):
        for form, built in self.built.items():
            definition = dict(built["definitions"])[built["form"]]
            gates = [g for c in definition["componentsData"] for g in c["bounds"].get("additionalInvalidatingHandles", [])]
            self.assertIn("HdrOn", gates, form)
            self.assertIn("HdrOff", gates, form)
            names = [c["componentData"]["name"] for c in definition["componentsData"]]
            self.assertEqual(names[:2], ["Title", "Power"], form)

    def test_shared_definitions_are_built_identically_by_every_form(self):
        seen = {}
        for built in self.built.values():
            for key, definition in built["definitions"]:
                self.assertEqual(seen.setdefault(key, definition), definition, key)

    def test_the_committed_skin_is_what_the_tool_writes(self):
        doc = json.loads((ROOT / SKIN_SPEC["file"]).read_text(encoding="utf-8"))
        committed = {e["key"]: e["value"] for e in doc["componentDefinitions"]["localComponentDefinitions"]}
        for built in self.built.values():
            for key, definition in built["definitions"]:
                self.assertEqual(committed[key], definition, key)
        owned = [k for k in committed if k.startswith(SKIN_SPEC["prefix"]) or k.startswith(("NativeActive " + SKIN_SPEC["prefix"], "NativeInactive " + SKIN_SPEC["prefix"]))]
        self.assertEqual(sorted(owned), sorted({k for b in self.built.values() for k, _ in b["definitions"]}))

    def test_faces_are_the_committed_pictures(self):
        for built in self.built.values():
            for name, face in built["images"].items():
                with Image.open(ROOT / "skin" / name) as image:
                    self.assertEqual(image.convert("RGBA").tobytes(), face.tobytes(), name)

    def test_a_spec_without_skin_block_or_unknown_slot_kind_is_refused(self):
        with self.assertRaises(KeyError):
            header.header_definitions(EXAMPLES[0])
        bad = json.loads(json.dumps(EXAMPLES[0]))
        bad["zones"]["right"] = [{"kind": "stepper"}]
        with self.assertRaises(ValueError):
            header.header_examples({"examples": [bad]})


class HeaderNoPowerSkinTests(unittest.TestCase):
    """The headers without POWER (Env, LFO, Note / Velo curves, Mod): the committed skin is what the tool writes from skin-<family>.json;
    no Power button, no HdrOn / HdrOff gating, no On / Off twins."""

    def test_the_committed_skin_is_what_the_tool_writes(self):
        by_id = {e["id"]: e for e in EXAMPLES}
        for family in ("env", "lfo", "curve", "mod"):
            spec = json.loads((DIRECTORY / f"skin-{family}.json").read_text(encoding="utf-8"))
            doc = json.loads((ROOT / spec["file"]).read_text(encoding="utf-8"))
            committed = {e["key"]: e["value"] for e in doc["componentDefinitions"]["localComponentDefinitions"]}
            for form in spec["forms"]:
                built = header.header_definitions(by_id[form])
                names = [c["componentData"]["name"] for c in dict(built["definitions"])[built["form"]]["componentsData"]]
                self.assertNotIn("Power", names, form)
                for key, definition in built["definitions"]:
                    self.assertEqual(committed[key], definition, key)
                    self.assertFalse(key.endswith("Off"), key)
                gates = [g for c in dict(built["definitions"])[built["form"]]["componentsData"] for g in c["bounds"].get("additionalInvalidatingHandles", [])]
                self.assertFalse({"HdrOn", "HdrOff"} & set(gates), form)


if __name__ == "__main__":
    unittest.main()
