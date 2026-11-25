#!/usr/bin/env python3
# SynthyBoy - tests of the short names of the controls on the MPC skin (gen-mpc-ui.py names, mpc-short-names.json).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import test_gen_mpc_ui_layout as lay  # noqa: E402
import test_mpc_skin_extras as base  # noqa: E402

ROOT = base.ROOT
SHORT_NAMES = ROOT / "scripts" / "mpc-short-names.json"
LAYOUTS = [ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"]
gen = lay.gen
NAME_AREA_W = gen.NAME_AREA_W


def make_meta(layout, tmp):
    code, out, err = lay.run(tmp, layout)
    assert code == 0, err
    return json.loads((Path(tmp) / "skin-meta.json").read_text(encoding="utf-8"))


def words_of_suffixes(full):
    w = full.split()
    return [" ".join(w[i:]) for i in range(len(w))]


class ResolveTests(unittest.TestCase):
    def test_short_label_is_the_name(self):
        out = gen.resolve_tab_names([("a.x", "Osc 1 Detune", "Detune"), ("a.y", "Osc 1 Level", "Level")], {"a.x", "a.y"}, {})
        self.assertEqual(out, {"a.x": "Detune", "a.y": "Level"})

    def test_override_wins(self):
        out = gen.resolve_tab_names([("a.x", "Osc 1 Unison Stereo Spread", "Unison Ste")], {"a.x"}, {"a.x": "Stereo Spread"})
        self.assertEqual(out["a.x"], "Stereo Spread")

    def test_clash_gets_the_words_that_differ(self):
        entries = [("o1.fb", "Osc 1 Filter Blend", "Filt Blend"), ("o2.fb", "Osc 2 Filter Blend", "Filt Blend"), ("n.fb", "Noise Filter Blend", "Filt Blend")]
        out = gen.resolve_tab_names(entries, {k for k, _, _ in entries}, {})
        self.assertEqual(out, {"o1.fb": "Osc 1 Filt Blend", "o2.fb": "Osc 2 Filt Blend", "n.fb": "Noise Filt Blend"})

    def test_only_placed_controls_clash(self):
        entries = [("o1.l", "Osc 1 Level", "Level"), ("o2.l", "Osc 2 Level", "Level")]
        out = gen.resolve_tab_names(entries, {"o1.l"}, {})
        self.assertEqual(out, {"o1.l": "Level", "o2.l": "Level"})

    def test_clash_without_a_common_suffix_is_an_error(self):
        entries = [("a", "Compressor Mid Lower Threshold", "Mid Lower "), ("b", "Compressor Mid Lower Ratio", "Mid Lower ")]
        with self.assertRaises(ValueError) as cm:
            gen.resolve_tab_names(entries, {"a", "b"}, {})
        self.assertIn("overrides", str(cm.exception))

    def test_clash_with_an_override_is_an_error(self):
        entries = [("a", "X One", "Same"), ("b", "Y One", "Same")]
        with self.assertRaises(ValueError):
            gen.resolve_tab_names(entries, {"a", "b"}, {"a": "Same"})

    def test_width_is_upper_case_and_checks_characters(self):
        self.assertAlmostEqual(gen.name_width("Ab", 20), (602 + 609) * 20 / 1000.0)
        with self.assertRaises(ValueError):
            gen.name_width("a#b", 20)

    def test_override_drops_module_prefix_when_tab_shares_it(self):
        entries = [("e1.a", "Env 1 Attack", "Attack"), ("e1.r", "Env 1 Release", "Release")]
        out = gen.resolve_tab_names(entries, {"e1.a", "e1.r"}, {"e1.r": "Env 1 Release"})
        self.assertEqual(out, {"e1.a": "Attack", "e1.r": "Release"})

    def test_override_drops_module_prefix_when_single_control_on_tab(self):
        entries = [("e1.r", "Env 1 Release", "Release")]
        out = gen.resolve_tab_names(entries, {"e1.r"}, {"e1.r": "Env 1 Release"})
        self.assertEqual(out, {"e1.r": "Release"})

    def test_override_keeps_module_prefix_when_tab_holds_multiple_modules(self):
        entries = [("m.1", "Macro 1", "Macro 1"), ("e1.r", "Env 1 Release", "Release")]
        out = gen.resolve_tab_names(entries, {"m.1", "e1.r"}, {"e1.r": "Env 1 Release"})
        self.assertEqual(out, {"m.1": "Macro 1", "e1.r": "Env 1 Release"})

    def test_override_keeps_module_prefix_when_dropping_causes_clash(self):
        entries = [("e1.r", "Env 1 Release", "Release"), ("e1.r2", "Env 1 Alt", "Release")]
        out = gen.resolve_tab_names(entries, {"e1.r", "e1.r2"}, {"e1.r": "Env 1 Release"})
        self.assertEqual(out["e1.r"], "Env 1 Release")
        self.assertEqual(out["e1.r2"], "Release")


class NameRulesTests(unittest.TestCase):
    """The names the generator hands to the skin, for every layout file of scripts/ (the V32 one and its V34 variant)."""

    @classmethod
    def setUpClass(cls):
        cls.metas = {}
        for layout in LAYOUTS:
            with tempfile.TemporaryDirectory() as tmp:
                cls.metas[layout.name] = make_meta(layout, tmp)
        cls.overrides = json.loads(SHORT_NAMES.read_text(encoding="utf-8"))["names"]

    def test_there_is_a_layout(self):
        self.assertTrue(LAYOUTS)

    def test_names_are_unique_inside_a_tab(self):
        for layout, meta in self.metas.items():
            for tab in meta["tabs"]:
                placed = tab.get("placed", list(tab["names"]))
                names = [tab["names"][k] for k in placed]
                self.assertEqual(len(names), len(set(names)), "%s / %s: %s" % (layout, tab["name"], names))

    def test_short_name_width_budget(self):
        px = 22  # Reference font size of the metadata width budget.
        widest = max(((gen.name_width(n, px), n, t["name"]) for m in self.metas.values() for t in m["tabs"] for n in t["names"].values()), key=lambda x: x[0])
        self.assertLessEqual(widest[0], NAME_AREA_W, "the widest name %r on %r is estimated %.0f px wide, the area is %d px" % (widest[1], widest[2], widest[0], NAME_AREA_W))
        # a little room is left: the estimate is an upper bound already, but the rule is not to run at the limit
        self.assertLessEqual(widest[0], NAME_AREA_W * 0.95)

    def test_names_are_not_cut_off_in_the_middle_of_a_word(self):
        # the shortLabels of the Kick pages are cut at 10 characters ("Unison Ste"): such a name must be overridden. Deliberate abbreviations
        # ("Dest", "Atk Curve") are short; only names of 9 characters or more can be a cut-off
        by_key = lay.BY_KEY
        for layout, meta in self.metas.items():
            for tab in meta["tabs"]:
                for key, name in tab["names"].items():
                    if key in self.overrides or not any(knob and knob["key"] == key and knob["shortLabel"] == name
                                                         for kp in lay.KICK.values() for knob in kp["knobs"]):
                        continue   # an override is the author's choice; a prefixed name ends in a short label that is checked itself
                    full = by_key[key]["name"]
                    for tail in words_of_suffixes(full):
                        if len(name) >= 9 and tail.startswith(name) and len(tail) > len(name):
                            self.assertEqual(tail[len(name)], " ", "%s / %s: %r is cut off from %r" % (layout, tab["name"], name, full))

    def test_override_keys_exist_and_are_used(self):
        used = {k for m in self.metas.values() for t in m["tabs"] for k in t["names"]}
        for key in self.overrides:
            self.assertIn(key, lay.BY_KEY)
            self.assertIn(key, used, "%s is in mpc-short-names.json but on no tab" % key)

    def test_a_name_that_needs_no_override_is_the_kick_short_label(self):
        short = {}
        for kp in lay.KICK.values():
            for knob in kp["knobs"]:
                if knob:
                    short.setdefault(knob["key"], set()).add(knob["shortLabel"])
        plain = 0
        for meta in self.metas.values():
            for tab in meta["tabs"]:
                for key, name in tab["names"].items():
                    if key in self.overrides:
                        if tab["name"] == "Env" and key == "env1.release":
                            self.assertEqual(name, "Release", "%s / %s" % (tab["name"], key))
                        elif tab["name"] == "Filter" and key in ("filter1.cutoff", "filter1.resonance"):
                            self.assertEqual(name, self.overrides[key].split()[-1], "%s / %s" % (tab["name"], key))
                        else:
                            self.assertEqual(name, self.overrides[key], "%s / %s" % (tab["name"], key))
                    elif name in short[key]:
                        plain += 1
                    else:   # prefixed because of a clash inside the tab: the Kick label is the end of the name
                        self.assertTrue(any(name.endswith(" " + s) for s in short[key]), (tab["name"], key, name))
        self.assertGreater(plain, 80)   # 15 tabs since the Env, LFO and Filter merges (it was 100 with 22)

    def test_documented_examples(self):
        tabs = {t["name"]: t["names"] for t in next(iter(self.metas.values()))["tabs"]}
        self.assertEqual(tabs["Mixer"]["osc1.filter_blend"], "Osc 1 Filt Blend")
        self.assertEqual(tabs["Mixer"]["osc2.filter_blend"], "Osc 2 Filt Blend")
        self.assertEqual(tabs["Mod"]["mod01.source"], "Mod 01 Source")
        self.assertEqual(tabs["Mod"]["mod02.source"], "Mod 02 Source")

    def test_env1_release_name_per_tab(self):
        for layout, meta in self.metas.items():
            tabs = {t["name"]: t["names"] for t in meta["tabs"]}
            self.assertEqual(tabs["Env"]["env1.release"], "Release", "%s / Env" % layout)
            self.assertEqual(tabs["Macros"]["env1.release"], "Env 1 Release", "%s / Macros" % layout)


if __name__ == "__main__":
    unittest.main()
