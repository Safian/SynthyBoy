#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The overlay controls (Pen::control) take the skin Button's gradient look from the same tokens.

docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md section C: change button.gradient.* / control.gradient and the generated C++ theme changes with it.
"""
import importlib.util
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from components import style

GENERATOR = ROOT / "scripts/gen-ui-theme.py"
HEADER = ROOT / "shell-mpc/ui/theme_tokens.h"


def load_generator():
    spec = importlib.util.spec_from_file_location("gen_ui_theme", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def field(text, look, name):
    block = text[text.index(f"inline const Look {look} = {{"):]
    block = block[:block.index("};")]
    match = re.search(rf"^\s*(\S+), // {name}$", block, re.M)
    assert match, name
    return match.group(1)


class OverlayControlLookTests(unittest.TestCase):
    def test_neon_control_fields_equal_the_button_tokens(self):
        text = HEADER.read_text()
        for name, token in (("controlTop", "button.gradient.top"), ("controlBottom", "button.gradient.bottom"),
                            ("controlBorder", "button.gradient.border"), ("controlBorderBottom", "button.gradient.border.bottom")):
            self.assertEqual(field(text, "kLook1", name), "0x" + style.color(token)[1:], name)
        span = style.size("control.gradient")
        self.assertAlmostEqual(float(field(text, "kLook1", "controlGradStart")[:-1]), span["start"], places=6)
        self.assertAlmostEqual(float(field(text, "kLook1", "controlGradEnd")[:-1]), span["end"], places=6)

    def test_other_looks_derive_from_their_own_panel_tokens(self):
        text = HEADER.read_text()
        for look, name in (("kLook2", "aurora"), ("kLook3", "plasma")):
            for target, source in (("controlTop", "panel0"), ("controlBottom", "panel1"), ("controlBorder", "keyline"), ("controlBorderBottom", "keyline")):
                self.assertEqual(field(text, look, target), "0x" + style.color(f"theme.{name}.{source}")[1:], f"{name} {target}")

    def test_changing_a_button_token_changes_the_generated_theme(self):
        generator = load_generator()
        before = generator.render_header()
        colours = style._data("colors.json")["colors"]
        original = colours["button.gradient.top"]
        colours["button.gradient.top"] = "#ff00ff"
        try:
            after = generator.render_header()
        finally:
            colours["button.gradient.top"] = original
        self.assertNotEqual(before, after)
        self.assertEqual(field(after, "kLook1", "controlTop"), "0xff00ff")
        self.assertEqual(field(before, "kLook1", "controlTop"), "0x" + original[1:])


if __name__ == "__main__":
    unittest.main()
