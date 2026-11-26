#!/usr/bin/env python3
# SynthyBoy - tests of scripts/apply-mpc-skin-extras.py (the seam for graphical MPC skin extras).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import contextlib
import copy
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
from types import SimpleNamespace

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
REAL_EXTRAS = ROOT / "scripts" / "mpc-skin-extras"
APPLY = ROOT / "scripts" / "apply-mpc-skin-extras.py"
GEN = ROOT / "scripts" / "gen-mpc-ui.py"
SDK = Path(os.environ.get("MPC_SDK") or Path.home() / ".local/share/local-worker/ext/mpc-sdk")

# The skin with directly bound FX scalar cells and 90 px Play Sub dials generates 745,687 bytes.
TUI_JSON_BYTES_MAX = 750000

def standalone_editor_entries(tab, definitions):
    """Read the standalone ENV/LFO leaves in their translated tab coordinates."""
    shared = {"envNativeSelectorStrip", "envNativeFullFields", "lfoNativeFullEditor"}
    for entry in tab["componentDefinition"]["componentsData"]:
        kind = entry["componentData"]["type"]
        if kind not in shared:
            yield entry
            continue
        dx, dy, _, _ = map(int, entry["bounds"]["bounds"].split())
        definition = definitions[kind]
        assert definition["ignoreMousePresses"] and not definition["actions"]
        for child in definition["componentsData"]:
            translated = copy.deepcopy(child)
            x, y, w, h = map(int, child["bounds"]["bounds"].split())
            translated["bounds"]["bounds"] = f"{x + dx} {y + dy} {w} {h}"
            yield translated


class UnusedChoiceTablesTest(unittest.TestCase):
    def test_only_unbound_overlay_choice_tables_are_removed(self):
        mod = load("unused_choices", REAL_EXTRAS / "99_unused_choices.py")
        ctx = SimpleNamespace(meta={"variant": "overlay"}, param_count=10)
        original = {
            "replacementStrings": [{"key": f"Parameter {i}", "value": [f"Choice {i}"]} for i in range(11)]
                                  + [{"key": "Named choices", "value": ["Named"]}],
            "componentDefinitions": {"localComponentDefinitions": [{"key": "strip", "value": {"repeats": 3}}]},
            "tabs": [{"handles": ["NonZero/Parameter 0", "Parameter {Increment:Base=3,Step=2,Offset=1}"]}],
        }
        qlinks = {"Screen Mode Q-Links": {"map": [{"Q-Links": {"Q-Link 1": 2}}]}}
        tui = {"pageData": copy.deepcopy(original)}
        mod.apply(tui, qlinks, ctx)
        self.assertEqual([r["key"] for r in tui["pageData"]["replacementStrings"]],
                         [f"Parameter {i}" for i in (0, 2, 4, 6, 8, 10)] + ["Named choices"])
        for variant, handles in (("production", []), ("overlay", ["Parameter {Unsupported} and Parameter {Increment:Base=3}"])):
            unpruned = {"pageData": copy.deepcopy(original)}
            unpruned["pageData"]["tabs"][0]["handles"].extend(handles)
            expected = copy.deepcopy(unpruned)
            mod.apply(unpruned, qlinks, SimpleNamespace(meta={"variant": variant}, param_count=10))
            self.assertEqual(unpruned, expected)


def leaf_model(entry, definitions):
    """Follow the packaged passive native Label wrapper, keeping its geometry on the entry."""
    model = entry["componentData"]
    if model["type"].startswith("sharedNativeLabel"):
        definition = definitions[model["type"]]
        child, = definition["componentsData"]
        assert definition["ignoreMousePresses"] and not definition["actions"]
        assert child["bounds"]["boundsType"] == "Proportional" and child["bounds"]["bounds"] == "0 0 1 1"
        assert child["componentData"]["type"] == "Label"
        return child["componentData"]
    return model


# 1x1 transparent PNG
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360000002000001e221bc330000000049454e44ae426082")

MODULE_OK = textwrap.dedent('''
    def apply(tui, qlinks, ctx):
        import shutil
        shutil.copy(ctx.skins_dir.parent / "sample.png", ctx.skins_dir / "sample.png")
        tab = ctx.tab("Filter")
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


RUN_GEN_UI = ROOT / "scripts" / "run-gen-ui.py"


def generate_skins(tmp, layout=LAYOUT_JSON):
    """Plugin Skins folder as the build makes it (gen-mpc-ui.py + SDK gen_ui.py through scripts/run-gen-ui.py). Needs the SDK."""
    ui = tmp / "ui"
    subprocess.run([sys.executable, str(GEN), "--params", str(PARAMS_JSON), "--pages", str(PAGES_JSON), "--layout", str(layout),
                    "--out", str(ui)], check=True, capture_output=True)
    skins = tmp / "pf" / "Plugin Skins"
    subprocess.run([sys.executable, str(RUN_GEN_UI), "--sdk", str(SDK), "--params", str(ui / "params-hostsim.json"),
                    "--ui", str(ui / "ui.json"), "--out", str(skins), "--quiet"], check=True, capture_output=True)
    (tmp / "pf" / "sample.png").write_bytes(PNG)
    (tmp / "pf" / "skin-meta.json").write_bytes((ui / "skin-meta.json").read_bytes())   # next to the skins: run_apply passes it as --meta
    return skins


def extras_without(tmp, *module_files):
    """A folder with every entry of the real extras folder (symlinked) except the named modules: a test of one module (the spectrum meters, the control names) runs the chain without the module that
    removes the host widgets of the skin (97_no_fallback.py; 99_backgrounds.py for a tab with a background), so that it still sees the widgets it is about."""
    out = Path(tmp) / "extras-without"
    out.mkdir(parents=True, exist_ok=True)
    for entry in sorted(REAL_EXTRAS.iterdir()):
        if entry.name not in module_files and entry.name != "__pycache__":
            (out / entry.name).symlink_to(entry)
    return out


def extras_with_manifest(tmp, tabs):
    """The real extras folder with the host-skin background manifest `tabs` (a list of view names: [] or ["macros"]) instead of overlay-backgrounds/manifest.json: the tests exercise both states of the
    shelved feature through the real chain. 99_backgrounds.py is copied with its folder and manifest paths pointed at the real ones."""
    out = Path(tmp) / ("extras-manifest-" + "-".join(tabs or ["empty"]))
    out.mkdir(parents=True, exist_ok=True)
    for entry in sorted(REAL_EXTRAS.iterdir()):
        if entry.name not in ("99_backgrounds.py", "__pycache__"):
            (out / entry.name).symlink_to(entry)
    manifest = Path(tmp) / ("manifest-" + "-".join(tabs or ["empty"]) + ".json")
    manifest.write_text(json.dumps({"tabs": list(tabs)}), encoding="utf-8")
    src = (REAL_EXTRAS / "99_backgrounds.py").read_text(encoding="utf-8")
    src = src.replace("HERE = Path(__file__).resolve().parent", "HERE = Path(%r)" % str(REAL_EXTRAS), 1)
    src = src.replace('MANIFEST = ASSETS_DIR / "manifest.json"', "MANIFEST = Path(%r)" % str(manifest), 1)
    (out / "99_backgrounds.py").write_text(src, encoding="utf-8")
    return out


def run_apply(skins, extras):
    out, err = io.StringIO(), io.StringIO()
    meta = skins.parent / "skin-meta.json"
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = apply_mod.main(["--skins", str(skins), "--params", str(PARAMS_JSON), "--extras-dir", str(extras)]
                              + (["--meta", str(meta)] if meta.is_file() else []))
    return code, err.getvalue()


def qlinks_by_tab_name(tui, qlinks_map):
    """{tab name: its "Q-Links" dict} of a TUI.json and the "Screen Mode Q-Links" map (Tab = fnKeyIndex + 1, SubTab = fnKeySubIndex + 1)."""
    entries = {(m["Tab"], m["SubTab"]): m["Q-Links"] for m in qlinks_map}
    return {t["tabName"]: entries[(t["fnKeyIndex"] + 1, t["fnKeySubIndex"] + 1)] for t in tui["pageData"]["tabs"]}


def snapshot(skins):
    return {str(p.relative_to(skins)): p.read_bytes() for p in sorted(skins.rglob("*")) if p.is_file()}


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
        # the real modules (10_env, ...) change the skin on purpose; their own tests check the content.
        # Here: the run succeeds, both JSON files stay valid and the generation is idempotent from the same start
        skins, _ = self.fresh()
        self.assertEqual(run_apply(skins, REAL_EXTRAS), (0, ""))
        for name in ("TUI.json", "Q-Links.json"):
            json.loads((skins / name).read_text(encoding="utf-8"))
        first = snapshot(skins)
        skins2, _ = self.fresh()
        self.assertEqual(run_apply(skins2, REAL_EXTRAS), (0, ""))
        self.assertEqual(snapshot(skins2), first)

    def test_popup_markers_keep_pixels_order_and_actions(self):
        before, extras = self.fresh()
        after, _ = self.fresh()
        self.assertEqual(run_apply(before, extras_without(extras.parent, "95_popup_markers.py")), (0, ""))
        self.assertEqual(run_apply(after, REAL_EXTRAS), (0, ""))
        old_file, new_file = before / "TUI.json", after / "TUI.json"
        unpacked = apply_mod.load_json(old_file.read_text(encoding="utf-8"))
        packed = apply_mod.load_json(new_file.read_text(encoding="utf-8"))
        self.assertEqual((after / "Q-Links.json").read_bytes(), (before / "Q-Links.json").read_bytes())
        old_defs = unpacked["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        new_defs = packed["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        self.assertEqual([d["key"] for d in new_defs], [d["key"] for d in old_defs])

        def marker_pixels(components, skins):
            strip = Image.new("RGBA", (1280, 6))
            for entry in components:
                data = entry["componentData"]
                if data["type"] != "Image" or entry["bounds"]["boundsType"] != "Absolute":
                    continue
                x, y, w, h = map(int, entry["bounds"]["bounds"].split())
                if y == 120 and h == 6 and w in (6, 30):
                    with Image.open(skins / data["data"]["image"]) as src:
                        strip.alpha_composite(src.convert("RGBA"), (x, 0))
            return strip.tobytes()

        changed = set()
        for old, new in zip(old_defs, new_defs):
            old_cells = old["value"].get("componentsData", [])
            new_cells = new["value"].get("componentsData", [])
            old_strips = [i for i in range(len(old_cells) - 2)
                          if [c["bounds"].get("bounds") for c in old_cells[i:i + 3]] == [f"{x} 120 6 6" for x in (16, 28, 40)]]
            if not old_strips:
                continue
            self.assertEqual(len(old_strips), 1, old["key"])
            i = old_strips[0]
            changed.add(old["key"])
            with self.subTest(popup=old["key"]):
                self.assertEqual(new_cells[i]["bounds"]["bounds"], "16 120 30 6")
                self.assertEqual(new_cells[i]["componentData"]["data"]["image"], "native_modal_markers.png")
                self.assertEqual(marker_pixels(old_cells, before), marker_pixels(new_cells, after))
                new_cells[i:i + 1] = old_cells[i:i + 3]
        self.assertIn("fx editor choice menu", changed)
        self.assertIn("fx editor number menu", changed)
        self.assertIn("playEnvLfoMenu SYNC", changed)
        self.assertEqual(apply_mod.dump_json(packed, indent_step=0), apply_mod.dump_json(unpacked, indent_step=0))
        self.assertLess(new_file.stat().st_size, old_file.stat().st_size)
        self.assertLess(new_file.stat().st_size, TUI_JSON_BYTES_MAX)

        mod = load("test_popup_marker_optimizer", REAL_EXTRAS / "95_popup_markers.py")
        choice = next(d for d in old_defs if d["key"] == "fx editor choice menu")
        conditional = copy.deepcopy(choice)
        bound = conditional["value"]["componentsData"][-2]["bounds"]
        bound["additionalInvalidatingHandles"] = ["NonZero/Parameter 0"]
        model = {"pageData": {"componentDefinitions": {"localComponentDefinitions": [conditional]}}}
        original = copy.deepcopy(model)
        mod.apply(model, [], SimpleNamespace(meta={"variant": "overlay"}, skins_dir=before))
        self.assertEqual(model, original)

    def test_module_without_change_keeps_bytes(self):
        skins, extras = self.fresh()
        (extras / "10_noop.py").write_text("def apply(tui, qlinks, ctx):\n    ctx.tab('Filter'); ctx.param_index('env1.delay')\n")
        before = snapshot(skins)
        self.assertEqual(run_apply(skins, extras), (0, ""))
        self.assertEqual(snapshot(skins), before)

    def test_sample_module_adds_image(self):
        skins, extras = self.fresh()
        before = (skins / "TUI.json").read_text(encoding="utf-8")
        (extras / "10_sample.py").write_text(MODULE_OK)
        self.assertEqual(run_apply(skins, extras), (0, ""))
        tui = json.loads((skins / "TUI.json").read_text(encoding="utf-8"))
        tab = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == "Filter")
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
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "37 tabs; at most 32" not in l]
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
            "unknown tab": ("def apply(tui, qlinks, ctx):\n    ctx.tab('No Such Tab')\n", "unknown tab 'No Such Tab'; tabs are: 'Play', 'Macros'"),
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
        parsed = apply_mod.load_json(text)
        self.assertEqual(apply_mod.dump_json(parsed) + "\n", text)
        compact = apply_mod.dump_json(parsed, indent_step=0)
        self.assertEqual(apply_mod.load_json(compact), parsed)
        self.assertNotIn("\n", compact)


if __name__ == "__main__":
    unittest.main()
