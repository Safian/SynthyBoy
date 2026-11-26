# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted contract test for Play LFO SMOOTH ActiveText and Level dial integration."""

import os
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
for p in (REPO_ROOT, REPO_ROOT / "scripts", HERE):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

from PIL import Image

SDK = Path(os.environ.get("MPC_SDK") or Path("/Users/szabi/.local/share/local-worker/ext/mpc-sdk") if Path("/Users/szabi/.local/share/local-worker/ext/mpc-sdk").is_dir() else Path.home() / ".local/share/local-worker/ext/mpc-sdk")


@unittest.skipUnless((SDK / "tools/gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class TestPlayNativeSmoothActiveText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ["MPC_SDK"] = str(SDK)
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        cls.skins = NoFallbackTests.skins
        cls.tui = NoFallbackTests.tui
        cls.play = next(t for t in cls.tui["pageData"]["tabs"] if t["tabName"] == "Play")
        cls.items = {c["componentData"]["name"]: c for c in cls.play["componentDefinition"]["componentsData"]}
        cls.defs = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_foreign_switch_and_toggle_definition_completely_removed(self):
        """Play LFO Smooth Switch and compactNativeSmoothToggle must be completely absent."""
        self.assertNotIn("Play LFO Smooth Switch", self.items,
                         "Former foreign slider-pill switch must be removed from Play tab")
        for name, comp in self.items.items():
            comp_type = comp["componentData"].get("type", "")
            self.assertNotEqual(comp_type, "compactNativeSmoothToggle",
                                f"Component {name} must not use compactNativeSmoothToggle")
        self.assertNotIn("compactNativeSmoothToggle", self.defs,
                         "compactNativeSmoothToggle definition must be removed from skin")

    def test_smooth_dual_state_pair_presence_and_bounds(self):
        """Play LFO Smooth (active) and Off (passive) must exist at exactly (1064, 546, 196, 72)."""
        self.assertIn("Play LFO Smooth", self.items)
        self.assertIn("Play LFO Smooth Off", self.items)

        active = self.items["Play LFO Smooth"]
        passive = self.items["Play LFO Smooth Off"]

        self.assertEqual(active["bounds"]["bounds"], "1064 546 196 72")
        self.assertEqual(passive["bounds"]["bounds"], "1064 546 196 72")

        # Focus: active allows HW focus, passive rejects it (OFF protects against time edit)
        self.assertEqual(active["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(passive["bounds"]["acceptsHWFocus"], "No")

        # Gating: smooth switch parameter (2699) and standard LFO parameter (2942)
        self.assertIn("IndexedEnabling/1/2/Parameter 2699", active["bounds"]["additionalInvalidatingHandles"])
        self.assertIn("IndexedEnabling/0/2/Parameter 2699", passive["bounds"]["additionalInvalidatingHandles"])
        self.assertIn("IndexedEnabling/1/2/Parameter 2942", active["bounds"]["additionalInvalidatingHandles"])
        self.assertIn("IndexedEnabling/1/2/Parameter 2942", passive["bounds"]["additionalInvalidatingHandles"])

        # Handle remapping
        active_remap = {m["key"]: m["value"] for m in active["handle remapping"]["map"]}
        passive_remap = {m["key"]: m["value"] for m in passive["handle remapping"]["map"]}

        self.assertEqual(active_remap["Data"], "Parameter 2698")
        self.assertEqual(active_remap["Caption"], "WithName[SMOOTH]/Parameter 2698")
        self.assertEqual(active_remap["State"], "Parameter 2699")
        self.assertEqual(active_remap["Active"], "Parameter 2699")

        self.assertEqual(passive_remap["Data"], "Parameter 2698")
        self.assertEqual(passive_remap["Caption"], "WithName[SMOOTH]/Parameter 2698")
        self.assertEqual(passive_remap["State"], "Parameter 2699")
        self.assertEqual(passive_remap["Active"], "Parameter 2699")

    def test_smooth_component_definitions_and_children(self):
        """compactNativeSmoothWithIndicator and compactNativeSmoothReadout definitions must have proper children and disjoint hitbox targets."""
        self.assertIn("compactNativeSmoothWithIndicator", self.defs)
        self.assertIn("compactNativeSmoothReadout", self.defs)

        active_def = self.defs["compactNativeSmoothWithIndicator"]
        passive_def = self.defs["compactNativeSmoothReadout"]

        act_children = {c["componentData"]["name"]: c for c in active_def["componentsData"]}
        pass_children = {c["componentData"]["name"]: c for c in passive_def["componentsData"]}

        self.assertEqual(set(act_children.keys()), {"focus", "knob", "name", "value"})
        self.assertEqual(set(pass_children.keys()), {"focus", "knob", "name", "value"})

        # Knob: active has vertical drag; passive has dragOrientation None and ignoreMousePresses
        act_knob = act_children["knob"]["componentData"]
        self.assertEqual(act_knob["type"], "Knob")
        self.assertEqual(act_knob["data"]["knobType"], "FilmStrip")
        self.assertEqual(act_knob["data"]["filmStrip"], "play_sub_level_60.png")
        self.assertEqual(act_knob["data"]["dragOrientation"], "Vertical")
        self.assertEqual(act_children["knob"]["bounds"]["bounds"], "2 6 60 60")

        pass_knob = pass_children["knob"]["componentData"]
        self.assertEqual(pass_knob["type"], "Knob")
        self.assertEqual(pass_knob["data"]["knobType"], "FilmStrip")
        self.assertEqual(pass_knob["data"]["filmStrip"], "play_sub_level_60_inactive.png")
        self.assertEqual(pass_knob["data"]["dragOrientation"], "None")
        self.assertTrue(pass_knob["data"].get("ignoreMousePresses"), "Passive knob must ignore mouse presses")
        self.assertEqual(pass_children["knob"]["bounds"]["bounds"], "2 6 60 60")
        self.assertEqual(pass_children["knob"]["bounds"]["acceptsHWFocus"], "No")

        # Name: ActiveText Button on both states (enables toggling ON->OFF and OFF->ON)
        for children_dict in (act_children, pass_children):
            name_comp = children_dict["name"]["componentData"]
            self.assertEqual(name_comp["type"], "Button")
            self.assertEqual(name_comp["data"]["buttonId"], 1)
            self.assertEqual(name_comp["data"]["numButtonsInGroup"], 1)
            self.assertEqual(name_comp["data"]["handleName"], "Active")
            self.assertEqual(name_comp["data"]["onImage"], "active_text_on.png")
            self.assertEqual(name_comp["data"]["offImage"], "active_text_off.png")
            self.assertEqual(name_comp["data"]["gestureBehaviour"], "Instant")
            self.assertEqual(children_dict["name"]["bounds"]["bounds"], "66 8 60 56")

        # Value label: same Data handle and bounds on both states
        for children_dict in (act_children, pass_children):
            val_comp = children_dict["value"]["componentData"]
            self.assertEqual(val_comp["type"], "Label")
            self.assertEqual(val_comp["data"]["type"], "Value")
            self.assertEqual(val_comp["data"]["handleName"], "Data")
            self.assertEqual(children_dict["value"]["bounds"]["bounds"], "130 22 62 28")

        # Value colour distinction
        from components.style import argb as _style_argb
        self.assertEqual(act_children["value"]["componentData"]["data"]["textStyle"]["colour"],
                         _style_argb("smooth.value.on"))
        self.assertEqual(pass_children["value"]["componentData"]["data"]["textStyle"]["colour"],
                         _style_argb("smooth.value.off"))

        # Hit target verification: Button is >= 56 px in both directions
        b_x, b_y, b_w, b_h = [int(v) for v in act_children["name"]["bounds"]["bounds"].split()]
        self.assertGreaterEqual(b_w, 56, "Caption button width must be >= 56 px")
        self.assertGreaterEqual(b_h, 56, "Caption button height must be >= 56 px")

        # Target disjointness: Knob [2, 6, 60, 60], Button [66, 8, 60, 56], Value [130, 22, 62, 28]
        k_x, k_y, k_w, k_h = [int(v) for v in act_children["knob"]["bounds"]["bounds"].split()]
        v_x, v_y, v_w, v_h = [int(v) for v in act_children["value"]["bounds"]["bounds"].split()]

        # Knob and Button disjoint
        self.assertLessEqual(k_x + k_w, b_x, "Knob target must not overlap caption button")
        # Button and Value disjoint (Value cannot intercept clicks)
        self.assertLessEqual(b_x + b_w, v_x, "Caption button must not overlap value label (no interception)")

    def test_active_text_assets_rendering_and_tokens(self):
        """ActiveText image assets must exist in skins directory and match design token colors."""
        on_path = self.skins / "active_text_on.png"
        off_path = self.skins / "active_text_off.png"

        self.assertTrue(on_path.is_file(), f"Missing {on_path}")
        self.assertTrue(off_path.is_file(), f"Missing {off_path}")

        im_on = Image.open(on_path).convert("RGBA")
        im_off = Image.open(off_path).convert("RGBA")

        self.assertEqual(im_on.size, (60, 56))
        self.assertEqual(im_off.size, (60, 56))

        # Check color tokens
        # ON text contains token active_text.on (#2ee6f0 -> RGB 46, 230, 240)
        on_pixels = list(im_on.getdata())
        has_cyan = any(p[0] == 46 and p[1] == 230 and p[2] == 240 and p[3] > 200 for p in on_pixels)
        self.assertTrue(has_cyan, "active_text_on.png must contain #2ee6f0 active cyan pixels")

        # OFF text contains token active_text.off (#aab2d8 -> RGB 170, 178, 216: the caption colour, readable on the canvas)
        off_pixels = list(im_off.getdata())
        has_gray = any(p[0] == 170 and p[1] == 178 and p[2] == 216 and p[3] > 200 for p in off_pixels)
        self.assertTrue(has_gray, "active_text_off.png must contain #aab2d8 inactive pixels")

    def test_smooth_state_transitions_and_lfo_switching_simulation(self):
        """Verify parameter writing and state visibility across ON->OFF->ON and Standard/RND LFO switches."""
        from components.native.level.level import is_smooth_applicable, lfo_smooth_param_key

        active = self.items["Play LFO Smooth"]
        passive = self.items["Play LFO Smooth Off"]

        def is_comp_visible(comp, params):
            for h in comp["bounds"].get("additionalInvalidatingHandles", []):
                parts = h.split("/")
                if parts[0] == "IndexedEnabling":
                    target = int(parts[1])
                    handle = parts[3]
                    if params.get(handle, 0) != target:
                        return False
            return True

        # Scenario 1: Standard LFO 1 with Smooth ON (param 2942=1, param 2699=1)
        state_on = {"Parameter 2942": 1, "Parameter 2699": 1}
        self.assertTrue(is_comp_visible(active, state_on), "Active Smooth must be visible when standard LFO smooth is ON")
        self.assertFalse(is_comp_visible(passive, state_on), "Passive Smooth must be hidden when standard LFO smooth is ON")

        # Scenario 2: Tap caption button -> switches to Smooth OFF (param 2699=0)
        state_off = {"Parameter 2942": 1, "Parameter 2699": 0}
        self.assertFalse(is_comp_visible(active, state_off), "Active Smooth must be hidden when standard LFO smooth is OFF")
        self.assertTrue(is_comp_visible(passive, state_off), "Passive Smooth must be visible when standard LFO smooth is OFF")
        # While OFF, passive component rejects HW focus and knob has dragOrientation None
        self.assertEqual(passive["bounds"]["acceptsHWFocus"], "No")
        pass_def = self.defs["compactNativeSmoothReadout"]
        pass_knob = next(c for c in pass_def["componentsData"] if c["componentData"]["name"] == "knob")
        self.assertEqual(pass_knob["componentData"]["data"]["dragOrientation"], "None")

        # Scenario 3: Tap caption button again -> restores Smooth ON (param 2699=1)
        self.assertTrue(is_comp_visible(active, state_on))
        self.assertFalse(is_comp_visible(passive, state_on))

        # Scenario 4: Switch LFO instance to RND 1 (LFO 11): standard_lfo becomes 0
        state_rnd = {"Parameter 2942": 0, "Parameter 2699": 1}
        self.assertFalse(is_comp_visible(active, state_rnd), "Active Smooth must be hidden for Random LFO")
        self.assertFalse(is_comp_visible(passive, state_rnd), "Passive Smooth must be hidden for Random LFO")
        self.assertFalse(is_smooth_applicable("RND 1"))
        self.assertIsNone(lfo_smooth_param_key("RND 1"))

        # Scenario 5: Switch back to Standard LFO 2 (smooth applicable)
        self.assertTrue(is_smooth_applicable("LFO 2"))
        self.assertEqual(lfo_smooth_param_key(2), "lfo2.smooth")


if __name__ == "__main__":
    unittest.main()
