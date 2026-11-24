#!/usr/bin/env python3
# SynthyBoy - tests of scripts/build-tier-bench.sh: the variants (plain, --sections, --dynamic) as the --dry-run lists them. No Docker is needed or touched.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "build-tier-bench.sh"
TARGETS = "synthyboy_tier_bench,synthyboy_libm_probe,synthyboy_pmu_probe"


def dry_run(*args):
    return subprocess.run(["bash", str(SCRIPT), "--dry-run", *args, "--out", "/tmp/x"], capture_output=True, text=True, timeout=60)


def line(arch, variant, static, libcxx, sections):
    return ("DRY-RUN arch=%s variant=%s static=%s libcxx_static=%s section_timing=%s targets=%s out=/tmp/x/tier-bench-%s%s"
            % (arch, variant, static, libcxx, sections, TARGETS, arch, variant))


class BuildTierBenchDryRunTest(unittest.TestCase):
    def test_three_variants_of_one_arch(self):
        r = dry_run("--arch", "arm32", "--sections", "--dynamic")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.splitlines(), [
            line("arm32", "", "ON", "OFF", "OFF"),
            line("arm32", "-sections", "ON", "OFF", "ON"),
            line("arm32", "-dyn", "OFF", "ON", "OFF"),
        ])

    def test_both_architectures(self):
        r = dry_run("--arch", "all", "--sections", "--dynamic")
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.splitlines()
        self.assertEqual(len(lines), 6)
        self.assertEqual([l.split()[1] for l in lines], ["arch=arm32"] * 3 + ["arch=arm64"] * 3)
        self.assertEqual(lines[3], line("arm64", "", "ON", "OFF", "OFF"))

    def test_plain_only(self):
        r = dry_run("--arch", "arm64")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.splitlines(), [line("arm64", "", "ON", "OFF", "OFF")])


if __name__ == "__main__":
    unittest.main()
