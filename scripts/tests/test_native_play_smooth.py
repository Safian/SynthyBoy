# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import copy
import tempfile
import unittest
from pathlib import Path

from components.native.level.level import (
    PLAY_SMOOTH_RECTS,
    build_native_smooth_dial_with_indicator_definition,
    build_native_smooth_dual_state_pair,
    build_standalone_smooth,
    clone,
    instantiate_standalone_smooth_for_tab,
    is_smooth_applicable,
    lfo_smooth_param_key,
    smooth_binding,
)


class TestNativePlaySmooth(unittest.TestCase):
    def setUp(self):
        self.template = {
            "componentData": {"type": "toggleIcon", "name": "Template Toggle"},
            "parameterIndex": 0,
            "handle remapping": {
                "map": [
                    {"key": "Value", "value": "Parameter 0"},
                    {"from": "Enable", "value": "Parameter 0"},
                    {"key": "Caption", "value": "OldCaption"},
                ]
            },
            "bounds": {
                "acceptsHWFocus": "Yes",
                "showWhenDataModelInvalid": "Hide",
                "whenVisible": "WhenValid",
                "bounds": "0 0 56 56",
                "additionalInvalidatingHandles": [
                    "IndexedEnabling/1/2/Parameter 42",
                    "KeepHandle",
                ],
            },
        }

    def test_is_smooth_applicable(self):
        # Standard LFOs 1..10 support smoothing
        for i in range(1, 11):
            self.assertTrue(is_smooth_applicable(i))
            self.assertTrue(is_smooth_applicable("LFO %d" % i))
            self.assertTrue(is_smooth_applicable("lfo%d" % i))

        # Random LFOs do NOT support smoothing
        for i in range(11, 15):
            self.assertFalse(is_smooth_applicable(i))
        for r in ("R1", "R2", "R3", "R4", "RND 1", "rnd2", "Random 1"):
            self.assertFalse(is_smooth_applicable(r))

    def test_lfo_smooth_param_key(self):
        self.assertEqual(lfo_smooth_param_key(1), "lfo1.smooth")
        self.assertEqual(lfo_smooth_param_key(10), "lfo10.smooth")
        self.assertEqual(lfo_smooth_param_key("LFO 3"), "lfo3.smooth")
        self.assertIsNone(lfo_smooth_param_key("RND 1"))
        self.assertIsNone(lfo_smooth_param_key(12))

    def test_compact_smooth_binding(self):
        proxy_indices = [100, 101, 102, 103, 104, 105, 106, 107]
        bound = smooth_binding(self.template, clone, proxy_indices, (10, 20, 56, 56))
        self.assertEqual(bound["componentData"]["name"], "Smooth")
        val_map = next(m for m in bound["handle remapping"]["map"] if m.get("key") == "Value")
        self.assertEqual(val_map["value"], "Parameter 104")
        self.assertEqual(bound["bounds"]["bounds"], "10 20 56 56")

    def test_build_standalone_smooth_standard_lfo(self):
        comp = build_standalone_smooth(self.template, 1, 1508, (500, 600, 180, 56))
        self.assertIsNotNone(comp)
        self.assertEqual(comp["componentData"]["name"], "LFO 1 Smooth Native")
        self.assertEqual(comp["bounds"]["bounds"], "500 600 180 56")
        self.assertEqual(comp["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(comp["bounds"]["showWhenDataModelInvalid"], "Show")
        self.assertEqual(comp["bounds"]["whenVisible"], "Always")

        # Caption remapped
        caption_map = next(m for m in comp["handle remapping"]["map"] if m.get("key") == "Caption")
        self.assertEqual(caption_map["value"], "WithName[Smooth]/Parameter 1508")

        # Enable mapping stripped
        self.assertFalse(any(m.get("from") == "Enable" or m.get("key") == "Enable" for m in comp["handle remapping"]["map"]))

    def test_build_standalone_smooth_inapplicable_random_lfo(self):
        # Smoothing does not exist on random LFOs; must return None
        self.assertIsNone(build_standalone_smooth(self.template, 11, 2000, (0, 0, 56, 56)))
        self.assertIsNone(build_standalone_smooth(self.template, "RND 1", 2000, (0, 0, 56, 56)))

    def test_build_standalone_smooth_overlay_precedence(self):
        comp = build_standalone_smooth(self.template, 2, 1521, (10, 20, 56, 56), overlay_param=800, last_visible_frame=18)
        handles = comp["bounds"]["additionalInvalidatingHandles"]
        self.assertIn("RangedEnabling/0/18/33/Parameter 800", handles)
        self.assertIn("KeepHandle", handles)

    def test_instantiate_standalone_smooth_for_tab(self):
        tab = {"componentDefinition": {"componentsData": []}}
        # Standard LFO 1: instantiates and appends
        comp = instantiate_standalone_smooth_for_tab(tab, 1, 1508, (10, 20, 180, 56), template=self.template)
        self.assertIsNotNone(comp)
        self.assertEqual(len(tab["componentDefinition"]["componentsData"]), 1)

        # Random LFO 1: skipped
        comp_rnd = instantiate_standalone_smooth_for_tab(tab, "RND 1", 1508, (10, 20, 180, 56), template=self.template)
        self.assertIsNone(comp_rnd)
        self.assertEqual(len(tab["componentDefinition"]["componentsData"]), 1)

    def test_build_native_smooth_dial_with_indicator_definition(self):
        comp_def = build_native_smooth_dial_with_indicator_definition(active=True)
        self.assertEqual(comp_def["version"], 2)
        children = comp_def["componentsData"]
        knob = next(c["componentData"]["data"] for c in children if c["componentData"]["type"] == "Knob")
        self.assertEqual(knob["knobType"], "FilmStrip")
        self.assertEqual(knob["handleName"], "Data")
        names = [c["componentData"]["name"] for c in children]
        self.assertIn("knob", names)
        self.assertNotIn("led", names)
        self.assertIn("name", names)
        self.assertIn("value", names)
        self.assertIn("focus", names)

        led_entry = next(c for c in children if c["componentData"]["name"] == "name")
        self.assertEqual(led_entry["componentData"]["type"], "Button")
        self.assertEqual(led_entry["componentData"]["data"]["handleName"], "State")
        self.assertEqual(led_entry["componentData"]["data"]["onImage"], "smooth_caption_on.png")
        self.assertEqual(comp_def["disableCoarseDataWheel"], False)

        # Passive / inactive definition
        comp_off = build_native_smooth_dial_with_indicator_definition(active=False)
        off_children = {c["componentData"]["name"]: c for c in comp_off["componentsData"]}
        passive_knob = off_children["knob"]["componentData"]["data"]
        self.assertEqual(passive_knob["knobType"], "FilmStrip")
        self.assertTrue(passive_knob["filmStrip"].endswith("_inactive.png"))
        self.assertEqual(passive_knob["dragOrientation"], "None")
        self.assertTrue(off_children["knob"]["componentData"]["data"].get("ignoreMousePresses"))
        self.assertEqual(off_children["knob"]["bounds"]["acceptsHWFocus"], "No")
        self.assertEqual(off_children["focus"]["bounds"]["acceptsHWFocus"], "No")
        self.assertEqual(comp_off["disableCoarseDataWheel"], True)
        # Value text colour is dimmed
        self.assertIn("colour", off_children["value"]["componentData"]["data"]["textStyle"])
        # Caption is still clickable Button with handle
        self.assertEqual(off_children["name"]["componentData"]["type"], "Button")

    def test_play_smooth_rects_hitbox_and_disjoint_targets(self):
        from components.native.level.level import PLAY_SMOOTH_RECTS
        f_x, f_y, f_w, f_h = PLAY_SMOOTH_RECTS["focus"]
        k_x, k_y, k_w, k_h = PLAY_SMOOTH_RECTS["knob"]
        n_x, n_y, n_w, n_h = PLAY_SMOOTH_RECTS["name"]
        v_x, v_y, v_w, v_h = PLAY_SMOOTH_RECTS["value"]

        # Hitbox rule: both width and height >= 56 px
        self.assertGreaterEqual(n_w, 56, f"Caption button width ({n_w}) must be >= 56 px")
        self.assertGreaterEqual(n_h, 56, f"Caption button height ({n_h}) must be >= 56 px")

        # Disjoint target rule: knob and caption target do NOT overlap
        knob_rect = (k_x, k_y, k_x + k_w, k_y + k_h)
        caption_rect = (n_x, n_y, n_x + n_w, n_y + n_h)
        value_rect = (v_x, v_y, v_x + v_w, v_y + v_h)

        def overlap(r1, r2):
            return not (r1[2] <= r2[0] or r2[2] <= r1[0] or r1[3] <= r2[1] or r2[3] <= r1[1])

        self.assertFalse(overlap(knob_rect, caption_rect), "Knob and caption button targets must be disjoint")
        self.assertFalse(overlap(caption_rect, value_rect), "Caption button and value label targets must be disjoint")
        self.assertFalse(overlap(knob_rect, value_rect), "Knob and value label targets must be disjoint")

        # Entire layout fits within parent cell
        self.assertLessEqual(n_x + n_w, f_w)
        self.assertLessEqual(v_x + v_w, f_w)
        self.assertLessEqual(n_y + n_h, f_h)
        self.assertLessEqual(v_y + v_h, f_h)

    def test_build_native_smooth_dual_state_pair(self):
        time_idx, switch_idx = 2050, 2051
        rect = (1064, 546, 196, 72)
        active, passive = build_native_smooth_dual_state_pair(time_idx, switch_idx, rect)

        self.assertEqual(active["bounds"]["bounds"], "1064 546 196 72")
        self.assertEqual(passive["bounds"]["bounds"], "1064 546 196 72")
        self.assertEqual(active["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(passive["bounds"]["acceptsHWFocus"], "No")

        self.assertIn(f"IndexedEnabling/1/2/Parameter {switch_idx}", active["bounds"]["additionalInvalidatingHandles"])
        self.assertIn(f"IndexedEnabling/0/2/Parameter {switch_idx}", passive["bounds"]["additionalInvalidatingHandles"])

        active_remap = {m["key"]: m["value"] for m in active["handle remapping"]["map"]}
        self.assertEqual(active_remap["Data"], f"Parameter {time_idx}")
        self.assertEqual(active_remap["Caption"], f"WithName[SMOOTH]/Parameter {time_idx}")
        self.assertEqual(active_remap["State"], f"Parameter {switch_idx}")
        self.assertEqual(active_remap["Active"], f"Parameter {switch_idx}")


    def test_level_exports_and_smooth_variant_api(self):
        import components.native.level.level as level_mod

        smooth_symbols = [
            "PLAY_SMOOTH_RECTS",
            "clone",
            "is_smooth_applicable",
            "lfo_smooth_param_key",
            "smooth_binding",
            "build_standalone_smooth",
            "instantiate_standalone_smooth_for_tab",
            "build_native_smooth_dial_with_indicator_definition",
            "build_native_smooth_dual_state_pair",
        ]
        for sym in smooth_symbols:
            self.assertTrue(hasattr(level_mod, sym), f"level_mod missing {sym}")
            self.assertIn(sym, level_mod.__all__)

        level_symbols = [
            "SIZE",
            "FRAMES",
            "COMPACT_48_RECTS",
            "DEFAULT_DIAL_RECTS",
            "_value_label",
            "default_level_template",
            "write_strip",
            "synthyboy_dial_data",
            "sub_dial_definition",
            "level_definition",
            "compact_level_definition",
            "DEFAULT_RECTS",
            "compact_dial_definition",
        ]
        for sym in level_symbols:
            self.assertTrue(hasattr(level_mod, sym), f"level_mod missing {sym}")
            self.assertIn(sym, level_mod.__all__)



if __name__ == "__main__":
    unittest.main()
