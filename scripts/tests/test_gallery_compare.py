#!/usr/bin/env python3
# SynthyBoy - tests of scripts/gallery-compare.py: which pictures of two UI gallery renders differ, with a side-by-side sheet of the changed ones.
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
TOOL = ROOT / "scripts" / "gallery-compare.py"


class GalleryCompare(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.a, self.b, self.out = d / "before", d / "after", d / "cmp"
        self.a.mkdir()
        self.b.mkdir()
        files_a = {"Init_env1.png": b"E1", "Init_lfo1.png": b"L1", "Init_master.png": b"M1", "Init_old.png": b"O", "index.html": b"<html>", "notes.txt": b"x"}
        files_b = {"Init_env1.png": b"E1", "Init_lfo1.png": b"L2-changed", "Init_master.png": b"M2", "Init_new.png": b"N", "index.html": b"<html2>"}
        for n, c in files_a.items():
            (self.a / n).write_bytes(c)
        for n, c in files_b.items():
            (self.b / n).write_bytes(c)

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *extra):
        return subprocess.run([sys.executable, str(TOOL), str(self.a), str(self.b)] + [str(e) for e in extra], capture_output=True, text=True)

    def test_summary_and_exit_code(self):
        r = self.run_tool()
        self.assertEqual(r.returncode, 1, "pictures differ: " + r.stdout + r.stderr)
        self.assertIn("1 identical, 2 changed, 1 only in A, 1 only in B", r.stdout)
        for name in ("Init_lfo1.png", "Init_master.png"):
            self.assertIn("changed    " + name, r.stdout)
        self.assertIn("only in A  Init_old.png", r.stdout)
        self.assertIn("only in B  Init_new.png", r.stdout)
        self.assertNotIn("Init_env1.png", r.stdout, "identical pictures are not listed without --all")
        self.assertNotIn("index.html", r.stdout, "only .png files are compared")
        self.assertNotIn("notes.txt", r.stdout)

    def test_all_lists_identical_ones_too(self):
        r = self.run_tool("--all")
        self.assertIn("identical  Init_env1.png", r.stdout)

    def test_identical_directories_exit_0(self):
        for n in list(self.b.glob("*.png")):
            n.unlink()
        for n in self.a.glob("*.png"):
            (self.b / n.name).write_bytes(n.read_bytes())
        r = self.run_tool()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("4 identical, 0 changed, 0 only in A, 0 only in B", r.stdout)

    def test_json(self):
        r = self.run_tool("--json")
        out = json.loads(r.stdout)
        self.assertEqual(out, {"identical": ["Init_env1.png"], "changed": ["Init_lfo1.png", "Init_master.png"], "only_a": ["Init_old.png"], "only_b": ["Init_new.png"]})

    def test_only_filter(self):
        r = self.run_tool("--only", "Init_lfo,Init_new")
        self.assertIn("0 identical, 1 changed, 0 only in A, 1 only in B", r.stdout)
        self.assertNotIn("Init_master.png", r.stdout)

    def test_sheet(self):
        r = self.run_tool("--out", self.out)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        sheet = self.out / "index.html"
        self.assertTrue(sheet.is_file())
        html = sheet.read_text(encoding="utf-8")
        rel_a = os.path.relpath(self.a / "Init_lfo1.png", self.out)
        rel_b = os.path.relpath(self.b / "Init_lfo1.png", self.out)
        self.assertIn('src="%s"' % rel_a, html, "pictures are referenced by relative path, not copied")
        self.assertIn('src="%s"' % rel_b, html)
        self.assertIn("Init_new.png", html)
        self.assertIn("Init_old.png", html)
        self.assertNotIn("Init_env1.png", html, "identical pictures are not on the sheet")
        self.assertLess(html.index("Init_lfo1.png"), html.index("Init_master.png"), "changed pictures in name order")
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ["index.html"], "nothing else is written")
        self.assertEqual(sorted(p.name for p in self.a.iterdir()), ["Init_env1.png", "Init_lfo1.png", "Init_master.png", "Init_old.png", "index.html", "notes.txt"], "the inputs are not touched")

    def test_names_are_escaped_in_the_sheet(self):
        (self.a / "Init_a&b.png").write_bytes(b"1")
        (self.b / "Init_a&b.png").write_bytes(b"2")
        self.run_tool("--out", self.out)
        html = (self.out / "index.html").read_text(encoding="utf-8")
        self.assertIn("Init_a&amp;b.png", html)

    def test_bad_input_is_exit_2(self):
        r = subprocess.run([sys.executable, str(TOOL), str(self.a), str(self.a / "missing")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)
        r = subprocess.run([sys.executable, str(TOOL), str(self.a)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
