#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/40_spectrum.py (32 Meter columns for the live spectrum in the bottom row of the Macros tab).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, ROOT, SDK, generate_skins, run_apply

METER_KEYS = ["version", "direction", "invert", "inactiveImage", "rmsImage", "rmsHandle", "peakImage", "peakHandle", "peakHoldHandle", "peakHoldSize", "peakHoldColour"]
COLUMNS, COLUMN_W, GAP, Y, COLUMN_H = 32, 30, 8, 480, 136
FRAME_X = (16, 336, 656, 976)


def rect(c):
    return tuple(int(v) for v in c["bounds"]["bounds"].split())


def png_size(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    return struct.unpack(">II", data[16:24])


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class SpectrumSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = generate_skins(root)
        cls.before = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.code, cls.err = run_apply(cls.skins, REAL_EXTRAS)
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, tui=None):
        return next(t for t in (tui or self.tui)["pageData"]["tabs"] if t["tabName"] == "Macros")

    def meters(self):
        return [c for c in self.tab()["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Meter"]

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_32_meters_up_with_the_filterbank_key_set(self):
        meters = self.meters()
        self.assertEqual(len(meters), COLUMNS)
        for k, c in enumerate(meters):
            d = c["componentData"]
            self.assertEqual(d["name"], "Spectrum %d" % (k + 1))
            self.assertEqual(list(d["data"].keys()), METER_KEYS)
            self.assertEqual(d["data"]["version"], 3)
            self.assertEqual(d["data"]["direction"], "Up")
            self.assertIs(d["data"]["invert"], False)
            self.assertEqual((d["data"]["inactiveImage"], d["data"]["peakImage"]), ("spectrum_off.png", "spectrum_on.png"))
            self.assertEqual((d["data"]["rmsImage"], d["data"]["rmsHandle"]), ("", "Invalid"))
            self.assertEqual((d["data"]["peakHoldSize"], d["data"]["peakHoldColour"]), (0, "00ffffff"))

    def test_handles_are_the_display_parameters_after_the_cursors(self):
        first = len(self.params) + 4
        self.assertEqual(len(self.params), 2637)
        for k, c in enumerate(self.meters()):
            handle = "Parameter %d" % (first + k)
            d = c["componentData"]["data"]
            self.assertEqual((d["peakHandle"], d["peakHoldHandle"]), (handle, "Hold/" + handle))
            self.assertEqual(c["handle remapping"]["map"], [{"key": "Data", "value": handle}])

    def test_geometry_32_columns_centred_in_the_bottom_row(self):
        rects = [rect(c) for c in self.meters()]
        self.assertEqual({r[2:] for r in rects}, {(COLUMN_W, COLUMN_H)})
        self.assertEqual({r[1] for r in rects}, {Y})
        xs = [r[0] for r in rects]
        self.assertEqual([b - a for a, b in zip(xs, xs[1:])], [COLUMN_W + GAP] * (COLUMNS - 1))
        left, right = xs[0], 1280 - (xs[-1] + COLUMN_W)
        self.assertEqual(left, right)
        self.assertEqual(xs[0], 36)
        self.assertLessEqual(Y + COLUMN_H, 628)   # inside the tab
        for c in self.meters():
            self.assertEqual(c["bounds"]["acceptsHWFocus"], "No")

    def test_meters_are_below_the_frames_and_the_controls(self):
        tab = self.tab()
        frames = [tuple(int(v) for v in s.split()) for s in tab["qlinkBoundsData"]]
        self.assertEqual([f[0] for f in frames], list(FRAME_X))
        self.assertEqual({(f[1], f[3]) for f in frames}, {(0, 474)})
        slots = [rect(c) for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"] != "Meter"]
        self.assertEqual(max(r[1] + r[3] for r in slots), 470)        # rows 1 to 3 end at y 470: the frames cover them and a little more
        for f in frames:
            self.assertLessEqual(f[1] + f[3], Y)                       # a picture across the columns must not touch the frames
        for c in self.meters():
            r = rect(c)
            for s in slots:
                self.assertFalse(r[0] < s[0] + s[2] and s[0] < r[0] + r[2] and r[1] < s[1] + s[3] and s[1] < r[1] + r[3])

    def test_the_controls_of_the_tab_are_unchanged(self):
        # the names of the controls are texts that 85_names.py adds: compare the controls themselves
        before = [c for c in self.tab(self.before)["componentDefinition"]["componentsData"] if c["componentData"]["type"] != "Label"]
        after = [c for c in self.tab()["componentDefinition"]["componentsData"] if c["componentData"]["type"] not in ("Meter", "Label")]
        self.assertEqual([c["componentData"]["name"] for c in after], [c["componentData"]["name"] for c in before])
        self.assertEqual([c["handle remapping"] for c in after], [c["handle remapping"] for c in before])

    def test_other_tabs_have_no_meters(self):
        for tab in self.tui["pageData"]["tabs"]:
            if tab["tabName"] != "Macros":
                self.assertEqual([c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Meter"], [], tab["tabName"])

    def test_pictures_are_in_the_skin_with_the_size_of_the_columns(self):
        for name in ("spectrum_off.png", "spectrum_on.png"):
            self.assertEqual(png_size(self.skins / name), (COLUMN_W, COLUMN_H))
        self.assertEqual(2 * COLUMN_W * COLUMN_H * 4, 32640)   # decoded: negligible (the skin's other pictures are 105 MB)

    def test_the_shell_has_the_parameters_the_skin_refers_to(self):
        src = (ROOT / "shell-mpc" / "synthyboy_mpc.cpp").read_text(encoding="utf-8")
        self.assertIn('"Spectrum %d"', src)
        self.assertIn("kSpectrumCount", src)
        header = (ROOT / "shell-mpc" / "spectrum.h").read_text(encoding="utf-8")
        self.assertIn("kSpectrumCount = kSpectrumBands", header)
        bands = (ROOT / "shell-mpc" / "spectrum_bands.h").read_text(encoding="utf-8")
        self.assertRegex(bands, r"kSpectrumBands\s*=\s*32\s*;")
        self.assertRegex((ROOT / "shell-mpc" / "lfo_live.h").read_text(encoding="utf-8"), r"kLfoLiveCount\s*=\s*4\s*;")   # the 4 before the columns
        cmake = (ROOT / "shell-mpc" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertRegex(cmake, r'option\(SYNTHYBOY_MPC_SPECTRUM [^\n]*\bON\)')

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, str(SDK / "tools" / "validate.py"), str(self.skins.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "34 tabs; at most 32" not in l]
        self.assertEqual(errors, [], report)
        # the validator does not look at the pictures of a Meter: the test does
        for c in self.meters():
            for key in ("inactiveImage", "peakImage"):
                self.assertTrue((self.skins / c["componentData"]["data"][key]).is_file(), c["componentData"]["data"][key])


if __name__ == "__main__":
    unittest.main()
