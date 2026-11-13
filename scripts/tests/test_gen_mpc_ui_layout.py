#!/usr/bin/env python3
# SynthyBoy - tests of scripts/gen-mpc-ui.py --layout (the MPC pages of the own engine, scripts/mpc-ui-pages.json).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import contextlib
import importlib.util
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "mpc-ui-pages.json"


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gen = load("t_gen_mpc_ui_layout", "scripts/gen-mpc-ui.py")


def run(out_dir, layout=LAYOUT_JSON, extra=(), pages=PAGES_JSON):
    out, err = io.StringIO(), io.StringIO()
    args = ["--params", str(PARAMS_JSON), "--pages", str(pages), "--out", str(out_dir), "--layout", str(layout)] + list(extra)
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = gen.main(args)
        except SystemExit as e:
            code = e.code
    return code, out.getvalue(), err.getvalue()


def read(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


PARAMS = read(PARAMS_JSON)["params"]
BY_KEY = {p["key"]: p for p in PARAMS}
BY_NAME = {p["name"]: p for p in PARAMS}
KICK = {p["id"]: p for p in read(PAGES_JSON)["pages"]}


def level_page(kid):
    """The fader rule, independent of the generator: the page has continuous (not stepped) own-engine controls and all of them are level or mix like."""
    names = []
    for knob in KICK[kid]["knobs"]:
        if knob and knob.get("key") in BY_KEY and BY_KEY[knob["key"]].get("ownEngine") and BY_KEY[knob["key"]]["scale"] != "stepped":
            names.append(BY_KEY[knob["key"]]["name"].lower())
    return bool(names) and all(any(w in n for w in ("level", "mix", "volume", "dry", "wet", "send")) for n in names)


def expected_pages(layout):
    """The rule, written independently of the generator: per Kick page the knobs in order, without null knobs, unknown keys,
    parameters that are not ownEngine and parameters already placed; every Kick page takes whole rows of 4 slots (padded with "empty" to a
    multiple of 4), the Kick pages of a page follow each other; the page is padded to 16 slots."""
    seen, pages = set(), []
    for page in layout["pages"]:
        slots = []
        for kid in page["kick"]:
            names = []
            for knob in KICK[kid]["knobs"]:
                if not knob or knob.get("key") not in BY_KEY:
                    continue
                p = BY_KEY[knob["key"]]
                if not p.get("ownEngine") or p["name"] in seen:
                    continue
                seen.add(p["name"])
                names.append(p["name"])
            slots += names + [None] * (-len(names) % 4)
        assert len(slots) <= 16, page["name"]
        slots += [None] * (16 - len(slots))
        pages.append((page["name"], slots))
    return pages, seen


class LayoutModeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.code, cls.out, cls.err = run(cls.tmp.name)
        cls.layout = read(LAYOUT_JSON)
        ui = Path(cls.tmp.name) / "ui.json"
        cls.ui = read(ui) if ui.exists() else {"pages": []}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_exit_zero(self):
        self.assertEqual(self.code, 0, self.err)

    def test_one_page_per_layout_entry_in_order(self):
        self.assertEqual([p["name"] for p in self.ui["pages"]], [p["name"] for p in self.layout["pages"]])

    def test_at_most_tab_limit_pages_names_at_most_8_and_unique(self):
        names = [p["name"] for p in self.ui["pages"]]
        self.assertLessEqual(len(names), self.layout.get("tabLimit", 32))
        self.assertTrue(all(1 <= len(n) <= 8 for n in names), names)
        self.assertEqual(len(names), len(set(names)))

    def test_every_page_has_16_slots(self):
        for p in self.ui["pages"]:
            self.assertEqual(len(p["controls"]), 16, p["name"])

    def test_only_own_engine_parameters(self):
        for p in self.ui["pages"]:
            for c in p["controls"]:
                if c == "empty":
                    continue
                self.assertIn(c["param"], BY_NAME, p["name"])
                self.assertTrue(BY_NAME[c["param"]].get("ownEngine"), "%s on page %s is not an own-engine parameter" % (c["param"], p["name"]))

    def test_no_parameter_twice(self):
        names = [c["param"] for p in self.ui["pages"] for c in p["controls"] if c != "empty"]
        self.assertEqual(len(names), len(set(names)))

    def test_no_gap_inside_a_block_of_four(self):
        for p in self.ui["pages"]:
            for b in range(0, 16, 4):
                block = [c != "empty" for c in p["controls"][b:b + 4]]
                self.assertEqual(block, sorted(block, reverse=True), "page %s, block %d has an empty slot before a control" % (p["name"], b // 4))

    def test_no_page_without_controls(self):
        for p in self.ui["pages"]:
            self.assertTrue(any(c != "empty" for c in p["controls"]), p["name"])

    def test_slots_follow_the_rule(self):
        want, _ = expected_pages(self.layout)
        got = [(p["name"], [None if c == "empty" else c["param"] for c in p["controls"]]) for p in self.ui["pages"]]
        self.assertEqual(got, want)

    def test_widgets_follow_the_rules(self):
        # written independently of the generator: stepped with 2-4 choices -> switch, 5 or more -> slider (menu selector), a continuous control is a
        # fader only if the Kick page it comes from has continuous controls and all of them are levels or mixes, otherwise a knob
        origin = {}
        for page in self.layout["pages"]:
            for kid in page["kick"]:
                for knob in KICK[kid]["knobs"]:
                    if knob and knob.get("key") in BY_KEY and BY_KEY[knob["key"]].get("ownEngine") and BY_KEY[knob["key"]]["name"] not in origin:
                        origin[BY_KEY[knob["key"]]["name"]] = kid
        for p in self.ui["pages"]:
            for c in p["controls"]:
                if c == "empty":
                    continue
                param = BY_NAME[c["param"]]
                n = len(param.get("choices") or [])
                if param["scale"] == "stepped" and 2 <= n <= 4:
                    want = "switch"
                elif param["scale"] == "stepped" and n >= 5:
                    want = "slider"
                elif level_page(origin[c["param"]]):
                    want = "fader"
                else:
                    want = "knob"
                self.assertEqual(c["widget"], want, c["param"])

    def test_faders_only_on_level_pages(self):
        faders = {c["param"] for p in self.ui["pages"] for c in p["controls"] if c != "empty" and c["widget"] == "fader"}
        self.assertEqual(faders, {"Bus 1 Level", "Bus 2 Level"} | {"%s Level" % m for m in
                                                                   ("Chorus", "Compressor", "Delay", "Distortion", "EQ", "Flanger", "Phaser", "Reverb")})
        for name in ("Osc 1 Level", "Osc 2 Level", "Osc 3 Level", "Noise Level", "Sub Level", "Master Volume", "Direct Level", "Osc 1 Pan"):
            for p in self.ui["pages"]:
                for c in p["controls"]:
                    if c != "empty" and c["param"] == name:
                        self.assertEqual(c["widget"], "knob", name)

    def test_omitted_count_is_printed(self):
        _, seen = expected_pages(self.layout)
        omitted = sum(1 for p in PARAMS if p.get("ownEngine") and p["name"] not in seen)
        m = re.search(r"^gen-mpc-ui: omitted own-engine parameters: (\d+)$", self.out, re.M)
        self.assertIsNotNone(m, "stdout must have the line 'gen-mpc-ui: omitted own-engine parameters: N'; stdout was:\n" + self.out)
        self.assertEqual(int(m.group(1)), omitted)

    def test_hostsim_still_lists_every_parameter(self):
        doc = read(Path(self.tmp.name) / "params-hostsim.json")
        self.assertEqual(len(doc["params"]), len(PARAMS))

    def test_deterministic(self):
        with tempfile.TemporaryDirectory() as other:
            run(other)
            self.assertEqual((Path(other) / "ui.json").read_bytes(), (Path(self.tmp.name) / "ui.json").read_bytes())

    @unittest.skipUnless(os.environ.get("MPC_SDK"), "MPC_SDK is not set")
    def test_sdk_gen_ui_accepts_it(self):
        skins = Path(self.tmp.name) / "skins"
        r = subprocess.run([sys.executable, str(ROOT / "scripts" / "run-gen-ui.py"), "--sdk", os.environ["MPC_SDK"], "--params", str(Path(self.tmp.name) / "params-hostsim.json"),
                            "--ui", str(Path(self.tmp.name) / "ui.json"), "--out", str(skins), "--quiet"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue((skins / "TUI.json").exists())


class FaderRuleUnitTests(unittest.TestCase):
    def test_level_page_needs_a_continuous_control_and_only_levels(self):
        by_key = {"a": {"key": "a", "name": "Bus 1 Level", "scale": "exp_skew", "ownEngine": True},
                  "b": {"key": "b", "name": "Bus 1 Dest", "scale": "stepped", "choices": ["x", "y", "z"], "ownEngine": True},
                  "c": {"key": "c", "name": "Osc 1 Pan", "scale": "linear", "ownEngine": True},
                  "d": {"key": "d", "name": "Other Level", "scale": "linear", "ownEngine": False}}
        page = lambda *keys: {"knobs": [{"key": k} for k in keys] + [None]}
        self.assertTrue(gen._is_level_page(page("a", "b"), by_key))      # the stepped switch does not count
        self.assertFalse(gen._is_level_page(page("a", "c"), by_key))     # a pan among the levels: knobs
        self.assertFalse(gen._is_level_page(page("b"), by_key))          # no continuous control at all
        self.assertTrue(gen._is_level_page(page("a", "d"), by_key))      # not an own-engine parameter: not looked at

    def test_widget_for_param(self):
        level = {"name": "Chorus Level", "scale": "linear"}
        self.assertEqual(gen._widget_for_param(level, True), "fader")
        self.assertEqual(gen._widget_for_param(level, False), "knob")
        self.assertEqual(gen._widget_for_param({"name": "Master Volume", "scale": "linear"}), "knob")
        self.assertEqual(gen._widget_for_param({"name": "X", "scale": "stepped", "choices": ["a", "b"]}, True), "switch")
        self.assertEqual(gen._widget_for_param({"name": "X", "scale": "stepped", "choices": list("abcde")}, True), "slider")
        self.assertEqual(gen._widget_for_param({"name": "Pan", "scale": "linear", "min": -1, "max": 1, "default": 0}), "knob")


class LayoutErrorTests(unittest.TestCase):
    def bad(self, pages, expect):
        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp) / "layout.json"
            layout.write_text(json.dumps({"format": "synthyboy-mpc-ui-pages", "version": 1, "pages": pages}), encoding="utf-8")
            code, out, err = run(tmp, layout)
            self.assertEqual(code, 2, "expected exit 2 for: " + expect)
            self.assertIn(expect, err)
            self.assertFalse((Path(tmp) / "ui.json").exists(), "nothing may be written on a layout error")

    def test_unknown_kick_page(self):
        self.bad([{"name": "A", "kick": ["no_such_page"]}], "no_such_page")

    def test_name_too_long(self):
        self.bad([{"name": "NineChars", "kick": ["macros"]}], "NineChars")

    def test_empty_name(self):
        self.bad([{"name": "", "kick": ["macros"]}], "name")

    def test_duplicate_name(self):
        self.bad([{"name": "A", "kick": ["macros"]}, {"name": "A", "kick": ["env1"]}], "duplicate")

    def test_five_kick_pages(self):
        self.bad([{"name": "A", "kick": ["env1", "env2", "env3", "env4", "lfo1"]}], "one to 4")

    def test_no_kick_page(self):
        self.bad([{"name": "A", "kick": []}], "one to 4")

    def test_kick_pages_that_do_not_fit_16_slots(self):
        self.bad([{"name": "A", "kick": ["env2", "env3", "env4"]}], "slots")

    def test_sections_differ(self):
        self.bad([{"name": "A", "kick": ["env1", "lfo1"]}], "section")

    def test_kick_page_used_twice(self):
        self.bad([{"name": "A", "kick": ["env1"]}, {"name": "B", "kick": ["env1"]}], "env1")

    def test_more_than_32_pages(self):
        ids = [p for p in KICK][:33]
        self.bad([{"name": "P%d" % i, "kick": [k]} for i, k in enumerate(ids)], "32")

    def test_page_left_without_controls(self):
        # two Kick pages with the same knobs: everything on the second one is already placed
        with tempfile.TemporaryDirectory() as tmp:
            doc = read(PAGES_JSON)
            twin = dict(KICK["env1"]); twin["id"] = "env1_twin"
            doc["pages"].append(twin)
            pages = Path(tmp) / "pages.json"; pages.write_text(json.dumps(doc), encoding="utf-8")
            layout = Path(tmp) / "layout.json"
            layout.write_text(json.dumps({"format": "synthyboy-mpc-ui-pages", "version": 1,
                                          "pages": [{"name": "A", "kick": ["env1"]}, {"name": "B", "kick": ["env1_twin"]}]}), encoding="utf-8")
            code, out, err = run(tmp, layout, pages=pages)
            self.assertEqual(code, 2)
            self.assertIn("no control", err)
            self.assertFalse((Path(tmp) / "ui.json").exists())

    def test_layout_and_page_ids_exclude_each_other(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, out, err = run(tmp, LAYOUT_JSON, ["--page-ids", "env1"])
            self.assertEqual(code, 2)

    def test_wrong_format_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            layout = Path(tmp) / "layout.json"
            layout.write_text(json.dumps({"format": "something-else", "version": 1, "pages": []}), encoding="utf-8")
            code, out, err = run(tmp, layout)
            self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
