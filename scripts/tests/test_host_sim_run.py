#!/usr/bin/env python3
# SynthyBoy - test of shell-mpc/host-sim-run.sh: a failed state round trip of display parameters is accepted, everything else that host_sim fails is not.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
RUN = ROOT / "shell-mpc" / "host-sim-run.sh"

FLAKE = '''PASS  something
FAIL  param 2638 "LFO 2 Cursor": saved 0.03125 but after effSetChunk it is 0.84375 ("27/32")
FAIL  param 2650 "Spectrum 10": saved 0.50000 but after effSetChunk it is 0.25000 ("16/64")
FAIL  state round-trip: 2 parameter(s) did not come back from the saved chunk
Result: FAIL  (18 passed, 1 warnings, 3 failures)
'''


class HostSimRunTest(unittest.TestCase):
    def run_with(self, output, code=1, n=2637):
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / "host_sim"
            fake.write_text("#!/bin/sh\ncat <<'EOF'\n%sEOF\nexit %d\n" % (output, code))
            fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
            r = subprocess.run(["sh", str(RUN), str(fake), "lib.so", str(n), str(Path(tmp) / "log"), "test"], capture_output=True, text=True)
            return r.returncode

    def test_success_passes(self):
        self.assertEqual(self.run_with("PASS  all\nResult: PASS  (1 passed, 0 warnings, 0 failures)\n", 0), 0)

    def test_display_parameters_only_is_accepted(self):
        self.assertEqual(self.run_with(FLAKE), 0)

    def test_engine_parameter_is_not_accepted(self):
        bad = FLAKE.replace('param 2638 "LFO 2 Cursor"', 'param 12 "Master Volume"')
        self.assertEqual(self.run_with(bad), 1)

    def test_display_name_on_an_engine_index_is_not_accepted(self):
        bad = FLAKE.replace("param 2638", "param 100")
        self.assertEqual(self.run_with(bad), 1)

    def test_another_check_failing_is_not_accepted(self):
        self.assertEqual(self.run_with(FLAKE.replace("PASS  something", "FAIL  no NaN/Inf: found 3").replace("3 failures", "4 failures")), 1)

    def test_uncounted_failure_is_not_accepted(self):
        self.assertEqual(self.run_with(FLAKE.replace("3 failures", "4 failures")), 1)

    def test_nonzero_exit_without_fail_lines_is_not_accepted(self):
        self.assertEqual(self.run_with("PASS  x\nResult: FAIL  (1 passed, 0 warnings, 0 failures)\n"), 1)


if __name__ == "__main__":
    unittest.main()
