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

from mpc_skin_fixture import write_native_skin_stub

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "package-mpc-release.py"
SOURCE_URL = "https://github.com/Safian/SynthyBoy"


def fake_elf(bits, machine, flags=0, endian=1):
    """Generates minimal valid ELF headers for testing."""
    fmt = "<" if endian == 1 else ">"
    ident = b"\x7fELF" + bytes([1 if bits == 32 else 2, endian, 1]) + bytes(9)
    if bits == 32:
        return ident + struct.pack(fmt + "HHIIIII", 3, machine, 1, 0, 0, 0, flags) + bytes(64)
    return ident + struct.pack(fmt + "HHIQQQI", 3, machine, 1, 0, 0, 0, flags) + bytes(64)


class TestPackageMpcRelease(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.out_dir = self.tmp / "dist"
        self.out_dir.mkdir()
        self.plugin_dir = self._create_synthetic_plugin_folder(self.tmp, num_presets=105)

    def tearDown(self):
        self._tmp.cleanup()

    def _create_synthetic_plugin_folder(
        self, base_dir: Path, num_presets: int = 105, binaries: tuple = ("arm32", "arm64")
    ) -> Path:
        folder = base_dir / "Safian - VST - SynthyBoy"
        folder.mkdir(parents=True, exist_ok=True)

        vst_dir = folder / "VST"
        vst_dir.mkdir(parents=True, exist_ok=True)
        manifest = {
            "name": "SynthyBoy",
            "manufacturer": "Safian",
            "uid": "SfSB",
            "version": "0.1.0",
            "format": "vst2",
            "kind": "instrument",
        }

        if "arm32" in binaries:
            bin32 = vst_dir / "synthyboy-arm32.so"
            bin32.write_bytes(fake_elf(32, 40))
            bin32.chmod(0o755)
            manifest["file32"] = "VST/synthyboy-arm32.so"

        if "arm64" in binaries:
            bin64 = vst_dir / "synthyboy-arm64.so"
            bin64.write_bytes(fake_elf(64, 183))
            bin64.chmod(0o755)
            manifest["file64"] = "VST/synthyboy-arm64.so"

        skins_dir = folder / "Plugin Skins"
        skins_dir.mkdir(parents=True, exist_ok=True)
        write_native_skin_stub(skins_dir)

        presets_dir = folder / "Content" / "presets"
        presets_dir.mkdir(parents=True, exist_ok=True)
        for i in range(num_presets):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"preset-data")

        (folder / "version.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            "<plugincontent>\n"
            "    <version>0.1.0.1</version>\n"
            "    <uiversion>0.1.0.0</uiversion>\n"
            "</plugincontent>\n",
            encoding="utf-8",
        )

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

    def test_success_and_determinism_two_architectures(self):
        """Default packaging produces both armv7 and aarch64 archives deterministically."""
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0, f"stdout: {proc.stdout}\nstderr: {proc.stderr}")

        zip_armv7 = self.out_dir / "SynthyBoy-MPC-0.1.0-armv7.zip"
        sha_armv7 = self.out_dir / "SynthyBoy-MPC-0.1.0-armv7.zip.sha256"
        zip_aarch64 = self.out_dir / "SynthyBoy-MPC-0.1.0-aarch64.zip"
        sha_aarch64 = self.out_dir / "SynthyBoy-MPC-0.1.0-aarch64.zip.sha256"

        self.assertTrue(zip_armv7.is_file())
        self.assertTrue(sha_armv7.is_file())
        self.assertTrue(zip_aarch64.is_file())
        self.assertTrue(sha_aarch64.is_file())

        self.assertIn("SynthyBoy-MPC-0.1.0-armv7.zip", proc.stdout)
        self.assertIn("SynthyBoy-MPC-0.1.0-aarch64.zip", proc.stdout)
        self.assertIn("sha256", proc.stdout)

        # Inspect armv7 zip
        with zipfile.ZipFile(zip_armv7, "r") as zf:
            namelist = zf.namelist()
            self.assertIn("Safian - VST - SynthyBoy/INSTALL.txt", namelist)
            self.assertIn("Safian - VST - SynthyBoy/LICENSE", namelist)
            self.assertIn("Safian - VST - SynthyBoy/plugin.json", namelist)
            self.assertIn("Safian - VST - SynthyBoy/version.xml", namelist)
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-arm32.so", namelist)
            self.assertNotIn("Safian - VST - SynthyBoy/VST/synthyboy-arm64.so", namelist)

            pj32 = json.loads(zf.read("Safian - VST - SynthyBoy/plugin.json").decode("utf-8"))
            self.assertEqual(pj32.get("file32"), "VST/synthyboy-arm32.so")
            self.assertNotIn("file64", pj32)

        # Inspect aarch64 zip
        with zipfile.ZipFile(zip_aarch64, "r") as zf:
            namelist = zf.namelist()
            self.assertIn("Safian - VST - SynthyBoy/INSTALL.txt", namelist)
            self.assertIn("Safian - VST - SynthyBoy/LICENSE", namelist)
            self.assertIn("Safian - VST - SynthyBoy/plugin.json", namelist)
            self.assertIn("Safian - VST - SynthyBoy/version.xml", namelist)
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-arm64.so", namelist)
            self.assertNotIn("Safian - VST - SynthyBoy/VST/synthyboy-arm32.so", namelist)

            pj64 = json.loads(zf.read("Safian - VST - SynthyBoy/plugin.json").decode("utf-8"))
            self.assertEqual(pj64.get("file64"), "VST/synthyboy-arm64.so")
            self.assertNotIn("file32", pj64)

        first_bytes_armv7 = zip_armv7.read_bytes()
        first_sha_armv7 = sha_armv7.read_text(encoding="utf-8")
        first_bytes_aarch64 = zip_aarch64.read_bytes()
        first_sha_aarch64 = sha_aarch64.read_text(encoding="utf-8")

        # Second run: byte-identical determinism
        out_dir2 = self.tmp / "dist2"
        proc2 = subprocess.run(
            [
                sys.executable,
                "-I",
                str(SCRIPT),
                "--plugin-dir",
                str(self.plugin_dir),
                "--out",
                str(out_dir2),
                "--source-url",
                SOURCE_URL,
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc2.returncode, 0)
        self.assertEqual((out_dir2 / "SynthyBoy-MPC-0.1.0-armv7.zip").read_bytes(), first_bytes_armv7)
        self.assertEqual((out_dir2 / "SynthyBoy-MPC-0.1.0-armv7.zip.sha256").read_text(encoding="utf-8"), first_sha_armv7)
        self.assertEqual((out_dir2 / "SynthyBoy-MPC-0.1.0-aarch64.zip").read_bytes(), first_bytes_aarch64)
        self.assertEqual((out_dir2 / "SynthyBoy-MPC-0.1.0-aarch64.zip.sha256").read_text(encoding="utf-8"), first_sha_aarch64)

    def test_arch_flag_armv7_and_arm32(self):
        """--arch armv7 (and alias arm32) produces only the armv7 archive."""
        for arch in ("armv7", "arm32"):
            out = self.tmp / f"dist_{arch}"
            proc = self._run_packager("--arch", arch, "--out", str(out))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertTrue((out / "SynthyBoy-MPC-0.1.0-armv7.zip").is_file())
            self.assertFalse((out / "SynthyBoy-MPC-0.1.0-aarch64.zip").exists())

    def test_arch_flag_aarch64_and_arm64(self):
        """--arch aarch64 (and alias arm64) produces only the aarch64 archive."""
        for arch in ("aarch64", "arm64"):
            out = self.tmp / f"dist_{arch}"
            proc = self._run_packager("--arch", arch, "--out", str(out))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertTrue((out / "SynthyBoy-MPC-0.1.0-aarch64.zip").is_file())
            self.assertFalse((out / "SynthyBoy-MPC-0.1.0-armv7.zip").exists())

    def test_arch_flag_combined(self):
        """--arch combined produces the legacy combined archive."""
        proc = self._run_packager("--arch", "combined")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        zip_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip"
        self.assertTrue(zip_path.is_file())
        with zipfile.ZipFile(zip_path, "r") as zf:
            namelist = zf.namelist()
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-arm32.so", namelist)
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-arm64.so", namelist)
            pj = json.loads(zf.read("Safian - VST - SynthyBoy/plugin.json").decode("utf-8"))
            self.assertIn("file32", pj)
            self.assertIn("file64", pj)

    def test_single_arch_folder_packaging(self):
        """Folders with only arm32 or only arm64 can be packaged with matching --arch."""
        single_32 = self._create_synthetic_plugin_folder(self.tmp / "s32", binaries=("arm32",))
        out_32 = self.tmp / "dist_s32"
        proc = subprocess.run(
            [sys.executable, "-I", str(SCRIPT), "--plugin-dir", str(single_32), "--out", str(out_32), "--source-url", SOURCE_URL, "--arch", "armv7"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertTrue((out_32 / "SynthyBoy-MPC-0.1.0-armv7.zip").is_file())

        # Calling --arch aarch64 on arm32 folder fails
        proc_fail = subprocess.run(
            [sys.executable, "-I", str(SCRIPT), "--plugin-dir", str(single_32), "--out", str(out_32), "--source-url", SOURCE_URL, "--arch", "aarch64"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc_fail.returncode, 1)
        self.assertIn("file64", proc_fail.stderr)

        single_64 = self._create_synthetic_plugin_folder(self.tmp / "s64", binaries=("arm64",))
        out_64 = self.tmp / "dist_s64"
        proc64 = subprocess.run(
            [sys.executable, "-I", str(SCRIPT), "--plugin-dir", str(single_64), "--out", str(out_64), "--source-url", SOURCE_URL, "--arch", "aarch64"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc64.returncode, 0, proc64.stderr)
        self.assertTrue((out_64 / "SynthyBoy-MPC-0.1.0-aarch64.zip").is_file())

    def test_install_txt_content(self):
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        # Check armv7 install.txt
        zip_armv7 = self.out_dir / "SynthyBoy-MPC-0.1.0-armv7.zip"
        with zipfile.ZipFile(zip_armv7, "r") as zf:
            install_armv7 = zf.read("Safian - VST - SynthyBoy/INSTALL.txt").decode("utf-8")
        self.assertIn("32-bit: MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force", install_armv7)
        self.assertNotIn("64-bit", install_armv7)
        self.assertIn(SOURCE_URL, install_armv7)
        self.assertIn("Enable Enhancements", install_armv7)
        self.assertIn("External Plugin Support", install_armv7)

        # Check aarch64 install.txt
        zip_aarch64 = self.out_dir / "SynthyBoy-MPC-0.1.0-aarch64.zip"
        with zipfile.ZipFile(zip_aarch64, "r") as zf:
            install_aarch64 = zf.read("Safian - VST - SynthyBoy/INSTALL.txt").decode("utf-8")
        self.assertIn("64-bit: MPC Live III, MPC XL", install_aarch64)
        self.assertNotIn("32-bit", install_aarch64)

    def test_no_apple_double_or_ds_store(self):
        (self.plugin_dir / ".DS_Store").write_bytes(b"junk")
        (self.plugin_dir / "._hidden").write_bytes(b"appledouble")
        (self.plugin_dir / "VST" / "._synthyboy-arm32.so").write_bytes(b"appledouble")
        (self.plugin_dir / "Plugin Skins" / ".DS_Store").write_bytes(b"junk")
        (self.plugin_dir / "Content" / "presets" / "._preset_000.sbpreset").write_bytes(b"appledouble")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        for zname in ("SynthyBoy-MPC-0.1.0-armv7.zip", "SynthyBoy-MPC-0.1.0-aarch64.zip"):
            with zipfile.ZipFile(self.out_dir / zname, "r") as zf:
                for name in zf.namelist():
                    for part in name.split("/"):
                        self.assertFalse(part.startswith("._"), f"AppleDouble entry found in {zname}: {name}")
                        self.assertFalse(part == ".DS_Store", f".DS_Store entry found in {zname}: {name}")

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
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(64, 40))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF class", proc.stderr)

        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").write_bytes(fake_elf(32, 183))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("wrong ELF class", proc2.stderr)

    def test_failure_wrong_elf_machine(self):
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 62))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF machine", proc.stderr)

        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").write_bytes(fake_elf(64, 40))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("wrong ELF machine", proc2.stderr)

    def test_failure_wrong_elf_endianness(self):
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40, endian=2))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF endianness", proc.stderr)

        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40, endian=1))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").write_bytes(fake_elf(64, 183, endian=2))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("wrong ELF endianness", proc2.stderr)

    def test_failure_not_elf_or_truncated(self):
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(b"not an elf binary file")
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("not an ELF binary", proc.stderr)

        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(b"\x7fELF" + bytes(20))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("not an ELF binary", proc2.stderr)

    def test_failure_plugin_json_without_file32(self):
        manifest_path = self.plugin_dir / "plugin.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        del manifest["file32"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("file32", proc.stderr)

    def test_failure_plugin_json_without_file64(self):
        manifest_path = self.plugin_dir / "plugin.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        del manifest["file64"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("file64", proc.stderr)

    def test_failure_plugin_json_escaping_path(self):
        manifest_path = self.plugin_dir / "plugin.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["file32"] = "/etc/passwd"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("relative path inside plugin folder", proc.stderr)

    def test_failure_missing_or_invalid_plugin_json(self):
        manifest_path = self.plugin_dir / "plugin.json"
        manifest_path.unlink()
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("plugin.json missing", proc.stderr)

        manifest_path.write_text("{invalid json", encoding="utf-8")
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("not valid JSON", proc2.stderr)

    def test_failure_fewer_than_100_presets_and_boundary(self):
        presets_dir = self.plugin_dir / "Content" / "presets"
        shutil.rmtree(presets_dir)
        presets_dir.mkdir(parents=True)
        for i in range(99):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"data")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("fewer than 100 presets", proc.stderr)

        (presets_dir / "preset_099.sbpreset").write_bytes(b"data")
        proc_ok = self._run_packager()
        self.assertEqual(proc_ok.returncode, 0)

    def test_apple_double_presets_in_subdirs_not_counted(self):
        presets_dir = self.plugin_dir / "Content" / "presets"
        shutil.rmtree(presets_dir)
        presets_dir.mkdir(parents=True)
        for i in range(99):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"data")

        hidden_dir = presets_dir / "._hidden_folder"
        hidden_dir.mkdir()
        (hidden_dir / "preset_099.sbpreset").write_bytes(b"data")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("fewer than 100 presets", proc.stderr)

    def test_failure_missing_version_xml(self):
        (self.plugin_dir / "version.xml").unlink()
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("version.xml", proc.stderr)

    def test_malformed_skin_cannot_be_released(self):
        (self.plugin_dir / "Plugin Skins/TUI.json").write_text("{}")
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("Invalid SynthyBoy skin", proc.stderr)
        self.assertEqual(list(self.out_dir.glob("*.zip")), [])

    def test_failure_missing_skin_files(self):
        (self.plugin_dir / "Plugin Skins" / "TUI.json").unlink()
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("TUI.json", proc.stderr)

        (self.plugin_dir / "Plugin Skins" / "TUI.json").write_text("{}", encoding="utf-8")
        (self.plugin_dir / "Plugin Skins" / "Q-Links.json").unlink()
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("Q-Links.json", proc2.stderr)

    def test_failure_empty_source_url(self):
        proc = self._run_packager("--source-url", "")
        self.assertEqual(proc.returncode, 1)
        self.assertIn("--source-url is required", proc.stderr)

    def test_failure_out_dir_inside_plugin_dir(self):
        inside_out = self.plugin_dir / "nested_out"
        proc = subprocess.run(
            [
                sys.executable,
                "-I",
                str(SCRIPT),
                "--plugin-dir",
                str(self.plugin_dir),
                "--out",
                str(inside_out),
                "--source-url",
                SOURCE_URL,
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 1)
        self.assertIn("cannot be inside", proc.stderr)

    def test_subfolder_install_and_license_preserved(self):
        sub_dir = self.plugin_dir / "Content" / "presets" / "PackA"
        sub_dir.mkdir(parents=True)
        sub_license = sub_dir / "LICENSE"
        sub_license.write_bytes(b"pack license")
        sub_install = sub_dir / "INSTALL.txt"
        sub_install.write_bytes(b"pack install")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        for zname in ("SynthyBoy-MPC-0.1.0-armv7.zip", "SynthyBoy-MPC-0.1.0-aarch64.zip"):
            with zipfile.ZipFile(self.out_dir / zname, "r") as zf:
                namelist = zf.namelist()
                self.assertIn("Safian - VST - SynthyBoy/Content/presets/PackA/LICENSE", namelist)
                self.assertIn("Safian - VST - SynthyBoy/Content/presets/PackA/INSTALL.txt", namelist)
                self.assertEqual(
                    zf.read("Safian - VST - SynthyBoy/Content/presets/PackA/LICENSE"), b"pack license"
                )
                self.assertEqual(
                    zf.read("Safian - VST - SynthyBoy/Content/presets/PackA/INSTALL.txt"), b"pack install"
                )

    def test_input_folder_not_modified(self):
        def snapshot_dir(dir_path: Path):
            res = {}
            for p in dir_path.rglob("*"):
                if p.is_file():
                    st = p.stat()
                    res[str(p.relative_to(dir_path))] = (st.st_size, st.st_mtime_ns, p.read_bytes())
            return res

        before = snapshot_dir(self.plugin_dir)
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)
        after = snapshot_dir(self.plugin_dir)

        self.assertEqual(before, after)

    def test_zip_permissions_and_attributes(self):
        bin32 = self.plugin_dir / "VST" / "synthyboy-arm32.so"
        bin32.chmod(0o755)

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        for zname in ("SynthyBoy-MPC-0.1.0-armv7.zip", "SynthyBoy-MPC-0.1.0-aarch64.zip"):
            with zipfile.ZipFile(self.out_dir / zname, "r") as zf:
                for info in zf.infolist():
                    self.assertEqual(info.create_system, 3, f"Non-unix create_system on {info.filename}")
                    self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                    mode = (info.external_attr >> 16) & 0o777
                    if info.filename.endswith(".so"):
                        self.assertEqual(mode, 0o755, f"Expected 0755 for binary {info.filename}")
                    else:
                        self.assertEqual(mode, 0o644, f"Expected 0644 for file {info.filename}")

    def test_gpu_helper_is_packaged_executable_and_validated(self):
        helper32 = self.plugin_dir / "VST" / "synthyboy-overlay-gpu-arm32"
        helper32.write_bytes(fake_elf(32, 40))
        helper32.chmod(0o755)
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        with zipfile.ZipFile(self.out_dir / "SynthyBoy-MPC-0.1.0-armv7.zip", "r") as zf:
            info = zf.getinfo("Safian - VST - SynthyBoy/VST/synthyboy-overlay-gpu-arm32")
            self.assertEqual((info.external_attr >> 16) & 0o777, 0o755)

        # Helper must NOT be present in aarch64 zip
        with zipfile.ZipFile(self.out_dir / "SynthyBoy-MPC-0.1.0-aarch64.zip", "r") as zf:
            self.assertNotIn("Safian - VST - SynthyBoy/VST/synthyboy-overlay-gpu-arm32", zf.namelist())

        helper32.write_bytes(fake_elf(64, 40))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF class", proc.stderr)
        helper32.write_bytes(fake_elf(32, 40))
        helper32.chmod(0o644)
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("not executable", proc.stderr)
        helper32.unlink()
        self.assertEqual(self._run_packager().returncode, 0, "a package without the helper (production) is still fine")

    def test_both_gpu_helpers_are_packaged_into_matching_archives(self):
        helper32 = self.plugin_dir / "VST" / "synthyboy-overlay-gpu-arm32"
        helper64 = self.plugin_dir / "VST" / "synthyboy-overlay-gpu-arm64"
        helper32.write_bytes(fake_elf(32, 40))
        helper64.write_bytes(fake_elf(64, 183))
        helper32.chmod(0o755)
        helper64.chmod(0o755)
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0, proc.stderr)

        with zipfile.ZipFile(self.out_dir / "SynthyBoy-MPC-0.1.0-armv7.zip", "r") as zf:
            namelist = zf.namelist()
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-overlay-gpu-arm32", namelist)
            self.assertNotIn("Safian - VST - SynthyBoy/VST/synthyboy-overlay-gpu-arm64", namelist)

        with zipfile.ZipFile(self.out_dir / "SynthyBoy-MPC-0.1.0-aarch64.zip", "r") as zf:
            namelist = zf.namelist()
            self.assertIn("Safian - VST - SynthyBoy/VST/synthyboy-overlay-gpu-arm64", namelist)
            self.assertNotIn("Safian - VST - SynthyBoy/VST/synthyboy-overlay-gpu-arm32", namelist)

        helper64.write_bytes(fake_elf(32, 40))
        helper64.chmod(0o755)
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF class", proc.stderr)

    def test_paths_with_spaces_in_parent_dir(self):
        spaced_root = self.tmp / "folder with spaces" / "Safian - VST - SynthyBoy"
        shutil.copytree(self.plugin_dir, spaced_root)
        spaced_out = self.tmp / "out with spaces"
        proc = subprocess.run(
            [
                sys.executable,
                "-I",
                str(SCRIPT),
                "--plugin-dir",
                str(spaced_root),
                "--out",
                str(spaced_out),
                "--source-url",
                SOURCE_URL,
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0, f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
        self.assertTrue((spaced_out / "SynthyBoy-MPC-0.1.0-armv7.zip").is_file())
        self.assertTrue((spaced_out / "SynthyBoy-MPC-0.1.0-aarch64.zip").is_file())

    def test_custom_version_flag(self):
        proc = self._run_packager("--version", "1.2.3")
        self.assertEqual(proc.returncode, 0)
        self.assertTrue((self.out_dir / "SynthyBoy-MPC-1.2.3-armv7.zip").is_file())
        self.assertTrue((self.out_dir / "SynthyBoy-MPC-1.2.3-aarch64.zip").is_file())


if __name__ == "__main__":
    unittest.main()
