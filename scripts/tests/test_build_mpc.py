#!/usr/bin/env python3
# SynthyBoy - tests of scripts/build-mpc.sh (both MPC architectures in one plugin folder), of its thin wrapper scripts/build-armv7-mpc.sh and of the manifest
# with both binaries. No Docker: MPC_DOCKER points at a stub that logs its arguments and writes a fake stripped module where the container would; the SDK
# is a fake directory whose validate.py prints what the test wants (the real validator is covered by the build itself and by test_mpc_skin_extras.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
BUILD_MPC = ROOT / "scripts" / "build-mpc.sh"
BUILD_ARMV7 = ROOT / "scripts" / "build-armv7-mpc.sh"
CONTAINER = ROOT / "scripts" / "mpc-container-build.sh"
MANIFEST = ROOT / "scripts" / "gen-mpc-manifest.py"
PLUGIN = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
FOLDER = "Safian - VST - SynthyBoy"

STUB_DOCKER = textwrap.dedent('''\
    #!/usr/bin/env bash
    # args of every "run" go to $STUB_LOG, one line each; the fake module is written into the mount of /build
    [ "$1" = info ] && exit 0
    [ "$1" = run ] || exit 64
    shift
    echo "$*" >> "$STUB_LOG"
    build=""; arch=""
    while [ $# -gt 0 ]; do
        case "$1" in
            -v) case "$2" in *:/build) build="${2%:/build}" ;; esac; shift 2 ;;
            -e) case "$2" in ARCH=*) arch="${2#ARCH=}" ;; esac; shift 2 ;;
            *) shift ;;
        esac
    done
    [ -n "${STUB_FAIL_ARCH:-}" ] && [ "$STUB_FAIL_ARCH" = "$arch" ] && exit 1
    mkdir -p "$build/shell-mpc"
    printf 'fake %s module' "$arch" > "$build/shell-mpc/synthyboy_mpc.stripped.so"
''')

VALIDATOR = textwrap.dedent('''\
    import os, sys
    print(os.environ.get("FAKE_VALIDATE", "OK     plugin.json"))
    sys.exit(0)
''')


class BuildMpcTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="sb-build-mpc-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.docker = self.tmp / "docker"
        self.docker.write_text(STUB_DOCKER)
        self.docker.chmod(self.docker.stat().st_mode | stat.S_IXUSR)
        self.sdk = self.tmp / "sdk"
        (self.sdk / "tools").mkdir(parents=True)
        (self.sdk / "tools" / "validate.py").write_text(VALIDATOR)
        self.skins = self.tmp / "skins"
        self.skins.mkdir()
        (self.skins / "TUI.json").write_text("{}")
        self.log = self.tmp / "docker.log"
        self.out = self.tmp / "out"
        self.tree = self.tmp / "tree"

    def env(self, **extra):
        env = dict(os.environ, MPC_DOCKER=str(self.docker), STUB_LOG=str(self.log), MPC_SDK=str(self.sdk))
        env.pop("ARMV7_IMAGE", None)
        env.pop("MPC_BUILD_IMAGE", None)
        env.update(extra)
        return env

    def run_script(self, script, *args, **env):
        return subprocess.run(["bash", str(script), *args], capture_output=True, text=True, env=self.env(**env))

    def build(self, *args, **env):
        common = ["--out", str(self.out), "--build-dir", str(self.tree), "--skins", str(self.skins)]
        return self.run_script(BUILD_MPC, *common, *args, **env)

    def runs(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def folder(self):
        return self.out / FOLDER

    def manifest(self):
        return json.loads((self.folder() / "plugin.json").read_text(encoding="utf-8"))

    # ---- argument handling -------------------------------------------------------------------------------------------------------------------

    def test_usage_errors(self):
        for args in (["--arch", "x86"], ["--arch"], ["--jobs", "0"], ["--jobs", "two"], ["--bogus"], ["--skins", str(self.tmp / "nope")]):
            r = self.run_script(BUILD_MPC, *args)
            self.assertEqual(r.returncode, 2, (args, r.stderr))
        self.assertEqual(self.runs(), [])

    def test_sdk_is_required(self):
        env = self.env()
        del env["MPC_SDK"]
        r = subprocess.run(["bash", str(BUILD_MPC)], capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 2)
        self.assertIn("MPC_SDK", r.stderr)

    def test_help_lists_the_options(self):
        for script in (BUILD_MPC, BUILD_ARMV7):
            r = self.run_script(script, "--help")
            self.assertEqual(r.returncode, 0)
            self.assertIn("Usage: scripts/", r.stdout)
        self.assertIn("--arch arm32|arm64|all", self.run_script(BUILD_MPC, "--help").stdout)

    def test_no_docker_prints_manual_commands_for_both_architectures(self):
        r = self.run_script(BUILD_MPC, "--out", str(self.out), MPC_DOCKER=str(self.tmp / "no-such-docker"))
        self.assertEqual(r.returncode, 3)
        self.assertIn("aarch64-linux-gnu.cmake", r.stderr)
        self.assertIn("armv7-linux-gnueabihf.cmake", r.stderr)

    def test_scripts_are_valid_bash(self):
        for script in (BUILD_MPC, BUILD_ARMV7, CONTAINER):
            self.assertEqual(subprocess.run(["bash", "-n", str(script)]).returncode, 0, script)

    def test_container_script_rejects_unknown_arch(self):
        r = subprocess.run(["bash", str(CONTAINER)], capture_output=True, text=True, env=dict(os.environ, ARCH="mips"))
        self.assertEqual(r.returncode, 2)
        self.assertIn("arm32 or arm64", r.stderr)

    # ---- the assembled folder ----------------------------------------------------------------------------------------------------------------

    def test_default_arch_is_all(self):
        r = self.build("--no-validate")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        runs = self.runs()
        self.assertEqual(len(runs), 2)
        self.assertIn("ARCH=arm32", runs[0])
        self.assertIn("ARCH=arm64", runs[1])
        vst = sorted(p.name for p in (self.folder() / "VST").iterdir())
        self.assertEqual(vst, ["synthyboy-arm32.so", "synthyboy-arm64.so"])
        m = self.manifest()
        self.assertEqual(m["file32"], "VST/synthyboy-arm32.so")
        self.assertEqual(m["file64"], "VST/synthyboy-arm64.so")
        self.assertEqual(m["version"], PLUGIN["version"])
        self.assertTrue((self.folder() / "version.xml").is_file())
        self.assertTrue((self.folder() / "Content" / "presets").is_dir())
        self.assertTrue((self.folder() / "Plugin Skins" / "TUI.json").is_file())
        # each architecture has a tree of its own; the sources are read-only in the container
        self.assertIn(str(self.tree / "arm32") + ":/build", runs[0])
        self.assertIn(str(self.tree / "arm64") + ":/build", runs[1])
        self.assertIn(":/src:ro", runs[0])
        self.assertIn("WITH_BENCH=0", runs[0])

    def test_single_architectures(self):
        for arch, key, absent, name in (("arm32", "file32", "file64", "synthyboy-arm32.so"), ("arm64", "file64", "file32", "synthyboy-arm64.so")):
            shutil.rmtree(self.out, ignore_errors=True)
            self.log.unlink(missing_ok=True)
            r = self.build("--arch", arch, "--no-validate")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(len(self.runs()), 1)
            self.assertEqual([p.name for p in (self.folder() / "VST").iterdir()], [name])
            m = self.manifest()
            self.assertIn(key, m)
            self.assertNotIn(absent, m)

    def test_binaries_of_an_older_run_do_not_leak_into_the_manifest(self):
        self.assertEqual(self.build("--no-validate").returncode, 0)
        self.assertEqual(self.build("--arch", "arm32", "--no-validate").returncode, 0)
        self.assertFalse((self.folder() / "VST" / "synthyboy-arm64.so").exists())
        self.assertNotIn("file64", self.manifest())

    def test_bench_flag_reaches_the_container(self):
        self.assertEqual(self.build("--arch", "arm64", "--bench", "--no-validate").returncode, 0)
        self.assertIn("WITH_BENCH=1", self.runs()[0])
        self.assertIn(str(self.tree / "arm64-bench") + ":/build-bench", self.runs()[0])

    def test_failed_container_fails_the_build_and_leaves_no_folder(self):
        r = self.build("--no-validate", STUB_FAIL_ARCH="arm64")
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(self.folder().exists())

    def test_image_selection(self):
        self.build("--arch", "arm32", "--no-validate", ARMV7_IMAGE="img-old")
        self.assertIn("img-old bash /src/scripts/mpc-container-build.sh", self.runs()[0])
        self.log.unlink()
        self.build("--arch", "arm32", "--no-validate", ARMV7_IMAGE="img-old", MPC_BUILD_IMAGE="img-new")
        self.assertIn("img-new bash /src/scripts/mpc-container-build.sh", self.runs()[0])

    # ---- the validator step ------------------------------------------------------------------------------------------------------------------

    def test_validator_all_needs_no_exception(self):
        self.assertEqual(self.build("--arch", "all", FAKE_VALIDATE="OK     x\n0 errors, 0 warnings").returncode, 0)
        r = self.build("--arch", "all", FAKE_VALIDATE="ERROR  plugin.json is missing required keys: file64\nOK     x")
        self.assertEqual(r.returncode, 1)
        self.assertIn("SDK validator failed", r.stderr)

    def test_validator_single_architecture_accepts_only_the_absent_key(self):
        accepted = (("arm32", "file64"), ("arm64", "file32"))
        for arch, absent in accepted:
            r = self.build("--arch", arch, FAKE_VALIDATE="ERROR  plugin.json is missing required keys: %s\nOK     x" % absent)
            self.assertEqual(r.returncode, 0, (arch, r.stderr))
            self.assertIn("expected", r.stdout)
        for arch, present in (("arm32", "file32"), ("arm64", "file64")):
            r = self.build("--arch", arch, FAKE_VALIDATE="ERROR  plugin.json is missing required keys: %s\nOK     x" % present)
            self.assertEqual(r.returncode, 1, arch)
        r = self.build("--arch", "arm32", FAKE_VALIDATE="ERROR  plugin.json is missing required keys: file64\nERROR  something else\nOK     x")
        self.assertEqual(r.returncode, 1)

    def test_validator_without_ok_line_fails(self):
        self.assertEqual(self.build("--arch", "all", FAKE_VALIDATE="nothing useful").returncode, 1)

    # ---- the wrapper -------------------------------------------------------------------------------------------------------------------------

    def test_wrapper_builds_arm32_only(self):
        r = self.run_script(BUILD_ARMV7, "--out", str(self.out), "--build-dir", str(self.tree), "--skins", str(self.skins), "--jobs", "3",
                            FAKE_VALIDATE="ERROR  plugin.json is missing required keys: file64\nOK     x")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        runs = self.runs()
        self.assertEqual(len(runs), 1)
        self.assertIn("ARCH=arm32", runs[0])
        self.assertIn("JOBS=3", runs[0])
        self.assertEqual([p.name for p in (self.folder() / "VST").iterdir()], ["synthyboy-arm32.so"])
        self.assertNotIn("file64", self.manifest())

    def test_wrapper_keeps_its_old_defaults(self):
        text = BUILD_ARMV7.read_text()
        self.assertIn('--arch arm32 --out "$repo_root/build-armv7-mpc" --build-dir "$repo_root/build-armv7-mpc/tree" "$@"', text)

    def test_wrapper_passes_usage_errors_through(self):
        self.assertEqual(self.run_script(BUILD_ARMV7, "--jobs", "0").returncode, 2)
        self.assertEqual(self.run_script(BUILD_ARMV7, "--bogus").returncode, 2)


class ManifestWithBothBinariesTest(unittest.TestCase):
    def run_manifest(self, names):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            (folder / "VST").mkdir()
            for n in names:
                (folder / "VST" / n).write_bytes(b"x")
            r = subprocess.run(["python3", "-I", str(MANIFEST), "--plugin-dir", str(folder)], capture_output=True, text=True)
            return r, (json.loads((folder / "plugin.json").read_text()) if r.returncode == 0 else None)

    def test_both_keys(self):
        r, m = self.run_manifest(["synthyboy-arm32.so", "synthyboy-arm64.so"])
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual((m["file32"], m["file64"]), ("VST/synthyboy-arm32.so", "VST/synthyboy-arm64.so"))
        self.assertNotIn("file", m)

    def test_only_arm64(self):
        r, m = self.run_manifest(["synthyboy-arm64.so"])
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("file32", m)
        self.assertEqual(m["file64"], "VST/synthyboy-arm64.so")

    def test_no_binary_is_an_error(self):
        r, _ = self.run_manifest([])
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
