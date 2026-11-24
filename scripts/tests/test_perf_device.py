#!/usr/bin/env python3
# SynthyBoy - tests of scripts/perf-device.sh, the runner of the device measurements. Only --dry-run is used: nothing is executed, no ssh, no process is touched.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "perf-device.sh"


def dry(*args):
    return subprocess.run(["bash", str(SCRIPT), "--dry-run", *args], capture_output=True, text=True, timeout=60)


class PerfDeviceDryRunTest(unittest.TestCase):
    def test_probe_k61(self):
        r = dry("--device", "k61", "--phase", "probe")
        self.assertEqual(r.returncode, 0, r.stderr)
        for needle in ("root@192.168.0.30", "cat /sys/devices/system/cpu/isolated", "systemctl show -p MainPID acvs", "perf_event_paranoid"):
            self.assertIn(needle, r.stdout)
        for line in r.stdout.splitlines():
            self.assertTrue(line.startswith("+"), "a line of the dry run that is not a command: " + line)

    def test_ab_xl(self):
        r = dry("--device", "xl", "--phase", "ab", "--sequence", "ABBA", "--rt", "20")
        self.assertEqual(r.returncode, 0, r.stderr)
        for needle in ("root@192.168.0.126", "--core 6", '--config "high,voices=16,control=economy"', "--rt 20 --rt-require"):
            self.assertIn(needle, r.stdout)
        ab = [line for line in r.stdout.splitlines() if "perf-ab.py --sequence ABBA" in line]
        self.assertEqual(len(ab), 1, r.stdout)
        for name in ("pass-1-A.csv", "pass-2-B.csv", "pass-3-B.csv", "pass-4-A.csv"):
            self.assertIn(name, ab[0])

    def test_module_table_requires_realtime_scheduling(self):
        for device in ("k61", "xl"):
            with self.subTest(device=device):
                result = dry("--device", device, "--phase", "modules")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("--rt 20 --rt-require --modules", result.stdout)

    def test_e1_needs_the_flag_and_not_on_xl(self):
        r = dry("--device", "k61", "--phase", "e1")
        self.assertEqual(r.returncode, 2)
        self.assertNotIn("kill -STOP", r.stdout)
        r = dry("--device", "k61", "--phase", "e1", "--i-know-the-host-stops")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("kill -STOP", r.stdout)
        self.assertIn("kill -CONT", r.stdout)
        r = dry("--device", "xl", "--phase", "e1", "--i-know-the-host-stops")
        self.assertEqual(r.returncode, 2)
        self.assertNotIn("kill -STOP", r.stdout)

    def test_unknown_phase_and_device(self):
        self.assertEqual(dry("--device", "k61", "--phase", "nonsense").returncode, 2)
        self.assertEqual(dry("--device", "nope", "--phase", "probe").returncode, 2)

    def test_cleanup_and_host_override(self):
        r = dry("--device", "k61", "--host", "10.1.2.3", "--phase", "cleanup")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("root@10.1.2.3", r.stdout)
        self.assertIn("rm -rf /tmp/sbperf", r.stdout)

    def test_default_interleaved_comparison(self):
        """Verify the default ab phase executes baseline, candidate, baseline in order within one session."""
        r = dry("--device", "k61", "--phase", "ab")
        self.assertEqual(r.returncode, 0, r.stderr)
        sequence = "ABBA" * 5
        executions = re.findall(r"\./(static|dyn)/synthyboy_tier_bench .*?pass-(\d+)-([AB])\.csv", r.stdout)
        expected = [("static" if side == "A" else "dyn", str(i), side)
                    for i, side in enumerate(sequence, start=1)]
        self.assertEqual(executions, expected)
        analysis, = [line for line in r.stdout.splitlines() if "perf-ab.py" in line]
        self.assertIn("--sequence " + sequence, analysis)
        self.assertEqual(re.findall(r"pass-(\d+)-([AB])\.csv", analysis),
                         [(i, side) for _, i, side in expected])


if __name__ == "__main__":
    unittest.main()
