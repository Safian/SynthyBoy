#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/10_env.py (envelope curves on the one Env tab: ENV 1).
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
        cls.before_tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        # Exercise the SDK envelope widget before the release stage replaces it.
        cls.code, cls.err = base.run_apply(cls.skins, base.extras_without(root, "96_env_native.py", "97_no_fallback.py"))
        cls.root = root / "pf"
        cls.labels = json.loads((cls.root / "skin-meta.json").read_text(encoding="utf-8"))["labels"]   # internal page name -> the label the first tab of a key carries
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"]
        params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.index = {p["key"]: i for i, p in enumerate(params)}
        # parameters of the plugin after params.json (82_play.py / 83_pages.py): 4 LFO cursors, 32 spectrum columns, 2 pad pressure, then "Overlay Instance",
        # the 3 Play selectors, the 24 Play proxies and "Q 1" .. "Q 16"
        cls.overlay_param = len(params) + 4 + 32 + 2
        cls.first_q = cls.overlay_param + 1 + 3 + 24

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, n):
        # the Env tab is named "Env" (the one tab of the key)
        assert n == 1, "one Env tab since the Env merge: the host widgets are ENV 1's"
        return next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == "Env")

    def defs(self):
        return {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_one_env_tab_replaces_the_pair_tabs(self):
        names = [t["tabName"] for t in self.tui["pageData"]["tabs"]]
        self.assertEqual(len(names), len(set(names)))
        self.assertIn("Env", names)
        for gone in ("Env 1", "Env 2", "Env 3", "Env 4", "Env 1-2"):
            self.assertNotIn(gone, names)

    def test_every_envelope_handle_points_at_its_parameter(self):
        defs = self.defs()
        for n in (1,):
            comp = next(c for c in self.tab(n)["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "envDisplay%d" % n)
            remap = {m["key"]: m["value"] for m in comp["handle remapping"]["map"]}
            self.assertEqual({k: remap[k] for k in remap if k in [s.capitalize() for s in STAGES]},
                             {s.capitalize(): "Parameter %d" % self.index["env%d.%s" % (n, s)] for s in STAGES})
            envelope = defs["envDisplay%d" % n]["componentsData"][0]["componentData"]
            self.assertEqual(envelope["type"], "Envelope")
            self.assertEqual(envelope["data"]["envelopeType"], "DAHDSR")
            self.assertEqual([envelope["data"][h] for h in HANDLES], [s.capitalize() for s in STAGES])

    def test_overlay_has_the_complete_key_set_and_opens_on_double_tap(self):
        # on the Key 61 (MPC 3.10.1.12) an EnvelopeOverlay without the shape/curve keys drops the whole TUI.json
        defs = self.defs()
        needed = ("attackShapeHandle", "decayShapeHandle", "releaseShapeHandle", "forceHideShapeHandles", "forceCurveShape",
                  "decayAndReleaseCurvesLinked", "curveType")
        for n in (1,):
            for name, typ in (("envDisplay%d" % n, "Envelope"), ("envOverlay%d" % n, "EnvelopeOverlay")):
                data = defs[name]["componentsData"][0]["componentData"]["data"]
                self.assertEqual(defs[name]["componentsData"][0]["componentData"]["type"], typ)
                for key in needed:
                    self.assertIn(key, data, (name, key))
            raw = base.apply_mod.load_json((self.skins / "TUI.json").read_text(encoding="utf-8"))  # keeps repeated keys
            raw_defs = {d["key"]: d["value"] for d in raw["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
            keys = list(raw_defs["envOverlay%d" % n]["componentsData"][0]["componentData"]["data"])
            self.assertEqual(keys[:3], ["version", "version\x001", "actions"])
            display_actions = {a["onAction"]: a["additionalData"] for a in defs["envDisplay%d" % n]["actions"]}
            self.assertEqual(display_actions, {"Double Click": "envOverlay%d" % n, "Enter Pressed": "envOverlay%d" % n})

    def test_shape_handles_are_bound_to_the_curve_parameters(self):
        for n in (1,):
            comp = next(c for c in self.tab(n)["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "envDisplay%d" % n)
            remap = {m["key"]: m["value"] for m in comp["handle remapping"]["map"]}
            for handle, key in (("AttackShape", "attack_curve"), ("DecayShape", "decay_curve"), ("ReleaseShape", "release_curve")):
                self.assertEqual(remap[handle], "Parameter %d" % self.index["env%d.%s" % (n, key)])

    def test_knobs_and_qlinks_match(self):
        for n in (1,):
            tab = self.tab(n)
            knobs = [c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"].startswith("knob")]
            self.assertEqual(len(knobs), 8)
            qmap = base.qlinks_by_tab_name(self.tui, self.qlinks)[tab["tabName"]]
            wanted = [self.index["env%d.%s" % (n, k)] for k in ("delay", "attack", "hold", "decay", "sustain", "release", "attack_curve", "decay_curve")]
            for c in knobs:
                x, y, w, h = map(int, c["bounds"]["bounds"].split())
                self.assertGreaterEqual(y, 301)  # below the envelope (it ends at y 296)
                col = {21: 0, 341: 1, 661: 2, 981: 3}[x]
                row = {343: 2, 483: 3}[y]
                param = int(c["handle remapping"]["map"][0]["value"].split()[1])
                self.assertIn(param, wanted, c["componentData"]["name"])
            self.assertEqual(sorted(int(c["handle remapping"]["map"][0]["value"].split()[1]) for c in knobs), sorted(wanted))
            # in the overlay skin the host knobs stay as the fallback (bound to the real parameters, hidden while the plugin's page is up: 83_pages.py),
            # but the hardware knobs of the tab, Q-Link 1 .. 16, are the sixteen Q proxies "Q 1" .. "Q 16"
            self.assertEqual(qmap, {"Q-Link %d" % (i + 1): self.first_q + i for i in range(16)})

    def test_every_tabs_qlinks_are_the_q_proxies(self):
        # the tab set is the generator's one (the generator numbers the tabs in the order of its pages, one function key each; the sub-tab module renumbers: compare by tab name)
        before = base.qlinks_by_tab_name(self.before_tui, json.loads(self.before_q)["Screen Mode Q-Links"]["map"])
        before = {self.labels.get(name, name): q for name, q in before.items()}   # the first tab under a key is named by its label in the finished skin
        after = base.qlinks_by_tab_name(self.tui, self.qlinks)
        self.assertEqual(sorted(before), sorted(after))
        # in the overlay skin EVERY tab is a page the plugin draws, so the Q-Links of every tab (the Env tabs and all the others alike) are the sixteen
        # Q proxies (83_pages.py / 82_play.py): none keeps the generator's per-tab map
        self.assertEqual(len(after), 15)
        for name, links in after.items():
            self.assertEqual(links, {"Q-Link %d" % (i + 1): self.first_q + i for i in range(16)}, name)

    def test_bounds_version_2_has_its_extra_key(self):
        # validate.py does not check this; the MPC drops the whole TUI.json when the key is missing.
        # Everywhere in the skin the key must be a list (the Osc pictures list their IndexedEnabling handle in it);
        # inside the Env tabs and definitions it is empty.
        def walk(o, expect_empty):
            if isinstance(o, dict):
                if o.get("version") == 2 and "boundsType" in o:
                    handles = o.get("additionalInvalidatingHandles")
                    self.assertIsInstance(handles, list, o)
                    if expect_empty:
                        self.assertEqual(handles, [], o)
                for v in o.values():
                    walk(v, expect_empty)
            elif isinstance(o, list):
                for v in o:
                    walk(v, expect_empty)
        walk(self.tui, False)
        hide = "RangedEnabling/0/18/33/Parameter %d" % self.overlay_param   # 83_pages.py hides the host widgets of a tab while the plugin's page is up
        for n in (1,):
            for c in self.tab(n)["componentDefinition"]["componentsData"]:
                if c["bounds"].get("version") == 2:
                    self.assertIn(c["bounds"]["additionalInvalidatingHandles"], ([], [hide]), c["componentData"]["name"])
        walk({k: v for k, v in self.defs().items() if k.startswith("env")}, True)

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(self.root)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "34 tabs; at most 32" not in l]
        self.assertEqual(errors, [], report)
        self.assertIn("OK     TUI.json: %d tabs" % len(self.tui["pageData"]["tabs"]), report)


if __name__ == "__main__":
    unittest.main()
