# SynthyBoy - tests for loudness.py (BS.1770 K-weighting) and the level solver of measure-loudness.py
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import unittest
from pathlib import Path

try:
    import numpy as np
except ImportError:   # the interpreter of the CMake test (python3 -I) may not have numpy: the file is skipped, pytest with numpy runs it
    raise unittest.SkipTest("numpy is not installed")

SCRIPTS = Path(__file__).resolve().parent.parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ln = load("loudness", "loudness.py")
ml = load("measure_loudness", "measure-loudness.py")
SR = 44100


def sine(f, db, secs=5.0):
    t = np.arange(int(SR * secs)) / SR
    return 10 ** (db / 20) * np.sin(2 * np.pi * f * t)


class TestKWeighting(unittest.TestCase):
    def test_997_hz_reads_its_level(self):
        x = sine(997, -23.0)
        self.assertAlmostEqual(ln.integrated(x, x, SR), -23.0, delta=0.05)
        self.assertAlmostEqual(ln.loudness_measures(x, x, SR)["fast_max"], -23.0, delta=0.05)

    def test_low_frequencies_are_weighted_down_high_ones_up(self):
        low, high = sine(50, -23.0), sine(4000, -23.0)
        self.assertLess(ln.integrated(low, low, SR), -26.0)
        self.assertGreater(ln.integrated(high, high, SR), -20.5)

    def test_gate_drops_silence(self):
        x = np.concatenate([sine(997, -23.0, 3.0), np.zeros(SR * 6)])
        self.assertAlmostEqual(ln.integrated(x, x, SR), -23.0, delta=0.3)

    def test_silence_is_the_floor(self):
        z = np.zeros(SR * 2)
        self.assertEqual(ln.integrated(z, z, SR), ln.FLOOR)

    def test_short_note_is_louder_in_the_fast_window_than_in_the_short_term(self):
        x = np.concatenate([sine(997, -20.0, 0.1), np.zeros(SR * 3)])
        m = ln.loudness_measures(x, x, SR)
        self.assertGreater(m["fast_max"], m["short_term_max"] + 10.0)


class TestSolve(unittest.TestCase):
    def test_solve_moves_volume_by_the_distance_to_the_target(self):
        calib = {"measure": "fast_max", "target": -23.0, "gesture_offset_db": {"Bass": 5.0}}
        res = ml.solve({"A": {"category": "Bass", "fast_max": -26.0}}, calib, {"A": -10.0})
        self.assertEqual(res["A"][0], -12.0)

    def test_solve_clamps_to_the_range_of_master_volume(self):
        calib = {"measure": "fast_max", "target": -23.0, "gesture_offset_db": {"Bass": 0.0}}
        res = ml.solve({"A": {"category": "Bass", "fast_max": -50.0}}, calib, {"A": 0.0})
        self.assertEqual(res["A"][0], 6.0)
        self.assertTrue(res["A"][2])


if __name__ == "__main__":
    unittest.main()
