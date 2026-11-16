#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import os
import re
import subprocess
import unittest

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

class DocsReleaseTests(unittest.TestCase):
    def test_changelog_structure_and_five_lines(self):
        path = os.path.join(ROOT_DIR, "docs", "release", "CHANGELOG-draft.md")
        self.assertTrue(os.path.exists(path), f"File {path} must exist")
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()

        # Check five lines for What SynthyBoy is
        parts = content.split("---", 1)
        self.assertGreater(len(parts), 1, "Must contain '---' separator after intro")
        intro_lines = [l.strip() for l in parts[0].strip().split("\n") if l.strip() and not l.startswith("#")]
        self.assertEqual(len(intro_lines), 5, f"What SynthyBoy is must be exactly five lines, got {len(intro_lines)}")

        # Check required areas exist
        required_headings = [
            "### Sound Engine",
            "### Presets",
            "### MPC Plugin Shell",
            "### Drawn UI Page by Page (Overlay Build)",
            "### Hardware Control",
            "### Known Problems and Limits",
            "### In Progress",
        ]
        for heading in required_headings:
            self.assertIn(heading, content, f"Missing required area heading: {heading}")

    def test_changelog_traceability(self):
        path = os.path.join(ROOT_DIR, "docs", "release", "CHANGELOG-draft.md")
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()

        # Check that bullet items have trailing source references
        bullet_count = 0
        for line in lines:
            line_str = line.strip()
            if line_str.startswith("- "):
                bullet_count += 1
                self.assertTrue(line_str.endswith(")") or line_str.endswith(")."), f"Bullet point missing trailing source reference: {line_str}")

        self.assertGreater(bullet_count, 20, "Should have substantive coverage of features")

        # Verify all commit hashes referenced in CHANGELOG-draft.md
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()

        hashes = re.findall(r'\b([0-9a-f]{7,8})\b', text)
        for h in set(hashes):
            res = subprocess.run(
                ["git", "rev-parse", "--verify", h],
                cwd=ROOT_DIR,
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, f"Referenced commit hash '{h}' does not exist in git history")

    def test_history_by_week_line_limits(self):
        path = os.path.join(ROOT_DIR, "docs", "release", "HISTORY-by-week.md")
        self.assertTrue(os.path.exists(path), f"File {path} must exist")
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()

        sections = re.split(r'### Week ', content)
        self.assertGreater(len(sections), 1, "Must contain week sections")

        for sec in sections[1:]:
            header, *body = sec.strip().split("\n")
            lines = [l.strip() for l in body if l.strip() and l.strip().startswith("-")]
            line_count = len(lines)
            self.assertGreaterEqual(line_count, 3, f"Week {header} has fewer than 3 lines: {line_count}")
            self.assertLessEqual(line_count, 8, f"Week {header} has more than 8 lines: {line_count}")

    def test_open_questions_completeness(self):
        path = os.path.join(ROOT_DIR, "docs", "release", "OPEN-QUESTIONS.md")
        self.assertTrue(os.path.exists(path), f"File {path} must exist")
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()

        # Check required sections
        self.assertIn("## 1. Items Marked as Not Verified / Unverified", content)
        self.assertIn("## 2. Items Marked as Not Measured / Not Seen", content)
        self.assertIn("## 3. Known Bugs and Faults", content)
        self.assertIn("## 4. Decisions Left to the Owner", content)
        self.assertIn("## 5. TODO and Implementation Backlog Items", content)
        self.assertIn("## 6. Commit Log Markers", content)

        # Check that it contains entries
        self.assertIn("Total in documentation:", content)
        self.assertIn("Total commits identified:", content)

if __name__ == "__main__":
    unittest.main()
