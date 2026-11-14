#!/usr/bin/env python3
# SynthyBoy - tests of the short names of the controls on the MPC skin (gen-mpc-ui.py names, mpc-short-names.json, 85_names.py).
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
SHORT_NAMES = ROOT / "scripts" / "mpc-short-names.json"
LAYOUTS = sorted((ROOT / "scripts").glob("mpc-ui-pages*.json"))
gen = lay.gen
names_mod = base.load("t_85_names", base.REAL_EXTRAS / "85_names.py")
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

    def test_every_name_fits_the_frame(self):
        px = names_mod.NAME_FONT
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
                        if tab["name"] == "Env 1" and key == "env1.release":
                            self.assertEqual(name, "Release", "%s / %s" % (tab["name"], key))
                        else:
                            self.assertEqual(name, self.overrides[key], "%s / %s" % (tab["name"], key))
                    elif name in short[key]:
                        plain += 1
                    else:   # prefixed because of a clash inside the tab: the Kick label is the end of the name
                        self.assertTrue(any(name.endswith(" " + s) for s in short[key]), (tab["name"], key, name))
        self.assertGreater(plain, 100)

    def test_documented_examples(self):
        tabs = {t["name"]: t["names"] for t in next(iter(self.metas.values()))["tabs"]}
        self.assertEqual(tabs["Osc1 Shp"]["osc1.unison.detune_curve"], "Detune Curve")
        self.assertEqual(tabs["Mixer"]["osc1.filter_blend"], "Osc 1 Filt Blend")
        self.assertEqual(tabs["Mixer"]["osc2.filter_blend"], "Osc 2 Filt Blend")
        self.assertEqual(tabs["Mod 1-2"]["mod01.source"], "Mod 01 Source")
        self.assertEqual(tabs["Mod 1-2"]["mod02.source"], "Mod 02 Source")

    def test_env1_release_name_per_tab(self):
        for layout, meta in self.metas.items():
            tabs = {t["name"]: t["names"] for t in meta["tabs"]}
            self.assertEqual(tabs["Env 1"]["env1.release"], "Release", "%s / Env 1" % layout)
            self.assertEqual(tabs["Macros"]["env1.release"], "Env 1 Release", "%s / Macros" % layout)


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class NameLabelsOnTheSkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root)
        code, err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        assert (code, err) == (0, ""), err
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.meta = json.loads((cls.skins.parent / "skin-meta.json").read_text(encoding="utf-8"))
        cls.names = {t["name"]: t["names"] for t in cls.meta["tabs"]}

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tabs(self):
        return self.tui["pageData"]["tabs"]

    @staticmethod
    def rect(c):
        return tuple(int(v) for v in c["bounds"]["bounds"].split())

    def controls_and_labels(self, tab):
        items = tab["componentDefinition"]["componentsData"]
        for i, c in enumerate(items):
            if c["componentData"]["type"] in names_mod.NAME_DY:
                yield c, items[i + 1]

    def test_definitions_have_no_name_child(self):
        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        for key in names_mod.NAME_DY:
            if key not in defs:   # the generator writes only the definitions the skin uses
                continue
            self.assertEqual([c for c in defs[key]["componentsData"] if c["componentData"]["name"] == "name"], [], key)

    def test_every_named_control_is_followed_by_its_text(self):
        count = 0
        for tab in self.tabs():
            for c, label in self.controls_and_labels(tab):
                count += 1
                self.assertEqual(label["componentData"]["type"], "Label")
                self.assertEqual(label["componentData"]["name"], c["componentData"]["name"] + " name")
                key = base_key(self.tui, c)
                expected = SHELL_NAMES[key] if key in SHELL_NAMES else self.names[tab["tabName"]][key]   # the short names of the shell's own parameters are 45_pad_pressure.py's
                self.assertEqual(label["componentData"]["data"]["text"], expected, (tab["tabName"], key))
        self.assertGreater(count, 250)

    def test_style_is_the_selector_header_weight(self):
        for tab in self.tabs():
            for _, label in self.controls_and_labels(tab):
                data = label["componentData"]["data"]
                style = data["textStyle"]
                self.assertEqual((data["type"], data["handleName"]), ("Text", "Data"))
                self.assertEqual((style["font"]["name"], style["font"]["style"], style["font"]["height"]), ("Titillium Web", "SemiBold", 22.0))
                self.assertEqual((style["colour"], style["case"], style["justification"]), ("ff6a6a6c", "Upper Case", "horizontallyCentred verticallyCentred"))

    def test_every_text_label_of_the_skin_is_bound_to_a_valid_parameter(self):
        # Key 61: a Text label with an empty remap is not drawn at all; the remap must point at the parameter of the control the text belongs to
        count = 0
        for tab in self.tabs():
            items = tab["componentDefinition"]["componentsData"]
            controls = {c["componentData"]["name"]: c for c in items if c["componentData"]["type"] in names_mod.NAME_DY}
            for c in items:
                d = c["componentData"]
                if d["type"] != "Label" or d["data"]["type"] != "Text":
                    continue
                count += 1
                remap = c["handle remapping"]["map"]
                self.assertEqual(len(remap), 1, (tab["tabName"], d["name"]))
                self.assertEqual(remap[0]["key"], "Data")
                index = int(remap[0]["value"].split()[1])
                self.assertTrue(0 <= index < len(lay.PARAMS) or index in SHELL_KEYS, (tab["tabName"], d["name"]))
                if d["name"].startswith("Pad Pressure hint "):   # 45_pad_pressure.py: the hint is bound to the toggle of the pad pressure
                    self.assertEqual(index, len(lay.PARAMS) + 4 + 32, (tab["tabName"], d["name"]))
                    continue
                owner = controls[d["name"].rsplit(" name", 1)[0] if d["name"].endswith(" name") else d["name"].rsplit(" seg ", 1)[0]]
                self.assertEqual(remap, owner["handle remapping"]["map"], (tab["tabName"], d["name"]))
        self.assertGreater(count, 300)

    def test_value_keeps_white_24(self):
        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        for key in ("knobUni", "knobBi", "toggle", "faderUni", "faderBi"):
            if key not in defs:
                continue
            value = next(c for c in defs[key]["componentsData"] if c["componentData"]["name"] == "value")
            self.assertEqual(value["componentData"]["data"]["textStyle"]["font"]["height"], 24.0, key)
            self.assertEqual(value["componentData"]["data"]["textStyle"]["colour"], "ffffffff", key)

    def test_label_is_over_the_top_of_its_slot_inside_the_frame(self):
        for tab in self.tabs():
            frames = [tuple(int(v) for v in f.split()) for f in tab["qlinkBoundsData"]]
            for c, label in self.controls_and_labels(tab):
                sx, sy, sw, sh = self.rect(c)
                x, y, w, h = self.rect(label)
                self.assertEqual((x, w), (sx + 3, 272))
                self.assertTrue(sy <= y and y + h <= sy + sh, (tab["tabName"], c["componentData"]["name"]))
                self.assertTrue(any(f[0] <= x and x + w <= f[0] + f[2] for f in frames))
                self.assertLessEqual(abs(x + w / 2 - sx - 139), 1)

    def test_label_has_the_visibility_of_its_control(self):
        for tab in self.tabs():
            for c, label in self.controls_and_labels(tab):
                self.assertEqual(label["bounds"]["version"], c["bounds"]["version"])
                self.assertEqual(label["bounds"].get("additionalInvalidatingHandles"), c["bounds"].get("additionalInvalidatingHandles"))
                self.assertEqual(label["bounds"]["acceptsHWFocus"], "No")

    def test_selectors_keep_the_host_header(self):
        # the SDK Slider has no key for another text: no label for the selectors
        for tab in self.tabs():
            items = tab["componentDefinition"]["componentsData"]
            for i, c in enumerate(items):
                if c["componentData"]["type"] in ("sliderWithFocus", "sliderWithFocusMenu") and i + 1 < len(items):
                    self.assertFalse(items[i + 1]["componentData"]["name"] == c["componentData"]["name"] + " name")

    def test_names_do_not_overlap_other_controls(self):
        for tab in self.tabs():
            items = tab["componentDefinition"]["componentsData"]
            for c, label in self.controls_and_labels(tab):
                x, y, w, h = self.rect(label)
                for o in items:
                    if o is c or o is label or o["componentData"]["type"] in ("Image", "Label", "envDisplay1", "envDisplay2", "envDisplay3", "envDisplay4"):
                        continue
                    ox, oy, ow, oh = self.rect(o)
                    hit = x < ox + ow and ox < x + w and y < oy + oh and oy < y + h
                    self.assertFalse(hit, "%s: the name of %s overlaps %s" % (tab["tabName"], c["componentData"]["name"], o["componentData"]["name"]))

    def test_full_parameter_names_are_untouched(self):
        # the host still shows the full name (overlay header, DAW lists): the parameter table does not change
        for t in self.tabs():
            for c in t["componentDefinition"]["componentsData"]:
                if c["componentData"]["type"] in names_mod.NAME_DY:
                    key = base_key(self.tui, c)
                    if key in SHELL_KEYS.values():   # not in params.json: the plugin's own parameters (shell-mpc/pad_pressure.h)
                        continue
                    self.assertIn(lay.BY_KEY[key]["name"], lay.BY_NAME)

    def test_env1_release_label_text(self):
        env1_tab = next(t for t in self.tabs() if t["tabName"] == "Env 1")
        env1_labels = {c["componentData"]["name"]: label["componentData"]["data"]["text"]
                       for c, label in self.controls_and_labels(env1_tab)}
        self.assertIn("Env 1 Release", env1_labels)
        self.assertEqual(env1_labels["Env 1 Release"], "Release")
        self.assertEqual(env1_labels["Env 1 Attack"], "Attack")
        self.assertEqual(env1_labels["Env 1 Decay"], "Decay")
        self.assertEqual(env1_labels["Env 1 Sustain"], "Sustain")

        macros_tab = next(t for t in self.tabs() if t["tabName"] == "Macros")
        macros_labels = {c["componentData"]["name"]: label["componentData"]["data"]["text"]
                         for c, label in self.controls_and_labels(macros_tab)}
        self.assertIn("Env 1 Release", macros_labels)
        self.assertEqual(macros_labels["Env 1 Release"], "Env 1 Release")


SHELL_KEYS = {len(lay.PARAMS) + 4 + 32: "shell.pad_pressure", len(lay.PARAMS) + 4 + 32 + 1: "shell.pad_pressure_amount"}   # 45_pad_pressure.py: the plugin's own parameters at the end of the list


SHELL_NAMES = {"shell.pad_pressure": "Pad Pressure", "shell.pad_pressure_amount": "Pressure Amount"}


def base_key(tui, control):
    index = int(control["handle remapping"]["map"][0]["value"].split()[1])
    if index in SHELL_KEYS:
        return SHELL_KEYS[index]
    return lay.PARAMS[index]["key"]


class MissingMetaTest(unittest.TestCase):
    @unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
    def test_the_module_fails_without_meta(self):
        with tempfile.TemporaryDirectory() as tmp:
            skins = base.generate_skins(Path(tmp))
            (skins.parent / "skin-meta.json").unlink()
            extras = Path(tmp) / "extras"
            extras.mkdir()
            (extras / "85_names.py").write_text((base.REAL_EXTRAS / "85_names.py").read_text(encoding="utf-8"), encoding="utf-8")
            code, err = base.run_apply(skins, extras)
            self.assertEqual(code, 1)
            self.assertIn("85_names.py", err)
            self.assertIn("skin-meta.json", err)


if __name__ == "__main__":
    unittest.main()
