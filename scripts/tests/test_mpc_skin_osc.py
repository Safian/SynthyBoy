#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/20_osc.py (gate version).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mpc_skin_extras as base  # noqa: E402

ROOT = base.ROOT


def png_size(path):
    data = Path(path).read_bytes()
    return struct.unpack(">II", data[16:24])


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class OscGateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root)
        only = root / "only_osc"
        only.mkdir()
        (only / "20_osc.py").symlink_to(ROOT / "scripts" / "mpc-skin-extras" / "20_osc.py")
        code, err = base.run_apply(cls.skins, only)
        assert code == 0, err
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.root = root

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def entries(self, tab):
        t = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == tab)
        return t["componentDefinition"]["componentsData"]

    def test_wave_picture_bound_to_wavetable(self):
        e = self.entries("Osc 1")[-1]
        d = e["componentData"]["data"]
        self.assertEqual((d["imageType"], d["numFrames"], d["image"]), ("FilmStrip", 15, "osc1_wavetable_strip.png"))
        self.assertEqual(e["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 16"}])
        self.assertEqual(e["bounds"]["acceptsHWFocus"], "No")
        self.assertEqual(png_size(self.skins / "osc1_wavetable_strip.png"), (278, 107 * 15))

    def test_frame_strips_conditional(self):
        e0, e1 = self.entries("Osc 2")[-2:]
        self.assertTrue(e0["bounds"]["additionalInvalidatingHandles"][0].startswith("IndexedEnabling/0/15/Parameter "))
        self.assertTrue(e1["bounds"]["additionalInvalidatingHandles"][0].startswith("IndexedEnabling/1/15/Parameter "))
        for e in (e0, e1):
            self.assertEqual(e["bounds"]["version"], 2)
            self.assertEqual(e["componentData"]["data"]["numFrames"], 11)
            self.assertEqual(png_size(self.skins / e["componentData"]["data"]["image"]), (278, 107 * 11))

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, str(base.SDK / "tools" / "validate.py"), str(self.skins.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)
        self.assertIn("OK     TUI.json: image osc1_wavetable_strip.png present", report)

    def test_deterministic(self):
        skins2 = base.generate_skins(self.root / "again")
        only = self.root / "only_osc"
        self.assertEqual(base.run_apply(skins2, only)[0], 0)
        for name in ("TUI.json", "osc1_wavetable_strip.png", "osc2_t0_frame_strip.png", "osc2_t1_frame_strip.png"):
            self.assertEqual((skins2 / name).read_bytes(), (self.skins / name).read_bytes(), name)


if __name__ == "__main__":
    unittest.main()
