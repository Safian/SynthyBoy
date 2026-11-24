#!/usr/bin/env python3
# SynthyBoy - tests verifying the Open MPC SDK tools/release.py packaging path
# and tools/catalog_check.py validation for both architectures (armv7 and aarch64).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

"""Offline packaging and catalog validation tests for Open MPC releases.

Note:
These tests use synthetic minimal ELF fixtures to verify offline packaging
mechanics and integration with Open MPC catalog verification tools.
They make no claims that synthetic fixtures validate final zips or production
firmware behavior on actual hardware devices.
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def _safe_is_file(p: Path) -> bool:
    """Check if path is a regular file without throwing on permission or OS errors."""
    try:
        return p.is_file()
    except (OSError, PermissionError):
        return False


# Candidate paths for genuine Open MPC tools.
# No private machine paths or checkout-internal git references are embedded.
RELEASE_CANDIDATES = [
    Path(os.environ.get("OPEN_MPC_RELEASE", "")),
    Path(os.environ.get("OPEN_MPC_TOOLS_DIR", "")) / "release.py",
    Path(os.environ.get("MPC_SDK", "")) / "tools" / "release.py",
    ROOT.parent / ".hw-local" / "mpc-knowledge" / "docs-community" / "open-mpc" / "tools" / "release.py",
]
OPEN_MPC_RELEASE = next((p for p in RELEASE_CANDIDATES if _safe_is_file(p)), None)

CATALOG_CHECK_CANDIDATES = [
    Path(os.environ.get("OPEN_MPC_CATALOG_CHECK", "")),
    Path(os.environ.get("OPEN_MPC_TOOLS_DIR", "")) / "catalog_check.py",
    Path(os.environ.get("MPC_SDK", "")) / "tools" / "catalog_check.py",
]
if OPEN_MPC_RELEASE:
    CATALOG_CHECK_CANDIDATES.append(OPEN_MPC_RELEASE.parent / "catalog_check.py")
CATALOG_CHECK_CANDIDATES.append(
    ROOT.parent / ".hw-local" / "mpc-knowledge" / "docs-community" / "open-mpc" / "tools" / "catalog_check.py"
)
OPEN_MPC_CATALOG_CHECK = next((p for p in CATALOG_CHECK_CANDIDATES if _safe_is_file(p)), None)


def fake_elf(bits, machine, endian=1):
    """Generates minimal valid ELF headers for offline tests."""
    fmt = "<" if endian == 1 else ">"
    ident = b"\x7fELF" + bytes([1 if bits == 32 else 2, endian, 1]) + bytes(9)
    if bits == 32:
        return ident + struct.pack(fmt + "HHIIIII", 3, machine, 1, 0, 0, 0, 0) + bytes(64)
    return ident + struct.pack(fmt + "HHIQQQI", 3, machine, 1, 0, 0, 0, 0) + bytes(64)


class TestOpenMpcRelease(unittest.TestCase):
    def setUp(self):
        if not OPEN_MPC_RELEASE:
            self.skipTest(
                "Open MPC tools/release.py not found in environment. "
                "Set OPEN_MPC_TOOLS_DIR or MPC_SDK to supply the dependency."
            )
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.out_dir = self.tmp / "dist"
        self.out_dir.mkdir()

        self.repo = "Safian/SynthyBoy"
        self.license = "AGPL-3.0-or-later"

        # Create mock skin directory: Safian - VST - SynthyBoy
        self.skin_dir = self.tmp / "Safian - VST - SynthyBoy"
        (self.skin_dir / "Plugin Skins").mkdir(parents=True)
        (self.skin_dir / "version.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n<plugincontent><version>0.1.0.1</version></plugincontent>\n',
            encoding="utf-8",
        )
        (self.skin_dir / "Plugin Skins" / "TUI.json").write_text("{}", encoding="utf-8")
        (self.skin_dir / "Plugin Skins" / "Q-Links.json").write_text("{}", encoding="utf-8")

        # Create mock entry files with hex uid
        self.entry_arm32 = self.tmp / "entry-arm32.xml"
        self.entry_arm32.write_text(
            '<PLUGIN name="SynthyBoy" file="synthyboy-arm32.so" format="VST" category="Synth" '
            'manufacturer="Safian" version="0.1.0" uid="53665342" isInstrument="1" numInputs="0" numOutputs="2" />\n',
            encoding="utf-8",
        )
        self.entry_arm64 = self.tmp / "entry-arm64.xml"
        self.entry_arm64.write_text(
            '<PLUGIN name="SynthyBoy" file="synthyboy-arm64.so" format="VST" category="Synth" '
            'manufacturer="Safian" version="0.1.0" uid="53665342" isInstrument="1" numInputs="0" numOutputs="2" />\n',
            encoding="utf-8",
        )

        # Mock binaries
        self.so_arm32 = self.tmp / "synthyboy-arm32.so"
        self.so_arm32.write_bytes(fake_elf(32, 40))
        self.so_arm64 = self.tmp / "synthyboy-arm64.so"
        self.so_arm64.write_bytes(fake_elf(64, 183))

    def tearDown(self):
        self._tmp.cleanup()

    def _package_release(self, arch: str, extra_args=None) -> Path:
        """Helper to invoke genuine Open MPC release.py packager."""
        if arch == "armv7":
            so_file = self.so_arm32
            entry_file = self.entry_arm32
            so_rel = "VST/synthyboy-arm32.so"
            expected_zip_name = "SynthyBoy-0.1.0-mpc-armv7.zip"
        elif arch == "aarch64":
            so_file = self.so_arm64
            entry_file = self.entry_arm64
            so_rel = "VST/synthyboy-arm64.so"
            expected_zip_name = "SynthyBoy-0.1.0-mpc-aarch64.zip"
        else:
            raise ValueError(f"Unknown arch: {arch}")

        cmd = [
            sys.executable,
            str(OPEN_MPC_RELEASE),
            "--so", str(so_file),
            "--skin", str(self.skin_dir),
            "--entry", str(entry_file),
            "--version", "0.1.0",
            "--repo", self.repo,
            "--license", self.license,
            "--so-path", so_rel,
            "-o", str(self.out_dir),
        ]
        if extra_args:
            cmd.extend(extra_args)

        proc = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, f"stdout: {proc.stdout}\nstderr: {proc.stderr}")

        zip_file = self.out_dir / expected_zip_name
        self.assertTrue(zip_file.is_file(), f"Expected {zip_file}")
        return zip_file

    def _run_catalog_check(self, zip_path: Path, *extra_args) -> subprocess.CompletedProcess:
        """Helper to invoke genuine Open MPC catalog_check.py tool interface."""
        if not OPEN_MPC_CATALOG_CHECK:
            self.skipTest(
                "Open MPC catalog_check.py not found in environment. "
                "Set OPEN_MPC_TOOLS_DIR or MPC_SDK to supply the dependency."
            )
        cmd = [
            sys.executable,
            str(OPEN_MPC_CATALOG_CHECK),
            str(zip_path),
            *extra_args,
        ]
        return subprocess.run(cmd, capture_output=True, text=True)

    def _call_catalog_check_fn(self, zip_path: Path, **kwargs):
        """Invoke genuine Open MPC catalog_check.check Python function interface."""
        if not OPEN_MPC_CATALOG_CHECK:
            self.skipTest(
                "Open MPC catalog_check.py not found in environment. "
                "Set OPEN_MPC_TOOLS_DIR or MPC_SDK to supply the dependency."
            )
        tools_dir = str(OPEN_MPC_CATALOG_CHECK.parent)
        if tools_dir not in sys.path:
            sys.path.insert(0, tools_dir)
        import catalog_check
        return catalog_check.check(str(zip_path), **kwargs)

    def test_open_mpc_release_armv7(self):
        """Verifies Open MPC tools/release.py packages armv7 (Key 61) into SynthyBoy-0.1.0-mpc-armv7.zip."""
        zip_file = self._package_release("armv7")

        with zipfile.ZipFile(zip_file, "r") as zf:
            names = set(zf.namelist())
            self.assertIn("SynthyBoy-0.1.0/install.sh", names)
            self.assertIn("SynthyBoy-0.1.0/uninstall.sh", names)
            self.assertIn("SynthyBoy-0.1.0/INSTALL.md", names)
            self.assertIn("SynthyBoy-0.1.0/mpc-plugin.json", names)
            self.assertIn("SynthyBoy-0.1.0/SHA256SUMS", names)
            self.assertIn("SynthyBoy-0.1.0/portable/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so", names)
            self.assertIn("SynthyBoy-0.1.0/portable/Safian - VST - SynthyBoy/plugin.json", names)

            pj = json.loads(zf.read("SynthyBoy-0.1.0/portable/Safian - VST - SynthyBoy/plugin.json").decode("utf-8"))
            self.assertEqual(pj.get("file32"), "VST/synthyboy-arm32.so")
            self.assertNotIn("file64", pj)
            self.assertEqual(pj.get("name"), "SynthyBoy")
            self.assertEqual(pj.get("manufacturer"), "Safian")

    def test_open_mpc_release_aarch64(self):
        """Verifies Open MPC tools/release.py packages aarch64 (XL) into SynthyBoy-0.1.0-mpc-aarch64.zip."""
        zip_file = self._package_release("aarch64")

        with zipfile.ZipFile(zip_file, "r") as zf:
            names = set(zf.namelist())
            self.assertIn("SynthyBoy-0.1.0/install.sh", names)
            self.assertIn("SynthyBoy-0.1.0/uninstall.sh", names)
            self.assertIn("SynthyBoy-0.1.0/INSTALL.md", names)
            self.assertIn("SynthyBoy-0.1.0/mpc-plugin.json", names)
            self.assertIn("SynthyBoy-0.1.0/SHA256SUMS", names)
            self.assertIn("SynthyBoy-0.1.0/portable/Safian - VST - SynthyBoy/VST/synthyboy-arm64.so", names)
            self.assertIn("SynthyBoy-0.1.0/portable/Safian - VST - SynthyBoy/plugin.json", names)

            pj = json.loads(zf.read("SynthyBoy-0.1.0/portable/Safian - VST - SynthyBoy/plugin.json").decode("utf-8"))
            self.assertEqual(pj.get("file64"), "VST/synthyboy-arm64.so")
            self.assertNotIn("file32", pj)
            self.assertEqual(pj.get("name"), "SynthyBoy")
            self.assertEqual(pj.get("manufacturer"), "Safian")

    def test_open_mpc_catalog_check_armv7(self):
        """Validates armv7 (Key 61) release archive against genuine Open MPC catalog checker."""
        zip_file = self._package_release("armv7")

        # CLI interface with --catalog, --expect-id, --expect-repo, --json
        proc = self._run_catalog_check(
            zip_file,
            "--catalog",
            "--expect-id", "synthyboy",
            "--expect-repo", self.repo,
            "--json",
        )
        self.assertEqual(proc.returncode, 0, f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
        record = json.loads(proc.stdout)

        manifest = record["manifest"]
        self.assertEqual(manifest["id"], "synthyboy")
        self.assertEqual(manifest["name"], "SynthyBoy")
        self.assertEqual(manifest["version"], "0.1.0")
        self.assertEqual(manifest["arch"], "armv7")
        self.assertEqual(manifest["kind"], "instrument")
        self.assertEqual(manifest["layout"], "portable")
        self.assertEqual(manifest["source_repo"], self.repo)
        self.assertEqual(manifest["license"], self.license)
        self.assertEqual(manifest["param_compat"], 0)
        self.assertTrue(record["defer"])

        # Also verify Python function interface
        errors, warnings, fn_rec = self._call_catalog_check_fn(
            zip_file,
            catalog=True,
            expect_id="synthyboy",
            expect_repo=self.repo,
        )
        self.assertEqual(errors, [])
        self.assertEqual(fn_rec["manifest"]["arch"], "armv7")

    def test_open_mpc_catalog_check_aarch64(self):
        """Validates aarch64 (XL) release archive against genuine Open MPC catalog checker."""
        zip_file = self._package_release("aarch64")

        # CLI interface with --catalog, --expect-id, --expect-repo, --json
        proc = self._run_catalog_check(
            zip_file,
            "--catalog",
            "--expect-id", "synthyboy",
            "--expect-repo", self.repo,
            "--json",
        )
        self.assertEqual(proc.returncode, 0, f"stdout: {proc.stdout}\nstderr: {proc.stderr}")
        record = json.loads(proc.stdout)

        manifest = record["manifest"]
        self.assertEqual(manifest["id"], "synthyboy")
        self.assertEqual(manifest["name"], "SynthyBoy")
        self.assertEqual(manifest["version"], "0.1.0")
        self.assertEqual(manifest["arch"], "aarch64")
        self.assertEqual(manifest["kind"], "instrument")
        self.assertEqual(manifest["layout"], "portable")
        self.assertEqual(manifest["source_repo"], self.repo)
        self.assertEqual(manifest["license"], self.license)
        self.assertEqual(manifest["param_compat"], 0)
        # Gen2 devices run MPC OS 3.x only
        self.assertEqual(record["os_compat"], ["3.x"])
        self.assertTrue(record["defer"])

        # Also verify Python function interface
        errors, warnings, fn_rec = self._call_catalog_check_fn(
            zip_file,
            catalog=True,
            expect_id="synthyboy",
            expect_repo=self.repo,
        )
        self.assertEqual(errors, [])
        self.assertEqual(fn_rec["manifest"]["arch"], "aarch64")
        self.assertEqual(fn_rec["os_compat"], ["3.x"])

    def test_open_mpc_catalog_check_rejects_checksum_mismatch(self):
        """Negative case: genuine catalog checker rejects an artifact with altered content (checksum mismatch)."""
        zip_file = self._package_release("armv7")

        tampered_zip = self.tmp / "tampered-checksum.zip"
        with zipfile.ZipFile(zip_file, "r") as zin, zipfile.ZipFile(tampered_zip, "w") as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename.endswith("INSTALL.md"):
                    data = b"Altered content without updating SHA256SUMS\n"
                zout.writestr(item, data)

        # CLI check
        proc = self._run_catalog_check(tampered_zip, "--catalog")
        self.assertEqual(proc.returncode, 1, f"Expected rejection, but got returncode 0.\nstdout: {proc.stdout}")
        self.assertIn("checksum mismatch: INSTALL.md", proc.stderr)

        # Python function interface check
        errors, warnings, record = self._call_catalog_check_fn(tampered_zip, catalog=True)
        self.assertTrue(any("checksum mismatch: INSTALL.md" in e for e in errors), f"Errors: {errors}")

    def test_open_mpc_catalog_check_rejects_architecture_mismatch(self):
        """Negative case: genuine catalog checker rejects an artifact whose ELF binary mismatches manifest arch."""
        zip_file = self._package_release("armv7")

        tampered_zip = self.tmp / "tampered-arch.zip"
        with zipfile.ZipFile(zip_file, "r") as zin:
            data = {item.filename: zin.read(item.filename) for item in zin.infolist()}

        top = [k.split("/")[0] for k in data.keys()][0]
        so_key = f"{top}/portable/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so"
        # Substitute 64-bit ELF into 32-bit armv7 package
        data[so_key] = fake_elf(64, 183)

        # Update SHA256SUMS so only the ELF architecture check fails
        sums = "\n".join(
            "%s  %s" % (hashlib.sha256(d).hexdigest(), n[len(top) + 1:])
            for n, d in sorted(data.items())
            if not n.endswith("/") and not n.endswith("SHA256SUMS")
        ) + "\n"
        data[f"{top}/SHA256SUMS"] = sums.encode()

        with zipfile.ZipFile(tampered_zip, "w") as zout:
            for k, v in data.items():
                zout.writestr(k, v)

        # CLI check
        proc = self._run_catalog_check(tampered_zip, "--catalog")
        self.assertEqual(proc.returncode, 1, f"Expected rejection, but got returncode 0.\nstdout: {proc.stdout}")
        self.assertIn("is not a 32-bit ARM library", proc.stderr)

        # Python function interface check
        errors, warnings, record = self._call_catalog_check_fn(tampered_zip, catalog=True)
        self.assertTrue(any("is not a 32-bit ARM library" in e for e in errors), f"Errors: {errors}")

    def test_open_mpc_catalog_check_rejects_uid_mismatch(self):
        """Negative case: genuine catalog checker rejects an artifact with plugin.json UID mismatch."""
        zip_file = self._package_release("armv7")

        tampered_zip = self.tmp / "tampered-uid.zip"
        with zipfile.ZipFile(zip_file, "r") as zin:
            data = {item.filename: zin.read(item.filename) for item in zin.infolist()}

        top = [k.split("/")[0] for k in data.keys()][0]
        pj_key = f"{top}/portable/Safian - VST - SynthyBoy/plugin.json"
        pj = json.loads(data[pj_key])
        pj["uid"] = "0xdeadbeef"
        data[pj_key] = (json.dumps(pj, indent=2) + "\n").encode()

        # Update SHA256SUMS so only the UID validation check fails
        sums = "\n".join(
            "%s  %s" % (hashlib.sha256(d).hexdigest(), n[len(top) + 1:])
            for n, d in sorted(data.items())
            if not n.endswith("/") and not n.endswith("SHA256SUMS")
        ) + "\n"
        data[f"{top}/SHA256SUMS"] = sums.encode()

        with zipfile.ZipFile(tampered_zip, "w") as zout:
            for k, v in data.items():
                zout.writestr(k, v)

        # CLI check
        proc = self._run_catalog_check(tampered_zip, "--catalog")
        self.assertEqual(proc.returncode, 1, f"Expected rejection, but got returncode 0.\nstdout: {proc.stdout}")
        self.assertIn("is not the manifest uid", proc.stderr)

        # Python function interface check
        errors, warnings, record = self._call_catalog_check_fn(tampered_zip, catalog=True)
        self.assertTrue(any("is not the manifest uid" in e for e in errors), f"Errors: {errors}")


if __name__ == "__main__":
    unittest.main()
