#!/usr/bin/env python3
# SynthyBoy - tests of the Q-Link frames and the layout of the controls inside them (90_frames.py, 80_layout.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mpc_skin_extras as base  # noqa: E402

FRAME_X = (16, 336, 656, 976)
FRAME_W = 288
BADGE = 40
GRAPHIC_TABS = ("Env 1", "Env 2", "Env 3", "Env 4", "Osc 1", "Osc 2", "Osc 3")
# sha256 of Q-Links.json as the build made it at 363083a (before the frame work): the frame work must not touch it
Q_LINKS_SHA256 = "cbbb2a06f3810640deccfc05a8559fe7842863f1cfc7f69a5bee170aaff74fab"


def rect(component):
    return tuple(int(v) for v in component["bounds"]["bounds"].split())


def frame_rects(tab):
    return [tuple(int(v) for v in s.split()) for s in tab["qlinkBoundsData"]]


def inside_horizontally(r, frame):
    return frame[0] <= r[0] and r[0] + r[2] <= frame[0] + frame[2]


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class FramesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root)
        cls.code, cls.err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        cls.root = root / "pf"
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tabs(self):
        return self.tui["pageData"]["tabs"]

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_four_equal_frames_on_every_tab(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            self.assertEqual([f[0] for f in frames], list(FRAME_X), tab["tabName"])
            self.assertEqual({f[2] for f in frames}, {FRAME_W}, tab["tabName"])
            self.assertEqual(len({(f[1], f[3]) for f in frames}), 1, tab["tabName"])
            if tab["tabName"] in GRAPHIC_TABS:
                self.assertEqual({(f[1], f[3]) for f in frames}, {(301, 323)}, tab["tabName"])
            else:
                self.assertEqual({(f[1], f[3]) for f in frames}, {(0, 624)}, tab["tabName"])

    def test_nothing_sticks_out_of_a_frame(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            for c in tab["componentDefinition"]["componentsData"]:
                r = rect(c)
                if any(inside_horizontally(r, f) for f in frames):
                    continue
                # only a picture across the columns may leave them: it must not touch the frames vertically
                self.assertIn(c["componentData"]["type"], ("Image", "envDisplay1", "envDisplay2", "envDisplay3", "envDisplay4"),
                              "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                self.assertIn(tab["tabName"], GRAPHIC_TABS, c["componentData"]["name"])
                for f in frames:
                    self.assertTrue(r[1] + r[3] <= f[1] or f[1] + f[3] <= r[1], "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

    def test_badge_zone_is_empty(self):
        for tab in self.tabs():
            for f in frame_rects(tab):
                zone = (f[0], f[1], BADGE, BADGE)
                for c in tab["componentDefinition"]["componentsData"]:
                    r = rect(c)
                    hit = r[0] < zone[0] + zone[2] and zone[0] < r[0] + r[2] and r[1] < zone[1] + zone[3] and zone[1] < r[1] + r[3]
                    self.assertFalse(hit, "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

    def test_q_links_json_is_unchanged(self):
        self.assertEqual(hashlib.sha256((self.skins / "Q-Links.json").read_bytes()).hexdigest(), Q_LINKS_SHA256)

    def test_bounds_version_2_has_its_extra_key(self):
        def walk(o):
            if isinstance(o, dict):
                if o.get("version") == 2 and "boundsType" in o:
                    self.assertIn("additionalInvalidatingHandles", o)
                for v in o.values():
                    walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)
        walk(self.tui)

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(self.root)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)


if __name__ == "__main__":
    unittest.main()
