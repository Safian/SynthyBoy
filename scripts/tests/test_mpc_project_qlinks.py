#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-project-qlinks.py: the Q-Link snapshot a project keeps per plugin track and the comparison with a skin's Q-Links.json.
# The shape of the project file (gzip, five header lines, JSON; tracks/N/program/customisable/mapping with "parameterIndex": "tab subtab qlink" 0-based) was read from a project of the Key 61.
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


def skin(entries):
    """entries: {(tab, sub): {qlink: parameter}} -> a Q-Links.json structure with all 16 keys per entry."""
    m = []
    for (t, s), q in sorted(entries.items()):
        m.append({"Tab": t, "SubTab": s, "Bank Direction": "Row", "Q-Links": {"Q-Link %d" % i: q.get(i, -1) for i in range(1, 17)}})
    return {"version": 4, "Screen Mode Q-Links": {"version": 4, "map": m}}


def project(snapshot):
    """snapshot: {(tab, sub, qlink): parameter} -> the bytes of a project with one plugin track holding it (and one track without a map)."""
    mapping = [{"parameterIndex": "%d %d %d" % (t - 1, s - 1, q - 1), "automationIndex": a, "value": 0.0, "name": "p%d" % a} for (t, s, q), a in sorted(snapshot.items())]
    doc = {"data": {"tracks": [{"program": {"name": "Plugin 001", "customisable": {"mapping": mapping}}}, {"program": {"name": "Drums", "customisable": {"mapping": []}}}]}}
    return gzip.compress(b"ACVS\n3.10.1.12\nSerialisableProjectData\njson\nLinux\n" + json.dumps(doc, indent=1).encode())


class ProjectQLinks(unittest.TestCase):
    def run_tool(self, snap, q):
        with tempfile.TemporaryDirectory() as d:
            p, j = Path(d) / "t.xpj", Path(d) / "Q-Links.json"
            p.write_bytes(project(snap))
            j.write_text(json.dumps(q))
            return subprocess.run([sys.executable, str(TOOL), str(p), str(j)], capture_output=True, text=True)

    def test_snapshot_of_the_same_skin_agrees(self):
        q = skin({(1, 1): {1: 2703, 2: 2704, 3: 2705, 4: 2706}, (2, 1): {1: 2703, 2: 2704, 3: 2705, 4: 2706}})
        r = self.run_tool(mod.skin_map(q), q)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("the same as the skin", r.stdout)

    def test_snapshot_of_an_older_skin_is_stale(self):
        """The case measured on the Key 61: the project's track holds the first skin's map (Macros tab = Tab 2 held parameters 24, 19, 20 on its Q-Links 1 .. 3), the skin now puts the four Q proxies there."""
        old = skin({(2, 1): {1: 24, 2: 19, 3: 20, 5: 22, 6: 23, 7: 25, 8: 26}})
        new = skin({(2, 1): {1: 2703, 2: 2704, 3: 2705, 4: 2706}})
        r = self.run_tool(mod.skin_map(old), new)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("differ from the skin", r.stdout)

    def test_compare_and_parsing(self):
        tr = mod.track_maps(mod.parse_project(project({(3, 2, 5): 77})))
        self.assertEqual(len(tr), 1)
        self.assertEqual(tr[0][2], {(3, 2, 5): 77})
        self.assertEqual(mod.compare({(1, 1, 1): 5}, {(1, 1, 1): 5}), {})
        self.assertEqual(mod.compare({(1, 1, 1): 5}, {(1, 1, 1): 6}), {(1, 1, 1): (5, 6)})
        self.assertEqual(mod.compare({}, {(1, 1, 1): 6}), {(1, 1, 1): (None, 6)})

    def test_unreadable_input_is_exit_2(self):
        r = subprocess.run([sys.executable, str(TOOL), "/nonexistent.xpj", "/nonexistent.json"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
