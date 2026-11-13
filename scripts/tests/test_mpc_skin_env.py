#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/10_env.py (envelope curves on the tabs Env 1 - Env 4).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mpc_skin_extras as base  # noqa: E402

STAGES = ("delay", "attack", "hold", "decay", "sustain", "release")
HANDLES = ("delayHandle", "attackHandle", "holdHandle", "decayHandle", "sustainHandle", "releaseHandle")


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class EnvSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root)
        cls.before_q = (cls.skins / "Q-Links.json").read_text(encoding="utf-8")
        cls.code, cls.err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        cls.root = root / "pf"
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"]
        params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.index = {p["key"]: i for i, p in enumerate(params)}

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, n):
        return next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == "Env %d" % n)

    def defs(self):
        return {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_four_env_tabs_replace_the_pair_tabs(self):
        names = [t["tabName"] for t in self.tui["pageData"]["tabs"]]
        self.assertEqual(len(names), 32)
        for n in (1, 2, 3, 4):
            self.assertIn("Env %d" % n, names)
        self.assertNotIn("Env 1-2", names)

    def test_every_envelope_handle_points_at_its_parameter(self):
        defs = self.defs()
        for n in (1, 2, 3, 4):
            comp = next(c for c in self.tab(n)["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "envDisplay%d" % n)
            remap = {m["key"]: m["value"] for m in comp["handle remapping"]["map"]}
            self.assertEqual(remap, {s.capitalize(): "Parameter %d" % self.index["env%d.%s" % (n, s)] for s in STAGES})
            envelope = defs["envDisplay%d" % n]["componentsData"][0]["componentData"]
            self.assertEqual(envelope["type"], "Envelope")
            self.assertEqual(envelope["data"]["envelopeType"], "DAHDSR")
            self.assertEqual([envelope["data"][h] for h in HANDLES], [s.capitalize() for s in STAGES])
            overlay = defs["envOverlay%d" % n]["componentsData"][0]["componentData"]
            self.assertEqual(overlay["type"], "EnvelopeOverlay")
            self.assertEqual([overlay["data"][h] for h in HANDLES], [s.capitalize() for s in STAGES])
            actions = {a["onAction"]: a["additionalData"] for a in defs["envDisplay%d" % n]["actions"]}
            self.assertEqual(actions, {"Double Click": "envOverlay%d" % n, "Enter Pressed": "envOverlay%d" % n})

    def test_knobs_and_qlinks_match(self):
        for n in (1, 2, 3, 4):
            tab = self.tab(n)
            knobs = [c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"].startswith("knob")]
            self.assertEqual(len(knobs), 8)
            qmap = next(m for m in self.qlinks if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1)["Q-Links"]
            for c in knobs:
                x, y, w, h = map(int, c["bounds"]["bounds"].split())
                self.assertGreaterEqual(y, 336)  # below the envelope (it ends at y 335)
                col = {23: 0, 343: 1, 663: 2, 983: 3}[x]
                row = {346: 2, 497: 3}[y]
                param = int(c["handle remapping"]["map"][0]["value"].split()[1])
                self.assertEqual(qmap["Q-Link %d" % (4 * (3 - row) + col + 1)], param, c["componentData"]["name"])
            self.assertEqual(sorted(v for v in qmap.values() if v >= 0), sorted(int(c["handle remapping"]["map"][0]["value"].split()[1]) for c in knobs))
            for key in ("release", "delay", "attack_curve", "decay_curve"):
                self.assertIn(self.index["env%d.%s" % (n, key)], qmap.values())

    def test_other_tabs_qlinks_unchanged(self):
        tabs = {t["fnKeyIndex"] + 1 for t in self.tui["pageData"]["tabs"] if t["tabName"].startswith("Env ")}
        before = json.loads(self.before_q)["Screen Mode Q-Links"]["map"]
        after = self.qlinks
        self.assertEqual([m for m in after if m["Tab"] not in tabs], [m for m in before if m["Tab"] not in tabs])

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(self.root)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)
        self.assertIn("OK     TUI.json: 32 tabs", report)


if __name__ == "__main__":
    unittest.main()
