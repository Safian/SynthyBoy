#!/usr/bin/env python3
# SynthyBoy - tests of scripts/preset-diff.py: what differs between two presets, parameter by parameter, with names and units.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TOOL = ROOT / "scripts" / "preset-diff.py"

PARAMS = {"format": "synthyboy-params", "version": 1, "params": [
    {"key": "master.volume", "name": "Master Volume", "default": -12, "unit": "dB", "decimals": 1},
    {"key": "osc1.level", "name": "Osc 1 Level", "default": 50, "unit": "%", "decimals": 1},
    {"key": "osc1.on", "name": "Osc 1 On", "default": 1, "unit": "", "decimals": 0},
    {"key": "filter1.cutoff", "name": "Filter 1 Cutoff", "default": 20000, "unit": "Hz", "decimals": 0},
]}


def preset(name, params, category="Bass"):
    return {"format": "synthyboy-preset", "version": 1, "meta": {"name": name, "category": category},
            "state": {"format": "synthyboy-state", "version": 4, "params": params}}


class PresetDiff(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        (self.d / "params.json").write_text(json.dumps(PARAMS))

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, fname, obj):
        p = self.d / fname
        p.write_text(obj if isinstance(obj, str) else json.dumps(obj))
        return p

    def run_tool(self, a, b, *extra):
        return subprocess.run([sys.executable, str(TOOL), str(a), str(b), "--params", str(self.d / "params.json")] + list(extra), capture_output=True, text=True)

    def test_identical_presets_exit_0(self):
        a = self.write("a.sbpreset", preset("A", {"master.volume": -12, "osc1.level": 50}))
        b = self.write("b.sbpreset", preset("A", {"osc1.level": 50, "master.volume": -12}))
        r = self.run_tool(a, b)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("no differences", r.stdout)

    def test_a_missing_parameter_counts_as_its_default(self):
        a = self.write("a.sbpreset", preset("A", {"master.volume": -12}))
        b = self.write("b.sbpreset", preset("A", {"master.volume": -12, "osc1.level": 50, "osc1.on": 1}))
        r = self.run_tool(a, b)
        self.assertEqual(r.returncode, 0, "a parameter left out equals one written at its default: " + r.stdout + r.stderr)

    def test_differences_are_listed_in_table_order_with_name_and_unit(self):
        a = self.write("a.sbpreset", preset("A", {"filter1.cutoff": 800, "master.volume": -12, "osc1.level": 50}))
        b = self.write("b.sbpreset", preset("A", {"filter1.cutoff": 1200, "master.volume": -6}))
        r = self.run_tool(a, b)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        lines = [l for l in r.stdout.splitlines() if l.startswith("master.volume") or l.startswith("filter1.cutoff") or l.startswith("osc1.")]
        self.assertEqual(len(lines), 2, r.stdout)
        self.assertTrue(lines[0].startswith("master.volume"), "the order of the parameter table, not of the file: " + r.stdout)
        self.assertIn("Master Volume", lines[0])
        self.assertIn("-12", lines[0])
        self.assertIn("-6", lines[0])
        self.assertIn("dB", lines[0])
        self.assertIn("Filter 1 Cutoff", lines[1])
        self.assertIn("800", lines[1])
        self.assertIn("1200", lines[1])
        self.assertIn("2 parameters differ", r.stdout)

    def test_json_output(self):
        a = self.write("a.sbpreset", preset("A", {"master.volume": -12}))
        b = self.write("b.sbpreset", preset("B", {"master.volume": -6, "osc1.on": 0}, category="Lead"))
        r = self.run_tool(a, b, "--json")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        out = json.loads(r.stdout)
        self.assertEqual(out["params"], [
            {"key": "master.volume", "name": "Master Volume", "unit": "dB", "a": -12, "b": -6},
            {"key": "osc1.on", "name": "Osc 1 On", "unit": "", "a": 1, "b": 0}])
        self.assertEqual(out["meta"], [{"key": "category", "a": "Bass", "b": "Lead"}, {"key": "name", "a": "A", "b": "B"}])

    def test_meta_only_difference_is_reported_but_exit_0_unless_asked(self):
        a = self.write("a.sbpreset", preset("A", {"master.volume": -12}))
        b = self.write("b.sbpreset", preset("B", {"master.volume": -12}))
        r = self.run_tool(a, b)
        self.assertEqual(r.returncode, 0, "the sound is the same: " + r.stdout + r.stderr)
        self.assertIn("meta name", r.stdout)
        r = self.run_tool(a, b, "--strict-meta")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)

    def test_unknown_parameter_is_listed_separately(self):
        a = self.write("a.sbpreset", preset("A", {"master.volume": -12, "old.thing": 3}))
        b = self.write("b.sbpreset", preset("A", {"master.volume": -12}))
        r = self.run_tool(a, b)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("unknown parameter old.thing", r.stdout)

    def test_small_float_noise_is_not_a_difference(self):
        a = self.write("a.sbpreset", preset("A", {"osc1.level": 50.0}))
        b = self.write("b.sbpreset", preset("A", {"osc1.level": 50.0000001}))
        self.assertEqual(self.run_tool(a, b).returncode, 0)
        c = self.write("c.sbpreset", preset("A", {"osc1.level": 50.2}))
        self.assertEqual(self.run_tool(a, c).returncode, 1)

    def test_bad_input_is_exit_2(self):
        a = self.write("a.sbpreset", preset("A", {"master.volume": -12}))
        self.assertEqual(self.run_tool(a, self.d / "missing.sbpreset").returncode, 2)
        bad = self.write("bad.sbpreset", "{not json")
        self.assertEqual(self.run_tool(a, bad).returncode, 2)
        other = self.write("other.sbpreset", {"format": "something-else", "state": {"params": {}}})
        self.assertEqual(self.run_tool(a, other).returncode, 2, "a file that is not a synthyboy-preset is refused")

    def test_real_presets_of_the_repository_load(self):
        bass = sorted((ROOT / "content" / "presets" / "Bass").glob("*.sbpreset"))[:2]
        r = subprocess.run([sys.executable, str(TOOL), str(bass[0]), str(bass[1])], capture_output=True, text=True)
        self.assertIn(r.returncode, (0, 1), r.stdout + r.stderr)
        r = subprocess.run([sys.executable, str(TOOL), str(bass[0]), str(bass[0])], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
