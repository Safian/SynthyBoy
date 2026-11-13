#!/usr/bin/env python3
# SynthyBoy - tests of scripts/preset-unison-report.py (unison voices, filters and effects of the factory presets as a Markdown table).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "preset-unison-report.py"
PRESETS = ROOT / "content" / "presets"
FX = ["chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb", "hyperdim", "utility", "bode", "convolve"]
HEADER = "| Preset | Category | Osc 1 | Osc 2 | Osc 3 | Max unison | Filters | FX |"


def run(*args):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), *args], capture_output=True, text=True, cwd=tempfile.gettempdir())


def preset(folder, rel, name, category, params):
    path = Path(folder) / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"format": "synthyboy-preset", "version": 1, "meta": {"name": name, "category": category},
                                "state": {"format": "synthyboy-state", "version": 4, "params": params}}), encoding="utf-8")


def rows(text):
    out = []
    for line in text.splitlines():
        if line.startswith("|") and not line.startswith("| Preset") and not line.startswith("|-") and not line.startswith("| -"):
            out.append([c.strip() for c in re.split(r"(?<!\\)\|", line.strip())[1:-1]])
    return out


def summary(text):
    found = {}
    for line in text.splitlines():
        for n in (3, 4, 5):
            prefix = "Unison above %d on an active oscillator: " % n
            if line.startswith(prefix):
                found[n] = int(line[len(prefix):].split()[0])
        if line.startswith("Presets: "):
            found["presets"] = int(line.split()[1])
    return found


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.tmp.name
        preset(d, "Bass/a.sbpreset", "Alpha", "Bass", {"osc1.on": 1, "osc1.unison.voices": 1, "osc2.on": 0, "osc2.unison.voices": 7, "osc3.on": 0,
                                                       "osc3.unison.voices": 1, "filter1.on": 1, "filter2.on": 0, "chorus.on": 1, "reverb.on": 1,
                                                       "noise.on": 1, "sub.on": 1, "splitter_lh.on": 1, "fxi01.on": 1})
        preset(d, "Lead/b.sbpreset", "Beta | two", "Lead", {"osc1.on": 1, "osc1.unison.voices": 4, "osc2.on": 1, "osc2.unison.voices": 5, "osc3.on": 1,
                                                            "osc3.unison.voices": 6.0, "filter1.on": 1, "filter2.on": 1, "fxfilter.on": 1})
        preset(d, "Lead/c.sbpreset", "Gamma", "Lead", {"osc1.on": 1, "osc1.unison.voices": 4, "osc2.on": 0, "osc3.on": 0})
        preset(d, "Pad/d.sbpreset", "Delta", "Pad", {"osc1.on": 0, "osc1.unison.voices": 16, "osc2.on": 0, "osc3.on": 0, "filter1.on": 0,
                                                     **{"%s.on" % f: 1 for f in FX}})
        Path(d, "contentpack.json").write_text("{}", encoding="utf-8")
        self.result = run("--presets", d)
        self.text = self.result.stdout

    def tearDown(self):
        self.tmp.cleanup()

    def test_exit_code_and_header(self):
        self.assertEqual(self.result.returncode, 0, self.result.stderr)
        self.assertIn(HEADER, self.text.splitlines())
        self.assertEqual(self.text.count(HEADER), 1)

    def test_rows_sorted_by_relative_path(self):
        self.assertEqual([r[0] for r in rows(self.text)], ["Alpha", "Beta \\| two", "Gamma", "Delta"])

    def test_row_values(self):
        r = {x[0]: x[1:] for x in rows(self.text)}
        self.assertEqual(r["Alpha"], ["Bass", "1", "off", "off", "1", "1", "2"])           # an oscillator that is off does not count; noise, sub, splitter, fxi are not counted
        self.assertEqual(r["Beta \\| two"], ["Lead", "4", "5", "6", "6", "2", "1"])     # fxfilter is an effect, not a filter
        self.assertEqual(r["Gamma"], ["Lead", "4", "off", "off", "4", "0", "0"])           # missing keys: unison 1, switches off
        self.assertEqual(r["Delta"], ["Pad", "off", "off", "off", "0", "0", "13"])

    def test_summary(self):
        self.assertEqual(summary(self.text), {"presets": 4, 3: 2, 4: 1, 5: 1})

    def test_out_option_writes_the_same_text(self):
        target = Path(self.tmp.name) / "report.md"
        r = run("--presets", self.tmp.name, "--out", str(target))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "")
        self.assertEqual(target.read_text(encoding="utf-8"), self.text)


class ErrorTests(unittest.TestCase):
    def test_broken_file_is_named_and_fails(self):
        with tempfile.TemporaryDirectory() as d:
            preset(d, "Bass/ok.sbpreset", "Ok", "Bass", {"osc1.on": 1})
            Path(d, "Bass", "broken.sbpreset").write_text("{not json", encoding="utf-8")
            r = run("--presets", d)
            self.assertEqual(r.returncode, 1)
            self.assertIn("broken.sbpreset", r.stderr)
            self.assertNotIn("Traceback", r.stderr)

    def test_file_without_params_fails(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, "x.sbpreset").write_text('{"meta": {"name": "X"}}', encoding="utf-8")
            r = run("--presets", d)
            self.assertEqual(r.returncode, 1)
            self.assertIn("x.sbpreset", r.stderr)
            self.assertNotIn("Traceback", r.stderr)

    def test_empty_or_missing_folder_fails(self):
        with tempfile.TemporaryDirectory() as d:
            for folder in (d, str(Path(d) / "nowhere")):
                r = run("--presets", folder)
                self.assertEqual(r.returncode, 1)
                self.assertNotEqual(r.stderr.strip(), "")
                self.assertNotIn("Traceback", r.stderr)


class FactoryPresetTests(unittest.TestCase):
    def test_default_folder_is_the_factory_presets(self):
        r = run()
        self.assertEqual(r.returncode, 0, r.stderr)
        files = sorted(PRESETS.rglob("*.sbpreset"))
        table = rows(r.stdout)
        self.assertEqual(len(table), len(files))
        above = {3: 0, 4: 0, 5: 0}
        for path, row in zip(sorted(files, key=lambda p: p.relative_to(PRESETS).as_posix()), table):
            data = json.loads(path.read_text(encoding="utf-8"))
            p = data["state"]["params"]
            self.assertEqual(row[0], data["meta"]["name"].replace("|", "\\|"))
            top = max([int(round(p.get("osc%d.unison.voices" % n, 1))) for n in (1, 2, 3) if p.get("osc%d.on" % n, 0)] or [0])
            self.assertEqual(row[5], str(top), path.name)
            self.assertEqual(row[7], str(sum(1 for f in FX if p.get("%s.on" % f, 0))), path.name)
            for n in above:
                above[n] += top > n
        self.assertEqual(summary(r.stdout), {"presets": len(files), **above})


if __name__ == "__main__":
    unittest.main()
