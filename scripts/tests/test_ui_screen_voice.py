# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for Voice screen production extra (99_ui_screen_voice.py), hook discovery, and native geometry."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from scripts.native_voice.voice_tab import (
    KEEP,
    UNCONVERTED_CONTROLS,
    VOICE_NATIVE_CONTROL_NAMES,
    VOICE_NATIVE_CONTROLS,
    VOICE_ROWS,
    VOICE_SEQUENCE_CONTROLS,
    VOICE_STEPS,
    VOICE_TAB_NAME,
    sequence_base,
)

PARAMS_JSON = ROOT / "docs" / "params.json"
EXTRA_84_PATH = SCRIPTS / "mpc-skin-extras" / "84_voice.py"
EXTRA_97_PATH = SCRIPTS / "mpc-skin-extras" / "97_no_fallback.py"
EXTRA_99_VOICE_PATH = SCRIPTS / "mpc-skin-extras" / "99_ui_screen_voice.py"

# Import 84_voice.py dynamically
_spec_84 = importlib.util.spec_from_file_location("mpc_skin_extra_84_voice", EXTRA_84_PATH)
voice_extra_84 = importlib.util.module_from_spec(_spec_84)
_spec_84.loader.exec_module(voice_extra_84)

# Import 97_no_fallback.py dynamically
_spec_97 = importlib.util.spec_from_file_location("mpc_skin_extra_97_no_fallback", EXTRA_97_PATH)
no_fallback_extra = importlib.util.module_from_spec(_spec_97)
_spec_97.loader.exec_module(no_fallback_extra)

# Import 99_ui_screen_voice.py dynamically
_spec_99_voice = importlib.util.spec_from_file_location("mpc_skin_extra_99_ui_screen_voice", EXTRA_99_VOICE_PATH)
ui_screen_voice_extra = importlib.util.module_from_spec(_spec_99_voice)
_spec_99_voice.loader.exec_module(ui_screen_voice_extra)


class MockContext:
    def __init__(self, params_dict):
        self.params = params_dict["params"]
        self._index = {p["key"]: i for i, p in enumerate(self.params)}
        self.meta = {"variant": "overlay"}
        self.skins_dir = str(ROOT / "build")

    def param_index(self, key):
        if key not in self._index:
            raise KeyError(f"Unknown param: {key}")
        return self._index[key]

    def add_component_definition(self, key, value):
        defs = self.tui.setdefault("pageData", {}).setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
        if any(d.get("key") == key for d in defs):
            raise ValueError(f"component definition {key!r} already exists")
        defs.append({"key": key, "value": value})


class TestUiScreenVoice(unittest.TestCase):
    def setUp(self):
        with open(PARAMS_JSON, "r", encoding="utf-8") as f:
            self.params_data = json.load(f)
        self.ctx = MockContext(self.params_data)
        self.ctx.voice_tab = {
            "tabName": VOICE_TAB_NAME,
            "fnKeyIndex": 4,
            "fnKeySubIndex": 0,
            "qlinkBoundsData": ["16 0 288 624", "336 0 288 624", "656 0 288 624", "976 0 288 624"],
            "componentDefinition": {
                "componentsData": [
                    {"componentData": {"name": "Overlay Marker Left"}},
                    {
                        "componentData": {"name": "Overlay Instance Voice"},
                        "handle remapping": {
                            "version": 1,
                            "map": [{"key": "Data", "value": "Parameter 2675"}],
                        },
                    },
                ]
            },
        }
        self.ctx.tui = {
            "pageData": {
                "tabs": [self.ctx.voice_tab],
                "componentDefinitions": {
                    "localComponentDefinitions": []
                },
                "replacementStrings": [],
            }
        }

    def test_hook_discovery_and_order(self):
        """99_ui_screen_voice.py is present in scripts/mpc-skin-extras/ and sorts at end of pipeline."""
        extras_dir = SCRIPTS / "mpc-skin-extras"
        py_files = sorted(p.name for p in extras_dir.glob("*.py"))
        self.assertIn("99_ui_screen_voice.py", py_files)
        idx_84 = py_files.index("84_voice.py")
        idx_97 = py_files.index("97_no_fallback.py")
        idx_99 = py_files.index("99_ui_screen_voice.py")
        self.assertLess(idx_84, idx_97)
        self.assertLess(idx_97, idx_99)

    def test_apply_chain_with_84_and_99_does_not_duplicate_definitions(self):
        """Applying 84_voice followed by 99_ui_screen_voice must not crash with duplicate definition errors."""
        # 1. Run 84_voice
        voice_extra_84.apply(self.ctx.tui, {}, self.ctx)
        # 2. Run 97_no_fallback
        no_fallback_extra.apply(self.ctx.tui, {}, self.ctx)
        # 3. Run 99_ui_screen_voice
        ui_screen_voice_extra.apply(self.ctx.tui, {}, self.ctx)

        tab = self.ctx.tui["pageData"]["tabs"][0]
        comp_names = {c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]}
        self.assertEqual(set(comp_names), VOICE_NATIVE_CONTROL_NAMES | {"Overlay Marker Left", "Overlay Instance Voice"})
        # Enforce Rule 5b: NO visible Q-Link frames or numbers
        self.assertEqual(tab["qlinkBoundsData"], [])

    def test_apply_standalone_without_84_populates_voice_tab(self):
        """If 84_voice did not run, 99_ui_screen_voice successfully populates and verifies the tab."""
        fresh_ctx = MockContext(self.params_data)
        fresh_ctx.voice_tab = copy.deepcopy(self.ctx.voice_tab)
        fresh_ctx.tui = {
            "pageData": {
                "tabs": [fresh_ctx.voice_tab],
                "componentDefinitions": {
                    "localComponentDefinitions": []
                },
                "replacementStrings": [],
            }
        }
        ui_screen_voice_extra.apply(fresh_ctx.tui, {}, fresh_ctx)

        tab = fresh_ctx.tui["pageData"]["tabs"][0]
        comp_names = {c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]}
        self.assertTrue(VOICE_NATIVE_CONTROL_NAMES.issubset(comp_names))
        self.assertEqual(tab["qlinkBoundsData"], [])

    def test_all_17_native_controls_geometry_rules(self):
        """All 17 native controls must have w >= 56, h >= 56, and fit within 1280x628."""
        all_controls = list(VOICE_NATIVE_CONTROLS)
        for name, kind, rect, caption, key in VOICE_SEQUENCE_CONTROLS:
            all_controls.append({"name": name, "rect": rect})

        self.assertEqual(len(all_controls), 17)
        for ctrl in all_controls:
            x, y, w, h = ctrl["rect"]
            self.assertGreaterEqual(w, 56, f"{ctrl['name']} width must be >= 56px")
            self.assertGreaterEqual(h, 56, f"{ctrl['name']} height must be >= 56px")
            self.assertGreaterEqual(x, 0, f"{ctrl['name']} x out of bounds")
            self.assertGreaterEqual(y, 0, f"{ctrl['name']} y out of bounds")
            self.assertLessEqual(x + w, 1280, f"{ctrl['name']} right edge exceeds 1280")
            self.assertLessEqual(y + h, 628, f"{ctrl['name']} bottom edge exceeds 628")

    def test_no_overlap_between_converted_controls_and_unconverted_regions(self):
        """Native apertures must not overlap each other or retained dynamic regions (VoiceGrid, stripRect)."""
        all_rects = [(c["name"], c["rect"]) for c in VOICE_NATIVE_CONTROLS]
        all_rects.extend((name, rect) for name, _, rect, _, _ in VOICE_SEQUENCE_CONTROLS)

        for i in range(len(all_rects)):
            n1, (x1, y1, w1, h1) = all_rects[i]
            for j in range(i + 1, len(all_rects)):
                n2, (x2, y2, w2, h2) = all_rects[j]
                overlaps = not (x1 + w1 <= x2 or x2 + w2 <= x1 or y1 + h1 <= y2 or y2 + h2 <= y1)
                self.assertFalse(overlaps, f"{n1} overlaps {n2}")

        for unc in UNCONVERTED_CONTROLS:
            ux, uy, uw, uh = unc["rect"]
            for name, (nx, ny, nw, nh) in all_rects:
                overlaps = not (nx + nw <= ux or ux + uw <= nx or ny + nh <= uy or uy + uh <= ny)
                self.assertFalse(overlaps, f"Native {name} overlaps retained {unc['name']}")

    def test_replacement_strings_populated(self):
        """Replacement strings for mode, rows and steps are properly populated."""
        voice_extra_84.apply(self.ctx.tui, {}, self.ctx)
        ui_screen_voice_extra.apply(self.ctx.tui, {}, self.ctx)
        replacements = {r["key"]: r["value"] for r in self.ctx.tui["pageData"]["replacementStrings"]}

        mode_key = f"Parameter {self.ctx.param_index('voicectl.scale.lfo_mode')}"
        self.assertIn(mode_key, replacements)
        self.assertEqual(replacements[mode_key], ["Percent", "Rate"])

        base = sequence_base(self.ctx)
        self.assertIn(f"Parameter {base}", replacements)
        self.assertEqual(replacements[f"Parameter {base}"], list(VOICE_ROWS))
        self.assertIn(f"Parameter {base + 1}", replacements)
        self.assertEqual(replacements[f"Parameter {base + 1}"], list(VOICE_STEPS))


    def test_voice_step_knobs_shipping_integration(self):
        """Verify eight visible knobs share two existing native families on the shipping page."""
        voice_extra_84.apply(self.ctx.tui, {}, self.ctx)
        ui_screen_voice_extra.apply(self.ctx.tui, {}, self.ctx)
        tab = self.ctx.tui["pageData"]["tabs"][0]
        comps_by_name = {c["componentData"]["name"]: c for c in tab["componentDefinition"]["componentsData"]}

        base = sequence_base(self.ctx)
        row_handle = f"Parameter {base}"
        for row_idx, label in enumerate(VOICE_ROWS):
            expected_row_gate = f"IndexedEnabling/{row_idx}/{len(VOICE_ROWS)}/{row_handle}"
            for step in range(1, 9):
                name = f"Voice Native Step Knob {label} {step}"
                self.assertIn(name, comps_by_name)
                comp = comps_by_name[name]
                self.assertEqual(comp["componentData"]["type"], "voiceNativeStepPan" if row_idx == 0 else "voiceNativeStepLevel")
                defs = {d["key"]:d["value"] for d in self.ctx.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
                bounds = comp["bounds"]
                self.assertEqual(bounds["acceptsHWFocus"], "Yes")
                self.assertEqual(bounds["showWhenDataModelInvalid"], "Hide")
                x, y, w, h = map(int, bounds["bounds"].split())
                self.assertGreaterEqual(w, 56)
                self.assertGreaterEqual(h, 56)
                self.assertEqual(w, 90)
                self.assertEqual(h, 90)
                self.assertEqual(x, 286 + (step - 1) * 125)
                self.assertEqual(y, 120)
                gates = bounds["additionalInvalidatingHandles"]
                self.assertEqual(len(gates), 2)
                self.assertEqual(gates[1], expected_row_gate)


if __name__ == "__main__":
    unittest.main()
