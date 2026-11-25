#!/usr/bin/env python3
# SynthyBoy - tests of scripts/check-mpc-envelope-curvature.py.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import math
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import importlib.util

spec = importlib.util.spec_from_file_location(
    "check_mpc_envelope_curvature", ROOT / "scripts" / "check-mpc-envelope-curvature.py"
)
check_mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_mod)

SDK = Path(os.environ.get("MPC_SDK") or Path.home() / ".local/share/local-worker/ext/mpc-sdk")


class EnvelopeCurvatureTest(unittest.TestCase):
    def test_sdk_docs_quadratic_formula_not_documented(self):
        """Verifies that the MPC SDK ui-reference.md mentions Quadratic curveType but gives no formula."""
        doc_info = check_mod.check_sdk_documentation(SDK)
        if doc_info["found_doc"]:
            self.assertTrue(doc_info["has_curvetype_mention"], "ui-reference.md must mention curveType/Quadratic")
            self.assertFalse(doc_info["documented"], "Quadratic curve formula is not documented in MPC SDK")
            self.assertIn("Nincs dokumentálva", doc_info["message"])

    def test_param_table_curve_definitions(self):
        """Verifies that core/src/param_table.cpp defines curves in [-20, 20] with expected defaults."""
        param_table = ROOT / "core" / "src" / "param_table.cpp"
        self.assertTrue(param_table.is_file(), "param_table.cpp must exist")
        content = param_table.read_text(encoding="utf-8")

        # Check definitions
        # cont ("env" #N ".attack_curve", ..., -20.0f, 20.0f, 0.0f, ...)
        # cont ("env" #N ".decay_curve", ..., -20.0f, 20.0f, -3.0f, ...)
        # cont ("env" #N ".release_curve", ..., -20.0f, 20.0f, -3.0f, ...)
        m_att = re.search(r'attack_curve.*?-20\.0f,\s*20\.0f,\s*([0-9.-]+)f', content)
        m_dec = re.search(r'decay_curve.*?-20\.0f,\s*20\.0f,\s*([0-9.-]+)f', content)
        m_rel = re.search(r'release_curve.*?-20\.0f,\s*20\.0f,\s*([0-9.-]+)f', content)

        self.assertIsNotNone(m_att, "attack_curve definition found")
        self.assertIsNotNone(m_dec, "decay_curve definition found")
        self.assertIsNotNone(m_rel, "release_curve definition found")

        self.assertAlmostEqual(float(m_att.group(1)), 0.0, places=3)
        self.assertAlmostEqual(float(m_dec.group(1)), -3.0, places=3)
        self.assertAlmostEqual(float(m_rel.group(1)), -3.0, places=3)

    def test_attack_curvature_deviation_at_default(self):
        """At default attack_curve=0.0, both engine and host are linear, max deviation is 0.0."""
        res = check_mod.measure_stage(
            "attack", 0.0, check_mod.engine_attack, check_mod.host_quadratic_attack, check_mod.host_linear_attack
        )
        self.assertAlmostEqual(res["max_diff_bezier"], 0.0, places=5)
        self.assertAlmostEqual(res["max_diff_linear"], 0.0, places=5)

    def test_decay_curvature_deviation_at_default(self):
        """At default decay_curve=-3.0, deviation is bounded by threshold (~0.3006 Bézier, ~0.3358 linear)."""
        res = check_mod.measure_stage(
            "decay", -3.0, check_mod.engine_decay, check_mod.host_quadratic_decay, check_mod.host_linear_decay
        )
        # Threshold bounds:
        self.assertLessEqual(res["max_diff_bezier"], 0.31)
        self.assertGreaterEqual(res["max_diff_bezier"], 0.29)
        self.assertAlmostEqual(res["max_diff_bezier"], 0.3006, places=3)

        self.assertLessEqual(res["max_diff_linear"], 0.35)
        self.assertGreaterEqual(res["max_diff_linear"], 0.32)
        self.assertAlmostEqual(res["max_diff_linear"], 0.3358, places=3)

    def test_release_curvature_deviation_at_default(self):
        """At default release_curve=-3.0, deviation is bounded by threshold (~0.3006 Bézier, ~0.3358 linear)."""
        res = check_mod.measure_stage(
            "release", -3.0, check_mod.engine_release, check_mod.host_quadratic_release, check_mod.host_linear_release
        )
        self.assertLessEqual(res["max_diff_bezier"], 0.31)
        self.assertGreaterEqual(res["max_diff_bezier"], 0.29)
        self.assertAlmostEqual(res["max_diff_bezier"], 0.3006, places=3)

        self.assertLessEqual(res["max_diff_linear"], 0.35)
        self.assertGreaterEqual(res["max_diff_linear"], 0.32)
        self.assertAlmostEqual(res["max_diff_linear"], 0.3358, places=3)

    def test_deviations_bounded_across_full_range(self):
        """Tests that deviations stay in [0, 1] across parameter range [-20, 20]."""
        test_curves = [-20.0, -10.0, -5.0, -3.0, 0.0, 3.0, 5.0, 10.0, 20.0]
        for cur in test_curves:
            res_att = check_mod.measure_stage(
                "attack", cur, check_mod.engine_attack, check_mod.host_quadratic_attack, check_mod.host_linear_attack
            )
            res_dec = check_mod.measure_stage(
                "decay", cur, check_mod.engine_decay, check_mod.host_quadratic_decay, check_mod.host_linear_decay
            )
            res_rel = check_mod.measure_stage(
                "release", cur, check_mod.engine_release, check_mod.host_quadratic_release, check_mod.host_linear_release
            )

            for r in (res_att, res_dec, res_rel):
                self.assertGreaterEqual(r["max_diff_bezier"], 0.0)
                self.assertLessEqual(r["max_diff_bezier"], 1.0)
                self.assertGreaterEqual(r["max_diff_linear"], 0.0)
                self.assertLessEqual(r["max_diff_linear"], 1.0)

            if abs(cur) < 1e-4:
                self.assertAlmostEqual(res_att["max_diff_bezier"], 0.0, places=5)
                self.assertAlmostEqual(res_dec["max_diff_bezier"], 0.0, places=5)
                self.assertAlmostEqual(res_rel["max_diff_bezier"], 0.0, places=5)

    def test_cli_execution_and_json(self):
        """Runs the CLI script and verifies return code and JSON output structure."""
        script_path = ROOT / "scripts" / "check-mpc-envelope-curvature.py"
        r = subprocess.run([sys.executable, str(script_path), "--json"], capture_output=True, text=True, check=True)
        self.assertEqual(r.returncode, 0)

        data = json.loads(r.stdout)
        self.assertIn("sdk_documentation", data)
        self.assertIn("stages", data)
        self.assertIn("attack", data["stages"])
        self.assertIn("decay", data["stages"])
        self.assertIn("release", data["stages"])

        self.assertAlmostEqual(data["stages"]["attack"]["max_diff_bezier"], 0.0, places=4)
        self.assertAlmostEqual(data["stages"]["decay"]["max_diff_bezier"], 0.3006, places=3)
        self.assertAlmostEqual(data["stages"]["release"]["max_diff_bezier"], 0.3006, places=3)


if __name__ == "__main__":
    unittest.main()
