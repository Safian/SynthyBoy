#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Static checks of the build files after the Vital engine removal (docs/plans/vital-removal.md): no cmake run, the files are read as text."""

import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(*p):
    with open(os.path.join(ROOT, *p), encoding="utf-8") as f:
        return f.read()


class DefaultEngine(unittest.TestCase):
    """R1: a configure without flags builds the own engine only."""

    def test_with_vital_defaults_off(self):
        m = re.search(r"^option\(SYNTHYBOY_WITH_VITAL\b[^\n]*?\b(ON|OFF)\)", read("CMakeLists.txt"), re.M)
        self.assertIsNotNone(m, "option(SYNTHYBOY_WITH_VITAL ...) not found")
        self.assertEqual(m.group(1), "OFF")

    def test_shell_engine_defaults_to_own(self):
        text = read("CMakeLists.txt")
        self.assertNotRegex(text, r'set\(SYNTHYBOY_SHELL_ENGINE_DEFAULT "vital"\)')
        self.assertRegex(text, r'set\(SYNTHYBOY_SHELL_ENGINE "\$\{SYNTHYBOY_SHELL_ENGINE_DEFAULT\}"|set\(SYNTHYBOY_SHELL_ENGINE "own"')


if __name__ == "__main__":
    unittest.main()
