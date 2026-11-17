#!/usr/bin/env python3
# SynthyBoy - tests of scripts/propose-pad-pressure.py (which factory presets carry a Poly Aftertouch -> Filter 1 Cutoff route) and of the route in the presets.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
spec = importlib.util.spec_from_file_location("propose_pad_pressure", ROOT / "scripts" / "propose-pad-pressure.py")
pp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pp)
DOC = json.loads(pp.PARAMS_JSON.read_text(encoding="utf-8"))
SRC = {s["key"]: s["choice"] for s in DOC["modSources"]}
DST = {d["key"]: d["choice"] for d in DOC["modDestinations"]}


def one(category, name="X", already=False, **p):
    params = {"filter1.on": 1, "env1.sustain": 100, "env1.attack": 5}
    params.update(p)
    return pp.propose_one(category, params, name, already)[0]


class Rules(unittest.TestCase):
    def test_categories(self):
        self.assertEqual(one("Bass"), 0)
        self.assertEqual(one("Pluck"), 0)
        self.assertEqual(one("Pad"), 40)
        self.assertEqual(one("Pad", **{"env1.attack": 1200}), 45)
        self.assertEqual(one("Lead"), 30)
        self.assertEqual(one("Keys"), 25)

    def test_the_envelope_decides(self):
        self.assertEqual(one("Lead", **{"env1.sustain": 50}), 0)       # struck
        self.assertEqual(one("Keys", **{"env1.sustain": 20}), 0)
        self.assertEqual(one("Keys", **{"env1.sustain": 80}), 25)
        self.assertEqual(one("FX"), 0)                                    # not a drone
        self.assertEqual(one("FX", **{"env1.attack": 800}), 30)
        self.assertEqual(one("Pad", **{"filter1.on": 0}), 0)

    def test_an_own_route_is_left_alone_and_demos_are_stronger(self):
        self.assertEqual(one("Lead", already=True), 0)
        self.assertEqual(one("Pad", name=pp.DEMOS[0]), pp.DEMO_AMOUNT)

    def test_the_route_is_found(self):
        p = {"mod01.source": SRC["poly_aftertouch"], "mod01.destination": DST["filter1.cutoff"], "mod01.amount": 30}
        self.assertTrue(pp.routes_pressure(p, DOC))
        p["mod01.bypass"] = 1
        self.assertFalse(pp.routes_pressure(p, DOC))
        p = {"mod01.source": SRC["poly_aftertouch"], "mod01.destination": DST["filter1.resonance"], "mod01.amount": 30}
        self.assertFalse(pp.routes_pressure(p, DOC))


class FactoryPresets(unittest.TestCase):
    def test_the_data_is_in_the_presets_and_nothing_else_has_a_route(self):
        data = json.loads(pp.DATA_FILE.read_text(encoding="utf-8"))["presets"]
        self.assertGreaterEqual(len(data), 30)
        self.assertLessEqual(len(data), 70)
        seen = 0
        for rel, name, category, params in pp.read_presets(pp.PRESETS_DIR):
            has = pp.routes_pressure(params, DOC)
            if name in data:
                self.assertTrue(has, name)
                self.assertNotIn(category, ("Bass", "Pluck"), name)
                seen += 1
            elif has:
                self.assertEqual(name, "MPE Pressure Lead")             # its own route
        self.assertEqual(seen, len(data))

    def test_check_passes(self):
        r = subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "propose-pad-pressure.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
