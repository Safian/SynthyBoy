#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
import subprocess
import unittest
from itertools import combinations

from test_mpc_skin_extras import REAL_EXTRAS, SDK, leaf_model, standalone_editor_entries


CONTRACT = REAL_EXTRAS / "lfo-native-contract.json"
EDITOR_CONTRACT = REAL_EXTRAS / "lfo-shape-editor-contract.json"
DETAIL_CONTRACT = REAL_EXTRAS / "lfo-shape-detail-contract.json"


class LfoContractTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SYNTHYBOY_LFO_UI_HOST"), "set SYNTHYBOY_LFO_UI_HOST to lfo_ui_host")
    def test_contract_matches_the_cpp_editor(self):
        result = subprocess.check_output([os.environ["SYNTHYBOY_LFO_UI_HOST"], "--skin-contract"])
        self.assertEqual(result, CONTRACT.read_bytes())
        result = subprocess.check_output([os.environ["SYNTHYBOY_LFO_UI_HOST"], "--shape-editor-skin-contract"])
        self.assertEqual(result, EDITOR_CONTRACT.read_bytes())
        result = subprocess.check_output([os.environ["SYNTHYBOY_LFO_UI_HOST"], "--shape-detail-skin-contract"])
        self.assertEqual(result, DETAIL_CONTRACT.read_bytes())


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class LfoSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        cls.tui = NoFallbackTests.tui
        cls.skins = NoFallbackTests.skins
        cls.params = NoFallbackTests.params
        cls.qlinks = NoFallbackTests.qlinks
        cls.applied = (NoFallbackTests.code, NoFallbackTests.err)
        cls.tab = next(t for t in cls.tui["pageData"]["tabs"] if t["tabName"] == "LFO")
        definitions = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        cls.entries = list(standalone_editor_entries(cls.tab, definitions))

    def test_targets_and_expand_use_independent_instant_handles(self):
        from native_ui_params import LFO_OFFSET, LFO_TARGETS_ACTION, native_base_index
        base = native_base_index(len(self.params))
        buttons = {c["componentData"]["name"]: c for c in self.entries}
        for action, offset in ((1, LFO_TARGETS_ACTION), (2, LFO_OFFSET + 5)):
            button = buttons[f"LFO 1 Native Action {action}"]
            self.assertEqual(button["handle remapping"]["map"],
                             [{"key": "Data", "value": f"FromGesture/Parameter {base + offset}"}])
            data = button["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["gestureBehaviour"]),
                             (1, 1, "Instant"))

    def test_markers_and_sentinel_clear_the_native_widgets(self):
        from PIL import Image

        items = self.entries
        by_name = {item["componentData"]["name"]: item for item in items}
        native = [item for item in items if item["componentData"]["name"].startswith("LFO 1 Native ")]
        rect = lambda item: tuple(map(int, item["bounds"]["bounds"].split()))
        for name, x in (("Overlay Marker lfo1 left", 33), ("Overlay Instance lfo1", 45), ("Overlay Marker lfo1 right", 1241)):
            marker = rect(by_name[name])
            self.assertEqual(marker, (x, 1, 6, 6))
            # The compositor covers the 6x6 marker and a 1px margin.
            for widget in native:
                wx, wy, ww, wh = rect(widget)
                self.assertTrue(x + 7 <= wx or wx + ww <= x - 1 or 8 <= wy or wy + wh <= 0, widget["componentData"]["name"])
        bg = self.tab["componentDefinition"]["backgroundData"]
        self.assertEqual(bg["focussed"]["image"], bg["unfocussed"]["image"])
        with Image.open(self.skins / bg["unfocussed"]["image"]) as image:
            self.assertEqual(image.size, (1280, 628))
            self.assertEqual(image.convert("RGB").crop((900, 2, 905, 7)).getcolors(), [(25, (11, 16, 41))])
        for widget in native:
            wx, wy, ww, wh = rect(widget)
            self.assertTrue(905 <= wx or wx + ww <= 900 or 7 <= wy or wy + wh <= 2)

    def test_live_widgets_match_all_six_layouts(self):
        self.assertEqual(self.applied, (0, ""))
        expected = {tuple(entry[:3]): tuple(entry[3:]) for entry in json.loads(CONTRACT.read_text())}
        self.assertEqual(len(expected), 74)
        elements = self.entries
        root = [e for e in elements if e["componentData"]["type"] != "nativeModulationEditor"
                and not e["componentData"]["name"].startswith(("LFO 1 Native Editor ", "LFO 1 Native Detail "))]
        items = [e for e in root if e["componentData"]["name"].startswith("LFO 1 Native ")]
        self.assertEqual(len(items), 29)
        self.assertEqual({e["componentData"]["name"] for e in root if e not in items},
                         {"Overlay Marker lfo1 left", "Overlay Marker lfo1 right", "Overlay Instance lfo1"})
        actual = {}
        for e in items:
            data = e["componentData"]
            name = data["name"].removeprefix("LFO 1 Native ")
            span, field, action = name.split()[0].split(".")
            first, last = map(int, span.split("-"))
            field, action = int(field), int(action)
            inactive = name.endswith(" inactive")
            gates = [f"IndexedEnabling/{first}/7/Parameter 2832" if first == last
                     else f"RangedEnabling/{first}/{last}/7/Parameter 2832"]
            target = ("FromGesture/" if action else "") + f"Parameter {2826 + field}"
            if field == 4:
                gates.append(f"IndexedEnabling/{0 if inactive else 1}/2/Parameter 2829")
            self.assertEqual(e["bounds"]["additionalInvalidatingHandles"], gates, name)
            self.assertEqual(e["bounds"]["showWhenDataModelInvalid"], "Hide")
            self.assertEqual(e["bounds"]["acceptsHWFocus"], "No" if inactive else "Yes")
            remaps = {m["key"]: m["value"] for m in e["handle remapping"]["map"]}
            self.assertEqual(remaps["Data"], target)
            if field in (3, 15, 19, 20):
                self.assertEqual(remaps["Active"], target)
            if field == 4:
                self.assertEqual(remaps["Active"], "Parameter 2829")
            rect = tuple(map(int, e["bounds"]["bounds"].split()))
            keys = [(layout, field, action) for layout in range(first, last + 1)]
            for key in keys:
                self.assertEqual(rect, expected[key])
            x, y, w, h = rect
            self.assertTrue(0 <= x and x + w <= 1280 and 0 <= y and y + h <= 628 and min(w, h) >= 56)
            if inactive:
                self.assertTrue(data["type"].endswith("Off"))
            else:
                for key in keys:
                    self.assertNotIn(key, actual)
                    actual[key] = rect
        self.assertEqual(actual, expected)
        for ((layout, _, _), (x, y, w, h)), ((other, _, _), (ox, oy, ow, oh)) in combinations(actual.items(), 2):
            if layout == other:
                self.assertTrue(x + w <= ox or ox + ow <= x or y + h <= oy or oy + oh <= y)
        self.assertTrue(all(field not in (1, 3, 4, 9, 18) and action != 2
                            for layout, field, action in actual if layout >= 4))

    def test_native_children_fit_their_real_contract_cells(self):
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        primitive = {"Button", "Label", "Image", "Knob", "Indicator", "Focus"}
        def visit(entry, parent, trail):
            data = entry["componentData"]
            bounds = entry["bounds"]
            x, y, w, h = map(float, bounds["bounds"].split())
            if bounds.get("boundsType") == "Proportional":
                x, w = x*parent[0], w*parent[0]
                y, h = y*parent[1], h*parent[1]
            self.assertTrue(x >= 0 and y >= 0 and w > 0 and h > 0 and x+w <= parent[0]+.01 and y+h <= parent[1]+.01, trail)
            kind = data["type"]
            if kind in definitions:
                for child in definitions[kind].get("componentsData", []):
                    visit(child, (w, h), trail + "/" + child["componentData"]["name"])
            else:
                self.assertIn(kind, primitive, f"unresolved LFO definition: {kind}")
                if kind == "Knob":
                    self.assertEqual(w, h, trail)
                    self.assertGreaterEqual(w, 56, trail)
                    self.assertEqual(data["data"].get("knobType"), "FilmStrip", trail)
                for key in ("filmStrip", "onImage", "offImage", "image"):
                    asset = data.get("data", {}).get(key)
                    if asset:
                        self.assertNotIn(asset, ("knob_uni.png", "seg3_on.png", "seg3_off.png"), trail)
                        self.assertTrue((self.skins / asset).is_file(), asset)
        for entry in self.entries:
            if entry["componentData"]["name"].startswith("LFO 1 Native "):
                visit(entry, (1280, 628), entry["componentData"]["name"])

    def test_specific_active_labels_smooth_states_and_real_sync_buttons(self):
        from PIL import Image
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        root = [e for e in self.entries if e["componentData"]["name"].startswith("LFO 1 Native ")
                and not e["componentData"]["name"].startswith(("LFO 1 Native Editor ", "LFO 1 Native Detail "))]
        for entry in root:
            name = entry["componentData"]["name"].split()[3]
            field = int(name.split(".")[1])
            definition = definitions[entry["componentData"]["type"]]
            children = definition["componentsData"]
            if field in (3, 15, 19, 20):
                button = next(c for c in children if c["componentData"]["type"] == "Button")
                expected = {3: "smooth", 15: "host", 19: "mono", 20: "stereo"}[field]
                data = button["componentData"]["data"]
                self.assertIn("lfo_"+expected+"_", data["onImage"])
                self.assertEqual(data["handleName"], "Active")
                self.assertGreaterEqual(min(map(int, button["bounds"]["bounds"].split()[2:])), 56)
            if field == 4:
                knob = next(c for c in children if c["componentData"]["type"] == "Knob")
                data = knob["componentData"]["data"]
                inactive = entry["componentData"]["name"].endswith("inactive")
                self.assertEqual("inactive" in data["filmStrip"], inactive)
                self.assertEqual(data["dragOrientation"], "None" if inactive else "Vertical")
                self.assertTrue(any(c["componentData"]["name"] == "value" for c in children))
            if field == 10:
                buttons = [c for c in children if c["componentData"]["type"] == "Button"]
                self.assertEqual({c["componentData"]["data"]["numButtonsInGroup"] for c in buttons}, {5})
                self.assertEqual({c["componentData"]["data"]["buttonId"] for c in buttons}, {0, 1, 2, 3, 4})
                self.assertTrue(all("choice_segmented_" in c["componentData"]["data"]["onImage"] for c in buttons))
                for choice in range(5):
                    visible = []
                    for button in buttons:
                        gates = button["bounds"].get("additionalInvalidatingHandles", [])
                        if all(int(g.split("/")[1]) <= choice <= int(g.split("/")[2]) for g in gates):
                            visible.append(button)
                    active = [b for b in visible if b["componentData"]["data"]["buttonId"] == choice]
                    self.assertEqual(len(active), 1)
                    expected_label = "HZ" if choice == 0 else "KEY" if choice == 4 else "BPM"
                    self.assertTrue(active[0]["componentData"]["name"].endswith(expected_label))

    def test_shape_editor_native_cells_own_the_exported_layout(self):
        from PIL import Image

        contract = json.loads(EDITOR_CONTRACT.read_text())
        prefix = "LFO 1 Native Editor "
        items = {item["componentData"]["name"].removeprefix(prefix): item
                 for item in self.entries
                 if item["componentData"]["name"].startswith(prefix)}
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        optional = {field: 3218 + i for i, field in enumerate(contract["availability"])}
        base_gate = "IndexedEnabling/1/2/Parameter 3195"
        self.assertEqual(len(contract["fields"]), 22)
        self.assertEqual(len(optional), 11)
        self.assertEqual(len(items), 29)
        canvas_x, canvas_y, canvas_w, canvas_h = contract["canvas"]

        def verify(name, field, rect, extra, kind):
            item = items.pop(name)
            self.assertEqual(tuple(map(int, item["bounds"]["bounds"].split())), tuple(rect))
            x, y, w, h = rect
            self.assertTrue(x + w <= canvas_x or y + h <= canvas_y or canvas_x + canvas_w <= x or canvas_y + canvas_h <= y)
            self.assertEqual(item["bounds"]["showWhenDataModelInvalid"], "Hide")
            target = ("FromGesture/" if kind == "Action" else "") + f"Parameter {3196 + field}"
            remap = {"Data": target}
            if kind == "Toggle":
                remap["Active"] = target
            if field in optional:
                remap["Enabled"] = f"Parameter {optional[field]}"
            self.assertEqual({h["key"]: h["value"] for h in item["handle remapping"]["map"]}, remap)
            self.assertEqual(item["bounds"]["additionalInvalidatingHandles"], [base_gate] + extra)
            self.assertEqual(item["bounds"]["acceptsHWFocus"], "Children" if field in optional else "Yes")
            if kind == "Tool":
                self.assertEqual(item["componentData"]["type"], "Button")
                return item["componentData"]["data"]
            definition = definitions[item["componentData"]["type"]]
            if field in optional:
                self.assertTrue(definition["ignoreMousePresses"])
                self.assertEqual(definition["backgroundData"]["unfocussed"]["colour"], "0")
                passive, active = definition["componentsData"]
                for i, child in enumerate((passive, active)):
                    self.assertEqual(child["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{i}/2/Enabled"])
                    self.assertEqual(child["bounds"]["acceptsHWFocus"], "Yes" if i else "No")
                    self.assertEqual(child["bounds"]["bounds"], "0 0 1 1")
                passive_def = definitions[passive["componentData"]["type"]]
                self.assertTrue(passive_def["ignoreMousePresses"])
                self.assertTrue(passive_def["disableCoarseDataWheel"])
                self.assertEqual(passive_def["actions"], [])
                self.assertEqual({c["componentData"]["type"] for c in passive_def["componentsData"]}, {"Label"})
                self.assertTrue(all(c["componentData"]["data"]["textStyle"]["colour"] == "ff9699a4"
                                    for c in passive_def["componentsData"]))
                definition = definitions[active["componentData"]["type"]]
            self.assertFalse(definition["ignoreMousePresses"])
            return definition

        for field, spec in enumerate(contract["fields"]):
            name, kind, rect = spec["name"], spec["type"], spec["rect"]
            if kind == "Tool":
                for i, tool in enumerate(contract["tools"]):
                    button = verify("Tool " + tool["name"], field, tool["rect"], [], kind)
                    self.assertEqual((button["buttonId"], button["numButtonsInGroup"], button["gestureBehaviour"]), (i, 7, "Instant"))
                    for key in ("onImage", "offImage"):
                        with Image.open(self.skins / button[key]) as art:
                            self.assertEqual(art.size, tuple(tool["rect"][2:]))
                continue
            if name == "CURVE":
                for scale, caption in enumerate(("CURVE", "SCALE")):
                    definition = verify(caption, field, rect, [f"IndexedEnabling/{scale}/2/Parameter 3229"], kind)
                    self.assertEqual({a["additionalData"] for a in definition["actions"]}, {"fx editor number menu"})
            else:
                definition = verify(name, field, rect, [], kind)
                if kind in ("Choice", "Number"):
                    menu = "choice" if kind == "Choice" else "number"
                    self.assertEqual({a["additionalData"] for a in definition["actions"]}, {f"fx editor {menu} menu"})
        self.assertEqual(items, {})
        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        self.assertEqual(replacements["Parameter 3217"],
                         next(p["choices"] for p in self.params if p["key"] == "lfo1.sync_type"))

    def test_shape_detail_has_exclusive_point_controls_and_eight_dynamic_choices(self):
        geometry = json.loads(DETAIL_CONTRACT.read_text())
        prefix = "LFO 1 Native Detail "
        items = {item["componentData"]["name"].removeprefix(prefix): item
                 for item in self.entries
                 if item["componentData"]["name"].startswith(prefix)}
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertEqual(len(items), 18)

        def check_item(name, field, index, gates, *, active=True, action=False):
            item = items.pop(name)
            box = item["bounds"]
            self.assertEqual(tuple(map(int, box["bounds"].split())), tuple(geometry[field]))
            self.assertEqual(box["acceptsHWFocus"], "Yes" if active else "No")
            target = ("FromGesture/" if action else "") + f"Parameter {index}"
            self.assertEqual(item["handle remapping"]["map"], [{"key": "Data", "value": target}])
            self.assertEqual(box["additionalInvalidatingHandles"], gates)
            kind = definitions[item["componentData"]["type"]]
            self.assertEqual(kind["ignoreMousePresses"], not active)
            if not active:
                self.assertEqual(kind["actions"], [])
                self.assertTrue(all(c["componentData"]["type"] == "Label" for c in kind["componentsData"]))
            return kind

        point = "IndexedEnabling/1/4/Parameter 3284"
        for field, title in enumerate(("BACK", "MODULATE", "AMOUNT", "REMOVE")):
            action = field in (0, 3)
            bindings = [point]
            if field > 0:
                low, high = ((1, 4), (2, 3), (2, 4))[field - 1]
                bindings += [f"RangedEnabling/{low}/{high}/5/Parameter 3289"]
            item = check_item(title, field, 3285 + field, bindings, action=action)
            if field in (1, 2):
                want = "choice" if field == 1 else "number"
                self.assertEqual({a["additionalData"] for a in item["actions"]}, {f"fx editor {want} menu"})
        for field, title, low, gate in (
            (1, "MODULATE", 0, "IndexedEnabling/0/5/Parameter 3289"),
            (2, "AMOUNT", 0, "RangedEnabling/0/1/5/Parameter 3289"),
            (2, "AMOUNT", 4, "IndexedEnabling/4/5/Parameter 3289"),
            (3, "REMOVE", 0, "RangedEnabling/0/1/5/Parameter 3289"),
        ):
            check_item(title + f" unavailable {low}", field, 3285 + field, [point, gate], active=False, action=field == 3)
        menu = "RangedEnabling/2/3/4/Parameter 3284"
        check_item("Heading", 4, 3292, [menu, "IndexedEnabling/0/2/Parameter 3290"], active=False)
        check_item("Header", 4, 3292, [menu, "IndexedEnabling/1/2/Parameter 3290"], action=True)
        for row in range(8):
            kind = check_item(f"Row {row + 1}", 5 + row, 3293 + row,
                              [menu, f"RangedEnabling/{row + 1}/8/9/Parameter 3291"], action=True)
            button, label = (c["componentData"] for c in kind["componentsData"])
            self.assertEqual((button["type"], label["type"], label["data"]["type"]), ("Button", "Label", "Name"))
        self.assertEqual(items, {})
        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        self.assertEqual(replacements["Parameter 3286"], ["TIME", "LEVEL", "CURVE"])

    def test_drawers_label_and_select_every_native_choice(self):
        page = self.tui["pageData"]
        definitions = {entry["key"]: entry["value"] for entry in page["componentDefinitions"]["localComponentDefinitions"]}
        replacements = {entry["key"]: entry["value"] for entry in page["replacementStrings"]}
        for field in (1, 2, 8, 11, 12, 16):   # field 0 (the source selector) is reserved: the 14 sub-tabs name the LFO
            names = replacements[f"Parameter {2826 + field}"]
            children = definitions[f"lfoNativeChoiceDrawer{field}"]["componentsData"]
            captions = [child["componentData"]["data"]["text"] for child in children
                        if child["componentData"]["type"] == "Label"]
            self.assertEqual(captions, names)
            rows = [child for child in children if child["componentData"]["type"] == "Button"]
            self.assertEqual(len(rows), len(names))
            rectangles = []
            for index, child in enumerate(rows):
                model = child["componentData"]["data"]
                self.assertEqual((model["buttonId"], model["numButtonsInGroup"]), (index, len(names)))
                x, y, w, h = map(int, child["bounds"]["bounds"].split())
                self.assertTrue(0 <= x < x + w <= 1280 and 0 <= y < y + h <= 800)
                rectangles.append((x, y, w, h))
            for (x, y, w, h), (px, py, pw, ph) in combinations(rectangles, 2):
                self.assertTrue(x + w <= px or px + pw <= x or y + h <= py or py + ph <= y)

    def test_host_menus_and_physical_q_proxies(self):
        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        params = {p["key"]: p for p in self.params}
        self.assertNotIn("Parameter 2826", replacements, "the reserved selector handle has no choice list")
        self.assertEqual(replacements["Parameter 2827"], params["lfo1.shape"]["choices"][:-1] + ["Drawn"])
        self.assertEqual(replacements["Parameter 2828"], params["rlfo1.style"]["choices"])
        self.assertEqual(replacements["Parameter 2836"], params["lfo1.sync"]["choices"])
        m = next(m for m in self.qlinks if m["Tab"] == self.tab["fnKeyIndex"] + 1 and m["SubTab"] == self.tab["fnKeySubIndex"] + 1)
        self.assertEqual([m["Q-Links"][f"Q-Link {i}"] for i in range(1, 17)], list(range(2703, 2719)))
        self.assertEqual(m["Bank Direction"], "Row")
        self.assertEqual(self.tab["qlinkBoundsData"], [])

        defs = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        for name, overlay in (("Choice", "lfoNativeChoiceDrawer1"), ("Number", "fx editor number menu")):
            field = 0 if name == "Choice" else 21
            entry = next(e for e in self.entries if e["componentData"]["name"].startswith("LFO 1 Native ")
                         and e["componentData"]["name"].split()[3].split(".")[1:3] == [str(field), "0"])
            actions = defs[entry["componentData"]["type"]]["actions"]
            self.assertEqual([(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in actions],
                             [(trigger, "Show Overlay", "Data", overlay) for trigger in ("Mouse Down", "Enter Pressed")])
        # The compact source and shape drawers must also open on the first touch.
        for caption in ("Select", "SHAPE"):
            control = defs["playEnvLfoChoice " + caption]
            menu = "playEnvLfoMenu " + caption
            actions = [(action["onAction"], action["additionalData"]) for action in control["actions"]
                       if action["handler"] == "Show Overlay"]
            self.assertCountEqual(actions, [(trigger, menu) for trigger in ("Mouse Down", "Enter Pressed")])
            children = {leaf_model(child, defs)["type"] for child in control["componentsData"]}
            self.assertTrue(children <= {"Label", "Image", "Focus"}, (caption, children))
        for field, asset in ((7, "play_sub_level_"), (18, "play_sub_pan_")):
            entry = next(e for e in self.entries if e["componentData"]["name"].startswith("LFO 1 Native ")
                         and e["componentData"]["name"].split()[3].split(".")[1:3] == [str(field), "0"])
            knob = next(c for c in defs[entry["componentData"]["type"]]["componentsData"]
                        if c["componentData"]["type"] == "Knob")
            self.assertEqual(knob["componentData"]["data"]["knobType"], "FilmStrip")
            self.assertTrue(knob["componentData"]["data"]["filmStrip"].startswith(asset))
        for field, action, count in ((3, 0, 1), (5, 1, 3), (5, 2, 3)):
            entry = next(e for e in self.entries if e["componentData"]["name"].startswith("LFO 1 Native ")
                         and e["componentData"]["name"].split()[3].split(".")[1:3] == [str(field), str(action)])
            button = next(c["componentData"]["data"] for c in defs[entry["componentData"]["type"]]["componentsData"]
                          if c["componentData"]["type"] == "Button")
            self.assertEqual((button["buttonId"], button["numButtonsInGroup"], button["gestureBehaviour"]),
                             (1 if field == 3 else action, count, "Instant"))
        self.assertTrue(defs["lfoNativeDisabled"]["ignoreMousePresses"])
        self.assertTrue((self.skins / "lfo_native_expand.png").is_file())


if __name__ == "__main__":
    unittest.main()
