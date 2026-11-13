#!/usr/bin/env python3
# SynthyBoy - test of scripts/make-spectrum-gate-package.py: the gate package has the skin and both binaries; --single-meter leaves one vertical Meter on "LFO 1 Cursor".
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, ROOT, SDK, generate_skins, run_apply

SCRIPT = ROOT / "scripts" / "make-spectrum-gate-package.py"


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class GatePackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls._tmp.name)
        skins = generate_skins(cls.root)
        code, err = run_apply(skins, REAL_EXTRAS)
        assert (code, err) == (0, ""), err
        cls.package = cls.root / "out" / "Safian - VST - SynthyBoy"
        (cls.package / "VST").mkdir(parents=True)
        (cls.package / "VST" / "synthyboy-arm32.so").write_bytes(b"32")
        (cls.package / "VST" / "synthyboy-arm64.so").write_bytes(b"64")
        (cls.package / "Plugin Skins").symlink_to(skins)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def make(self, name, *extra):
        out = self.root / name
        r = subprocess.run([sys.executable, str(SCRIPT), "--package", str(self.package), "--out", str(out), *extra], capture_output=True, text=True)
        self.assertEqual((r.returncode, r.stderr), (0, ""))
        return out

    def meters(self, out):
        tui = json.loads((out / "Plugin Skins" / "TUI.json").read_text(encoding="utf-8"))
        tab = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == "Macros")
        return [c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Meter"]

    def test_full_package(self):
        out = self.make("full")
        self.assertEqual(sorted(p.name for p in out.iterdir()), ["Plugin Skins", "synthyboy-arm32.so", "synthyboy-arm64.so"])
        self.assertEqual((out / "synthyboy-arm64.so").read_bytes(), b"64")
        self.assertEqual(len(self.meters(out)), 32)
        self.assertTrue((out / "Plugin Skins" / "spectrum_on.png").is_file())

    def test_single_meter_is_bound_to_the_first_cursor(self):
        out = self.make("single", "--single-meter")
        meters = self.meters(out)
        self.assertEqual(len(meters), 1)
        count = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["count"]
        handle = "Parameter %d" % count
        d = meters[0]["componentData"]["data"]
        self.assertEqual((d["direction"], d["peakHandle"], d["peakHoldHandle"]), ("Up", handle, "Hold/" + handle))
        self.assertEqual(meters[0]["handle remapping"]["map"], [{"key": "Data", "value": handle}])
        self.assertEqual(meters[0]["bounds"]["bounds"], "36 480 30 136")

    def test_missing_binary_is_an_error(self):
        (self.package / "VST" / "synthyboy-arm32.so").rename(self.package / "VST" / "x.so")
        try:
            r = subprocess.run([sys.executable, str(SCRIPT), "--package", str(self.package), "--out", str(self.root / "bad")], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
        finally:
            (self.package / "VST" / "x.so").rename(self.package / "VST" / "synthyboy-arm32.so")


if __name__ == "__main__":
    unittest.main()
