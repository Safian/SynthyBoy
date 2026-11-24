# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for LFO screen production extra (99_ui_screen_lfo.py), hook discovery, contracts, and preview images."""

import copy
import importlib.util
import json
from pathlib import Path
import re
import sys
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

PARAMS_JSON = ROOT / "docs" / "params.json"
EXTRA_53_PATH = SCRIPTS / "mpc-skin-extras" / "53_lfo_smooth.py"
EXTRA_97_PATH = SCRIPTS / "mpc-skin-extras" / "97_no_fallback.py"
EXTRA_99_LFO_PATH = SCRIPTS / "mpc-skin-extras" / "99_ui_screen_lfo.py"

CONTRACT_PATH = SCRIPTS / "mpc-skin-extras" / "lfo-native-contract.json"
EDITOR_CONTRACT_PATH = SCRIPTS / "mpc-skin-extras" / "lfo-shape-editor-contract.json"
DETAIL_CONTRACT_PATH = SCRIPTS / "mpc-skin-extras" / "lfo-shape-detail-contract.json"

# Dynamically import extras
_spec_53 = importlib.util.spec_from_file_location("mpc_skin_extra_53_lfo_smooth", EXTRA_53_PATH)
lfo_extra_53 = importlib.util.module_from_spec(_spec_53)
_spec_53.loader.exec_module(lfo_extra_53)

_spec_97 = importlib.util.spec_from_file_location("mpc_skin_extra_97_no_fallback", EXTRA_97_PATH)
no_fallback_extra = importlib.util.module_from_spec(_spec_97)
_spec_97.loader.exec_module(no_fallback_extra)

_spec_99_lfo = importlib.util.spec_from_file_location("mpc_skin_extra_99_ui_screen_lfo", EXTRA_99_LFO_PATH)
ui_screen_lfo_extra = importlib.util.module_from_spec(_spec_99_lfo)
_spec_99_lfo.loader.exec_module(ui_screen_lfo_extra)


class MockContext:
    def __init__(self, params_dict):
        self.params = params_dict["params"]
        self._index = {p["key"]: i for i, p in enumerate(self.params)}
        self.param_count = len(self.params)
        self.meta = {"variant": "overlay"}
        self.skins_dir = Path(ROOT / "build" / "mock_skins")
        self.skins_dir.mkdir(parents=True, exist_ok=True)

    def param_index(self, key):
        if key not in self._index:
            raise KeyError(f"Unknown param: {key}")
        return self._index[key]

    def add_component_definition(self, key, value):
        defs = self.tui.setdefault("pageData", {}).setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
        if any(d.get("key") == key for d in defs):
            raise ValueError(f"component definition {key!r} already exists")
        defs.append({"key": key, "value": value})


class TestUiScreenLfo(unittest.TestCase):
    def setUp(self):
        with open(PARAMS_JSON, "r", encoding="utf-8") as f:
            self.params_data = json.load(f)
        self.ctx = MockContext(self.params_data)
        self.ctx.lfo_tab = {
            "tabName": "LFO",
            "fnKeyIndex": 2,
            "fnKeySubIndex": 0,
            "qlinkBoundsData": ["16 0 288 624", "336 0 288 624", "656 0 288 624", "976 0 288 624"],
            "componentDefinition": {
                "componentsData": [
                    {"componentData": {"name": "Overlay Marker lfo1 left", "type": "Image"},
                     "bounds": {"bounds": "33 1 6 6"}},
                    {"componentData": {"name": "Overlay Instance lfo1", "type": "Image"},
                     "bounds": {"bounds": "45 1 6 6"},
                     "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 2827"}]}},
                    {"componentData": {"name": "Overlay Marker lfo1 right", "type": "Image"},
                     "bounds": {"bounds": "1241 1 6 6"}},
                ]
            },
        }
        self.ctx.tui = {
            "pageData": {
                "tabs": [self.ctx.lfo_tab],
                "componentDefinitions": {
                    "localComponentDefinitions": []
                },
                "replacementStrings": [],
            }
        }

    def test_hook_discovery_and_order(self):
        """99_ui_screen_lfo.py is present in scripts/mpc-skin-extras/ and sorts at end of pipeline."""
        extras_dir = SCRIPTS / "mpc-skin-extras"
        py_files = sorted(p.name for p in extras_dir.glob("*.py"))
        self.assertIn("99_ui_screen_lfo.py", py_files)
        idx_53 = py_files.index("53_lfo_smooth.py")
        idx_97 = py_files.index("97_no_fallback.py")
        idx_99 = py_files.index("99_ui_screen_lfo.py")
        self.assertLess(idx_53, idx_97)
        self.assertLess(idx_97, idx_99)

    def test_apply_chain_with_53_and_99_does_not_duplicate_definitions(self):
        """Applying 53_lfo_smooth followed by 97_no_fallback and 99_ui_screen_lfo succeeds without duplicate definition clashes."""
        # 1. Run 53_lfo_smooth
        lfo_extra_53.apply(self.ctx.tui, {}, self.ctx)
        # 2. Run 97_no_fallback
        no_fallback_extra.apply(self.ctx.tui, {}, self.ctx)
        # 3. Run 99_ui_screen_lfo
        ui_screen_lfo_extra.apply(self.ctx.tui, {}, self.ctx)

        tab = self.ctx.tui["pageData"]["tabs"][0]
        # Rule 5b: strictly no visible Q-Link frames or numbers
        self.assertEqual(tab["qlinkBoundsData"], [])

        # Check definitions are unique
        defs = self.ctx.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        keys = [d["key"] for d in defs]
        self.assertEqual(len(keys), len(set(keys)), "Duplicate definition keys found")

    def test_apply_standalone_without_53_populates_lfo_tab(self):
        """If 53_lfo_smooth did not run, 99_ui_screen_lfo successfully populates and verifies the tab."""
        fresh_ctx = MockContext(self.params_data)
        fresh_ctx.lfo_tab = copy.deepcopy(self.ctx.lfo_tab)
        fresh_ctx.tui = {
            "pageData": {
                "tabs": [fresh_ctx.lfo_tab],
                "componentDefinitions": {
                    "localComponentDefinitions": []
                },
                "replacementStrings": [],
            }
        }
        ui_screen_lfo_extra.apply(fresh_ctx.tui, {}, fresh_ctx)

        tab = fresh_ctx.tui["pageData"]["tabs"][0]
        comp_names = {c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]}
        self.assertTrue(any(n.startswith("LFO 1 Native ") for n in comp_names))
        self.assertEqual(tab["qlinkBoundsData"], [])

    def test_all_contract_counts(self):
        """Verify the 29 base standalone widgets, 29 Shape Editor widgets, and 18 Shape Detail widgets."""
        lfo_extra_53.apply(self.ctx.tui, {}, self.ctx)
        ui_screen_lfo_extra.apply(self.ctx.tui, {}, self.ctx)

        tab = self.ctx.tui["pageData"]["tabs"][0]
        items = tab["componentDefinition"]["componentsData"]
        native = [c for c in items if c["componentData"]["name"].startswith("LFO 1 Native ")]

        root = [c for c in native if not c["componentData"]["name"].startswith(("LFO 1 Native Editor ", "LFO 1 Native Detail "))]
        editor = [c for c in native if c["componentData"]["name"].startswith("LFO 1 Native Editor ")]
        detail = [c for c in native if c["componentData"]["name"].startswith("LFO 1 Native Detail ")]

        self.assertEqual(len(root), 29)
        self.assertEqual(len(editor), 29)
        self.assertEqual(len(detail), 18)

    def test_preview_images_present_and_valid(self):
        """Generated preview images exist, have valid dimensions, and have 0 yellow Q-Link badge pixels."""
        composite_path = (Path(__file__).resolve().parents[2] / 'docs/screens/lfo.jpg')
        ui_map_path = (Path(__file__).resolve().parents[2] / 'docs/screens/lfo.jpg')
        breakdown_path = (Path(__file__).resolve().parents[2] / 'docs/screens/lfo.jpg')

        self.assertTrue(composite_path.is_file(), f"Missing {composite_path}")
        self.assertTrue(ui_map_path.is_file(), f"Missing {ui_map_path}")
        self.assertTrue(breakdown_path.is_file(), f"Missing {breakdown_path}")

        with Image.open(composite_path) as im_comp:
            self.assertEqual(im_comp.size, (1280, 800))

        with Image.open(ui_map_path) as im_ui_map:
            self.assertEqual(im_ui_map.size, (1280, 800))

        with Image.open(breakdown_path) as im_breakdown:
            self.assertEqual(im_breakdown.size[0], 1280)
            self.assertGreaterEqual(im_breakdown.size[1], 1944)

        # Check for yellow Q-Link badge pixels
        for path in (composite_path, ui_map_path):
            with Image.open(path) as raw_im:
                im = raw_im.convert("RGB")
            yellows = 0
            for y in range(im.height):
                for x in range(im.width):
                    r, g, b = im.getpixel((x, y))
                    if r > 220 and g > 180 and b < 50:
                        yellows += 1
            self.assertEqual(yellows, 0, f"Found yellow Q-Link badge pixels in {path}")

    def test_zero_visible_q_numbers_and_clean_qlink_bounds(self):
        """Verify Rule 5b: strictly no visible Q-Link numbers, chips, or frames in component text or qlinkBoundsData."""
        lfo_extra_53.apply(self.ctx.tui, {}, self.ctx)
        ui_screen_lfo_extra.apply(self.ctx.tui, {}, self.ctx)

        tab = self.ctx.tui["pageData"]["tabs"][0]
        self.assertEqual(tab["qlinkBoundsData"], [])

        q_pattern = re.compile(r"\bQ\s*[1-9][0-9]?\b", re.IGNORECASE)
        for item in tab["componentDefinition"]["componentsData"]:
            name = item["componentData"].get("name", "")
            # Component internal names may contain technical tags, but visible text must have no Q numbers
            text = item["componentData"].get("data", {}).get("text", "")
            self.assertFalse(q_pattern.search(text), f"Visible Q-number found in text: {text}")

    def test_screen_lfo_contract_metadata(self):
        """Verify screen_lfo_contract is registered with rebuilt components, approved radio 1, and rejected concepts."""
        lfo_extra_53.apply(self.ctx.tui, {}, self.ctx)
        ui_screen_lfo_extra.apply(self.ctx.tui, {}, self.ctx)

        self.assertTrue(hasattr(self.ctx, "screen_lfo_contract"))
        contract = self.ctx.screen_lfo_contract
        self.assertEqual(contract["title"], "LFO")
        self.assertGreaterEqual(len(contract["rebuilt_components"]), 10)
        self.assertEqual(contract["approved_radio_variant"]["variant_id"], "VARIANT_RADIO_DOT")
        self.assertIn("Visible Q-Link numbers", contract["rejected_concepts"][0])

    def test_native_choice_group_radio1_available(self):
        """Verify native_choice_group module provides approved Variant 1 segmented radio selector."""
        import native_choice_group as ncg
        self.assertEqual(ncg.VARIANT_SEGMENTED, "segmented")
        self.assertTrue(hasattr(ncg, "create_segmented_choice_group"))
        self.assertTrue(hasattr(ncg, "render_segmented_button_art"))


if __name__ == "__main__":
    unittest.main()
