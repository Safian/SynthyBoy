#!/usr/bin/env python3
# SynthyBoy - tests of scripts/package-mpc-release.py (Akai MPC release packager).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "package-mpc-release.py"
SOURCE_URL = "https://github.com/Safian/SynthyBoy"


def fake_elf(bits, machine, flags=0):
    """Generates minimal valid ELF headers for testing."""
    ident = b"\x7fELF" + bytes([1 if bits == 32 else 2, 1, 1]) + bytes(9)
    if bits == 32:
        return ident + struct.pack("<HHIIIII", 3, machine, 1, 0, 0, 0, flags) + bytes(64)
    return ident + struct.pack("<HHIQQQI", 3, machine, 1, 0, 0, 0, flags) + bytes(64)


class TestPackageMpcRelease(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.out_dir = self.tmp / "dist"
        self.out_dir.mkdir()
        self.plugin_dir = self._create_synthetic_plugin_folder(self.tmp, num_presets=105)

    def tearDown(self):
        self._tmp.cleanup()

    def _create_synthetic_plugin_folder(self, base_dir: Path, num_presets: int = 105) -> Path:
        folder = base_dir / "Safian - VST - SynthyBoy"
        folder.mkdir(parents=True, exist_ok=True)

        vst_dir = folder / "VST"
        vst_dir.mkdir(parents=True, exist_ok=True)
        (vst_dir / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40))
        (vst_dir / "synthyboy-arm64.so").write_bytes(fake_elf(64, 183))

        skins_dir = folder / "Plugin Skins"
        skins_dir.mkdir(parents=True, exist_ok=True)
        (skins_dir / "TUI.json").write_text("{}", encoding="utf-8")
        (skins_dir / "Q-Links.json").write_text("{}", encoding="utf-8")

        presets_dir = folder / "Content" / "presets"
        presets_dir.mkdir(parents=True, exist_ok=True)
        for i in range(num_presets):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"preset-data")

        (folder / "version.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            "<plugincontent>\n"
            "    <version>0.1.0.0</version>\n"
            "    <uiversion>0.1.0.0</uiversion>\n"
            "</plugincontent>\n",
            encoding="utf-8",
        )

        manifest = {
            "name": "SynthyBoy",
            "manufacturer": "Safian",
            "version": "0.1.0",
            "format": "vst2",
            "kind": "instrument",
            "file32": "VST/synthyboy-arm32.so",
            "file64": "VST/synthyboy-arm64.so",
        }
        (folder / "plugin.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )

        return folder

    def _run_packager(self, *extra_args):
        cmd = [
            sys.executable,
            "-I",
            str(SCRIPT),
            "--plugin-dir",
            str(self.plugin_dir),
            "--out",
            str(self.out_dir),
            "--source-url",
            SOURCE_URL,
            *extra_args,
        ]
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_success_and_determinism(self):
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0, f"stdout: {proc.stdout}\nstderr: {proc.stderr}")

        zip_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip"
        sha_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip.sha256"
        self.assertTrue(zip_path.is_file())
        self.assertTrue(sha_path.is_file())

        self.assertIn("SynthyBoy-MPC-0.1.0.zip", proc.stdout)
        self.assertIn("bytes", proc.stdout)
        self.assertIn("sha256", proc.stdout)

        # Inspect zip archive structure
        with zipfile.ZipFile(zip_path, "r") as zf:
            namelist = zf.namelist()
            self.assertTrue(len(namelist) > 0)
            for name in namelist:
                self.assertTrue(
                    name.startswith("Safian - VST - SynthyBoy/"),
                    f"Entry {name} does not start with top-level folder",
                )
            self.assertIn("Safian - VST - SynthyBoy/INSTALL.txt", namelist)
            self.assertIn("Safian - VST - SynthyBoy/LICENSE", namelist)
            self.assertIn("Safian - VST - SynthyBoy/plugin.json", namelist)
            self.assertIn("Safian - VST - SynthyBoy/version.xml", namelist)
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-arm32.so", namelist)
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-arm64.so", namelist)

        first_bytes = zip_path.read_bytes()
        first_sha = sha_path.read_text(encoding="utf-8")

        # Second run: must be byte-identical
        out_dir2 = self.tmp / "dist2"
        cmd2 = [
            sys.executable,
            "-I",
            str(SCRIPT),
            "--plugin-dir",
            str(self.plugin_dir),
            "--out",
            str(out_dir2),
            "--source-url",
            SOURCE_URL,
        ]
        proc2 = subprocess.run(cmd2, capture_output=True, text=True)
        self.assertEqual(proc2.returncode, 0)
        zip_path2 = out_dir2 / "SynthyBoy-MPC-0.1.0.zip"
        sha_path2 = out_dir2 / "SynthyBoy-MPC-0.1.0.zip.sha256"

        self.assertEqual(zip_path2.read_bytes(), first_bytes)
        self.assertEqual(sha_path2.read_text(encoding="utf-8"), first_sha)

    def test_install_txt_content(self):
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        zip_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip"
        with zipfile.ZipFile(zip_path, "r") as zf:
            install_txt = zf.read("Safian - VST - SynthyBoy/INSTALL.txt").decode("utf-8")

        # Verify source URL is populated
        self.assertIn(SOURCE_URL, install_txt)
        self.assertNotIn("<SOURCE_URL>", install_txt)

        # Verify the two switches
        self.assertIn("Enable Enhancements", install_txt)
        self.assertIn("External Plugin Support", install_txt)
        self.assertIn("Preferences > Enhancements (Beta)", install_txt)

        # Verify hardware support
        self.assertIn("MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force", install_txt)
        self.assertIn("MPC Live III, MPC XL", install_txt)

        # Verify OS requirement
        self.assertIn("MPC OS 3.10.1 or later", install_txt)

        # Verify installation / removal
        self.assertIn('VST2 Plugins', install_txt)
        self.assertIn("Restart the MPC", install_txt)
        self.assertIn("External Plugins", install_txt)
        self.assertIn("Delete the", install_txt)

        # Verify third-party warning and disclaimer
        self.assertIn("Third-party plugins are not supported by Akai", install_txt)
        self.assertIn("AGPL-3.0-or-later", install_txt)
        self.assertIn("Not affiliated with or endorsed by Akai Professional / inMusic Brands.", install_txt)

    def test_no_apple_double_or_ds_store(self):
        # Create macOS metadata and .DS_Store files across the tree
        (self.plugin_dir / ".DS_Store").write_bytes(b"junk")
        (self.plugin_dir / "._hidden").write_bytes(b"appledouble")
        (self.plugin_dir / "VST" / "._synthyboy-arm32.so").write_bytes(b"appledouble")
        (self.plugin_dir / "Plugin Skins" / ".DS_Store").write_bytes(b"junk")
        (self.plugin_dir / "Content" / "presets" / "._preset_000.sbpreset").write_bytes(b"appledouble")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        zip_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip"
        with zipfile.ZipFile(zip_path, "r") as zf:
            namelist = zf.namelist()
            for name in namelist:
                parts = name.split("/")
                for part in parts:
                    self.assertFalse(part.startswith("._"), f"AppleDouble entry found in zip: {name}")
                    self.assertFalse(part == ".DS_Store", f".DS_Store entry found in zip: {name}")

    def test_failure_missing_binary(self):
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").unlink()
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("missing binary", proc.stderr)

        # Restore arm32 and remove arm64
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").unlink()
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("missing binary", proc2.stderr)

    def test_failure_wrong_elf_class(self):
        # 64-bit ELF placed in 32-bit binary path
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(64, 40))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF class", proc.stderr)

        # 32-bit ELF placed in 64-bit binary path
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").write_bytes(fake_elf(32, 183))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("wrong ELF class", proc2.stderr)

    def test_failure_plugin_json_without_file64(self):
        manifest_path = self.plugin_dir / "plugin.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        del manifest["file64"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("file64", proc.stderr)

    def test_failure_fewer_than_100_presets(self):
        presets_dir = self.plugin_dir / "Content" / "presets"
        shutil.rmtree(presets_dir)
        presets_dir.mkdir(parents=True)
        for i in range(50):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"data")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("fewer than 100 presets", proc.stderr)

    def test_failure_missing_version_xml(self):
        (self.plugin_dir / "version.xml").unlink()
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("version.xml", proc.stderr)

    def test_failure_missing_skin_files(self):
        (self.plugin_dir / "Plugin Skins" / "TUI.json").unlink()
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("TUI.json", proc.stderr)

    def test_custom_version_flag(self):
        proc = self._run_packager("--version", "1.2.3")
        self.assertEqual(proc.returncode, 0)
        zip_path = self.out_dir / "SynthyBoy-MPC-1.2.3.zip"
        self.assertTrue(zip_path.is_file())


if __name__ == "__main__":
    unittest.main()
