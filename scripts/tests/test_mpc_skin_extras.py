#!/usr/bin/env python3
# SynthyBoy - tests of scripts/apply-mpc-skin-extras.py (the seam for graphical MPC skin extras).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import contextlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "mpc-ui-pages.json"
REAL_EXTRAS = ROOT / "scripts" / "mpc-skin-extras"
APPLY = ROOT / "scripts" / "apply-mpc-skin-extras.py"
GEN = ROOT / "scripts" / "gen-mpc-ui.py"
SDK = Path(os.environ.get("MPC_SDK") or Path.home() / ".local/share/local-worker/ext/mpc-sdk")

# 1x1 transparent PNG
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360000002000001e221bc330000000049454e44ae426082")

MODULE_OK = textwrap.dedent('''
    def apply(tui, qlinks, ctx):
        import shutil
        shutil.copy(ctx.skins_dir.parent / "sample.png", ctx.skins_dir / "sample.png")
        tab = ctx.tab("Osc 1")
        tab["componentDefinition"]["componentsData"].append({
            "version": 2,
            "componentData": {"version": 1, "name": "Sample Image", "type": "Image", "data": {
                "version": 2, "imageType": "Regular", "colour": "0", "image": "sample.png"}},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide", "whenVisible": "Always",
                       "boundsType": "Absolute", "bounds": "10 10 1 1"},
        })
''')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


apply_mod = load("t_apply_mpc_skin_extras", APPLY)


def generate_skins(tmp):
    """Plugin Skins folder as the build makes it (gen-mpc-ui.py + SDK gen_ui.py). Needs the SDK."""
    ui = tmp / "ui"
    subprocess.run([sys.executable, str(GEN), "--params", str(PARAMS_JSON), "--pages", str(PAGES_JSON), "--layout", str(LAYOUT_JSON),
                    "--out", str(ui)], check=True, capture_output=True)
    skins = tmp / "pf" / "Plugin Skins"
    subprocess.run([sys.executable, str(SDK / "tools" / "gen_ui.py"), "--params", str(ui / "params-hostsim.json"),
                    "--ui", str(ui / "ui.json"), "--out", str(skins), "--quiet"], check=True, capture_output=True)
    (tmp / "pf" / "sample.png").write_bytes(PNG)
    (tmp / "pf" / "skin-meta.json").write_bytes((ui / "skin-meta.json").read_bytes())   # next to the skins: run_apply passes it as --meta
    return skins


def run_apply(skins, extras):
    out, err = io.StringIO(), io.StringIO()
    meta = skins.parent / "skin-meta.json"
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = apply_mod.main(["--skins", str(skins), "--params", str(PARAMS_JSON), "--extras-dir", str(extras)]
                              + (["--meta", str(meta)] if meta.is_file() else []))
    return code, err.getvalue()


def snapshot(skins):
    return {p.name: p.read_bytes() for p in sorted(skins.iterdir())}


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class SkinExtrasTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls._tmp.name)
        cls.skins_master = generate_skins(cls.base / "master")

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def fresh(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        pf = root / "pf"
        pf.mkdir()
        (pf / "sample.png").write_bytes(PNG)
        (pf / "skin-meta.json").write_bytes((self.skins_master.parent / "skin-meta.json").read_bytes())
        skins = pf / "Plugin Skins"
        skins.mkdir()
        for p in self.skins_master.iterdir():
            (skins / p.name).write_bytes(p.read_bytes())
        extras = root / "extras"
        extras.mkdir()
        return skins, extras

    def test_empty_extras_dir_keeps_bytes(self):
        skins, extras = self.fresh()
        before = snapshot(skins)
        self.assertEqual(run_apply(skins, extras), (0, ""))
        self.assertEqual(snapshot(skins), before)

    def test_real_extras_dir_applies_cleanly(self):
        # the real modules (10_env, 20_osc, ...) change the skin on purpose; their own tests check the content.
        # Here: the run succeeds, both JSON files stay valid and the generation is idempotent from the same start
        skins, _ = self.fresh()
        self.assertEqual(run_apply(skins, REAL_EXTRAS), (0, ""))
        for name in ("TUI.json", "Q-Links.json"):
            json.loads((skins / name).read_text(encoding="utf-8"))
        first = snapshot(skins)
        skins2, _ = self.fresh()
        self.assertEqual(run_apply(skins2, REAL_EXTRAS), (0, ""))
        self.assertEqual(snapshot(skins2), first)

    def test_module_without_change_keeps_bytes(self):
        skins, extras = self.fresh()
        (extras / "10_noop.py").write_text("def apply(tui, qlinks, ctx):\n    ctx.tab('Osc 1'); ctx.param_index('env1.delay')\n")
        before = snapshot(skins)
        self.assertEqual(run_apply(skins, extras), (0, ""))
        self.assertEqual(snapshot(skins), before)

    def test_sample_module_adds_image(self):
        skins, extras = self.fresh()
        before = (skins / "TUI.json").read_text(encoding="utf-8")
        (extras / "10_sample.py").write_text(MODULE_OK)
        self.assertEqual(run_apply(skins, extras), (0, ""))
        tui = json.loads((skins / "TUI.json").read_text(encoding="utf-8"))
        tab = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == "Osc 1")
        last = tab["componentDefinition"]["componentsData"][-1]
        self.assertEqual(last["componentData"]["name"], "Sample Image")
        self.assertEqual(last["componentData"]["data"]["image"], "sample.png")
        self.assertEqual((skins / "sample.png").read_bytes(), PNG)
        self.assertNotEqual((skins / "TUI.json").read_text(encoding="utf-8"), before)
        self.assertTrue((skins / "Q-Links.json").read_bytes().endswith(b"}\n"))
        # the rest of the file is untouched: removing the added entry gives the original JSON
        tab["componentDefinition"]["componentsData"].pop()
        self.assertEqual(tui, json.loads(before))

    def test_validate_py_has_no_error(self):
        skins, extras = self.fresh()
        (extras / "10_sample.py").write_text(MODULE_OK)
        self.assertEqual(run_apply(skins, extras)[0], 0)
        res = subprocess.run([sys.executable, str(SDK / "tools" / "validate.py"), str(skins.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        # the folder holds only the skin files, so plugin.json is expected to be missing; every other finding fails
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)
        self.assertIn("OK     TUI.json: image sample.png present", report)

    def test_add_component_definition(self):
        skins, extras = self.fresh()
        (extras / "10_def.py").write_text(
            "def apply(tui, qlinks, ctx):\n"
            "    ctx.add_component_definition('myDef', {'version': 2, 'actions': [], 'componentsData': []})\n")
        self.assertEqual(run_apply(skins, extras)[0], 0)
        defs = json.loads((skins / "TUI.json").read_text(encoding="utf-8"))["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        self.assertEqual(defs[-1]["key"], "myDef")
        (extras / "10_def.py").unlink()
        (extras / "20_dup.py").write_text("def apply(tui, qlinks, ctx):\n    ctx.add_component_definition('knobUni', {})\n")
        code, err = run_apply(skins, extras)
        self.assertEqual(code, 1)
        self.assertIn("20_dup.py", err)
        self.assertIn("'knobUni' already exists", err)

    def test_param_index_resolves_key(self):
        skins, extras = self.fresh()
        (extras / "10_idx.py").write_text(
            "def apply(tui, qlinks, ctx):\n"
            "    assert ctx.param_index('env1.delay') == 133 and ctx.param_index('osc1.wavetable') == 16\n")
        self.assertEqual(run_apply(skins, extras), (0, ""))

    def test_errors_stop_with_clear_message(self):
        cases = {
            "unknown key": ("def apply(tui, qlinks, ctx):\n    ctx.param_index('nope.nothing')\n", "unknown parameter key 'nope.nothing'"),
            "unknown tab": ("def apply(tui, qlinks, ctx):\n    ctx.tab('No Such Tab')\n", "unknown tab 'No Such Tab'; tabs are: 'Macros'"),
            "raises": ("def apply(tui, qlinks, ctx):\n    raise ValueError('boom')\n", "ValueError: boom"),
            "no apply": ("x = 1\n", "has no apply"),
            "syntax": ("def apply(:\n", "SyntaxError"),
        }
        for name, (src, expect) in cases.items():
            with self.subTest(name):
                skins, extras = self.fresh()
                before = snapshot(skins)
                (extras / "10_bad.py").write_text(src)
                code, err = run_apply(skins, extras)
                self.assertEqual(code, 1)
                self.assertIn("10_bad.py", err)
                self.assertIn(expect, err)
                self.assertEqual(snapshot(skins), before)

    def test_cli_exit_code_nonzero(self):
        skins, extras = self.fresh()
        (extras / "10_bad.py").write_text("def apply(tui, qlinks, ctx):\n    ctx.param_index('nope')\n")
        res = subprocess.run([sys.executable, str(APPLY), "--skins", str(skins), "--params", str(PARAMS_JSON), "--extras-dir", str(extras)],
                             capture_output=True, text=True)
        self.assertEqual(res.returncode, 1)
        self.assertIn("unknown parameter key 'nope'", res.stderr)

    def test_missing_dirs_are_errors(self):
        skins, extras = self.fresh()
        self.assertEqual(run_apply(skins, extras / "absent")[0], 1)
        self.assertEqual(run_apply(skins / "absent", extras)[0], 1)


class RoundtripTest(unittest.TestCase):
    """No SDK needed: the writer reproduces the SDK layout including repeated keys."""

    def test_repeated_keys_and_layout(self):
        text = '{\n    "a": 1,\n    "a": [\n        {\n            "v": 1,\n            "v": 2.5\n        },\n        "xé",\n        true,\n        null\n    ],\n    "e": {},\n    "f": []\n}\n'
        self.assertEqual(apply_mod.dump_json(apply_mod.load_json(text)) + "\n", text)


if __name__ == "__main__":
    unittest.main()
