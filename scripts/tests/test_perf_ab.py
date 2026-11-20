#!/usr/bin/env python3
# SynthyBoy - tests for the interleaved A/B analysis (scripts/perf-ab.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "perf-ab.py"

COLUMNS = ["profile", "load", "median_pct", "p99_pct", "max_pct", "blocks_over_100pct"]


def write_csv(path: Path, median_pct: float) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(COLUMNS)
        w.writerow(["P", "L", "%.6f" % median_pct, "%.6f" % (median_pct * 2.0), "%.6f" % (median_pct * 3.0), "0"])


class PerfAbTest(unittest.TestCase):
    def run_ab(self, rhos, sequence, extra=None):
        """One file per label of the sequence; the pair k (files 2k, 2k+1) has A = 100.0 and B = 100.0 * rhos[k]."""
        with tempfile.TemporaryDirectory() as tmp:
            files = []
            for i, label in enumerate(sequence):
                pair = i // 2
                rho = rhos[pair] if pair < len(rhos) else 1.0
                p = Path(tmp) / ("pass%02d.csv" % i)
                write_csv(p, 100.0 if label == "A" else round(100.0 * rho, 6))
                files.append(str(p))
            cmd = [sys.executable, "-I", str(SCRIPT), "--metric", "median_pct", "--sequence", sequence] + (extra or []) + files
            return subprocess.run(cmd, capture_output=True, text=True)

    def test_gain(self):
        rhos = [0.90, 0.91, 0.92, 0.93, 0.94, 0.95, 0.96, 0.97, 0.98, 0.99]
        r = self.run_ab(rhos, "ABBA" * 5)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("AB-VERDICT P | L | K=10 | median_ratio=0.9450 | ci=[0.9100, 0.9800] | GAIN", r.stdout.splitlines())
        self.assertIn("AB-SUMMARY gain=1 regression=0 unproven=0 geomean_ratio=0.9450", r.stdout.splitlines())

    def test_regression(self):
        rhos = [1.01, 1.02, 1.03, 1.04, 1.05, 1.06, 1.07, 1.08, 1.09, 1.10]
        r = self.run_ab(rhos, "ABBA" * 5)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("ci=[1.0200, 1.0900] | REGRESSION", r.stdout)
        r = self.run_ab(rhos, "ABBA" * 5, ["--fail-on-regression"])
        self.assertEqual(r.returncode, 1, r.stderr)

    def test_no_change_proven(self):
        rhos = [0.97, 0.98, 0.98, 0.99, 0.99, 1.00, 1.00, 1.00, 1.01, 1.02]
        r = self.run_ab(rhos, "ABBA" * 5)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("ci=[0.9800, 1.0100] | NO-CHANGE-PROVEN", r.stdout)

    def test_too_few_pairs(self):
        r = self.run_ab([0.9] * 5, "ABBAABBAAB")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_pair_with_equal_labels(self):
        r = self.run_ab([0.9] * 10, "AABB" * 5)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
