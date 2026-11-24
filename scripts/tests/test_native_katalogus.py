#!/usr/bin/env python3
"""Unit tests for the Native Screen Catalogue (docs/ui-components/native-katalogus.html)."""

import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DOCS_UI = ROOT / "docs" / "ui-components"
KATALOGUS_PATH = DOCS_UI / "native-katalogus.html"
PAN_LEVEL_WORKTREE_PATH = Path("/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-pan-level-lfo-preview/docs/ui-components/native-katalogus.html")

EXPECTED_15_SCREENS = [
    "Play",
    "Macros",
    "Osc",
    "Osc B",
    "Osc C",
    "Sub",
    "Noise",
    "Env",
    "LFO",
    "FX",
    "Mix",
    "Filter",
    "Mod",
    "Master",
    "Voice"
]


class TestNativeKatalogus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.content = KATALOGUS_PATH.read_text(encoding="utf-8")

    def test_file_exists_and_not_empty(self):
        self.assertTrue(KATALOGUS_PATH.exists())
        self.assertGreater(len(self.content), 50000)

    def test_contains_all_15_screens(self):
        sections = re.findall(r'<section id=[\"\'](.*?)[\"\']>', self.content)
        self.assertEqual(len(sections), 15)
        for expected in EXPECTED_15_SCREENS:
            self.assertIn(expected, sections)

    def test_change_box_and_badges_on_every_screen(self):
        for screen in EXPECTED_15_SCREENS:
            sec_match = re.search(rf'<section id=[\"\']{re.escape(screen)}[\"\']>(.*?)</section>', self.content, re.DOTALL)
            self.assertIsNotNone(sec_match, f"Missing section for {screen}")
            sec = sec_match.group(1)

            # Check change box
            self.assertIn("Mi változott ezen a fülön?", sec)
            self.assertIn("badge-task", sec)
            self.assertIn("badge-code", sec)
            self.assertIn("badge-preview", sec)
            self.assertIn("badge-hw", sec)

            # Check labels
            self.assertIn("🔴 Futó feladat", sec)
            self.assertIn("🟢 Elkészült kód", sec)
            self.assertIn("🔵 Komponált előnézet", sec)
            self.assertIn("🟣 Valódi eszközkép", sec)

    def test_image_integrity_and_existence(self):
        img_srcs = re.findall(r'<img [^>]*src=[\"\']([^\"\']+)[\"\']', self.content)
        self.assertGreater(len(img_srcs), 30)

        missing = []
        for src in img_srcs:
            if src.startswith("data:"):
                self.assertIn("data:image/webp;base64,", src)
                self.assertGreater(len(src), 500)
            else:
                img_path = (DOCS_UI / src).resolve()
                if not img_path.exists():
                    missing.append(src)
        self.assertEqual(missing, [], f"Missing image references: {missing}")

    def test_rejected_designs_excluded(self):
        self.assertNotIn("osc-curve-side-controls", self.content)
        self.assertNotIn("osc_curve_side_controls", self.content)

    def test_overlay_only_warning_preserved(self):
        self.assertIn("play-preview-overlay-only.png", self.content)
        self.assertIn("Nem natív képernyőkép", self.content)

    def test_pan_level_worktree_matches_persistent(self):
        if PAN_LEVEL_WORKTREE_PATH.exists():
            pan_content = PAN_LEVEL_WORKTREE_PATH.read_text(encoding="utf-8")
            self.assertEqual(pan_content, self.content)


if __name__ == "__main__":
    unittest.main()
