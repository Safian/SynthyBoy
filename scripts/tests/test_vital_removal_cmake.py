#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Static checks of the build files after the Vital engine removal (docs/plans/vital-removal.md): no cmake run, the files are read as text."""

import json
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


class ReferenceToolsOwnEngine(unittest.TestCase):
    """R5 (V3, corrected): the tools that measure AGAINST Vital are gone (own-oracle, the Vital engine round trip, bench --engine vital). param-audit and ref-compare
    also serve Serum 2 and the own engine: they stay, with the own engine as the only engine (no --engine option) and no WITH_VITAL gate."""

    GONE = [
        "tools/vital-import/vital_roundtrip.cpp", "tools/own-oracle/CMakeLists.txt", "tools/own-oracle/own_oracle.cpp",
        "tools/own-oracle/fx_oracle.cpp", "tools/own-oracle/osc_oracle.cpp",
    ]
    KEPT = ["tools/param-audit/exemptions.tsv", "tools/param-audit/param_audit.cpp", "tools/ref-compare/ref_render.cpp", "tools/ref-compare/test_metrics.py",
            "scripts/run-param-audit.sh", "scripts/param_audit_report.py", "scripts/run-ref-compare.sh"]

    def test_paths_are_gone(self):
        for f in self.GONE:
            self.assertFalse(os.path.exists(os.path.join(ROOT, f)), f)

    def test_audit_and_ref_compare_are_kept(self):
        for f in self.KEPT:
            self.assertTrue(os.path.isfile(os.path.join(ROOT, f)), f)

    def test_the_helper_header_of_the_own_engine_tests_stays(self):
        # core/tests/test_own_*.cpp include own_oracle.h from tools/own-oracle (core/tests/CMakeLists.txt adds the include directory)
        self.assertTrue(os.path.isfile(os.path.join(ROOT, "tools", "own-oracle", "own_oracle.h")))

    def test_cmake_lines(self):
        text = read("CMakeLists.txt")
        self.assertNotIn("tools/own-oracle", text)
        self.assertRegex(text, r"\n    add_subdirectory\(tools/param-audit\)\n    add_subdirectory\(tools/ref-compare\)\n")
        self.assertNotIn("synthyboy_vital_roundtrip", read("tools/vital-import/CMakeLists.txt"))

    def test_no_vital_engine_option_or_path(self):
        pat = re.compile(r"EngineKind::vital|WITH_VITAL|--engine vital|vital\|own|own\|vital|--engine\b")
        for f in ("tools/param-audit/param_audit.cpp", "tools/param-audit/audit.h", "tools/param-audit/test_param_semantics.cpp", "tools/param-audit/CMakeLists.txt",
                  "tools/ref-compare/ref_render.cpp", "tools/ref-compare/CMakeLists.txt", "scripts/run-param-audit.sh"):
            hits = [n + 1 for n, line in enumerate(read(f).splitlines()) if pat.search(line)]
            self.assertEqual(hits, [], f"{f}: lines {hits}")

    def test_audit_baseline_is_the_own_one(self):
        self.assertIn("param-audit-baseline-own.json", read("scripts/run-param-audit.sh"))
        self.assertIn("param-audit-baseline-own.json", read("tools/param-audit/CMakeLists.txt"))

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
        for needle in ("SYNTHYBOY_SHELL_ENGINE", "--engine vital", "ios-vital-free", "-DSYNTHYBOY_WITH_VITAL"):
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


class LayersModel(unittest.TestCase):
    """R8 (V2): the wavetable editor's layer model is wtedit::layers (it was wtedit::vital); the model is ours, the importer stays."""

    FILES = ["core/include/synthyboy/wavetable_edit.h", "core/wavetable-edit/src/layers_model.cpp", "core/wavetable-edit/CMakeLists.txt",
             "core/wavetable-edit/tests/test_wtedit.cpp", "shell-juce/ui/panels/WavetableEditorSheet.cpp",
             "shell-juce/ui/panels/WavetableEditorSheet.h", "shell-juce/ui/tests/test_wavetable.cpp"]

    def test_namespace_is_layers(self):
        self.assertIn("namespace layers", read("core/include/synthyboy/wavetable_edit.h"))
        self.assertIn("namespace synthyboy::wtedit::layers", read("core/wavetable-edit/src/layers_model.cpp"))

    def test_no_vital_name_in_the_model_and_the_editor(self):
        for f in self.FILES:
            text = read(f)
            for needle in ("wtedit::vital", "namespace vital", "using namespace vital", "vitalGroup", "Vital Group", "vital_model"):
                self.assertNotIn(needle, text, f"{f}: {needle}")


class DocsAndLegal(unittest.TestCase):
    """R9 (V4): licence stays AGPL-3.0-or-later, the Vital engine leaves NOTICE, THIRD_PARTY.md, plugin.json, README and the generators; old records say they are history."""

    def test_notice_and_third_party(self):
        notice = read("NOTICE")
        self.assertIn("AGPL-3.0-or-later", notice)
        for needle in ("Vital (https://github.com/mtytel/vital)", "core/engine-vital", "KISS FFT", "JSON for Modern C++"):
            self.assertNotIn(needle, notice, needle)
        third = read("THIRD_PARTY.md")
        self.assertNotIn("core/engine-vital/upstream", third)
        self.assertNotIn("Source included in this repository", third)

    def test_plugin_json_description(self):
        with open(os.path.join(ROOT, "plugin.json"), encoding="utf-8") as f:
            self.assertNotIn("Vital", json.load(f)["description"])

    def test_readme_has_one_engine(self):
        text = read("README.md")
        for needle in ("SYNTHYBOY_SHELL_ENGINE", "SYNTHYBOY_WITH_VITAL=ON (default)", "ios-vital-free", "EngineKind::own | EngineKind::vital", "Two engines"):
            self.assertNotIn(needle, text, needle)

    def test_generators_do_not_name_the_vital_engine(self):
        self.assertNotIn("core/engine-vital", read("scripts/gen-params-doc.py"))
        self.assertNotIn("core/engine-vital", read("docs/PARAMETERS.md"))
        self.assertNotIn("Vital engine", read("scripts/gen-sound-set-batch2-doc.py"))
        self.assertNotIn("engine-vital", read("scripts/gen-factory-presets.py"))

    def test_history_records_say_so(self):
        self.assertRegex(read("docs/UPSTREAM.md")[:600], r"(?i)history")
        self.assertRegex(read("docs/ARCHITECTURE.md")[:900], r"(?i)vital-derived engine (has been )?removed")


class EngineDirectoryGone(unittest.TestCase):
    """R7: core/engine-vital is deleted, core no longer links it, the SYNTHYBOY_WITH_VITAL option and SYNTHYBOY_HAS_VITAL are gone."""

    # text of the build files, scripts, workflows, shells and the living docs (history records and reports keep their old wording)
    LIVING = ["CMakeLists.txt", "core/CMakeLists.txt", "core/src/engine_factory.cpp", "core/include/synthyboy/engine.h", "README.md", "docs/RELEASE.md",
              "docs/MPC.md", "docs/BITMAP-GUI-EXTENSION.md", "shell-juce/ui/README.md", "tests/bitmap-gui/CMakeLists.txt", "tools/vital-import/CMakeLists.txt",
              "tools/param-audit/CMakeLists.txt", ".github/workflows/build.yml", "scripts/build-mpc.sh", "scripts/mpc-container-build.sh", "scripts/build-arm32-test.sh",
              "scripts/build-tier-bench.sh", "scripts/test-ios-static-ui.sh", "scripts/run-param-audit.sh", "docs/release/VOICE-COUNT-GAIN-SPEC.md"]

    def test_directory_is_gone(self):
        self.assertFalse(os.path.exists(os.path.join(ROOT, "core", "engine-vital")))

    def test_core_cmake_has_no_engine_vital(self):
        self.assertNotIn("engine-vital", read("core/CMakeLists.txt"))

    def test_no_option_and_no_define(self):
        for f in self.LIVING:
            text = read(f)
            for needle in ("SYNTHYBOY_WITH_VITAL", "SYNTHYBOY_HAS_VITAL"):
                self.assertNotIn(needle, text, f"{f}: {needle}")


if __name__ == "__main__":
    unittest.main()
