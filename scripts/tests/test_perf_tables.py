#!/usr/bin/env python3
# SynthyBoy - tests for the section and module tables of tier_bench output (scripts/perf-tables.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "perf-tables.py"

PROFILE = "high,voices=16,control=economy"
HEADER = "synthyboy_tier_bench: 44100 Hz, block 128 frames = 2.902 ms, 10.0 s measured per run after 1 s of warm-up"


def result_row(profile, load):
    # the C format of bench/tier_bench.cpp:1076: "%-58s %-24.24s %5d %6.1f | %7.2f %7.2f %7.2f %7.2f %7.2f | %5lld %8ld %4d"
    return "%-58s %-24.24s %5d %6.1f | %7.2f %7.2f %7.2f %7.2f %7.2f | %5d %8d %4d" % (profile, load, 8, 8.0, 5.0, 10.0, 12.0, 15.0, 10.5, 0, 1608000, 1)


def section_line(name, ms, share, calls, reads, less_ms, less_share):
    # the C format of bench/tier_bench.cpp:1096
    return "    section %-52s %8.4f ms / block %6.1f %%  %7.1f calls / block %7.1f clock reads  less clock %8.4f ms %6.1f %%" % (name, ms, share, calls, reads, less_ms, less_share)


def run_tables(mode, text):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), mode], input=text, capture_output=True, text=True)


class PerfTablesTest(unittest.TestCase):
    def test_section_row(self):
        text = "\n".join([HEADER, result_row(PROFILE, "basic"), section_line("filter1", 0.1234, 10.0, 8.0, 16.0, 0.2902, 9.0), ""])
        r = run_tables("sections", text)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("| filter1 | 0.1234 | 0.2902 | 10.00 |", r.stdout.splitlines())   # 0.2902 / 2.90249 = 0.09998
        self.assertIn("| section | ms/block | ms/block less clock | % of block |", r.stdout.splitlines())
        self.assertIn(PROFILE, r.stdout)
        self.assertIn("basic", r.stdout)

    def test_two_loads_two_tables(self):
        text = "\n".join([result_row(PROFILE, "basic"), section_line("filter1", 0.1, 1.0, 1.0, 2.0, 0.05, 1.0),
                          result_row(PROFILE, "chord"), section_line("filter2", 0.2, 1.0, 1.0, 2.0, 0.10, 1.0), ""])
        r = run_tables("sections", text)
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.splitlines()
        self.assertEqual(sum(1 for l in lines if l.startswith("| section |")), 2)
        self.assertIn("| filter2 | 0.2000 | 0.1000 | 3.45 |", lines)   # 0.10 / 2.902 (the block from the default 128 / 44100 without a header)

    def test_module_rows(self):
        text = "\n".join([
            "module fx | Chorus | base 812.40 us | case 934.15 us | cost +121.75 us | +4.19 % of block | reps 3 | spread 2.1 %",
            "module fx | Reverb | base 812.40 us | case 1500.00 us | cost +687.60 us | +23.69 % of block | reps 3 | spread 1.0 %",
            "module filter | Ladder | base 812.40 us | case 850.00 us | cost +37.60 us | +1.30 % of block | reps 3 | spread 4.0 %",
            "module-table: 3 cases", ""])
        r = run_tables("modules", text)
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.splitlines()
        self.assertIn("| fx | Chorus | 121.75 | 4.19 | 2.1 |", lines)
        self.assertLess(lines.index("| fx | Reverb | 687.60 | 23.69 | 1.0 |"), lines.index("| fx | Chorus | 121.75 | 4.19 | 2.1 |"))   # cost descending inside a group
        self.assertLess(lines.index("| filter | Ladder | 37.60 | 1.30 | 4.0 |"), lines.index("| fx | Reverb | 687.60 | 23.69 | 1.0 |"))   # then by group


if __name__ == "__main__":
    unittest.main()
