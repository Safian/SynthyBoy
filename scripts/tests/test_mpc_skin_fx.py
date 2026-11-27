#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/20_fx.py (native controls throughout standalone FX on tab FX).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import os
import subprocess
import sys
import unittest
from itertools import combinations
from pathlib import Path

import test_mpc_skin_extras as base

HERE = Path(__file__).resolve().parent
EXTRAS_DIR = HERE.parent / "mpc-skin-extras"
sys.path.insert(0, str(HERE.parent))

spec = importlib.util.spec_from_file_location("fx_extra", EXTRAS_DIR / "20_fx.py")
fx_extra = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fx_extra)


class MockContext:
    def __init__(self, tab_data, params_map):
        self._tab = tab_data
        self._params_map = params_map
        self.meta = None

    def tab(self, name):
        if name == self._tab.get("tabName"):
            return self._tab
        raise KeyError("Tab %s not found" % name)

    def param_index(self, key):
        if key in self._params_map:
            return self._params_map[key]
        raise KeyError("Unknown key %s" % key)


def _rect(comp):
    return tuple(int(v) for v in comp["bounds"]["bounds"].split())


class FxSkinExtraTest(unittest.TestCase):
    def setUp(self):
        self.params_map = {
            "fx.order.1": 100,
            "fx.order.2": 101,
            "fx.order.3": 102,
            "fx.order.4": 103,
            "fx.order.5": 104,
            "fx.order.6": 105,
            "fx.order.7": 106,
            "fx.order.8": 107,
            "fx.order.9": 108,
            "fxfilter.on": 110,
            "distortion.on": 111,
            "compressor.on": 112,
            "eq.on": 113,
            "chorus.on": 114,
            "delay.on": 115,
            "reverb.on": 116,
        }

        self.tab = {
            "version": 1,
            "tabName": "FX",
            "fnKeyIndex": 3,
            "fnKeySubIndex": 0,
            "componentDefinition": {
                "version": 4,
                "componentsData": [],
            },
            "qlinkBoundsData": ["23 301 278 323", "343 301 278 323"],
        }

        self.tui = {
            "version": 1,
            "pageData": {
                "tabs": [self.tab],
                "componentDefinitions": {"localComponentDefinitions": []},
            },
        }

        self.qlinks = {
            "Screen Mode Q-Links": {
                "map": [
                    {
                        "Tab": 4,  # fnKeyIndex + 1
                        "SubTab": 1,
                        "Q-Links": {},
                    }
                ]
            }
        }

    def test_apply_creates_unobscured_controls_and_qlinks(self):
        ctx = MockContext(self.tab, self.params_map)
        fx_extra.apply(self.tui, self.qlinks, ctx)

        items = self.tab["componentDefinition"]["componentsData"]
        self.assertEqual(len(items), 16)

        defs = {
            d["key"]: d["value"]
            for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
        }
        self.assertIn(fx_extra.SELECTOR_DEF, defs)
        self.assertIn(fx_extra.TOGGLE_DEF, defs)

        # Child geometry inside each local definition must fit within KNOB_W x KNOB_H.
        for def_name in (fx_extra.SELECTOR_DEF, fx_extra.TOGGLE_DEF):
            for child in defs[def_name]["componentsData"]:
                cx, cy, cw, ch = _rect(child)
                self.assertGreaterEqual(cx, 0, def_name)
                self.assertGreaterEqual(cy, 0, def_name)
                self.assertLessEqual(cx + cw, fx_extra.KNOB_W, def_name)
                self.assertLessEqual(cy + ch, fx_extra.KNOB_H, def_name)

        by_name = {c["componentData"]["name"]: c for c in items}

        # Universal lower aperture in tab coordinates:
        # FxChainBar (8..96), FxSlotEditor header (112..168), and max FxDisplay/FxBandBoard
        # (Splitter L/M/H ends at 416) all sit above y=424; knobArea ends at y=620.
        rects = []
        for i in range(1, 10):
            name = "FX Order %d" % i
            self.assertIn(name, by_name)
            c = by_name[name]
            self.assertEqual(c["componentData"]["type"], fx_extra.SELECTOR_DEF)
            self.assertEqual(c["componentData"]["data"], {"version": 1, "handleName": "Data"})
            self.assertEqual(c["handle remapping"]["map"][0]["value"], "Parameter %d" % (100 + i - 1))
            rx, ry, rw, rh = _rect(c)
            self.assertGreaterEqual(rw, 56)
            self.assertGreaterEqual(rh, 56)
            self.assertGreaterEqual(ry, 424, "%s must sit below every display/band board" % name)
            self.assertLessEqual(ry + rh, 620, "%s must stay inside tab knobArea" % name)
            rects.append((name, (rx, ry, rw, rh)))

        switches = [
            ("FX Filter On", "fxfilter.on", 1, 1),
            ("FX Distortion On", "distortion.on", 1, 2),
            ("FX Compressor On", "compressor.on", 1, 3),
            ("FX EQ On", "eq.on", 1, 4),
            ("FX Chorus On", "chorus.on", 1, 5),
            ("FX Delay On", "delay.on", 1, 6),
            ("FX Reverb On", "reverb.on", 1, 7),
        ]
        for name, key, row, col in switches:
            self.assertIn(name, by_name)
            c = by_name[name]
            self.assertEqual(c["componentData"]["type"], fx_extra.TOGGLE_DEF)
            self.assertEqual(c["componentData"]["data"], {"version": 1, "handleName": "Data"})
            self.assertEqual(c["handle remapping"]["map"][0]["value"], "Parameter %d" % self.params_map[key])
            expected_bounds = "%d %d %d %d" % (
                fx_extra.COLS[col],
                fx_extra.ROWS[row],
                fx_extra.KNOB_W,
                fx_extra.KNOB_H,
            )
            self.assertEqual(c["bounds"]["bounds"], expected_bounds)
            rx, ry, rw, rh = _rect(c)
            self.assertGreaterEqual(rw, 56)
            self.assertGreaterEqual(rh, 56)
            self.assertGreaterEqual(ry, 424, "%s must sit below every display/band board" % name)
            self.assertLessEqual(ry + rh, 620, "%s must stay inside tab knobArea" % name)
            rects.append((name, (rx, ry, rw, rh)))

        # Verify Chorus On specifically moved out of the Distortion WAVESHAPER region (y=196..303 tab)
        chorus_rect = _rect(by_name["FX Chorus On"])
        self.assertGreaterEqual(chorus_rect[1], 424)

        # Verify no two native controls overlap each other
        for i, (na, ra) in enumerate(rects):
            for nb, rb in rects[i + 1:]:
                overlap = (
                    ra[0] < rb[0] + rb[2]
                    and rb[0] < ra[0] + ra[2]
                    and ra[1] < rb[1] + rb[3]
                    and rb[1] < ra[1] + ra[3]
                )
                self.assertFalse(overlap, "%s overlaps %s" % (na, nb))

        # Verify ALL 16 Q-Links are uniquely mapped
        qmap = self.qlinks["Screen Mode Q-Links"]["map"][0]["Q-Links"]
        mapped = [qmap["Q-Link %d" % i] for i in range(1, 17)]
        self.assertEqual(len(set(mapped)), 16)
        self.assertNotIn(-1, mapped)

    def test_replaces_existing_legacy_controls_without_ranged_enabling(self):
        # Simulate gen_ui.py pre-populating legacy 4x4 grid entries with RangedEnabling
        self.tab["componentDefinition"]["componentsData"].append({
            "version": 2,
            "componentData": {"version": 1, "name": "FX Chorus On", "type": "toggle", "data": {}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter 0"}]},
            "bounds": {
                "version": 2,
                "acceptsHWFocus": "Yes",
                "showWhenDataModelInvalid": "Show",
                "whenVisible": "Always",
                "boundsType": "Absolute",
                "bounds": "343 196 278 107",
                "additionalInvalidatingHandles": ["RangedEnabling/0/18/33/Parameter 999"],
            },
        })
        ctx = MockContext(self.tab, self.params_map)
        fx_extra.apply(self.tui, self.qlinks, ctx)

        chorus = next(c for c in self.tab["componentDefinition"]["componentsData"] if c["componentData"]["name"] == "FX Chorus On")
        self.assertEqual(chorus["componentData"]["type"], fx_extra.TOGGLE_DEF)
        self.assertEqual(_rect(chorus)[1], fx_extra.ROWS[1])
        self.assertEqual(chorus["bounds"]["additionalInvalidatingHandles"], [])

    def test_apply_missing_tab_handled_gracefully(self):
        other_tab = {
            "version": 1,
            "tabName": "Other",
            "componentDefinition": {"componentsData": []},
        }
        ctx = MockContext(other_tab, self.params_map)
        fx_extra.apply(self.tui, self.qlinks, ctx)


class FxNativeManifestTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SYNTHYBOY_FX_NATIVE_HOST"), "set SYNTHYBOY_FX_NATIVE_HOST to the built host test")
    def test_contract_matches_the_cpp_export(self):
        host = os.environ["SYNTHYBOY_FX_NATIVE_HOST"]
        result = subprocess.check_output([host, "--skin-contract"])
        self.assertEqual(result, fx_extra.CONTRACT.read_bytes(),
                         f"regenerate {fx_extra.CONTRACT.name} with fx_native_ui_host --skin-contract")


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class FxNativeGeneratedSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        cls.tui = NoFallbackTests.tui
        cls.skins = NoFallbackTests.skins
        cls.qlinks = NoFallbackTests.qlinks
        cls.assert_apply = (NoFallbackTests.code, NoFallbackTests.err)
        cls.manifest = json.loads(fx_extra.CONTRACT.read_text(encoding="utf-8"))
        cls.params = NoFallbackTests.params
        cls.definitions = {obj["key"]: obj["value"] for obj in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        cls.fx = next(tab for tab in cls.tui["pageData"]["tabs"] if tab["tabName"] == "FX")

    def test_every_live_cell_matches_the_contract_and_has_one_input_owner(self):
        self.assertEqual(self.assert_apply, (0, ""))
        items = self.definitions["fxNativeParameterCells"]["componentsData"]
        for surface in ("fxNativeEditorSurface",):
            controls = {c["componentData"]["name"]: c for c in self.definitions[surface]["componentsData"]}
            _, add_y, _, add_h = _rect(controls["FX Chain Add Menu"])
            cells = controls["FX parameter cells"]
            states = self.definitions[cells["componentData"]["type"]]["componentsData"]
            self.assertEqual(len(states), 2, "active and bypassed editors each own one cell subtree")
            bounds = _rect(cells)
            self.assertGreaterEqual(bounds[1], add_y + add_h)
        self.assertNotIn("mixerPeekFxEditorSurface", self.definitions, "the Mixer peek surface is gone")
        cells = next(c for c in self.definitions["fxNativeEditorSurface"]["componentsData"]
                     if c["componentData"]["name"] == "FX parameter cells")
        layer_x, layer_y, _, _ = _rect(cells)
        self.assertTrue(self.definitions["fxNativeParameterCells"]["ignoreMousePresses"])
        native = [c for c in items if c["componentData"]["name"].startswith("FX Cell ")]
        marker_names = {c["componentData"]["name"] for c in self.fx["componentDefinition"]["componentsData"]
                        if c["componentData"]["name"].startswith("Overlay ")}
        self.assertEqual(marker_names, {"Overlay Marker fx left", "Overlay Marker fx right", "Overlay Instance fx", "Overlay Background Sentinel fx"})
        self.assertEqual(self.fx["qlinkBoundsData"], [])
        match = next(m for m in self.qlinks if m["Tab"] == self.fx["fnKeyIndex"] + 1
                     and m["SubTab"] == self.fx["fnKeySubIndex"] + 1)
        self.assertEqual(match["Bank Direction"], "Row")
        self.assertEqual([match["Q-Links"][f"Q-Link {i}"] for i in range(1, 17)], list(range(2703, 2719)))

        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        self.assertEqual(len(self.params), 2637)
        self.assertEqual(len(self.manifest["domains"]), 48)
        self.assertEqual({f"Parameter {d}": replacements[f"Parameter {d}"] for d in range(2778, 2826)},
                         {f"Parameter {2778 + i}": choices for i, choices in enumerate(self.manifest["domains"])})
        observed = {}
        for c in native:
            data = c["componentData"]
            layout, cell, kind = map(int, data["name"].removeprefix("FX Cell ").split("."))
            slot = cell - 1
            key = (layout, slot, kind)
            self.assertNotIn(key, observed, "two native hit targets for one state")
            self.assertEqual(c["bounds"]["additionalInvalidatingHandles"],
                             [f"IndexedEnabling/{layout}/{len(self.manifest['layouts'])+1}/Parameter 2777", f"IndexedEnabling/{kind}/52/Parameter {2761 + slot}"])
            self.assertEqual(c["bounds"]["showWhenDataModelInvalid"], "Hide")

            widget = {1: "Uni", 2: "Switch", 51: "Bi"}.get(kind)
            if widget is None:
                domain = kind - 3
                choices = self.manifest["domains"][domain]
                widget = "ReadOnly" if len(choices) == 1 else "Number" if domain in (18, 19) else "Choice"
                target = 2778 + domain
            else:
                target = 2745 + slot
            self.assertEqual(data["type"], "fxNativeCell" + widget, key)
            self.assertEqual(c["bounds"]["acceptsHWFocus"], "No" if widget == "ReadOnly" else "Yes")
            self.assertEqual(c["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {target}"}], key)

            top, rows, *side = self.manifest["layouts"][layout - 1]
            row, col = divmod(slot, 4)
            row_h = (620 - top) // rows
            x, y, w, h = _rect(c)
            x += layer_x
            y += layer_y
            if side and side[0]:   # display left, controls right: two cells to a line at x 644, a 16 px lane above each row
                lines = side[1:5]
                row_y = top + sum(16 + 72 * lines[r] for r in range(row)) + 16
                self.assertEqual((x, y, w, h), (644 + (col % 2) * 318, row_y + 72 * (col // 2), 310, 72))
                self.assertLessEqual(y + h, 620)
            else:
                self.assertEqual((x, y, w, h), (8 + col * 318, top + 16 + row * row_h, 310, min(max(72, row_h - 16), self.manifest["cell_max_h"])))
            self.assertTrue(0 <= x < x + w <= 1280 and 176 <= y < y + h <= 628 and min(w, h) >= 56)
            observed[key] = _rect(c)
        live = {tuple(row) for row in self.manifest["widgets"]}
        self.assertEqual(set(observed), live)
        self.assertTrue({l for l, _, _ in observed}.issubset(set(range(1, len(self.manifest["layouts"])+1))))
        rects = {(layout, slot): rect for (layout, slot, _), rect in observed.items()}
        for ((layout, _), (x, y, w, h)), ((other_layout, _), (ox, oy, ow, oh)) in combinations(rects.items(), 2):
            if layout == other_layout:
                self.assertTrue(x + w <= ox or ox + ow <= x or y + h <= oy or oy + oh <= y,
                                "different live slots must never overlap")
        for tab in self.tui["pageData"]["tabs"]:
            if tab["tabName"] != "FX":
                self.assertFalse(any(c["componentData"]["type"].startswith("fxNativeCell")
                                     for c in tab["componentDefinition"]["componentsData"]), tab["tabName"])

    def test_native_art_and_actions_preserve_control_semantics(self):
        defs = self.definitions
        self.assertEqual({key for key in defs if key.startswith("fxNativeCell")},
                         {"fxNativeCellUni", "fxNativeCellBi", "fxNativeCellSwitch", "fxNativeCellChoice",
                          "fxNativeCellNumber", "fxNativeCellReadOnly"})
        for key, polarity in (("fxNativeCellUni", "UniPolar"), ("fxNativeCellBi", "BiPolar")):
            children = [base.leaf_model(c, defs) for c in defs[key]["componentsData"]]
            self.assertEqual([c["type"] for c in children], ["Knob", "Label", "Label"])
            self.assertEqual(children[0]["data"]["knobType"], "FilmStrip")
            self.assertTrue((self.skins / children[0]["data"]["filmStrip"]).is_file())
            self.assertEqual(children[1]["data"]["type"], "Name")
            self.assertEqual(children[2]["data"]["type"], "Value")
            self.assertEqual(defs[key]["actions"], [])
        uni = defs["fxNativeCellUni"]["componentsData"][0]["componentData"]["data"]["filmStrip"]
        bi = defs["fxNativeCellBi"]["componentsData"][0]["componentData"]["data"]["filmStrip"]
        self.assertNotEqual(uni, bi, "bipolar artwork must not replace the unipolar asset")
        toggle = defs["fxNativeCellSwitch"]["componentsData"][0]["componentData"]
        self.assertEqual(toggle["type"], "Button")
        self.assertEqual((toggle["data"]["buttonId"], toggle["data"]["numButtonsInGroup"], toggle["data"]["gestureBehaviour"]),
                         (1, 1, "Instant"))
        for key, popup in (("fxNativeCellChoice", "fx editor choice menu"), ("fxNativeCellNumber", "fx editor number menu")):
            definition = defs[key]
            cells = [base.leaf_model(c, defs) for c in definition["componentsData"]]
            self.assertEqual({c["data"]["type"] for c in cells if c["type"] == "Label"}, {"Name", "Value"})
            self.assertTrue(any(c["type"] == "Image" for c in cells))
            self.assertEqual([(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in definition["actions"]],
                             [(trigger, "Show Overlay", "Data", popup) for trigger in ("Mouse Down", "Enter Pressed")])
        self.assertEqual(defs["fxNativeCellReadOnly"]["actions"], [])
        self.assertTrue(defs["fxNativeCellReadOnly"]["ignoreMousePresses"])
        self.assertTrue(defs["fxNativeCellReadOnly"]["disableCoarseDataWheel"])

    def test_fx_popups_have_their_own_markers_and_sdk_ordered_keys(self):
        from PIL import Image

        spec = importlib.util.spec_from_file_location("extras_writer", HERE.parent / "apply-mpc-skin-extras.py")
        writer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(writer)
        text = (self.skins / "TUI.json").read_text(encoding="utf-8")
        ordered = writer.load_json(text)
        reformatted = writer.dump_json(ordered, indent_step=2)
        self.assertEqual(json.loads(text, object_pairs_hook=list), json.loads(reformatted, object_pairs_hook=list))
        self.assertEqual(list(ordered), ["pageData"])
        ordered_defs = {entry["key"]: entry["value"] for entry in ordered["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        for name, kind in (("fx editor choice menu", "MenuOverlay"), ("fx editor number menu", "NumericOverlay")):
            children = ordered_defs[name]["componentsData"]
            self.assertEqual([c["componentData"]["type"] for c in children], [kind, "Image"])
            data = children[0]["componentData"]["data"]
            self.assertEqual([key.split(writer.DUP)[0] for key in data][:3], ["version", "version", "actions"])
            self.assertEqual(data["handleName"], "Data")
            self.assertEqual(_rect(children[0]), (0, 0, 1280, 800))
            self.assertEqual(_rect(children[1]), (16, 120, 30, 6))
            payload = children[1]["componentData"]["data"]
            self.assertEqual((payload["version"], payload["imageType"]), (2, "Regular"))
            with Image.open(self.skins / payload["image"]) as png:
                self.assertEqual((png.mode, png.size), ("RGBA", (30, 6)))
                patches = ((0, "fe356cff"), (6, "00000000"), (12, "24c7ffff"),
                           (18, "00000000"), (24, "90ed43ff"))
                for x, rgba in patches:
                    self.assertEqual(png.crop((x, 0, x + 6, 6)).tobytes(), bytes.fromhex(rgba) * 36)
        for name, definition in self.definitions.items():
            if name.startswith("fx editor") or name == "fx chain add menu":
                continue
            self.assertFalse(any(c["componentData"]["name"].startswith("FX Native Popup Marker")
                                 for c in definition.get("componentsData", [])), name)


if __name__ == "__main__":
    unittest.main()
