#!/usr/bin/env python3
# SynthyBoy - tests of the --clear mode of scripts/mpc-project-qlinks.py: a copy of a project whose stored Q-Link maps are emptied, so the host takes the skin's Q-Links.json again.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import gzip
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "scripts" / "mpc-project-qlinks.py"
spec = importlib.util.spec_from_file_location("mpc_project_qlinks", TOOL)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

HEADER = b"ACVS\n3.10.1.12\nSerialisableProjectData\njson\nLinux\n"


def entry(t, s, q, a):
    return {"parameterIndex": "%d %d %d" % (t, s, q), "automationIndex": a, "value": 0.25, "name": "p%d" % a}


def doc():
    return {"data": {"version": 30, "key": "C Major", "volume": 0.7079457640647888, "tracks": [
        {"program": {"name": "Plugin 001", "pan": 0.5, "customisable": {"mapping": [entry(0, 0, 4, 880), entry(4, 0, 0, 137)]}}},
        {"program": {"name": "Plugin 002", "customisable": {"mapping": [entry(1, 0, 0, 24)]}}},
        {"program": {"name": "Drums", "customisable": {"mapping": []}}},
        {"audio": {"name": "Audio 001"}}]}}


def project_bytes(compressed=True):
    raw = HEADER + json.dumps(doc(), indent=0).encode()
    return gzip.compress(raw) if compressed else raw


def read_back(path):
    raw = Path(path).read_bytes()
    was_gzip = raw[:2] == b"\x1f\x8b"
    if was_gzip:
        raw = gzip.decompress(raw)
    return was_gzip, raw[:len(HEADER)], json.loads(raw[len(HEADER):].decode("utf-8"))


def without_maps(d, only=None):
    for t in d["data"]["tracks"]:
        p = t.get("program")
        if p and "customisable" in p and (only is None or p["name"] == only):
            p["customisable"]["mapping"] = []
    return d


class Clear(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL)] + [str(a) for a in args], capture_output=True, text=True)

    def test_clear_empties_every_map_and_changes_nothing_else(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "in.xpj", Path(d) / "out.xpj"
            src.write_bytes(project_bytes())
            before = src.read_bytes()
            r = self.run_tool(src, "--clear", out)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(src.read_bytes(), before, "the input file must not be written")
            was_gzip, header, got = read_back(out)
            self.assertTrue(was_gzip, "a gzip input gives a gzip output")
            self.assertEqual(header, HEADER, "the five header lines are kept byte for byte")
            self.assertEqual(got, without_maps(doc()), "only the mappings are emptied, every other value is the same (floats included)")
            self.assertEqual(mod.track_maps(mod.load_project(str(out))), [])
            self.assertIn("3 entries", r.stdout, "the tool says how many entries it removed in all")
            self.assertIn("2 tracks", r.stdout, "and from how many tracks")

    def test_plain_input_gives_plain_output(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "in.xpj", Path(d) / "out.xpj"
            src.write_bytes(project_bytes(compressed=False))
            r = self.run_tool(src, "--clear", out)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            was_gzip, header, got = read_back(out)
            self.assertFalse(was_gzip)
            self.assertEqual(got, without_maps(doc()))

    def test_only_the_named_program(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "in.xpj", Path(d) / "out.xpj"
            src.write_bytes(project_bytes())
            r = self.run_tool(src, "--clear", out, "--program", "Plugin 002")
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            _, _, got = read_back(out)
            self.assertEqual(got, without_maps(doc(), only="Plugin 002"))
            self.assertIn("1 entries", r.stdout)
            self.assertIn("1 tracks", r.stdout)

    def test_unknown_program_is_exit_2_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "in.xpj", Path(d) / "out.xpj"
            src.write_bytes(project_bytes())
            r = self.run_tool(src, "--clear", out, "--program", "No Such")
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertFalse(out.exists())

    def test_never_in_place_and_never_over_an_existing_file(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "in.xpj", Path(d) / "out.xpj"
            src.write_bytes(project_bytes())
            before = src.read_bytes()
            r = self.run_tool(src, "--clear", src)
            self.assertEqual(r.returncode, 2, "the output must not be the input")
            self.assertEqual(src.read_bytes(), before)
            out.write_bytes(b"keep me")
            r = self.run_tool(src, "--clear", out)
            self.assertEqual(r.returncode, 2, "an existing output file is not overwritten without --force")
            self.assertEqual(out.read_bytes(), b"keep me")
            r = self.run_tool(src, "--clear", out, "--force")
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(read_back(out)[2], without_maps(doc()))

    def test_nothing_to_clear(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "in.xpj", Path(d) / "out.xpj"
            src.write_bytes(gzip.compress(HEADER + json.dumps(without_maps(doc()), indent=0).encode()))
            r = self.run_tool(src, "--clear", out)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("nothing to clear", r.stdout)
            self.assertFalse(out.exists(), "no output file when there is nothing to clear")

    def test_the_compare_mode_still_works(self):
        with tempfile.TemporaryDirectory() as d:
            src, q = Path(d) / "in.xpj", Path(d) / "Q-Links.json"
            src.write_bytes(project_bytes())
            q.write_text(json.dumps({"version": 4, "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Row", "Q-Links": {"Q-Link 1": 2703}}]}}))
            r = self.run_tool(src, q)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("differ from the skin", r.stdout)


if __name__ == "__main__":
    unittest.main()
