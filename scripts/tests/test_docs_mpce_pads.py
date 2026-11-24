#!/usr/bin/env python3
# SynthyBoy - checks docs/user-guide/mpce-pads.md: required sections, manual citations, plugin details, recipes, and comparison table.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DOC_PATH = os.path.join(ROOT, "docs", "user-guide", "mpce-pads.md")


class MpcePadsDocTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assertTrue(cls, os.path.exists(DOC_PATH), "docs/user-guide/mpce-pads.md must exist")
        with open(DOC_PATH, "r", encoding="utf-8") as f:
            cls.text = f.read()

    def test_single_top_level_heading(self):
        h1 = re.findall(r"^# \S.*$", self.text, re.M)
        self.assertEqual(len(h1), 1, "There should be exactly one level-1 title heading")
        self.assertIn("MPCe", h1[0])

    def test_required_major_sections(self):
        required_sections = [
            "Hardware Generations: Gen 2 (MPCe) vs Gen 1",
            "Host Reality: What Reaches a Plugin Track",
            "SynthyBoy Modulation Sources: What Moves and What Does Not",
            "Master Page Settings: Tuning & MPE",
            "Two Ways to Use Pad Pressure in SynthyBoy",
            "How Factory Presets Use Pad Pressure",
            "Step-by-Step Guide: Routing Pad Pressure on the Mod Page",
            "Five Concrete Playing Recipes",
            "Generation 1 vs Generation 2 Comparison",
            "To Confirm on the Device",
        ]
        for sec in required_sections:
            pattern = r"^## \d+\.?\s+" + re.escape(sec)
            self.assertTrue(re.search(pattern, self.text, re.M), f"Missing section heading: {sec}")

    def test_official_akai_manual_citations(self):
        manuals = [
            "MPC Live III, MPC XL User Guide v3.10.1",
            "MPC Standalone OS User Guide v3.10.1",
            "MPC 3.6 Firmware Update Release Notes",
            "MPC 3.7 Update Release Notes",
            "MPC 3.10 Release Notes",
        ]
        for m in manuals:
            self.assertIn(m, self.text, f"Missing manual citation: {m}")
        # Verify specific page numbers cited
        for page_cite in ["p. 63", "p. 64", "p. 350", "p. 11", "p. 13", "p. 72"]:
            self.assertIn(page_cite, self.text, f"Missing page citation: {page_cite}")

    def test_plugin_side_facts_documented(self):
        required_phrases = [
            "Poly Aftertouch", "Velocity", "Filter 1 Cutoff",
            "PAD PRESS", "PAD %", "35%", "36 semitones", "12 ms",
            "ModSourceBoard", "57 modulation sources",
            "TUNING & MPE", "master.mpe",
            "Full Level", "0xA0", "0xD0",
        ]
        for phrase in required_phrases:
            self.assertIn(phrase, self.text, f"Missing plugin-side detail: {phrase}")

    def test_five_recipes_complete(self):
        recipes = ["Bass", "Lead", "Pad", "Pluck", "FX"]
        for r in recipes:
            self.assertTrue(re.search(r"Recipe \d+:\s+" + r, self.text, re.IGNORECASE), f"Missing recipe for {r}")
        # Every recipe must specify Source, Target, Amount, What You Hear
        matches = re.findall(r"### Recipe \d+:.*?(?=### Recipe|\Z)", self.text, re.S)
        self.assertGreaterEqual(len(matches), 5, "Must have at least 5 recipe blocks")
        for m in matches[:5]:
            self.assertIn("Source:", m)
            self.assertIn("Target", m)
            self.assertIn("Amount:", m)
            self.assertIn("What You Hear:", m)

    def test_gen1_vs_gen2_comparison_table(self):
        table_matches = re.findall(r"(\|.+?\|\n\|[-:| ]+\|\n(?:\|.+?\|\n)+)", self.text)
        self.assertTrue(len(table_matches) >= 2, "Document must contain at least 2 tables (MIDI reality and Gen1 vs Gen2)")
        # Look for the Gen 1 vs Gen 2 table
        gen_table = next((t for t in table_matches if "First Generation" in t and "Second Generation" in t), None)
        self.assertIsNotNone(gen_table, "Comparison table Gen 1 vs Gen 2 not found")
        rows = [line for line in gen_table.splitlines() if line.startswith("|")]
        self.assertGreaterEqual(len(rows), 8, "Gen 1 vs Gen 2 comparison table must have at least 8 rows")

    def test_to_confirm_on_device_section(self):
        confirm_match = re.search(r"^## 10\. To Confirm on the Device.*", self.text, re.M | re.S)
        self.assertIsNotNone(confirm_match, "To Confirm on the Device section missing")
        sec_text = confirm_match.group(0)
        # Check specific unverified areas
        self.assertIn("Q-Link", sec_text)
        self.assertIn("Pad Perform", sec_text)
        self.assertIn("8×8", sec_text)
        self.assertIn("Note Repeat", sec_text)
        self.assertIn("Release Velocity", sec_text)

    def test_no_private_paths(self):
        for bad in ["/Users/", "szabi", "192.168."]:
            self.assertNotIn(bad, self.text, f"Found private or machine-specific string in documentation: {bad}")


if __name__ == "__main__":
    unittest.main()
