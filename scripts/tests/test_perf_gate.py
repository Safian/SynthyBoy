#!/usr/bin/env python3
# SynthyBoy - tests for the performance regression gate (scripts/perf-gate.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "perf-gate.py"

COLUMNS = ["device", "profile", "block", "sr", "load", "median_pct", "p99_pct", "max_pct", "blocks_over_100pct",
           "rss_kb", "minflt_measured", "majflt", "valid", "invalid_reason"]

# a quiet row: every metric far inside its limit unless a test overrides it
QUIET = {"device": "dev", "profile": "P", "block": "128", "sr": "44100", "load": "L", "median_pct": "10.0", "p99_pct": "20.0",
         "max_pct": "50.0", "blocks_over_100pct": "0", "rss_kb": "50000", "minflt_measured": "0", "majflt": "0", "valid": "1",
         "invalid_reason": ""}


def write_csv(path: Path, **overrides) -> None:
    row = dict(QUIET)
    row.update({k: str(v) for k, v in overrides.items()})
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerow(row)


class PerfGateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def passes(self, tag, metric, values, **common):
        files = []
        for i, v in enumerate(values):
            p = self.tmp / ("%s%d.csv" % (tag, i))
            fields = dict(common)
            fields[metric] = v
            write_csv(p, **fields)
            files.append(str(p))
        return files

    def run_gate(self, *args):
        return subprocess.run([sys.executable, "-I", str(SCRIPT)] + [str(a) for a in args], capture_output=True, text=True)

    def baseline(self, metric, values):
        out = self.tmp / ("baseline_%s.json" % metric)
        r = self.run_gate("baseline", "--out", out, *self.passes("base_" + metric, metric, values))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return out

    def test_p99_limit(self):
        base = self.baseline("p99_pct", [50.0, 51.0, 52.0])   # med 51, spread 2/51, tau 0.078431, limit 55.5
        r = self.run_gate("compare", base, *self.passes("ok", "p99_pct", [55.3, 55.4, 55.5]))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("GATE-PASS 1 keys", r.stdout)
        r = self.run_gate("compare", base, *self.passes("bad", "p99_pct", [55.5, 55.6, 55.7]))
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("GATE-FAIL P | L | p99_pct | base 51.00 | new 55.60 | limit 55.50", r.stdout)
        self.assertIn("GATE-RESULT FAIL 1 violations", r.stdout)

    def test_max_limit(self):
        base = self.baseline("max_pct", [140.0, 150.0, 160.0])   # med 150, spread 20/150, tau 0.2667, limit 192.0
        r = self.run_gate("compare", base, *self.passes("ok", "max_pct", [191.0, 191.0, 191.0]))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = self.run_gate("compare", base, *self.passes("bad", "max_pct", [193.0, 193.0, 193.0]))
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("| max_pct | base 150.00 | new 193.00 | limit 192.00", r.stdout)

    def test_blocks_over_limit(self):
        base = self.baseline("blocks_over_100pct", [0, 0, 0])   # limit 0 * 1.2 + 2 = 2
        r = self.run_gate("compare", base, *self.passes("ok", "blocks_over_100pct", [2, 2, 2]))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = self.run_gate("compare", base, *self.passes("bad", "blocks_over_100pct", [3, 3, 3]))
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("| blocks_over_100pct |", r.stdout)

    def test_majflt_fails(self):
        base = self.baseline("p99_pct", [50.0, 51.0, 52.0])
        files = self.passes("mf", "p99_pct", [51.0, 51.0, 51.0])
        write_csv(Path(files[1]), p99_pct=51.0, majflt=1)
        r = self.run_gate("compare", base, *files)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("| majflt |", r.stdout)

    def test_invalid_row_is_exit_3(self):
        base = self.baseline("p99_pct", [50.0, 51.0, 52.0])
        files = self.passes("inv", "p99_pct", [51.0, 51.0, 51.0])
        write_csv(Path(files[2]), p99_pct=51.0, valid=0, invalid_reason="stall")
        r = self.run_gate("compare", base, *files)
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertIn("GATE-INVALID P | L | stall", r.stdout)

    def test_too_few_files(self):
        base = self.baseline("p99_pct", [50.0, 51.0, 52.0])
        r = self.run_gate("compare", base, *self.passes("few", "p99_pct", [51.0, 51.0]))
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_spread_report(self):
        files = self.passes("sp", "p99_pct", [50.0, 51.0, 52.0])
        r = self.run_gate("spread", *files)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        # median_pct is 10.0 in every pass: gap = 51.00 - 10.00
        self.assertIn("SPREAD P | L | p99_med=51.00 | spread=3.92 % | gap_pts=41.00", r.stdout)


if __name__ == "__main__":
    unittest.main()
