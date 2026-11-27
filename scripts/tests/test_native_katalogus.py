#!/usr/bin/env python3
"""Unit tests for the Native Screen Catalogue (docs/ui-components/native-katalogus.html)."""

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DOCS_UI = ROOT / "docs" / "ui-components"
KATALOGUS_PATH = DOCS_UI / "native-katalogus.html"
CATALOG_PATH = ROOT / "scripts" / "screen-catalog.json"


class TestNativeKatalogus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.content = KATALOGUS_PATH.read_text(encoding="utf-8")
        cls.screens = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))["screens"]

    def test_file_exists_and_not_empty(self):
        self.assertTrue(KATALOGUS_PATH.exists())
        self.assertGreater(len(self.content), 1000)

    def test_contains_all_screens(self):
        sections = re.findall(r'<section id=[\"\'](.*?)[\"\']>', self.content)
        expected = [s["name"] for s in self.screens]
        self.assertEqual(sections, expected)

    def test_section_titles_and_pair_containers(self):
        for screen in self.screens:
            sec_match = re.search(rf'<section id=[\"\']{re.escape(screen["name"])}[\"\']>(.*?)</section>', self.content, re.DOTALL)
            self.assertIsNotNone(sec_match, f"Missing section for {screen['name']}")
            sec = sec_match.group(1)
            self.assertIn(f"<h2>{screen['title']}</h2>", sec)
            self.assertIn('class="pair"', sec)

    def test_image_integrity_and_existence(self):
        img_srcs = re.findall(r'<img [^>]*src=[\"\']([^\"\']+)[\"\']', self.content)
        self.assertGreaterEqual(len(img_srcs), len(self.screens))

        missing = []
        for src in img_srcs:
            src_clean = src.split("?")[0]
            img_path = (DOCS_UI / src_clean).resolve()
            if not img_path.exists():
                missing.append(src)
        self.assertEqual(missing, [], f"Missing image references: {missing}")

    def test_rejected_designs_excluded(self):
        self.assertNotIn("osc-curve-side-controls", self.content)
        self.assertNotIn("osc_curve_side_controls", self.content)


if __name__ == "__main__":
    unittest.main()
