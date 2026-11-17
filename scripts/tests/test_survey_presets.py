#!/usr/bin/env python3
# SynthyBoy - unit tests for scripts/survey-presets.py
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
TOOL_PATH = ROOT_DIR / "scripts" / "survey-presets.py"

MINIMAL_PARAMS = {
    "format": "synthyboy-params",
    "version": 1,
    "stateVersion": 4,
    "count": 20,
    "params": [
        {"key": "master.volume", "name": "Master Volume", "group": "Master", "default": -12.0, "min": -60.0, "max": 6.0, "unit": "dB"},
        {"key": "master.velocity_track", "name": "Master Velocity Tracking", "group": "Master", "default": 100.0, "min": 0.0, "max": 100.0, "unit": "%"},
        {"key": "master.voices", "name": "Master Voices", "group": "Master / Voicing", "default": 16.0, "min": 1.0, "max": 32.0, "unit": "none"},
        {"key": "master.legato", "name": "Master Legato", "group": "Master", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "master.mpe.on", "name": "MPE On", "group": "Master / MPE", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "master.tuning.scale", "name": "Tuning Scale", "group": "Master / Tuning", "default": 0.0, "min": 0.0, "max": 3.0, "choices": ["12-TET", "Pythagorean", "Just intonation", "User file"]},
        {"key": "master.scale.type", "name": "Scale Type", "group": "Master / Scale", "default": 0.0, "min": 0.0, "max": 19.0, "choices": ["Chromatic (off)", "Major", "Minor"]},
        {"key": "osc1.on", "name": "Osc 1 On", "group": "Osc 1", "default": 1.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "osc1.wavetable", "name": "Osc 1 Wavetable", "group": "Osc 1 / Wavetable", "default": 1.0, "min": 0.0, "max": 3.0, "choices": ["Sine", "Saw", "Square", "Triangle"]},
        {"key": "osc1.warp.type", "name": "Osc 1 Warp Type", "group": "Osc 1 / Warp", "default": 0.0, "min": 0.0, "max": 3.0, "choices": ["None", "Sync", "Formant", "Bend"]},
        {"key": "osc1.warp.amount", "name": "Osc 1 Warp Amount", "group": "Osc 1 / Warp", "default": 0.0, "min": 0.0, "max": 100.0, "unit": "%"},
        {"key": "osc1.unison.voices", "name": "Osc 1 Unison Voices", "group": "Osc 1 / Unison", "default": 1.0, "min": 1.0, "max": 16.0, "unit": "none"},
        {"key": "osc2.on", "name": "Osc 2 On", "group": "Osc 2", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "osc2.unison.detune", "name": "Osc 2 Unison Detune", "group": "Osc 2 / Unison", "default": 25.0, "min": 0.0, "max": 100.0, "unit": "%"},
        {"key": "osc3.on", "name": "Osc 3 On", "group": "Osc 3", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "sub.on", "name": "Sub On", "group": "Sub", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "sub.waveform", "name": "Sub Waveform", "group": "Sub", "default": 0.0, "min": 0.0, "max": 3.0, "choices": ["Sine", "Square", "Triangle", "Saw"]},
        {"key": "noise.on", "name": "Noise On", "group": "Noise", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "noise.type", "name": "Noise Type", "group": "Noise", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Noise", "Sample"]},
        {"key": "noise.sample", "name": "Noise Sample", "group": "Noise", "default": 0.0, "min": 0.0, "max": 2.0, "choices": ["White Noise", "Pink Noise", "Vinyl Clicks"]},
        {"key": "filter1.on", "name": "Filter 1 On", "group": "Filter 1", "default": 1.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "filter1.model", "name": "Filter 1 Model", "group": "Filter 1", "default": 0.0, "min": 0.0, "max": 3.0, "choices": ["Analog", "Ladder", "Digital", "Diode"]},
        {"key": "filter1.style", "name": "Filter 1 Style", "group": "Filter 1", "default": 0.0, "min": 0.0, "max": 2.0, "choices": ["12 dB", "24 dB", "Dual"]},
        {"key": "filter1.cutoff", "name": "Filter 1 Cutoff", "group": "Filter 1", "default": 20000.0, "min": 20.0, "max": 20000.0, "unit": "Hz"},
        {"key": "filter2.on", "name": "Filter 2 On", "group": "Filter 2", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "filter2.cutoff", "name": "Filter 2 Cutoff", "group": "Filter 2", "default": 20000.0, "min": 20.0, "max": 20000.0, "unit": "Hz"},
        {"key": "filter.routing", "name": "Filter Routing", "group": "Filters", "default": 0.0, "min": 0.0, "max": 2.0, "choices": ["Parallel", "Series 1 to 2", "Series 2 to 1"]},
        {"key": "chorus.on", "name": "Chorus On", "group": "FX / Chorus", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "delay.on", "name": "Delay On", "group": "FX / Delay", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "reverb.on", "name": "Reverb On", "group": "FX / Reverb", "default": 0.0, "min": 0.0, "max": 1.0, "choices": ["Off", "On"]},
        {"key": "reverb.mix", "name": "Reverb Mix", "group": "FX / Reverb", "default": 0.0, "min": 0.0, "max": 100.0, "unit": "%"},
        {"key": "env1.attack_curve", "name": "Env 1 Attack Curve", "group": "Envelope 1 / Curves", "default": 0.0, "min": -20.0, "max": 20.0, "unit": "none"},
        {"key": "lfo1.shape", "name": "LFO 1 Shape", "group": "LFO 1", "default": 0.0, "min": 0.0, "max": 3.0, "choices": ["Sine", "Triangle", "Saw Up", "Saw Down"]},
        {"key": "lfo1.sync_type", "name": "LFO 1 Sync Type", "group": "LFO 1", "default": 0.0, "min": 0.0, "max": 2.0, "choices": ["Retrigger", "Free-run", "Envelope"]},
        {"key": "fxi01.type", "name": "FX Slot 01 Type", "group": "FX / Slot 01", "default": 0.0, "min": 0.0, "max": 3.0, "choices": ["None", "Chorus", "Delay", "Reverb"]},
        {"key": "mod01.source", "name": "Mod 01 Source", "group": "Mod Matrix / Slot 01", "default": 0.0, "min": 0.0, "max": 57.0},
        {"key": "mod01.destination", "name": "Mod 01 Destination", "group": "Mod Matrix / Slot 01", "default": 0.0, "min": 0.0, "max": 1248.0},
        {"key": "mod01.amount", "name": "Mod 01 Amount", "group": "Mod Matrix / Slot 01", "default": 0.0, "min": -100.0, "max": 100.0},
        {"key": "mod01.bypass", "name": "Mod 01 Bypass", "group": "Mod Matrix / Slot 01", "default": 0.0, "min": 0.0, "max": 1.0},
        {"key": "mod02.source", "name": "Mod 02 Source", "group": "Mod Matrix / Slot 02", "default": 0.0, "min": 0.0, "max": 57.0},
        {"key": "mod02.destination", "name": "Mod 02 Destination", "group": "Mod Matrix / Slot 02", "default": 0.0, "min": 0.0, "max": 1248.0},
        {"key": "mod02.amount", "name": "Mod 02 Amount", "group": "Mod Matrix / Slot 02", "default": 0.0, "min": -100.0, "max": 100.0},
        {"key": "mod02.bypass", "name": "Mod 02 Bypass", "group": "Mod Matrix / Slot 02", "default": 0.0, "min": 0.0, "max": 1.0},
    ],
    "modSources": [
        {"choice": 1, "key": "env1", "name": "Env 1"},
        {"choice": 7, "key": "lfo1", "name": "LFO 1"},
        {"choice": 19, "key": "macro1", "name": "Macro 1"},
        {"choice": 20, "key": "macro2", "name": "Macro 2"},
        {"choice": 23, "key": "velocity", "name": "Velocity"},
        {"choice": 25, "key": "aftertouch", "name": "Aftertouch"},
        {"choice": 26, "key": "pitch_wheel", "name": "Pitch Wheel"},
        {"choice": 27, "key": "mod_wheel", "name": "Mod Wheel"},
        {"choice": 38, "key": "poly_aftertouch", "name": "Poly Aftertouch"},
        {"choice": 36, "key": "slide", "name": "Slide"},
    ],
    "modDestinations": [
        {"choice": 1, "key": "filter1.cutoff", "name": "Filter 1 Cutoff"},
        {"choice": 2, "key": "filter2.cutoff", "name": "Filter 2 Cutoff"},
        {"choice": 3, "key": "osc1.warp.amount", "name": "Osc 1 Warp Amount"},
        {"choice": 4, "key": "reverb.mix", "name": "Reverb Mix"},
    ],
}


def make_preset(name: str, category: str, params: dict) -> dict:
    return {
        "format": "synthyboy-preset",
        "version": 1,
        "meta": {"name": name, "category": category, "tags": "synth", "description": f"Test {name}"},
        "state": {"format": "synthyboy-state", "version": 4, "params": params},
    }


class TestSurveyPresets(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp_dir.name)
        self.params_file = self.d / "params.json"
        self.params_file.write_text(json.dumps(MINIMAL_PARAMS))
        self.presets_dir = self.d / "presets"
        self.presets_dir.mkdir(parents=True)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def write_preset(self, cat: str, fname: str, obj: dict) -> Path:
        cat_dir = self.presets_dir / cat
        cat_dir.mkdir(parents=True, exist_ok=True)
        p = cat_dir / fname
        p.write_text(json.dumps(obj))
        return p

    def run_tool(self, *args) -> subprocess.CompletedProcess:
        cmd = [sys.executable, str(TOOL_PATH), "--params", str(self.params_file)] + list(args)
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_empty_directory_returns_empty_results(self):
        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        self.assertEqual(data["summary"]["total_presets"], 0)

    def test_param_differences_from_default(self):
        # Preset 1: all defaults -> 0 differences
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {
            "master.volume": -12.0,
            "filter1.cutoff": 20000.0,
        }))
        # Preset 2: 2 differences from default
        self.write_preset("Bass", "p2.sbpreset", make_preset("P2", "Bass", {
            "master.volume": -6.0,  # diff 1
            "filter1.cutoff": 1000.0,  # diff 2
        }))
        # Preset 3: 3 differences from default in Lead
        self.write_preset("Lead", "p3.sbpreset", make_preset("P3", "Lead", {
            "master.volume": 0.0,  # diff 1
            "filter1.cutoff": 500.0,  # diff 2
            "osc1.wavetable": 2.0,  # diff 3
        }))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        p_diffs = data["parameter_differences"]

        self.assertEqual(p_diffs["overall"]["count"], 3)
        self.assertEqual(p_diffs["overall"]["min"], 0)
        self.assertEqual(p_diffs["overall"]["max"], 3)
        self.assertAlmostEqual(p_diffs["overall"]["mean"], (0 + 2 + 3) / 3, places=1)

        self.assertEqual(p_diffs["by_category"]["Bass"]["count"], 2)
        self.assertEqual(p_diffs["by_category"]["Bass"]["min"], 0)
        self.assertEqual(p_diffs["by_category"]["Bass"]["max"], 2)

        self.assertEqual(p_diffs["by_category"]["Lead"]["count"], 1)
        self.assertEqual(p_diffs["by_category"]["Lead"]["min"], 3)
        self.assertEqual(p_diffs["by_category"]["Lead"]["max"], 3)

    def test_missing_params_count_as_defaults(self):
        # A preset with only master.volume specified: all other parameters count as default
        self.write_preset("Bass", "missing.sbpreset", make_preset("Missing", "Bass", {
            "master.volume": -6.0
        }))
        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        self.assertEqual(data["parameter_differences"]["overall"]["min"], 1)
        self.assertEqual(data["parameter_differences"]["overall"]["max"], 1)

    def test_similarity_closest_pairs_and_clusters(self):
        # P1 & P2 differ by 2 params
        # P2 & P3 differ by 2 params (P1 & P3 differ by 4 params)
        # P4 differs from P1, P2, P3 by 20 params
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {
            "filter1.cutoff": 1000.0,
            "master.volume": -6.0,
        }))
        self.write_preset("Bass", "p2.sbpreset", make_preset("P2", "Bass", {
            "filter1.cutoff": 1000.0,
            "master.volume": -8.0,  # 1 diff from P1
            "osc1.wavetable": 2.0,  # 1 diff from P1 -> total 2 diffs from P1
        }))
        self.write_preset("Bass", "p3.sbpreset", make_preset("P3", "Bass", {
            "filter1.cutoff": 1000.0,
            "master.volume": -8.0,  # same as P2
            "osc1.wavetable": 3.0,  # 1 diff from P2
            "reverb.mix": 50.0,     # 1 diff from P2 -> total 2 diffs from P2
        }))
        # P4 with 20 diffs
        p4_params = {"p_dummy_" + str(i): float(i) for i in range(20)}
        self.write_preset("FX", "p4.sbpreset", make_preset("P4", "FX", p4_params))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        sim = data["similarity"]

        self.assertEqual(sim["total_pairs"], 6)  # 4 * 3 / 2
        closest = sim["closest_pairs"]
        self.assertEqual(closest[0]["diff_count"], 2)

        # Clusters N=15: P1, P2, P3 are connected (diff <= 15). P4 is far away (>15)
        c_15 = sim["clusters_n15"]
        self.assertEqual(len(c_15), 2)
        sizes_15 = sorted([c["size"] for c in c_15], reverse=True)
        self.assertEqual(sizes_15, [3, 1])

        # Clusters N=30: all 4 presets are in cluster (P4 has ~22 diffs from P1/P2/P3, so <= 30)
        c_30 = sim["clusters_n30"]
        self.assertEqual(len(c_30), 1)
        self.assertEqual(c_30[0]["size"], 4)

    def test_feature_use_and_unused_values(self):
        # Osc 1 wavetable has choices: ["Sine", "Saw", "Square", "Triangle"]
        # Preset 1 uses Saw (1)
        # Preset 2 uses Saw (1)
        # Unused: Sine (0), Square (2), Triangle (3)
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {
            "osc1.wavetable": 1.0,
            "filter1.model": 2.0,  # Digital
        }))
        self.write_preset("Lead", "p2.sbpreset", make_preset("P2", "Lead", {
            "osc1.wavetable": 1.0,
            "filter1.model": 2.0,  # Digital
        }))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        feat = data["feature_use"]
        osc_params = feat["kind_of_thing"]["oscillator_type_wavetable"]["params"]
        wt_info = osc_params["osc1.wavetable"]
        self.assertIn("Sine (idx 0)", wt_info["unused_values"])
        self.assertIn("Square (idx 2)", wt_info["unused_values"])
        self.assertIn("Triangle (idx 3)", wt_info["unused_values"])

        counts = {v["label"]: v["count"] for v in wt_info["value_counts"]}
        self.assertEqual(counts["Saw"], 2)
        self.assertEqual(counts["Sine"], 0)

    def test_modulation_matrix_and_macro_analysis(self):
        # Route 1: Macro 1 (19) -> Filter 1 Cutoff (1)
        # Route 2: LFO 1 (7) -> Filter 1 Cutoff (1)
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {
            "mod01.source": 19.0,
            "mod01.destination": 1.0,
            "mod01.amount": 40.0,
            "mod02.source": 7.0,
            "mod02.destination": 1.0,
            "mod02.amount": 25.0,
        }))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        mod_m = data["feature_use"]["modulation_matrix"]
        self.assertEqual(mod_m["total_routes"], 2)
        self.assertEqual(mod_m["used_sources_count"], 2)

        # Macro tracking
        macros = data["feature_use"]["macros"]
        m1 = macros["macro1"]
        self.assertEqual(m1["preset_count"], 1)
        self.assertEqual(m1["total_routes"], 1)
        self.assertEqual(m1["favoured_destinations"][0]["key"], "filter1.cutoff")

    def test_performance_response_detection(self):
        # P1: No performance routing, vel track 100
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {
            "master.velocity_track": 100.0,
        }))
        # P2: Poly aftertouch (38) and velocity track 80
        self.write_preset("Lead", "p2.sbpreset", make_preset("P2", "Lead", {
            "master.velocity_track": 80.0,
            "mod01.source": 38.0,  # Poly Aftertouch
            "mod01.destination": 1.0,
            "mod01.amount": 30.0,
        }))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        perf = data["performance_response"]

        self.assertEqual(perf["summary_counts"]["any_performance_source"], 1)
        self.assertEqual(perf["summary_counts"]["aftertouch_poly"], 1)
        self.assertEqual(perf["summary_counts"]["velocity"], 0)
        self.assertEqual(perf["velocity_track_distribution"]["80.0"], 1)
        self.assertEqual(perf["velocity_track_distribution"]["100.0"], 1)

    def test_housekeeping_disabled_module_and_zero_amount(self):
        # Route targeting filter2.cutoff (dest 2), but filter2.on is 0
        self.write_preset("Bass", "p_bad.sbpreset", make_preset("Bad", "Bass", {
            "filter2.on": 0.0,
            "mod01.source": 19.0,
            "mod01.destination": 2.0,  # filter2.cutoff
            "mod01.amount": 35.0,
            "mod01.bypass": 0.0,
            # Zero amount route on slot 2
            "mod02.source": 7.0,
            "mod02.destination": 1.0,
            "mod02.amount": 0.0,
        }))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        hk = data["housekeeping"]

        self.assertEqual(hk["disabled_module_routes_count"], 1)
        self.assertEqual(hk["disabled_module_routes"][0]["destination_key"], "filter2.cutoff")
        self.assertEqual(hk["zero_amount_routes_count"], 1)

    def test_levels_master_volume_distribution(self):
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {"master.volume": -10.0}))
        self.write_preset("Bass", "p2.sbpreset", make_preset("P2", "Bass", {"master.volume": -20.0}))
        self.write_preset("Lead", "p3.sbpreset", make_preset("P3", "Lead", {"master.volume": -6.0}))

        r = self.run_tool("--presets", str(self.presets_dir), "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        lvl = data["levels"]

        self.assertEqual(lvl["overall"]["count"], 3)
        self.assertEqual(lvl["overall"]["min"], -20.0)
        self.assertEqual(lvl["overall"]["max"], -6.0)
        self.assertAlmostEqual(lvl["overall"]["mean"], -12.0, places=1)
        self.assertEqual(lvl["by_category"]["Bass"]["count"], 2)
        self.assertEqual(lvl["by_category"]["Lead"]["count"], 1)

    def test_out_directory_creates_markdown_and_json(self):
        self.write_preset("Bass", "p1.sbpreset", make_preset("P1", "Bass", {}))
        out_dir = self.d / "out_test"
        r = self.run_tool("--presets", str(self.presets_dir), "--out", str(out_dir))
        self.assertEqual(r.returncode, 0, r.stderr)

        md_file = out_dir / "survey.md"
        json_file = out_dir / "survey.json"
        self.assertTrue(md_file.is_file(), "survey.md was not written")
        self.assertTrue(json_file.is_file(), "survey.json was not written")

        md_content = md_file.read_text(encoding="utf-8")
        self.assertIn("# SynthyBoy Factory Sound Set Survey", md_content)
        json_content = json.loads(json_file.read_text(encoding="utf-8"))
        self.assertEqual(json_content["summary"]["total_presets"], 1)

    def test_cli_missing_params_file_exits_2(self):
        cmd = [sys.executable, str(TOOL_PATH), "--params", "/nonexistent/params.json"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)
        self.assertIn("Error:", r.stderr)

    def test_run_on_actual_repository_presets(self):
        # Run tool directly on repo presets
        cmd = [sys.executable, str(TOOL_PATH), "--presets", str(ROOT_DIR / "content" / "presets"), "--params", str(ROOT_DIR / "docs" / "params.json"), "--json"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        data = json.loads(r.stdout)
        self.assertEqual(data["summary"]["total_presets"], 112)
        self.assertEqual(data["housekeeping"]["disabled_module_routes_count"], 79)
        self.assertEqual(data["similarity"]["total_pairs"], 6216)


if __name__ == "__main__":
    unittest.main()
