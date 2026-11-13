#!/usr/bin/env python3
# SynthyBoy - tests for gen-mpc-manifest.py (stdlib unittest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run: python3 -I scripts/tests/test_gen_mpc_manifest.py

import contextlib
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gen_mpc = load("t_gen_mpc", "scripts/gen-mpc-manifest.py")

PLUGIN = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))

# Load the MPC SDK schema if available
MPC_SCHEMA = None
if os.environ.get("MPC_SDK"):
    schema_path = Path(os.environ["MPC_SDK"]) / "schemas" / "plugin.schema.json"
    if schema_path.exists():
        MPC_SCHEMA = json.loads(schema_path.read_text(encoding="utf-8"))


def run_quiet(func, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = func(*args)
    return code, out.getvalue(), err.getvalue()


class FolderNameTests(unittest.TestCase):
    def test_basic_folder_name(self):
        result = gen_mpc.folder_name("Safian", "SynthyBoy")
        self.assertEqual(result, "Safian - VST - SynthyBoy")

    def test_empty_manufacturer(self):
        result = gen_mpc.folder_name("", "SynthyBoy")
        self.assertEqual(result, "VST - SynthyBoy")

    def test_forbidden_characters_removed(self):
        # " and # are forbidden; @ is forbidden
        # 'Sa"fian #VST' -> remove " and # -> 'Safian VST' (space is NOT forbidden)
        # 'Synthy@Boy' -> remove @ -> 'SynthyBoy'
        result = gen_mpc.folder_name('Sa"fian #VST', "Synthy@Boy")
        self.assertEqual(result, "Safian VST - VST - SynthyBoy")

    def test_all_forbidden_characters(self):
        forbidden = {'"', '#', '@', ',', ';', ':', '<', '>', '*', '^', '|', '?', '\\', '/'}
        for ch in forbidden:
            result = gen_mpc.folder_name("X" + ch + "Y", "Z" + ch + "W")
            self.assertNotIn(ch, result)

    def test_spaces_and_utf8_unchanged(self):
        # Space is NOT forbidden
        result = gen_mpc.folder_name("Safian", "Synthy Boy café")
        self.assertEqual(result, "Safian - VST - Synthy Boy café")


class GenerateTests(unittest.TestCase):
    def test_fixed_field_values(self):
        mpc = gen_mpc.generate(PLUGIN)
        self.assertEqual(mpc["name"], "SynthyBoy")
        self.assertEqual(mpc["manufacturer"], "Safian")
        self.assertEqual(mpc["uid"], "SfSB")
        self.assertEqual(mpc["format"], "vst2")
        self.assertEqual(mpc["kind"], "instrument")
        self.assertEqual(mpc["numInputs"], 0)
        self.assertEqual(mpc["numOutputs"], 2)
        self.assertEqual(mpc["file32"], "VST/synthyboy-arm32.so")
        self.assertEqual(mpc["file64"], "VST/synthyboy-arm64.so")

    def test_version_from_root_manifest(self):
        mpc = gen_mpc.generate(PLUGIN)
        self.assertEqual(mpc["version"], PLUGIN["version"])

    def test_description_author_license_copied(self):
        mpc = gen_mpc.generate(PLUGIN)
        self.assertEqual(mpc["description"], PLUGIN["description"])
        self.assertEqual(mpc["author"], PLUGIN["author"])
        self.assertEqual(mpc["license"], PLUGIN["license"])

    def test_missing_optional_keys_use_empty_string(self):
        minimal = {"name": "Test", "version": "1.0.0"}
        mpc = gen_mpc.generate(minimal)
        self.assertEqual(mpc["description"], "")
        self.assertEqual(mpc["author"], "")
        self.assertEqual(mpc["license"], "")

    def test_deterministic_output(self):
        mpc = gen_mpc.generate(PLUGIN)
        text1 = json.dumps(mpc, indent=2, ensure_ascii=False) + "\n"
        text2 = json.dumps(mpc, indent=2, ensure_ascii=False) + "\n"
        self.assertEqual(text1, text2)

    def test_trailing_newline(self):
        mpc = gen_mpc.generate(PLUGIN)
        text = json.dumps(mpc, indent=2, ensure_ascii=False) + "\n"
        self.assertTrue(text.endswith("\n"))
        self.assertFalse(text.endswith("\n\n"))


class SchemaTests(unittest.TestCase):
    @unittest.skipUnless(MPC_SCHEMA, "MPC_SDK environment variable not set")
    def test_required_keys_present_with_correct_types(self):
        mpc = gen_mpc.generate(PLUGIN)
        required = MPC_SCHEMA.get("required", [])
        properties = MPC_SCHEMA.get("properties", {})
        for key in required:
            self.assertIn(key, mpc, "missing required key: %s" % key)
            expected_type = properties.get(key, {}).get("type")
            if expected_type == "string":
                self.assertIsInstance(mpc[key], str)
            elif expected_type == "integer":
                self.assertIsInstance(mpc[key], int)
                self.assertNotIsInstance(mpc[key], bool)
            elif expected_type == "number":
                self.assertIsInstance(mpc[key], (int, float))
                self.assertNotIsInstance(mpc[key], bool)

    @unittest.skipUnless(MPC_SCHEMA, "MPC_SDK environment variable not set")
    def test_no_missing_required_keys(self):
        mpc = gen_mpc.generate(PLUGIN)
        required = MPC_SCHEMA.get("required", [])
        missing = [k for k in required if k not in mpc]
        self.assertEqual(missing, [], "missing required keys: %s" % missing)


class CheckModeTests(unittest.TestCase):
    def test_check_passes_on_identical_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            out_path = tmp_path / "plugin.json"
            code, _, _ = run_quiet(gen_mpc.main, ["--out", str(tmp_path)])
            self.assertEqual(code, 0)
            code, _, _ = run_quiet(gen_mpc.main, ["--check", str(out_path)])
            self.assertEqual(code, 0)

    def test_check_fails_on_modified_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            out_path = tmp_path / "plugin.json"
            run_quiet(gen_mpc.main, ["--out", str(tmp_path)])
            # Modify the file
            data = json.loads(out_path.read_text(encoding="utf-8"))
            data["version"] = "9.9.9"
            out_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            code, _, _ = run_quiet(gen_mpc.main, ["--check", str(out_path)])
            self.assertEqual(code, 1)

    def test_check_does_not_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            out_path = tmp_path / "plugin.json"
            run_quiet(gen_mpc.main, ["--out", str(tmp_path)])
            # Modify the file
            data = json.loads(out_path.read_text(encoding="utf-8"))
            data["version"] = "9.9.9"
            out_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            modified = out_path.read_text(encoding="utf-8")
            code, _, _ = run_quiet(gen_mpc.main, ["--check", str(out_path)])
            self.assertEqual(code, 1)
            # File should not have been changed back by --check
            self.assertEqual(out_path.read_text(encoding="utf-8"), modified)

    def test_check_nonexistent_file_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, _, _ = run_quiet(gen_mpc.main, ["--check", str(Path(tmp) / "nonexistent.json")])
            self.assertEqual(code, 1)


class CommandLineTests(unittest.TestCase):
    def test_out_writes_correct_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            code, _, _ = run_quiet(gen_mpc.main, ["--out", str(tmp_path)])
            self.assertEqual(code, 0)
            out_path = tmp_path / "plugin.json"
            self.assertTrue(out_path.exists())
            data = json.loads(out_path.read_text(encoding="utf-8"))
            self.assertEqual(data["name"], "SynthyBoy")
            self.assertEqual(data["manufacturer"], "Safian")
            self.assertEqual(data["version"], PLUGIN["version"])

    def test_custom_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            # Copy the root manifest to a custom location
            custom = tmp_path / "custom.json"
            custom.write_text((ROOT / "plugin.json").read_text(encoding="utf-8"))
            code, _, _ = run_quiet(gen_mpc.main, ["--out", str(tmp_path), "--manifest", str(custom)])
            self.assertEqual(code, 0)
            out_path = tmp_path / "plugin.json"
            data = json.loads(out_path.read_text(encoding="utf-8"))
            self.assertEqual(data["version"], PLUGIN["version"])

    def test_no_args_fails(self):
        with self.assertRaises(SystemExit) as ctx:
            run_quiet(gen_mpc.main, [])
        self.assertEqual(ctx.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
