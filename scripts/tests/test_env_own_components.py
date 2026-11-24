#!/usr/bin/env python3
"""Planner-owned acceptance for the ENV native component migration."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tests.test_ui_screen_env import UiScreenEnvTest


class EnvOwnComponentsTest(UiScreenEnvTest):
    def test_real_blue_filmstrips_and_value_labels(self):
        defs = self.local_defs()
        strips = []
        for kind in ("envNativeDial", "envNativeCurve"):
            definition = defs[kind]
            children = [c["componentData"] for c in definition["componentsData"]]
            knobs = [c for c in children if c["type"] == "Knob"]
            self.assertEqual(len(knobs), 1)
            data = knobs[0]["data"]
            self.assertEqual(data.get("knobType"), "FilmStrip",
                             "ENV must use our actual blue Level artwork")
            filename = data["filmStrip"]
            self.assertNotIn("knob_uni", filename)
            self.assertTrue((self.skins / filename).is_file())
            self.assertGreater(data["numFrames"], 2)
            strips.append(filename)
            self.assertTrue(any(c["type"] == "Label" and
                                c["data"].get("type") == "Value" for c in children),
                            "Every dial must show its bound value")
        self.assertNotEqual(*strips, "Bipolar curves must not overwrite unipolar art")

    def test_switches_use_shared_active_text_with_real_hitboxes(self):
        defs = self.local_defs()
        fields = defs["envNativeFullFields"]["componentsData"]
        for name in ("Beats", "Retrig"):
            entry = next(c for c in fields if c["componentData"]["name"] == f"Env Native {name}")
            definition = defs[entry["componentData"]["type"]]
            buttons = [c for c in definition["componentsData"]
                       if c["componentData"]["type"] == "Button"]
            self.assertEqual(len(buttons), 1)
            button = buttons[0]
            _, _, w, h = map(int, button["bounds"]["bounds"].split())
            self.assertGreaterEqual(w, 56)
            self.assertGreaterEqual(h, 56)
            data = button["componentData"]["data"]
            self.assertNotEqual(data["onImage"], data["offImage"])
            for key in ("onImage", "offImage"):
                self.assertTrue((self.skins / data[key]).is_file())

    def test_state_buttons_have_one_bound_native_write_owner(self):
        defs = self.local_defs()
        for kind in ('envNativeBeatsSwitch', 'envNativeRetrigSwitch', 'envNativeTargets'):
            definition = defs[kind]
            self.assertEqual(definition['actions'], [])
            self.assertTrue(definition['ignoreMousePresses'])
            child = definition['componentsData'][0]
            self.assertEqual(child['componentData']['type'], 'Button')
            self.assertEqual(child['componentData']['data']['handleName'], 'Data')
        for kind in ('envNativeDial', 'envNativeCurve'):
            names = [c for c in defs[kind]['componentsData'] if c['componentData']['name'] == 'name']
            self.assertEqual(len(names),1)
            self.assertEqual(names[0]['componentData']['data']['handleName'],'Caption')

    def test_hooks_use_component_apis_without_duplicate_implementation(self):
        root = Path(__file__).resolve().parents[2]
        canonical = (root / "scripts/mpc-skin-extras/96_env_native.py").read_text()
        self.assertIn("components.native.active_text", canonical)
        self.assertIn("components.native.level", canonical)
        self.assertNotIn("ImageDraw", canonical)


if __name__ == "__main__":
    import unittest
    unittest.main()
