#!/usr/bin/env python3
# SynthyBoy - tests of the release scripts: package-release.py, fill-manifest.py and the modes of check-manifest.py (stdlib unittest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run: python3 -I -m unittest discover -s scripts/tests -v     (registered with CTest as synthyboy_test_release_scripts)
# The fixtures are tiny hand made binaries (only the headers the packager looks at) and the real factory preset pack.

import contextlib
import copy
import io
import importlib.util
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package_release = load("t_package_release", "scripts/package-release.py")
fill_manifest = load("t_fill_manifest", "scripts/fill-manifest.py")
check_manifest = load("t_check_manifest", "scripts/check-manifest.py")
contentpack = load("t_contentpack", "tools/contentpack/contentpack.py")

PLUGIN = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
PLATFORMS = list(PLUGIN["builds"])


def fake_elf(bits, machine, flags=0):
    ident = b"\x7fELF" + bytes([1 if bits == 32 else 2, 1, 1]) + bytes(9)
    if bits == 32:
        return ident + struct.pack("<HHIIIII", 3, machine, 1, 0, 0, 0, flags) + bytes(64)
    return ident + struct.pack("<HHIQQQI", 3, machine, 1, 0, 0, 0, flags) + bytes(64)


def fake_fat(cpu_types):
    data = struct.pack(">II", 0xCAFEBABE, len(cpu_types))
    for t in cpu_types:
        data += struct.pack(">IIIII", t, 0, 4096, 100, 14)
    return data + bytes(4096)


def fake_pe(machine):
    return b"MZ" + bytes(58) + struct.pack("<I", 64) + b"PE\0\0" + struct.pack("<H", machine) + bytes(64)


FAKE_BINARIES = {
    "linux-armv7": fake_elf(32, 40, 0x05000400),
    "linux-aarch64": fake_elf(64, 183),
    "linux-x86_64": fake_elf(64, 62),
    "macos-universal": fake_fat([package_release.CPU_TYPE_X86_64, package_release.CPU_TYPE_ARM64]),
    "windows-x64": fake_pe(0x8664),
}


def run_quiet(func, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = func(*args)
    return code, out.getvalue(), err.getvalue()


class Workspace(unittest.TestCase):
    """A temporary directory with a native fake binary per platform and a place for the assets."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.assets = self.tmp / "assets"
        self.assets.mkdir()
        self.bins = self.tmp / "bin"
        self.bins.mkdir()
        for platform, data in FAKE_BINARIES.items():
            (self.bins / platform).write_bytes(data)

    def package(self, platform, **kw):
        return package_release.package(PLUGIN, platform, self.bins / platform, self.assets, **kw)

    def package_all(self):
        return {p: self.package(p) for p in PLATFORMS}

    def build_pack(self):
        return contentpack.build_pack(ROOT / "content" / "presets", self.assets)

    def fill_args(self, *extra):
        return ["--assets", str(self.assets), *extra]


class PackageReleaseTests(Workspace):
    def test_every_platform_packages_with_the_expected_layout(self):
        for platform in PLATFORMS:
            result = self.package(platform)
            self.assertEqual(result["file"], "com.safian.synthyboy-0.1.0-%s.zip" % platform)
            with zipfile.ZipFile(self.assets / result["file"]) as zf:
                names = zf.namelist()
            self.assertEqual(sorted(names), ["LICENSE", "NOTICE", "README.txt", "THIRD_PARTY.md", "synthyboy.clap"])
            self.assertEqual((self.assets / (result["file"] + ".sha256")).read_text().split()[0], result["sha256"])

    def test_the_zip_is_reproducible(self):
        first = self.package("linux-x86_64")["sha256"]
        second = self.package("linux-x86_64")["sha256"]
        self.assertEqual(first, second)

    def test_wrong_architecture_is_refused(self):
        wrong = {"linux-armv7": "linux-aarch64", "linux-aarch64": "linux-x86_64", "linux-x86_64": "windows-x64",
                 "macos-universal": "linux-x86_64", "windows-x64": "macos-universal"}
        for platform, other in wrong.items():
            (self.bins / platform).write_bytes(FAKE_BINARIES[other])
            with self.assertRaises(package_release.PackageError, msg=platform):
                self.package(platform)
        self.assertEqual(os.listdir(self.assets), [])

    def test_soft_float_arm_and_thin_macho_are_refused(self):
        (self.bins / "linux-armv7").write_bytes(fake_elf(32, 40, 0x05000200))  # soft float ABI
        with self.assertRaises(package_release.PackageError):
            self.package("linux-armv7")
        (self.bins / "macos-universal").write_bytes(fake_fat([package_release.CPU_TYPE_ARM64]))
        with self.assertRaises(package_release.PackageError):
            self.package("macos-universal")
        (self.bins / "linux-x86_64").write_bytes(b"not a binary")
        with self.assertRaises(package_release.PackageError):
            self.package("linux-x86_64")

    @unittest.skipIf(os.name == "nt", "POSIX permission bits and symbolic links (the macOS bundles are packaged on macOS)")
    def test_juce_bundles_keep_permissions_and_symlinks(self):
        bundle = self.tmp / "SynthyBoy.vst3"
        (bundle / "Contents" / "MacOS").mkdir(parents=True)
        exe = bundle / "Contents" / "MacOS" / "SynthyBoy"
        exe.write_bytes(b"\xcf\xfa\xed\xfe")
        exe.chmod(0o755)
        (bundle / "Contents" / "Info.plist").write_text("<plist/>")
        os.symlink("MacOS/SynthyBoy", bundle / "Contents" / "link")
        clap = self.tmp / "SynthyBoy.clap"
        clap.write_bytes(b"clap")
        result = self.package("macos-universal", juce_vst3=bundle, juce_clap=clap)
        with zipfile.ZipFile(self.assets / result["file"]) as zf:
            names = zf.namelist()
            self.assertIn("juce/SynthyBoy.vst3/Contents/MacOS/SynthyBoy", names)
            self.assertIn("juce/SynthyBoy.clap", names)
            self.assertEqual((zf.getinfo("juce/SynthyBoy.vst3/Contents/MacOS/SynthyBoy").external_attr >> 16) & 0o777, 0o755)
            self.assertEqual((zf.getinfo("juce/SynthyBoy.vst3/Contents/Info.plist").external_attr >> 16) & 0o777, 0o644)
            link = zf.getinfo("juce/SynthyBoy.vst3/Contents/link")
            self.assertEqual((link.external_attr >> 16) & 0o170000, 0o120000)
            self.assertEqual(zf.read(link).decode(), "MacOS/SynthyBoy")
            self.assertIn("juce/SynthyBoy.vst3", zf.read("README.txt").decode())

    def test_command_line(self):
        code, out, err = run_quiet(package_release.main, ["--platform", "linux-armv7", "--native", str(self.bins / "linux-armv7"),
                                                           "--out", str(self.assets)])
        self.assertEqual(code, 0, err)
        self.assertIn("com.safian.synthyboy-0.1.0-linux-armv7.zip", out)
        code, _, err = run_quiet(package_release.main, ["--platform", "linux-armv7", "--native", str(self.bins / "linux-x86_64"),
                                                         "--out", str(self.assets)])
        self.assertEqual(code, 1)
        self.assertIn("ARM", err)


class FillManifestTests(Workspace):
    def fill(self, *extra):
        out = self.tmp / "plugin.json.release"
        code, stdout, stderr = run_quiet(fill_manifest.main, self.fill_args("--out", str(out), *extra))
        return code, stdout, stderr, out

    def test_github_release_dry_run_on_fake_artifacts(self):
        self.package_all()
        pack = self.build_pack()
        before = (ROOT / "plugin.json").read_bytes()
        code, stdout, stderr, out = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0", "--sums", str(self.tmp / "SHA256SUMS"))
        self.assertEqual(code, 0, stderr)
        self.assertEqual((ROOT / "plugin.json").read_bytes(), before, "the checked-in manifest must not change")
        filled = json.loads(out.read_text())
        for platform in PLATFORMS:
            name = "com.safian.synthyboy-0.1.0-%s.zip" % platform
            build = filled["builds"][platform]
            self.assertEqual(build["status"], "released")
            self.assertEqual(build["url"], "https://github.com/Safian/SynthyBoy/releases/download/v0.1.0/" + name)
            self.assertEqual(build["size"], (self.assets / name).stat().st_size)
            self.assertEqual(build["sha256"], check_manifest.sha256_file(self.assets / name))
            self.assertEqual(build["entry_point"], "synthyboy.clap")
        cp = filled["contentPacks"][0]
        self.assertEqual((cp["status"], cp["sha256"], cp["size"]), ("released", pack["sha256"], pack["size"]))
        self.assertTrue(cp["url"].endswith("/com.safian.synthyboy.factory-presets-1.2.0.zip"))
        self.assertEqual(filled["assets"], [{"id": cp["id"], "name": cp["name"], "version": cp["version"], "url": cp["url"],
                                             "sha256": cp["sha256"], "size": cp["size"], "required": False, "type": "presets",
                                             "license": "CC0-1.0"}])
        # the checker agrees, also when it is run on its own against the asset directory
        code, _, err = run_quiet(check_manifest.main, ["--released", "--assets", str(self.assets), str(out)])
        self.assertEqual(code, 0, err)
        sums = (self.tmp / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(len(sums), len(PLATFORMS) + 2)
        self.assertTrue(sums[-1].endswith("  plugin.json.release"))

    def test_the_key_order_of_the_input_is_kept(self):
        self.package_all()
        self.build_pack()
        _, _, _, out = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        filled = json.loads(out.read_text())
        self.assertEqual(list(filled)[:len(PLUGIN)], list(PLUGIN))
        self.assertEqual(list(filled["builds"]), PLATFORMS)
        self.assertEqual(list(filled["builds"]["linux-armv7"]), ["status", "url", "sha256", "size", "entry_point"])
        self.assertEqual(list(filled["contentPacks"][0]), list(PLUGIN["contentPacks"][0]))

    def test_tag_must_match_the_version(self):
        self.package_all()
        self.build_pack()
        code, _, err, out = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.1")
        self.assertEqual(code, 1)
        self.assertIn("does not match", err)
        self.assertFalse(out.exists())

    def test_missing_asset_is_an_error_unless_partial(self):
        self.package("linux-armv7")
        self.build_pack()
        code, _, err, out = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        self.assertEqual(code, 1)
        for platform in PLATFORMS[1:]:
            self.assertIn(platform, err)
        self.assertFalse(out.exists())

    def test_missing_pack_is_an_error(self):
        self.package_all()
        code, _, err, _ = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        self.assertEqual(code, 1)
        self.assertIn("factory-presets", err)

    def test_partial_local_manifest_lists_only_what_was_built(self):
        self.package("linux-armv7")
        self.package("macos-universal")
        code, stdout, stderr, out = self.fill("--relative", "--partial")
        self.assertEqual(code, 0, stderr)
        filled = json.loads(out.read_text())
        self.assertEqual(list(filled["builds"]), ["linux-armv7", "macos-universal"])
        self.assertEqual(filled["contentPacks"], [])
        self.assertEqual(filled["assets"], [])
        # the manifest is in tmp, the zips in tmp/assets: relative to the manifest
        self.assertEqual(filled["builds"]["linux-armv7"]["url"], "assets/com.safian.synthyboy-0.1.0-linux-armv7.zip")
        code, _, err = run_quiet(check_manifest.main, ["--local", str(out)])
        self.assertEqual(code, 0, err)
        # ... and the same file is not a valid release manifest
        code, _, _ = run_quiet(check_manifest.main, ["--released", str(out)])
        self.assertEqual(code, 1)
        # next to the zips (the default place) the urls are the bare file names
        code, _, err = run_quiet(fill_manifest.main, self.fill_args("--relative", "--partial"))
        self.assertEqual(code, 0, err)
        default = json.loads((self.assets / "plugin.json.release").read_text())
        self.assertEqual(default["builds"]["macos-universal"]["url"], "com.safian.synthyboy-0.1.0-macos-universal.zip")

    def test_file_urls(self):
        self.package("linux-armv7")
        self.build_pack()
        code, _, stderr, out = self.fill("--file-urls", "--partial")
        self.assertEqual(code, 0, stderr)
        filled = json.loads(out.read_text())
        url = filled["builds"]["linux-armv7"]["url"]
        self.assertTrue(url.startswith("file:///"), url)
        self.assertEqual(filled["assets"][0]["url"], filled["contentPacks"][0]["url"])
        code, _, err = run_quiet(check_manifest.main, ["--local", str(out)])
        self.assertEqual(code, 0, err)

    def test_partial_without_any_build_fails(self):
        self.build_pack()
        code, _, err, _ = self.fill("--relative", "--partial")
        self.assertEqual(code, 1)
        self.assertIn("no platform build", err)

    def test_a_file_that_changed_after_packaging_is_detected(self):
        self.package_all()
        self.build_pack()
        sidecar = self.assets / "com.safian.synthyboy-0.1.0-windows-x64.zip.sha256"
        sidecar.write_text("0" * 64 + "  com.safian.synthyboy-0.1.0-windows-x64.zip\n")
        code, _, err, _ = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        self.assertEqual(code, 1)
        self.assertIn("changed after packaging", err)

    def test_broken_and_incomplete_zips_are_refused(self):
        self.package_all()
        self.build_pack()
        target = self.assets / "com.safian.synthyboy-0.1.0-linux-x86_64.zip"
        with zipfile.ZipFile(target, "w") as zf:  # no .clap at the root
            zf.writestr("LICENSE", "x")
            zf.writestr("README.txt", "x")
            zf.writestr("sub/synthyboy.clap", "x")
        (self.assets / (target.name + ".sha256")).unlink()
        code, _, err, _ = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        self.assertEqual(code, 1)
        self.assertIn("exactly one .clap", err)
        target.write_bytes(b"this is not a zip")
        code, _, err, _ = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        self.assertEqual(code, 1)
        self.assertIn("not a readable zip", err)

    def test_a_pack_zip_with_a_changed_file_is_refused(self):
        self.package_all()
        self.build_pack()
        target = self.assets / "com.safian.synthyboy.factory-presets-1.2.0.zip"
        with zipfile.ZipFile(target) as zf:
            members = {n: zf.read(n) for n in zf.namelist()}
        first = next(n for n in members if n.endswith(".sbpreset"))
        members[first] = members[first] + b"x"
        with zipfile.ZipFile(target, "w") as zf:
            for n, data in members.items():
                zf.writestr(n, data)
        code, _, err, _ = self.fill("--repo", "Safian/SynthyBoy", "--tag", "v0.1.0")
        self.assertEqual(code, 1)
        self.assertIn("mismatch", err)


class CheckManifestModeTests(Workspace):
    def released(self):
        self.package_all()
        self.build_pack()
        out = self.tmp / "plugin.json.release"
        code, _, err = run_quiet(fill_manifest.main, self.fill_args("--out", str(out), "--repo", "Safian/SynthyBoy", "--tag", "v0.1.0"))
        self.assertEqual(code, 0, err)
        return out, json.loads(out.read_text())

    def check(self, manifest, mode="released", assets=True):
        path = self.tmp / "candidate.json"
        path.write_text(json.dumps(manifest))
        return check_manifest.check_manifest(path, mode, self.assets if assets else None)[1]

    def test_the_checked_in_manifest_passes_and_is_not_a_release(self):
        self.assertEqual(check_manifest.check_manifest(ROOT / "plugin.json")[1], [])
        errors = check_manifest.check_manifest(ROOT / "plugin.json", "released")[1]
        self.assertTrue(any("unbuilt" in e for e in errors))

    def test_checked_in_placeholders_are_enforced(self):
        broken = copy.deepcopy(PLUGIN)
        broken["builds"]["linux-armv7"]["sha256"] = "a" * 64
        broken["contentPacks"][0]["size"] = 5
        errors = self.check(broken, "checked-in", False)
        self.assertEqual(len([e for e in errors if "placeholders" in e]), 2, errors)

    def test_released_manifest_is_valid_and_each_tampering_is_caught(self):
        _, ok = self.released()
        self.assertEqual(self.check(ok), [])

        tampered = copy.deepcopy(ok)
        tampered["builds"]["linux-armv7"]["sha256"] = "b" * 64
        self.assertTrue(any("sha256 does not match" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        tampered["builds"]["windows-x64"]["size"] += 1
        self.assertTrue(any("size" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        tampered["builds"]["linux-x86_64"]["url"] = "http://example.com/x.zip"
        self.assertTrue(any("https" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        tampered["builds"]["linux-x86_64"]["url"] = tampered["builds"]["linux-x86_64"]["url"].replace("x86_64", "aarch64")
        self.assertTrue(any("asset name" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        del tampered["builds"]["macos-universal"]["entry_point"]
        self.assertTrue(any("entry_point" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        tampered["assets"][0]["sha256"] = "c" * 64
        self.assertTrue(any("assets" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        del tampered["assets"]
        self.assertTrue(any("assets" in e for e in self.check(tampered)))

        tampered = copy.deepcopy(ok)
        tampered["builds"]["linux-armv7"]["status"] = "unbuilt"
        self.assertTrue(self.check(tampered))

    def test_limit_of_one_release_asset(self):
        _, ok = self.released()
        big = copy.deepcopy(ok)
        big["contentPacks"][0]["size"] = 2_100_000_000
        big["assets"] = check_manifest.kick_assets(big)
        self.assertTrue(any("limit" in e for e in self.check(big, assets=False)))

    def test_pack_identity_must_match_the_pack_manifest(self):
        broken = copy.deepcopy(PLUGIN)
        broken["contentPacks"][0]["version"] = "9.9.9"
        self.assertTrue(any("version" in e for e in self.check(broken, "checked-in", False)))
        broken = copy.deepcopy(PLUGIN)
        broken["contentPacks"][0]["license"] = "MIT"
        self.assertTrue(any("license" in e for e in self.check(broken, "checked-in", False)))
        broken = copy.deepcopy(PLUGIN)
        broken["contentPacks"].append(copy.deepcopy(broken["contentPacks"][0]))
        self.assertTrue(any("twice" in e for e in self.check(broken, "checked-in", False)))
        broken = copy.deepcopy(PLUGIN)
        broken["contentPacks"][0]["path"] = "content/does-not-exist"
        self.assertTrue(any("does not exist" in e for e in self.check(broken, "checked-in", False)))

    def test_requires_plugin_semantics(self):
        ok = check_manifest.requires_ok
        self.assertTrue(ok(">=0.1.0", "0.1.0"))
        self.assertTrue(ok(">=0.1.0", "0.10.0"))
        self.assertTrue(ok(">=0.9.0", "1.0.0"))
        self.assertFalse(ok(">=0.2.0", "0.1.9"))
        self.assertTrue(ok("0.1.0", "0.1.0"))
        self.assertFalse(ok("0.1.0", "0.1.1"))
        self.assertFalse(ok("", "0.1.0"))
        self.assertFalse(ok("^0.1.0", "0.1.0"))

    def test_url_kinds(self):
        self.assertEqual(check_manifest.url_kind("https://x/y.zip"), "https")
        self.assertEqual(check_manifest.url_kind("file:///tmp/y.zip"), "file")
        self.assertEqual(check_manifest.url_kind("y.zip"), "relative")
        self.assertEqual(check_manifest.url_kind("sub/y.zip"), "relative")
        self.assertEqual(check_manifest.url_kind("../assets/y.zip"), "relative")
        for bad in ("/etc/y.zip", "ftp://x/y.zip", "a\\b.zip", "C:/y.zip"):
            self.assertEqual(check_manifest.url_kind(bad), "bad", bad)


if __name__ == "__main__":
    unittest.main()
