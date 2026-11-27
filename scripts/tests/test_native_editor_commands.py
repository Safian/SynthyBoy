#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for native PointMod source controls and curve editor command buttons (94_editor_commands.py)."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
EXTRA_PATH = SCRIPTS / "mpc-skin-extras" / "94_editor_commands.py"
PARAMS_JSON = ROOT / "docs" / "params.json"

spec = importlib.util.spec_from_file_location("mpc_skin_extra_94_editor_commands", EXTRA_PATH)
editor_commands = importlib.util.module_from_spec(spec)
spec.loader.exec_module(editor_commands)


class MockContext:
    def __init__(self, params_dict, skins_dir=None):
        self.params = params_dict["params"]
        self._index = {p["key"]: i for i, p in enumerate(self.params)}
        self.param_count = len(self.params)
        self.meta = {"variant": "overlay"}
        self.skins_dir = Path(skins_dir) if skins_dir else Path(ROOT / "build" / "mock_skins")
        self.skins_dir.mkdir(parents=True, exist_ok=True)
        self.tui = {
            "pageData": {
                "tabs": [],
                "componentDefinitions": {
                    "localComponentDefinitions": []
                },
                "replacementStrings": [],
            }
        }

    def param_index(self, key):
        if key not in self._index:
            raise KeyError(f"Unknown param: {key}")
        return self._index[key]

    def add_component_definition(self, key, value):
        defs = self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        if any(d.get("key") == key for d in defs):
            raise ValueError(f"component definition {key!r} already exists")
        defs.append({"key": key, "value": value})


class TestNativeEditorCommands(unittest.TestCase):
    def setUp(self):
        with open(PARAMS_JSON, "r", encoding="utf-8") as f:
            self.params_data = json.load(f)
        self.ctx = MockContext(self.params_data)

    def test_pointmod_source_controls_added(self):
        items = []
        base = 3284
        point_view = f"IndexedEnabling/1/4/Parameter {base}"

        editor_commands.apply_pointmod_source_controls(self.ctx.tui, items, self.ctx, base, point_view)

        by_name = {c["componentData"]["name"]: c for c in items}

        # 1. Check dropdown
        self.assertIn("PointMod Native Source Dropdown", by_name)
        dd = by_name["PointMod Native Source Dropdown"]
        self.assertEqual(dd["bounds"]["bounds"], "640 24 220 56")
        self.assertEqual(dd["bounds"]["additionalInvalidatingHandles"], [point_view])
        self.assertEqual(dd["componentData"]["type"], "pointmodNativeSourceDropdown_220x56")
        self.assertEqual(dd["handle remapping"]["map"][0]["value"], f"WithName[Source]/Parameter {base + 17}")

        # Check dropdown definition has Show Overlay action
        defs = {d["key"]: d["value"] for d in self.ctx.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertIn("pointmodNativeSourceDropdown_220x56", defs)
        dd_def = defs["pointmodNativeSourceDropdown_220x56"]
        self.assertTrue(any(a["handler"] == "Show Overlay" and a["additionalData"] == "fx editor choice menu"
                            for a in dd_def["actions"]))

        # 2. Check quick chips
        chip_labels = [c[0] for c in editor_commands.POINTMOD_CHIPS]
        self.assertEqual(len(chip_labels), 17)
        for label in chip_labels:
            chip_name = f"PointMod Source {label}"
            self.assertIn(chip_name, by_name)
            chip = by_name[chip_name]
            self.assertEqual(chip["componentData"]["type"], "Button")
            self.assertEqual(chip["componentData"]["data"]["gestureBehaviour"], "Instant")
            self.assertEqual(chip["componentData"]["data"]["numButtonsInGroup"], 58)
            self.assertEqual(chip["bounds"]["additionalInvalidatingHandles"], [point_view])
            self.assertEqual(chip["handle remapping"]["map"][0]["value"],
                             f"WithName[{label}]/FromGesture/Parameter {base + 17}")

        # 3. Check All Sources button
        self.assertIn("PointMod Native All Sources", by_name)
        all_src = by_name["PointMod Native All Sources"]
        self.assertEqual(all_src["bounds"]["bounds"], "160 410 200 48")
        self.assertEqual(all_src["bounds"]["additionalInvalidatingHandles"], [point_view])

    def test_scalar_curve_buttons_added(self):
        items = []
        editor_commands.apply_scalar_curve_buttons(self.ctx.tui, items, self.ctx, filter_num=1)

        by_name = {c["componentData"]["name"]: c for c in items}
        for label, action_id, rect in editor_commands.SCALAR_CURVE_BUTTONS:
            btn_name = f"Filter 1 ScalarCurve {label}"
            self.assertIn(btn_name, by_name)
            btn = by_name[btn_name]
            self.assertEqual(btn["componentData"]["type"], "Button")
            self.assertEqual(btn["componentData"]["data"]["gestureBehaviour"], "Instant")
            self.assertEqual(btn["componentData"]["data"]["buttonId"], action_id)
            self.assertEqual(btn["componentData"]["data"]["numButtonsInGroup"], 13)
            self.assertEqual(btn["bounds"]["bounds"], "%d %d %d %d" % rect)

    def test_apply_pipeline_integration_and_idempotence(self):
        lfo_tab = {
            "tabName": "LFO",
            "componentDefinition": {"componentsData": []},
        }
        note_tab = {
            "tabName": "Note",
            "componentDefinition": {"componentsData": []},
        }
        self.ctx.tui["pageData"]["tabs"] = [lfo_tab, note_tab]

        # First run
        editor_commands.apply(self.ctx.tui, {}, self.ctx)

        lfo_items = lfo_tab["componentDefinition"]["componentsData"]
        self.assertTrue(any(c["componentData"]["name"] == "PointMod Native Source Dropdown" for c in lfo_items))
        self.assertEqual(len([c for c in lfo_items if c["componentData"]["name"].startswith("PointMod Source ")]), 17)

        note_items = note_tab["componentDefinition"]["componentsData"]
        self.assertTrue(any("ScalarCurve INIT" in c["componentData"]["name"] for c in note_items))

        count_lfo_first = len(lfo_items)
        count_note_first = len(note_items)

        # Second run - should be idempotent
        editor_commands.apply(self.ctx.tui, {}, self.ctx)
        self.assertEqual(len(lfo_items), count_lfo_first)
        self.assertEqual(len(note_items), count_note_first)


if __name__ == "__main__":
    unittest.main()
