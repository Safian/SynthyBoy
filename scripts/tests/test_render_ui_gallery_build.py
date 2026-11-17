#!/usr/bin/env python3
"""render-ui-gallery.py always builds the ui_gallery_host target when a build directory with a CMake cache is known (a fake build step stands in for cmake)."""
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "render-ui-gallery.py"
spec = importlib.util.spec_from_file_location("render_ui_gallery", SCRIPT)
gal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gal)


def make_binary(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/bin/sh\n")
    path.chmod(0o755)


class FindOrBuildHost(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "repo"
        self.root.mkdir()
        self.bdir = Path(self.tmp.name) / "mybuild"
        self.bdir.mkdir()
        self.calls = []

    def tearDown(self):
        self.tmp.cleanup()

    def fake_build(self, make=True, rc=0):
        def build(bdir):
            self.calls.append(Path(bdir))
            if make and rc == 0:
                make_binary(Path(bdir) / "shell-mpc" / "ui_gallery_host")
            return rc
        return build

    def test_existing_binary_is_rebuilt_first(self):
        (self.bdir / "CMakeCache.txt").write_text("")
        make_binary(self.bdir / "shell-mpc" / "ui_gallery_host")   # an old binary is there
        got = gal.find_or_build_host(self.root, build_dir=self.bdir, build=self.fake_build())
        self.assertEqual(self.calls, [self.bdir])
        self.assertEqual(got, self.bdir / "shell-mpc" / "ui_gallery_host")

    def test_no_binary_yet_is_built(self):
        (self.bdir / "CMakeCache.txt").write_text("")
        got = gal.find_or_build_host(self.root, build_dir=self.bdir, build=self.fake_build())
        self.assertEqual(self.calls, [self.bdir])
        self.assertTrue(got.is_file())

    def test_failed_build_is_an_error_not_a_stale_binary(self):
        (self.bdir / "CMakeCache.txt").write_text("")
        make_binary(self.bdir / "shell-mpc" / "ui_gallery_host")
        with self.assertRaises(RuntimeError):
            gal.find_or_build_host(self.root, build_dir=self.bdir, build=self.fake_build(rc=2))
        self.assertEqual(self.calls, [self.bdir])

    def test_usual_directory_under_the_repository_is_built_too(self):
        d = self.root / "build-mpc"
        d.mkdir()
        (d / "CMakeCache.txt").write_text("")
        make_binary(d / "shell-mpc" / "ui_gallery_host")
        gal.find_or_build_host(self.root, build=self.fake_build())
        self.assertEqual(self.calls, [d])

    def test_named_directory_only(self):
        (self.bdir / "CMakeCache.txt").write_text("")
        other = self.root / "build-mpc"
        other.mkdir()
        (other / "CMakeCache.txt").write_text("")
        gal.find_or_build_host(self.root, build_dir=self.bdir, build=self.fake_build())
        self.assertEqual(self.calls, [self.bdir])

    def test_host_bin_is_used_as_given(self):
        b = Path(self.tmp.name) / "mine"
        make_binary(b)
        (self.bdir / "CMakeCache.txt").write_text("")
        got = gal.find_or_build_host(self.root, custom_bin=str(b), build_dir=self.bdir, build=self.fake_build())
        self.assertEqual(got, b)
        self.assertEqual(self.calls, [])

    def test_directory_without_cache_uses_existing_binary_without_build(self):
        make_binary(self.bdir / "ui_gallery_host")
        got = gal.find_or_build_host(self.root, build_dir=self.bdir, build=self.fake_build())
        self.assertEqual(got, self.bdir / "ui_gallery_host")
        self.assertEqual(self.calls, [])


if __name__ == "__main__":
    unittest.main()
