#!/usr/bin/env python3
# SynthyBoy - tests for scripts/mpc-fix-xpl-paths.py
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPTS = HERE.parent
REPO = SCRIPTS.parent
FIX_PATHS_SCRIPT = SCRIPTS / "mpc-fix-xpl-paths.py"
GEN_MPC_XPL_SCRIPT = SCRIPTS / "gen-mpc-xpl.py"
OBSERVED_XL = HERE / "data" / "synthyboy-host-saved-Space-Wobble-Bass.xpl"

# Import mpc-fix-xpl-paths dynamically
spec = importlib.util.spec_from_file_location("mpc_fix_xpl_paths", FIX_PATHS_SCRIPT)
fixer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixer)

# Import gen-mpc-xpl for validation
spec_gen = importlib.util.spec_from_file_location("gen_mpc_xpl", GEN_MPC_XPL_SCRIPT)
gen_xpl = importlib.util.module_from_spec(spec_gen)
spec_gen.loader.exec_module(gen_xpl)

PATH_XL = "/nvme/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm64.so"
PATH_K61 = "/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so"
PATH_SHARED = "/custom/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy.so"


class TestMpcFixXplPaths(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="sb-test-fix-paths-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_rewrite_content_preserves_crlf_and_structure(self):
        original_bytes = OBSERVED_XL.read_bytes()
        original_text = original_bytes.decode("utf-8")
        self.assertIn(PATH_XL, original_text)

        new_text, changed = fixer.rewrite_xpl_content(original_text, PATH_K61)
        self.assertTrue(changed)
        self.assertIn(PATH_K61, new_text)
        self.assertNotIn(PATH_XL, new_text)

        # Byte-level checks: CRLF intact, no bare LF introduced
        new_bytes = new_text.encode("utf-8")
        self.assertNotIn(b"\n", new_bytes.replace(b"\r\n", b""))

        # Validating parsed structure matches original except for the plugin path
        orig_parsed = gen_xpl.parse_xpl(original_text)
        new_parsed = gen_xpl.parse_xpl(new_text)

        self.assertEqual(orig_parsed["preset"], new_parsed["preset"])
        self.assertEqual(orig_parsed["version_file"], new_parsed["version_file"])
        self.assertEqual(orig_parsed["version_date"], new_parsed["version_date"])
        self.assertEqual(orig_parsed["state_bytes"], new_parsed["state_bytes"])
        self.assertEqual(orig_parsed["header"], new_parsed["header"])
        self.assertEqual(orig_parsed["chunk_bytes"], new_parsed["chunk_bytes"])

        # Check plugin attributes
        self.assertEqual(new_parsed["plugin_attrs"]["file"], PATH_K61)
        for k, v in orig_parsed["plugin_attrs"].items():
            if k != "file":
                self.assertEqual(new_parsed["plugin_attrs"][k], v, f"attribute mismatch for {k}")

    def test_rewrite_idempotent(self):
        original_text = OBSERVED_XL.read_bytes().decode("utf-8")
        new_text, changed1 = fixer.rewrite_xpl_content(original_text, PATH_K61)
        self.assertTrue(changed1)

        same_text, changed2 = fixer.rewrite_xpl_content(new_text, PATH_K61)
        self.assertFalse(changed2)
        self.assertEqual(new_text, same_text)

    def test_missing_plugin_tag_raises_value_error(self):
        bad_xml = '<?xml version="1.0"?><pluginstate><state></state></pluginstate>'
        with self.assertRaises(ValueError):
            fixer.rewrite_xpl_content(bad_xml, PATH_K61)

    def test_fix_xpl_file(self):
        test_file = self.tmp / "test.xpl"
        test_file.write_bytes(OBSERVED_XL.read_bytes())

        changed = fixer.fix_xpl_file(test_file, PATH_K61)
        self.assertTrue(changed)

        content = test_file.read_bytes().decode("utf-8")
        self.assertIn(PATH_K61, content)
        self.assertNotIn(PATH_XL, content)

    def test_fix_xpl_directory_recursive(self):
        # Create hierarchy:
        # root/
        #   000-Bass-Test.xpl
        #   Bass/
        #     001-Bass-Sub.xpl
        #   Lead/
        #     002-Lead-Saw.xpl
        sub_bass = self.tmp / "Bass"
        sub_lead = self.tmp / "Lead"
        sub_bass.mkdir()
        sub_lead.mkdir()

        raw = OBSERVED_XL.read_bytes()
        (self.tmp / "000-Root.xpl").write_bytes(raw)
        (sub_bass / "001-Sub.xpl").write_bytes(raw)
        (sub_lead / "002-Saw.xpl").write_bytes(raw)

        total, modified = fixer.fix_xpl_directory(self.tmp, PATH_K61, recursive=True)
        self.assertEqual(total, 3)
        self.assertEqual(modified, 3)

        for p in self.tmp.rglob("*.xpl"):
            parsed = gen_xpl.parse_xpl(p.read_bytes().decode("utf-8"))
            self.assertEqual(parsed["plugin_attrs"]["file"], PATH_K61)

    def test_fix_xpl_directory_non_recursive(self):
        sub = self.tmp / "Sub"
        sub.mkdir()
        raw = OBSERVED_XL.read_bytes()
        (self.tmp / "000-Root.xpl").write_bytes(raw)
        (sub / "001-Sub.xpl").write_bytes(raw)

        total, modified = fixer.fix_xpl_directory(self.tmp, PATH_K61, recursive=False)
        self.assertEqual(total, 1)
        self.assertEqual(modified, 1)

        root_parsed = gen_xpl.parse_xpl((self.tmp / "000-Root.xpl").read_bytes().decode("utf-8"))
        self.assertEqual(root_parsed["plugin_attrs"]["file"], PATH_K61)

        sub_parsed = gen_xpl.parse_xpl((sub / "001-Sub.xpl").read_bytes().decode("utf-8"))
        self.assertEqual(sub_parsed["plugin_attrs"]["file"], PATH_XL)

    def test_dry_run(self):
        test_file = self.tmp / "dry_run.xpl"
        test_file.write_bytes(OBSERVED_XL.read_bytes())

        total, modified = fixer.fix_xpl_directory(self.tmp, PATH_K61, dry_run=True)
        self.assertEqual(total, 1)
        self.assertEqual(modified, 1)

        # File on disk must remain untouched
        content = test_file.read_bytes().decode("utf-8")
        self.assertIn(PATH_XL, content)
        self.assertNotIn(PATH_K61, content)

    def test_cli_execution(self):
        test_file = self.tmp / "cli_test.xpl"
        test_file.write_bytes(OBSERVED_XL.read_bytes())

        cmd = [
            sys.executable,
            str(FIX_PATHS_SCRIPT),
            "--dir",
            str(self.tmp),
            "--path",
            PATH_SHARED,
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("updated 1/1 .xpl files", res.stdout)
        self.assertIn(PATH_SHARED, res.stdout)

        content = test_file.read_bytes().decode("utf-8")
        self.assertIn(PATH_SHARED, content)

    def test_cli_errors(self):
        # Non-existent directory
        cmd = [
            sys.executable,
            str(FIX_PATHS_SCRIPT),
            "--dir",
            str(self.tmp / "nonexistent"),
            "--path",
            PATH_SHARED,
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("error", res.stderr)

        # Missing required argument
        cmd2 = [sys.executable, str(FIX_PATHS_SCRIPT), "--dir", str(self.tmp)]
        res2 = subprocess.run(cmd2, capture_output=True, text=True)
        self.assertNotEqual(res2.returncode, 0)


if __name__ == "__main__":
    unittest.main()
