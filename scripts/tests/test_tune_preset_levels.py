#!/usr/bin/env python3
# SynthyBoy - tests for tune-preset-levels.py (sustain-aware level normalisation)
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "tune-preset-levels.py"

spec = importlib.util.spec_from_file_location("tune_preset_levels", SCRIPT)
tpl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tpl)


class TestSustainHoldSeconds(unittest.TestCase):
    def test_minimum_hold_is_one_second(self):
        # Fast attack pluck / bass: 5 ms attack
        params = {"env1.attack": 5.0, "env1.delay": 0.0}
        self.assertEqual(tpl.sustain_hold_seconds(params), 1.0)

    def test_slow_attack_pad_holds_until_sustain(self):
        # Pad with 2.5 s attack (2500 ms): hold is attack + 0.4 s = 2.9 s
        params = {"env1.attack": 2500.0, "env1.delay": 0.0}
        self.assertAlmostEqual(tpl.sustain_hold_seconds(params), 2.9, places=3)

    def test_delay_added_to_attack(self):
        # 2 s attack + 1 s delay: 3 s + 0.4 s = 3.4 s
        params = {"env1.attack": 2000.0, "env1.delay": 1000.0}
        self.assertAlmostEqual(tpl.sustain_hold_seconds(params), 3.4, places=3)

    def test_maximum_cap_at_five_seconds(self):
        # 6 s attack: 6.4 s -> capped at 5.0 s
        params = {"env1.attack": 6000.0, "env1.delay": 0.0}
        self.assertEqual(tpl.sustain_hold_seconds(params), 5.0)


class TestPinnedPresets(unittest.TestCase):
    def test_pinned_112_preset_names(self):
        pinned = tpl.pinned_112_preset_names()
        self.assertEqual(len(pinned), 112)
        self.assertIn("Acid Saw Reso", pinned)
        self.assertIn("808 Boom", pinned)
        # Sound set batch 1 presets must NOT be in the pinned 112
        self.assertNotIn("Subterranean Duo", pinned)
        self.assertNotIn("Stretched Glass Choir", pinned)


class TestTuneLevelsRound(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.presets_dir = Path(self.tmp.name)
        (self.presets_dir / "Pad").mkdir(parents=True)
        (self.presets_dir / "Bass").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def _write_preset(self, cat: str, fname: str, name: str, vol: float, attack_ms: float = 2000.0):
        doc = {
            "format": "synthyboy-preset",
            "version": 1,
            "meta": {"name": name, "category": cat},
            "state": {
                "format": "synthyboy-state",
                "version": 4,
                "params": {"master.volume": vol, "env1.attack": attack_ms, "env1.delay": 0.0},
            },
        }
        (self.presets_dir / cat / fname).write_text(json.dumps(doc), encoding="utf-8")

    def test_pinned_112_preset_keeps_old_measure_when_sustain_aware_is_on(self):
        pinned_name = "Acid Saw Reso"
        pinned_set = {pinned_name}
        self._write_preset("Bass", "acid_saw_reso.sbpreset", pinned_name, vol=-12.0)

        # Old measure: chord_rms = -20.0 (already on target, delta 0)
        rows_old = {
            "acid_saw_reso.sbpreset": {"category": "Bass", "peak": -5.0, "chord_peak": -4.0, "rms": -22.0, "chord_rms": -20.0}
        }
        # Sustain-aware measure says chord is louder at sustain (-15.0)
        rows_sustain = {
            "acid_saw_reso.sbpreset": {"category": "Bass", "peak": -5.0, "chord_peak": -4.0, "rms": -22.0, "chord_rms": -15.0}
        }
        low = dict(rows_old)
        low_sustain = dict(rows_sustain)

        levels = {pinned_name: -12.0}
        changed, worst = tpl.tune_levels_round(
            rows=rows_old,
            low=low,
            pinned_112=pinned_set,
            levels=levels,
            target=-20.0,
            sustain_aware=True,  # sustain-aware enabled
            presets_dir=str(self.presets_dir),
            rows_sustain=rows_sustain,
            low_sustain=low_sustain,
        )
        # Pinned preset keeps old measure: unchanged!
        self.assertEqual(changed, 0)
        self.assertEqual(levels[pinned_name], -12.0)

    def test_new_preset_uses_sustain_aware_measure_when_enabled(self):
        new_name = "Stretched Glass Choir"
        pinned_set = {"Acid Saw Reso"}  # new_name is NOT in pinned_set
        self._write_preset("Pad", "stretched_glass_choir.sbpreset", new_name, vol=-10.0, attack_ms=3000.0)

        # In old 1 s measure, slow pad had not reached sustain: chord_rms was measured too quiet (-28 dBFS)
        rows_old = {
            "stretched_glass_choir.sbpreset": {"category": "Pad", "peak": -8.0, "chord_peak": -8.0, "rms": -30.0, "chord_rms": -28.0}
        }
        # In sustain-aware measure (holding until 3.4 s), chord reached sustain (-20.0 dBFS)
        rows_sustain = {
            "stretched_glass_choir.sbpreset": {"category": "Pad", "peak": -5.0, "chord_peak": -5.0, "rms": -22.0, "chord_rms": -20.0}
        }
        low = dict(rows_old)
        low_sustain = dict(rows_sustain)

        # 1. Without sustain-aware option: new preset would use old measure and boost volume to -2.0 (+8 dB too loud!)
        levels_default = {new_name: -10.0}
        changed_def, _ = tpl.tune_levels_round(
            rows=rows_old,
            low=low,
            pinned_112=pinned_set,
            levels=levels_default,
            target=-20.0,
            sustain_aware=False,
            presets_dir=str(self.presets_dir),
            rows_sustain=rows_sustain,
            low_sustain=low_sustain,
        )
        self.assertEqual(changed_def, 1)
        self.assertAlmostEqual(levels_default[new_name], -2.0, places=1)

        # 2. With sustain-aware option: new preset uses sustain measure (-20.0), stays at -10.0 (delta 0)!
        levels_sustain = {new_name: -10.0}
        changed_sus, _ = tpl.tune_levels_round(
            rows=rows_old,
            low=low,
            pinned_112=pinned_set,
            levels=levels_sustain,
            target=-20.0,
            sustain_aware=True,
            presets_dir=str(self.presets_dir),
            rows_sustain=rows_sustain,
            low_sustain=low_sustain,
        )
        self.assertEqual(changed_sus, 0)
        self.assertEqual(levels_sustain[new_name], -10.0)


if __name__ == "__main__":
    unittest.main()
