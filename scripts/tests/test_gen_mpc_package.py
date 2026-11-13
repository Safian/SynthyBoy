#!/usr/bin/env python3
# SynthyBoy - acceptance tests for the --plugin-dir mode of scripts/gen-mpc-manifest.py (stdlib unittest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   python3 scripts/tests/test_gen_mpc_package.py
#
# --plugin-dir DIR fills a plugin folder that already holds the binaries: plugin.json names only the binaries that exist
# (the SDK validator rejects a key whose file is missing), version.xml declares the versions.

import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
GEN = ROOT / "scripts" / "gen-mpc-manifest.py"


def run(*args):
    return subprocess.run([sys.executable, str(GEN), *args], capture_output=True, text=True)


def folder(tmp, binaries, skins=False):
    d = Path(tmp) / "Safian - VST - SynthyBoy"
    (d / "VST").mkdir(parents=True)
    for name in binaries:
        (d / "VST" / name).write_bytes(b"\x7fELF")
    if skins:
        (d / "Plugin Skins").mkdir()
        (d / "Plugin Skins" / "TUI.json").write_text("{}")
    return d


class PluginDirTests(unittest.TestCase):
    def test_only_arm32(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, ["synthyboy-arm32.so"])
            r = run("--plugin-dir", str(d))
            self.assertEqual(r.returncode, 0, r.stderr)
            m = json.loads((d / "plugin.json").read_text())
            self.assertEqual(m["file32"], "VST/synthyboy-arm32.so")
            self.assertNotIn("file64", m)
            self.assertEqual(m["uid"], "SfSB")

    def test_only_arm64(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, ["synthyboy-arm64.so"])
            self.assertEqual(run("--plugin-dir", str(d)).returncode, 0)
            m = json.loads((d / "plugin.json").read_text())
            self.assertEqual(m["file64"], "VST/synthyboy-arm64.so")
            self.assertNotIn("file32", m)

    def test_both(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, ["synthyboy-arm32.so", "synthyboy-arm64.so"])
            self.assertEqual(run("--plugin-dir", str(d)).returncode, 0)
            m = json.loads((d / "plugin.json").read_text())
            self.assertIn("file32", m)
            self.assertIn("file64", m)

    def test_no_binary_is_an_error_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, [])
            r = run("--plugin-dir", str(d))
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertFalse((d / "plugin.json").exists())
            self.assertFalse((d / "version.xml").exists())

    def test_missing_dir_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(run("--plugin-dir", str(Path(tmp) / "nope")).returncode, 2)

    def test_version_xml_without_skins(self):
        root_version = json.loads((ROOT / "plugin.json").read_text())["version"]
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, ["synthyboy-arm32.so"])
            self.assertEqual(run("--plugin-dir", str(d)).returncode, 0)
            text = (d / "version.xml").read_text(encoding="utf-8")
            self.assertTrue(text.startswith('<?xml version="1.0" encoding="utf-8"?>'))
            x = ET.fromstring(text.split("?>", 1)[1])
            self.assertEqual(x.tag, "plugincontent")
            parts = x.findtext("version").split(".")
            self.assertEqual(len(parts), 4)
            self.assertEqual(".".join(parts[:3]), root_version)
            self.assertEqual(parts[3], "0")
            self.assertIsNone(x.find("uiversion"), "no skins: no uiversion element")

    def test_version_xml_with_skins(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, ["synthyboy-arm32.so"], skins=True)
            self.assertEqual(run("--plugin-dir", str(d)).returncode, 0)
            x = ET.fromstring((d / "version.xml").read_text(encoding="utf-8").split("?>", 1)[1])
            self.assertEqual(x.findtext("uiversion"), "0.1.0.0")

    def test_deterministic(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = folder(tmp, ["synthyboy-arm32.so"])
            run("--plugin-dir", str(d))
            a = (d / "plugin.json").read_bytes(), (d / "version.xml").read_bytes()
            run("--plugin-dir", str(d))
            self.assertEqual(a, ((d / "plugin.json").read_bytes(), (d / "version.xml").read_bytes()))

    def test_out_mode_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(run("--out", tmp).returncode, 0)
            m = json.loads((Path(tmp) / "plugin.json").read_text())
            self.assertIn("file32", m)
            self.assertIn("file64", m)


if __name__ == "__main__":
    unittest.main()
