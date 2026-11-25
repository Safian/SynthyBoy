#!/usr/bin/env python3
# SynthyBoy - Unit tests for the extracted native Mixer components:
# SoloMutePair, CalibratedFader, MeterBlock, StripHeader, and Bottom TabBar.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import tempfile
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))

from components.native.solo_mute.solo_mute import (
    build_solo_mute_definitions,
    create_solo_mute_instance,
    derive_channel_presentation_state,
    STATE_NEUTRAL,
    STATE_SOLO,
    STATE_MUTE,
    STATE_IMPLICIT_SOLO_MUTE,
    STATE_DISABLED,
    VARIANT_SOURCE,
    VARIANT_BUS,
    VARIANT_MASTER,
    TYPE_SOLO_MUTE_SOURCE,
    TYPE_SOLO_MUTE_BUS,
    TYPE_SOLO_MUTE_MASTER,
)
from components.native.fader.fader import (
    build_fader_definition,
    create_fader_instance,
    format_fader_value,
    unity_normalized,
    SCALE_PERCENT,
    SCALE_LINEAR_DB,
    SCALE_SKEW_DB,
)
from components.native.meter_block.meter_block import (
    build_meter_block_definition,
    create_meter_block_instance,
    TYPE_METER_BLOCK_SOURCE,
    TYPE_METER_BLOCK_BUS,
)
from components.native.mixer_strip.mixer_strip import (
    build_mixer_strip_definitions,
    build_mixer_strip_header_definition,
    TYPE_STRIP_SOURCE,
    TYPE_HEADER_SOURCE,
    TYPE_HEADER_BUS,
    TYPE_HEADER_MASTER,
)


class TestNativeMixerComponents(unittest.TestCase):
    def test_inactive_source_dials_stay_visible_and_read_only(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            definitions = build_mixer_strip_definitions(directory)
            cells = {e["componentData"]["name"]: e for e in definitions[TYPE_STRIP_SOURCE]["componentsData"]}
            for name, handle, family in (("pan", "Pan", "pan"), ("send1", "Send1", "level"),
                                         ("send2", "Send2", "level")):
                with self.subTest(name=name):
                    on, off = cells[name], cells[name + " off"]
                    self.assertEqual(off["bounds"]["bounds"], on["bounds"]["bounds"])
                    self.assertEqual(off["handle remapping"], on["handle remapping"])
                    self.assertIn({"key": "Data", "value": handle}, on["handle remapping"]["map"])
                    for entry, power in ((on, 1), (off, 0)):
                        self.assertEqual(entry["bounds"]["additionalInvalidatingHandles"],
                                         [f"IndexedEnabling/{power}/2/Power"])
                        self.assertEqual(entry["bounds"]["showWhenDataModelInvalid"], "Hide")
                    self.assertEqual(off["bounds"]["acceptsHWFocus"], "No")
                    disabled = definitions[off["componentData"]["type"]]
                    self.assertEqual(disabled["actions"], [])
                    self.assertTrue(disabled["disableCoarseDataWheel"])
                    self.assertTrue(disabled["ignoreMousePresses"])
                    for child in disabled["componentsData"]:
                        self.assertEqual(child["bounds"]["acceptsHWFocus"], "No")
                    knobs = []
                    for entry in (on, off):
                        definition = definitions[entry["componentData"]["type"]]
                        knobs.append(next(c["componentData"]["data"] for c in definition["componentsData"]
                                          if c["componentData"]["type"] == "Knob"))
                    active, inactive = knobs
                    self.assertEqual(active["dragOrientation"], "Vertical")
                    self.assertEqual(inactive["dragOrientation"], "None")
                    self.assertEqual(active["filmStrip"], f"play_sub_{family}_40.png")
                    self.assertEqual(inactive["filmStrip"], f"play_sub_{family}_40_disabled.png")
                    for knob in knobs:
                        self.assertEqual((knob["knobType"], knob["handleName"]), ("FilmStrip", "Data"))
                        with Image.open(directory / knob["filmStrip"]) as strip:
                            self.assertEqual(strip.size, (40, 40 * knob["numFrames"]))
                            self.assertIsNotNone(strip.getchannel("A").getbbox())

    def test_solo_mute_pair_definitions(self):
        defs = build_solo_mute_definitions()
        self.assertIn(TYPE_SOLO_MUTE_SOURCE, defs)
        self.assertIn(TYPE_SOLO_MUTE_BUS, defs)
        self.assertIn(TYPE_SOLO_MUTE_MASTER, defs)

        # Source variant exposes FilmStrip visual Layers + 56x56 Button hitboxes
        src_comps = defs[TYPE_SOLO_MUTE_SOURCE]["componentsData"]
        self.assertEqual(len(src_comps), 4)
        self.assertEqual([c["componentData"]["name"] for c in src_comps],
                         ["Solo Visual", "Mute Visual", "Solo Button", "Mute Button"])

        # Master variant exposes FX Enable + Mute FilmStrip visual layers + 56x56 Button hitboxes
        mst_comps = defs[TYPE_SOLO_MUTE_MASTER]["componentsData"]
        self.assertEqual(len(mst_comps), 4)
        self.assertEqual([c["componentData"]["name"] for c in mst_comps],
                         ["FX Enable Visual", "Master Mute Visual", "FX Enable Button", "Master Mute Button"])

    def test_solo_mute_multi_state_derivation(self):
        # 1. Normal active channel: neutral
        s, m = derive_channel_presentation_state(is_source_on=True, user_solo=False, user_mute=False, any_solo_active=False, is_routing_active=True)
        self.assertEqual((s, m), (STATE_NEUTRAL, STATE_NEUTRAL))

        # 2. Explicit solo
        s, m = derive_channel_presentation_state(is_source_on=True, user_solo=True, user_mute=False, any_solo_active=True, is_routing_active=True)
        self.assertEqual((s, m), (STATE_SOLO, STATE_NEUTRAL))

        # 3. Explicit mute
        s, m = derive_channel_presentation_state(is_source_on=True, user_solo=False, user_mute=True, any_solo_active=False, is_routing_active=True)
        self.assertEqual((s, m), (STATE_NEUTRAL, STATE_MUTE))

        # 4. Implicit solo mute: another channel soloed, current channel not routed
        s, m = derive_channel_presentation_state(is_source_on=True, user_solo=False, user_mute=False, any_solo_active=True, is_routing_active=False)
        self.assertEqual((s, m), (STATE_NEUTRAL, STATE_IMPLICIT_SOLO_MUTE))

        # 5. Disabled source: both buttons disabled
        s, m = derive_channel_presentation_state(is_source_on=False, user_solo=False, user_mute=False, any_solo_active=False, is_routing_active=False)
        self.assertEqual((s, m), (STATE_DISABLED, STATE_DISABLED))

    def test_calibrated_fader_contracts(self):
        fader_def = build_fader_definition("testFader", height=198, scale_mode=SCALE_PERCENT)
        self.assertIn("componentsData", fader_def)
        comps = fader_def["componentsData"]
        self.assertEqual(len(comps), 2)
        self.assertEqual(comps[0]["componentData"]["type"], "Slider")
        self.assertEqual(comps[1]["componentData"]["type"], "Label")
        self.assertEqual(comps[0]["componentData"]["data"]["sliderType"], "TrackAndThumb")

        # Unity references
        self.assertEqual(unity_normalized(SCALE_PERCENT), 1.0)
        self.assertAlmostEqual(unity_normalized(SCALE_LINEAR_DB), 60.0 / 72.0, places=3)
        self.assertAlmostEqual(unity_normalized(SCALE_SKEW_DB), 0.7885, places=2)

        # Formatting values
        self.assertEqual(format_fader_value(100.0, SCALE_PERCENT), "100%")
        self.assertEqual(format_fader_value(0.0, SCALE_LINEAR_DB), "0.0 dB")
        self.assertEqual(format_fader_value(-60.0, SCALE_LINEAR_DB), "-inf")
        self.assertEqual(format_fader_value(0.0, SCALE_SKEW_DB), "0.0 dB")

    def test_meter_block_composition(self):
        for hold in (False, True):
            with self.subTest(hold=hold):
                rect = (0, 0, 12, 198)
                mb_def = build_meter_block_definition("testMeterBlock", width=12, height=198, hold=hold, with_clip=False)
                meter, = mb_def["componentsData"]
                self.assertEqual(meter["bounds"]["bounds"], "0 0 12 198")
                self.assertEqual(meter["bounds"]["acceptsHWFocus"], "No")
                self.assertEqual(meter["componentData"]["type"], "Meter")
                data = meter["componentData"]["data"]
                self.assertEqual((data["peakHandle"], data["peakHoldHandle"]), ("Peak", "Hold" if hold else ""))
                bindings = {"Peak": "Parameter 123"}
                if hold:
                    bindings["Hold"] = "Parameter 124"
                self.assertEqual(meter["handle remapping"]["map"], [{"key": k, "value": k} for k in bindings])
                instance = create_meter_block_instance("Test", "testMeterBlock", rect, 123, 124 if hold else None)
                self.assertEqual(instance["handle remapping"]["map"], [{"key": k, "value": v} for k, v in bindings.items()])
                self.assertTrue(mb_def["ignoreMousePresses"])
                self.assertEqual(mb_def["actions"], [])

    def test_strip_header_definition(self):
        hdr_src = build_mixer_strip_header_definition(VARIANT_SOURCE)
        self.assertIn("componentsData", hdr_src)
        comps = hdr_src["componentsData"]
        self.assertEqual(len(comps), 2)
        self.assertEqual(comps[0]["componentData"]["name"], "name")
        self.assertEqual(comps[1]["componentData"]["type"], TYPE_SOLO_MUTE_SOURCE)

    def test_mixer_definitions_integration(self):
        defs = build_mixer_strip_definitions()
        for expected in (
            TYPE_SOLO_MUTE_SOURCE,
            TYPE_SOLO_MUTE_BUS,
            TYPE_SOLO_MUTE_MASTER,
            TYPE_METER_BLOCK_SOURCE,
            TYPE_METER_BLOCK_BUS,
            TYPE_HEADER_SOURCE,
            TYPE_HEADER_BUS,
            TYPE_HEADER_MASTER,
            "mixerStripSource",
            "mixerStripBus",
            "mixerStripMaster",
        ):
            self.assertIn(expected, defs, f"Shared definition {expected} must be registered")

        # Point A: Shared Header/SoloMutePair and MeterBlock MUST actually be instanced in all 3 strip variants!
        # No dead definitions and no legacy direct Button/Meter children.
        for strip_key, hdr_type, mb_type in (
            ("mixerStripSource", TYPE_HEADER_SOURCE, TYPE_METER_BLOCK_SOURCE),
            ("mixerStripBus", TYPE_HEADER_BUS, TYPE_METER_BLOCK_BUS),
            ("mixerStripMaster", TYPE_HEADER_MASTER, TYPE_METER_BLOCK_BUS),
        ):
            children = defs[strip_key]["componentsData"]
            child_types = [c["componentData"]["type"] for c in children]
            self.assertIn(hdr_type, child_types, f"{strip_key} must instance {hdr_type}")
            self.assertIn(mb_type, child_types, f"{strip_key} must instance {mb_type}")
            self.assertNotIn("Button", child_types, f"{strip_key} must not contain legacy direct Button")
            self.assertNotIn("Meter", child_types, f"{strip_key} must not contain legacy plain Meter")

        # SoloMutePair definitions must expose FilmStrip multi-state Images + 56x56 Button hitboxes
        for sm_key, visual_handle in (
            (TYPE_SOLO_MUTE_SOURCE, "SoloVisual"),
            (TYPE_SOLO_MUTE_BUS, "SoloVisual"),
            (TYPE_SOLO_MUTE_MASTER, "FxVisual"),
        ):
            sm_children = defs[sm_key]["componentsData"]
            types = [c["componentData"]["type"] for c in sm_children]
            self.assertIn("Image", types, f"{sm_key} must have FilmStrip Image visual layer")
            self.assertIn("Button", types, f"{sm_key} must preserve 56x56 Button hitboxes")
            remaps = {m["value"] for c in sm_children for m in c["handle remapping"]["map"]}
            self.assertIn(visual_handle, remaps, f"{sm_key} must bind {visual_handle}")
            self.assertIn("MuteVisual", remaps, f"{sm_key} must bind MuteVisual")
            for c in sm_children:
                if c["componentData"]["type"] == "Button":
                    _, _, bw, bh = map(int, c["bounds"]["bounds"].split())
                    self.assertGreaterEqual(bw, 56)
                    self.assertGreaterEqual(bh, 56)

    def test_meter_dbfs_calibration_matches_engine(self):
        # OwnEngine writeReadout uses: 1.0 + 20 * log10(pk) / 60.0 (-60 dBFS = 0.0, 0 dBFS = 1.0)
        from components.native.meter_block.meter_block import dbfs_to_normalized, calibrated_dbfs_marks
        self.assertAlmostEqual(dbfs_to_normalized(0.0), 1.0, places=4)
        self.assertAlmostEqual(dbfs_to_normalized(-6.0), 0.90, places=4)
        self.assertAlmostEqual(dbfs_to_normalized(-12.0), 0.80, places=4)
        self.assertAlmostEqual(dbfs_to_normalized(-24.0), 0.60, places=4)
        self.assertAlmostEqual(dbfs_to_normalized(-48.0), 0.20, places=4)
        self.assertAlmostEqual(dbfs_to_normalized(-60.0), 0.0, places=4)
        marks = dict((lbl, norm) for norm, lbl, _ in calibrated_dbfs_marks())
        self.assertAlmostEqual(marks["-6"], 0.90, places=4)
        self.assertAlmostEqual(marks["-12"], 0.80, places=4)
        self.assertAlmostEqual(marks["-24"], 0.60, places=4)


if __name__ == "__main__":
    unittest.main()
