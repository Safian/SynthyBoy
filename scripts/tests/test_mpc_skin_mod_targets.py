"""Shared modulation editor geometry, bindings, and passive label ownership."""
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_mpc_skin_extras as base


CONTRACT = base.REAL_EXTRAS / "mod-targets-native-contract.json"
OWNERS = {"Macros": 1, "Env": 2, "Env 2": 2, "Env 3": 2, "Env 4": 2, "Env 5": 2, "Env 6": 2, "LFO": 3, "Filter": 4, "Filter 2": 4, "Note": 4, "Velo": 4, "Mod": 5, "Play": 6,
          "Osc": 7, "Osc A": 7, "Osc B": 7, "Osc C": 7, "Sub": 7, "Noise": 7,
          "Mix": 8, "Mixer": 8}


def rect(item):
    return list(map(int, item["bounds"]["bounds"].split()))


def remap(item):
    return {p["key"]: p["value"] for p in item["handle remapping"]["map"]}


class ModTargetsContractTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SYNTHYBOY_ENV_UI_HOST"), "set SYNTHYBOY_ENV_UI_HOST")
    def test_matches_the_cpp_editor(self):
        actual = subprocess.check_output([os.environ["SYNTHYBOY_ENV_UI_HOST"], "--mod-targets-skin-contract"])
        self.assertEqual(actual, CONTRACT.read_bytes())


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found")
class NativeModTargetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        if NoFallbackTests.code:
            raise AssertionError(NoFallbackTests.err)
        cls.skin = NoFallbackTests.skins
        cls.page = NoFallbackTests.tui["pageData"]
        cls.definitions = {d["key"]: d["value"] for d in cls.page["componentDefinitions"]["localComponentDefinitions"]}

    def test_each_owner_mounts_the_same_isolated_editor(self):
        active = 0
        for tab in self.page["tabs"]:
            components = tab["componentDefinition"]["componentsData"]
            widgets = [c for c in components if c["componentData"]["type"] == "nativeModulationEditor"]
            frame = OWNERS.get(tab["tabName"])
            self.assertEqual(len(widgets), 0 if frame is None else 1, tab["tabName"])
            if frame is None:
                continue
            active += 1
            widget, = widgets
            self.assertEqual(rect(widget), [0, 0, 1280, 628])
            self.assertEqual(widget["bounds"]["acceptsHWFocus"], "Children")
            self.assertEqual(remap(widget), {"Data": "Parameter 3529"})
            self.assertEqual(widget["bounds"]["additionalInvalidatingHandles"],
                             [f"IndexedEnabling/{frame}/9/Parameter 3529"])
            self.assertGreater(32, max(rect(c)[1] + rect(c)[3] for c in components
                                       if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))))
        self.assertEqual(active, 20)

    def test_six_rows_bind_exact_hitboxes_and_do_not_overlap(self):
        manifest = json.loads(CONTRACT.read_text())
        parent = self.definitions["nativeModulationEditor"]
        route = self.definitions["nativeModulationRoute"]
        self.assertTrue(parent["ignoreMousePresses"] and route["ignoreMousePresses"])
        by = {c["componentData"]["name"]: c for c in parent["componentsData"]}
        self.assertEqual(rect(by["BACK"]), manifest["back"])
        self.assertEqual(rect(by["Add"]), manifest["add"])
        self.assertEqual(remap(by["BACK"]), {"Data": "FromGesture/Parameter 3502"})
        self.assertEqual(remap(by["Add"]), {"Data": "FromGesture/Parameter 3503", "Caption": "Parameter 3503"})
        self.assertEqual(rect(by["Page arrows"]), manifest["page"])
        for i, name in enumerate(("Previous", "Next"), 1):
            field = by[name]
            self.assertEqual(field["bounds"]["additionalInvalidatingHandles"],
                             [f"RangedEnabling/{i}/{i + 1}/4/Parameter 3530"])
            data = field["componentData"]["data"]
            self.assertEqual((data["buttonId"], data["numButtonsInGroup"]), (i, 4))
            self.assertTrue(remap(field)["Data"].endswith("FromGesture/Parameter 3504"))
        y = manifest["row"][0][1]
        for row in range(6):
            widget = by[f"Route {row + 1}"]
            self.assertEqual(rect(widget), [0, y + row * manifest["stride"], 1280, 56])
            self.assertEqual(widget["bounds"]["additionalInvalidatingHandles"], [f"NonZero/Parameter {3531 + row}"])
            expected = {name: f"Parameter {3505 + 4 * row + i}" for i, name in
                        enumerate(("Amount", "Polarity", "Active", "Remove"))}
            self.assertEqual(remap(widget), {"Data": f"Parameter {3531 + row}", "Present": f"Parameter {3531 + row}", **expected})
        widgets = {c["componentData"]["name"]: c for c in route["componentsData"]}
        for key, expected in zip(("Target", "Amount", "Polarity", "Active", "Remove"), manifest["row"]):
            x, top, w, h = expected
            self.assertEqual(rect(widgets[key]), [x, top - y, w, h])
        self.assertEqual(widgets["Target"]["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/2/3/Present"])
        self.assertEqual(remap(widgets["Amount"]), {"Data": "Amount"})
        self.assertEqual(widgets["Amount"]["bounds"]["acceptsHWFocus"], "Children")
        amount = self.definitions[widgets["Amount"]["componentData"]["type"]]
        slider, = [child for child in amount["componentsData"] if child["componentData"]["type"] == "Slider"]
        data = slider["componentData"]["data"]
        self.assertEqual((data["sliderType"], data["direction"], data["revealType"], data["handleName"]),
                         ("TrackAndThumb", "Horizontal", "Bipolar", "Data"))
        self.assertNotIn("Depth", widgets)
        for control in ("Polarity", "Active", "Remove"):
            widget = widgets[control]
            self.assertEqual(widget["componentData"]["type"], "Button")
            self.assertEqual(remap(widget), {"Data": f"FromGesture/{control}"})
            self.assertEqual(widget["bounds"]["acceptsHWFocus"], "Yes")

    def test_passive_label_sharing_preserves_the_ordered_skin(self):
        spec = importlib.util.spec_from_file_location("mod_targets_extras_loader", base.APPLY)
        loader = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(loader)
        with tempfile.TemporaryDirectory() as work:
            from test_mpc_skin_no_fallback import OVERLAY_LAYOUT

            root = Path(work)
            extras = root / "extras"
            extras.mkdir()
            for extra in base.REAL_EXTRAS.iterdir():
                if extra.name != "99_shared_labels.py":
                    (extras / extra.name).symlink_to(extra)
            skins = base.generate_skins(root, OVERLAY_LAYOUT)
            self.assertEqual(base.run_apply(skins, extras), (0, ""))
            expected = loader.load_json((skins / "TUI.json").read_text(encoding="utf-8"))
        actual = loader.load_json((self.skin / "TUI.json").read_text(encoding="utf-8"))
        page = actual["pageData"]
        definitions = page["componentDefinitions"]["localComponentDefinitions"]
        shared = {d["key"]: d["value"] for d in definitions if d["key"].startswith("sharedNativeLabel")}
        self.assertGreaterEqual(len(shared), 2)
        definitions[:] = [d for d in definitions if d["key"] not in shared]
        surfaces = [definition["value"] for definition in definitions]
        surfaces.extend(tab["componentDefinition"] for tab in page["tabs"])
        changed = 0
        for surface in surfaces:
            for entry in surface.get("componentsData", []):
                model = entry["componentData"]
                if model["type"] in shared:
                    leaf = base.leaf_model(entry, shared)
                    self.assertFalse(shared[model["type"]]["disableCoarseDataWheel"])
                    self.assertEqual(entry["bounds"]["showWhenDataModelInvalid"], "Show")
                    entry["bounds"]["showWhenDataModelInvalid"] = shared[model["type"]]["componentsData"][0]["bounds"]["showWhenDataModelInvalid"]
                    model["type"], model["data"] = leaf["type"], leaf["data"]
                    changed += 1
        self.assertGreaterEqual(changed, 81)
        def assert_ordered(actual, expected, path="TUI"):
            if isinstance(expected, dict):
                self.assertEqual(list(actual), list(expected), path)
                for key in expected:
                    assert_ordered(actual[key], expected[key], f"{path}.{key}")
            elif isinstance(expected, list):
                self.assertEqual(len(actual), len(expected), path)
                for i, (a, b) in enumerate(zip(actual, expected)):
                    assert_ordered(a, b, f"{path}[{i}]")
            else:
                self.assertEqual(actual, expected, path)

        assert_ordered(actual, expected)


class MixerPeekModTargetsSkinTest(unittest.TestCase):
    def test_mixer_mounts_clipped_native_modulation_editor_with_own_frame(self):
        mod = base.apply_mod.load_module(base.REAL_EXTRAS / "98_mod_targets.py")
        params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))
        layout = json.loads((base.ROOT / "shell-mpc" / "overlay" / "overlay_layout.json").read_text(encoding="utf-8"))
        mixer_view = next(v for v in layout["views"] if v["name"] == "mixer")
        mixer_markers = [
            {"componentData": {"name": "Overlay Marker mixer left", "type": "Image", "data": {}},
             "bounds": {"bounds": f"{m['x']} {m['y']} {m['w']} {m['h']}"}}
            for m in mixer_view["markers"]
        ] + [
            {"componentData": {"name": "Overlay Instance mixer", "type": "Image", "data": {}},
             "bounds": {"bounds": f"{mixer_view['instance']['x']} {mixer_view['instance']['y']} {mixer_view['instance']['w']} {mixer_view['instance']['h']}"}}
        ]
        tab_names = ("Macros", "Env", "LFO", "Filter", "Mod", "Play", "Osc A", "Mix", "Master")
        tui = {
            "pageData": {
                "componentDefinitions": {"localComponentDefinitions": []},
                "tabs": [
                    {
                        "tabName": name,
                        "componentDefinition": {
                            "componentsData": list(mixer_markers) if name == "Mix" else []
                        },
                    }
                    for name in tab_names
                ],
            }
        }
        with tempfile.TemporaryDirectory() as tmp:
            ctx = base.apply_mod.Context(tui, params, Path(tmp), meta={"variant": "overlay"})
            mod.apply(tui, {}, ctx)

        tabs = {t["tabName"]: t["componentDefinition"]["componentsData"] for t in tui["pageData"]["tabs"]}
        mix_widgets = [c for c in tabs["Mix"] if c["componentData"]["type"] == "nativeModulationEditor"]
        filter_widgets = [c for c in tabs["Filter"] if c["componentData"]["type"] == "nativeModulationEditor"]
        master_widgets = [c for c in tabs["Master"] if c["componentData"]["type"] == "nativeModulationEditor"]
        self.assertEqual(len(mix_widgets), 1)
        self.assertEqual(len(filter_widgets), 1)
        self.assertEqual(master_widgets, [])

        mix_editor = mix_widgets[0]
        self.assertEqual(rect(mix_editor), [0, 0, 1280, 628])
        self.assertEqual(mix_editor["bounds"]["acceptsHWFocus"], "Children")
        self.assertEqual(remap(mix_editor), {"Data": "Parameter 3529"})
        self.assertEqual(mix_editor["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/8/9/Parameter 3529"])
        self.assertEqual(filter_widgets[0]["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/4/9/Parameter 3529"])

        defs = {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        editor_def = defs["nativeModulationEditor"]
        route_def = defs["nativeModulationRoute"]
        # Mixer PEEK panel cutout is (4, 4, 1272, 620) in tab-local coordinates; markers occupy y=24..30.
        cutout = (4, 4, 1276, 624)
        for child in editor_def["componentsData"]:
            x, y, w, h = rect(child)
            if child["componentData"]["type"] == "nativeModulationRoute":
                self.assertGreaterEqual(y, 32, child["componentData"]["name"])
                for sub in route_def["componentsData"]:
                    sx, sy, sw, sh = rect(sub)
                    self.assertTrue(cutout[0] <= x + sx and x + sx + sw <= cutout[2]
                                    and cutout[1] <= y + sy and y + sy + sh <= cutout[3],
                                    f"{child['componentData']['name']}.{sub['componentData']['name']}")
            else:
                self.assertTrue(cutout[0] <= x and x + w <= cutout[2] and cutout[1] <= y and y + h <= cutout[3],
                                child["componentData"]["name"])
                self.assertGreaterEqual(y, 32, child["componentData"]["name"])
            for target in remap(child).values():
                if target.startswith("Parameter ") or "/Parameter " in target:
                    idx = int(target.split("Parameter ")[-1])
                    self.assertTrue(3502 <= idx <= 3536, target)
