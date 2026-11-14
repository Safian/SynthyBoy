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

    def _create_synthetic_plugin_folder(self, base_dir: Path, num_presets: int = 105) -> Path:
        folder = base_dir / "Safian - VST - SynthyBoy"
        folder.mkdir(parents=True, exist_ok=True)

        vst_dir = folder / "VST"
        vst_dir.mkdir(parents=True, exist_ok=True)
        bin32 = vst_dir / "synthyboy-arm32.so"
        bin32.write_bytes(fake_elf(32, 40))
        bin32.chmod(0o755)
        bin64 = vst_dir / "synthyboy-arm64.so"
        bin64.write_bytes(fake_elf(64, 183))
        bin64.chmod(0o755)

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

    def test_failure_wrong_elf_machine(self):
        # x86_64 machine (62) in arm32
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 62))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF machine", proc.stderr)

        # ARM machine (40) in arm64
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").write_bytes(fake_elf(64, 40))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("wrong ELF machine", proc2.stderr)

    def test_failure_wrong_elf_endianness(self):
        # Big-endian ELF for arm32
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40, endian=2))
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("wrong ELF endianness", proc.stderr)

        # Big-endian ELF for arm64
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(fake_elf(32, 40, endian=1))
        (self.plugin_dir / "VST" / "synthyboy-arm64.so").write_bytes(fake_elf(64, 183, endian=2))
        proc2 = self._run_packager()
        self.assertEqual(proc2.returncode, 1)
        self.assertIn("wrong ELF endianness", proc2.stderr)

    def test_failure_not_elf_or_truncated(self):
        # Not an ELF file
        (self.plugin_dir / "VST" / "synthyboy-arm32.so").write_bytes(b"not an elf binary file")
        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("not an ELF binary", proc.stderr)

        # Truncated ELF header (< 52 bytes)
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
        # Exactly 99 presets: must fail
        for i in range(99):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"data")

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 1)
        self.assertIn("fewer than 100 presets", proc.stderr)

        # 100 presets: boundary case, must succeed
        (presets_dir / "preset_099.sbpreset").write_bytes(b"data")
        proc_ok = self._run_packager()
        self.assertEqual(proc_ok.returncode, 0)

    def test_apple_double_presets_in_subdirs_not_counted(self):
        presets_dir = self.plugin_dir / "Content" / "presets"
        shutil.rmtree(presets_dir)
        presets_dir.mkdir(parents=True)
        for i in range(99):
            (presets_dir / f"preset_{i:03d}.sbpreset").write_bytes(b"data")

        # 100th preset is inside an AppleDouble directory: must not be counted
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

        zip_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip"
        with zipfile.ZipFile(zip_path, "r") as zf:
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
        # Set executable permissions on binary
        bin32 = self.plugin_dir / "VST" / "synthyboy-arm32.so"
        bin32.chmod(0o755)

        proc = self._run_packager()
        self.assertEqual(proc.returncode, 0)

        zip_path = self.out_dir / "SynthyBoy-MPC-0.1.0.zip"
        with zipfile.ZipFile(zip_path, "r") as zf:
            for info in zf.infolist():
                self.assertEqual(info.create_system, 3, f"Non-unix create_system on {info.filename}")
                self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                mode = (info.external_attr >> 16) & 0o777
                if info.filename.endswith(".so"):
                    self.assertEqual(mode, 0o755, f"Expected 0755 for binary {info.filename}")
                else:
                    self.assertEqual(mode, 0o644, f"Expected 0644 for file {info.filename}")

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
        self.assertTrue((spaced_out / "SynthyBoy-MPC-0.1.0.zip").is_file())

    def test_custom_version_flag(self):
        proc = self._run_packager("--version", "1.2.3")
        self.assertEqual(proc.returncode, 0)
        zip_path = self.out_dir / "SynthyBoy-MPC-1.2.3.zip"
        self.assertTrue(zip_path.is_file())


if __name__ == "__main__":
    unittest.main()
