#!/usr/bin/env python3
# SynthyBoy - tests for level-to-stock.py (parse_only and selective preset solve)
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPTS = HERE.parent
SCRIPT_PATH = SCRIPTS / "level-to-stock.py"

spec = importlib.util.spec_from_file_location("level_to_stock", SCRIPT_PATH)
lts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lts)


class TestParseOnly(unittest.TestCase):
    def test_none_and_empty(self):
        self.assertIsNone(lts.parse_only(None))
        self.assertIsNone(lts.parse_only(""))
        self.assertIsNone(lts.parse_only("   "))
        self.assertIsNone(lts.parse_only([]))

    def test_single_name(self):
        self.assertEqual(lts.parse_only("Acoustic Bore Reed"), {"Acoustic Bore Reed"})
        self.assertEqual(lts.parse_only("  Acoustic Bore Reed  "), {"Acoustic Bore Reed"})

    def test_comma_separated_names(self):
        parsed = lts.parse_only("Acoustic Bore Reed, Air Chiff Flute, Analog Sweep Bass")
        expected = {"Acoustic Bore Reed", "Air Chiff Flute", "Analog Sweep Bass"}
        self.assertEqual(parsed, expected)

    def test_list_of_names(self):
        parsed = lts.parse_only(["Acoustic Bore Reed", "Air Chiff Flute, Analog Sweep Bass"])
        expected = {"Acoustic Bore Reed", "Air Chiff Flute", "Analog Sweep Bass"}
        self.assertEqual(parsed, expected)


class TestSolveOnlyFilterLogic(unittest.TestCase):
    def test_only_filter_preserves_untouched_presets(self):
        levels = {
            "Preset A": -10.0,
            "Preset B": -8.5,
            "Preset C": -6.0,
        }
        tier_trims = {
            "Preset A": 1.2,
            "Preset B": -0.8,
        }
        only_names = lts.parse_only("Preset B")

        new_levels = {}
        new_tier_trims = {}
        for n, v in levels.items():
            if only_names is not None and n not in only_names:
                new_levels[n] = v
                if n in tier_trims:
                    new_tier_trims[n] = tier_trims[n]
                continue
            # Mutate Preset B
            new_levels[n] = v + 2.0
            new_tier_trims[n] = 0.5

        self.assertEqual(new_levels["Preset A"], -10.0)
        self.assertEqual(new_levels["Preset B"], -6.5)
        self.assertEqual(new_levels["Preset C"], -6.0)

        self.assertEqual(new_tier_trims["Preset A"], 1.2)
        self.assertEqual(new_tier_trims["Preset B"], 0.5)
        self.assertNotIn("Preset C", new_tier_trims)


if __name__ == "__main__":
    unittest.main()
