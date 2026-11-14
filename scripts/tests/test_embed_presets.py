#!/usr/bin/env python3
# SynthyBoy - tests of the factory preset embedding generator (scripts/embed-presets.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "embed-presets.py"
PRESETS_DIR = ROOT / "content" / "presets"


class EmbedPresetsTest(unittest.TestCase):
    def test_preset_count_and_categories(self):
        self.assertTrue(SCRIPT.is_file(), f"{SCRIPT} must exist")
        self.assertTrue(PRESETS_DIR.is_dir(), f"{PRESETS_DIR} must exist")

        with tempfile.TemporaryDirectory() as tmpdir:
            out_h = Path(tmpdir) / "EmbeddedPresets.h"
            out_cpp = Path(tmpdir) / "EmbeddedPresets.cpp"

            cmd = [
                sys.executable,
                str(SCRIPT),
                "--presets-dir",
                str(PRESETS_DIR),
                "--out-h",
                str(out_h),
                "--out-cpp",
                str(out_cpp),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Script failed: {res.stderr}")
            self.assertTrue(out_h.is_file())
            self.assertTrue(out_cpp.is_file())

            h_text = out_h.read_text(encoding="utf-8")
            self.assertIn("struct EmbeddedPreset", h_text)
            self.assertIn("getEmbeddedPresets", h_text)

            cpp_text = out_cpp.read_text(encoding="utf-8")
            self.assertIn("kEmbeddedPresets[] = {", cpp_text)
            self.assertIn("p001_json", cpp_text)
            self.assertIn("p112_json", cpp_text)
            self.assertNotIn("p113_json", cpp_text)

            # Check that 112 entries are in kEmbeddedPresets
            entry_count = cpp_text.count("sizeof(p")
            self.assertEqual(entry_count, 112)

    def test_canonical_sort_order(self):
        """Verify sorting: category ascending, name case-insensitively ascending, relative path."""
        files = list(PRESETS_DIR.glob("*/*.sbpreset"))
        self.assertEqual(len(files), 112)

        raw = []
        for p in files:
            with open(p, "r", encoding="utf-8") as f:
                d = json.load(f)
            meta = d["meta"]
            rel = p.relative_to(PRESETS_DIR).as_posix()
            raw.append((meta["category"], meta["name"], rel))

        def sort_key(item):
            cat, name, rel = item
            return (cat, [c.lower() for c in name], rel)

        expected = sorted(raw, key=sort_key)

        with tempfile.TemporaryDirectory() as tmpdir:
            out_h = Path(tmpdir) / "EmbeddedPresets.h"
            out_cpp = Path(tmpdir) / "EmbeddedPresets.cpp"

            cmd = [
                sys.executable,
                str(SCRIPT),
                "--presets-dir",
                str(PRESETS_DIR),
                "--out-h",
                str(out_h),
                "--out-cpp",
                str(out_cpp),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0)

            cpp_lines = out_cpp.read_text(encoding="utf-8").splitlines()
            table_lines = [l.strip() for l in cpp_lines if l.strip().startswith('{ "') and "sizeof(p" in l]
            self.assertEqual(len(table_lines), 112)

            for i, (cat, name, _) in enumerate(expected):
                line = table_lines[i]
                expected_prefix = f'{{ "{cat}", "{name}",'
                self.assertTrue(
                    line.startswith(expected_prefix),
                    f"At index {i}: expected prefix '{expected_prefix}', got '{line}'",
                )

    def test_deterministic_output(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            d1 = Path(tmpdir) / "d1"
            d2 = Path(tmpdir) / "d2"
            out_h1 = d1 / "EmbeddedPresets.h"
            out_cpp1 = d1 / "EmbeddedPresets.cpp"
            out_h2 = d2 / "EmbeddedPresets.h"
            out_cpp2 = d2 / "EmbeddedPresets.cpp"

            subprocess.run(
                [sys.executable, str(SCRIPT), "--presets-dir", str(PRESETS_DIR), "--out-h", str(out_h1), "--out-cpp", str(out_cpp1)],
                check=True,
            )
            subprocess.run(
                [sys.executable, str(SCRIPT), "--presets-dir", str(PRESETS_DIR), "--out-h", str(out_h2), "--out-cpp", str(out_cpp2)],
                check=True,
            )

            self.assertEqual(out_h1.read_bytes(), out_h2.read_bytes())
            self.assertEqual(out_cpp1.read_bytes(), out_cpp2.read_bytes())

    def test_missing_directory_fails(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            out_h = Path(tmpdir) / "h.h"
            out_cpp = Path(tmpdir) / "cpp.cpp"
            cmd = [
                sys.executable,
                str(SCRIPT),
                "--presets-dir",
                str(Path(tmpdir) / "nonexistent"),
                "--out-h",
                str(out_h),
                "--out-cpp",
                str(out_cpp),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(res.returncode, 0)


if __name__ == "__main__":
    unittest.main()
