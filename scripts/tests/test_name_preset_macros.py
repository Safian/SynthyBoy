#!/usr/bin/env python3
# SynthyBoy - tests of scripts/name-preset-macros.py (names for the macros of the factory presets, derived from their targets) and of the names in the factory presets.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "name-preset-macros.py"
GEN = ROOT / "scripts" / "gen-factory-presets.py"
PRESETS = ROOT / "content" / "presets"

spec = importlib.util.spec_from_file_location("name_preset_macros", SCRIPT)
npm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(npm)
DOC, PARAMS = npm.load_params()


def run(*args, cwd=None):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), *args], capture_output=True, text=True, cwd=cwd or tempfile.gettempdir())


def name(*keys):
    return npm.combine(list(keys), PARAMS, 1)


class ShortNames(unittest.TestCase):
    def test_the_overrides_are_parameters_of_the_table(self):
        for key in npm.SHORT_NAMES:
            self.assertIn(key, PARAMS, key)

    def test_the_names_of_the_examples(self):
        for key, short in {"filter1.cutoff": "Cutoff", "filter2.cutoff": "Cutoff", "filter1.resonance": "Reso", "filter1.drive": "Drive", "distortion.drive": "Drive",
                           "osc1.warp.amount": "Warp", "osc2.warp.amount": "Warp", "osc1.unison.detune": "Detune", "lfo1.rate": "LFO Rate", "env1.release": "Release",
                           "reverb.mix": "Reverb", "osc1.unison.spread": "Spread", "osc1.morph.amount": "Morph", "noise.level": "Noise"}.items():
            self.assertEqual(npm.short_name(key, PARAMS), short, key)

    def test_every_parameter_has_a_short_name_that_fits(self):
        for key in PARAMS:
            s = npm.short_name(key, PARAMS)
            self.assertTrue(s.strip() and s == " ".join(s.split()), key)
        # the ones the factory presets use fit a name with room for a second one
        for key in {k for name, info in npm.derive().items() for t in info["targets"].values() for k, _ in t}:
            self.assertLessEqual(len(npm.short_name(key, PARAMS)), 12, key)


class Combine(unittest.TestCase):
    def test_one_target_or_one_shared_name(self):
        self.assertEqual(name("filter1.cutoff"), "Cutoff")
        self.assertEqual(name("filter1.cutoff", "filter2.cutoff"), "Cutoff")      # the same short name twice
        self.assertEqual(name("filter1.cutoff", "filter1.cutoff"), "Cutoff")      # the same parameter twice
        self.assertEqual(name(), "")

    def test_two_targets(self):
        self.assertEqual(name("filter1.cutoff", "filter1.resonance"), "Cutoff + Reso")
        self.assertEqual(name("reverb.mix", "reverb.decay"), "Reverb")                      # "Reverb + Rev Decay" is 18: the module both belong to
        self.assertEqual(name("reverb.mix", "delay.feedback"), "Reverb + Delay")            # "Reverb + Feedback" is 17: the two modules
        self.assertEqual(name("lfo1.rate", "phaser.mix"), "LFO + Phaser")
        self.assertEqual(name("rlfo1.rate", "lfo1.rate"), "LFO")                            # one module
        self.assertEqual(name("osc1.morph.amount", "noise.level"), "Morph + Noise")

    def test_two_targets_never_longer_than_the_limit(self):
        keys = list(PARAMS)[::37]
        for a in keys:
            for b in keys:
                self.assertLessEqual(len(npm.combine([a, b], PARAMS, 1)), npm.MAX_CHARS, (a, b))

    def test_three_or_more(self):
        self.assertEqual(name("filter1.drive", "distortion.drive", "osc1.warp.amount", "distortion.mix"), "Drive")     # the short name most of them share
        self.assertEqual(name("reverb.mix", "reverb.decay", "chorus.mix"), "Reverb + Chorus")                           # two modules
        self.assertEqual(name("osc1.frame", "osc1.warp.amount", "osc1.morph.amount"), "Osc")                           # one module
        self.assertEqual(name("filter1.cutoff", "fxfilter.cutoff", "fxfilter.mix"), "FX Filter")                       # the module most of them belong to
        self.assertEqual(name("filter1.cutoff", "osc1.warp.amount", "reverb.mix"), "")                                 # nothing fits: the default name

    def test_the_limit_is_the_one_of_the_format(self):
        header = (ROOT / "core" / "include" / "synthyboy" / "content_state.h").read_text(encoding="utf-8")
        self.assertIn(f"kMaxMacroNameChars = {npm.MAX_CHARS};", header)


class Targets(unittest.TestCase):
    def test_only_slots_that_drive_something_count(self):
        src = {s["key"]: s["choice"] for s in DOC["modSources"]}
        dst = {d["key"]: d["choice"] for d in DOC["modDestinations"]}
        state = {
            "mod01.source": src["macro3"], "mod01.destination": dst["filter1.cutoff"], "mod01.amount": 40,
            "mod02.source": src["macro3"], "mod02.destination": dst["filter1.resonance"], "mod02.amount": 0,                     # no amount
            "mod03.source": src["macro3"], "mod03.destination": dst["filter1.drive"], "mod03.amount": 10, "mod03.bypass": 1,     # bypassed
            "mod04.source": src["lfo1"], "mod04.destination": dst["filter1.drive"], "mod04.amount": 10,                          # not a macro
            "mod05.source": src["macro8"], "mod05.destination": dst["reverb.mix"], "mod05.amount": -20,
            "mod06.source": src["macro8"], "mod06.destination": 0, "mod06.amount": 20,                                           # no destination
        }
        self.assertEqual(npm.macro_targets(state, DOC), {3: [("filter1.cutoff", 40.0)], 8: [("reverb.mix", -20.0)]})

    def test_aux_source_counts_as_macro_target_and_generates_sensible_name(self):
        src = {s["key"]: s["choice"] for s in DOC["modSources"]}
        dst = {d["key"]: d["choice"] for d in DOC["modDestinations"]}
        # LFO 1 -> filter1.cutoff scaled by Macro 2 as aux source
        state = {
            "mod01.source": src["lfo1"],
            "mod01.destination": dst["filter1.cutoff"],
            "mod01.amount": 50,
            "mod01.aux_source": src["macro2"],
        }
        targets = npm.macro_targets(state, DOC)
        self.assertIn(2, targets)
        self.assertEqual(targets[2], [("filter1.cutoff", 50.0)])
        derived_name = npm.combine([k for k, _ in targets[2]], PARAMS, 2)
        self.assertEqual(derived_name, "Cutoff")

    def test_macro_driving_route_amount_resolves_sensibly(self):
        src = {s["key"]: s["choice"] for s in DOC["modSources"]}
        dst = {d["key"]: d["choice"] for d in DOC["modDestinations"]}
        # Slot 1: LFO -> Cutoff. Slot 2: Macro 1 -> mod01.amount
        state = {
            "mod01.source": src["lfo1"],
            "mod01.destination": dst["filter1.cutoff"],
            "mod01.amount": 50,
            "mod02.source": src["macro1"],
            "mod02.destination": dst["mod01.amount"],
            "mod02.amount": 30,
        }
        targets = npm.macro_targets(state, DOC)
        self.assertIn(1, targets)
        self.assertEqual(targets[1], [("filter1.cutoff", 30.0)])
        derived_name = npm.combine([k for k, _ in targets[1]], PARAMS, 1)
        self.assertEqual(derived_name, "Cutoff")


class FactoryPresets(unittest.TestCase):
    def test_the_names_are_in_the_presets_and_follow_the_rules(self):
        names = npm.load_names(npm.NAMES_FILE)
        named = 0
        for rel, preset, state in npm.read_presets(PRESETS):
            with open(PRESETS / rel, "r", encoding="utf-8") as f:
                doc = json.load(f)
            self.assertEqual(doc["state"]["version"], 4, rel)                                    # the state version did not change
            written = doc["state"].get("content", {}).get("macro_names")
            self.assertIn(preset, names, rel)
            targets = npm.macro_targets(state, DOC)
            for m in range(1, 9):
                n = names[preset][m - 1]
                if m not in targets:
                    self.assertEqual(n, "", f"{rel}: macro {m} has no target")
                self.assertLessEqual(len(n), npm.MAX_CHARS, rel)
                self.assertNotEqual(n, f"Macro {m}", rel)
                self.assertEqual(n, " ".join(n.split()), rel)
            if any(names[preset]):
                self.assertEqual(written, names[preset], rel)
                named += 1
            else:
                self.assertIsNone(written, rel)
        self.assertGreaterEqual(named, 100)

    def test_every_macro_with_targets_is_named_unless_nothing_fits(self):
        names = npm.load_names(npm.NAMES_FILE)
        derived = npm.derive()
        for preset, info in derived.items():
            for m, t in info["targets"].items():
                if names[preset][m - 1] == "":
                    self.assertEqual(info["names"][m - 1], "", f"{preset} macro {m}: unnamed but a name was derived")

    def test_check_passes(self):
        r = run("--check")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_the_generator_reproduces_the_presets(self):
        r = subprocess.run([sys.executable, "-I", str(GEN), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        shutil.copytree(PRESETS / "Bass", self.dir / "presets" / "Bass")
        for f in list((self.dir / "presets" / "Bass").glob("*.sbpreset"))[3:]:
            f.unlink()
        self.names = self.dir / "names.json"

    def tearDown(self):
        self.tmp.cleanup()

    def test_derive_keeps_what_was_corrected_by_hand(self):
        first = run("--presets", str(self.dir / "presets"), "--names", str(self.names), "--proposal", str(self.dir / "proposal.md"))
        self.assertEqual(first.returncode, 0, first.stderr)
        doc = json.loads(self.names.read_text(encoding="utf-8"))
        self.assertEqual(doc["format"], npm.FORMAT)
        some = next(iter(doc["presets"]))
        self.assertEqual(doc["presets"][some][0], "Cutoff")
        doc["presets"][some][0] = "My Own"
        self.names.write_text(json.dumps(doc), encoding="utf-8")
        again = run("--presets", str(self.dir / "presets"), "--names", str(self.names))
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(json.loads(self.names.read_text(encoding="utf-8"))["presets"][some][0], "My Own")      # kept
        run("--presets", str(self.dir / "presets"), "--names", str(self.names), "--rederive")
        self.assertEqual(json.loads(self.names.read_text(encoding="utf-8"))["presets"][some][0], "Cutoff")      # derived again on request
        text = (self.dir / "proposal.md").read_text(encoding="utf-8")
        self.assertIn("| Preset | Macro | Targets | Name |", text)
        self.assertIn("Filter 1 Cutoff (+45%)", text)

    def test_check_finds_a_name_on_a_macro_without_target(self):
        run("--presets", str(self.dir / "presets"), "--names", str(self.names))
        doc = json.loads(self.names.read_text(encoding="utf-8"))
        some = next(iter(doc["presets"]))
        doc["presets"][some][6] = "Nothing"        # macro 7 has no target in these presets
        self.names.write_text(json.dumps(doc), encoding="utf-8")
        bad = run("--check", "--presets", str(self.dir / "presets"), "--names", str(self.names))
        self.assertEqual(bad.returncode, 1)
        self.assertIn("has no target", bad.stdout)


class GeneratorRules(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location("gen_factory_presets", GEN)
        self.gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.gen)

    def test_names_are_written_as_the_content_member(self):
        text = self.gen.content_text({"macro_names": ["Cutoff", "", "", "", "", "", "", ""]})
        self.assertEqual(text, ',"content":{"macro_names":["Cutoff","","","","","","",""]}')
        self.assertEqual(self.gen.content_text({"macro_names": [""] * 8}), "")
        shapes = self.gen.content_text({"lfo_shapes": [{"lfo": 1, "name": "x", "points": [(0, 0), (1, 1)]}], "macro_names": ["A"] + [""] * 7})
        self.assertTrue(shapes.index("lfo_shapes") < shapes.index("macro_names"))

    def test_a_name_outside_the_rules_is_an_error(self):
        for bad in ["Macro 3", " a", "a  b", "a\tb", "x" * 17, "ä"]:
            with self.assertRaises(ValueError, msg=repr(bad)):
                self.gen.check_macro_names("p", ["", "", bad, "", "", "", "", ""])
        with self.assertRaises(ValueError):
            self.gen.check_macro_names("p", ["a"])


if __name__ == "__main__":
    unittest.main()
