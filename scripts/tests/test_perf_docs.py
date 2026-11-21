#!/usr/bin/env python3
# SynthyBoy - consistency test for performance documentation (docs/plans/perf-measurement.md, task T18).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TIER_BENCH_CPP = ROOT / "bench" / "tier_bench.cpp"
PERF_README = ROOT / "docs" / "perf" / "README.md"
PERFORMANCE_MD = ROOT / "docs" / "PERFORMANCE.md"


def parse_tier_bench_long_options(source_path: Path) -> list[str]:
    """Parse every long option of tier_bench.cpp's argument parser."""
    text = source_path.read_text(encoding="utf-8")
    return re.findall(r'a\s*==\s*"(--[a-z0-9-]+)"', text)


class TestPerfDocs(unittest.TestCase):
    def test_tier_bench_options_documented(self):
        self.assertTrue(TIER_BENCH_CPP.is_file(), f"missing {TIER_BENCH_CPP}")
        options = parse_tier_bench_long_options(TIER_BENCH_CPP)
        self.assertGreaterEqual(len(options), 35, f"parsed too few options: {len(options)}")

        # Verify key options specifically named in plan task T18
        required_options = [
            "--no-aa",
            "--rt",
            "--pmu",
            "--modules",
            "--evict",
            "--first-blocks",
            "--rt-gap",
            "--rt-require",
            "--mlock",
            "--no-helpers-away",
            "--retry-invalid",
            "--pmu-require",
            "--module-group",
            "--module-reps",
        ]
        for opt in required_options:
            self.assertIn(opt, options, f"expected {opt} to be parsed from {TIER_BENCH_CPP}")

        self.assertTrue(PERF_README.is_file(), f"{PERF_README} does not exist")
        readme_text = PERF_README.read_text(encoding="utf-8")

        missing = [opt for opt in options if opt not in readme_text]
        self.assertEqual(missing, [], f"options missing from {PERF_README}: {missing}")

    def test_load_syntax_documented(self):
        self.assertTrue(PERF_README.is_file(), f"{PERF_README} does not exist")
        readme_text = PERF_README.read_text(encoding="utf-8")
        self.assertIn("dist:", readme_text, "dist: load syntax missing from docs/perf/README.md")
        self.assertIn("warp:", readme_text, "warp: load syntax missing from docs/perf/README.md")

    def test_scripts_and_tools_documented(self):
        self.assertTrue(PERF_README.is_file(), f"{PERF_README} does not exist")
        readme_text = PERF_README.read_text(encoding="utf-8")
        for tool in ("scripts/perf-device.sh", "scripts/perf-tables.py", "scripts/build-tier-bench.sh", "synthyboy_pmu_probe"):
            self.assertIn(tool, readme_text, f"{tool} must be documented in docs/perf/README.md")

    def test_protocol_required_strings(self):
        self.assertTrue(PERF_README.is_file(), f"{PERF_README} does not exist")
        readme_text = PERF_README.read_text(encoding="utf-8")
        for req in ("SET12", "GATE-INVALID", "97.9 %"):
            self.assertIn(req, readme_text, f"required string {req!r} missing from docs/perf/README.md")

    def test_performance_md_updated(self):
        self.assertTrue(PERFORMANCE_MD.is_file(), f"{PERFORMANCE_MD} does not exist")
        perf_text = PERFORMANCE_MD.read_text(encoding="utf-8")
        self.assertNotIn("Apple M5 Pro", perf_text, "Apple M5 Pro must no longer appear in docs/PERFORMANCE.md")
        self.assertTrue(
            "docs/perf/README.md" in perf_text or "perf/README.md" in perf_text,
            "docs/PERFORMANCE.md must link to docs/perf/README.md",
        )
        self.assertIn("synthyboy_bench", perf_text)
        self.assertIn("44.1 kHz", perf_text)
        self.assertIn("flush-to-zero", perf_text)


if __name__ == "__main__":
    unittest.main()
