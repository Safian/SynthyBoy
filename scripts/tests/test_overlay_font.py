#!/usr/bin/env python3
# SynthyBoy - tests of the overlay UI font (scripts/gen-overlay-font.py -> shell-mpc/overlay/sdf_font_data.h; the font and its licence in shell-mpc/overlay/fonts/).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "scripts" / "gen-overlay-font.py"
DATA = ROOT / "shell-mpc" / "overlay" / "sdf_font_data.h"
FONTS = ROOT / "shell-mpc" / "overlay" / "fonts"


class OverlayFontTest(unittest.TestCase):
    def test_licence_files_are_with_the_font(self):
        self.assertTrue((FONTS / "TitilliumWeb-SemiBold.ttf").is_file())
        text = (FONTS / "OFL.txt").read_text()
        self.assertIn("SIL Open Font License, Version 1.1", text)

    def test_generated_header_has_the_characters(self):
        text = DATA.read_text()
        cps = {int(m, 16) for m in re.findall(r"\{ 0x([0-9a-f]+), \d+, \d+, \d+, \d+,", text)}
        for ch in "AZaz09 ?%/ÁÉÍÓÖŐÚÜŰáéíóöőúüű°…":
            self.assertIn(ord(ch), cps, ch)
        for ch in "←→↑↓":   # the arrows are drawn by the generator
            self.assertIn(ord(ch), cps, hex(ord(ch)))
        self.assertEqual(sorted(cps), [int(m, 16) for m in re.findall(r"\{ 0x([0-9a-f]+), \d+, \d+, \d+, \d+,", text)], "the table is sorted (binary search)")

    def test_header_is_what_the_generator_makes(self):
        try:
            import numpy, scipy, PIL, fontTools  # noqa: F401
        except ImportError:
            self.skipTest("the generator needs numpy, scipy, Pillow and fontTools")
        r = subprocess.run([sys.executable, str(GEN), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
