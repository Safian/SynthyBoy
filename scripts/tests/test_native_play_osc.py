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
        wrappers = [w for w in self.widgets if w["componentData"]["type"] == "playNativeOsc"]
        self.assertEqual(len(wrappers), 4)
        self.assertFalse(any(w["componentData"]["name"].startswith(("Play Osc Pan ", "Play Osc Level ")) for w in self.widgets))
        fields = ("power", "expand", "routing", "first", "second", "pan", "level")
        for source, root in enumerate(wrappers, 1):
            with self.subTest(source=source):
                data = root["componentData"]
                group = ("sub", "abc", "abc", "abc", "noise")[source]
                self.assertEqual(data["type"], "playNativeOsc")
                self.assertEqual(root["bounds"]["acceptsHWFocus"], "Children")
                gates = root["bounds"]["additionalInvalidatingHandles"]
                self.assertIn("RangedEnabling/0/18/33/Parameter 2675", gates)
                self.assertIn(f"IndexedEnabling/{source}/5/Parameter 2676", gates)
                start = 3313 + 7 * source
                remaps = self.remaps(root)
                self.assertEqual([remaps[f] for f in fields], [f"Parameter {start + i}" for i in range(7)])
                first = 2 * source
                if group == "abc":
                    self.assertIn(f"RangedEnabling/{first}/{first + 1}/9/Parameter 3312", gates)
                    self.assertEqual(remaps["Wavetable"], f"IndexedEnabling/{first}/9/Parameter 3312")
                    self.assertEqual(remaps["Content"], f"IndexedEnabling/{first + 1}/9/Parameter 3312")
                else:
                    self.assertIn(f"IndexedEnabling/{1 if group == 'sub' else 8}/9/Parameter 3312", gates)
                children = self.definitions[data["type"]]["componentsData"]
                for on in (False, True):
                    observed = {}
                    for child in children:
                        bounds = child["bounds"]
                        guard = bounds.get("additionalInvalidatingHandles", ())
                        mode = next((gate for gate in guard if gate.isalpha()), None)
                        if (f"IndexedEnabling/{int(not on)}/2/power" in guard or mode == "Content"
                                or (mode is not None and remaps[mode] == "Invalid")):
                            continue
                        field = self.remaps(child)["Data"]
                        self.assertNotIn(field, observed)
                        observed[field] = self.rect(child)
                        if not on and field != "power":
                            expected = {"first": "playOscInactiveChoice", "second": "playOscInactiveChoice",
                                        "expand": "Image"}.get(field, "playOscInactive")
                            self.assertEqual(child["componentData"]["type"], expected)
                            self.assertEqual(bounds["acceptsHWFocus"], "No")
                    names = (set(fields) - ({"routing"} if group == "sub" else set())) if on else {"power", "pan", "level"}
                    if not on and group == "sub":
                        names.update(("expand", "first", "second"))
                    self.assertEqual(observed, {f: self.manifest[group][f] for f in names})
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
        expected = {"power", "expand", "expand off"}
        for field, index, caption, enabled_kind in (
                ("first", 3316, "WAVE", "playSubWaveField"), ("second", 3317, "OCT", "playOscNumberField"),
                ("pan", 3318, "PAN", "playSubPan"), ("level", 3319, "LEVEL", "playSubLevel")):
            expected.update((field, field + " off"))
            for on, suffix in ((True, ""), (False, " off")):
                item = sub[field + suffix]
                self.assertEqual(item["bounds"]["bounds"], " ".join(map(str, self.manifest["sub"][field])))
                self.assertEqual(self.remaps(item), {"Data": f"Parameter {index}", "Caption": f"WithName[{caption}]/Parameter {index}"})
                self.assertEqual(item["bounds"]["acceptsHWFocus"], "Yes" if on else "No")
                kind = item["componentData"]["type"]
                off_kind = {"first": "playSubWaveInactive", "second": "playSubOctInactive"}.get(field, "playOscInactive")
                self.assertEqual(kind, enabled_kind if on else off_kind)
                self.assertEqual(set(item["bounds"]["additionalInvalidatingHandles"]), {
                    "IndexedEnabling/1/9/Parameter 3312", "IndexedEnabling/0/5/Parameter 2676",
                    f"IndexedEnabling/{int(on)}/2/Parameter 1558", "RangedEnabling/0/18/33/Parameter 2675"})
        self.assertEqual(sub.keys(), expected)
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
        oct_off = self.definitions["playSubOctInactive"]
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

    def test_source_off_replaces_only_the_pan_and_level_value(self):
        definition = self.definitions["playOscInactive"]
        self.assertEqual(definition["actions"], [])
        self.assertTrue(definition["ignoreMousePresses"])
        self.assertTrue(definition["disableCoarseDataWheel"])
        caption, value = [child["componentData"]["data"] for child in definition["componentsData"]]
        self.assertEqual((caption["type"], caption["handleName"], caption["textStyle"]["colour"]),
                         ("Name", "Caption", "ff999999"))
        self.assertEqual((value["type"], value["text"], value["textStyle"]["colour"]),
                         ("Text", "OFF", "ffff5a7a"))
        for key in ("playSubWaveInactive", "playSubOctInactive"):
            choice = self.definitions[key]["componentsData"][-1]["componentData"]["data"]
            self.assertEqual((choice["type"], choice["handleName"]), ("Value", "Data"))

    def test_choices_source_buttons_and_octave_are_stock_widgets(self):
        expand, = [child for child in self.definitions["playNativeOsc"]["componentsData"]
                   if child["componentData"]["name"] == "expand"]
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
