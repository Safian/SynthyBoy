# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for scripts/mpc-skin-extras/99_ui_screen_macros.py."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "tests"))

from PIL import Image

from test_mpc_skin_extras import PARAMS_JSON, ROOT as EXTRAS_ROOT, generate_skins, run_apply
from test_mpc_skin_overlay import OVERLAY_LAYOUT

# Load apply-mpc-skin-extras loader
spec = importlib.util.spec_from_file_location("ame", str(ROOT / "scripts" / "apply-mpc-skin-extras.py"))
ame = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ame)

filter_overlay_extra = importlib.import_module("mpc-skin-extras.50_filter_overlay")
from native_ui_params import MACRO_TARGET, native_base_index
screen_macros_99 = importlib.import_module("mpc-skin-extras.99_ui_screen_macros")


class TestUiScreenMacros(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self._tmp.name)
        self.skins = generate_skins(self.tmp_path, OVERLAY_LAYOUT)
        self.tui = ame.load_json((self.skins / "TUI.json").read_text(encoding="utf-8"))
        self.qlinks = ame.load_json((self.skins / "Q-Links.json").read_text(encoding="utf-8"))
        self.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))
        self.meta = {"variant": "overlay"}
        self.ctx = ame.Context(self.tui, self.params, self.skins, self.meta)
        filter_overlay_extra.apply(self.tui, self.qlinks, self.ctx)

    def tearDown(self):
        self._tmp.cleanup()

    def test_hook_discovery_and_order(self):
        """Verifies 99_ui_screen_macros.py is discovered and ordered after 97_no_fallback."""
        extras_dir = ROOT / "scripts" / "mpc-skin-extras"
        all_extras = sorted([p.name for p in extras_dir.glob("*.py")])
        self.assertIn("99_ui_screen_macros.py", all_extras)
        idx_97 = all_extras.index("97_no_fallback.py")
        idx_99 = all_extras.index("99_ui_screen_macros.py")
        self.assertLess(idx_97, idx_99, "99_ui_screen_macros must run after 97_no_fallback")

    def test_screen_macros_adds_card_definitions_and_assets(self):
        """Verifies macroCard and macroSubLevel component definitions and generated visual assets."""
        screen_macros_99.apply(self.tui, self.qlinks, self.ctx)
        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertIn("macroSubLevel", defs)
        self.assertIn("macroCard", defs)

        # 1. Level dial filmstrip asset
        strip_file = self.skins / "play_sub_level_90.png"
        self.assertTrue(strip_file.is_file(), "play_sub_level_90.png must exist in skins directory")
        with Image.open(strip_file) as im:
            self.assertEqual(im.size, (90, 90 * 127))

        # Overlay macroSubLevel omits host Name label so C++ overlay owns Name exclusively
        sub_children = {c["componentData"]["name"]: c for c in defs["macroSubLevel"]["componentsData"]}
        self.assertIn("knob", sub_children)
        self.assertIn("value", sub_children)
        self.assertNotIn("name", sub_children)
        self.assertEqual(sub_children["knob"]["bounds"]["bounds"], "31 4 90 90")
        self.assertEqual(sub_children["value"]["bounds"]["bounds"], "8 96 136 22")

        # 2. Card composite structure and target button asset
        card_def = defs["macroCard"]
        sub_level = next(c for c in card_def["componentsData"] if c["componentData"]["name"] == "level")
        self.assertEqual(sub_level["componentData"]["type"], "macroSubLevel")
        self.assertEqual(sub_level["bounds"]["bounds"], "0 0 152 118")

        target_btn = next(c for c in card_def["componentsData"] if c["componentData"]["name"] == "target")
        self.assertEqual(target_btn["componentData"]["type"], "Button")
        self.assertEqual(target_btn["bounds"]["bounds"], "4 178 144 56")
        self.assertEqual(target_btn["handle remapping"]["map"], [{"key": "Data", "value": "Target"}])

        btn_img = target_btn["componentData"]["data"]["onImage"]
        btn_path = self.skins / btn_img
        self.assertTrue(btn_path.is_file(), f"Image {btn_img} must exist in skins directory")
        with Image.open(btn_path) as im:
            self.assertEqual(im.size, (144, 56), "Button slot image must be compact 144x56 slot")

    def test_screen_macros_geometry_and_non_overlap(self):
        """Verifies 8-in-one-row MacroCard placement, spectrum clearance, no bank widget, and isolated target action."""
        screen_macros_99.apply(self.tui, self.qlinks, self.ctx)
        tab = self.ctx.tab("Macros")
        components = tab["componentDefinition"]["componentsData"]
        cards = [c for c in components if c["componentData"]["type"] == "macroCard"]
        self.assertEqual(len(cards), 8, "Must place exactly 8 native MacroCard components")

        # Discarded controls check: separate dial, readout, standalone action, and bank switcher must not be present
        self.assertFalse(any(c["componentData"]["type"] == "macroNativeDial" for c in components))
        self.assertFalse(any(c["componentData"]["type"] == "macroNativeReadout" for c in components))
        self.assertFalse(any(c["componentData"]["type"] == "macroNativeAction" for c in components))
        self.assertFalse(any("bank" in c["componentData"]["name"].lower() for c in components))

        expected_positions = [
            (8, 382), (166, 382), (324, 382), (482, 382),
            (646, 382), (804, 382), (962, 382), (1120, 382),
        ]
        sx, sy, sw, sh = (8, 72, 1264, 290)

        for i, card in enumerate(cards):
            expected_x, expected_y = expected_positions[i]
            bounds = [int(v) for v in card["bounds"]["bounds"].split()]
            self.assertEqual(bounds, [expected_x, expected_y, 152, 238], f"Card {i+1} bounds must match 152x238 bottom-row layout")

            # Must not overlap upper SpectrumStrip {8, 72, 1264, 290}
            cx, cy, cw, ch = bounds
            overlaps_spec = (cx < sx + sw and sx < cx + cw and cy < sy + sh and sy < cy + ch)
            self.assertFalse(overlaps_spec, f"Card {i+1} must not overlap upper spectrum strip")

            # Verify isolated parameter bindings: Data -> macro.N, Target -> separate action handle
            remap = {m["key"]: m["value"] for m in card["handle remapping"]["map"]}
            param_key = f"macro.{i+1}"
            param_idx = self.ctx.param_index(param_key)
            self.assertEqual(remap["Data"], f"Parameter {param_idx}")
            self.assertEqual(remap["Caption"], f"WithName[Macro {i+1}]/Parameter {param_idx}")
            self.assertEqual(remap["Target"], f"FromGesture/Parameter {native_base_index(self.ctx.param_count) + MACRO_TARGET + i}")
            self.assertNotEqual(remap["Target"], remap["Data"], "Target button must not write to macro parameter")

    def test_screen_macros_idempotent_and_non_duplicating(self):
        """Verifies that running apply multiple times does not duplicate components."""
        screen_macros_99.apply(self.tui, self.qlinks, self.ctx)
        tab = self.ctx.tab("Macros")
        count_first = len(tab["componentDefinition"]["componentsData"])
        defs_first = len(self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"])

        # Run apply again
        screen_macros_99.apply(self.tui, self.qlinks, self.ctx)
        count_second = len(tab["componentDefinition"]["componentsData"])
        defs_second = len(self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"])

        self.assertEqual(count_first, count_second, "Repeated apply must not add duplicate components")
        self.assertEqual(defs_first, defs_second, "Repeated apply must not add duplicate definitions")

    def test_marker_ordering_at_tail(self):
        """Verifies that Overlay Marker and Instance components remain at the tail."""
        screen_macros_99.apply(self.tui, self.qlinks, self.ctx)
        tab = self.ctx.tab("Macros")
        components = tab["componentDefinition"]["componentsData"]
        tail_names = [c["componentData"]["name"] for c in components[-3:]]
        self.assertEqual(sum(n.startswith("Overlay Marker") for n in tail_names), 2)
        self.assertEqual(sum(n.startswith("Overlay Instance") for n in tail_names), 1)
        self.assertFalse(any(c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))
                             for c in components[:-3]))


if __name__ == "__main__":
    unittest.main()
