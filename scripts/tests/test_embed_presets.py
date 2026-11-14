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
            return (cat.encode(), name.encode().lower(), rel)

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

    def _run(self, presets_dir, tmpdir):
        out_h = Path(tmpdir) / "EmbeddedPresets.h"
        out_cpp = Path(tmpdir) / "EmbeddedPresets.cpp"
        res = subprocess.run(
            [sys.executable, str(SCRIPT), "--presets-dir", str(presets_dir), "--out-h", str(out_h), "--out-cpp", str(out_cpp)],
            capture_output=True, text=True)
        return res, out_h, out_cpp

    def _arrays(self, cpp_text):
        """name -> bytes of each `static const char pNNN_json[] = { ... 0 };` array."""
        import re
        result = {}
        for m in re.finditer(r"static const char (p\d+_json)\[\] = \{(.*?)\};", cpp_text, re.S):
            nums = [int(x) for x in m.group(2).replace("\n", "").split(",") if x.strip()]
            result[m.group(1)] = bytes(n & 0xFF for n in nums[:-1])
        return result

    def test_embedded_bytes_are_the_file_bytes(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            res, _, out_cpp = self._run(PRESETS_DIR, tmpdir)
            self.assertEqual(res.returncode, 0, res.stderr)
            text = out_cpp.read_text(encoding="utf-8")
            arrays = self._arrays(text)
            self.assertEqual(len(arrays), 112)
            embedded = sorted(arrays.values())
            on_disk = sorted(p.read_bytes() for p in PRESETS_DIR.glob("*/*.sbpreset"))
            self.assertEqual(embedded, on_disk)
            # portable: nothing of the build machine (source path, temp path) in the generated source
            self.assertNotIn(str(ROOT), text)
            self.assertNotIn(str(tmpdir), text)

    def test_synthetic_presets_crlf_utf8_and_quotes(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src = Path(tmpdir) / "presets" / "Lead"
            src.mkdir(parents=True)
            doc = '{\r\n"meta":{"name":"Caf\u00e9 \\"Q\\"? 1","category":"Lead"}}\r\n'
            (src / "a.sbpreset").write_bytes(doc.encode("utf-8"))
            (src / "b.sbpreset").write_bytes('{"meta":{"name":"alpha","category":"Lead"}}'.encode())
            out = Path(tmpdir) / "out"
            res, _, out_cpp = self._run(Path(tmpdir) / "presets", out)
            self.assertEqual(res.returncode, 0, res.stderr)
            text = out_cpp.read_text(encoding="utf-8")
            arrays = self._arrays(text)
            self.assertIn(doc.encode("utf-8"), arrays.values())  # CRLF and UTF-8 bytes kept
            # ASCII-case-insensitive order: "alpha" < "Caf\xc3\xa9..."
            table = [l.strip() for l in text.splitlines() if l.strip().startswith('{ "Lead"')]
            self.assertTrue(table[0].startswith('{ "Lead", "alpha"'))
            self.assertIn('Caf\\303\\251 \\"Q\\"\\? 1', table[1])

    def test_invalid_preset_fails(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src = Path(tmpdir) / "presets"
            src.mkdir()
            (src / "x.sbpreset").write_text("{not json", encoding="utf-8")
            res, _, _ = self._run(src, Path(tmpdir) / "out")
            self.assertNotEqual(res.returncode, 0)


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
