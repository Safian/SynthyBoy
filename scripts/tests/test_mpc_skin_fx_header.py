"""Generated standalone FX header bindings and native popup contract."""
import json
import unittest
from itertools import combinations

import test_mpc_skin_extras as base


@unittest.skipUnless((base.SDK / "tools/gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class NativeFxHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        if NoFallbackTests.code:
            raise AssertionError(NoFallbackTests.err)
        cls.tui = NoFallbackTests.tui
        cls.definitions = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        cls.controls = cls.definitions["fxNativeEditorSurface"]["componentsData"]
        cls.root = next(t for t in cls.tui["pageData"]["tabs"] if t["tabName"] == "FX")["componentDefinition"]["componentsData"]

    def test_each_header_has_its_own_source_bound_actions(self):
        items = self.controls
        headers = [item for item in items if item["componentData"]["type"] == "fxNativeHeader"]
        replacement = {row["key"]: row["value"] for row in self.tui["pageData"]["replacementStrings"]}
        self.assertEqual(len(headers), 32)
        for item, header in enumerate(headers):
            with self.subTest(item=item):
                start = 3068 + 4 * item
                self.assertEqual(header["bounds"]["additionalInvalidatingHandles"],
                                 [f"IndexedEnabling/{item + 1}/33/Parameter 3066"])
                self.assertEqual(header["handle remapping"]["map"],
                                 [{"key": field, "value": f"Parameter {start + i}"} for i, field in
                                  enumerate(("Power", "Rack", "Page", "Remove"))]
                                 + ([{"key": "Band", "value": f"Parameter {3461 + item}"}] if item == 1 or item >= 16 else []))
                racks = ["MAIN", "BUS 1", "BUS 2"]
                if item < 13:
                    racks.extend(("L/H LOW", "L/H HIGH", "L/M/H LOW", "L/M/H MID", "L/M/H HIGH", "M/S MID", "M/S SIDE"))
                self.assertEqual(replacement[f"Parameter {start + 1}"], racks)

    def test_each_visible_header_frame_uses_only_its_exact_native_hitboxes(self):
        definitions = self.definitions
        contract = json.loads((base.REAL_EXTRAS / "fx-native-contract.json").read_text())["editor_headers"]
        header = definitions["fxNativeHeader"]
        origins = (0, contract[0]["power"][1])
        self.assertTrue(header["ignoreMousePresses"])
        manifest = json.loads((base.REAL_EXTRAS / "fx-native-contract.json").read_text())
        for panel in manifest["panels"]:
            for on in (False, True):
                layout = 7 + 2 * (panel["kind"] - 1) + int(on)
                active = []
                for child in header["componentsData"]:
                    gates = [g.split("/") for g in child["bounds"]["additionalInvalidatingHandles"]]
                    if any(g[-1] == "Parameter 3493" for g in gates):
                        continue  # compressor band frames are exercised independently
                    frames = {"Power": int(on), "Parameter 3066": 1, "Parameter 3067": layout}
                    visible = True
                    for expr in gates:
                        frame = frames[expr[-1]]
                        if expr[0] == "NonZero":
                            visible &= frame != 0
                        elif expr[0] == "IndexedEnabling":
                            visible &= frame == int(expr[1])
                        else:
                            self.assertEqual(expr[0], "RangedEnabling")
                            visible &= int(expr[1]) <= frame <= int(expr[2])
                    if visible:
                        active.append(child)
                        x, y, w, h = map(int, child["bounds"]["bounds"].split())
                        if not child["componentData"]["name"].startswith("fx_header_"):
                            self.assertGreaterEqual(min(w, h), 56)
                names = [c["componentData"]["name"] for c in active]
                self.assertIn("power", names)
                self.assertEqual(names.count("fx_header_title"), 1)
                self.assertEqual(names.count("fx_header_path"), 1)
                self.assertEqual("fx_header_notice" in names, panel["kind"] == 13)
                self.assertEqual(any(n == "rack" for n in names), on)
                self.assertEqual(any(n == "remove" for n in names), on)
                tabs = [c for c in active if c["componentData"]["name"].startswith("FX panel ")]
                self.assertEqual(len(tabs), len(panel["names"]) if len(panel["names"]) > 1 else 0)
                self.assertEqual([c["componentData"]["data"]["buttonId"] for c in tabs], list(range(len(tabs))))
                for tab in tabs:
                    self.assertEqual(tab["componentData"]["data"]["numButtonsInGroup"], 3)
                for left, right in combinations(tabs, 2):
                    lx, ly, lw, lh = map(int, left["bounds"]["bounds"].split())
                    rx, ry, rw, rh = map(int, right["bounds"]["bounds"].split())
                    self.assertTrue(lx+lw <= rx or rx+rw <= lx or ly+lh <= ry or ry+rh <= ly)
                self.assertFalse(any(n.upper() in ("MORE", "LESS", "LESS_IDLE") for n in names))
        rack = definitions["fxNativeHeaderRack"]
        modal = definitions["fx editor rack menu"]["componentsData"]
        self.assertEqual(sum(c["componentData"]["name"] == "FX Rack Popup Marker" for c in modal), 1)
        self.assertTrue(rack["disableCoarseDataWheel"])
        self.assertEqual([(action["onAction"], action["handler"], action["additionalData"]) for action in rack["actions"]],
                         [(event, "Show Overlay", "fx editor rack menu") for event in ("Mouse Down", "Enter Pressed")])

    def assert_drawer(self, children, choices, offset, domain):
        labels = {c["componentData"]["data"]["text"] for c in children if c["componentData"]["type"] == "Label"}
        buttons = {c["componentData"]["name"]: c for c in children if c["componentData"]["type"] == "Button"}
        self.assertEqual(labels, set(choices))
        self.assertEqual(len(buttons), len(choices))
        rects = []
        for index, name in enumerate(choices):
            item = buttons["Choose " + name]
            data = item["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["handleName"]),
                             (index+offset, domain, "Data"))
            x,y,w,h = map(int, item["bounds"]["bounds"].split())
            self.assertGreaterEqual(min(w,h), 56)
            self.assertTrue(0 <= x < x+w <= 1280 and 100 <= y < y+h <= 800)
            rects.append((x,y,w,h))
        for (x,y,w,h), (u,v,a,b) in combinations(rects,2):
            self.assertTrue(x+w <= u or u+a <= x or y+h <= v or v+b <= y)

    def test_rack_drawers_match_the_source_domain(self):
        full = ["MAIN", "BUS 1", "BUS 2", "L/H LOW", "L/H HIGH", "L/M/H LOW", "L/M/H MID",
                "L/M/H HIGH", "M/S MID", "M/S SIDE"]
        header = self.definitions["fxNativeHeader"]["componentsData"]
        for name, choices, span in (("rack", full, (1,13)), ("rack pool",full[:3], (14,32))):
            row = next(c for c in header if c["componentData"]["name"] == name)
            self.assertIn(f"RangedEnabling/{span[0]}/{span[1]}/33/Parameter 3066", row["bounds"]["additionalInvalidatingHandles"])
            trigger = self.definitions[row["componentData"]["type"]]
            popup = self.definitions[trigger["actions"][0]["additionalData"]]["componentsData"]
            self.assert_drawer(popup, choices, 0, len(choices))
            self.assertTrue(any(c["componentData"]["name"] == "FX Rack Popup Marker" for c in popup))

    def test_compressor_band_has_one_enabled_or_passive_cell_per_choice(self):
        manifest = json.loads((base.REAL_EXTRAS / "fx-native-contract.json").read_text())
        x, y, w, h = manifest["chain"]["band"]
        origin = manifest["editor_headers"][0]["power"][1]
        children = self.definitions["fxNativeHeader"]["componentsData"]
        band = [child for child in children if child["componentData"]["name"].startswith("Band ")]
        availability = ((), ("LOW", "MID"), ("LOW", "MID", "HIGH"), ("MID", "HIGH"))
        for mode, expected in enumerate(availability):
            active, visible = [], []
            for item in band:
                gate, = item["bounds"]["additionalInvalidatingHandles"]
                kind, *values, parameter = gate.split("/")
                self.assertEqual(parameter, "Parameter 3493")
                if kind == "NonZero":
                    enabled = mode > 0
                elif kind == "IndexedEnabling":
                    enabled = mode == int(values[0])
                else:
                    enabled = int(values[0]) <= mode <= int(values[1])
                if not enabled:
                    continue
                left, top, width, height = map(int, item["bounds"]["bounds"].split())
                self.assertTrue(x <= left < left + width <= x + w)
                self.assertTrue(y <= top + origin < top + origin + height <= y + h)
                self.assertGreaterEqual(min(width, height), 56)
                name = item["componentData"]["name"].split()[1]
                visible.append((left, name))
                if item["componentData"]["type"] == "Button":
                    active.append(name)
                    data = item["componentData"]["data"]
                    self.assertEqual((data["buttonId"], data["numButtonsInGroup"]), (("LOW", "MID", "HIGH").index(name) + 1, 4))
                    self.assertEqual(item["handle remapping"]["map"],
                                     [{"key": "Data", "value": f"WithName[{name}]/FromGesture/Band"}])
                else:
                    self.assertEqual(item["componentData"]["type"], "Image")
                    self.assertEqual(item["bounds"]["acceptsHWFocus"], "No")
            self.assertEqual(active, list(expected))
            self.assertEqual([name for _, name in sorted(visible)], ["LOW", "MID", "HIGH"] if mode else [])

    def test_chain_and_gallery_repeat_exact_cpp_rectangles_and_bindings(self):
        geometry = json.loads((base.REAL_EXTRAS / "fx-native-contract.json").read_text())["chain"]
        items = {c["componentData"]["name"]: c for c in self.controls}
        definitions = self.definitions

        def rect(item):
            return list(map(int, item["bounds"]["bounds"].split()))

        def repeated(name, n, expected, gates):
            placed = items[f"FX {name}"]
            layer = definitions[name]
            self.assertEqual((layer["version"], layer["repeats"]), (3, n))
            self.assertEqual(placed["bounds"]["acceptsHWFocus"], "Children")
            tile, = layer["componentsData"]
            self.assertEqual(tile["bounds"]["additionalInvalidatingHandles"], gates)
            self.assertEqual(tile["handle remapping"]["map"], [
                {"key": key, "value": f"Parameter {{Increment:Base={first}}}"} for key, first in expected
            ])
            x, y, w, _ = rect(placed)
            dx, dy, width, _ = rect(tile)
            self.assertEqual(w, n * width)
            return [(x + dx + col * width, y + dy) for col in range(n)]

        tiles = definitions["fxNativeChainRow"]["componentsData"]
        self.assertEqual(len(tiles), 16)
        roots = []
        for slot, tile in enumerate(tiles):
            roots.append(rect(tile)[:2])
            bindings = {entry["key"]: entry["value"] for entry in tile["handle remapping"]["map"]}
            self.assertEqual(tile["bounds"]["additionalInvalidatingHandles"], ["Present"])
            self.assertEqual(bindings["Present"], f"RangedEnabling/{slot + 1}/16/17/Parameter 3232")
            self.assertEqual(bindings["Select"], f"Parameter {3233 + slot}")
            self.assertEqual(bindings["Power"], f"Parameter {3249 + slot}")
            for enabled, name in enumerate(("Bypassed", "Powered")):
                self.assertEqual(bindings[name], f"IndexedEnabling/{enabled}/2/Parameter {3249 + slot}")
        card = definitions["fxNativeChainCard"]["componentsData"]
        power = card[0]
        self.assertEqual(power["componentData"]["type"], "Button")
        self.assertEqual(power["handle remapping"]["map"], [{"key": "Data", "value": "Power"}])
        selections = [c for c in card[1:] if c["componentData"]["type"] == "Button"]
        selection, = selections
        self.assertIs(selection, card[-1])
        self.assertEqual(selection["handle remapping"]["map"], [{"key": "Data", "value": "Select"}])
        self.assertEqual(selection["componentData"]["data"]["onImage"], "native_button_hit.png")
        for i, (x, y) in enumerate(roots):
            actual = [[x + a, y + b, w, h] for a, b, w, h in (rect(power), rect(selection))]
            self.assertEqual(actual, geometry["cards"][i])
        self.assertEqual({tuple(c["bounds"]["additionalInvalidatingHandles"]) for c in card
                          if c["componentData"]["name"] == "Selection face"},
                         {(power, state) for power in ("Powered", "Bypassed") for state in ("Selected", "Unselected")})
        for row in range(2):
            first = 3265 + row * 8
            gallery = repeated(f"fxNativeGalleryRow{row + 1}", 8, [("Data", first)],
                               [f"IndexedEnabling/1/2/Parameter {{Increment:Base={first}}}"])
            tile, name = definitions["fxNativeGalleryCard"]["componentsData"]
            self.assertEqual((tile["componentData"]["type"], name["componentData"]["data"]["type"]), ("Button", "Name"))
            for i, (x, y) in enumerate(gallery):
                _, _, w, h = rect(tile)
                self.assertEqual([x, y, w, h], geometry["gallery"][row * 8 + i][1])
        nav = [("FX Chain Back", "back", 3283), ("FX Chain Add Menu", "add", 3281),
               ("FX Chain Filtered Add", "add", 3282), ("FX Chain Page 1", "page", 3284), ("FX Chain Page 2", "page", 3284)]
        for label, pos, target in nav:
            self.assertEqual(rect(items[label]), geometry[pos])
            self.assertEqual(items[label]["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {target}"}])

        hitboxes = [geometry[key] for key in ("drag", "back", "add", "page")]
        hitboxes.extend(box for pair in geometry["cards"] for box in pair)
        hitboxes.extend(box for _, box in geometry["gallery"])
        for x, y, w, h in hitboxes:
            self.assertTrue(w >= 56 and h >= 56 and 0 <= x < x + w <= 1280 and 0 <= y < y + h <= 628)
        for (x, y, w, h), (xx, yy, ww, hh) in combinations(hitboxes, 2):
            self.assertTrue(x + w <= xx or xx + ww <= x or y + h <= yy or yy + hh <= y)
        markers = {c["componentData"]["name"]: c for c in self.root if c["componentData"]["name"].startswith("Overlay ")}
        self.assertEqual(set(markers), {"Overlay Marker fx left", "Overlay Marker fx right", "Overlay Instance fx",
                                        "Overlay Background Sentinel fx"})
        for marker in markers.values():
            x, y, w, h = rect(marker)
            for xx, yy, ww, hh in hitboxes[1:]:   # the grip is drawn on the separate overlay plane
                self.assertTrue(x + w <= xx or xx + ww <= x or y + h <= yy or yy + hh <= y)

    def test_add_uses_its_own_native_radio_rows_and_type_list(self):
        definitions = self.definitions
        chooser = definitions["fxNativeChainAdd"]
        self.assertEqual([(a["onAction"], a["handler"], a["additionalData"]) for a in chooser["actions"]],
                         [(ev, "Show Overlay", "fx chain add menu") for ev in ("Mouse Down", "Enter Pressed")])
        popup = definitions["fx chain add menu"]["componentsData"]
        self.assertEqual(sum(c["componentData"]["type"] == "MenuOverlay" for c in popup), 1)
        add, = [c for c in popup if c["componentData"]["name"] == "FX Add Popup Marker"]
        self.assertEqual(add["bounds"]["bounds"], "64 120 6 6")
        self.assertFalse(any(c["componentData"]["name"] == "FX Rack Popup Marker" for c in popup))
        replacements = {row["key"]: row["value"] for row in self.tui["pageData"]["replacementStrings"]}
        manifest = json.loads((base.REAL_EXTRAS / "fx-native-contract.json").read_text())
        names = [name for name, _ in manifest["chain"]["gallery"]]
        self.assert_drawer(popup, names, 1, len(names)+1)
        self.assertEqual(len({int(c["bounds"]["bounds"].split()[0]) for c in popup
                              if c["componentData"]["type"] == "Button"}), 2)
        self.assertEqual(replacements["Parameter 3281"], ["Select an effect"] + [name for name, _ in manifest["chain"]["gallery"]])


if __name__ == "__main__":
    unittest.main()
