#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Unit tests for standalone Master tab native controls generator."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
sys.path.insert(0, str(ROOT / "scripts"))

from native_master.master_controls import (
    MASTER_SPECS,
    MASTER_INTEGER_FIELDS,
    MASTER_LINKED_BEND,
    UNCONVERTED_CONTROLS,
    apply_master_native_controls,
    get_master_control_specs,
    get_unconverted_controls,
)


class DummyContext:
    def __init__(self, tui, params, skins_dir):
        self.skins_dir = skins_dir
        self._tui = tui
        self._index = {p["key"]: i for i, p in enumerate(params["params"])}
        self.param_count = len(self._index)

    def param_index(self, key):
        return self._index[key]

    def add_component_definition(self, name, value):
        definitions = self._tui["pageData"].setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
        definitions.append({"key": name, "value": value})

    def tab(self, title):
        for t in self._tui["pageData"]["tabs"]:
            if t.get("tabName") == title:
                return t
        raise ValueError(f"Tab {title} not found")


class MasterNativeControlsTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.skins_dir = Path(tmp.name)
        with open(PARAMS_JSON, encoding="utf-8") as f:
            self.params_doc = json.load(f)
        self.param_keys = {p["key"] for p in self.params_doc["params"]}

    def test_master_specs_use_engine_or_pad_pressure_parameters(self):
        specs = get_master_control_specs()
        self.assertEqual(len(specs), 37, "Exactly 37 standard controls specified for Master tab")
        for spec in specs:
            self.assertIn(
                spec["key"],
                self.param_keys | {"shell.pad_pressure", "shell.pad_pressure_amount"},
                f"Parameter key {spec['key']} must be an engine or pad pressure setting",
            )

    def test_unconverted_controls_inventory(self):
        unconverted = get_unconverted_controls()
        names = {u["name"] for u in unconverted}
        self.assertIn("Glide Slope Curve", names)
        self.assertNotIn("Mini Keyboard", names)

    def test_geometry_and_touch_target_bounds(self):
        specs = get_master_control_specs()
        for spec in specs:
            x, y, w, h = spec["rect"]
            self.assertGreaterEqual(x, 0, f"{spec['name']}: x must be non-negative")
            self.assertGreaterEqual(y, 110, f"{spec['name']}: y must be within tab area (>= 110)")
            self.assertLessEqual(x + w, 1280, f"{spec['name']}: right edge must fit within canvas 1280")
            self.assertLessEqual(y + h, 738, f"{spec['name']}: bottom edge must fit within tab height")
            self.assertGreaterEqual(h, 56, f"{spec['name']}: height must be at least 56px for touch target")
            self.assertGreaterEqual(w, 56, f"{spec['name']}: width must be at least 56px for touch target")

    def test_apply_master_native_controls(self):
        tui = {
            "pageData": {
                "tabs": [
                    {
                        "tabName": "Master",
                        "componentDefinition": {
                            "componentsData": [
                                {
                                    "componentData": {
                                        "name": "Overlay Marker master left",
                                        "type": "Image",
                                    }
                                }
                            ]
                        },
                    }
                ]
            }
        }
        ctx = DummyContext(tui, self.params_doc, self.skins_dir)
        created = apply_master_native_controls(tui, {}, ctx)
        self.assertEqual(created, 46, "All standard native controls, output widgets and panel buttons created")
        tab_components = tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]
        self.assertEqual(len(tab_components), 47, "Marker plus native editor, output and navigation widgets")

        # Verify specific remappings
        vol = next(c for c in tab_components if c["componentData"]["name"] == "Master Volume")
        self.assertEqual(vol["componentData"]["type"], "masterNative_dial_bi")
        # The dial is a local component definition; the instance carries only
        # its Data handle. The shared child provides the bipolar filmstrip.
        self.assertEqual(vol["componentData"]["data"], {"version": 1, "handleName": "Data"})
        self.assertEqual(vol["bounds"]["acceptsHWFocus"], "Yes")
        self.assertEqual(vol["bounds"]["whenVisible"], "Always")
        # Verify bounds are tab-relative (0..628), not raw screen coordinates
        self.assertEqual(vol["bounds"]["bounds"], "122 110 119 124")
        param_idx = ctx.param_index("master.volume")
        self.assertEqual(vol["handle remapping"]["map"][0]["value"], f"Parameter {param_idx}")
        self.assertEqual(vol["handle remapping"]["map"][1]["value"], f"WithName[VOLUME]/Parameter {param_idx}")

        # Verify all components have tab-relative bounds within tab area (0..628)
        for c in tab_components:
            if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance")):
                continue
            x, y, w, h = map(int, c["bounds"]["bounds"].split())
            self.assertGreaterEqual(y, 0, f"{c['componentData']['name']}: tab-relative y must be >= 0")
            self.assertLessEqual(y + h, 628, f"{c['componentData']['name']}: tab-relative y+h must fit in tab (<= 628)")

        legato = next(c for c in tab_components if c["componentData"]["name"] == "Master Legato")
        self.assertEqual(legato["componentData"]["type"], "masterNative_toggle_189x56")

        # Second apply is idempotent
        second_created = apply_master_native_controls(tui, {}, ctx)
        self.assertEqual(second_created, 0, "Idempotent apply adds no duplicates")

    def test_legacy_fallback_purge(self):
        tui = {
            "pageData": {
                "tabs": [
                    {
                        "tabName": "Master",
                        "componentDefinition": {
                            "componentsData": [
                                {
                                    "componentData": {
                                        "name": "Overlay Marker master left",
                                        "type": "Image",
                                    }
                                },
                                {
                                    "componentData": {
                                        "name": "Master Transpose",
                                        "type": "knobBi",
                                    },
                                    "bounds": {"bounds": "23 45 278 107"},
                                },
                                {
                                    "componentData": {
                                        "name": "Master Pitch Bend Range",
                                        "type": "knobUni",
                                    },
                                    "bounds": {"bounds": "23 196 278 107"},
                                },
                            ]
                        },
                    }
                ]
            }
        }
        ctx = DummyContext(tui, self.params_doc, self.skins_dir)
        created = apply_master_native_controls(tui, {}, ctx)
        self.assertEqual(created, 46, "All standard native controls, output widgets and panel buttons created after purge")
        tab_components = tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]
        self.assertEqual(len(tab_components), 47, "Marker plus native editor, output and navigation widgets")

        # Verify Master Transpose now has correct geometry (856 110 134 112 in tab space), not legacy 23 45 278 107
        transpose = next(c for c in tab_components if c["componentData"]["name"] == "Master Transpose")
        self.assertEqual(transpose["bounds"]["bounds"], "856 110 134 112")

    def test_subpage_exclusivity_metadata(self):
        specs = get_master_control_specs()
        main_specs = [s for s in specs if s["side"] == "main"]
        tuning_specs = [s for s in specs if s["side"] == "tuning"]
        self.assertEqual(len(main_specs), 24, "Exactly 24 controls on SideMain")
        self.assertEqual(len(tuning_specs), 13, "Exactly 13 controls on SideTuning")

        # Verify no two controls on the same side overlap
        def rect_overlaps(r1, r2):
            return not (r1[0] + r1[2] <= r2[0] or r2[0] + r2[2] <= r1[0] or
                        r1[1] + r1[3] <= r2[1] or r2[1] + r2[3] <= r1[1])

        # SideMain controls must not overlap
        for i in range(len(main_specs)):
            for j in range(i + 1, len(main_specs)):
                s1, s2 = main_specs[i], main_specs[j]
                self.assertFalse(
                    rect_overlaps(s1["rect"], s2["rect"]),
                    f"Controls {s1['name']} and {s2['name']} on SideMain must not overlap"
                )

        # SideTuning controls must not overlap
        for i in range(len(tuning_specs)):
            for j in range(i + 1, len(tuning_specs)):
                s1, s2 = tuning_specs[i], tuning_specs[j]
                self.assertFalse(
                    rect_overlaps(s1["rect"], s2["rect"]),
                    f"Controls {s1['name']} and {s2['name']} on SideTuning must not overlap"
                )

    def test_caption_separation(self):
        specs = get_master_control_specs()
        for spec in specs:
            caption = spec.get("caption")
            self.assertTrue(caption, f"{spec['name']}: must have a non-empty caption")
            self.assertEqual(caption, caption.upper(), f"{spec['name']}: caption must be uppercase")

        # Check remapping in apply
        tui = {
            "pageData": {
                "tabs": [
                    {
                        "tabName": "Master",
                        "componentDefinition": {"componentsData": []},
                    }
                ]
            }
        }
        ctx = DummyContext(tui, self.params_doc, self.skins_dir)
        apply_master_native_controls(tui, {}, ctx)
        tab_components = tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]
        for spec in specs:
            comp = next(c for c in tab_components if c["componentData"]["name"] == spec["name"])
            caption_entry = next(m for m in comp["handle remapping"]["map"] if m["key"] == "Caption")
            expected_prefix = f"WithName[{spec['caption']}]/Parameter"
            self.assertTrue(
                caption_entry["value"].startswith(expected_prefix),
                f"{spec['name']}: caption mapping {caption_entry['value']} must start with {expected_prefix}"
            )

    def test_replacement_strings_for_selectors(self):
        tui = {
            "pageData": {
                "tabs": [
                    {
                        "tabName": "Master",
                        "componentDefinition": {"componentsData": []},
                    }
                ]
            }
        }
        ctx = DummyContext(tui, self.params_doc, self.skins_dir)
        apply_master_native_controls(tui, {}, ctx)

        replacement = tui["pageData"].get("replacementStrings", [])
        self.assertTrue(len(replacement) >= 8, "All 8 native selectors must have replacementStrings entries")
        rep_dict = {r["key"]: r["value"] for r in replacement}

        # Check each selector choice list
        scale_type_idx = ctx.param_index("master.scale.type")
        self.assertIn(f"Parameter {scale_type_idx}", rep_dict)
        self.assertEqual(len(rep_dict[f"Parameter {scale_type_idx}"]), 20, "Scale type has 20 choices")

        scale_key_idx = ctx.param_index("master.scale.key")
        self.assertIn(f"Parameter {scale_key_idx}", rep_dict)
        self.assertEqual(len(rep_dict[f"Parameter {scale_key_idx}"]), 12, "Scale key has 12 choices")

        steal_idx = ctx.param_index("master.voice.steal_priority")
        self.assertIn(f"Parameter {steal_idx}", rep_dict)
        self.assertEqual(rep_dict[f"Parameter {steal_idx}"], ["Newest", "Oldest", "Highest", "Lowest", "Velocity"])

        voice_override_idx = ctx.param_index("master.voice_override")
        self.assertIn(f"Parameter {voice_override_idx}", rep_dict)
        self.assertEqual(rep_dict[f"Parameter {voice_override_idx}"], ["Kill", "Steal"])

    def test_conditional_enabling_and_gating(self):
        tui = {
            "pageData": {
                "tabs": [
                    {
                        "tabName": "Master",
                        "componentDefinition": {"componentsData": []},
                    }
                ]
            }
        }
        ctx = DummyContext(tui, self.params_doc, self.skins_dir)
        ctx.overlay_param = 2675
        apply_master_native_controls(tui, {}, ctx)

        tab_components = tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]
        link_idx = ctx.param_index("master.bend_down_link")
        mpe_idx = ctx.param_index("master.mpe.on")

        # BEND DOWN must carry IndexedEnabling/0/2 on master.bend_down_link
        bend_down = next(c for c in tab_components if c["componentData"]["name"] == "Master Bend Range Down")
        self.assertEqual(bend_down["bounds"]["version"], 2)
        self.assertEqual(bend_down["bounds"]["showWhenDataModelInvalid"], "Hide")
        handles = bend_down["bounds"]["additionalInvalidatingHandles"]
        self.assertIn(f"IndexedEnabling/0/2/Parameter {link_idx}", handles)
        self.assertIn("IndexedEnabling/1/3/Parameter 2887", handles)

        # Inactive MPE controls stay visible and retain their readouts.
        for name in ("Master MPE Bend Range", "Master MPE Bend to X"):
            widget = next(c for c in tab_components if c["componentData"]["name"] == name)
            self.assertEqual(widget["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/2/3/Parameter 2887"])
            self.assertEqual(widget["bounds"]["acceptsHWFocus"], "Children")
            mapping = {m["key"]: m["value"] for m in widget["handle remapping"]["map"]}
            for state in (0, 1):
                self.assertEqual(mapping[f"NativeActiveGate{state}"], f"IndexedEnabling/{state}/2/Parameter {mpe_idx}")

    def test_release_skin_has_one_editor_side_gate_per_control(self):
        import tempfile
        import test_mpc_skin_extras as skin

        if not (skin.SDK / "tools" / "gen_ui.py").is_file():
            self.skipTest("MPC SDK unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            skins = skin.generate_skins(Path(tmp), ROOT / "scripts/overlay-skin/mpc-ui-pages-overlay.json")
            self.assertEqual(skin.run_apply(skins, skin.REAL_EXTRAS), (0, ""))
            tui = json.loads((skins / "TUI.json").read_text())
            from PIL import Image
            with Image.open(skins / "master_peak_scale.png") as plate:
                self.assertEqual(plate.size, (100, 128))
                for box in ((0, 3, 36, 17), (0, 19, 36, 33), (0, 38, 36, 52), (0, 77, 36, 91),
                            (38, 102, 64, 124), (68, 102, 94, 124)):
                    self.assertGreater(len(plate.crop(box).getcolors()), 1)
        master = next(tab for tab in tui["pageData"]["tabs"] if tab["tabName"] == "Master")
        widgets = master["componentDefinition"]["componentsData"]
        definitions = {entry["key"]: entry["value"] for entry in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        indices = {param["key"]: i for i, param in enumerate(self.params_doc["params"])}
        indices.update({"shell.pad_pressure": 2673, "shell.pad_pressure_amount": 2674})
        frames = {"main": 1, "tuning": 2}
        for spec in (*MASTER_SPECS, MASTER_LINKED_BEND):
            with self.subTest(control=spec["name"]):
                matches = [w for w in widgets if w["componentData"]["name"] == spec["name"]]
                self.assertEqual(len(matches), 1)
                widget = matches[0]
                gates = widget["bounds"]["additionalInvalidatingHandles"]
                expected_gates = [f"IndexedEnabling/{frames[spec['side']]}/3/Parameter 2887"]
                if condition := spec.get("condition"):
                    expected_gates.append(f"IndexedEnabling/{condition['choice']}/{condition['numChoices']}/Parameter {indices[condition['param']]}")
                self.assertEqual(gates, expected_gates)
                self.assertEqual(widget["bounds"]["showWhenDataModelInvalid"], "Hide")
                x, y, width, height = spec["rect"]
                self.assertEqual(widget["bounds"]["bounds"], f"{x} {y - 110} {width} {height}")
                binding = {link["key"]: link["value"] for link in widget["handle remapping"]["map"]}
                self.assertEqual(binding["Data"], f"Parameter {indices[spec['key']]}")
                self.assertEqual(binding["Caption"], f"WithName[{spec['caption']}]/Parameter {indices[spec['key']]}")
                definition = definitions[widget["componentData"]["type"]]
                if "NativeActiveGate0" in binding:
                    self.assertTrue(spec["key"].startswith("master.mpe."))
                    off, on = definition["componentsData"]
                    self.assertEqual(off["bounds"]["additionalInvalidatingHandles"], ["NativeActiveGate0"])
                    self.assertEqual(on["bounds"]["additionalInvalidatingHandles"], ["NativeActiveGate1"])
                    inactive = definitions[off["componentData"]["type"]]
                    self.assertEqual(inactive["actions"], [])
                    self.assertTrue(inactive["ignoreMousePresses"])
                    self.assertTrue(inactive["disableCoarseDataWheel"])
                    definition = definitions[on["componentData"]["type"]]
                actions = definition["actions"]
                children = [child["bounds"] for child in definition["componentsData"]]
                if spec["widget"].startswith("dial_") and spec["key"] not in MASTER_INTEGER_FIELDS:
                    self.assertTrue(all(b["boundsType"] == "Relative" for b in children))
                    sizes = {"parent.width": width, "parent.height": height}

                    def edge(expression):
                        anchor, separator, inset = expression.strip().partition(" - ")
                        position = sizes[anchor] if anchor in sizes else int(anchor)
                        return position - int(inset) if separator else position

                    self.assertEqual([tuple(map(edge, b["relativeBounds"].split(","))) for b in children], [
                        (4, 0, width - 4, 24), (4, 24, width - 4, height - 24),
                        (4, height - 24, width - 4, height),
                    ])
                    self.assertGreaterEqual(height - 48, 56)
                    self.assertGreaterEqual(width - 8, 56)
                else:
                    for child in children:
                        cx, cy, cw, ch = map(int, child["bounds"].split())
                        self.assertGreaterEqual(min(cx, cy), 0)
                        self.assertLessEqual(cx + cw, width)
                        self.assertLessEqual(cy + ch, height)
                if spec["widget"] == "toggle":
                    self.assertEqual({(a["onAction"], a["handler"], a["handleName"]) for a in actions},
                                     {("Mouse Down", "Toggle Switch", "Data"), ("Enter Pressed", "Toggle Switch", "Data")})
                else:
                    popup = "fx editor choice menu" if spec["widget"] == "selector" else "fx editor number menu"
                    trigger = "Mouse Down" if spec["widget"] in ("selector", "number") or spec["key"] in MASTER_INTEGER_FIELDS else "Double Click"
                    self.assertEqual({(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in actions},
                                     {(trigger, "Show Overlay", "Data", popup),
                                      ("Enter Pressed", "Show Overlay", "Data", popup)})
                    self.assertIn(popup, definitions)

        by_name = {widget["componentData"]["name"]: widget for widget in widgets}
        for i, (name, rect) in enumerate((("MAIN", "300 4 160 56"), ("TUNING & MPE", "468 4 220 56")), 1):
            button = by_name[f"Master Panel {name}"]
            self.assertEqual(button["bounds"]["bounds"], rect)
            self.assertEqual(button["bounds"]["additionalInvalidatingHandles"], ["NonZero/Parameter 2887"])
            self.assertEqual(button["componentData"]["type"], "Button")
            data = button["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"]), (i, 3))
            self.assertEqual(button["handle remapping"]["map"],
                             [{"key": "Data", "value": f"WithName[{name}]/FromGesture/Parameter 3460"}])
        main_gate = "IndexedEnabling/1/3/Parameter 2887"

        def output(name, rect, links, gates=(), focus=False):
            item = by_name[name]
            self.assertEqual(item["bounds"]["bounds"], rect)
            self.assertEqual(item["bounds"]["additionalInvalidatingHandles"], [main_gate, *gates])
            self.assertEqual(item["bounds"]["acceptsHWFocus"], "Yes" if focus else "No")
            self.assertEqual({m["key"]: m["value"] for m in item["handle remapping"]["map"]}, links)
            return definitions[item["componentData"]["type"]]

        peaks = output("Master Peak Meters", "14 166 100 128", {
            "PeakL": "Parameter 2977", "PeakR": "Parameter 2978",
            "HoldL": "Parameter 2979", "HoldR": "Parameter 2980",
        })
        for side in ("focussed", "unfocussed"):
            self.assertEqual(peaks["backgroundData"][side]["image"], "master_peak_scale.png")
        meters = [c["componentData"]["data"] for c in peaks["componentsData"] if c["componentData"]["type"] == "Meter"]
        self.assertEqual([(m["direction"], m["peakHandle"], m["peakHoldHandle"]) for m in meters],
                         [("Up", "PeakL", "HoldL"), ("Up", "PeakR", "HoldR")])
        voice = output("Master Voice Meter", "400 552 382 44", {
            "Peak": "Parameter 2981", "Data": "Parameter 2982",
        })
        self.assertEqual([skin.leaf_model(c, definitions)["type"] for c in voice["componentsData"]], ["Meter", "Label"])
        self.assertEqual(voice["componentsData"][0]["componentData"]["data"]["direction"], "Right")
        limiter = output("Master Output Limiter", "122 238 96 56", {
            "Data": "Parameter 2983", "Caption": "WithName[LIMIT]/Parameter 2983",
        }, focus=True)
        clips = [output("Master Clip " + label, "14 110 100 56", {"Data": "Parameter 2985"},
                        (f"IndexedEnabling/{state}/2/Parameter 2984",), focus=True)
                 for state, label in enumerate(("Off", "On"))]
        self.assertNotEqual(clips[0]["backgroundData"], clips[1]["backgroundData"])
        for definition in (limiter, *clips):
            self.assertEqual({(a["onAction"], a["handler"], a["handleName"]) for a in definition["actions"]},
                             {("Mouse Down", "Toggle Switch", "Data"), ("Enter Pressed", "Toggle Switch", "Data")})


if __name__ == "__main__":
    unittest.main()
