# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FilterSelector catalogue entry: four lamp states at 56 x 48, catalogue data, order, old Selector pictures gone."""
import json
import re
import sys
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))

from components import style
from components.native.filter_selector import filter_selector as fs

NATIVE = ROOT / "components/native"
HTML = ROOT / "docs/ui-components/kepes-katalogus.html"


class FilterSelectorTests(unittest.TestCase):
    def test_four_states_at_the_token_size_from_the_recipe(self):
        geometry = style.size("filter.chain")
        self.assertEqual((geometry["width"], geometry["height"]), (56, 48))
        self.assertEqual(len(fs.STATES), 4)
        seen = set()
        for _, f1, f2, file, _ in fs.STATES:
            expected = fs.render_state(f1, f2)
            with Image.open(NATIVE / "filter_selector/img" / file) as stored:
                self.assertEqual(stored.size, (56, 48), file)
                self.assertEqual(stored.convert("RGBA").tobytes(), expected.tobytes(), file)
            seen.add(expected.tobytes())
        self.assertEqual(len(seen), 4)   # the lamp bar tells the four on/off combinations apart

    def test_lamp_colours_cyan_on_dim_off(self):
        geometry = style.size("filter.chain")
        lamp_w = (geometry["width"] - geometry["lamp_gap"]) // 2
        y = geometry["lamp_y"] + geometry["lamp_height"] // 2
        on, off = style.rgb("accent.cyan"), style.rgb("theme.neon.track")
        image = fs.render_state(True, False).convert("RGB")
        self.assertEqual(image.getpixel((lamp_w // 2, y)), on)
        self.assertEqual(image.getpixel((lamp_w + geometry["lamp_gap"] + lamp_w // 2, y)), off)

    def test_catalogue_entry_matches_the_module_and_the_old_selector_pictures_are_gone(self):
        catalog = json.loads((NATIVE / "filter_selector/catalog.json").read_text(encoding="utf-8"))
        self.assertEqual(catalog["sections"], fs.catalog_sections())
        selector = json.loads((NATIVE / "selector/catalog.json").read_text(encoding="utf-8"))
        self.assertEqual(selector["sections"], [])
        for name in ("catalog-filter-chain.png", "catalog-selector-56-nameval.png", "catalog-selector-56-valonly.png"):
            self.assertFalse((NATIVE / "selector/img" / name).exists(), name)
        html = HTML.read_text(encoding="utf-8")
        self.assertNotIn("catalog-filter-chain", html)
        self.assertNotIn("catalog-selector-56", html)
        for _, _, _, file, _ in fs.STATES:
            self.assertIn(f"components/native/filter_selector/img/{file}", html)

    def test_order_filter_selector_directly_after_selector(self):
        titles = re.findall(r"<h3>([^<]*)</h3>", HTML.read_text(encoding="utf-8"))
        self.assertEqual(titles[titles.index("Selector") + 1], "FilterSelector")


if __name__ == "__main__":
    unittest.main()
