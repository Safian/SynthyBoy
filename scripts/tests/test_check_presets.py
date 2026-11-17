#!/usr/bin/env python3
# SynthyBoy - tests for factory preset checker (scripts/check-presets.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "check-presets.py"
REAL_PARAMS = ROOT / "docs" / "params.json"
REAL_PRESETS = ROOT / "content" / "presets"


def make_synthetic_params() -> dict:
    params_list = [
        {"key": "master.volume", "name": "Master Volume", "min": -60.0, "max": 6.0, "default": -12.0, "scale": "linear", "interval": 0, "flags": ["automatable", "modulatable"]},
        {"key": "osc1.on", "name": "Osc 1 On", "min": 0.0, "max": 1.0, "default": 1.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
        {"key": "osc1.level", "name": "Osc 1 Level", "min": 0.0, "max": 100.0, "default": 50.0, "scale": "linear", "interval": 0},
        {"key": "filter1.on", "name": "Filter 1 On", "min": 0.0, "max": 1.0, "default": 1.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
        {"key": "filter1.cutoff", "name": "Filter 1 Cutoff", "min": 20.0, "max": 20000.0, "default": 1000.0, "scale": "exp_skew", "interval": 0, "flags": ["modulatable"]},
        {"key": "filter2.on", "name": "Filter 2 On", "min": 0.0, "max": 1.0, "default": 0.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
        {"key": "filter2.cutoff", "name": "Filter 2 Cutoff", "min": 20.0, "max": 20000.0, "default": 1000.0, "scale": "exp_skew", "interval": 0, "flags": ["modulatable"]},
    ]

    for i in range(1, 65):
        params_list.extend([
            {"key": f"mod{i:02d}.source", "min": 0.0, "max": 57.0, "default": 0.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
            {"key": f"mod{i:02d}.destination", "min": 0.0, "max": 1248.0, "default": 0.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
            {"key": f"mod{i:02d}.amount", "min": -100.0, "max": 100.0, "default": 0.0, "scale": "linear", "interval": 0},
            {"key": f"mod{i:02d}.bypass", "min": 0.0, "max": 1.0, "default": 0.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
            {"key": f"mod{i:02d}.aux_source", "min": 0.0, "max": 57.0, "default": 0.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
            {"key": f"mod{i:02d}.bipolar", "min": 0.0, "max": 1.0, "default": 0.0, "scale": "stepped", "interval": 1, "flags": ["stepped"]},
        ])

    return {
        "format": "synthyboy-params",
        "version": 1,
        "stateVersion": 4,
        "modSources": [
            {"choice": 1, "key": "env1", "name": "Env 1"},
            {"choice": 7, "key": "lfo1", "name": "LFO 1"},
            {"choice": 19, "key": "macro1", "name": "Macro 1"},
            {"choice": 20, "key": "macro2", "name": "Macro 2"},
        ],
        "modDestinations": [
            {"choice": 1, "key": "master.volume", "name": "Master Volume"},
            {"choice": 2, "key": "filter1.cutoff", "name": "Filter 1 Cutoff"},
            {"choice": 3, "key": "filter2.cutoff", "name": "Filter 2 Cutoff"},
        ],
        "params": params_list,
    }


def make_valid_preset(name="Test Preset", category="Bass", overrides=None) -> dict:
    params = {
        "master.volume": -12.0,
        "osc1.on": 1.0,
        "osc1.level": 50.0,
        "filter1.on": 1.0,
        "filter1.cutoff": 1000.0,
        "filter2.on": 0.0,
        "filter2.cutoff": 1000.0,
    }
    for i in range(1, 65):
        params[f"mod{i:02d}.source"] = 0.0
        params[f"mod{i:02d}.destination"] = 0.0
        params[f"mod{i:02d}.amount"] = 0.0
        params[f"mod{i:02d}.bypass"] = 0.0
        params[f"mod{i:02d}.aux_source"] = 0.0
        params[f"mod{i:02d}.bipolar"] = 0.0

    if overrides:
        params.update(overrides)

    return {
        "format": "synthyboy-preset",
        "version": 1,
        "meta": {
            "name": name,
            "category": category,
            "author": "Tester",
            "license": "CC0-1.0",
            "tags": "test,bass",
            "description": "Clean test patch",
        },
        "state": {
            "format": "synthyboy-state",
            "version": 4,
            "params": params,
        },
    }


class CheckPresetsTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.tmppath = Path(self.tmpdir.name)
        self.params_file = self.tmppath / "params.json"
        self.params_file.write_text(json.dumps(make_synthetic_params(), indent=2), encoding="utf-8")
        self.pack_dir = self.tmppath / "presets"
        self.pack_dir.mkdir()
        self.bass_dir = self.pack_dir / "Bass"
        self.bass_dir.mkdir()

    def tearDown(self):
        self.tmpdir.cleanup()

    def _run_checker(self, *args, extra_cli=None) -> subprocess.CompletedProcess:
        cmd = [sys.executable, str(SCRIPT), "--params", str(self.params_file)]
        if extra_cli:
            cmd.extend(extra_cli)
        cmd.extend(args)
        return subprocess.run(cmd, capture_output=True, text=True)

    def _write_manifest(self, file_entries):
        manifest = {
            "schemaVersion": 1,
            "id": "com.safian.synthyboy.test-pack",
            "name": "Test Pack",
            "version": "1.0.0",
            "license": "CC0-1.0",
            "attribution": "Tester",
            "kind": "presets",
            "requires_plugin": ">=0.1.0",
            "files": file_entries,
        }
        (self.pack_dir / "contentpack.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    def test_good_preset_passes(self):
        preset_doc = make_valid_preset(name="Good Preset", category="Bass")
        preset_bytes = json.dumps(preset_doc).encode("utf-8")
        preset_file = self.bass_dir / "good.sbpreset"
        preset_file.write_bytes(preset_bytes)
        self._write_manifest([{
            "path": "Bass/good.sbpreset",
            "sha256": hashlib.sha256(preset_bytes).hexdigest(),
            "size": len(preset_bytes),
        }])

        res = self._run_checker(str(self.pack_dir))
        self.assertEqual(res.returncode, 0, f"Expected 0 errors, stderr: {res.stderr}\nstdout: {res.stdout}")
        self.assertIn("0 error(s)", res.stdout)
        self.assertIn("0 warning(s)", res.stdout)

    def test_defect_invalid_json(self):
        bad_file = self.bass_dir / "bad_json.sbpreset"
        bad_file.write_text("{\"format\": \"synthyboy-preset\", invalid json}", encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("json_syntax", res.stdout)

    def test_defect_wrong_preset_format_and_version(self):
        preset_doc = make_valid_preset()
        preset_doc["format"] = "wrong-preset"
        preset_doc["version"] = 2
        bad_file = self.bass_dir / "bad_fmt.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("format_version", res.stdout)

    def test_defect_wrong_state_format_and_version(self):
        preset_doc = make_valid_preset()
        preset_doc["state"]["format"] = "wrong-state"
        preset_doc["state"]["version"] = 99
        bad_file = self.bass_dir / "bad_state.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("format_version", res.stdout)

    def test_defect_unknown_parameter(self):
        preset_doc = make_valid_preset(overrides={"fake.mystery.knob": 42.0})
        bad_file = self.bass_dir / "bad_key.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("unknown_parameter", res.stdout)
        self.assertIn("fake.mystery.knob", res.stdout)

    def test_defect_value_outside_min_max(self):
        preset_doc = make_valid_preset(overrides={"master.volume": 50.0})  # max is 6.0
        bad_file = self.bass_dir / "bad_val.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("value_range", res.stdout)
        self.assertIn("master.volume", res.stdout)

    def test_defect_value_not_on_interval_grid(self):
        preset_doc = make_valid_preset(overrides={"osc1.on": 0.5})  # interval=1 stepped
        bad_file = self.bass_dir / "bad_grid.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("interval_grid", res.stdout)
        self.assertIn("osc1.on", res.stdout)

    def test_defect_mod_source_outside_list(self):
        preset_doc = make_valid_preset(overrides={"mod01.source": 99.0, "mod01.destination": 1.0, "mod01.amount": 50.0})
        bad_file = self.bass_dir / "bad_src.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("mod_source_invalid", res.stdout)

    def test_defect_mod_destination_outside_list(self):
        preset_doc = make_valid_preset(overrides={"mod01.source": 1.0, "mod01.destination": 999.0, "mod01.amount": 50.0})
        bad_file = self.bass_dir / "bad_dst.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([])

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("mod_destination_invalid", res.stdout)

    def test_defect_mod_slot_unbalanced(self):
        # Case 1: source without destination
        preset_doc1 = make_valid_preset(overrides={"mod01.source": 1.0, "mod01.destination": 0.0, "mod01.amount": 20.0})
        bad_file1 = self.bass_dir / "unbal1.sbpreset"
        bad_file1.write_text(json.dumps(preset_doc1), encoding="utf-8")

        res1 = self._run_checker(str(bad_file1))
        self.assertEqual(res1.returncode, 1)
        self.assertIn("mod_slot_unbalanced", res1.stdout)

        # Case 2: destination without source
        preset_doc2 = make_valid_preset(overrides={"mod01.source": 0.0, "mod01.destination": 1.0, "mod01.amount": 20.0})
        bad_file2 = self.bass_dir / "unbal2.sbpreset"
        bad_file2.write_text(json.dumps(preset_doc2), encoding="utf-8")

        res2 = self._run_checker(str(bad_file2))
        self.assertEqual(res2.returncode, 1)
        self.assertIn("mod_slot_unbalanced", res2.stdout)

    def test_defect_mod_slot_amount_zero_when_on(self):
        preset_doc = make_valid_preset(overrides={
            "mod01.source": 1.0,
            "mod01.destination": 1.0,
            "mod01.amount": 0.0,
            "mod01.bypass": 0.0,
        })
        bad_file = self.bass_dir / "zero_amt.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("mod_slot_amount_zero", res.stdout)

    def test_defect_mod_slot_duplicate(self):
        preset_doc = make_valid_preset(overrides={
            "mod01.source": 1.0,
            "mod01.destination": 1.0,
            "mod01.amount": 25.0,
            "mod02.source": 1.0,
            "mod02.destination": 1.0,
            "mod02.amount": 50.0,
        })
        bad_file = self.bass_dir / "dup_slot.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("mod_slot_duplicate", res.stdout)

    def test_mod_slot_not_duplicate_when_aux_source_differs(self):
        # Plain route vs route with aux source (engine sums them: different routes)
        preset_doc = make_valid_preset(overrides={
            "mod01.source": 1.0,
            "mod01.destination": 1.0,
            "mod01.amount": 25.0,
            "mod01.aux_source": 0.0,
            "mod02.source": 1.0,
            "mod02.destination": 1.0,
            "mod02.amount": 50.0,
            "mod02.aux_source": 7.0,  # LFO 1 as aux
        })
        ok_file = self.bass_dir / "aux_slot.sbpreset"
        ok_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(ok_file))
        self.assertNotIn("mod_slot_duplicate", res.stdout)
        self.assertEqual(res.returncode, 0)

    def test_mod_slot_not_duplicate_when_polarity_differs(self):
        # Route with bipolar=0 vs route with bipolar=1
        preset_doc = make_valid_preset(overrides={
            "mod01.source": 1.0,
            "mod01.destination": 1.0,
            "mod01.amount": 25.0,
            "mod01.bipolar": 0.0,
            "mod02.source": 1.0,
            "mod02.destination": 1.0,
            "mod02.amount": 50.0,
            "mod02.bipolar": 1.0,
        })
        ok_file = self.bass_dir / "bipolar_slot.sbpreset"
        ok_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(ok_file))
        self.assertNotIn("mod_slot_duplicate", res.stdout)
        self.assertEqual(res.returncode, 0)

    def test_mod_slot_duplicate_when_source_aux_target_polarity_match(self):
        # Two routes with identical source, aux_source, destination and polarity
        preset_doc = make_valid_preset(overrides={
            "mod01.source": 1.0,
            "mod01.destination": 1.0,
            "mod01.amount": 25.0,
            "mod01.aux_source": 7.0,
            "mod01.bipolar": 1.0,
            "mod02.source": 1.0,
            "mod02.destination": 1.0,
            "mod02.amount": 50.0,
            "mod02.aux_source": 7.0,
            "mod02.bipolar": 1.0,
        })
        bad_file = self.bass_dir / "dup_aux_slot.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("mod_slot_duplicate", res.stdout)

    def test_defect_macro_with_targets_named_or_documented_unused(self):
        # Case A: explicit macro structure with name "Unused"
        preset_doc1 = make_valid_preset()
        preset_doc1["macros"] = [
            {"macroKey": "macro.1", "label": "Unused", "targets": [{"destination": "master.volume"}]}
        ]
        bad_file1 = self.bass_dir / "unused_macro1.sbpreset"
        bad_file1.write_text(json.dumps(preset_doc1), encoding="utf-8")

        res1 = self._run_checker(str(bad_file1))
        self.assertEqual(res1.returncode, 1)
        self.assertIn("macro_unused_with_targets", res1.stdout)

        # Case B: mod matrix routes macro1 (choice 19), but meta description states macro 1 is unused
        preset_doc2 = make_valid_preset(overrides={
            "mod01.source": 19.0,  # macro1
            "mod01.destination": 1.0,  # master.volume
            "mod01.amount": 30.0,
        })
        preset_doc2["meta"]["description"] = "Bass patch, Macro 1 is unused."
        bad_file2 = self.bass_dir / "unused_macro2.sbpreset"
        bad_file2.write_text(json.dumps(preset_doc2), encoding="utf-8")

        res2 = self._run_checker(str(bad_file2))
        self.assertEqual(res2.returncode, 1)
        self.assertIn("macro_unused_with_targets", res2.stdout)

        # Case C: mod matrix routes macro 1 as AUX source, but meta description states macro 1 is unused
        preset_doc3 = make_valid_preset(overrides={
            "mod01.source": 1.0,  # env1
            "mod01.destination": 1.0,  # master.volume
            "mod01.amount": 30.0,
            "mod01.aux_source": 19.0,  # macro1 as aux source
        })
        preset_doc3["meta"]["description"] = "Bass patch, Macro 1 is unused."
        bad_file3 = self.bass_dir / "unused_macro3.sbpreset"
        bad_file3.write_text(json.dumps(preset_doc3), encoding="utf-8")

        res3 = self._run_checker(str(bad_file3))
        self.assertEqual(res3.returncode, 1)
        self.assertIn("macro_unused_with_targets", res3.stdout)

    def test_defect_macro_target_pointing_at_nonexistent_param(self):
        preset_doc = make_valid_preset()
        preset_doc["macros"] = [
            {"macroKey": "macro.1", "label": "Macro 1", "targets": [{"destination": "phantom.param.key"}]}
        ]
        bad_file = self.bass_dir / "phantom_macro.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("macro_target_nonexistent", res.stdout)
        self.assertIn("phantom.param.key", res.stdout)

    def test_warning_disabled_module_modulation(self):
        # filter2.on is 0.0 (switched off), slot routes to filter2.cutoff (choice 3)
        preset_doc = make_valid_preset(overrides={
            "filter2.on": 0.0,
            "mod01.source": 1.0,  # env1
            "mod01.destination": 3.0,  # filter2.cutoff
            "mod01.amount": 40.0,
            "mod01.bypass": 0.0,
        })
        warn_file = self.bass_dir / "warn_mod.sbpreset"
        warn_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(warn_file))
        self.assertEqual(res.returncode, 0, "A warning alone must exit with 0")
        self.assertIn("disabled_module_modulation", res.stdout)
        self.assertIn("0 error(s)", res.stdout)
        self.assertIn("1 warning(s)", res.stdout)

    def test_defect_meta_missing_name_and_category(self):
        preset_doc = make_valid_preset()
        preset_doc["meta"]["name"] = ""
        preset_doc["meta"]["category"] = ""
        bad_file = self.bass_dir / "no_meta.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(bad_file))
        self.assertEqual(res.returncode, 1)
        self.assertIn("meta_missing_fields", res.stdout)

    def test_defect_meta_category_folder_mismatch(self):
        # Preset file placed in "Bass" folder, but meta says "Lead"
        preset_doc = make_valid_preset(category="Lead")
        bad_file = self.bass_dir / "wrong_cat.sbpreset"
        bad_file.write_text(json.dumps(preset_doc), encoding="utf-8")
        self._write_manifest([{
            "path": "Bass/wrong_cat.sbpreset",
            "sha256": hashlib.sha256(bad_file.read_bytes()).hexdigest(),
        }])

        res = self._run_checker(str(self.pack_dir))
        self.assertEqual(res.returncode, 1)
        self.assertIn("meta_category_mismatch", res.stdout)

    def test_defect_meta_name_differs_from_contentpack(self):
        preset_doc = make_valid_preset(name="File Name")
        bad_file = self.bass_dir / "mismatch_name.sbpreset"
        raw = json.dumps(preset_doc).encode("utf-8")
        bad_file.write_bytes(raw)
        self._write_manifest([{
            "path": "Bass/mismatch_name.sbpreset",
            "name": "Manifest Name",
            "sha256": hashlib.sha256(raw).hexdigest(),
        }])

        res = self._run_checker(str(self.pack_dir))
        self.assertEqual(res.returncode, 1)
        self.assertIn("meta_name_mismatch_contentpack", res.stdout)

    def test_defect_duplicate_names_across_pack(self):
        p1 = make_valid_preset(name="Cloned Name")
        p2 = make_valid_preset(name="Cloned Name", overrides={"master.volume": -6.0})
        f1 = self.bass_dir / "dup1.sbpreset"
        f2 = self.bass_dir / "dup2.sbpreset"
        f1.write_text(json.dumps(p1), encoding="utf-8")
        f2.write_text(json.dumps(p2), encoding="utf-8")
        self._write_manifest([
            {"path": "Bass/dup1.sbpreset", "sha256": hashlib.sha256(f1.read_bytes()).hexdigest()},
            {"path": "Bass/dup2.sbpreset", "sha256": hashlib.sha256(f2.read_bytes()).hexdigest()},
        ])

        res = self._run_checker(str(self.pack_dir))
        self.assertEqual(res.returncode, 1)
        self.assertIn("duplicate_preset_name", res.stdout)

    def test_defect_preset_name_too_long(self):
        # Host limit is 24 chars
        long_name = "A Very Long Preset Name Exceeding Twenty Four"
        p = make_valid_preset(name=long_name)
        f = self.bass_dir / "long_name.sbpreset"
        f.write_text(json.dumps(p), encoding="utf-8")

        res = self._run_checker(str(f))
        self.assertEqual(res.returncode, 1)
        self.assertIn("preset_name_length", res.stdout)

    def test_defect_contentpack_file_mismatches(self):
        # 1. Manifest entry without file
        # 2. File without manifest entry
        f_real = self.bass_dir / "present.sbpreset"
        raw = json.dumps(make_valid_preset(name="Present")).encode("utf-8")
        f_real.write_bytes(raw)

        self._write_manifest([
            {"path": "Bass/present.sbpreset", "sha256": hashlib.sha256(raw).hexdigest()},
            {"path": "Bass/ghost.sbpreset", "sha256": "0" * 64},  # missing on disk
        ])

        # Also add unlisted file on disk
        f_unlisted = self.bass_dir / "unlisted.sbpreset"
        f_unlisted.write_text(json.dumps(make_valid_preset(name="Unlisted")), encoding="utf-8")

        res = self._run_checker(str(self.pack_dir))
        self.assertEqual(res.returncode, 1)
        self.assertIn("contentpack_file_mismatch", res.stdout)
        self.assertIn("ghost.sbpreset", res.stdout)
        self.assertIn("unlisted.sbpreset", res.stdout)

    def test_defect_duplicate_preset_content(self):
        # Exact byte duplicates
        p1 = make_valid_preset(name="P1")
        raw1 = json.dumps(p1).encode("utf-8")
        f1 = self.bass_dir / "f1.sbpreset"
        f2 = self.bass_dir / "f2.sbpreset"
        f1.write_bytes(raw1)
        f2.write_bytes(raw1)

        self._write_manifest([
            {"path": "Bass/f1.sbpreset", "sha256": hashlib.sha256(raw1).hexdigest()},
            {"path": "Bass/f2.sbpreset", "sha256": hashlib.sha256(raw1).hexdigest()},
        ])

        res = self._run_checker(str(self.pack_dir))
        self.assertEqual(res.returncode, 1)
        self.assertIn("duplicate_preset_content", res.stdout)

        # Different metadata / bytes, but identical parameter dicts
        p3 = make_valid_preset(name="P3")
        p4 = make_valid_preset(name="P4")
        f3 = self.bass_dir / "f3.sbpreset"
        f4 = self.bass_dir / "f4.sbpreset"
        f3.write_text(json.dumps(p3), encoding="utf-8")
        f4.write_text(json.dumps(p4), encoding="utf-8")
        self._write_manifest([
            {"path": "Bass/f3.sbpreset", "sha256": hashlib.sha256(f3.read_bytes()).hexdigest()},
            {"path": "Bass/f4.sbpreset", "sha256": hashlib.sha256(f4.read_bytes()).hexdigest()},
        ])

        res2 = self._run_checker(str(self.pack_dir))
        self.assertEqual(res2.returncode, 1)
        self.assertIn("duplicate_preset_content", res2.stdout)

    def test_json_output_shape(self):
        preset_doc = make_valid_preset(name="JSON Test", overrides={"filter2.on": 0.0, "mod01.source": 1.0, "mod01.destination": 3.0, "mod01.amount": 20.0})
        f = self.bass_dir / "json_test.sbpreset"
        f.write_text(json.dumps(preset_doc), encoding="utf-8")

        res = self._run_checker(str(f), extra_cli=["--json"])
        self.assertEqual(res.returncode, 0)
        data = json.loads(res.stdout)

        self.assertIn("summary", data)
        self.assertIn("errors", data)
        self.assertIn("warnings", data)
        self.assertEqual(data["summary"]["total_presets"], 1)
        self.assertEqual(data["summary"]["total_errors"], 0)
        self.assertEqual(data["summary"]["total_warnings"], 1)
        self.assertIsInstance(data["summary"]["longest_name"], dict)
        self.assertEqual(data["summary"]["longest_name"]["name"], "JSON Test")
        self.assertEqual(len(data["warnings"]), 1)
        self.assertEqual(data["warnings"][0]["rule"], "disabled_module_modulation")

    def test_cli_usage_errors(self):
        # Missing params file
        res1 = self._run_checker("--params", str(self.tmppath / "missing.json"))
        self.assertEqual(res1.returncode, 2)

        # Missing input path
        res2 = self._run_checker(str(self.tmppath / "missing_dir"))
        self.assertEqual(res2.returncode, 2)

    def test_real_presets_run(self):
        # Runs check-presets.py on repository real presets
        cmd = [sys.executable, str(SCRIPT), "--params", str(REAL_PARAMS), str(REAL_PRESETS)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertIn(res.returncode, (0, 1), f"Expected returncode 0 or 1, got {res.returncode}\nstderr: {res.stderr}")
        pattern = r"Checked \d+ preset\(s\): \d+ error\(s\), \d+ warning\(s\)\."
        self.assertTrue(re.search(pattern, res.stdout), f"Summary line not found in output:\n{res.stdout}")


if __name__ == "__main__":
    unittest.main()
