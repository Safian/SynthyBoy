#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Static checks of the build files after the Vital engine removal (docs/plans/vital-removal.md): no cmake run, the files are read as text."""

import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(*p):
    with open(os.path.join(ROOT, *p), encoding="utf-8") as f:
        return f.read()


class DefaultEngine(unittest.TestCase):
    """R1: a configure without flags builds the own engine only."""

    def test_with_vital_defaults_off(self):
        m = re.search(r"^option\(SYNTHYBOY_WITH_VITAL\b[^\n]*?\b(ON|OFF)\)", read("CMakeLists.txt"), re.M)
        self.assertIsNotNone(m, "option(SYNTHYBOY_WITH_VITAL ...) not found")
        self.assertEqual(m.group(1), "OFF")

    def test_the_shell_engine_is_not_selectable_and_never_vital(self):
        text = read("CMakeLists.txt")
        self.assertNotRegex(text, r'set\(SYNTHYBOY_SHELL_ENGINE_DEFAULT "vital"\)')
        self.assertNotIn("SYNTHYBOY_SHELL_ENGINE ", text.replace("SYNTHYBOY_SHELL_ENGINE_VITAL", ""))


SHELL_FILES = [
    "shell-juce/PluginProcessor.cpp", "shell-clap/synthyboy_plugin.cpp", "shell-clap/synthyboy_clap.cpp", "shell-clap/bitmap/NativeUiHost.cpp",
    "shell-mpc/synthyboy_mpc.cpp", "shell-mpc/CMakeLists.txt", "shell-mpc/accept.sh", "shell-mpc/tests/pad_pressure_host.cpp",
]


class ShellsOwnEngineOnly(unittest.TestCase):
    """R2: the JUCE, CLAP and MPC shells create the own engine only."""

    def test_no_engine_switch_in_the_shells(self):
        pat = re.compile(r"SYNTHYBOY_SHELL_ENGINE_VITAL|SYNTHYBOY_MPC_ENGINE_VITAL|SYNTHYBOY_HAS_VITAL|EngineKind::vital|vital DSP")
        for f in SHELL_FILES:
            hits = [n + 1 for n, line in enumerate(read(f).splitlines()) if pat.search(line)]
            self.assertEqual(hits, [], f"{f}: Vital engine switch at lines {hits}")

    def test_about_text_names_the_own_engine_only(self):
        for f in ("shell-juce/PluginProcessor.cpp", "shell-clap/bitmap/NativeUiHost.cpp"):
            self.assertNotIn("kCapVoiceControl", read(f).split("aboutText")[1].split("}")[0], f)


class TestDefinitions(unittest.TestCase):
    """R4: no test is compiled for a Vital shell engine; golden-own is the only set of bitmap golden pictures (V5)."""

    def test_no_shell_engine_definition(self):
        for f in ("CMakeLists.txt", "tests/bitmap-gui/CMakeLists.txt"):
            text = read(f)
            self.assertNotIn("SYNTHYBOY_SHELL_ENGINE_VITAL", text, f)
            self.assertNotIn("SYNTHYBOY_HARNESS_OWN_ENGINE", text, f)
            self.assertNotRegex(text, r"if\(SYNTHYBOY_SHELL_ENGINE STREQUAL", f)

    def test_golden_own_is_the_only_golden_set(self):
        self.assertFalse(os.path.isdir(os.path.join(ROOT, "tests", "bitmap-gui", "golden")))
        self.assertIn("golden-own", read("tests/bitmap-gui/CMakeLists.txt"))
        self.assertNotRegex(read("tests/bitmap-gui/CMakeLists.txt"), r'/golden"')


class ReferenceToolsGone(unittest.TestCase):
    """R5 (V3): the tools that measure against the Vital engine, their scripts and the Vital engine round trip are gone; no file still points at them."""

    GONE = [
        "tools/param-audit", "tools/ref-compare", "scripts/run-param-audit.sh", "scripts/param_audit_report.py", "scripts/run-ref-compare.sh",
        "tools/vital-import/vital_roundtrip.cpp", "tools/own-oracle/CMakeLists.txt", "tools/own-oracle/own_oracle.cpp",
        "tools/own-oracle/fx_oracle.cpp", "tools/own-oracle/osc_oracle.cpp",
    ]

    def test_paths_are_gone(self):
        for f in self.GONE:
            self.assertFalse(os.path.exists(os.path.join(ROOT, f)), f)

    def test_the_helper_header_of_the_own_engine_tests_stays(self):
        # core/tests/test_own_*.cpp include own_oracle.h from tools/own-oracle (core/tests/CMakeLists.txt adds the include directory)
        self.assertTrue(os.path.isfile(os.path.join(ROOT, "tools", "own-oracle", "own_oracle.h")))

    def test_no_build_file_names_the_removed_tools(self):
        text = read("CMakeLists.txt")
        for name in ("tools/param-audit", "tools/ref-compare", "tools/own-oracle"):
            self.assertNotIn(name, text)
        self.assertNotIn("synthyboy_vital_roundtrip", read("tools/vital-import/CMakeLists.txt"))

    def test_bench_has_one_engine(self):
        text = read("bench/bench_engine.cpp")
        self.assertNotIn("EngineKind::vital", text)
        self.assertNotRegex(text, r"own\|vital")

    def test_audit_script_has_no_engine_round_trip(self):
        self.assertNotIn("roundtrip", read("scripts/audit-vital-import.py"))


class Workflow(unittest.TestCase):
    """R6: CI builds the own engine; the Vital-free rule is the general rule (check-no-vital.sh on the core library of every Unix build)."""

    def test_no_vital_engine_in_the_jobs(self):
        text = read(".github/workflows/build.yml")
        for needle in ("SYNTHYBOY_SHELL_ENGINE", "--engine vital", "run-param-audit", "ios-vital-free", "-DSYNTHYBOY_WITH_VITAL"):
            self.assertNotIn(needle, text, needle)

    def test_the_general_rule_runs_in_the_build_job(self):
        text = read(".github/workflows/build.yml")
        build_job = text.split("\n  build:\n")[1].split("\n  core-sanitizers:")[0] if "\n  build:\n" in text else ""
        self.assertIn("scripts/check-no-vital.sh build/core/libsynthyboy_core.a", build_job)

    def test_the_file_is_yaml_and_every_job_has_steps(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("no PyYAML")
        doc = yaml.safe_load(read(".github/workflows/build.yml"))
        for name, job in doc["jobs"].items():
            self.assertIn("steps", job, name)


if __name__ == "__main__":
    unittest.main()
