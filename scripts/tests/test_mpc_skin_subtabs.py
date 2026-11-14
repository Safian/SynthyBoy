#!/usr/bin/env python3
# SynthyBoy - tests of the function keys and sub-tabs of the MPC skin (layout files scripts/mpc-ui-pages*.json, 95_subtabs.py, run-gen-ui.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_gen_mpc_ui_layout as lay  # noqa: E402
import test_mpc_skin_extras as base  # noqa: E402

ROOT = base.ROOT
LAYOUTS = sorted((ROOT / "scripts").glob("mpc-ui-pages*.json"))
DEFAULT_LAYOUT = ROOT / "scripts" / "mpc-ui-pages.json"
BASELINE = json.loads((ROOT / "scripts" / "tests" / "data" / "mpc-skin-7a0d198-params.json").read_text(encoding="utf-8"))["keys"]
SLOT_TYPES = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu")
MAX_SUBTABS = 8


def layout_doc(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


class LayoutFileTests(unittest.TestCase):
    """The layout files themselves (no SDK needed)."""

    def test_default_layout_has_34_tabs_and_the_fallback_is_within_the_documented_limit(self):
        doc = layout_doc(DEFAULT_LAYOUT)       # the Key 61 loads 34 tabs (docs/MPC.md)
        self.assertEqual((len(doc["pages"]), doc["tabLimit"]), (34, 34))
        fallback = layout_doc(ROOT / "scripts" / "mpc-ui-pages-v32.json")
        self.assertEqual(len(fallback["pages"]), 32)
        self.assertNotIn("tabLimit", fallback)

    def test_function_keys_follow_each_other_and_have_at_most_8_sub_tabs(self):
        for path in LAYOUTS:
            keys = []
            for page in layout_doc(path)["pages"]:
                fn = page.get("fn", page["name"])
                if keys and keys[-1][0] == fn:
                    keys[-1][1].append(page["name"])
                else:
                    self.assertNotIn(fn, [k for k, _ in keys], "%s: function key %s is split" % (path.name, fn))
                    keys.append((fn, [page["name"]]))
            for fn, tabs in keys:
                self.assertLessEqual(len(tabs), MAX_SUBTABS, (path.name, fn))

    def test_tab_names_are_short_and_unique(self):
        for path in LAYOUTS:
            names = [p["name"] for p in layout_doc(path)["pages"]]
            self.assertEqual(len(names), len(set(names)), path.name)
            self.assertTrue(all(1 <= len(n) <= 8 for n in names), (path.name, names))

    def test_every_page_fits_16_controls(self):
        for path in LAYOUTS:
            with tempfile.TemporaryDirectory() as tmp:
                code, out, err = lay.run(tmp, path)
                self.assertEqual(code, 0, err)
                ui = json.loads((Path(tmp) / "ui.json").read_text(encoding="utf-8"))
                for page in ui["pages"]:
                    self.assertEqual(len(page["controls"]), 16)

    def test_v34_and_v32_hold_the_same_parameters(self):
        sets = {}
        for path in LAYOUTS:
            with tempfile.TemporaryDirectory() as tmp:
                code, out, err = lay.run(tmp, path)
                self.assertEqual(code, 0, err)
                ui = json.loads((Path(tmp) / "ui.json").read_text(encoding="utf-8"))
                sets[path.name] = {c["param"] for p in ui["pages"] for c in p["controls"] if c != "empty"}
        first = next(iter(sets.values()))
        for name, s in sets.items():
            self.assertEqual(s, first, name)

    def test_the_whole_baseline_is_in_the_layouts(self):
        # nothing is lost in the rearrangement: every parameter that had a control at 7a0d198 has one (the Env tabs repeat the eight knobs of the
        # envelope: 10_env.py, checked on the skin below)
        for path in LAYOUTS:
            with tempfile.TemporaryDirectory() as tmp:
                code, out, err = lay.run(tmp, path)
                ui = json.loads((Path(tmp) / "ui.json").read_text(encoding="utf-8"))
                names = {c["param"] for p in ui["pages"] for c in p["controls"] if c != "empty"}
            keys = {lay.BY_NAME[n]["key"] for n in names}
            missing = [k for k in BASELINE if k not in keys and not k.startswith("env")]
            self.assertEqual(missing, [], path.name)


class FunctionKeyErrorTests(unittest.TestCase):
    def bad(self, pages, expect, **extra):
        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp) / "layout.json"
            layout.write_text(json.dumps(dict({"format": "synthyboy-mpc-ui-pages", "version": 2, "pages": pages}, **extra)), encoding="utf-8")
            code, out, err = lay.run(tmp, layout)
            self.assertEqual(code, 2, "expected exit 2 for: " + expect)
            self.assertIn(expect, err)
            self.assertFalse((Path(tmp) / "ui.json").exists(), "nothing may be written on a layout error")

    def test_split_function_key(self):
        self.bad([{"name": "A", "fn": "K", "kick": ["env1"]}, {"name": "B", "fn": "L", "kick": ["env2"]}, {"name": "C", "fn": "K", "kick": ["env3"]}], "follow each other")

    def test_nine_sub_tabs(self):
        ids = ["env1", "env2", "env3", "env4"] + ["osc1_main", "osc2_main", "osc3_main", "sub_main", "noise"]
        self.bad([{"name": "T%d" % i, "fn": "K", "kick": [k]} for i, k in enumerate(ids)], "at most 8")

    def test_tab_limit_is_a_number_of_at_least_32(self):
        self.bad([{"name": "A", "kick": ["env1"]}], "tabLimit", tabLimit=31)

    def test_33_pages_need_a_tab_limit(self):
        ids = [k for page in layout_doc(DEFAULT_LAYOUT)["pages"] for k in page["kick"]][:33]   # Kick pages of the skin (names that need no override)
        pages = [{"name": "P%d" % i, "fn": "K%d" % (i // 8), "kick": [k]} for i, k in enumerate(ids)]
        self.bad(pages, "more than 32")
        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp) / "layout.json"
            layout.write_text(json.dumps({"format": "synthyboy-mpc-ui-pages", "version": 2, "tabLimit": 33, "pages": pages}), encoding="utf-8")
            code, out, err = lay.run(tmp, layout)
            self.assertEqual(code, 0, err)
            meta = json.loads((Path(tmp) / "skin-meta.json").read_text(encoding="utf-8"))
            self.assertEqual(sum(len(k["tabs"]) for k in meta["fnKeys"]), 33)

    def test_a_page_without_fn_is_a_key_of_its_own(self):
        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp) / "layout.json"
            layout.write_text(json.dumps({"format": "synthyboy-mpc-ui-pages", "version": 2, "pages": [
                {"name": "A", "kick": ["env1"]}, {"name": "B", "fn": "K", "kick": ["env2"]}, {"name": "C", "fn": "K", "kick": ["env3"]}]}), encoding="utf-8")
            code, out, err = lay.run(tmp, layout)
            self.assertEqual(code, 0, err)
            meta = json.loads((Path(tmp) / "skin-meta.json").read_text(encoding="utf-8"))
            self.assertEqual(meta["fnKeys"], [{"name": "A", "tabs": ["A"]}, {"name": "K", "tabs": ["B", "C"]}])


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class SubTabSkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.skins = {}
        cls.tui = {}
        cls.q = {}
        cls.meta = {}
        for i, path in enumerate(LAYOUTS):
            root = Path(cls._tmp.name) / str(i)
            root.mkdir()
            skins = base.generate_skins(root, path)
            code, err = base.run_apply(skins, base.REAL_EXTRAS)
            assert (code, err) == (0, ""), err
            cls.skins[path.name] = skins
            cls.tui[path.name] = json.loads((skins / "TUI.json").read_text(encoding="utf-8"))
            cls.q[path.name] = json.loads((skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"]
            cls.meta[path.name] = json.loads((skins.parent / "skin-meta.json").read_text(encoding="utf-8"))
        cls.params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))["params"]

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def each(self):
        for name in self.tui:
            yield name, self.tui[name]["pageData"]["tabs"], self.q[name]

    def test_function_key_and_sub_index_structure(self):
        for name, tabs, _ in self.each():
            fn = [t["fnKeyIndex"] for t in tabs]
            self.assertEqual(sorted(set(fn)), list(range(max(fn) + 1)), name)   # continuous 0 .. N-1
            self.assertEqual(fn, sorted(fn), name)                              # in the order of the keys
            for key in set(fn):
                subs = [t["fnKeySubIndex"] for t in tabs if t["fnKeyIndex"] == key]
                self.assertEqual(subs, list(range(len(subs))), (name, key))      # 0 .. k-1
                self.assertLessEqual(len(subs), MAX_SUBTABS, (name, key))
            self.assertEqual(len({(t["fnKeyIndex"], t["fnKeySubIndex"]) for t in tabs}), len(tabs), name)

    def test_tab_count(self):
        counts = {name: len(tabs) for name, tabs, _ in self.each()}
        self.assertEqual((counts["mpc-ui-pages.json"], counts["mpc-ui-pages-v32.json"]), (34, 32))
        for name, n in counts.items():
            self.assertEqual(n, len(layout_doc(ROOT / "scripts" / name)["pages"]), name)

    def test_names_follow_the_layout_and_fit_the_button(self):
        for name, tabs, _ in self.each():
            want = [p["name"] for p in layout_doc(ROOT / "scripts" / name)["pages"]]
            self.assertEqual([t["tabName"] for t in tabs], want)
            self.assertTrue(all(len(t["tabName"]) <= 8 for t in tabs))

    def test_q_links_have_an_entry_for_every_tab_and_sub_tab(self):
        for name, tabs, qmap in self.each():
            self.assertEqual([(m["Tab"], m["SubTab"]) for m in qmap], [(t["fnKeyIndex"] + 1, t["fnKeySubIndex"] + 1) for t in tabs], name)
            for m in qmap:
                self.assertEqual(list(m["Q-Links"]), ["Q-Link %d" % i for i in range(1, 17)])
                self.assertEqual(m["Bank Direction"], "Column")

    def test_every_control_is_in_the_q_link_map_of_its_tab(self):
        for name, tabs, qmap in self.each():
            by_tab = base.qlinks_by_tab_name(self.tui[name], qmap)
            for tab in tabs:
                controls = set()
                for c in tab["componentDefinition"]["componentsData"]:
                    if c["componentData"]["type"] in SLOT_TYPES:
                        controls.add(int(c["handle remapping"]["map"][0]["value"].split()[1]))
                self.assertEqual({v for v in by_tab[tab["tabName"]].values() if v >= 0}, controls, (name, tab["tabName"]))

    def test_component_names_are_unique_inside_a_tab(self):
        for name, tabs, _ in self.each():
            for tab in tabs:
                names = [c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]]
                self.assertEqual(len(names), len(set(names)), (name, tab["tabName"], sorted(n for n in names if names.count(n) > 1)))

    def test_every_baseline_parameter_still_has_a_control(self):
        for name, tabs, _ in self.each():
            have = set()
            for tab in tabs:
                for c in tab["componentDefinition"]["componentsData"]:
                    if c["componentData"]["type"] in SLOT_TYPES:
                        idx = int(c["handle remapping"]["map"][0]["value"].split()[1])
                        if idx < len(self.params):
                            have.add(self.params[idx]["key"])
            self.assertEqual([k for k in BASELINE if k not in have], [], name)

    def test_no_parameter_has_a_control_on_two_tabs_except_the_envelope_knobs(self):
        for name, tabs, _ in self.each():
            seen = {}
            for tab in tabs:
                tab_keys = set()
                for c in tab["componentDefinition"]["componentsData"]:
                    if c["componentData"]["type"] in SLOT_TYPES:
                        idx = int(c["handle remapping"]["map"][0]["value"].split()[1])
                        if idx < len(self.params):
                            tab_keys.add(self.params[idx]["key"])
                for k in tab_keys:
                    if k in seen:
                        self.assertTrue(k.startswith("env"), "%s: %s on %s and %s" % (name, k, seen[k], tab["tabName"]))
                    seen[k] = tab["tabName"]

    def test_validate_py(self):
        for name, skins in self.skins.items():
            res = subprocess.run([sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(skins.parent)], capture_output=True, text=True)
            report = res.stdout + res.stderr
            errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
            if name == "mpc-ui-pages-v32.json":
                self.assertEqual(errors, [], report)
            else:
                # the 34 tab skin: the SDK validator rejects the number of tabs and nothing else (the build accepts exactly this finding)
                self.assertEqual([e for e in errors if "tabs; at most 32" not in e], [], report)
                self.assertEqual([e for e in errors if "tabs; at most 32" in e], ["ERROR  TUI.json: 34 tabs; at most 32 are supported"], report)

    def test_module_without_meta_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            skins = base.generate_skins(Path(tmp))
            (skins.parent / "skin-meta.json").unlink()
            extras = Path(tmp) / "extras"
            extras.mkdir()
            (extras / "95_subtabs.py").write_text((base.REAL_EXTRAS / "95_subtabs.py").read_text(encoding="utf-8"), encoding="utf-8")
            code, err = base.run_apply(skins, extras)
            self.assertEqual(code, 1)
            self.assertIn("95_subtabs.py", err)
            self.assertIn("fnKeys", err)


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class RunGenUiTests(unittest.TestCase):
    def test_limit_is_lifted_only_for_more_pages(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            code, out, err = lay.run(tmp / "ui", ROOT / "scripts" / "mpc-ui-pages-v32.json")
            self.assertEqual(code, 0, err)
            ok = subprocess.run([sys.executable, str(ROOT / "scripts" / "run-gen-ui.py"), "--sdk", str(base.SDK), "--params", str(tmp / "ui" / "params-hostsim.json"),
                                 "--ui", str(tmp / "ui" / "ui.json"), "--out", str(tmp / "a"), "--quiet"], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
            self.assertNotIn("lifted", ok.stdout)
            # the plain SDK tool refuses 33 pages, the wrapper accepts them and says so
            ui = json.loads((tmp / "ui" / "ui.json").read_text(encoding="utf-8"))
            while len(ui["pages"]) < 33:
                ui["pages"].append(dict(ui["pages"][-1], name="X%d" % len(ui["pages"])))
            (tmp / "ui" / "big.json").write_text(json.dumps(ui), encoding="utf-8")
            args = ["--params", str(tmp / "ui" / "params-hostsim.json"), "--ui", str(tmp / "ui" / "big.json"), "--out", str(tmp / "b"), "--quiet"]
            plain = subprocess.run([sys.executable, str(base.SDK / "tools" / "gen_ui.py")] + args, capture_output=True, text=True)
            self.assertNotEqual(plain.returncode, 0)
            wrapped = subprocess.run([sys.executable, str(ROOT / "scripts" / "run-gen-ui.py"), "--sdk", str(base.SDK)] + args, capture_output=True, text=True)
            self.assertEqual(wrapped.returncode, 0, wrapped.stdout + wrapped.stderr)
            self.assertIn("lifted", wrapped.stdout)


if __name__ == "__main__":
    unittest.main()
