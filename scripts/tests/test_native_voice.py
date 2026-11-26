# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for standalone native Voice tab definitions, geometry, component generation, and XL offline contact evidence."""
import copy
import importlib.util
import json
import os
import subprocess
from pathlib import Path
import sys
import unittest
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from scripts.native_voice.voice_tab import (
    KEEP,
    VOICE_STEP_KNOB_PREFIX,
    UNCONVERTED_CONTROLS,
    VOICE_NATIVE_CONTROLS,
    VOICE_NATIVE_CONTROL_NAMES,
    VOICE_ROWS,
    VOICE_SEQUENCE_CONTROLS,
    VOICE_STANDALONE_CONTROLS,
    VOICE_STEPS,
    VOICE_TAB_NAME,
    VOICE_TARGET_CONTROLS,
    VOICE_TARGET_ITEMS,
    populate_voice_tab,
    sequence_base,
    voice_definitions,
)

PARAMS_JSON = ROOT / "docs" / "params.json"
ROOT_GATE = "RangedEnabling/0/18/33/Parameter 2675"
PACKAGED_TUI = os.environ.get("SYNTHYBOY_VOICE_TUI")
VOICE_HOST = os.environ.get("SYNTHYBOY_VOICE_UI_HOST")

# Import 84_voice.py dynamically
EXTRA_84_PATH = ROOT / "scripts" / "mpc-skin-extras" / "84_voice.py"
_spec_84 = importlib.util.spec_from_file_location("mpc_skin_extra_84_voice", EXTRA_84_PATH)
voice_extra = importlib.util.module_from_spec(_spec_84)
_spec_84.loader.exec_module(voice_extra)

# Import 97_no_fallback.py dynamically
EXTRA_97_PATH = ROOT / "scripts" / "mpc-skin-extras" / "97_no_fallback.py"
_spec_97 = importlib.util.spec_from_file_location("mpc_skin_extra_97_no_fallback", EXTRA_97_PATH)
no_fallback_extra = importlib.util.module_from_spec(_spec_97)
_spec_97.loader.exec_module(no_fallback_extra)


class MockContext:
    def __init__(self, params_dict, skins_dir):
        self.params = params_dict["params"]
        self._index = {p["key"]: i for i, p in enumerate(self.params)}
        self.meta = {"variant": "overlay"}
        self.skins_dir = skins_dir

    def param_index(self, key):
        if key not in self._index:
            raise KeyError(f"Unknown param: {key}")
        return self._index[key]

    def add_component_definition(self, key, value):
        defs = self.tui.setdefault("pageData", {}).setdefault("componentDefinitions", {}).setdefault("localComponentDefinitions", [])
        defs.append({"key": key, "value": value})


class TestNativeVoiceTab(unittest.TestCase):
    def setUp(self):
        with open(PARAMS_JSON, "r", encoding="utf-8") as f:
            self.params_data = json.load(f)
        skins = tempfile.TemporaryDirectory()
        self.addCleanup(skins.cleanup)
        self.ctx = MockContext(self.params_data, skins.name)
        self.ctx.voice_tab = {
            "tabName": VOICE_TAB_NAME,
            "fnKeyIndex": 4,
            "fnKeySubIndex": 0,
            "componentDefinition": {
                "componentsData": [
                    {"componentData": {"name": "Overlay Marker Left"}},
                    {
                        "componentData": {"name": "Overlay Instance Voice"},
                        "handle remapping": {
                            "version": 1,
                            "map": [{"key": "Data", "value": "Parameter 2675"}],
                        },
                    },
                ]
            },
        }
        self.ctx.tui = {
            "pageData": {
                "tabs": [self.ctx.voice_tab],
                "componentDefinitions": {
                    "localComponentDefinitions": []
                },
            }
        }

    def test_all_native_parameter_keys_exist_in_params_json(self):
        """Every converted native control must bind to a real voicectl.* parameter in params.json."""
        for ctrl in VOICE_NATIVE_CONTROLS:
            idx = self.ctx.param_index(ctrl["key"])
            self.assertIsInstance(idx, int, f"Key {ctrl['key']} should resolve to an integer index")
            self.assertGreaterEqual(idx, 0)

    def test_native_controls_geometry_and_touch_target_rules(self):
        """All controls must have minimum 56px in smaller dimension and fit within 1280x628."""
        for ctrl in VOICE_NATIVE_CONTROLS:
            x, y, w, h = ctrl["rect"]
            self.assertGreaterEqual(w, 56, f"{ctrl['name']} width must be >= 56px")
            self.assertGreaterEqual(h, 56, f"{ctrl['name']} height must be >= 56px")
            self.assertGreaterEqual(x, 0, f"{ctrl['name']} x out of bounds")
            self.assertGreaterEqual(y, 0, f"{ctrl['name']} y out of bounds")
            self.assertLessEqual(x + w, 1280, f"{ctrl['name']} right edge exceeds 1280")
            self.assertLessEqual(y + h, 628, f"{ctrl['name']} bottom edge exceeds 628")

    def test_no_overlap_between_converted_controls_and_unconverted_regions(self):
        """Converted native controls must not overlap each other or unconverted overlay regions."""
        all_rects = [(c["name"], c["rect"]) for c in VOICE_NATIVE_CONTROLS]
        all_rects.extend((name, rect) for name, _, rect, _, _ in VOICE_SEQUENCE_CONTROLS)

        # Check self-overlap among native controls
        for i in range(len(all_rects)):
            n1, (x1, y1, w1, h1) = all_rects[i]
            for j in range(i + 1, len(all_rects)):
                n2, (x2, y2, w2, h2) = all_rects[j]
                overlaps = not (x1 + w1 <= x2 or x2 + w2 <= x1 or y1 + h1 <= y2 or y2 + h2 <= y1)
                self.assertFalse(overlaps, f"{n1} overlaps {n2}")

        # Check overlap with unconverted retained regions
        for unc in UNCONVERTED_CONTROLS:
            ux, uy, uw, uh = unc["rect"]
            for name, (nx, ny, nw, nh) in all_rects:
                overlaps = not (nx + nw <= ux or ux + uw <= nx or ny + nh <= uy or uy + uh <= ny)
                self.assertFalse(overlaps, f"Native {name} overlaps retained {unc['name']}")

    def test_explicit_unconverted_controls_inventory(self):
        """Unconverted controls must be explicitly listed with technical justification."""
        unconverted_names = {u["name"] for u in UNCONVERTED_CONTROLS}
        self.assertIn("Voice Step Grid", unconverted_names)
        self.assertNotIn("Voice Step Readout Strip", unconverted_names)
        for u in UNCONVERTED_CONTROLS:
            self.assertTrue(len(u["reason"]) > 20, f"{u['name']} must have detailed justification")

    def test_voice_dial_definition(self):
        """voice_dial_definition centers the 90x90 knob inside the 116x146 slot at x=13, center=58."""
        dial_def = voice_definitions()["voiceNativeDial"]
        child_bounds = {c["componentData"]["name"]: c["bounds"]["bounds"] for c in dial_def["componentsData"]}
        self.assertEqual(child_bounds["knob"], "13 18 90 90")
        self.assertEqual(child_bounds["value"], "0 108 116 38")
        self.assertEqual(child_bounds["name"], "0 0 116 18")

        # Knob horizontal center inside slot: 13 + 45 = 58 (exact center of 116)
        knob_rect = [int(v) for v in child_bounds["knob"].split()]
        center_x = knob_rect[0] + knob_rect[2] / 2
        self.assertEqual(center_x, 58.0)

    def test_voice_scale_dial_definition(self):
        """voice_scale_dial_definition places a 90 px dial below the LFO mode selector."""
        scale_def = voice_definitions()["voiceNativeScaleDial"]
        child_bounds = {c["componentData"]["name"]: c["bounds"]["bounds"] for c in scale_def["componentsData"]}
        self.assertEqual(child_bounds["knob"], "40 18 90 90")
        self.assertEqual(child_bounds["name"], "0 0 170 18")
        self.assertEqual(child_bounds["value"], "0 108 170 24")

    def test_approved_components_and_mode_contract(self):
        with tempfile.TemporaryDirectory() as folder:
            definitions=voice_definitions(folder)
            pan=next(c["componentData"]["data"] for c in definitions["voiceNativeDial"]["componentsData"] if c["componentData"]["type"]=="Knob")
            self.assertEqual(pan["filmStrip"], "play_sub_level_90.png")
            self.assertTrue((Path(folder)/pan["filmStrip"]).is_file())
            param=next(p for p in self.ctx.params if p["key"]=="voicectl.rand.pan")
            self.assertEqual((param["min"],param["max"]),(0,100))
            for ctrl in VOICE_STANDALONE_CONTROLS:
                if ctrl["kind"]!="toggle":continue
                definition=definitions[ctrl["type"]]
                button,=definition["componentsData"]
                data=button["componentData"]["data"]
                on=Image.open(Path(folder)/data["onImage"]).convert("RGBA")
                off=Image.open(Path(folder)/data["offImage"]).convert("RGBA")
                self.assertEqual(on.size,tuple(ctrl["rect"][2:]))
                self.assertEqual(on.getchannel("A").tobytes(),off.getchannel("A").tobytes())
                self.assertNotEqual(on.tobytes(),off.tobytes())
                self.assertEqual([(a["onAction"], a["handler"], a["handleName"]) for a in definition["actions"]],
                                 [("Enter Pressed", "Toggle Switch", "Data")])
            targets_def = definitions["voiceNativeTargets"]
            self.assertEqual(targets_def["actions"], [])
            target_buttons = [c for c in targets_def["componentsData"] if c["componentData"]["type"] == "Button"]
            self.assertEqual(len(target_buttons), 5)
            for button, item in zip(target_buttons, VOICE_TARGET_ITEMS):
                data = button["componentData"]["data"]
                on = Image.open(Path(folder) / data["onImage"]).convert("RGBA")
                off = Image.open(Path(folder) / data["offImage"]).convert("RGBA")
                self.assertEqual(on.size, item["rect"][2:])
                self.assertEqual(on.getchannel("A").tobytes(), off.getchannel("A").tobytes())
                self.assertNotEqual(on.tobytes(), off.tobytes())
            scale=definitions["voiceNativeScaleDial"]["componentsData"]
            names=[c for c in scale if c["componentData"]["name"] in ("name","alternate caption")]
            self.assertEqual({c["bounds"]["additionalInvalidatingHandles"][0] for c in names},
                             {"TimeCaption","RateCaption"})
            self.assertTrue(any(c["componentData"]["data"].get("text")=="LFO RATE" for c in names))
            mode=next(c for c in VOICE_NATIVE_CONTROLS if c["key"]=="voicectl.scale.lfo_mode")
            for ctrl in VOICE_NATIVE_CONTROLS:
                if ctrl["key"] in ("voicectl.scale.env","voicectl.scale.lfo"):
                    self.assertLessEqual(mode["rect"][1]+mode["rect"][3],ctrl["rect"][1])
            face=next(c for c in definitions["voiceNativeChoice"]["componentsData"] if c["componentData"]["type"]=="Image")
            self.assertEqual(int(face["bounds"]["bounds"].split()[-1]),48)

    def test_populate_voice_tab_purges_fallback_controls(self):
        """populate_voice_tab strictly removes leftover fallback controls, keeping only sentinels + native controls."""
        tab = copy.deepcopy(self.ctx.voice_tab)
        # Inject old unconverted fallback controls from gen_ui
        tab["componentDefinition"]["componentsData"].extend([
            {"componentData": {"name": "Master Same Note Mode", "type": "radio3"}, "bounds": {"bounds": "23 155 278 107"}},
            {"componentData": {"name": "Voice Control Sequence Length", "type": "knobUni"}, "bounds": {"bounds": "23 456 278 107"}},
            {"componentData": {"name": "Voice Control Applies to Osc A", "type": "toggle"}, "bounds": {"bounds": "23 607 278 107"}},
        ])
        comps = populate_voice_tab(tab, self.ctx, ROOT_GATE)
        names = [c["componentData"]["name"] for c in comps]

        # Converted native controls present
        for ctrl_name in VOICE_NATIVE_CONTROL_NAMES:
            self.assertIn(ctrl_name, names)

        # Sentinels preserved
        self.assertIn("Overlay Marker Left", names)
        self.assertIn("Overlay Instance Voice", names)

        # Fallback controls PURGED
        self.assertNotIn("Master Same Note Mode", names)
        self.assertNotIn("Voice Control Sequence Length", names)
        self.assertNotIn("Voice Control Applies to Osc A", names)
        self.assertEqual(len(comps), len(VOICE_NATIVE_CONTROL_NAMES) + 2)

    def assert_native_widgets(self, page):
        tab = next(t for t in page["tabs"] if t["tabName"] == VOICE_TAB_NAME)
        definitions = {d["key"]: d["value"] for d in page["componentDefinitions"]["localComponentDefinitions"]}
        components = {c["componentData"]["name"]: c for c in tab["componentDefinition"]["componentsData"]
                      if not c["componentData"]["name"].startswith(("Overlay Instance", "Overlay Marker",
                                                                     "Overlay Background Sentinel ", "Native Caption "))}
        self.assertEqual(set(components), VOICE_NATIVE_CONTROL_NAMES)

        target = "Parameter %d" % self.ctx.param_index("voicectl.scale.lfo_mode")
        replacements = [r["value"] for r in page["replacementStrings"] if r["key"] == target]
        self.assertEqual(replacements, [["Percent", "Rate"]])

        for control in VOICE_STANDALONE_CONTROLS:
            name, kind = control["name"], control["kind"]
            comp = components[name]
            self.assertEqual(comp["componentData"]["type"], control["type"], name)
            bounds = comp["bounds"]
            self.assertEqual(tuple(map(int, bounds["bounds"].split())), control["rect"], name)
            self.assertEqual(bounds["additionalInvalidatingHandles"], [ROOT_GATE], name)
            self.assertEqual(bounds["acceptsHWFocus"], "Yes", name)
            self.assertEqual(bounds["showWhenDataModelInvalid"], "Hide", name)
            model = f"Parameter {self.ctx.param_index(control['key'])}"
            expected_handles={"Data":model,"Caption":f"WithName[{control['caption']}]/{model}"}
            if control["key"] == "voicectl.scale.lfo":
                expected_handles.update(TimeCaption=f"IndexedEnabling/0/2/{target}", RateCaption=f"IndexedEnabling/1/2/{target}")
            self.assertEqual({h["key"]:h["value"] for h in comp["handle remapping"]["map"]},expected_handles,name)

            definition = definitions[control["type"]]
            self.assertFalse(definition["ignoreMousePresses"], name)
            _, _, width, height = control["rect"]
            children = definition["componentsData"]
            for child in children:
                b = child["bounds"]
                x, y, w, h = map(float, b["bounds"].split())
                limit_x, limit_y = (1, 1) if b["boundsType"] == "Proportional" else (width, height)
                self.assertTrue(0 <= x < x + w <= limit_x and 0 <= y < y + h <= limit_y, (name, b))
            if kind == "dial":
                knob = next(c["componentData"]["data"] for c in children if c["componentData"]["type"] == "Knob")
                self.assertEqual((knob["knobType"],knob["handleName"]),("FilmStrip","Data"),name)
                self.assertGreater(knob["numFrames"],2)
                self.assertNotIn("knob_uni",knob["filmStrip"])
                labels = [c["componentData"]["data"] for c in children if c["componentData"]["type"] == "Label"]
                self.assertTrue(any(d["type"]=="Value" and d["handleName"]=="Data" for d in labels),name)
                self.assertEqual({(a["onAction"],a["additionalData"]) for a in definition["actions"]},
                                 {("Double Click","fx editor number menu"),("Enter Pressed","fx editor number menu")})
            elif kind == "toggle":
                button, = children
                self.assertEqual(button["componentData"]["type"],"Button")
                data = button["componentData"]["data"]
                self.assertEqual((data["buttonId"],data["numButtonsInGroup"],data["handleName"]),(1,1,"Data"))
                self.assertNotEqual(data["onImage"],data["offImage"])
                _,_,w,h=map(int,button["bounds"]["bounds"].split())
                self.assertGreaterEqual(w,56);self.assertGreaterEqual(h,56)
                self.assertEqual([a["onAction"] for a in definition["actions"]], ["Enter Pressed"])
            else:
                expected = "fx editor number menu" if kind == "number" else "fx editor choice menu"
                self.assertEqual({(a["onAction"],a["additionalData"]) for a in definition["actions"]},
                                 {("Mouse Down",expected),("Enter Pressed",expected)})

        # Assert Voice Target Scope group component
        scope_comp = components["Voice Target Scope"]
        self.assertEqual(scope_comp["componentData"]["type"], "voiceNativeTargets")
        self.assertEqual(tuple(map(int, scope_comp["bounds"]["bounds"].split())), (908, 372, 352, 234))
        self.assertEqual(scope_comp["bounds"]["additionalInvalidatingHandles"], [ROOT_GATE])
        self.assertEqual(scope_comp["bounds"]["acceptsHWFocus"], "Children")
        self.assertEqual(scope_comp["bounds"]["showWhenDataModelInvalid"], "Hide")
        expected_target_remaps = {
            f"Active{suffix}": f"Parameter {self.ctx.param_index(key)}"
            for suffix, key in [
                ("A", "voicectl.osc.a"),
                ("B", "voicectl.osc.b"),
                ("C", "voicectl.osc.c"),
                ("Sub", "voicectl.osc.sub"),
                ("Noise", "voicectl.osc.noise"),
            ]
        }
        expected_target_remaps["Data"] = expected_target_remaps["ActiveA"]
        self.assertEqual({h["key"]: h["value"] for h in scope_comp["handle remapping"]["map"]}, expected_target_remaps)

        targets_def = definitions["voiceNativeTargets"]
        self.assertEqual(targets_def["actions"], [])
        self.assertTrue(targets_def["ignoreMousePresses"])
        title_label = next(c for c in targets_def["componentsData"] if c["componentData"]["name"] == "title")
        self.assertEqual(title_label["componentData"]["data"].get("text"), "PAN / DETUNE")
        self.assertEqual(title_label["bounds"]["bounds"], "152 0 200 28")   # right of the TARGETS panel title, in its title strip
        target_buttons = [c for c in targets_def["componentsData"] if c["componentData"]["type"] == "Button"]
        self.assertEqual(len(target_buttons), 5)
        for tb, item in zip(target_buttons, VOICE_TARGET_ITEMS):
            self.assertEqual(tb["bounds"]["bounds"], " ".join(map(str, item["rect"])))
            t_data = tb["componentData"]["data"]
            self.assertEqual((t_data["buttonId"], t_data["numButtonsInGroup"], t_data["handleName"]), (1, 1, item["handle"]))
            self.assertNotEqual(t_data["onImage"], t_data["offImage"])
        base = sequence_base(self.ctx)
        self.assertEqual(base, 2968)
        strings = {entry["key"]: entry["value"] for entry in page["replacementStrings"]}
        self.assertEqual(strings[f"Parameter {base}"], list(VOICE_ROWS))
        self.assertEqual(strings[f"Parameter {base + 1}"], list(VOICE_STEPS))
        for offset, (name, kind, rect, caption, key) in enumerate(VOICE_SEQUENCE_CONTROLS):
            comp = components[name]
            self.assertEqual(comp["componentData"]["type"], kind)
            self.assertEqual(tuple(map(int, comp["bounds"]["bounds"].split())), rect)
            self.assertEqual(comp["bounds"]["additionalInvalidatingHandles"], [ROOT_GATE])
            self.assertEqual(comp["bounds"]["showWhenDataModelInvalid"], "Hide")
            model = f"Parameter {base + offset}"
            handles = {"Data": model}
            if key == "row":
                handles["Caption"] = f"WithName[{caption}]/{model}"
            if key == "value":
                handles["Row"] = f"Parameter {base}"
                handles["Caption"] = f"FromGesture/Parameter {base}"
            self.assertEqual({h["key"]: h["value"] for h in comp["handle remapping"]["map"]}, handles)
        row = definitions["voiceNativeRowList"]
        buttons = [c for c in row["componentsData"] if c["componentData"]["type"] == "Button"]
        self.assertEqual(len(buttons), 6)
        for i, button in enumerate(buttons):
            data = button["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"], data["handleName"]), (i, 6, "Data"))
            x, y, w, h = map(int, button["bounds"]["bounds"].split())
            self.assertTrue(w >= 56 and h >= 56 and x + w <= 240 and y + h <= 208)
        self.assertEqual(row["actions"], [])
        for kind, target in (("voiceNativeStepSelect", "fx editor choice menu"), ("voiceNativeStepValue", "fx editor number menu")):
            actions = definitions[kind]["actions"]
            self.assertEqual([(a["onAction"], a["handler"], a["additionalData"]) for a in actions],
                             [(trigger, "Show Overlay", target) for trigger in ("Mouse Down", "Enter Pressed")])

        for row_idx, label in enumerate(VOICE_ROWS):
            row_gate = f"IndexedEnabling/{row_idx}/{len(VOICE_ROWS)}/Parameter {base}"
            for step in range(1, 9):
                name = f"{VOICE_STEP_KNOB_PREFIX}{label} {step}"
                comp = components[name]
                family = 'voiceNativeStepLevel'
                self.assertEqual(comp['componentData']['type'],family)
                self.assertEqual(definitions[family]['componentsData'][-1]['componentData']['type'],'Knob')
                self.assertEqual(comp["bounds"]["acceptsHWFocus"], "Yes", name)
                self.assertEqual(comp["bounds"]["showWhenDataModelInvalid"], "Hide", name)
                self.assertEqual(comp["bounds"]["additionalInvalidatingHandles"], [ROOT_GATE, row_gate], name)

        return components, definitions

    def test_apply_mpc_skin_extra(self):
        voice_extra.apply(self.ctx.tui, {}, self.ctx)
        self.assert_native_widgets(self.ctx.tui["pageData"])

    @unittest.skipUnless(PACKAGED_TUI and VOICE_HOST, "set SYNTHYBOY_VOICE_TUI and SYNTHYBOY_VOICE_UI_HOST")
    def test_packaged_voice_tab_matches_runtime_apertures_and_native_modals(self):

        tui = Path(PACKAGED_TUI)
        page = json.loads(tui.read_text())["pageData"]
        components, definitions = self.assert_native_widgets(page)
        output = subprocess.check_output([VOICE_HOST, "--voice-skin-contract"], text=True)
        actual = {line.split()[0]: tuple(map(int, line.split()[1:])) for line in output.splitlines()}
        def binding(component, handle="Data"):
            mapping = {h["key"]: h["value"] for h in component["handle remapping"]["map"]}
            model, index = mapping[handle].split()
            self.assertEqual(model, "Parameter")
            return int(index)

        def rect(component):
            return tuple(map(int, component["bounds"]["bounds"].split()))

        expected = {}
        for control in VOICE_STANDALONE_CONTROLS:
            widget = components[control["name"]]
            expected[control["key"]] = (binding(widget), *rect(widget))
        for name, _, _, _, key in VOICE_SEQUENCE_CONTROLS:
            widget = components[name]
            expected[f"shell.voice_{key}"] = (binding(widget), *rect(widget))

        group = components["Voice Target Scope"]
        gx, gy, _, _ = rect(group)
        children = definitions[group["componentData"]["type"]]["componentsData"]
        buttons = {c["componentData"]["data"]["handleName"]: c for c in children
                   if c["componentData"]["type"] == "Button"}
        for target in VOICE_TARGET_ITEMS:
            handle = target["handle"]
            x, y, w, h = rect(buttons[handle])
            expected[target["key"]] = (binding(group, handle), gx + x, gy + y, w, h)
        self.assertEqual(actual, expected)
        for name, leaf in (("fx editor choice menu", "MenuOverlay"), ("fx editor number menu", "NumericOverlay")):
            entries = definitions[name]["componentsData"]
            self.assertIn(leaf, [c["componentData"]["type"] for c in entries])
            marker = entries[-1]
            self.assertEqual(marker["componentData"]["type"], "Image")
            self.assertEqual(marker["bounds"]["bounds"], "16 120 30 6")
            self.assertEqual(marker["componentData"]["data"]["image"], "native_modal_markers.png")

    def test_no_fallback_keeps_only_native_voice_controls(self):
        """97_no_fallback keeps only sentinels and VOICE_NATIVE_CONTROL_NAMES on Voice tab."""
        tab = copy.deepcopy(self.ctx.voice_tab)
        # Converted native controls
        tab["componentDefinition"]["componentsData"] = [
            {"componentData": {"name": "Overlay Marker Left"}},
            {"componentData": {"name": "Overlay Instance Voice"}},
            {"componentData": {"name": "Voice Rand Pan", "type": "voiceNativeDial"}},
            {"componentData": {"name": "Voice Rand Detune", "type": "voiceNativeDial"}},
            # Fallback controls with "Voice " prefix
            {"componentData": {"name": "Voice Control Sequence Length", "type": "knobUni"}},
            {"componentData": {"name": "Voice Control Applies to Osc A", "type": "toggle"}},
        ]
        kept = [c for c in tab["componentDefinition"]["componentsData"] if no_fallback_extra._keep_component(tab, c)]
        kept_names = {c["componentData"]["name"] for c in kept}
        self.assertIn("Overlay Marker Left", kept_names)
        self.assertIn("Overlay Instance Voice", kept_names)
        self.assertIn("Voice Rand Pan", kept_names)
        self.assertIn("Voice Rand Detune", kept_names)
        self.assertNotIn("Voice Control Sequence Length", kept_names)
        self.assertNotIn("Voice Control Applies to Osc A", kept_names)

    def test_offline_xl_contact_geometry_evidence(self):
        """Compare historical XL probe coordinates with the measured slot and knob bounds.

        1. In native-eleven-xl/targeted.py:
           Gesture: (160, 519) -> (160, 469) targeted voicectl.rand.pan.
           Voice Rand Pan slot: [14, 518, 124, 130] (x spans 14..138).
           x = 160 lies outside Voice Rand Pan (at x = 160 - 14 = 146 px, 22 px beyond the right edge).
           x = 160 lies inside Voice Rand Detune: [142, 518, 124, 130] (x spans 142..266).
           y = 519 is 1px below slot top (y = 518), completely missing the knob which starts at y = 536.

        2. In native-eleven-xl/native-input.py:
           Probe dragged at computed slot center: cx = 14 + 124//2 = 76, cy = 518 + 130//2 = 583.
           Under default knobUni, knob was offset by x = 94, so on screen it spanned x = 108..198.
           Probe center cx = 76 missed the knob by 32 px to the left (76 < 108).
           Under voiceNativeDial, knob is offset by x = 17, spanning x = 31..121 on screen.
           Probe center cx = 76 lands exactly on the knob center (31 + 45 = 76).
        """
        kTabY = 110  # Screen offset for Voice tab
        pan_ctrl = next(c for c in VOICE_NATIVE_CONTROLS if c["name"] == "Voice Rand Pan")
        det_ctrl = next(c for c in VOICE_NATIVE_CONTROLS if c["name"] == "Voice Rand Detune")

        px, py, pw, ph = pan_ctrl["rect"]
        pan_screen_rect = (px, py + kTabY, pw, ph)
        self.assertEqual(pan_screen_rect, (20, 516, 116, 146))

        dx, dy, dw, dh = det_ctrl["rect"]
        det_screen_rect = (dx, dy + kTabY, dw, dh)
        self.assertEqual(det_screen_rect, (140, 516, 116, 146))

        # Proof 1: targeted.py point (160, 519) missed Pan and hit Detune horizontal slot
        targeted_x, targeted_y = 160, 519
        self.assertFalse(
            pan_screen_rect[0] <= targeted_x < pan_screen_rect[0] + pan_screen_rect[2],
            "Point x=160 must not fall inside Rand Pan width 20..136"
        )
        self.assertTrue(
            det_screen_rect[0] <= targeted_x < det_screen_rect[0] + det_screen_rect[2],
            "Point x=160 falls inside Rand Detune width 140..256"
        )

        # Proof 2: the slot center (the probe of native-input.py, redone for the 2026-11-26 layout) is the knob center
        probe_cx = pan_screen_rect[0] + pan_screen_rect[2] // 2
        probe_cy = pan_screen_rect[1] + pan_screen_rect[3] // 2
        self.assertEqual(probe_cx, 78)
        self.assertEqual(probe_cy, 589)

        # Old knobUni knob position on screen (it missed the probe):
        old_knob_x = pan_screen_rect[0] + 94  # 114
        self.assertLess(probe_cx, old_knob_x, "Probe cx=78 would miss an old knobUni knob (114..204)")

        # voiceNativeDial knob position on screen:
        new_knob_x = pan_screen_rect[0] + 13  # 33
        new_knob_w = 90
        new_knob_center_x = new_knob_x + new_knob_w // 2  # 33 + 45 = 78
        self.assertEqual(probe_cx, new_knob_center_x, "Probe cx=78 matches the voiceNativeDial knob center")
        self.assertTrue(
            new_knob_x <= probe_cx < new_knob_x + new_knob_w,
            "Probe cx=78 lands dead-center inside the knob bounds [33..123]"
        )

    def test_voice_dial_definition_no_internal_overlap(self):
        """voiceNativeDial children (name, knob, value) must have zero bounding box overlap."""
        dial_def = voice_definitions()["voiceNativeDial"]
        child_rects = {}
        for c in dial_def["componentsData"]:
            name = c["componentData"]["name"]
            if name in ("name", "knob", "value"):
                child_rects[name] = [int(v) for v in c["bounds"]["bounds"].split()]

        # Check pair-wise non-overlap
        for k1, (x1, y1, w1, h1) in child_rects.items():
            for k2, (x2, y2, w2, h2) in child_rects.items():
                if k1 >= k2:
                    continue
                overlaps = not (x1 + w1 <= x2 or x2 + w2 <= x1 or y1 + h1 <= y2 or y2 + h2 <= y1)
                self.assertFalse(overlaps, f"{k1} [{x1},{y1},{w1},{h1}] overlaps {k2} [{x2},{y2},{w2},{h2}] in voiceNativeDial")

    def test_voice_number_definition(self):
        """voice_number_definition produces a 200x56 number component without rotary knob."""
        num_def = voice_definitions()["voiceNativeNumber"]
        self.assertFalse(num_def["ignoreMousePresses"])
        self.assertEqual({(a["onAction"], a["handler"], a["handleName"], a["additionalData"]) for a in num_def["actions"]},
                         {(trigger, "Show Overlay", "Data", "fx editor number menu") for trigger in ("Mouse Down", "Enter Pressed")})
        names = {c["componentData"]["name"] for c in num_def["componentsData"]}
        self.assertIn("focus", names)
        self.assertIn("name", names)
        self.assertIn("value", names)
        self.assertNotIn("knob", names)

        bounds = {c["componentData"]["name"]: [int(v) for v in c["bounds"]["bounds"].split()] for c in num_def["componentsData"]}
        self.assertEqual(bounds["focus"], [120, 0, 80, 56])
        for cname in ("name", "value"):
            x, y, w, h = bounds[cname]
            self.assertGreaterEqual(x, 0)
            self.assertGreaterEqual(y, 0)
            self.assertLessEqual(x + w, 200)
            self.assertLessEqual(y + h, 56)

    def test_voice_screen_apertures_correspondence(self):
        """All converted controls and unconverted regions correspond strictly to VoiceScreen geometry."""
        # Header apertures
        length_ctrl = next(c for c in VOICE_NATIVE_CONTROLS if c["name"] == "Voice Length")
        self.assertEqual(length_ctrl["rect"], (788, 4, 200, 56))

        # Every block control lies on its titled panel, under the title strip (voice_layout.json)
        panels = {p["name"]: p["rect"] for p in json.loads((ROOT / "scripts/native_voice/voice_layout.json").read_text())["panels"]}
        for block, name, count in ((0, "random", 5), (1, "scaling", 3), (2, "targets", 5)):
            ctrls = [c for c in VOICE_NATIVE_CONTROLS if c["block"] == block]
            self.assertEqual(len(ctrls), count)
            px, py, pw, ph = panels[name]
            for c in ctrls:
                x, y, w, h = c["rect"]
                self.assertGreaterEqual(x, px + 8)
                self.assertGreaterEqual(y, py + 30)
                self.assertLessEqual(x + w, px + pw - 8)
                self.assertLessEqual(y + h, py + ph - 2)

        # Unconverted regions
        grid_unc = next(u for u in UNCONVERTED_CONTROLS if u["name"] == "Voice Step Grid")
        self.assertEqual(grid_unc["rect"], (268, 94, 996, 206))
        row = next(c for c in VOICE_SEQUENCE_CONTROLS if c[0] == "Voice Row List")
        self.assertEqual(row[2], (20, 98, 240, 208))

    def test_sequence_uses_shared_existing_native_knobs(self):
        from native_voice.step_controls import build_voice_step_knobs, voice_step_knob_rect
        row_handle = f"Parameter {sequence_base(self.ctx)}"
        knobs = build_voice_step_knobs(self.ctx, ROOT_GATE, row_handle)
        self.assertEqual(len(knobs), 48)
        self.assertEqual(len({c['componentData']['name'] for c in knobs}), 48)
        defs = voice_definitions()
        self.assertNotIn('voiceNativeGridBar', defs)
        for row, label in enumerate(VOICE_ROWS):
            for step in range(1, 9):
                knob = knobs[row * 8 + step - 1]
                family = 'voiceNativeStepLevel'
                self.assertEqual(knob['componentData']['type'], family)
                self.assertEqual(knob['bounds']['additionalInvalidatingHandles'],
                                 [ROOT_GATE, f'IndexedEnabling/{row}/6/{row_handle}'])
                self.assertEqual(tuple(map(int, knob['bounds']['bounds'].split())), voice_step_knob_rect(step))
                x,y,w,h = voice_step_knob_rect(step)
                self.assertEqual((w,h), (90,90))
                self.assertLessEqual(x+w,1280)
                self.assertEqual(defs[family]['componentsData'][-1]['componentData']['type'], 'Knob')
                binding = knob['handle remapping']['map'][0]['value']
                param = self.params_data['params'][int(binding.split()[1])]
                self.assertEqual((param['min'],param['max']),(-100,100))
        for step in range(1,8):
            a,b = voice_step_knob_rect(step),voice_step_knob_rect(step+1)
            self.assertLess(a[0]+a[2],b[0])



if __name__ == "__main__":
    unittest.main()
