#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/97_no_fallback.py: the overlay skin carries NO host fallback widgets (owner decision, 2026-11-21).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path

import test_mpc_skin_extras as base
from test_mpc_backgrounds import bg_mod
from native_ui_params import LEGACY_EXTRAS, MIXER_STEREO_BASE, NATIVE_PARAMS
from validate_synthyboy_skin import check_legacy_display_bindings, validate_package

SHARED_JSON = base.ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"
OVERLAY_LAYOUT = base.ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
BASE_DECODED_BYTES = 9072 + 1234 * 190 * 4 + 408 * 56 * 4        # marker/instance art, compact Play art, selector frame

NATIVE_NAME_PREFIXES = (
    "Native Caption ", "Overlay Background Sentinel ", "Env Native ", "Filter ", "LFO 1 ", "Mixer ", "FX ", "Mod ",
    "Master ", "Voice ", "Macro ", "Macros ", "Osc Native ", "Play Native EnvLfo ", "Play Native OSC ", "Play Expanded ",
)
NATIVE_TYPE_PREFIXES = (
    "masterNative", "voiceNative", "modNative", "macroCard",
    "fxNative", "oscNative", "nativeModulationEditor",
)


def is_native_component(component):
    data = component["componentData"]
    name = data.get("name", "")
    kind = data.get("type", "")
    return (name.startswith(NATIVE_NAME_PREFIXES)
            or name.endswith((" Power", " Smooth Native"))
            or kind.startswith(NATIVE_TYPE_PREFIXES))


def png_size(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    return struct.unpack(">II", data[16:24])


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class NoFallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root, OVERLAY_LAYOUT)
        cls.code, cls.err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"]
        cls.params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        cls.definitions = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        cls.background_assets = {view: asset for view, _, asset in bg_mod.BACKGROUND_TABS}

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_mod_source_board_keeps_its_native_targets_and_picker_title(self):
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == "Mod")
        entries = {c["componentData"]["name"]: c for c in tab["componentDefinition"]["componentsData"]}
        chips = [c for name, c in entries.items() if name.startswith("Mod Native Board Chip ")]
        self.assertEqual(len(chips), 17)
        for chip in chips:
            self.assertEqual(chip["componentData"]["type"], "Button")
            self.assertTrue(chip["bounds"]["additionalInvalidatingHandles"][0].startswith("IndexedEnabling/0/5/Parameter "))
        title_type = entries["Mod Native Picker Title"]["componentData"]["type"]
        children = self.definitions[title_type]["componentsData"]
        self.assertEqual([child["componentData"]["type"] for child in children], ["Label"])

    def test_native_skin_has_no_legacy_live_display_consumers(self):
        page = self.tui["pageData"]
        qlinks = json.loads((self.skins / "Q-Links.json").read_bytes())
        first = json.loads((base.ROOT / "docs/params.json").read_bytes())["count"]
        check_legacy_display_bindings(page, qlinks)
        for section in ("tabs", "componentDefinitions"):
            with self.subTest(section=section):
                binding = {"handle remapping": {"map": [{"key": "Data", "value": f"FromGesture/Parameter {first}"}]}}
                with self.assertRaisesRegex(ValueError, "legacy display binding"):
                    check_legacy_display_bindings(dict(page, **{section: [binding]}), qlinks)
        qlinks["Program Mode Q-Links"]["Q-Link 1"] = first + 35
        with self.assertRaisesRegex(ValueError, "legacy display binding in native Q-Links"):
            check_legacy_display_bindings(page, qlinks)

    def test_native_package_rejects_fallbacks_in_the_finished_skin(self):
        with tempfile.TemporaryDirectory() as tmp:
            package = Path(tmp) / "Safian - VST - SynthyBoy"
            skins = package / "Plugin Skins"
            shutil.copytree(self.skins, skins)
            subprocess.run([sys.executable, str(base.ROOT / "scripts/gen-mpc-manifest.py"),
                            "--plugin-dir", str(package), "--allow-no-binary"], check=True, capture_output=True)
            validate_package(package)
            path = skins / "TUI.json"
            original = path.read_bytes()
            page = json.loads(original)["pageData"]
            ctrl = page["tabs"][0]["componentDefinition"]["componentsData"][0]["componentData"]
            for kind, data in (("Knob", {"knobType": "GenericLegacy"}), ("missingSelector", {})):
                with self.subTest(kind=kind):
                    ctrl.update(type=kind, data=data)
                    path.write_text(json.dumps({"pageData": page}))
                    with self.assertRaisesRegex(ValueError, "custom dial|unresolved"):
                        validate_package(package)
            path.write_bytes(original)
            variant = skins / "TUI - MPC Key 61.json"
            variant.write_text("{}")
            with self.assertRaisesRegex(ValueError, "shadows"):
                validate_package(package)
            variant.unlink()
            image = skins / "overlay_canvas.png"
            image.write_bytes(b"broken")
            with self.assertRaises(OSError):
                validate_package(package)
            image.unlink()
            with self.assertRaises(FileNotFoundError):
                validate_package(package)

    def test_each_tab_contains_its_detection_pictures_and_configured_background(self):
        tabs = self.tui["pageData"]["tabs"]
        self.assertEqual(len(tabs), 24)
        views = {v["tab"]: v for v in self.layout["views"]}
        labels = {p["name"]: p.get("label", p["name"]) for p in json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))["pages"]}
        for name, view in views.items():
            tab = next(t for t in tabs if t["tabName"] == labels[name])
            items = list(base.standalone_editor_entries(tab, self.definitions))
            image = self.background_assets.get(view["name"], "")
            for state in ("focussed", "unfocussed"):
                self.assertEqual(tab["componentDefinition"]["backgroundData"][state]["image"], image, name)
            expected = ["Overlay Marker %s left" % view["name"], "Overlay Marker %s right" % view["name"], "Overlay Instance %s" % view["name"]]
            if name == "Play":
                expected += ["Play Osc Select", "Play Env Select", "Play LFO Select", "Play Env Attack", "Play Env Decay", "Play Env Sustain", "Play Env Release", "Play LFO Rise", "Play LFO Smooth", "Play LFO Smooth Off"]
                expected.extend(f"Play Native OSC {source} power" for source in ("Sub", "Osc A", "Osc B", "Osc C", "Noise"))
            required_native = {
                "Env": {"Env Native Attack", "Env Native Decay", "Env Native Sustain", "Env Native Release",
                        "Env Native Attack Curve", "Env Native Decay Curve", "Env Native Release Curve",
                        "Env Native Targets", "Env Native Delay", "Env Native Hold", "Env Native Beats", "Env Native Retrig"},
                "Filter": {"Filter Native Editor Surface", "Filter Native PZ Toolbar F1", "Filter Native PZ Toolbar F2"},
                "Filter 2": {"Filter Native Editor Surface", "Filter Native PZ Toolbar F1", "Filter Native PZ Toolbar F2"},
                "Note": {"Filter Native Curve Surface", "Filter Native Glide note"},
                "Velo": {"Filter Native Curve Surface", "Filter Native Glide velocity"},
            }.get("Env" if name.startswith("Env ") else name, set())   # every Env sub-tab carries the same native controls
            actual_names = {c["componentData"]["name"] for c in items}
            native_names = {c["componentData"]["name"] for c in items if is_native_component(c)}
            self.assertEqual((set(expected) | required_native) - actual_names, set(), name)
            self.assertEqual(actual_names - set(expected) - required_native - native_names, set(), name)
            expected_types = {"Image", "playOscSelector", "playModSelector", "compactNativeDial",
                              "compactNativeRiseDial", "compactNativeSmoothWithIndicator", "compactNativeSmoothReadout"} if name == "Play" else {"Image"}
            native_types = {c["componentData"]["type"] for c in items if is_native_component(c)}
            self.assertEqual({c["componentData"]["type"] for c in items}, expected_types | native_types, "%s: only documented/native controls remain" % name)
            self.assertEqual(self.tui["pageData"]["tabs"].count(tab), 1)
            self.assertEqual(tab["qlinkBoundsData"], [], "%s: no Q-Link frames, so the host draws no highlight or badges over the plugin page" % name)

    def test_no_component_definition_is_left(self):
        defs = {d["key"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertTrue({"playOscSelector", "playModSelector", "oscNativeSourceChoices", "fx editor choice menu", "compactNativeDial", "compactNativeRiseDial"}.issubset(defs))

    def test_every_retained_component_type_and_builtin_payload_is_host_resolvable(self):
        # MPC rejects the complete TUI when an instance names neither a
        # documented built-in nor a local definition.  This gate caught the
        # native final-page candidates' unresolved Selector/switch/Number
        # aliases before they reached a device.
        builtins = {"Image", "Label", "Button", "Knob", "Slider", "Meter", "Fader", "Text",
                    "Separator", "MenuOverlay", "KnobOverlay", "NumericOverlay", "Envelope",
                    "Path", "Plotter", "Indicator", "HaloDisplay", "XYPad"}
        defs = {d["key"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        unresolved = []
        for tab in self.tui["pageData"]["tabs"]:
            for component in tab["componentDefinition"]["componentsData"]:
                data = component["componentData"]
                typ = data["type"]
                if typ not in builtins and typ not in defs:
                    unresolved.append((tab["tabName"], data["name"], typ))
                if typ == "Button":
                    self.assertEqual(data["data"].get("version"), 2, data["name"])
                    self.assertIn("onImage", data["data"], data["name"])
                    self.assertIn("offImage", data["data"], data["name"])
                if typ == "Label" and data.get("data", {}).get("type") == "Text":
                    self.assertEqual(data["data"].get("handleName"), "Data", data["name"])
                if typ in defs:
                    self.assertEqual(data.get("data"), {"version": 1, "handleName": "Data"}, data["name"])
        self.assertEqual(unresolved, [])

    def test_static_captions_have_a_read_only_host_model_and_respect_layout_gates(self):
        captions = json.loads((base.REAL_EXTRAS / "static-native-captions.json").read_text())
        tabs = self.tui["pageData"]["tabs"]
        labels = {c["componentData"]["name"]: c for tab in tabs
                  for c in base.standalone_editor_entries(tab, self.definitions)}
        def check(view, prefix, gates=()):
            for index, row in enumerate(captions[view]):
                name = f"{prefix} {index}"
                with self.subTest(caption=name):
                    c = labels[name]
                    data = c["componentData"]["data"]
                    self.assertEqual(c["componentData"]["type"], "Label")
                    self.assertEqual((data["type"], data["text"], data["handleName"]), ("Text", row["text"], "Data"))
                    self.assertEqual(c["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 0"}])
                    self.assertEqual(c["bounds"]["acceptsHWFocus"], "No")
                    self.assertEqual(c["bounds"].get("additionalInvalidatingHandles", []), list(gates))
                    if gates:
                        self.assertEqual(c["bounds"]["showWhenDataModelInvalid"], "Hide")

        for view in ("env1", "mod", "voice"):
            check(view, f"Native Caption {view}")
        from native_ui_params import MASTER_VIEW, mixer_peek_index
        handle = mixer_peek_index(len(self.params)) + MASTER_VIEW
        check("master_header", "Native Caption master header")
        check("master", "Native Caption master", (f"IndexedEnabling/1/3/Parameter {handle}",))
        check("master_tuning", "Native Caption master tuning", (f"IndexedEnabling/2/3/Parameter {handle}",))
        for view, names in (
            ("master", ("Native Caption master header 0", "Master Scale Input")),
            ("voice", ("Native Caption voice 3", "Voice Step Value")),
        ):
            tab = next(tab for tab in tabs if tab["tabName"].lower() == view)
            entries = tab["componentDefinition"]["componentsData"]
            markers = [entry for entry in entries
                       if entry["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
            for name in names:
                x, y, w, h = map(int, labels[name]["bounds"]["bounds"].split())
                for marker in markers:
                    mx, my, mw, mh = map(int, marker["bounds"]["bounds"].split())
                    # The overlay covers one extra pixel around every marker.
                    self.assertTrue(x > mx + mw or mx > x + w or y > my + mh or my > y + h, name)
        for layout, view in enumerate(("lfo1", "lfo1", "lfo3", "lfo4", "lfo4", "lfo6"), 1):
            check(view, f"LFO 1 Native Group {layout}", (f"IndexedEnabling/{layout}/7/Parameter 2833",))

    def test_the_q_proxies_are_on_all_24_tabs(self):
        first_proxy = len(self.params) + 4 + 32 + 2 + 1 + 3 + 24
        self.assertEqual(len(self.qlinks), 24)
        for entry in self.qlinks:
            self.assertEqual([entry["Q-Links"]["Q-Link %d" % n] for n in range(1, 17)], [first_proxy + i for i in range(16)], entry)
            self.assertEqual(entry["Bank Direction"], "Row")

    def test_the_markers_and_the_instance_picture_are_what_the_plugin_reads(self):
        from PIL import Image

        index = len(self.params) + 4 + 32 + 2
        views = {v["name"]: v for v in self.layout["views"]}
        for tab in self.tui["pageData"]["tabs"]:
            for c in base.standalone_editor_entries(tab, self.definitions):
                name = c["componentData"]["name"]
                if name.startswith("Play Expanded "):
                    self.assertTrue(c["bounds"]["additionalInvalidatingHandles"][0].endswith("/7/Parameter 3537"), name)
                    continue
                if name.startswith("Play ") and (name.startswith("Play Native EnvLfo ") or name.endswith(" Select") or name.startswith("Play Native OSC ") or name in ("Play Env Attack", "Play Env Decay", "Play Env Sustain", "Play Env Release", "Play LFO Rise", "Play LFO Smooth", "Play LFO Smooth Off")):
                    self.assertIn("RangedEnabling/0/18/33/Parameter %d" % (index), json.dumps(c["bounds"]))
                    continue
                if c["componentData"]["type"] == "nativeModulationEditor":
                    self.assertIn("/9/Parameter 3529", c["bounds"]["additionalInvalidatingHandles"][0])
                    continue
                if name.startswith("Mixer Native Peek "):
                    view = 1 if name.endswith("Filter") else 2
                    gates = [f"IndexedEnabling/{view}/3/Parameter 3349"]
                    if view == 1:
                        gates.append("RangedEnabling/1/2/5/Parameter 2850")
                    self.assertEqual(c["bounds"]["additionalInvalidatingHandles"], gates)
                    continue
                if name.startswith("Mixer Native ") or c["componentData"]["type"] == "macroCard" \
                        or tab["tabName"] == "Voice" and c["componentData"]["type"].startswith("voiceNative"):
                    self.assertIn(f"RangedEnabling/0/18/33/Parameter {index}", c["bounds"]["additionalInvalidatingHandles"])
                    continue
                if name.startswith("LFO 1 Native "):
                    if name.startswith("LFO 1 Native Detail "):
                        expected = 3285
                    else:
                        expected = 3196 if name.startswith("LFO 1 Native Editor ") else 2833
                    self.assertTrue(any(gate.endswith(f"/Parameter {expected}") for gate in c["bounds"]["additionalInvalidatingHandles"]), name)
                    continue
                if name.startswith("Filter Native "):
                    gate = 3496 if name.startswith("Filter Native PZ Toolbar ") else 2850
                    self.assertIn(f"Parameter {gate}", c["bounds"]["additionalInvalidatingHandles"][0])
                    continue
                if name.startswith("Mod Native "):
                    gate = c["bounds"]["additionalInvalidatingHandles"][0]
                    expected = 2944 if name.startswith("Mod Native Mode ") or name.endswith((" Group", " ADD")) else 2888
                    self.assertTrue(gate.endswith(f"/Parameter {expected}"), name)
                    continue
                if name.startswith("Env Native "):
                    self.assertNotIn("RangedEnabling", json.dumps(c["bounds"]), "%s is host-owned on the native page" % name)
                    continue
                if not name.startswith(("Overlay Instance ", "Overlay Marker ", "Overlay Background Sentinel ")):
                    continue
                self.assertFalse(c["bounds"].get("additionalInvalidatingHandles"), name)
                data = c["componentData"]["data"]
                x, y, width, height = map(int, c["bounds"]["bounds"].split())
                with Image.open(self.skins / data["image"]) as art:
                    if name.startswith("Overlay Instance "):
                        region = views[name.split()[-1]]["instance"]
                        inst = self.layout["instance"]
                        self.assertEqual(c["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {index}"}])
                        self.assertEqual((data["imageType"], data["numFrames"]), ("FilmStrip", inst["frames"]))
                        self.assertEqual(art.size, (width, height * inst["frames"]))
                        self.assertEqual((x, y, width, height), tuple(region[k] for k in ("x", "y", "w", "h")))
                    elif name.startswith("Overlay Marker "):
                        _, _, view, side = name.split()
                        marker = views[view]["markers"][("left", "right").index(side)]
                        self.assertEqual((x, y, width, height), tuple(marker[k] for k in ("x", "y", "w", "h")))
                        self.assertEqual(art.convert("RGB").getcolors(), [(width * height, tuple(bytes.fromhex(marker["rgb"])))])
                        self.assertEqual(art.size, (width, height))
                    else:
                        self.assertEqual(data["imageType"], "Regular")
                        self.assertEqual(art.size, (width, height))

    def test_mixer_native_controls_use_real_parameters_and_keep_dry_racks_available(self):
        tab = next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == "Mix")
        native = {c["componentData"]["name"].removeprefix("Mixer Native "): c
                  for c in tab["componentDefinition"]["componentsData"]
                  if c["componentData"]["name"].startswith("Mixer Native ")
                  and not c["componentData"]["name"].startswith("Mixer Native Peek ")}
        handles = {p["key"]: f"Parameter {i}" for i, p in enumerate(self.params)}
        choices = {f"Parameter {i}": p["choices"] for i, p in enumerate(self.params) if "choices" in p}
        replacements = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        modal = f"RangedEnabling/0/18/33/Parameter {len(self.params) + 38}"
        native_base = len(self.params) + 82
        self.assertEqual(native_base, 2719)

        def remap(c):
            return {h["key"]: h["value"] for h in c["handle remapping"]["map"]}

        strips = (("SUB", "sub"), ("A", "osc1"), ("B", "osc2"), ("C", "osc3"), ("NOISE", "noise"),
                  ("F1", "filter1"), ("F2", "filter2"), ("BUS 1", "bus1"), ("BUS 2", "bus2"), ("MAIN", "main"))
        strip_kinds = ["mixerStripSource"] * 5 + ["mixerStripFilter"] * 2 + ["mixerStripBus"] * 2 + ["mixerStripMaster"]
        for i, (caption, source) in enumerate(strips):
            c = native[f"{caption} Strip"]
            self.assertEqual(c["componentData"]["type"], strip_kinds[i] + "Frame")
            self.assertEqual(c["bounds"]["additionalInvalidatingHandles"], [modal, f"IndexedEnabling/0/6/Parameter {native_base + 856}"])
            self.assertEqual(c["bounds"]["showWhenDataModelInvalid"], "Hide")
            self.assertEqual(tuple(map(int, c["bounds"]["bounds"].split())), (10 + 127 * i, 10, 115, 550))
            rm = remap(c)
            for field, handle in enumerate(("PeakL", "PeakR", "HoldL", "HoldR", "ClipL", "ClipR")):
                param = f"Parameter {native_base + MIXER_STEREO_BASE + 10 * field + i}"
                self.assertEqual(rm[handle], param)
                leaf = ("Clip " if handle.startswith("Clip") else "Meter ") + handle[-1]
                self.assertEqual(remap(native[f"{caption} Strip {leaf}"])[handle], param)
            self.assertEqual(rm["Level"], handles[f"{source}.level" if i < 9 else "master.volume"])
            self.assertEqual(rm["Name"], handles[f"{source}.level" if i < 9 else "master.volume"])
            self.assertEqual(rm["Mute"], f"Parameter {native_base + 840 + i}")
            if i < 9:
                destination = handles[f"{source}.destination"]
                self.assertEqual(rm["Destination"], destination)
                self.assertEqual(replacements[destination], choices[destination])
                self.assertEqual(rm["Solo"], f"Parameter {native_base + 830 + i}")
                self.assertEqual(rm["SoloVisual"], f"Parameter {native_base + 857 + i}")
            else:
                self.assertEqual(rm["FxEnable"], handles["main.fx_bypass"])
                self.assertEqual(rm["FxVisual"], f"Parameter {native_base + 866}")
            self.assertEqual(rm["MuteVisual"], f"Parameter {native_base + 867 + i}")
            pairs = (("FX Enable", "FxVisual", "FxEnable"), ("Master Mute", "MuteVisual", "Mute")) if i == 9 else (
                ("Solo", "SoloVisual", "Solo"), ("Mute", "MuteVisual", "Mute"))
            for title, state_key, action_key in pairs:
                face = native[f"{caption} Strip {title} Visual"]
                action = native[f"{caption} Strip {title} Button"]
                self.assertEqual(face["componentData"]["type"], "Knob")
                film = face["componentData"]["data"]
                self.assertEqual((film["knobType"], film["dragOrientation"], film["numFrames"]), ("FilmStrip", "None", 5))
                self.assertEqual(face["bounds"]["acceptsHWFocus"], "No")
                self.assertEqual(remap(face)[film["handleName"]], rm[state_key])
                self.assertEqual(action["componentData"]["type"], "Button")
                self.assertEqual(remap(action)["Data"], rm[action_key])
                self.assertEqual(face["bounds"]["bounds"], action["bounds"]["bounds"])


        # Persistent 6-tab bar (MIXER, FLT 1, FLT 2, MAIN FX, BUS 1, BUS 2) gated together during full-screen dialogs
        all_tabs = ("MIXER", "FLT 1", "FLT 2", "MAIN FX", "BUS 1", "BUS 2")
        for i, caption in enumerate(all_tabs):
            btn = native[f"{caption} Peek"]
            self.assertEqual(btn["componentData"]["type"], "Button")
            data = btn["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["gestureBehaviour"]), (i, 6, "Instant"))
            self.assertEqual(remap(btn), {"Data": f"Parameter {native_base}"})
            self.assertEqual(btn["bounds"]["additionalInvalidatingHandles"], [modal])
            self.assertEqual(btn["bounds"]["bounds"], f"{10 + 210 * i} 566 210 56")
        vis = native["Tabs"]
        self.assertEqual(vis["componentData"]["type"], "Image")
        self.assertEqual(vis["bounds"]["bounds"], "10 566 1260 56")
        self.assertEqual(vis["componentData"]["data"]["numFrames"], 6)
        self.assertEqual(remap(vis), {"Data": f"Parameter {native_base + 856}"})
        self.assertEqual(vis["bounds"]["additionalInvalidatingHandles"], [modal])

        menu = definitions["mixerDestination56"]["actions"]
        self.assertEqual([(a["onAction"], a["handler"], a["additionalData"]) for a in menu],
                         [(trigger, "Show Overlay", "fx editor choice menu") for trigger in ("Mouse Down", "Enter Pressed")])

    def test_mixer_peek_reuses_the_complete_standalone_editor_trees(self):
        tabs = {t["tabName"]: t["componentDefinition"]["componentsData"] for t in self.tui["pageData"]["tabs"]}
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        mixer = {c["componentData"]["name"]: c for c in tabs["Mix"]}
        for title, index, kind, dock_kind in (
            ("Filter", 1, "filterNativeEditorSurface", "mixerPeekFilterEditorSurface"),
            ("FX", 2, "fxNativeEditorSurface", "mixerPeekFxEditorSurface"),
        ):
            dock = mixer[f"Mixer Native Peek {title}"]
            standalone, = [c for c in tabs[title] if c["componentData"]["type"] == kind]
            self.assertEqual(dock["componentData"]["type"], dock_kind)
            self.assertEqual(standalone["bounds"]["bounds"], "0 0 1280 628")
            self.assertEqual(dock["bounds"]["bounds"], "0 0 1280 564")
            self.assertEqual(dock["bounds"]["acceptsHWFocus"], "Children")
            gates = [f"IndexedEnabling/{index}/3/Parameter 3349"]
            if title == "Filter":
                gates.append("RangedEnabling/1/2/5/Parameter 2850")
            self.assertEqual(dock["bounds"]["additionalInvalidatingHandles"], gates)
            root = 2850 if index == 1 else 3231
            self.assertEqual(standalone["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {root}"}])
            self.assertEqual(dock["handle remapping"]["map"], [{"key": "Data", "value": "Parameter 3349"}])
            self.assertTrue(definitions[kind]["ignoreMousePresses"])
            self.assertTrue(definitions[dock_kind]["ignoreMousePresses"])
        f = {c["componentData"]["name"] for c in definitions["mixerPeekFilterEditorSurface"]["componentsData"]}
        self.assertTrue({"Filter Native Power", "Filter Native Model", "Filter Native Cutoff 0"} <= f)
        self.assertTrue({"Filter Native F1", "Filter Native F2", "Filter Native dual", "Filter Native NOTE",
                         "Filter Native VELO", "Filter Native Edit PZ Curve"}.isdisjoint(f))
        fx = {c["componentData"]["type"] for c in definitions["mixerPeekFxEditorSurface"]["componentsData"]}
        self.assertTrue({"fxNativeChainRow", "fxNativeHeader", "mixerPeekGalleryRow1"} <= fx)
        cell_group, = [entry for entry in definitions["mixerPeekFxEditorSurface"]["componentsData"]
                       if entry["componentData"]["name"] == "FX parameter cells"]
        states = definitions[cell_group["componentData"]["type"]]["componentsData"]
        self.assertIn("mixerPeekFxParameterCells", {state["componentData"]["type"] for state in states})
        self.assertIn("fxNativeCellChoice", {child["componentData"]["type"]
                                             for child in definitions["mixerPeekFxParameterCells"]["componentsData"]})

    def test_expanded_play_mounts_the_same_editor_trees_and_three_independent_close_actions(self):
        tabs = {t["tabName"]: t["componentDefinition"]["componentsData"] for t in self.tui["pageData"]["tabs"]}
        play = {entry["componentData"]["name"]: entry for entry in tabs["Play"]}
        groups = (
            ("Oscillator Controls", "Osc", "oscNativeSurface", "0 0 1280 628", "RangedEnabling/1/2"),
            ("Envelope Controls", "Env", "envNativeFullFields", "0 0 1280 628", "IndexedEnabling/3"),
            ("Envelope Selector", "Env", "envNativeSelectorStrip", "164 12 528 56", "IndexedEnabling/3"),
            ("LFO Controls", "LFO", "lfoNativeFullEditor", "0 0 1280 628", "RangedEnabling/5/6"),
        )
        first_marker = next(i for i, name in enumerate(play) if name.startswith("Overlay Marker"))
        for label, tab, kind, rect, gate in groups:
            name = "Play Expanded " + label
            entry = play[name]
            self.assertEqual(entry["componentData"]["type"], kind)
            # the selector strip belongs to the Play page's expanded editor only: no Env sub-tab has one (the hardware sub-tab names the envelope)
            self.assertEqual(len([c for c in tabs[tab] if c["componentData"]["type"] == kind]), 0 if kind == "envNativeSelectorStrip" else 1)
            self.assertEqual(entry["bounds"]["bounds"], rect)
            self.assertEqual(entry["bounds"]["additionalInvalidatingHandles"], [f"{gate}/7/Parameter 3537"])
            self.assertLess(list(play).index(name), first_marker)
            self.assertTrue(self.definitions[kind]["ignoreMousePresses"])
        for label, root, action, rect in (("Oscillator", 1, 3538, "1148 116 120 56"),
                                          ("Envelope", 3, 3539, "58 12 96 56"),
                                          ("LFO", 5, 3540, "968 210 96 56")):
            entry = play[f"Play Expanded {label} Close"]
            self.assertEqual(entry["componentData"]["type"], "oscNativeSetting_action")
            self.assertEqual(entry["bounds"]["bounds"], rect)
            self.assertEqual(entry["bounds"]["acceptsHWFocus"], "Yes")
            self.assertEqual(entry["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{root}/7/Parameter 3537"])
            self.assertEqual({b["key"]: b["value"] for b in entry["handle remapping"]["map"]},
                             {"Data": f"FromGesture/Parameter {action}", "Caption": f"Parameter {action}"})

    def test_env_native_scalar_cells_and_targets_use_their_exclusive_page_handles(self):
        env = next(tab for tab in self.tui["pageData"]["tabs"] if tab["tabName"] == "Env")
        definitions = {d["key"]: d["value"] for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        components = base.standalone_editor_entries(env, definitions)
        scalars = {c["componentData"]["name"].removeprefix("Env Native "): c for c in components
                   if c["componentData"]["type"] in {"envNativeDial", "envNativeCurve", "envNativeBeatsSwitch", "envNativeRetrigSwitch", "envNativeTargets"}}
        specs = ("Attack", "Decay", "Sustain", "Release", "Attack Curve", "Decay Curve", "Release Curve", "Delay", "Hold", "Beats", "Retrig")
        expected_x = (14, 125, 236, 347, 474, 584, 694, 821, 933, 1045, 1157)
        self.assertEqual(set(scalars), set(specs) | {"Targets"})
        for caption, parameter, x in zip((*specs, "Targets"), (*range(2930, 2941), 2942), (*expected_x, 1072)):
            control = scalars[caption]
            self.assertEqual(control["bounds"]["additionalInvalidatingHandles"], ["NonZero/Parameter 2929"])
            self.assertEqual(control["bounds"]["showWhenDataModelInvalid"], "Hide")
            remap = {h["key"]: h["value"] for h in control["handle remapping"]["map"]}
            self.assertEqual(remap["Data"], f"Parameter {parameter}")
            rx, ry, w, h = map(int, control["bounds"]["bounds"].split())
            self.assertEqual((rx, ry), (x, 12 if caption == "Targets" else 482))
            definition = definitions[control["componentData"]["type"]]
            for child in definition["componentsData"]:
                cx, cy, cw, ch = map(int, child["bounds"]["bounds"].split())
                self.assertTrue(0 <= cx <= cx + cw <= w and 0 <= cy <= cy + ch <= h)
            actions = [(a["onAction"], a["handler"], a["handleName"], a["additionalData"])
                       for a in definition["actions"] if a["handler"] != "Q-Link"]
            if caption in {"Beats", "Retrig", "Targets"}:
                self.assertEqual(actions, [])
                button, = definition["componentsData"]
                self.assertEqual(button["componentData"]["type"], "Button")
                data = button["componentData"]["data"]
                self.assertEqual((data["gestureBehaviour"], data["handleName"], data["buttonId"],
                                  data["numButtonsInGroup"]), ("Instant", "Data", 1, 1))
                self.assertEqual(data["onImage"], f"env_{caption.lower()}_on.png")
                self.assertEqual(data["offImage"], data["onImage"] if caption == "Targets" else
                                 f"env_{caption.lower()}_off.png")
            else:
                self.assertEqual(actions, [(trigger, "Show Overlay", "Data", "fx editor number menu")
                                           for trigger in ("Double Click", "Enter Pressed")])
        popup = definitions["fx editor number menu"]["componentsData"]
        self.assertEqual(sum(c["componentData"]["type"] == "NumericOverlay" for c in popup), 1)
        marker, = [c for c in popup if c["componentData"]["name"] == "Native Modal Markers"]
        self.assertEqual(marker["bounds"]["bounds"], "16 120 30 6")

    def test_env_native_selector_slots_bind_the_dedicated_guarded_choice(self):
        tabs = {tab["tabName"]: list(base.standalone_editor_entries(tab, self.definitions))
                for tab in self.tui["pageData"]["tabs"]}
        definitions = {d["key"]: d["value"]
                       for d in self.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        self.assertEqual([c for name in ("Env", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6") for c in tabs[name] if c["componentData"]["type"] == "envNativeSelectorCell"], [],
                         "the Env sub-tabs have no selector cells: the hardware sub-tab names the envelope")
        # the cells live in the strip of the Play page's expanded envelope editor (origin 68, 12)
        cells = definitions["envNativeSelectorStrip"]["componentsData"]
        self.assertEqual(len(cells), 6)
        definition = definitions["envNativeSelectorCell"]
        children = definition["componentsData"]
        art = children[0]["componentData"]
        self.assertEqual(art["type"], "Image")
        self.assertEqual((art["data"]["imageType"], art["data"]["numFrames"], art["data"]["handleName"]),
                         ("FilmStrip", 31, "Frame"))
        self.assertEqual(png_size(self.skins / art["data"]["image"]), (88, 56 * 31))
        from PIL import Image
        with Image.open(self.skins / art["data"]["image"]) as strip:
            frame = lambda index: strip.crop((0, 56 * index, 88, 56 * (index + 1)))
            self.assertIsNone(frame(0).getbbox())
            self.assertIsNotNone(frame(1).getbbox())
            from components.native.radio_button.radio_button import render_segmented_button_art
            for number in range(1, 7):
                for variant, (caption, selected) in enumerate(((str(number), False), (f"{number} ·", False),
                                                               (str(number), True), (f"{number} ·", True), ("+", False))):
                    expected = render_segmented_button_art(caption, selected, 88, 56)
                    self.assertEqual(frame(1 + (number - 1) * 5 + variant).tobytes(), expected.tobytes(),
                                     (number, variant))


        buttons = children[1:]
        self.assertEqual(len(buttons), 6)
        frame_base = len(self.params) + 82 + 21   # append only, after the entire Mixer block
        for slot, cell in enumerate(cells):
            self.assertEqual(cell["bounds"]["bounds"], f"{88 * slot} 0 88 56")
            remap = {item["key"]: item["value"] for item in cell["handle remapping"]["map"]}
            frame_handle = f"Parameter {frame_base + slot}"
            self.assertEqual(remap["Data"], "Parameter 2941")
            self.assertEqual(remap["Frame"], frame_handle)
            self.assertEqual(cell["bounds"]["additionalInvalidatingHandles"], ["NonZero/Parameter 2929", f"NonZero/{frame_handle}"])
            available = {}
            for env, c in enumerate(buttons, 1):
                payload = c["componentData"]["data"]
                self.assertEqual(c["componentData"]["type"], "Button")
                self.assertEqual((payload["handleName"], payload["buttonId"], payload["numButtonsInGroup"]),
                                 ("Data", env - 1, 6))
                gates = c["bounds"]["additionalInvalidatingHandles"]
                self.assertEqual(len(gates), 1)
                low, high = 1 + (env - 1) * 5, env * 5
                self.assertEqual(remap[gates[0]], f"RangedEnabling/{low}/{high}/31/{frame_handle}")
                for visible_frame in range(low, high + 1):
                    self.assertNotIn(visible_frame, available, "two native buttons would claim the same cell")
                    available[visible_frame] = payload["buttonId"]
            self.assertEqual(available, {value: (value - 1) // 5 for value in range(1, 31)})

    def test_all_packaged_parameter_handles_fit_the_shell_table(self):
        end = len(self.params) + LEGACY_EXTRAS + NATIVE_PARAMS
        # Read the serialized files so SDK compound bindings with duplicate JSON keys are included.
        raw = (self.skins / "TUI.json").read_text(encoding="utf-8")
        handles = {int(index) for index in re.findall(r"\bParameter (-?\d+)\b", raw)}
        self.assertTrue(handles)
        self.assertEqual(sorted(index for index in handles if not 0 <= index < end), [])

    def test_the_decoded_size_of_the_whole_skin_is_the_documented_one(self):
        # decoded size = sum of width x height x 4 over every PNG in the Plugin Skins folder (the host keeps a picture decoded: docs/MPC.md "Host fallback")
        files = sorted(self.skins.glob("*.png"))
        total = sum(w * h * 4 for w, h in (png_size(p) for p in files))
        # Native standalone controls intentionally retain their SDK filmstrips
        # and definitions; the old no-fallback byte total is no longer valid.
        backgrounds = set(self.background_assets.values())
        self.assertGreater(total, BASE_DECODED_BYTES + len(backgrounds) * 1280 * 628 * 4)
        self.assertGreaterEqual(len(files), 75 + len(backgrounds))
        self.assertLess((self.skins / "TUI.json").stat().st_size, base.TUI_JSON_BYTES_MAX)
        doc = (base.ROOT / "docs" / "MPC.md").read_text(encoding="utf-8")
        self.assertIn("9 072", doc)     # the number of the documentation: it must follow the skin
        self.assertNotIn("40 588 576 bytes decoded", doc)

    def test_every_picture_file_is_referenced_and_every_reference_exists(self):
        import re
        text = (self.skins / "TUI.json").read_text(encoding="utf-8")
        refs = set(re.findall(r'"([^"]+\.png)"', text))
        self.assertEqual(refs, {p.name for p in self.skins.glob("*.png")})

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(self.skins.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "34 tabs; at most 32" not in l]
        self.assertEqual(errors, [], report)

    def test_the_step_is_inactive_without_the_overlay_variant(self):
        spec = importlib.util.spec_from_file_location("t_97_no_fallback", base.REAL_EXTRAS / "97_no_fallback.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        tui = {"pageData": {"tabs": [{"componentDefinition": {"componentsData": [{"componentData": {"name": "a knob"}}]}}], "componentDefinitions": {"localComponentDefinitions": [{"key": "knobUni"}]}}}
        before = json.dumps(tui)
        for meta in (None, {}, {"variant": "other"}):
            module.apply(tui, {}, types.SimpleNamespace(meta=meta, skins_dir=Path(self._tmp.name)))
            self.assertEqual(json.dumps(tui), before)


if __name__ == "__main__":
    unittest.main()
