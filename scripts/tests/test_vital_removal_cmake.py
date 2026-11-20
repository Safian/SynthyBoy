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

    def test_shell_engine_defaults_to_own(self):
        text = read("CMakeLists.txt")
        self.assertNotRegex(text, r'set\(SYNTHYBOY_SHELL_ENGINE_DEFAULT "vital"\)')
        self.assertRegex(text, r'set\(SYNTHYBOY_SHELL_ENGINE "\$\{SYNTHYBOY_SHELL_ENGINE_DEFAULT\}"|set\(SYNTHYBOY_SHELL_ENGINE "own"')


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


if __name__ == "__main__":
    unittest.main()
