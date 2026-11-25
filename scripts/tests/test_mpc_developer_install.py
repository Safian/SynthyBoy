#!/usr/bin/env python3
# Offline fixture for the Key 61 and XL developer package installers.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
SSH_STUB = r"""
import os
from pathlib import Path
import subprocess
import sys

root = Path(os.environ["SSH_STUB_ROOT"])
host, command = sys.argv[-2:]
with (root / "ssh.log").open("a") as log:
    print(host, file=log)
routes = {
    "root@192.168.0.30": ("/sdcard/VST2 Plugins", "/var/tmp/filmstrips", "key61"),
    "root@192.168.0.126": ("/nvme/VST2 Plugins", "/var/volatile/tmp/filmstrips", "xl"),
}
plugin_root, cache, device = routes[host]
assert command.startswith(f"plugin_root='{plugin_root}'; filmstrips='{cache}'; ")
command = command.replace(plugin_root, str(root / device / "VST2 Plugins"))
command = command.replace(cache, str(root / device / "filmstrips"))
command = command.replace("/tmp/synthyboy-overlay-", str(root / device / "synthyboy-overlay-"))
stubs = '''
systemctl() {
    [ "$#" = 2 ] && [ "$2" = acvs ] || return 1
    printf '%s\n' "$1" >> "$SSH_STUB_ROOT/service.log"
    if [ "$1" = start ] && [ "${FAIL_FIRST_START:-0}" = 1 ] &&
       [ ! -e "$SSH_STUB_ROOT/failed-once" ]; then
        touch "$SSH_STUB_ROOT/failed-once"
        return 1
    fi
}
sleep() { :; }
md5sum() { test -s "$1"; }
sha256sum() { shasum -a 256 "$@"; }
'''
archive = sys.stdin.buffer.read()
if os.environ.get("CORRUPT_SKIN"):
    archive = archive.replace(b"tile" + bytes(508), b"oops" + bytes(508), 1)
if os.environ.get("BROKEN_UPLOAD"):
    archive = b"truncated archive"
sys.exit(subprocess.run(["/bin/sh", "-c", stubs + command], input=archive).returncode)
"""


@unittest.skipIf(sys.platform == "win32", "the developer installers require a Unix shell")
class TestMpcDeveloperInstall(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.package = self.root / "source" / "Safian - VST - SynthyBoy"
        stub = self.root / "ssh"
        stub.write_text(f"#!{sys.executable}\n{SSH_STUB}", encoding="utf-8")
        stub.chmod(0o755)

    def write(self, root, name, contents):
        entry = root / name
        entry.parent.mkdir(parents=True, exist_ok=True)
        entry.write_bytes(contents)

    def run_installer(self, device, *args, **flags):
        env = dict(os.environ, PATH=f"{self.root}{os.pathsep}{os.defpath}",
                   SSH_STUB_ROOT=str(self.root), **flags)
        return subprocess.run(
            [str(SCRIPTS / f"{device}-install.sh"), str(self.package), *args],
            env=env, capture_output=True, text=True, timeout=15,
        )

    def target(self, device):
        return self.root / device / "VST2 Plugins" / "Safian - VST - SynthyBoy"

    def make_package(self, arch=None):
        files = {
            "Plugin Skins/browser_images/soundsmode.png": b"tile",
            "plugin.json": b'{"manufacturer":"Safian","name":"SynthyBoy","uid":"SfSB"}',
            "version.xml": b"<version><uiversion>0.1.0.0</uiversion></version>",
        }
        if arch:
            files.update({
                f"VST/synthyboy-{arch}.so": b"binary",
                f"VST/synthyboy-overlay-gpu-{arch}": b"helper",
                "Presets/Factory preset.xpl": b"preset",
                "Content/presets/Factory preset.sbpreset": b"state",
                "Content/packs/bank/wavetable.wav": b"wavetable",
                "Content/packs/bank/samples/Kick.wav": b"sample",
            })
        for name, contents in files.items():
            self.write(self.package, name, contents)
        from mpc_skin_fixture import write_native_skin_stub
        skins = self.package / "Plugin Skins"
        write_native_skin_stub(skins)
        files.update({str(p.relative_to(self.package)): p.read_bytes()
                      for p in skins.glob("*") if p.is_file()})
        return files

    def assert_files(self, directory, files):
        for name, contents in files.items():
            self.assertEqual((directory / name).read_bytes(), contents, name)

    def events(self):
        return (self.root / "service.log").read_text().splitlines()

    def test_full_package_on_each_device(self):
        for device, remote_name, arch, other_arch, host in (
            ("k61", "key61", "arm32", "arm64", "root@192.168.0.30"),
            ("xl", "xl", "arm64", "arm32", "root@192.168.0.126"),
        ):
            with self.subTest(device=device):
                self.package = self.root / device / "source" / "Safian - VST - SynthyBoy"
                files = self.make_package(arch)
                target = self.target(remote_name)
                for folder in ("Plugin Skins", "Presets", "Content", "VST"):
                    self.write(target, f"{folder}/obsolete", b"stale")
                self.write(target, "local-note", b"keep")
                self.write(target, f"VST/synthyboy-{other_arch}.so", b"other arch")
                self.write(self.package, "Content/packs/bank/._wavetable.wav", b"AppleDouble")
                self.write(self.root / remote_name, "filmstrips/temp_123.img", b"cache")
                self.write(self.root / remote_name, "filmstrips/keep.img", b"keep")
                extra = ("noreopen",) if device == "xl" else ()
                result = self.run_installer(device, *extra)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("installed; app started", result.stdout)
                self.assert_files(target, files)
                self.assert_files(target, {
                    "local-note": b"keep", f"VST/synthyboy-{other_arch}.so": b"other arch",
                })
                for folder in ("Plugin Skins", "Presets", "Content", "VST"):
                    self.assertFalse((target / folder / "obsolete").exists())
                self.assertFalse((target / "Content/packs/bank/._wavetable.wav").exists())
                self.assertFalse((self.root / remote_name / "filmstrips/temp_123.img").exists())
                self.assertTrue((self.root / remote_name / "filmstrips/keep.img").exists())
                self.assertFalse((target.parent / ".synthyboy-install").exists())
                self.assertEqual((self.root / "ssh.log").read_text().splitlines()[-1], host)
        self.assertEqual(self.events(), ["stop", "start", "stop", "start"])

    def test_skin_only_package_keeps_omitted_folders(self):
        files = self.make_package()
        target = self.target("key61")
        installed = {
            "VST/synthyboy-arm32.so": b"current",
            "Presets/current.xpl": b"current preset",
            "Content/presets/current.sbpreset": b"current state",
        }
        for name, contents in installed.items():
            self.write(target, name, contents)
        result = self.run_installer("k61")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_files(target, {**files, **installed})
        self.assertEqual(self.events(), ["stop", "start"])

    def test_wrong_architecture_fails_before_ssh(self):
        self.make_package("arm64")
        result = self.run_installer("k61")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing VST/synthyboy-arm32.so", result.stderr)
        self.assertFalse((self.root / "ssh.log").exists())

    def test_missing_tui_fails_before_ssh(self):
        self.make_package("arm32")
        (self.package / "Plugin Skins/TUI.json").unlink()
        self.assertNotEqual(self.run_installer("k61").returncode, 0)
        self.assertFalse((self.root / "ssh.log").exists())

    def test_corrupt_skin_transfer_leaves_installation_running(self):
        self.make_package("arm32")
        result = self.run_installer("k61", CORRUPT_SKIN="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "service.log").exists())

    def test_incomplete_transfer_leaves_installation_running(self):
        self.make_package("arm64")
        target = self.target("xl")
        self.write(target, "VST/synthyboy-arm64.so", b"current")
        result = self.run_installer("xl", BROKEN_UPLOAD="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "service.log").exists())
        self.assert_files(target, {"VST/synthyboy-arm64.so": b"current"})
        self.assertFalse((target.parent / ".synthyboy-install").exists())

    def test_failed_restart_restores_previous_package(self):
        self.make_package("arm64")
        target = self.target("xl")
        installed = {"VST/synthyboy-arm64.so": b"current", "Presets/current.xpl": b"old"}
        for name, contents in installed.items():
            self.write(target, name, contents)
        result = self.run_installer("xl", FAIL_FIRST_START="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("installed; app started", result.stdout)
        self.assertEqual(self.events(), ["stop", "start", "start"])
        self.assert_files(target, installed)
        self.assertFalse((target / "Plugin Skins").exists())
        self.assertFalse((target.parent / ".synthyboy-install").exists())


if __name__ == "__main__":
    unittest.main()
