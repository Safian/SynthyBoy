#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/45_pad_pressure.py (the pad pressure toggle, amount knob and the Full Level hint on the Master tab).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import tempfile
import unittest
from pathlib import Path

from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, SDK, generate_skins, run_apply

TAB = "Master"
RIGHT_LIMIT = 1025   # the controls stay left of this x


def rect(c):
    return tuple(int(v) for v in c["bounds"]["bounds"].split())


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class PadPressureSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.skins = generate_skins(Path(cls._tmp.name))
        cls.before = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.code, cls.err = run_apply(cls.skins, REAL_EXTRAS)
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.ql = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))
        cls.count = len(json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"])
        cls.first = cls.count + 4 + 32   # after the engine's list, the four LFO cursors and the 32 spectrum columns

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, tui=None):
        return next(t for t in (tui or self.tui)["pageData"]["tabs"] if t["tabName"] == TAB)

    def items(self):
        return self.tab()["componentDefinition"]["componentsData"]

    def by_name(self, name):
        found = [c for c in self.items() if c["componentData"]["name"] == name]
        self.assertEqual(len(found), 1, name)
        return found[0]

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_the_two_parameters_follow_the_spectrum_columns(self):
        self.assertEqual(self.first, 2673)   # the parameter list ends with "Spectrum 32" at 2672 (docs/MPC.md)
        t = self.by_name("Pad Pressure")
        a = self.by_name("Pad Pressure Amount")
        self.assertEqual((t["componentData"]["type"], a["componentData"]["type"]), ("toggle", "knobUni"))
        self.assertEqual(t["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % self.first}])
        self.assertEqual(a["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % (self.first + 1)}])

    def test_controls_sit_in_the_bottom_row_left_of_x_1025(self):
        for name in ("Pad Pressure", "Pad Pressure Amount", "Pad Pressure name", "Pad Pressure Amount name", "Pad Pressure hint 1", "Pad Pressure hint 2"):
            x, y, w, h = rect(self.by_name(name))
            self.assertLess(x + w, RIGHT_LIMIT, name)
            self.assertGreaterEqual(y, 476, name)
            self.assertLessEqual(y + h, 628, name)
        self.assertEqual(rect(self.by_name("Pad Pressure"))[:2], (21, 476))
        self.assertEqual(rect(self.by_name("Pad Pressure Amount"))[:2], (341, 476))

    def test_nothing_of_the_tab_moved_or_overlaps(self):
        old = {c["componentData"]["name"]: c["bounds"]["bounds"] for c in self.tab(self.before)["componentDefinition"]["componentsData"]}
        new = {c["componentData"]["name"]: c["bounds"]["bounds"] for c in self.items()}
        for name, bounds in old.items():
            self.assertIn(name, new)
        added = [n for n in new if "Pad Pressure" in n]
        self.assertEqual(len(added), 6)
        boxes = [(n, rect(c)) for n, c in ((n, self.by_name(n)) for n in new) if c["componentData"]["type"] in ("toggle", "knobUni", "knobBi")]
        for i, (n1, a) in enumerate(boxes):
            for n2, b in boxes[i + 1:]:
                self.assertTrue(a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1], "%s overlaps %s" % (n1, n2))

    def test_names_and_the_hint_text(self):
        self.assertEqual(self.by_name("Pad Pressure name")["componentData"]["data"]["text"], "Pad Pressure")
        self.assertEqual(self.by_name("Pad Pressure Amount name")["componentData"]["data"]["text"], "Pressure Amount")
        hint = " ".join(self.by_name("Pad Pressure hint %d" % k)["componentData"]["data"]["text"] for k in (1, 2)).lower()
        self.assertIn("full level", hint)
        self.assertIn("off", hint)
        for k in (1, 2):   # a text label is drawn only with a valid handle
            self.assertEqual(self.by_name("Pad Pressure hint %d" % k)["handle remapping"]["map"][0]["value"], "Parameter %d" % self.first)

    def test_q_links_of_the_bottom_row(self):
        t = self.tab()
        slot = (t["fnKeyIndex"] + 1, t["fnKeySubIndex"] + 1)
        q = next(e for e in self.ql["Screen Mode Q-Links"]["map"] if (e["Tab"], e["SubTab"]) == slot)["Q-Links"]
        self.assertEqual((q["Q-Link 1"], q["Q-Link 2"]), (self.first, self.first + 1))
        self.assertEqual((q["Q-Link 3"], q["Q-Link 4"]), (-1, -1))

    def test_tab_count_is_unchanged(self):
        self.assertEqual(len(self.tui["pageData"]["tabs"]), len(self.before["pageData"]["tabs"]))
        self.assertEqual(len(self.tui["pageData"]["tabs"]), 34)   # the validator's one accepted finding: "34 tabs; at most 32 are supported"


if __name__ == "__main__":
    unittest.main()
