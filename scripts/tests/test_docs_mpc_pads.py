#!/usr/bin/env python3
# SynthyBoy - docs/MPC.md states what the MPC XL pads send to the plugin (measured facts, 2026-11-14).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def section(text, heading):
    m = re.search(r"^## " + re.escape(heading) + r"\s*$", text, re.M)
    if not m:
        return None
    nxt = re.search(r"^## ", text[m.end():], re.M)
    return text[m.end(): m.end() + nxt.start()] if nxt else text[m.end():]


class PadsSection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = open(os.path.join(ROOT, "docs", "MPC.md"), encoding="utf-8").read()
        cls.sec = section(cls.text, "MPC XL pads")

    def test_section_exists_once(self):
        self.assertIsNotNone(self.sec, "docs/MPC.md needs a '## MPC XL pads' section")
        self.assertEqual(len(re.findall(r"^## MPC XL pads\s*$", self.text, re.M)), 1)

    def test_states_the_measured_facts(self):
        s = self.sec or ""
        for phrase in [
            "MPC XL", "3.10.1.12", "Full Level",
            "poly aftertouch", "velocity", "127",
            "channel 1", "pitch bend", "CC 74", "channel pressure",
            "X/Y", "receiveVstMidiEvent", "bypass", "effVendorSpecific",
            "MPE Pressure Lead", "Filter 1 Cutoff", "Osc 1 Unison Detune",
            "6 ms",
        ]:
            self.assertIn(phrase, s, "missing in the pads section: " + phrase)

    def test_has_the_comparison_table(self):
        s = self.sec or ""
        rows = [l for l in s.splitlines() if l.startswith("|")]
        self.assertGreaterEqual(len(rows), 7, "a table with a header, a separator and at least 5 data rows")
        header = rows[0].lower()
        self.assertIn("full level on", header)
        self.assertIn("full level off", header)

    def test_says_what_was_not_measured(self):
        s = (self.sec or "").lower()
        self.assertIn("not measured", s)
        for phrase in ["q-link", "16 levels", "note repeat"]:
            self.assertIn(phrase, s)

    def test_no_private_data_and_size(self):
        s = self.sec or ""
        for bad in ["/Users/", "szabi", "192.168."]:
            self.assertNotIn(bad, s)
        words = len(s.split())
        self.assertTrue(180 <= words <= 520, "the section has %d words, wanted 180..520" % words)


if __name__ == "__main__":
    unittest.main()
