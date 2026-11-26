# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
PAGES_JSON = ROOT / "docs" / "kick-pages.json"
LAYOUT_JSON = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
OVERLAY_LAYOUT_SHARED = ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"
EXTRAS_DIR = ROOT / "scripts" / "mpc-skin-extras"
GEN = ROOT / "scripts" / "gen-mpc-ui.py"
RUN_GEN_UI = ROOT / "scripts" / "run-gen-ui.py"
SDK = Path(os.environ.get("MPC_SDK") or Path.home() / ".local/share/local-worker/ext/mpc-sdk")

from scripts.native_ui_params import (OSC_DETAIL, OSC_DETAIL_CELLS, OSC_ROUTING_BY_SOURCE, OSC_SOURCE,
                                      OSC_VISIBILITY, native_base_index)
from components.native.selector.selector import (
    OSC_COMPACT_LEVEL_COMPONENT_TYPE,
    OSC_COMPACT_PAN_COMPONENT_TYPE,
    OSC_COMPACT_ROUTING_COMPONENT_TYPE,
    OSC_LEVEL_CONTROL_NAME,
    OSC_LEVEL_LAYER,
    OSC_PAN_CONTROL_NAME,
    OSC_PAN_LAYER,
    OSC_ROUTING_CHOICES,
    OSC_ROUTING_COMPONENT_TYPE,
    OSC_ROUTING_CONTROL_NAME,
    OSC_ROUTING_OVERLAY_BG_IMAGE,
    OSC_ROUTING_OVERLAY_NAME,
    OSC_ROUTING_PARAM_KEYS,
    STANDALONE_COMPACT_OSC_LEVEL_RECT,
    STANDALONE_COMPACT_OSC_PAN_RECT,
    STANDALONE_COMPACT_OSC_ROUTING_RECT,
    STANDALONE_OSC_ROUTING_RECT,
    build_standalone_osc_routing_choice_overlay,
    build_standalone_osc_routing_component,
    build_compact_osc_routing_definition,
)


def _load_extra(filename, module_name):
    path = EXTRAS_DIR / filename
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _decode_rgb_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    idat = b""
    info = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            info = struct.unpack(">IIBBBBB", body[:13])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    w, h, depth, colour, _, _, interlace = info
    assert depth == 8 and colour in (2, 6) and interlace == 0
    ch = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * ch
    prev = bytearray(stride)
    pixels = set()
    p = 0
    for _ in range(h):
        flt = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if flt == 1:
                line[i] = (line[i] + a) & 255
            elif flt == 2:
                line[i] = (line[i] + b) & 255
            elif flt == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif flt == 4:
                pa = abs(b - c)
                pb = abs(a - c)
                pc = abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        prev = line
        for x in range(w):
            pixels.add(tuple(line[x * ch:x * ch + 3]))
    return w, h, pixels


def _rect_tuple(s):
    return tuple(int(v) for v in s.split())


def _rects_overlap(a, b):
    return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]


class TestNativeOscRouting(unittest.TestCase):
    def _make_base_tui(self):
        return {
            "pageData": {
                "componentDefinitions": {
                    "localComponentDefinitions": [
                        {
                            "key": "sliderWithFocusMenu",
                            "value": {
                                "version": 2,
                                "actions": [
                                    {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
                                    {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"},
                                    {"version": 1, "onAction": "Enter Pressed", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"},
                                ],
                                "componentsData": [
                                    {
                                        "version": 2,
                                        "componentData": {"version": 1, "name": "focus", "type": "Image", "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": ""}},
                                        "handle remapping": {"version": 1, "map": []},
                                        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 278 107"},
                                    },
                                    {
                                        "version": 2,
                                        "componentData": {"version": 1, "name": "slider", "type": "Slider", "data": {"version": 1, "handleName": "Data"}},
                                        "handle remapping": {"version": 1, "map": []},
                                        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always", "boundsType": "Absolute", "bounds": "15 11 248 77"},
                                    },
                                ],
                            },
                        }
                    ]
                },
                "tabs": [
                    {
                        "tabName": name,
                        "additionalInvalidatingHandles": [],
                        "componentDefinition": {
                            "additionalInvalidatingHandles": [],
                            "componentsData": [
                                {
                                    "version": 2,
                                    "componentData": {"version": 1, "name": "Existing Menu", "type": "sliderWithFocusMenu", "data": {"version": 1, "handleName": "Data"}},
                                    "bounds": {"version": 1, "bounds": "0 0 100 50"},
                                    "handle remapping": {"version": 1, "map": []},
                                }
                            ],
                        },
                    }
                    for name in ("Osc A", "Osc B", "Osc C", "Sub", "Noise")
                ],
            }
        }

    def test_params_json_matches_per_source_choices(self):
        params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        by_key = {p["key"]: p for p in params}
        for key in OSC_ROUTING_PARAM_KEYS:
            self.assertIn(key, by_key)
            self.assertEqual(tuple(by_key[key]["choices"]), OSC_ROUTING_CHOICES, key)

    def test_choice_overlay_and_cancellation_contract(self):
        source = {
            "actions": [
                {"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data", "additionalData": ""},
                {"version": 1, "onAction": "Double Click", "handler": "Show Overlay", "handleName": "Data", "additionalData": "menu overlay"},
            ],
            "componentsData": [
                {"componentData": {"name": "focus", "type": "Image"}, "bounds": {"bounds": "0 0 278 107"}},
                {"componentData": {"name": "slider", "type": "Slider"}, "bounds": {"bounds": "15 11 248 77"}},
            ],
        }
        with tempfile.TemporaryDirectory() as skins:
            trig_def = build_compact_osc_routing_definition(source, skins)
        show_actions = [a for a in trig_def["actions"] if a["handler"] == "Show Overlay"]
        self.assertEqual({a["onAction"] for a in show_actions}, {"Mouse Down", "Enter Pressed"})
        for action in show_actions:
            self.assertEqual(action["additionalData"], OSC_ROUTING_OVERLAY_NAME)
            self.assertEqual(action["handleName"], "Data")
        children = {c["componentData"]["name"]: c["componentData"] for c in trig_def["componentsData"]}
        self.assertEqual(children["Selector face"]["data"]["image"], "selector_face_150x48.png")
        self.assertEqual(children["Title"]["type"], "Label")

        overlay = build_standalone_osc_routing_choice_overlay()
        self.assertTrue(overlay["hideQLinkBounds"])
        self.assertFalse(overlay["ignoreMousePresses"])
        items = overlay["componentsData"]
        bg = next(c for c in items if c["componentData"]["type"] == "Image")
        self.assertEqual(bg["bounds"]["bounds"], "0 0 1280 800")
        self.assertEqual(bg["componentData"]["data"]["image"], OSC_ROUTING_OVERLAY_BG_IMAGE)

        # MenuOverlay close handler guarantees clean Focus Lost cancellation without touch trapping.
        close = next(c for c in items if c["componentData"]["type"] == "MenuOverlay")
        self.assertEqual(close["bounds"]["bounds"], "0 0 1280 800")
        self.assertEqual(close["componentData"]["data"]["withNumbers"], False)
        self.assertEqual(close["componentData"]["data"]["handleName"], "Data")
        self.assertIn(
            {"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""},
            close["componentData"]["data"]["actions"],
        )

        self.assertEqual([c["componentData"]["type"] for c in items],
                         ["Image", "MenuOverlay", "Image", "Image", "Image"])
        self.assertEqual([_rect_tuple(c["bounds"]["bounds"]) for c in items[-3:]],
                         [(x, 120, 6, 6) for x in (16, 28, 40)])

    def test_optional_modal_ranged_handle(self):
        comp = build_standalone_osc_routing_component(dest_param_idx=21, power_param_idx=12, overlay_param=999)
        handles = comp["bounds"]["additionalInvalidatingHandles"]
        self.assertIn("IndexedEnabling/1/2/Parameter 12", handles)
        self.assertIn("RangedEnabling/0/18/33/Parameter 999", handles)

    def test_skin_extra_with_selector_assets_and_non_marker_colors(self):
        extra_selector = _load_extra("54_osc_selector.py", "mpc_skin_extra_54_osc_selector_for_routing")
        extra_routing = _load_extra("55_osc_routing.py", "mpc_skin_extra_55_osc_routing")
        params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        key_to_idx = {p["key"]: i for i, p in enumerate(params)}
        shared_layout = json.loads(OVERLAY_LAYOUT_SHARED.read_text(encoding="utf-8"))
        marker_colors = {
            tuple(int(m["rgb"][i:i + 2], 16) for i in (0, 2, 4))
            for v in shared_layout["views"]
            for m in v["markers"]
        }

        with tempfile.TemporaryDirectory() as tmp:
            skins_dir = Path(tmp)

            class MockCtx:
                meta = {"variant": "overlay"}
                param_count = len(params)
                _extra = {param_count + 39: "shell.play_osc"}

                def __init__(self):
                    self.skins_dir = skins_dir

                def add_component_definition(self, key, value):
                    tui["pageData"]["componentDefinitions"]["localComponentDefinitions"].append({"key": key, "value": value})

                def param_index(self, key):
                    return key_to_idx[key]

            tui = self._make_base_tui()
            ctx = MockCtx()
            extra_selector.apply(tui, {}, ctx)
            extra_routing.apply(tui, {}, ctx)

            defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
            self.assertNotIn("oscNativeSelector", defs)
            self.assertIn(OSC_COMPACT_ROUTING_COMPONENT_TYPE, defs)
            self.assertIn(OSC_COMPACT_PAN_COMPONENT_TYPE, defs)
            self.assertIn(OSC_COMPACT_LEVEL_COMPONENT_TYPE, defs)
            self.assertIn(OSC_PAN_LAYER, defs)
            self.assertIn(OSC_LEVEL_LAYER, defs)
            self.assertIn(OSC_ROUTING_OVERLAY_NAME, defs)

            replacements = {r["key"]: r["value"] for r in tui["pageData"]["replacementStrings"]}
            base = native_base_index(ctx.param_count)
            route_base, view_idx = base + OSC_ROUTING_BY_SOURCE, base + OSC_VISIBILITY
            routing_layer = defs["oscNativeRoutingLayer"]
            self.assertTrue(routing_layer["ignoreMousePresses"])
            self.assertEqual(len(routing_layer["componentsData"]), 5)
            for source, component in enumerate(routing_layer["componentsData"]):
                route_idx = route_base + source
                self.assertEqual(replacements[f"Parameter {route_idx}"], list(OSC_ROUTING_CHOICES))
                self.assertEqual(component["bounds"]["bounds"], "0 0 150 48")
                self.assertEqual(component["componentData"]["type"], OSC_COMPACT_ROUTING_COMPONENT_TYPE)
                self.assertEqual(component["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{source}/5/Parameter {base + OSC_SOURCE}"])
                self.assertEqual(component["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {route_idx}"}])

            pan_layer = defs[OSC_PAN_LAYER]
            self.assertTrue(pan_layer["ignoreMousePresses"])
            self.assertEqual(len(pan_layer["componentsData"]), 5)
            for source, component in enumerate(pan_layer["componentsData"]):
                self.assertEqual(component["bounds"]["bounds"], "0 0 202 48")
                self.assertEqual(component["componentData"]["type"], OSC_COMPACT_PAN_COMPONENT_TYPE)
                self.assertEqual(component["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{source}/5/Parameter {base + OSC_SOURCE}"])

            level_layer = defs[OSC_LEVEL_LAYER]
            self.assertTrue(level_layer["ignoreMousePresses"])
            self.assertEqual(len(level_layer["componentsData"]), 5)
            for source, component in enumerate(level_layer["componentsData"]):
                self.assertEqual(component["bounds"]["bounds"], "0 0 202 48")
                self.assertEqual(component["componentData"]["type"], OSC_COMPACT_LEVEL_COMPONENT_TYPE)
                self.assertEqual(component["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{source}/5/Parameter {base + OSC_SOURCE}"])

            for tab in tui["pageData"]["tabs"]:
                items = tab["componentDefinition"]["componentsData"]
                self.assertFalse(any(c["componentData"]["name"].startswith("Osc Native Source ") for c in items))
                pan = next(c for c in items if c["componentData"]["name"] == OSC_PAN_CONTROL_NAME)
                level = next(c for c in items if c["componentData"]["name"] == OSC_LEVEL_CONTROL_NAME)
                routing = next(c for c in items if c["componentData"]["name"] == OSC_ROUTING_CONTROL_NAME)

                self.assertIn(OSC_PAN_LAYER, pan["componentData"]["type"])
                self.assertEqual(pan["bounds"]["bounds"], "%d %d %d %d" % STANDALONE_COMPACT_OSC_PAN_RECT)
                self.assertEqual(pan["bounds"]["additionalInvalidatingHandles"], [f"NonZero/Parameter {view_idx}"])

                self.assertIn(OSC_LEVEL_LAYER, level["componentData"]["type"])
                self.assertEqual(level["bounds"]["bounds"], "%d %d %d %d" % STANDALONE_COMPACT_OSC_LEVEL_RECT)
                self.assertEqual(level["bounds"]["additionalInvalidatingHandles"], [f"NonZero/Parameter {view_idx}"])

                self.assertIn("oscNativeRoutingLayer", routing["componentData"]["type"])
                self.assertEqual(routing["bounds"]["bounds"], "%d %d %d %d" % STANDALONE_COMPACT_OSC_ROUTING_RECT)
                self.assertEqual(routing["bounds"]["showWhenDataModelInvalid"], "Hide")
                self.assertEqual(routing["bounds"]["whenVisible"], "Always")
                self.assertEqual(routing["bounds"]["version"], 2)
                self.assertEqual(routing["handle remapping"]["map"][0], {"key": "Data", "value": f"Parameter {view_idx}"})
                self.assertEqual(routing["bounds"]["additionalInvalidatingHandles"], [f"NonZero/Parameter {view_idx}"])

                r_pan = _rect_tuple(pan["bounds"]["bounds"])
                r_lvl = _rect_tuple(level["bounds"]["bounds"])
                r_rt = _rect_tuple(routing["bounds"]["bounds"])
                self.assertFalse(_rects_overlap(r_pan, r_lvl))
                self.assertFalse(_rects_overlap(r_lvl, r_rt))
                self.assertEqual(r_pan[3], 48)
                self.assertEqual(r_lvl[3], 48)
                self.assertEqual(r_rt[3], 48)
                self.assertEqual(r_pan[1], 580)
                self.assertEqual(r_lvl[1], 580)
                self.assertEqual(r_rt[1], 37)

            # Verify all PNGs created, valid, non-empty, distinct on/off, and free of detector marker colors.
            bg_w, bg_h, bg_px = _decode_rgb_png(skins_dir / OSC_ROUTING_OVERLAY_BG_IMAGE)
            self.assertEqual((bg_w, bg_h), (16, 16))
            self.assertTrue(bg_px.isdisjoint(marker_colors))

            self.assertFalse(list(skins_dir.glob("osc_routing_choice_*.png")))

    def test_skin_extra_non_overlay_ignored(self):
        extra_routing = _load_extra("55_osc_routing.py", "mpc_skin_extra_55_osc_routing_noop")
        for meta in (None, {}, {"variant": "production"}):
            with tempfile.TemporaryDirectory() as tmp:
                tui = self._make_base_tui()
                before = json.dumps(tui)

                class MockCtx:
                    def __init__(self, m):
                        self.meta = m
                        self.skins_dir = Path(tmp)

                extra_routing.apply(tui, {}, MockCtx(meta))
                self.assertEqual(json.dumps(tui), before)
                self.assertEqual(list(Path(tmp).iterdir()), [])

    @unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
    def test_full_skin_pipeline_end_to_end(self):
        apply_script = ROOT / "scripts" / "apply-mpc-skin-extras.py"
        with tempfile.TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            ui = tmp / "ui"
            subprocess.run(
                [sys.executable, str(GEN), "--params", str(PARAMS_JSON), "--pages", str(PAGES_JSON), "--layout", str(LAYOUT_JSON), "--out", str(ui)],
                check=True,
                capture_output=True,
            )
            skins = tmp / "pf" / "Plugin Skins"
            subprocess.run(
                [sys.executable, str(RUN_GEN_UI), "--sdk", str(SDK), "--params", str(ui / "params-hostsim.json"), "--ui", str(ui / "ui.json"), "--out", str(skins), "--quiet"],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                [sys.executable, str(apply_script), "--skins", str(skins), "--params", str(PARAMS_JSON), "--meta", str(ui / "skin-meta.json")],
                check=True,
                capture_output=True,
            )
            val = subprocess.run(
                [sys.executable, "-I", str(SDK / "tools" / "validate.py"), str(skins.parent)],
                capture_output=True,
                text=True,
            )
            report = val.stdout + val.stderr
            errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "37 tabs; at most 32" not in l]
            self.assertEqual(errors, [], report)

            tui = json.loads((skins / "TUI.json").read_text(encoding="utf-8"), object_pairs_hook=dict)
            defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
            self.assertIn(OSC_COMPACT_ROUTING_COMPONENT_TYPE, defs)
            self.assertIn(OSC_COMPACT_PAN_COMPONENT_TYPE, defs)
            self.assertIn(OSC_COMPACT_LEVEL_COMPONENT_TYPE, defs)
            self.assertIn(OSC_PAN_LAYER, defs)
            self.assertIn(OSC_LEVEL_LAYER, defs)
            self.assertIn(OSC_ROUTING_OVERLAY_NAME, defs)
            self.assertNotIn("oscNativeSelector", defs)
            controls = defs["oscNativeSurface"]["componentsData"]
            self.assertTrue(defs["oscNativeSurface"]["ignoreMousePresses"])
            self.assertEqual([c["componentData"]["name"] for c in controls],
                             ["Osc Native Pan", "Osc Native Level",
                              "Osc Native Routing", "Osc Native Power",
                              "Osc Native Settings", "Osc Native Detail", "Osc Native Tune A", "Osc Native Tune B", "Osc Native Tune C"])
            widgets = {c["componentData"]["name"]: c for c in controls}
            for tab_name in ("Osc", "Osc B", "Osc C", "Sub", "Noise"):
                tab = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == tab_name)
                children = tab["componentDefinition"]["componentsData"]
                types = [c["componentData"]["type"] for c in children]
                self.assertEqual(types.count("oscNativeSurface"), 1, tab_name)
                pos = types.index("oscNativeSurface")
                self.assertEqual(children[pos]["bounds"]["bounds"], "0 0 1280 628")
                self.assertTrue(all(c["componentData"]["name"].startswith("Overlay ") for c in children[pos + 1:]))
            play = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == "Play")
            dock, = [c for c in play["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "oscNativeSurface"]
            self.assertEqual(dock["bounds"]["additionalInvalidatingHandles"], ["RangedEnabling/1/2/7/Parameter 3535"])
            self.assertEqual(dock["bounds"]["bounds"], "0 0 1280 628")
            self.assertNotIn("Osc Native Source Tabs", widgets)
            self.assertIn(OSC_PAN_LAYER, widgets["Osc Native Pan"]["componentData"]["type"])
            self.assertEqual(widgets["Osc Native Pan"]["bounds"]["bounds"], "%d %d %d %d" % STANDALONE_COMPACT_OSC_PAN_RECT)
            self.assertIn(OSC_LEVEL_LAYER, widgets["Osc Native Level"]["componentData"]["type"])
            self.assertEqual(widgets["Osc Native Level"]["bounds"]["bounds"], "%d %d %d %d" % STANDALONE_COMPACT_OSC_LEVEL_RECT)
            self.assertIn("oscNativeRoutingLayer", widgets[OSC_ROUTING_CONTROL_NAME]["componentData"]["type"])
            self.assertEqual(widgets[OSC_ROUTING_CONTROL_NAME]["bounds"]["bounds"], "%d %d %d %d" % STANDALONE_COMPACT_OSC_ROUTING_RECT)
            self.assertEqual(widgets["Osc Native Power"]["componentData"]["type"], "oscNativePowerLayer")
            self.assertEqual(widgets["Osc Native Power"]["bounds"]["bounds"], "12 8 106 164")
            self.assertEqual(widgets["Osc Native Power"]["bounds"]["additionalInvalidatingHandles"], ["NonZero/Parameter 2885"])
            self.assertEqual(widgets["Osc Native Settings"]["bounds"]["additionalInvalidatingHandles"], ["NonZero/Parameter 2985"])

            detail = json.loads((EXTRAS_DIR / "osc-native-detail.json").read_text(encoding="utf-8"))
            viewport = defs["oscNativeDetail"]["componentsData"]
            base = 2719 + OSC_DETAIL
            self.assertEqual(base, 3348)
            self.assertEqual(widgets["Osc Native Detail"]["bounds"]["additionalInvalidatingHandles"], [f"NonZero/Parameter {base}"])
            for frame, grids, gate in (("plain", viewport[0], f"IndexedEnabling/3/6/Parameter {base}"),
                                       ("graph", viewport[1], f"RangedEnabling/4/5/6/Parameter {base}")):
                x, y, w, h, stride, rows = detail[frame]
                grid = defs[grids["componentData"]["type"]]
                self.assertEqual((grid["version"], grid["repeats"], len(grid["componentsData"])), (3, 8, rows))
                self.assertEqual(_rect_tuple(grids["bounds"]["bounds"]), (x, y, 8 * w, (rows - 1) * stride + h))
                self.assertEqual(grids["bounds"]["additionalInvalidatingHandles"], [gate] if frame == "plain" else
                                 [gate, "RangedEnabling/0/13/17/Parameter 3417"])
                self.assertLessEqual(x + 8 * w, 1280)
                self.assertLessEqual(y + stride * (rows - 1) + h, 628)
                for row, child in enumerate(grid["componentsData"]):
                    self.assertEqual(_rect_tuple(child["bounds"]["bounds"]), (0, row * stride, w, h))
                    binding = {b["key"]: b["value"] for b in child["handle remapping"]["map"]}
                    for handle, offset in (("Data", 0), ("Step", OSC_DETAIL_CELLS), ("Kind", 2 * OSC_DETAIL_CELLS)):
                        prefix = "FromGesture/" if handle == "Step" else ""
                        self.assertEqual(binding[handle], f"{prefix}Parameter {{Increment:Base={base + 1 + offset + row},Step=4}}")
            self.assertNotIn("more_small", detail)
            for i, name in enumerate(("sub_blend", "noise_blend", "noise_sample_blend"), 2):
                control = viewport[i]
                self.assertEqual(control["componentData"]["type"], "oscNativeSetting_dial_uni")
                self.assertEqual(control["handle remapping"]["map"][0]["value"], f"Parameter {base + 1}")
                self.assertEqual(_rect_tuple(control["bounds"]["bounds"]), tuple(detail[name]))
            for i, name in enumerate(("back", "load"), 5):
                self.assertEqual(_rect_tuple(viewport[i]["bounds"]["bounds"]), tuple(detail[name]))
            for i, button in enumerate(viewport[7:10]):
                with self.subTest(root_load=i):
                    self.assertEqual(button["componentData"]["type"], "oscNativeSetting_action")
                    self.assertEqual(_rect_tuple(button["bounds"]["bounds"]), tuple(detail["root_load"]))
                    self.assertEqual(button["bounds"]["additionalInvalidatingHandles"],
                                     [f"IndexedEnabling/{i + 1}/4/Parameter 3499"])
                    self.assertEqual(button["bounds"]["acceptsHWFocus"], "Yes")
                    index = 3496 + i
                    self.assertEqual(button["handle remapping"]["map"],
                                     [{"key": "Data", "value": f"FromGesture/Parameter {index}"},
                                      {"key": "Caption", "value": f"Parameter {index}"}])
            unison = viewport[10:]
            self.assertEqual(len(unison), 9)
            for control in unison:
                self.assertEqual(control["bounds"]["additionalInvalidatingHandles"][:2], [
                    "RangedEnabling/4/5/6/Parameter 3348", "IndexedEnabling/14/17/Parameter 3417"])
            mode, stack, *dials = unison
            self.assertEqual(mode["handle remapping"]["map"], [
                {"key": "Data", "value": "Parameter 3349"}, {"key": "Step", "value": "FromGesture/Parameter 3381"}])
            buttons = [c["componentData"]["data"] for c in defs[mode["componentData"]["type"]]["componentsData"]
                       if c["componentData"]["type"] == "Button"]
            self.assertEqual([(b["buttonId"], b["handleName"]) for b in buttons], [(1, "Step"), (3, "Step")])
            self.assertEqual(stack["componentData"]["type"], "oscNativeSetting_menu")
            self.assertEqual(stack["handle remapping"]["map"][0], {"key": "Data", "value": "Parameter 3455"})
            for kinds in ((1, 1, 2, 2, 2), (1, 1, 2, 2, 0), (1, 1, 1, 1, 0)):
                visible = []
                for control in dials:
                    cx, cy, cw, ch = _rect_tuple(control["bounds"]["bounds"])
                    column = (cx - x) // w
                    self.assertEqual((cx, cy, cw, ch), (x + column * w, y, w, h))
                    gate = control["bounds"]["additionalInvalidatingHandles"][2]
                    polarity = 2 if control["componentData"]["type"].endswith("_bi") else 1
                    self.assertEqual(gate, f"IndexedEnabling/{polarity}/17/Parameter {3413 + column * 4}")
                    self.assertEqual(control["handle remapping"]["map"][0]["value"], f"Parameter {3349 + column * 4}")
                    if polarity == kinds[column - 2]:
                        visible.append(column)
                self.assertEqual(visible, [i for i, k in enumerate(kinds, 2) if k])
            tile = defs["oscDetailTile"]["componentsData"]
            shared = tile[6]
            self.assertEqual(shared["bounds"]["additionalInvalidatingHandles"], ["RangedEnabling/7/16/17/Kind"])
            face = defs[shared["componentData"]["type"]]
            self.assertTrue(face["ignoreMousePresses"])
            self.assertEqual([item["componentData"]["name"] for item in face["componentsData"]], ["Selector face", "Name"])
            self.assertEqual(face["componentsData"][1]["componentData"]["data"]["handleName"], "Caption")
            long_menus = tile[7:]
            self.assertEqual(len(long_menus), len(detail["menus"]))
            all_replacements = {x["key"]: x["value"] for x in tui["pageData"]["replacementStrings"]}
            self.assertEqual(all_replacements["Parameter 3456"], all_replacements["Parameter 3021"])
            self.assertEqual(len(all_replacements["Parameter 3456"]), 99)
            for i, child in enumerate(long_menus):
                index = base + 4 + 3 * OSC_DETAIL_CELLS + i
                original = "oscNativeSettingWarpChoice" if detail["menus"][i] in ("warp.type", "warp2.type") else "oscNativeSetting_menu"
                widget = defs[child["componentData"]["type"]]
                self.assertEqual(child["componentData"]["type"], original + "DetailValue")
                popup = "oscWarpChoices" if original.endswith("WarpChoice") else "fx editor choice menu"
                self.assertEqual([(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in widget["actions"]],
                                 [(event, "Show Overlay", "Data", popup) for event in ("Mouse Down", "Enter Pressed")])
                label, = widget["componentsData"]
                self.assertEqual(label["componentData"]["data"]["handleName"], "Data")
                self.assertIn({"key": "Data", "value": f"Parameter {index}"}, child["handle remapping"]["map"])
                self.assertGreater(len(all_replacements[f"Parameter {index}"]), 6)

            labels = ("Sub", "Osc A", "Osc B", "Osc C", "Noise")
            routes = defs["oscNativeRoutingLayer"]["componentsData"]
            powers = defs["oscNativePowerLayer"]["componentsData"]
            pans = defs[OSC_PAN_LAYER]["componentsData"]
            levels = defs[OSC_LEVEL_LAYER]["componentsData"]
            self.assertEqual(len(routes), len(labels))
            self.assertEqual(len(powers), len(labels))
            self.assertEqual(len(pans), len(labels))
            self.assertEqual(len(levels), len(labels))
            for i, source in enumerate(labels):
                route, power = routes[i], powers[i]
                pan, level = pans[i], levels[i]
                self.assertEqual(route["componentData"]["name"], f"{OSC_ROUTING_CONTROL_NAME} {source}")
                self.assertEqual(route["bounds"]["bounds"], "0 0 150 48")
                self.assertEqual(route["componentData"]["type"], OSC_COMPACT_ROUTING_COMPONENT_TYPE)
                self.assertEqual(route["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {3005 + i}"}])
                self.assertEqual(pan["componentData"]["name"], f"{OSC_PAN_CONTROL_NAME} {source}")
                self.assertEqual(pan["bounds"]["bounds"], "0 0 202 48")
                self.assertEqual(pan["componentData"]["type"], OSC_COMPACT_PAN_COMPONENT_TYPE)
                self.assertEqual(level["componentData"]["name"], f"{OSC_LEVEL_CONTROL_NAME} {source}")
                self.assertEqual(level["bounds"]["bounds"], "0 0 202 48")
                self.assertEqual(level["componentData"]["type"], OSC_COMPACT_LEVEL_COMPONENT_TYPE)
                self.assertEqual(power["componentData"]["name"], f"Osc Native {source} Power")
                self.assertEqual(power["componentData"]["type"], "Button")
                data = power["componentData"]["data"]
                self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["gestureBehaviour"]), (1, 1, "Instant"))
                self.assertEqual(power["handle remapping"]["map"], [{"key": "Data", "value": f"Parameter {2971 + i}"}])
                self.assertEqual(power["bounds"]["bounds"], ("42 0 64 56" if i in (1, 2, 3) else "0 108 64 56"))
                self.assertEqual(power["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{i}/5/Parameter 2883"])
                with Image.open(skins / data["onImage"]) as on, Image.open(skins / data["offImage"]) as off:
                    self.assertEqual((on.size, off.size), ((64, 56), (64, 56)))
                    self.assertEqual((on.getpixel((32, 16)), off.getpixel((32, 16))),
                                     ((96, 255, 163, 255), (255, 82, 81, 255)))

            source = json.loads((EXTRAS_DIR / "osc-native-settings.json").read_text(encoding="utf-8"))
            root = defs["oscNativeSettingsLayer"]
            self.assertTrue(root["ignoreMousePresses"])
            self.assertEqual(len(root["componentsData"]), 2 * (26 + 3))
            self.assertEqual({variant["layout"] for variant in source}, set(range(1, 9)))
            abc = defs["oscNativeAbcSettings"]["componentsData"]
            replacements = {item["key"]: item["value"] for item in tui["pageData"]["replacementStrings"]}
            for i, wrapper in enumerate(root["componentsData"][-6::2]):
                self.assertEqual(wrapper["componentData"]["type"], "oscNativeAbcSettings")
                self.assertEqual(wrapper["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{i + 1}/5/Parameter 2883", f"IndexedEnabling/1/2/Parameter {2972 + i}"])
                handles = {item["key"]: item["value"] for item in wrapper["handle remapping"]["map"]}
                self.assertEqual([handles[f"OscAbc{slot}"] for slot in range(18)],
                                 [f"Parameter {3011 + i * 18 + slot}" for slot in range(18)])
            def active(widget, variant):
                frames = {2985: variant["layout"], 3010: variant["warp"]}
                for gate in widget["bounds"]["additionalInvalidatingHandles"]:
                    kind, lo, *tail = gate.split("/")
                    hi, _, parameter = (lo, *tail) if kind == "IndexedEnabling" else tail
                    if not int(lo) <= frames[int(parameter.split()[1])] <= int(hi):
                        return False
                return True

            for variant in source:
                gate = f"IndexedEnabling/{variant['layout']}/9/Parameter 2985"
                if "warp" in variant:
                    widgets = [widget for widget in abc if active(widget, variant)]
                else:
                    widgets = [widget for widget in root["componentsData"] if gate in widget["bounds"]["additionalInvalidatingHandles"] and not widget["componentData"]["name"].endswith(" Off")]
                expected_fields = [f for f in variant["fields"] if "warp" not in variant or f["id"] != 110]
                self.assertEqual(len(widgets), len(expected_fields), variant)
                by_handle = {widget["handle remapping"]["map"][0]["value"]: widget for widget in widgets}
                for field in expected_fields:
                    slot = field["slot"]
                    parameter = f"OscAbc{slot}" if "warp" in variant else f"Parameter {2986 + slot}"
                    widget = by_handle[parameter]
                    self.assertEqual(_rect_tuple(widget["bounds"]["bounds"]), tuple(field["rect"]))
                    definition = defs[widget["componentData"]["type"]]
                    if field["kind"] == "menu":
                        targets = [f"Parameter {3011 + i * 18 + slot}" for i in range(3)] if "warp" in variant else [parameter]
                        for target in targets:
                            self.assertEqual(replacements[target], field["choices"])
                        drawer = "fx editor choice menu"
                        if field["key"].endswith(("warp.type", "warp2.type")):
                            drawer = "oscWarpChoices"
                        elif 2 <= len(field["choices"]) <= 9:
                            drawer = f"oscNativeSetting_menu_{field['label']}Choices"
                        self.assertEqual({a["additionalData"] for a in definition["actions"]}, {drawer})
                        self.assertIn(drawer, defs)
                    elif field["kind"] == "number":
                        self.assertEqual(field["step"], 1)
                        self.assertEqual({a["additionalData"] for a in definition["actions"]}, {"fx editor number menu"})
                    elif field["kind"] == "toggle":
                        button, = definition["componentsData"]
                        self.assertEqual(button["componentData"]["type"], "Button")
                        artwork = button["componentData"]["data"]
                        from components.native.active_text.active_text import render_active_text_art
                        for state, attr in ((True, "onImage"), (False, "offImage")):
                            with Image.open(skins / artwork[attr]) as image:
                                reference = render_active_text_art(field["label"], state, *image.size)
                                self.assertEqual(image.convert("RGBA").tobytes(), reference.tobytes())
                    elif field["label"] in ("BROWSE", "MAP"):
                        button, = definition["componentsData"]
                        self.assertEqual(button["componentData"]["type"], "Button")
                        self.assertEqual(button["componentData"]["name"], field["label"])
                    elif field["kind"] in ("source", "action"):
                        self.assertEqual({a["handler"] for a in definition["actions"]}, {"Toggle Switch"})
                    elif field["kind"] == "table":
                        buttons = [child["componentData"]["data"] for child in definition["componentsData"]
                                   if child["componentData"]["type"] == "Button"]
                        self.assertEqual([button["buttonId"] for button in buttons], [1, 2, 3])
                        self.assertTrue(all(button["numButtonsInGroup"] == 4 for button in buttons))
                    else:
                        knob_data = definition["componentsData"][0]["componentData"]["data"]
                        self.assertEqual(knob_data["knobType"], "FilmStrip")
                        family = "pan" if "bi" in field["kind"] else "level"
                        side = _rect_tuple(definition["componentsData"][0]["bounds"]["bounds"])[2]
                        if "warp" in variant:
                            self.assertEqual(side, 40, "OSC A/B/C use the compact catalog dial")
                        expected = f"play_sub_{family}_{side}.png"
                        self.assertEqual(knob_data["filmStrip"], expected)


if __name__ == "__main__":
    unittest.main()
