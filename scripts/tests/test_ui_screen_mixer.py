#!/usr/bin/env python3
# SynthyBoy - Unit tests for the Mixer screen native controls, hooks, and PEEK surfaces.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
OVERLAY_LAYOUT = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
SDK_DOCS = Path("/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-knowledge/docs-official/mpc-sdk")

sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "tests"))

import test_mpc_skin_extras as base
from native_ui_params import (
    MIXER_CONTROL,
    MIXER_HOLD_OFFSET,
    MIXER_MUTE_OFFSET,
    MIXER_PEAK_OFFSET,
    MIXER_PEEK_TARGETS,
    MIXER_SOLO_OFFSET,
    mixer_peek_index,
)


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class UiScreenMixerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root, OVERLAY_LAYOUT)
        cls.code, cls.err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))
        cls.params_doc = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))
        cls.params = cls.params_doc["params"]

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_true_stereo_meter_contract(self):
        """The generated native tree binds all ten stereo readouts without duplicating mono/RMS."""
        from PIL import Image
        from native_ui_params import MIXER_STEREO_BASE, NATIVE_PARAMS
        self.assertEqual(self.code, 0, self.err)
        self.assertEqual((MIXER_STEREO_BASE, NATIVE_PARAMS), (900, 961))
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer"))
        entries = [e for e in tab["componentDefinition"]["componentsData"]
                   if e["componentData"]["type"] in ("mixerStripSource", "mixerStripBus", "mixerStripMaster")]
        self.assertEqual(len(entries), 10)
        base_offset = mixer_peek_index(len(self.params)) + MIXER_STEREO_BASE
        fields = ("PeakL", "PeakR", "HoldL", "HoldR", "ClipL", "ClipR")
        bound = set()
        for strip, entry in enumerate(entries):
            handles = {m["key"]: m["value"] for m in entry["handle remapping"]["map"]}
            for field, name in enumerate(fields):
                value = f"Parameter {base_offset + field * 10 + strip}"
                self.assertEqual(handles[name], value)
                bound.add(value)
            nested = next(c for c in definitions[entry["componentData"]["type"]]["componentsData"]
                          if c["componentData"]["name"] == "meter_block")
            self.assertEqual(nested["componentData"]["data"]["handleName"], "Data")
            self.assertEqual({m["key"]: m["value"] for m in nested["handle remapping"]["map"]}, {name: name for name in fields})
        self.assertEqual(len(bound), 60)
        for name, signal_height in (("mixerMeterBlockSource", 192), ("mixerMeterBlockBus", 408)):
            block = definitions[name]
            self.assertTrue(block["ignoreMousePresses"])
            self.assertEqual(block["actions"], [])
            self.assertEqual(len(block["componentsData"]), 4)
            for child in block["componentsData"]:
                model = child["componentData"]
                self.assertEqual(model["type"], "Meter")
                data = model["data"]
                self.assertEqual(data["rmsHandle"], "")
                self.assertEqual(data["rmsImage"], "")
                clip = model["name"].startswith("Clip")
                right = model["name"].endswith("R")
                self.assertEqual(child["bounds"]["bounds"], f"{10 if right else 0} {0 if clip else 6} 8 {4 if clip else signal_height}")
                self.assertEqual(child["bounds"]["acceptsHWFocus"], "No")
                self.assertEqual(data["peakHandle"], ("Clip" if clip else "Peak") + ("R" if right else "L"))
                for image in (data["inactiveImage"], data["peakImage"]):
                    with Image.open(self.skins / image) as artwork:
                        self.assertEqual(artwork.size, (8, 4 if clip else signal_height))
                if clip:
                    self.assertEqual(data["peakHoldHandle"], "")
                else:
                    self.assertEqual(data["peakHoldHandle"], "Hold" + ("R" if right else "L"))
                    self.assertEqual(data["peakHoldSize"], 2)

    def test_hook_discovery_and_execution_clean(self):
        """Verify apply-mpc-skin-extras runs with code 0 and no error."""
        self.assertEqual(self.code, 0, f"run_apply failed with stderr: {self.err}")
        self.assertEqual(self.err, "")

    def test_hook_order_and_no_duplication(self):
        """Verify 84_mixer and 99_ui_screen_mixer run in expected alphabetical sequence."""
        extras_dir = ROOT / "scripts" / "mpc-skin-extras"
        py_files = sorted(p.name for p in extras_dir.glob("*.py"))
        self.assertIn("84_mixer.py", py_files)
        self.assertIn("99_ui_screen_mixer.py", py_files)
        idx_84 = py_files.index("84_mixer.py")
        idx_99 = py_files.index("99_ui_screen_mixer.py")
        self.assertLess(idx_84, idx_99, "84_mixer.py must run before 99_ui_screen_mixer.py")

    def test_no_visible_qlink_numbers_or_highlight_frames(self):
        """Owner rule: no visible Q-Link frames or yellow number badges on Mixer tab."""
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer"))
        self.assertEqual(tab.get("qlinkBoundsData"), [], "Mixer tab must have qlinkBoundsData == []")

    def test_mixer_ten_channel_strips_structure(self):
        """Verify all 10 channel strip compound instances have proper variant definitions and handle mappings."""
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer"))
        components = {c["componentData"]["name"]: c for c in tab["componentDefinition"]["componentsData"]}
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

        handles = {p["key"]: f"Parameter {i}" for i, p in enumerate(self.params)}
        native_base = mixer_peek_index(len(self.params))

        strips = (
            ("SUB", "sub"), ("A", "osc1"), ("B", "osc2"), ("C", "osc3"), ("NOISE", "noise"),
            ("F1", "filter1"), ("F2", "filter2"), ("BUS 1", "bus1"), ("BUS 2", "bus2"), ("MAIN", "main")
        )

        for i, (caption, source) in enumerate(strips):
            strip_name = f"Mixer Native {caption} Strip"
            self.assertIn(strip_name, components)
            comp = components[strip_name]

            # Verify geometry: start_x=10, stride=127, y=10, w=115, h=550
            expected_bounds = f"{10 + 127 * i} 10 115 550"
            self.assertEqual(comp["bounds"]["bounds"], expected_bounds)

            remap = {h["key"]: h["value"] for h in comp["handle remapping"]["map"]}

            if i < 7:
                self.assertEqual(comp["componentData"]["type"], "mixerStripSource")
                self.assertEqual(remap.get("Solo"), f"Parameter {native_base + MIXER_CONTROL + MIXER_SOLO_OFFSET + i}")
                self.assertEqual(remap.get("Mute"), f"Parameter {native_base + MIXER_CONTROL + MIXER_MUTE_OFFSET + i}")
                self.assertEqual(remap.get("Power"), handles[f"{source}.on"])
                self.assertNotEqual(remap.get("Solo"), remap.get("Mute"))
                self.assertNotEqual(remap.get("Solo"), handles[f"{source}.on"])
                self.assertNotEqual(remap.get("Mute"), handles[f"{source}.on"])
                self.assertEqual(remap.get("Destination"), handles[f"{source}.destination"])
                self.assertEqual(remap.get("Level"), handles[f"{source}.level"])
                self.assertEqual(remap.get("Peak"), f"Parameter {native_base + MIXER_PEAK_OFFSET + i}")
                self.assertEqual(remap.get("Hold"), f"Parameter {native_base + MIXER_HOLD_OFFSET + i}")
                self.assertEqual(remap.get("Pan"), handles[f"{source}.pan"])
                self.assertEqual(remap.get("Send1"), handles[f"{source}.bus1_send"])
                self.assertEqual(remap.get("Send2"), handles[f"{source}.bus2_send"])
            elif i in (7, 8):
                self.assertEqual(comp["componentData"]["type"], "mixerStripBus")
                self.assertEqual(remap.get("Solo"), f"Parameter {native_base + MIXER_CONTROL + MIXER_SOLO_OFFSET + i}")
                self.assertEqual(remap.get("Mute"), f"Parameter {native_base + MIXER_CONTROL + MIXER_MUTE_OFFSET + i}")
                self.assertNotEqual(remap.get("Solo"), remap.get("Mute"))
                self.assertNotEqual(remap.get("Solo"), handles[f"{source}.destination"])
                self.assertEqual(remap.get("Destination"), handles[f"{source}.destination"])
                self.assertEqual(remap.get("Level"), handles[f"{source}.level"])
                self.assertEqual(remap.get("Peak"), f"Parameter {native_base + MIXER_PEAK_OFFSET + i}")
                self.assertEqual(remap.get("Hold"), f"Parameter {native_base + MIXER_HOLD_OFFSET + i}")
            else:
                self.assertEqual(comp["componentData"]["type"], "mixerStripMaster")
                self.assertEqual(remap.get("FxEnable"), handles["main.fx_bypass"])
                self.assertEqual(remap.get("Mute"), f"Parameter {native_base + MIXER_CONTROL + MIXER_MUTE_OFFSET + 9}")
                self.assertNotEqual(remap.get("Mute"), handles["main.fx_bypass"])
                self.assertEqual(remap.get("Level"), handles["master.volume"])
                self.assertEqual(remap.get("Peak"), f"Parameter {native_base + MIXER_PEAK_OFFSET + i}")
                self.assertEqual(remap.get("Hold"), f"Parameter {native_base + MIXER_HOLD_OFFSET + i}")

        # Verify shared definitions in componentDefinitions (including Header, SoloMutePair, MeterBlock - NONE pruned!)
        for def_name in ("mixerStripSource", "mixerStripBus", "mixerStripMaster",
                         "mixerStripHeaderSource", "mixerStripHeaderBus", "mixerStripHeaderMaster",
                         "soloMutePairSource", "soloMutePairBus", "soloMutePairMaster",
                         "mixerMeterBlockSource", "mixerMeterBlockBus",
                         "mixerDestination56", "mixerSliderLEVEL198", "mixerSliderLEVEL414",
                         "mixerSliderPAN56", "mixerSliderB156", "mixerSliderB256"):
            self.assertIn(def_name, definitions, f"{def_name} must remain in generated TUI.json (not pruned as dead)")
            self.assertIn("componentsData", definitions[def_name])

        # Verify nested handle mappings are present on all 10 strip instances
        for i, (caption, _) in enumerate(strips):
            comp = components[f"Mixer Native {caption} Strip"]
            remap = {h["key"]: h["value"] for h in comp["handle remapping"]["map"]}
            self.assertIn("Name", remap, f"{caption} strip must map Name for MixerStripHeader")
            self.assertIn("MuteVisual", remap, f"{caption} strip must map MuteVisual")
            if i < 9:
                self.assertIn("SoloVisual", remap, f"{caption} strip must map SoloVisual")
            else:
                self.assertIn("FxVisual", remap, f"{caption} strip must map FxVisual")

    def test_mixer_peek_buttons_and_surfaces(self):
        """Verify the 6 persistent bottom tab buttons (0..5) and non-overlapping Filter/FX editor surfaces."""
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] in ("Mix", "Mixer"))
        components = {c["componentData"]["name"]: c for c in tab["componentDefinition"]["componentsData"]}
        native_base = mixer_peek_index(len(self.params))

        for s_cap in ("SUB", "A", "B", "C", "NOISE", "F1", "F2", "BUS 1", "BUS 2", "MAIN"):
            strip_gates = components[f"Mixer Native {s_cap} Strip"]["bounds"]["additionalInvalidatingHandles"]
            self.assertIn(f"IndexedEnabling/0/6/Parameter {native_base + 856}", strip_gates)

        all_tabs = ("MIXER", "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")
        for i, caption in enumerate(all_tabs):
            peek_name = f"Mixer Native {caption} Peek"

            self.assertIn(peek_name, components)
            btn = components[peek_name]
            self.assertEqual(btn["componentData"]["type"], "Button")
            remap = {h["key"]: h["value"] for h in btn["handle remapping"]["map"]}
            self.assertEqual(remap.get("Data"), f"Parameter {native_base}")
            data = btn["componentData"]["data"]
            self.assertEqual(data["buttonId"], i, f"{caption} buttonId must be {i} (0..5)")
            self.assertEqual(data["numButtonsInGroup"], 6)
            self.assertEqual(data["gestureBehaviour"], "Instant")
            bounds = btn["bounds"]["bounds"].split()
            self.assertEqual(int(bounds[1]), 566)
            self.assertEqual(int(bounds[3]), 56)
            # Persistent tabs keep modal_gate (active in 0..18 across MIXER + 5 docked views, hidden on full-screen popups)
            gates = btn["bounds"].get("additionalInvalidatingHandles", [])
            self.assertTrue(any(g.startswith("RangedEnabling/0/18/33/") for g in gates),
                            f"{caption} persistent tab must carry Overlay Instance modal gate (< 19)")
            self.assertFalse(any(g.startswith("IndexedEnabling/0/6/") for g in gates),
                             f"{caption} persistent tab must NOT be gated to tab 0 only")

        # Docked surfaces must exclude bottom tab bar (height <= 564)
        self.assertIn("Mixer Native Peek Filter", components)
        self.assertIn("Mixer Native Peek FX", components)
        for name in ("Mixer Native Peek Filter", "Mixer Native Peek FX"):
            _, y, _, h = map(int, components[name]["bounds"]["bounds"].split())
            self.assertLessEqual(y + h, 564, f"{name} must not cover bottom tab bar y=566..622")

    def test_sdk_docs_present_and_applicable(self):
        """Verify targeted SDK documentation exists at the required location."""
        self.assertTrue(SDK_DOCS.is_dir(), f"SDK docs directory {SDK_DOCS} must exist")
        ui_ref = SDK_DOCS / "docs" / "ui-reference.md"
        self.assertTrue(ui_ref.is_file(), f"{ui_ref} must exist")
        content = ui_ref.read_text(encoding="utf-8")
        self.assertIn("ColourBar", content)
        self.assertIn("Meter", content)


if __name__ == "__main__":
    unittest.main()
