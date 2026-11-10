#!/usr/bin/env python3
# SynthyBoy - tests of tools/contentpack/contentpack.py (stdlib unittest).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run: python3 -I -m unittest discover -s tools/contentpack/tests -v     (registered with CTest as synthyboy_test_contentpack)

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import random
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent.parent

spec = importlib.util.spec_from_file_location("t_contentpack", ROOT / "tools" / "contentpack" / "contentpack.py")
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)


def run_quiet(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = cp.main(argv)
    return code, out.getvalue(), err.getvalue()


class PackTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.pack = self.tmp / "pack"

    def make_instruments(self, names=("Bass Pluck", "Full Strings LE"), size=2000):
        for name in names:
            d = self.pack / name
            d.mkdir(parents=True, exist_ok=True)
            (d / "instrument.sfz").write_text("<region> sample=a.wav\n")
            (d / "a.wav").write_bytes(random.Random(name).getrandbits(8 * size).to_bytes(size, "little"))  # incompressible
        code, _, err = run_quiet(["manifest", "--dir", str(self.pack), "--id", "com.safian.synthyboy.test-instruments", "--name", "Test Instruments",
                                  "--version", "1.0.0", "--kind", "samples", "--license", "CC0-1.0", "--attribution", "Test",
                                  "--requires-plugin", ">=0.1.0", "--description", "fixture"])
        self.assertEqual(code, 0, err)

    def test_manifest_lists_every_file_sorted_with_hash_and_size(self):
        self.make_instruments()
        manifest = json.loads((self.pack / "contentpack.json").read_text())
        self.assertEqual([f["path"] for f in manifest["files"]],
                         ["Bass Pluck/a.wav", "Bass Pluck/instrument.sfz", "Full Strings LE/a.wav", "Full Strings LE/instrument.sfz"])
        first = manifest["files"][0]
        self.assertEqual(first["size"], 2000)
        self.assertEqual(first["sha256"], hashlib.sha256((self.pack / first["path"]).read_bytes()).hexdigest())
        self.assertEqual(list(manifest)[:3], ["schemaVersion", "id", "name"])
        self.assertEqual(run_quiet(["manifest", "--dir", str(self.pack), "--check"])[0], 0)
        (self.pack / "Bass Pluck" / "a.wav").write_bytes(b"changed")
        self.assertEqual(run_quiet(["manifest", "--dir", str(self.pack), "--check"])[0], 1)

    def test_manifest_without_metadata_is_refused(self):
        (self.pack).mkdir()
        (self.pack / "x.sbpreset").write_text("x")
        code, _, err = run_quiet(["manifest", "--dir", str(self.pack)])
        self.assertEqual(code, 1)
        self.assertIn("invalid", err)

    def test_build_is_verified_reproducible_and_loadable_layout(self):
        self.make_instruments()
        out = self.tmp / "out"
        code, stdout, err = run_quiet(["build", "--dir", str(self.pack), "--out", str(out), "--summary", str(self.tmp / "s.json")])
        self.assertEqual(code, 0, err)
        zip_path = out / "com.safian.synthyboy.test-instruments-1.0.0.zip"
        with zipfile.ZipFile(zip_path) as zf:
            self.assertEqual(zf.namelist()[0], "contentpack.json")
            self.assertEqual(sorted(zf.namelist()[1:]), sorted(f["path"] for f in json.loads(zf.read("contentpack.json"))["files"]))
        summary = json.loads((self.tmp / "s.json").read_text())
        self.assertEqual(summary["sha256"], hashlib.sha256(zip_path.read_bytes()).hexdigest())
        self.assertEqual(summary["size"], zip_path.stat().st_size)
        self.assertEqual(cp.verify_zip(zip_path, "com.safian.synthyboy.test-instruments", "1.0.0"), [])
        again = cp.build_pack(self.pack, self.tmp / "out2")
        self.assertEqual(again["sha256"], summary["sha256"])

    def test_oversized_pack_is_refused_and_leaves_no_file(self):
        self.make_instruments(size=64000)
        out = self.tmp / "out"
        code, _, err = run_quiet(["build", "--dir", str(self.pack), "--out", str(out), "--max-size", "1000"])
        self.assertEqual(code, 1)
        self.assertIn("split the pack", err)
        self.assertEqual(os.listdir(out), [])

    def test_split_makes_one_valid_pack_per_instrument_below_the_limit(self):
        self.make_instruments(names=("Bass Pluck", "Full Strings LE", "Zebra 2"), size=30000)
        split = self.tmp / "split"
        code, stdout, err = run_quiet(["split", "--dir", str(self.pack), "--out", str(split)])
        self.assertEqual(code, 0, err)
        self.assertEqual(sorted(os.listdir(split)), ["bass-pluck", "full-strings-le", "zebra-2"])
        for slug in os.listdir(split):
            manifest = json.loads((split / slug / "contentpack.json").read_text())
            self.assertEqual(manifest["id"], "com.safian.synthyboy.test-instruments-" + slug)
            self.assertEqual(manifest["version"], "1.0.0")
            self.assertEqual(manifest["kind"], "samples")
            self.assertEqual(cp.verify_directory(split / slug), [])
            summary = cp.build_pack(split / slug, self.tmp / "zips", max_bytes=50000)  # each part fits where the whole did not
            self.assertLess(summary["size"], 50000)
        whole = run_quiet(["build", "--dir", str(self.pack), "--out", str(self.tmp / "whole"), "--max-size", "50000"])
        self.assertEqual(whole[0], 1)

    def test_split_refuses_loose_files_and_colliding_names(self):
        self.make_instruments()
        (self.pack / "loose.txt").write_text("x")
        run_quiet(["manifest", "--dir", str(self.pack)])
        self.assertEqual(run_quiet(["split", "--dir", str(self.pack), "--out", str(self.tmp / "s")])[0], 1)
        (self.pack / "loose.txt").unlink()
        (self.pack / "bass-pluck").mkdir()
        (self.pack / "bass-pluck" / "x.wav").write_text("x")
        run_quiet(["manifest", "--dir", str(self.pack)])
        code, _, err = run_quiet(["split", "--dir", str(self.pack), "--out", str(self.tmp / "s2")])
        self.assertEqual(code, 1)
        self.assertIn("same id part", err)

    def test_verify_finds_tampering(self):
        self.make_instruments()
        self.assertEqual(run_quiet(["verify", "--dir", str(self.pack)])[0], 0)
        (self.pack / "Bass Pluck" / "a.wav").write_bytes(b"x" * 2000)  # same size, other content
        code, out, _ = run_quiet(["verify", "--dir", str(self.pack)])
        self.assertEqual(code, 1)
        self.assertIn("checksum mismatch", out)
        (self.pack / "Bass Pluck" / "extra.wav").write_bytes(b"1")
        self.assertIn("not listed", run_quiet(["verify", "--dir", str(self.pack)])[1])
        (self.pack / "Full Strings LE" / "a.wav").unlink()
        self.assertIn("missing file", run_quiet(["verify", "--dir", str(self.pack)])[1])

    def test_verify_zip_finds_tampering_and_unsafe_members(self):
        self.make_instruments()
        zip_path = Path(cp.build_pack(self.pack, self.tmp / "out")["file"])
        zip_path = self.tmp / "out" / zip_path
        self.assertEqual(cp.verify_zip(zip_path, expect_id="com.safian.synthyboy.other"), [
            "%s: pack id 'com.safian.synthyboy.test-instruments', expected 'com.safian.synthyboy.other'" % zip_path])
        with zipfile.ZipFile(zip_path) as zf:
            members = {n: zf.read(n) for n in zf.namelist()}

        def rewrite(mutate):
            changed = dict(members)
            mutate(changed)
            target = self.tmp / "t.zip"
            with zipfile.ZipFile(target, "w") as zf:
                for n, data in changed.items():
                    zf.writestr(n, data)
            return cp.verify_zip(target)

        self.assertTrue(any("checksum mismatch" in e for e in rewrite(lambda m: m.update({"Bass Pluck/a.wav": b"y" * 2000}))))
        self.assertTrue(any("size mismatch" in e for e in rewrite(lambda m: m.update({"Bass Pluck/a.wav": b"y"}))))
        self.assertTrue(any("not listed" in e for e in rewrite(lambda m: m.update({"extra.bin": b"1"}))))
        self.assertTrue(any("unsafe member" in e for e in rewrite(lambda m: m.update({"../evil": b"1"}))))
        self.assertTrue(any("unsafe member" in e for e in rewrite(lambda m: m.update({"/abs": b"1"}))))
        self.assertTrue(any("missing file" in e for e in rewrite(lambda m: m.pop("Bass Pluck/a.wav"))))
        self.assertTrue(any("no contentpack.json" in e for e in rewrite(lambda m: m.pop("contentpack.json"))))
        (self.tmp / "bad.zip").write_bytes(b"nope")
        self.assertTrue(any("not a readable zip" in e for e in cp.verify_zip(self.tmp / "bad.zip")))

    def test_unsafe_paths_in_the_manifest_are_refused(self):
        self.make_instruments()
        manifest = json.loads((self.pack / "contentpack.json").read_text())
        for bad in ("../outside.wav", "/abs.wav", "a/../b.wav", "a\\b.wav", "C:/x.wav", "contentpack.json"):
            manifest["files"][0]["path"] = bad
            (self.pack / "contentpack.json").write_text(json.dumps(manifest))
            self.assertTrue(any("unsafe" in e or "itself" in e for e in cp.verify_directory(self.pack)), bad)

    @unittest.skipIf(os.name == "nt", "creating symbolic links needs a privilege on Windows")
    def test_symlinks_are_not_allowed_in_a_pack(self):
        self.make_instruments()
        os.symlink("a.wav", self.pack / "Bass Pluck" / "link.wav")
        code, _, err = run_quiet(["manifest", "--dir", str(self.pack)])
        self.assertEqual(code, 1)
        self.assertIn("not a regular file", err)

    def test_ds_store_is_ignored(self):
        self.make_instruments()
        (self.pack / ".DS_Store").write_bytes(b"x")
        self.assertEqual(cp.verify_directory(self.pack), [])

    def test_the_factory_preset_pack_verifies_builds_and_is_small(self):
        factory = ROOT / "content" / "presets"
        self.assertEqual(cp.verify_directory(factory), [])
        summary = cp.build_pack(factory, self.tmp / "factory")
        self.assertEqual((summary["id"], summary["files"]), ("com.safian.synthyboy.factory-presets", 112))
        self.assertLess(summary["size"], cp.DEFAULT_MAX_BYTES)
        self.assertEqual(cp.verify_zip(self.tmp / "factory" / summary["file"], summary["id"], summary["version"]), [])

    def test_build_all_builds_the_packs_of_plugin_json_and_checks_identity(self):
        out = self.tmp / "all"
        code, text, _ = run_quiet(["build-all", "--manifest", str(ROOT / "plugin.json"), "--out", str(out)])
        self.assertEqual(code, 0, text)
        self.assertEqual(sorted(p.name for p in out.glob("*.zip")), ["com.safian.synthyboy.factory-presets-1.2.0.zip"])
        # a plugin.json that names another version than the pack holds is refused
        plugin = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
        plugin["contentPacks"][0]["version"] = "9.9.9"
        (self.tmp / "plugin.json").write_text(json.dumps(plugin), encoding="utf-8")
        (self.tmp / "content").symlink_to(ROOT / "content")
        code, _, err = run_quiet(["build-all", "--manifest", str(self.tmp / "plugin.json"), "--out", str(self.tmp / "bad")])
        self.assertEqual(code, 1)
        self.assertIn("9.9.9", err)


if __name__ == "__main__":
    unittest.main()
