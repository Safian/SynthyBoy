#!/usr/bin/env python3
# SynthyBoy - tests for product_version.py.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = ROOT / "scripts"

import importlib.util

spec = importlib.util.spec_from_file_location("product_version", SCRIPTS_DIR / "product_version.py")
product_version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(product_version)


class TestProductVersion(unittest.TestCase):
    def test_version_conventions(self):
        vst2_int, four_part = product_version.parse_version_build("0.1.0", 1)
        self.assertEqual(vst2_int, 101)
        self.assertEqual(four_part, "0.1.0.1")

        vst2_int, four_part = product_version.parse_version_build("1.2.3", 4)
        self.assertEqual(vst2_int, 1234)
        self.assertEqual(four_part, "1.2.3.4")

    def test_load_repo_root(self):
        version_text, build, vst2_int, four_part = product_version.load(ROOT)
        self.assertEqual(version_text, "0.1.0")
        self.assertEqual(build, 1)
        self.assertEqual(vst2_int, 101)
        self.assertEqual(four_part, "0.1.0.1")

    def test_load_custom_manifest(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_root = Path(tmp_dir)
            (tmp_root / "plugin.json").write_text(
                json.dumps({"version": "1.2.3", "build": 4}),
                encoding="utf-8",
            )
            version_text, build, vst2_int, four_part = product_version.load(tmp_root)
            self.assertEqual(version_text, "1.2.3")
            self.assertEqual(build, 4)
            self.assertEqual(vst2_int, 1234)
            self.assertEqual(four_part, "1.2.3.4")


if __name__ == "__main__":
    unittest.main()
