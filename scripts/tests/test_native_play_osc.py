"""Compact Play oscillator surface in the packaged MPC skin."""
import json
import unittest
from itertools import combinations

from PIL import Image
import test_mpc_skin_extras as base
import test_mpc_skin_no_fallback as fallback
import native_play.osc as play_osc


@unittest.skipUnless((base.SDK / "tools/gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class NativePlayOscTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fallback.NoFallbackTests.setUpClass()
        cls.addClassCleanup(fallback.NoFallbackTests.tearDownClass)
        if fallback.NoFallbackTests.code:
            raise AssertionError(fallback.NoFallbackTests.err)
        data = fallback.NoFallbackTests.tui["pageData"]
        cls.widgets = next(t for t in data["tabs"] if t["tabName"] == "Play")["componentDefinition"]["componentsData"]
        cls.definitions = {row["key"]: row["value"] for row in data["componentDefinitions"]["localComponentDefinitions"]}
        cls.replacements = {row["key"]: row["value"] for row in data["replacementStrings"]}
        cls.manifest = json.loads((base.REAL_EXTRAS / "play-osc-native-contract.json").read_text())

    @staticmethod
    def rect(widget):
        return list(map(int, widget["bounds"]["bounds"].split()))

    @staticmethod
    def remaps(widget):
        return {kv["key"]: kv["value"] for kv in widget["handle remapping"]["map"]}

    def test_each_source_owns_its_own_widgets_and_does_not_share_the_legacy_proxy(self):
        self.assertNotIn("playNativeOsc", self.definitions)
        fields = ("power", "expand", "routing", "first", "second", "pan", "level")
        for ordinal, source in enumerate(("Sub", "Osc A", "Osc B", "Osc C", "Noise")):
            group = ("sub", "abc", "abc", "abc", "noise")[ordinal]
            prefix = f"Play Native OSC {source} "
            controls = [w for w in self.widgets if w["componentData"]["name"].startswith(prefix)]
            start = 3313 + 7 * ordinal
            modes = (2 * ordinal, 2 * ordinal + 1) if group == "abc" else (1 if group == "sub" else 8,)
            for mode in modes:
                with self.subTest(source=source, mode=mode):
                    observed = {}
                    for item in controls:
                        field = item["componentData"]["name"].removeprefix(prefix)
                        gates = item["bounds"]["additionalInvalidatingHandles"]
                        self.assertIn(f"IndexedEnabling/{ordinal}/5/Parameter 2676", gates)
                        self.assertIn("RangedEnabling/0/18/33/Parameter 2675", gates)
                        source_gate = (f"RangedEnabling/{modes[0]}/{modes[-1]}/9/Parameter 3312" if group == "abc"
                                       else f"IndexedEnabling/{mode}/9/Parameter 3312")
                        self.assertIn(source_gate, gates)
                        if group == "abc" and field == "second":
                            kind = item["componentData"]["type"]
                            slot = {"NativeActive playOscTableField": modes[0],
                                    "NativeActive playOscContentField": modes[1]}[kind]
                            self.assertIn(f"IndexedEnabling/{slot}/9/Parameter 3312", gates)
                            if slot != mode:
                                continue
                        bindings = self.remaps(item)
                        self.assertEqual(bindings["Data"], f"Parameter {start + fields.index(field)}")
                        self.assertNotIn(field, observed)
                        observed[field] = self.rect(item)
                        if field == "power":
                            self.assertEqual(item["componentData"]["type"], "Button")
                        else:
                            for active in (0, 1):
                                self.assertEqual(bindings[f"NativeActiveGate{active}"],
                                                 f"IndexedEnabling/{active}/2/Parameter {start}")
                    expected = set(fields) - ({"routing"} if group == "sub" else set())
                    self.assertEqual(observed, {f: self.manifest[group][f] for f in expected})
                    for (x, y, w, h), (xx, yy, ww, hh) in combinations(observed.values(), 2):
                        self.assertTrue(x + w <= xx or xx + ww <= x or y + h <= yy or yy + hh <= y)
                    self.assertTrue(all(w >= 56 and h >= 56 for _, _, w, h in observed.values()))
            if group != "sub":
                self.assertEqual(self.replacements[f"Parameter {start + 2}"][-1], "Routing controls")

    def test_sub_body_is_on_the_tab_with_resolved_data_caption_and_power_gates(self):
        self.assertEqual(self.manifest["sub"]["first"], [14, 404, 292, 72])
        self.assertEqual(self.manifest["sub"]["second"], [310, 412, 112, 56])
        self.assertEqual(self.manifest["abc"]["first"], [14, 404, 202, 72])
        self.assertEqual(self.manifest["abc"]["second"], [220, 404, 202, 72])
        self.assertEqual(self.manifest["noise"]["first"], [14, 404, 202, 72])
        self.assertEqual(self.manifest["noise"]["second"], [220, 404, 202, 72])
        self.assertEqual(
            play_osc.CONTRACT_HEADER.read_text(encoding="utf-8"),
            play_osc.render_contract_header(base.REAL_EXTRAS / "play-osc-native-contract.json"),
            "Generated C++ Play OSC contract header must match JSON",
        )
        sub = {w["componentData"]["name"].removeprefix("Play Native OSC Sub "): w for w in self.widgets
               if w["componentData"]["name"].startswith("Play Native OSC Sub ")}
        self.assertEqual(set(sub), {"power", "expand", "first", "second", "pan", "level"})
        for field, index, caption, enabled_kind in (
                ("first", 3316, "WAVE", "playSubWaveField"), ("second", 3317, "OCT", "playOscNumberField"),
                ("pan", 3318, "PAN", "playSubPan"), ("level", 3319, "LEVEL", "playSubLevel")):
            item = sub[field]
            self.assertEqual(self.remaps(item)["Caption"], f"WithName[{caption}]/Parameter {index}")
            states = self.definitions[item["componentData"]["type"]]["componentsData"]
            self.assertEqual([(s["componentData"]["type"], s["bounds"]["additionalInvalidatingHandles"],
                               s["bounds"]["acceptsHWFocus"]) for s in states],
                             [("NativeInactive " + enabled_kind, ["NativeActiveGate0"], "No"),
                              (enabled_kind, ["NativeActiveGate1"], "Children")])
        # Sub OCT uses shared name_value (transparent root, outside Caption label, framed right value box, 40px visual in >=56px touch area)
        oct_def = self.definitions["playOscNumberField"]
        self.assertEqual(oct_def["backgroundData"]["unfocussed"]["colour"], "0")
        self.assertEqual(oct_def["backgroundData"]["focussed"]["colour"], "0")
        self.assertFalse(oct_def["ignoreMousePresses"])
        self.assertFalse(oct_def["disableCoarseDataWheel"])
        self.assertEqual([(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in oct_def["actions"]],
                         [(t, "Show Overlay", "Data", "play osc octave") for t in ("Mouse Down", "Enter Pressed")])
        oct_children = {c["componentData"]["name"]: c for c in oct_def["componentsData"]}
        self.assertEqual(set(oct_children), {"focus", "Face", "name", "value"})
        self.assertEqual(self.rect(oct_children["focus"]), [44, 8, 68, 40])
        self.assertEqual(self.rect(oct_children["Face"]), [44, 8, 68, 40])
        self.assertEqual(oct_children["Face"]["componentData"]["data"]["image"], "name_value_box_68x40.png")
        self.assertTrue((fallback.NoFallbackTests.skins / "name_value_box_68x40.png").is_file())
        self.assertEqual(self.rect(oct_children["name"]), [8, 8, 36, 40])
        self.assertEqual(self.rect(oct_children["value"]), [44, 8, 68, 40])
        self.assertEqual((oct_children["name"]["componentData"]["data"]["type"], oct_children["name"]["componentData"]["data"]["handleName"]), ("Name", "Caption"))
        self.assertEqual((oct_children["value"]["componentData"]["data"]["type"], oct_children["value"]["componentData"]["data"]["handleName"]), ("Value", "Data"))
        # Sub OCT OFF readout keeps name_value geometry with passive grey styling
        oct_off = self.definitions["NativeInactive playOscNumberField"]
        self.assertEqual(oct_off["actions"], [])
        self.assertTrue(oct_off["ignoreMousePresses"])
        self.assertTrue(oct_off["disableCoarseDataWheel"])
        oct_off_children = {c["componentData"]["name"]: c for c in oct_off["componentsData"]}
        self.assertNotIn("focus", oct_off_children)
        self.assertEqual(self.rect(oct_off_children["Face"]), [44, 8, 68, 40])
        self.assertEqual(self.rect(oct_off_children["name"]), [8, 8, 36, 40])
        self.assertEqual(self.rect(oct_off_children["value"]), [44, 8, 68, 40])
        # Sub WAVE uses wider 292px selector face with room for all waveform labels
        wave_def = self.definitions["playSubWaveField"]
        wave_children = {c["componentData"]["name"]: c for c in wave_def["componentsData"]}
        self.assertEqual(wave_children["Selector face"]["componentData"]["data"]["image"], "selector_face_292x56.png")
        self.assertTrue((fallback.NoFallbackTests.skins / "selector_face_292x56.png").is_file())
        self.assertEqual(self.rect(wave_children["Title"]), [54, 29, 206, 29])
        self.assertEqual(self.rect(wave_children["Name"]), [54, 12, 206, 18])
        pan = {item["componentData"]["name"]: item for item in self.definitions["playSubPan"]["componentsData"]}
        knob = pan["knob"]["componentData"]["data"]
        self.assertEqual((self.rect(pan["focus"]), self.rect(pan["knob"])), ([0, 0, 202, 108], [2, 9, 90, 90]))
        self.assertEqual((knob["knobType"], knob["numFrames"], knob["invert"], knob["handleName"]),
                         ("FilmStrip", 127, False, "Data"))
        self.assertEqual(pan["value"]["componentData"]["data"]["type"], "Value")
        self.assertEqual(pan["name"]["componentData"]["data"]["handleName"], "Caption")
        with Image.open(fallback.NoFallbackTests.skins / knob["filmStrip"]) as strip:
            self.assertEqual(strip.size, (90, 90 * knob["numFrames"]))
        level = {item["componentData"]["name"]: item for item in self.definitions["playSubLevel"]["componentsData"]}
        knob_l = level["knob"]["componentData"]["data"]
        self.assertEqual((self.rect(level["focus"]), self.rect(level["knob"])), ([0, 0, 202, 108], [2, 9, 90, 90]))
        self.assertEqual((knob_l["knobType"], knob_l["numFrames"], knob_l["invert"], knob_l["handleName"]),
                         ("FilmStrip", 127, False, "Data"))
        with Image.open(fallback.NoFallbackTests.skins / knob_l["filmStrip"]) as strip:
            self.assertEqual(strip.size, (90, 90 * knob_l["numFrames"]))

    def test_inactive_dials_keep_their_bound_readouts(self):
        for key in ("playSubPan", "playSubLevel", "playOscPan", "compactNativeDial", "compactNativeRiseDial"):
            definition = self.definitions["NativeInactive " + key]
            self.assertEqual(definition["actions"], [])
            self.assertTrue(definition["ignoreMousePresses"])
            self.assertTrue(definition["disableCoarseDataWheel"])
            children = {child["componentData"]["name"]: child["componentData"] for child in definition["componentsData"]}
            self.assertEqual(children["value"]["data"]["handleName"], "Data")
            self.assertEqual(children["name"]["data"]["handleName"], "Caption")
            self.assertNotIn("focus", children)

    def test_choices_source_buttons_and_octave_are_stock_widgets(self):
        sub_expand, = [w for w in self.widgets if w["componentData"]["name"] == "Play Native OSC Sub expand"]
        active, = [c for c in self.definitions[sub_expand["componentData"]["type"]]["componentsData"]
                   if c["bounds"]["additionalInvalidatingHandles"] == ["NativeActiveGate1"]]
        expand, = self.definitions[active["componentData"]["type"]]["componentsData"]
        for image in ("onImage", "offImage"):
            self.assertTrue(expand["componentData"]["data"][image].startswith("native_action_"))
        self.assertEqual(self.replacements["Parameter 3323"],
                         ["Wavetable", "Multisample", "Sample", "Granular", "Spectral"])
        for name, marker_x, marker_rgb, stock_type in (
            ("play osc routing", 52, "e5a44f", "MenuOverlay"),
            ("play osc choice", 64, "de78c9", "MenuOverlay"),
            ("play sub waveform", 64, "de78c9", "MenuOverlay"),
            ("play osc octave", 76, "58d4b1", "MenuOverlay"),
        ):
            popup = self.definitions[name]["componentsData"]
            self.assertEqual(popup[0]["componentData"]["type"], stock_type)
            marker, = [c for c in popup if c["componentData"]["name"] == f"Play OSC marker {marker_x}"]
            self.assertEqual(marker["componentData"]["type"], "Image")
            self.assertEqual(self.rect(marker), [marker_x, 120, 6, 6])
            art = fallback.NoFallbackTests.skins / marker["componentData"]["data"]["image"]
            with Image.open(art) as image:
                self.assertEqual(image.tobytes(), bytes.fromhex(marker_rgb) * 36)
        for menu, labels in (("play sub waveform", self.replacements["Parameter 3316"]),
                             ("play osc octave", [str(i) for i in range(-4, 5)])):
            hint, = [child for child in self.definitions[menu]["componentsData"]
                     if child["componentData"]["name"] == "Apply hint"]
            self.assertGreater(self.rect(hint)[0], 426, "the stock drawer header must receive the commit tap")
            buttons = [child["componentData"] for child in self.definitions[menu]["componentsData"]
                       if child["componentData"]["type"] == "Button"]
            self.assertEqual([item["name"] for item in buttons], labels)
            for i, item in enumerate(buttons):
                data = item["data"]
                self.assertEqual((data["buttonId"], data["numButtonsInGroup"]), (i, len(labels)))
                for art in ("onImage", "offImage"):
                    self.assertTrue((fallback.NoFallbackTests.skins / data[art]).is_file())
        buttons = [child for child in self.definitions["playOscTableField"]["componentsData"]
                   if child["componentData"]["type"] == "Button"]
        self.assertEqual({child["componentData"]["name"]: child["componentData"]["data"]["buttonId"] for child in buttons},
                         {"previous": 1, "browse": 2, "next": 3})
        self.assertTrue(all(self.rect(child)[2] >= 56 for child in buttons))


if __name__ == "__main__":
    unittest.main()
