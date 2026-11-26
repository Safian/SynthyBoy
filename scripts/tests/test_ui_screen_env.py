#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the standalone Env screen integration (96_env_native.py)."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# Add scripts directory to path for imports
_SCRIPTS = Path(__file__).resolve().parent.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

import tests.test_mpc_skin_extras as base
from native_ui_params import (
    ENV_FIELDS,
    ENV_SELECTION,
    ENV_TARGETS,
    ENV_VIEW,
    mixer_peek_index,
)


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found")
class UiScreenEnvTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root)
        cls.code, cls.err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))
        params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.param_count = len(params)
        cls.base = mixer_peek_index(cls.param_count)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self):
        tabs = self.tui["pageData"]["tabs"]
        found = [t for t in tabs if t.get("tabName") == "Env"]
        self.assertEqual(len(found), 1, "Env tab must exist exactly once")
        return found[0]

    def local_defs(self):
        return {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_autoload_hook_execution(self):
        """Autoload hook run order and discovery produces exit code 0 without errors."""
        self.assertEqual((self.code, self.err), (0, ""))

    def test_qlink_bounds_empty_no_visible_numbers_or_bars(self):
        """Owner rule: qlinkBoundsData must be empty so host draws no orange frames or badges."""
        tab = self.tab()
        self.assertEqual(tab.get("qlinkBoundsData"), [], "qlinkBoundsData must be [] on Env screen")

    def test_env_background_is_static_overlay_bg(self):
        """The tab background must use the pre-rendered overlay_canvas.png."""
        tab = self.tab()
        bg = tab["componentDefinition"]["backgroundData"]
        self.assertEqual(bg["focussed"].get("image"), "overlay_canvas.png")
        self.assertEqual(bg["unfocussed"].get("image"), "overlay_canvas.png")

    def test_selector_strip_and_cells(self):
        """The Env tabs have no selector (the hardware sub-tab names the envelope); the Play page's expanded editor keeps its 6 cells with >= 56px touch targets."""
        defs = self.local_defs()
        for name in ("Env", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6"):
            tab = next(t for t in self.tui["pageData"]["tabs"] if t.get("tabName") == name)
            self.assertFalse([c for c in tab["componentDefinition"]["componentsData"] if "Selector" in c["componentData"]["name"]], name)
        play = next(t for t in self.tui["pageData"]["tabs"] if t.get("tabName") == "Play")
        selector_strip = next((c for c in play["componentDefinition"]["componentsData"] if c["componentData"]["name"] == "Play Expanded Envelope Selector"), None)
        self.assertIsNotNone(selector_strip, "the expanded Play envelope editor keeps its selector strip")
        rect = [int(v) for v in selector_strip["bounds"]["bounds"].split()]
        self.assertEqual(rect, [164, 12, 528, 56])

        # Verify packed child cells
        strip_def = defs.get(selector_strip["componentData"]["type"])
        self.assertIsNotNone(strip_def)
        cells = strip_def.get("componentsData", [])
        self.assertEqual(len(cells), 6, "Must contain exactly 6 selector cells")
        for i, cell in enumerate(cells):
            b = [int(v) for v in cell["bounds"]["bounds"].split()]
            self.assertEqual(b[2], 88, f"Cell {i+1} width must be 88")
            self.assertEqual(b[3], 56, f"Cell {i+1} height must be 56 (>= 56px touch target)")

    def test_full_controls_and_11_fields(self):
        """All 11 bottom-band controls and TARGETS button must be present."""
        defs = self.local_defs()
        tab = self.tab()
        items = tab["componentDefinition"]["componentsData"]
        full_ctrl = next((c for c in items if c["componentData"]["name"] == "Env Native Full Controls"), None)
        self.assertIsNotNone(full_ctrl, "Env Native Full Controls must be present on Env tab")

        full_def = defs.get(full_ctrl["componentData"]["type"])
        self.assertIsNotNone(full_def)
        child_names = {c["componentData"]["name"]: c for c in full_def.get("componentsData", [])}

        expected_controls = [
            ("Env Native Targets", "envNativeTargets", (1072, 12, 160, 56)),
            ("Env Native Attack", "envNativeDial", (14, 482, 107, 132)),
            ("Env Native Decay", "envNativeDial", (125, 482, 107, 132)),
            ("Env Native Sustain", "envNativeDial", (236, 482, 107, 132)),
            ("Env Native Release", "envNativeDial", (347, 482, 107, 132)),
            ("Env Native Attack Curve", "envNativeCurve", (474, 482, 106, 132)),
            ("Env Native Decay Curve", "envNativeCurve", (584, 482, 106, 132)),
            ("Env Native Release Curve", "envNativeCurve", (694, 482, 106, 132)),
            ("Env Native Delay", "envNativeDial", (821, 482, 108, 132)),
            ("Env Native Hold", "envNativeDial", (933, 482, 108, 132)),
            ("Env Native Beats", "envNativeBeatsSwitch", (1045, 482, 108, 132)),
            ("Env Native Retrig", "envNativeRetrigSwitch", (1157, 482, 109, 132)),
        ]

        for name, expected_type, expected_rect in expected_controls:
            self.assertIn(name, child_names, f"Control {name} missing")
            c = child_names[name]
            self.assertEqual(c["componentData"]["type"], expected_type)
            rect = tuple(int(v) for v in c["bounds"]["bounds"].split())
            self.assertEqual(rect, expected_rect, f"Geometry mismatch for {name}")
            # Touch target minimum check
            self.assertGreaterEqual(rect[2], 56, f"Width of {name} must be >= 56")
            self.assertGreaterEqual(rect[3], 56, f"Height of {name} must be >= 56")

    def test_popup_action_on_knobs(self):
        """Native dials must have double-click/enter action to open fx editor number menu."""
        defs = self.local_defs()
        dial_def = defs.get("envNativeDial")
        self.assertIsNotNone(dial_def)
        actions = {a["onAction"]: a["additionalData"] for a in dial_def.get("actions", [])}
        self.assertEqual(actions.get("Double Click"), "fx editor number menu")
        self.assertEqual(actions.get("Enter Pressed"), "fx editor number menu")

    def test_state_strings_present(self):
        """Replacement strings for Beats (MS/BEATS) and Retrig (OFF/ON) must be registered."""
        strings = {s["key"]: s["value"] for s in self.tui["pageData"].get("replacementStrings", [])}
        values = self.base + ENV_FIELDS
        beats_key = f"Parameter {values + 9}"
        retrig_key = f"Parameter {values + 10}"
        self.assertEqual(strings.get(beats_key), ["MS", "BEATS"])
        self.assertEqual(strings.get(retrig_key), ["OFF", "ON"])

    def test_sdk_validate_passes_cleanly(self):
        """Official SDK validate.py must pass with 0 errors on generated skin."""
        res = subprocess.run(
            [sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(self.skins.parent)],
            capture_output=True,
            text=True,
        )
        report = res.stdout + res.stderr
        errors = [line for line in report.splitlines() if line.startswith("ERROR") and "plugin.json missing" not in line]
        self.assertEqual(errors, [], report)


if __name__ == "__main__":
    unittest.main()
