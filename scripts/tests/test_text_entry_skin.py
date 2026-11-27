# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for scripts/mpc-skin-extras/93_text_entry.py."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "tests"))

text_entry_extra = importlib.import_module("mpc-skin-extras.93_text_entry")


class TestTextEntrySkin(unittest.TestCase):
    def test_hook_discovery_and_order(self):
        """Verifies 93_text_entry.py is discovered and ordered before 97_no_fallback and 99 screens."""
        extras_dir = ROOT / "scripts" / "mpc-skin-extras"
        all_extras = sorted([p.name for p in extras_dir.glob("*.py")])
        self.assertIn("93_text_entry.py", all_extras)
        idx_93 = all_extras.index("93_text_entry.py")
        idx_97 = all_extras.index("97_no_fallback.py")
        idx_99_m = all_extras.index("99_ui_screen_macros.py")
        idx_99_l = all_extras.index("99_ui_screen_lfo.py")
        self.assertLess(idx_93, idx_97)
        self.assertLess(idx_93, idx_99_m)
        self.assertLess(idx_93, idx_99_l)

    def test_text_edit_overlay_definition_structure(self):
        """Verifies TextEditOverlay definition matches factory / SDK ui-reference.md 6.12 schema."""
        defn = text_entry_extra.build_text_edit_overlay_definition()
        self.assertEqual(defn["version"], 2)
        self.assertEqual(defn["actions"], [])
        self.assertTrue(defn["ignoreMousePresses"])
        self.assertFalse(defn["disableCoarseDataWheel"])
        self.assertEqual(len(defn["componentsData"]), 1)

        comp = defn["componentsData"][0]
        self.assertEqual(comp["version"], 2)
        cdata = comp["componentData"]
        self.assertEqual(cdata["type"], "TextEditOverlay")
        self.assertEqual(cdata["name"], "Text Edit Overlay")

        data = cdata["data"]
        # Must have duplicate version keys (represented with \\x001 for apply loader)
        self.assertEqual(data["version"], 1)
        self.assertEqual(data.get("version\x001"), 1)
        self.assertEqual(data["withNumbers"], False)
        self.assertEqual(data["handleName"], "Data")
        self.assertEqual(len(data["actions"]), 1)
        self.assertEqual(data["actions"][0]["handler"], "Focus Lost")
        self.assertEqual(data["actions"][0]["onAction"], "Focus Lost")

        bounds = comp["bounds"]
        self.assertEqual(bounds["bounds"], "0 0 1280 800")
        self.assertEqual(bounds["acceptsHWFocus"], "No")
        self.assertEqual(bounds["boundsType"], "Absolute")

    def test_text_edit_actions(self):
        """Verifies action list generation for Double Click and Enter Pressed."""
        acts = text_entry_extra.text_edit_actions(handle_name="Caption", overlay_name="text edit overlay")
        self.assertEqual(len(acts), 2)
        self.assertEqual(acts[0]["onAction"], "Double Click")
        self.assertEqual(acts[0]["handler"], "Show Overlay")
        self.assertEqual(acts[0]["handleName"], "Caption")
        self.assertEqual(acts[0]["additionalData"], "text edit overlay")
        self.assertEqual(acts[1]["onAction"], "Enter Pressed")
        self.assertEqual(acts[1]["handler"], "Show Overlay")
        self.assertEqual(acts[1]["handleName"], "Caption")
        self.assertEqual(acts[1]["additionalData"], "text edit overlay")

    def test_wire_macros_and_lfo(self):
        """Verifies wire_macros and wire_lfo hooks register definitions and wire actions."""
        tui = {
            "pageData": {
                "componentDefinitions": {
                    "localComponentDefinitions": [
                        {"key": "macroCard", "value": {"version": 2, "actions": [], "componentsData": []}}
                    ]
                }
            }
        }
        text_entry_extra.wire_macros(tui)
        defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertIn("text edit overlay", defs)
        self.assertEqual(len(defs["macroCard"]["actions"]), 2)
        self.assertEqual(defs["macroCard"]["actions"][0]["handler"], "Show Overlay")
        self.assertEqual(defs["macroCard"]["actions"][0]["additionalData"], "text edit overlay")

        # Test wire_lfo idempotency
        text_entry_extra.wire_lfo(tui)
        defs_after = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertIn("text edit overlay", defs_after)


if __name__ == "__main__":
    unittest.main()
