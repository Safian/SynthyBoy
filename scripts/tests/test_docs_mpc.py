#!/usr/bin/env python3
# SynthyBoy - checks docs/MPC.md: required sections and terms, links and repository paths that exist, no measured numbers.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DOC = ROOT / "docs" / "MPC.md"
TEXT = DOC.read_text(encoding="utf-8") if DOC.exists() else ""

SECTIONS = ["Build", "Package layout", "Install on the MPC", "Known host behaviour", "Sound differences at tier low"]
TERMS = ["scripts/build-armv7-mpc.sh", "MPC_SDK", "ARMV7_IMAGE", "VST2 Plugins", "Safian - VST - SynthyBoy", "plugin.json", "version.xml",
         "VST/synthyboy-arm32.so", "Content/presets", "Plugin Skins", "Enable Enhancements", "External Plugin Support", "44.1 kHz", "128",
         "GCC", "Zig", "scripts/mpc-ui-pages.json", "shell-mpc/accept.sh", "file64"]


def section(title):
    m = re.search(r"^## %s\s*$(.*?)(?=^## |\Z)" % re.escape(title), TEXT, re.M | re.S)
    return m.group(1) if m else None


class MpcDocTests(unittest.TestCase):
    def test_exists_with_one_title(self):
        self.assertTrue(DOC.exists(), "docs/MPC.md is missing")
        self.assertEqual(len(re.findall(r"^# \S", TEXT, re.M)), 1, "exactly one level-1 heading")

    def test_sections_in_order(self):
        found = re.findall(r"^## (.+?)\s*$", TEXT, re.M)
        self.assertEqual([s for s in found if s in SECTIONS], SECTIONS, "sections (## headings) found: %r" % found)

    def test_every_section_has_text(self):
        for s in SECTIONS:
            body = section(s)
            self.assertIsNotNone(body, s)
            self.assertGreaterEqual(len(body.split()), 40, "section '%s' is too thin" % s)

    def test_terms_present(self):
        missing = [t for t in TERMS if t not in TEXT]
        self.assertEqual(missing, [])

    def test_relative_links_resolve(self):
        for target in re.findall(r"\]\(([^)#\s]+)(?:#[^)]*)?\)", TEXT):
            if re.match(r"[a-z]+://", target):
                continue
            self.assertTrue((DOC.parent / target).exists(), "broken link: %s" % target)

    def test_repository_paths_in_backticks_exist(self):
        tops = ("scripts/", "shell-mpc/", "docs/", "core/", "content/", "cmake/", "bench/")
        for code in re.findall(r"`([^`\n]+)`", TEXT):
            for word in code.split():
                word = word.strip(",;:()\"'")
                if word.startswith(tops) and "<" not in word and "*" not in word and "$" not in word:
                    self.assertTrue((ROOT / word).exists(), "path in backticks does not exist: %s" % word)

    def test_tier_low_section_lists_the_changes(self):
        body = section("Sound differences at tier low") or ""
        self.assertGreaterEqual(len(re.findall(r"^\s*[-*] ", body, re.M)), 5, "at least five list items")
        for word in ("oversampl", "tanh", "LFO", "silence", "reverb"):
            self.assertIn(word.lower(), body.lower(), "the tier low section does not mention '%s'" % word)

    def test_no_measured_numbers(self):
        # CPU / timing results come from the measurement on the device, not from this document
        hits = re.findall(r"\b\d+(?:[.,]\d+)?\s*(?:ms\b|µs\b|us\b|%|dBFS\b|x faster\b|times faster\b)", TEXT)
        self.assertEqual(hits, [], "measured numbers do not belong here")

    def test_no_placeholders(self):
        for word in ("TODO", "TBD", "FIXME", "lorem"):
            self.assertNotIn(word, TEXT)


if __name__ == "__main__":
    unittest.main()
