#!/usr/bin/env python3
# SynthyBoy - tests for assign-preset-categories.py
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPTS = HERE.parent
REPO = SCRIPTS.parent
PRESETS_DIR = REPO / "content" / "presets"
DATA_FILE = SCRIPTS / "factory-preset-categories.json"

spec = importlib.util.spec_from_file_location("assign_preset_categories", SCRIPTS / "assign-preset-categories.py")
apc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apc)


class TestCheckRealData(unittest.TestCase):
    def test_real_data_passes_all_checks(self):
        success, errors = apc.check_categories(PRESETS_DIR, DATA_FILE)
        self.assertTrue(success, f"Validation failed with errors: {errors}")
        self.assertEqual(errors, [])

    def test_real_data_has_at_most_16_groups_all_with_min_4_presets(self):
        data = apc.load_category_data(DATA_FILE)
        groups = data.get("groups", [])
        presets = data.get("presets", {})
        self.assertLessEqual(len(groups), apc.MAX_GROUPS)
        self.assertEqual(len(presets), 160)

        counts = {g: 0 for g in groups}
        for name, entry in presets.items():
            grp = entry["group"]
            self.assertIn(grp, counts, f"Unknown group '{grp}' for preset '{name}'")
            counts[grp] += 1
            self.assertTrue(entry.get("why", "").strip(), f"Empty 'why' for preset '{name}'")

        for g, c in counts.items():
            self.assertGreaterEqual(
                c, apc.MIN_PRESETS_PER_GROUP,
                f"Group '{g}' has {c} presets (< {apc.MIN_PRESETS_PER_GROUP})"
            )


class TestCheckFailureCases(unittest.TestCase):
    def setUp(self):
        self.base_data = apc.load_category_data(DATA_FILE)

    def _run_check_with_data(self, data_dict):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(data_dict, f)
            temp_path = Path(f.name)
        try:
            return apc.check_categories(PRESETS_DIR, temp_path)
        finally:
            temp_path.unlink(missing_ok=True)

    def test_fails_when_preset_missing_from_data(self):
        corrupted = copy.deepcopy(self.base_data)
        del corrupted["presets"]["808 Boom"]
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any("808 Boom" in e and "no category entry" in e for e in errors))

    def test_fails_when_extra_preset_in_data(self):
        corrupted = copy.deepcopy(self.base_data)
        corrupted["presets"]["NonExistent Super Sound"] = {
            "group": "Bass Synth",
            "why": "Invented test preset",
        }
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any("NonExistent Super Sound" in e for e in errors))

    def test_fails_when_assigned_to_unknown_group(self):
        corrupted = copy.deepcopy(self.base_data)
        corrupted["presets"]["808 Boom"]["group"] = "Invented Group"
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any("unknown group 'Invented Group'" in e for e in errors))

    def test_fails_when_group_has_fewer_than_4_presets(self):
        corrupted = copy.deepcopy(self.base_data)
        corrupted["groups"].append("Tiny Group")
        # Assign only 1 preset to Tiny Group
        corrupted["presets"]["808 Boom"]["group"] = "Tiny Group"
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any("Tiny Group" in e and "fewer than minimum" in e for e in errors))

    def test_fails_when_group_name_contains_slash(self):
        corrupted = copy.deepcopy(self.base_data)
        corrupted["groups"].append("Bass Sub/808")
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any("forbidden character '/'" in e for e in errors))

    def test_fails_when_more_than_16_groups(self):
        corrupted = copy.deepcopy(self.base_data)
        while len(corrupted["groups"]) <= apc.MAX_GROUPS:
            corrupted["groups"].append(f"Extra Group {len(corrupted['groups'])}")
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any(f"exceeds maximum of {apc.MAX_GROUPS}" in e for e in errors))

    def test_fails_when_why_is_empty(self):
        corrupted = copy.deepcopy(self.base_data)
        corrupted["presets"]["808 Boom"]["why"] = "   "
        success, errors = self._run_check_with_data(corrupted)
        self.assertFalse(success)
        self.assertTrue(any("808 Boom" in e and "missing 'why'" in e for e in errors))


class TestSuggestRules(unittest.TestCase):
    def test_suggests_bass_sub_808(self):
        grp, why = apc.suggest_category("Bass", "Test Sub Bass", "sub,sine", "Deep sub", {})
        self.assertEqual(grp, "Bass Sub+808")

    def test_suggests_bass_reese_for_unison_saw(self):
        grp, why = apc.suggest_category("Bass", "Detuned Monster", "detune,saw", "Detuned bass", {"osc1.unison.voices": 5})
        self.assertEqual(grp, "Bass Reese")

    def test_suggests_bass_acid(self):
        grp, why = apc.suggest_category("Bass", "Silver 303 Line", "acid,tb303", "Classic acid", {})
        self.assertEqual(grp, "Bass Acid")

    def test_suggests_lead_sync_fm(self):
        grp, why = apc.suggest_category("Lead", "FM Cutter", "fm,digital", "Crisp FM lead", {})
        self.assertEqual(grp, "Lead Sync+FM")

        grp, why = apc.suggest_category("Lead", "Sync Screamer", "sync,saw", "Hard sync lead", {})
        self.assertEqual(grp, "Lead Sync+FM")

    def test_suggests_bells_mallets(self):
        grp, why = apc.suggest_category("Pluck", "Crystal Celesta", "celesta,bell", "Celesta bell strike", {})
        self.assertEqual(grp, "Bells+Mallets")

        grp, why = apc.suggest_category("Keys", "Vibraphone Melody", "vibraphone,jazz", "Struck bars", {})
        self.assertEqual(grp, "Bells+Mallets")

    def test_suggests_strings_brass(self):
        grp, why = apc.suggest_category("Pad", "Bowed Ensemble", "bowed,strings", "Expressive strings", {})
        self.assertEqual(grp, "Strings+Brass")

    def test_suggests_sequence_for_arp_or_step(self):
        grp, why = apc.suggest_category("Pluck", "Pentatonic Runner", "sequence,step", "Arpeggiated pluck", {})
        self.assertEqual(grp, "Sequence")


class TestCommandLineInterface(unittest.TestCase):
    def test_cli_check_returns_zero(self):
        res = subprocess.run(
            [sys.executable, str(SCRIPTS / "assign-preset-categories.py"), "--check"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(res.returncode, 0, f"CLI --check failed:\n{res.stderr}")
        self.assertIn("check-preset-categories: OK", res.stdout)

    def test_cli_suggest_all_runs_without_error(self):
        res = subprocess.run(
            [sys.executable, str(SCRIPTS / "assign-preset-categories.py"), "--suggest", "--all"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(res.returncode, 0)
        self.assertIn("Category suggestions for 160 presets:", res.stdout)


if __name__ == "__main__":
    unittest.main()
