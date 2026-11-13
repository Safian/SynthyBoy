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


def expected_pages(layout):
    """The rule, written independently of the generator: per Kick page the knobs in order, without null knobs, unknown keys,
    parameters that are not ownEngine and parameters already placed; padded with "empty" to 8 slots; a second Kick page fills slots 8-15."""
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
            slots += names + [None] * (8 - len(names))
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

    def test_at_most_32_pages_names_at_most_8_and_unique(self):
        names = [p["name"] for p in self.ui["pages"]]
        self.assertLessEqual(len(names), 32)
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

    def test_controls_keep_the_widget_rules_of_the_default_mode(self):
        for p in self.ui["pages"]:
            for c in p["controls"]:
                if c == "empty":
                    continue
                self.assertEqual(c["widget"], gen._widget_for_param(BY_NAME[c["param"]]), c["param"])

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
        r = subprocess.run([sys.executable, str(Path(os.environ["MPC_SDK"]) / "tools" / "gen_ui.py"), "--params", str(Path(self.tmp.name) / "params-hostsim.json"),
                            "--ui", str(Path(self.tmp.name) / "ui.json"), "--out", str(skins), "--quiet"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue((skins / "TUI.json").exists())


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

    def test_three_kick_pages(self):
        self.bad([{"name": "A", "kick": ["env1", "env2", "env3"]}], "one or two")

    def test_no_kick_page(self):
        self.bad([{"name": "A", "kick": []}], "one or two")

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
