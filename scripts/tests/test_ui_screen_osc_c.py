#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Targeted unit tests for the Osc C screen native integration and autoload extra."""

import json
import os
from pathlib import Path
import tempfile
import unittest

from native_ui_params import (
    OSC_ABC_SETTINGS,
    OSC_ABC_SETTINGS_FIELDS,
    OSC_POWER,
    OSC_ROUTING_BY_SOURCE,
    OSC_SOURCE,
    native_base_index,
)
from test_mpc_skin_extras import (
    PARAMS_JSON,
    REAL_EXTRAS,
    ROOT,
    generate_skins,
    run_apply,
)


class OscCScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp_dir = tempfile.TemporaryDirectory()
        cls.skins = generate_skins(Path(cls.tmp_dir.name))
        code, err = run_apply(cls.skins, REAL_EXTRAS)
        assert code == 0, f"run_apply failed with error: {err}"
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))
        cls.defs = {
            d["key"]: d["value"]
            for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        }
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.param_count = len(cls.params)
        cls.base = native_base_index(cls.param_count)

    @classmethod
    def tearDownClass(cls):
        cls.tmp_dir.cleanup()

    def test_autoload_discovery_and_order(self):
        """99_ui_screen_osc_c.py is discovered in mpc-skin-extras and runs after 98_osc_surface."""
        files = sorted(p.name for p in REAL_EXTRAS.glob("*.py"))
        self.assertIn("99_ui_screen_osc_c.py", files)
        idx_surface = files.index("98_osc_surface.py")
        idx_oscc = files.index("99_ui_screen_osc_c.py")
        self.assertGreater(idx_oscc, idx_surface, "99_ui_screen_osc_c must execute after 98_osc_surface")

    def test_osc_c_tab_structure(self):
        """Osc C tab exists, has empty qlinkBoundsData and retains only native surface and markers."""
        tab = next((t for t in self.tui["pageData"]["tabs"] if t.get("tabName") == "Osc C"), None)
        self.assertIsNotNone(tab, "Tab 'Osc C' must exist in TUI.json")
        self.assertEqual(tab["qlinkBoundsData"], [], "qlinkBoundsData must be [] to eliminate host Q-Link number frames")

        comp_names = [c["componentData"]["name"] for c in tab["componentDefinition"]["componentsData"]]
        self.assertIn("Osc Native Surface", comp_names, "Tab must contain 'Osc Native Surface'")
        self.assertIn("Overlay Marker oscc left", comp_names)
        self.assertIn("Overlay Marker oscc right", comp_names)
        self.assertIn("Overlay Instance oscc", comp_names)

        # No old fallback controls should remain
        for name in comp_names:
            self.assertTrue(
                name.startswith(("Osc Native", "Overlay Marker", "Overlay Background Sentinel", "Overlay Instance", "Native Modulation", "Host Background")),
                f"Unexpected fallback component on Osc C tab: {name}",
            )

    def test_osc_c_native_surface_components(self):
        """oscNativeSurface contains selector, routing, power, settings, and detail."""
        surface = self.defs.get("oscNativeSurface")
        self.assertIsNotNone(surface, "oscNativeSurface definition must exist")

        kids = surface["componentsData"]
        kid_names = {c["componentData"]["name"]: c for c in kids}

        self.assertNotIn("Osc Native Source Tabs", kid_names)

        self.assertIn("Osc Native Pan", kid_names)
        self.assertEqual(kid_names["Osc Native Pan"]["bounds"]["bounds"], "864 580 202 48")

        self.assertIn("Osc Native Level", kid_names)
        self.assertEqual(kid_names["Osc Native Level"]["bounds"]["bounds"], "1070 580 202 48")

        self.assertIn("Osc Native Routing", kid_names)
        self.assertEqual(kid_names["Osc Native Routing"]["bounds"]["bounds"], "1122 37 150 48")

        self.assertIn("Osc Native Power", kid_names)
        self.assertEqual(kid_names["Osc Native Power"]["bounds"]["bounds"], "12 8 106 164")

        self.assertIn("Osc Native Settings", kid_names)
        self.assertEqual(kid_names["Osc Native Settings"]["bounds"]["bounds"], "0 0 1280 628")

        self.assertIn("Osc Native Detail", kid_names)
        self.assertEqual(kid_names["Osc Native Detail"]["bounds"]["bounds"], "0 0 1280 628")

    def test_osc_c_instance_bindings(self):
        """C instance bindings specifically route to osc3 parameters and source index 3."""
        # 1. Power binding: button 3 in oscNativePowerLayer is Parameter {base + OSC_POWER + 3}
        power_layer = self.defs.get("oscNativePowerLayer")
        self.assertIsNotNone(power_layer)
        power_c = next((c for c in power_layer["componentsData"] if c["componentData"]["name"] == "Osc Native Osc C Power"), None)
        self.assertIsNotNone(power_c, "Osc C power button must exist in power layer")
        remap_power = power_c["handle remapping"]["map"][0]["value"]
        expected_power = f"Parameter {self.base + OSC_POWER + 3}"
        self.assertEqual(remap_power, expected_power, "Power must bind to osc3.on parameter handle")
        self.assertIn(f"IndexedEnabling/3/5/Parameter {self.base + OSC_SOURCE}", power_c["bounds"]["additionalInvalidatingHandles"])

        # 2. Routing binding: control for Osc C in oscNativeRoutingLayer is Parameter {base + OSC_ROUTING_BY_SOURCE + 3}
        routing_layer = self.defs.get("oscNativeRoutingLayer")
        self.assertIsNotNone(routing_layer)
        route_c = next((c for c in routing_layer["componentsData"] if "Osc C" in c["componentData"]["name"]), None)
        self.assertIsNotNone(route_c, "Osc C routing control must exist in routing layer")
        remap_route = route_c["handle remapping"]["map"][0]["value"]
        expected_route = f"Parameter {self.base + OSC_ROUTING_BY_SOURCE + 3}"
        self.assertEqual(remap_route, expected_route, "Routing must bind to osc3.destination parameter handle")
        self.assertIn(f"IndexedEnabling/3/5/Parameter {self.base + OSC_SOURCE}", route_c["bounds"]["additionalInvalidatingHandles"])

        # 2b. Pan binding: control for Osc C in oscNativePanLayer is Parameter 61 (osc3.pan)
        pan_layer = self.defs.get("oscNativePanLayer")
        self.assertIsNotNone(pan_layer)
        pan_c = next((c for c in pan_layer["componentsData"] if "Osc C" in c["componentData"]["name"]), None)
        self.assertIsNotNone(pan_c, "Osc C pan control must exist in pan layer")
        self.assertEqual(pan_c["handle remapping"]["map"][0]["value"], "Parameter 61")
        self.assertEqual(pan_c["handle remapping"]["map"][1]["value"], "WithName[PAN]/Parameter 61")
        self.assertIn(f"IndexedEnabling/3/5/Parameter {self.base + OSC_SOURCE}", pan_c["bounds"]["additionalInvalidatingHandles"])

        # 2c. Level binding: control for Osc C in oscNativeLevelLayer is Parameter 60 (osc3.level)
        level_layer = self.defs.get("oscNativeLevelLayer")
        self.assertIsNotNone(level_layer)
        level_c = next((c for c in level_layer["componentsData"] if "Osc C" in c["componentData"]["name"]), None)
        self.assertIsNotNone(level_c, "Osc C level control must exist in level layer")
        self.assertEqual(level_c["handle remapping"]["map"][0]["value"], "Parameter 60")
        self.assertEqual(level_c["handle remapping"]["map"][1]["value"], "WithName[LEVEL]/Parameter 60")
        self.assertIn(f"IndexedEnabling/3/5/Parameter {self.base + OSC_SOURCE}", level_c["bounds"]["additionalInvalidatingHandles"])

        # 3. Settings layer: Osc C ABC remapping
        settings_layer = self.defs.get("oscNativeSettingsLayer")
        self.assertIsNotNone(settings_layer)
        abc_c = next((c for c in settings_layer["componentsData"] if c["componentData"]["name"] == "Osc C"), None)
        self.assertIsNotNone(abc_c, "Osc C entry must exist in settings layer")
        self.assertIn(f"IndexedEnabling/3/5/Parameter {self.base + OSC_SOURCE}", abc_c["bounds"]["additionalInvalidatingHandles"])

        abc_remap = {m["key"]: m["value"] for m in abc_c["handle remapping"]["map"]}
        expected_abc_base = self.base + OSC_ABC_SETTINGS + 2 * OSC_ABC_SETTINGS_FIELDS
        self.assertEqual(abc_remap["OscAbc0"], f"Parameter {expected_abc_base}", "OscAbc0 must point to Osc C ABC base parameter")

    def test_osc_c_qlinks_row_mapping(self):
        """Hardware Q-Links for Osc C tab map to proxies Q 1..16 with Bank Direction 'Row'."""
        tab = next((t for t in self.tui["pageData"]["tabs"] if t.get("tabName") == "Osc C"), None)
        fn_idx = tab["fnKeyIndex"]
        sub_idx = tab["fnKeySubIndex"]

        entry = next(
            (e for e in self.qlinks["Screen Mode Q-Links"]["map"] if e["Tab"] == fn_idx + 1 and e["SubTab"] == sub_idx + 1),
            None,
        )
        self.assertIsNotNone(entry, "Q-Links.json must have an entry for Osc C")
        self.assertEqual(entry.get("Bank Direction"), "Row")

        q_dict = entry.get("Q-Links", {})
        self.assertEqual(len(q_dict), 16)
        # All 16 knobs must have positive parameter indices
        for i in range(1, 17):
            self.assertGreater(q_dict.get(f"Q-Link {i}", -1), 0)

    def test_touch_targets_meet_sdk_standard(self):
        """All primary native touch targets on Osc C satisfy the min 56 px touch requirement."""
        surface = self.defs.get("oscNativeSurface")
        kids = surface["componentsData"]
        for c in kids:
            b = [int(v) for v in c["bounds"]["bounds"].split()]
            w, h = b[2], b[3]
            name = c["componentData"]["name"]
            if name == "Osc Native Power":
                self.assertGreaterEqual(w, 56, f"{name} width {w} < 56 px")
                self.assertGreaterEqual(h, 56, f"{name} height {h} < 56 px")
            elif name.startswith("Osc Native Source ") or name in ("Osc Native Routing", "Osc Native Pan", "Osc Native Level"):
                self.assertGreaterEqual(w, 56, f"{name} width {w} < 56 px")
                self.assertGreaterEqual(h, 48, f"{name} height {h} < 48 px")


if __name__ == "__main__":
    unittest.main()
