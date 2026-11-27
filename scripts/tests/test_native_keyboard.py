# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Unit tests for native MPC touch keyboard component, geometry, and skin extra hook."""

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import components.native.keyboard.keyboard as kbd

# Load 96_native_keyboard dynamically due to hyphen in folder name
extra_path = REPO_ROOT / "scripts/mpc-skin-extras/96_native_keyboard.py"
spec = importlib.util.spec_from_file_location("native_keyboard_extra", extra_path)
extra_mod = importlib.util.module_from_spec(spec)
sys.path.insert(0, str(REPO_ROOT / "scripts"))
spec.loader.exec_module(extra_mod)


class TestNativeKeyboard(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.tmp_dir.name)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_key_codes_and_ascii_mapping(self):
        """Verify ASCII to key code mapping (1..95) and special control codes."""
        # 1. Printable ASCII 32..126 maps to 1..95
        self.assertEqual(kbd.ascii_to_key_code(" "), 1)
        self.assertEqual(kbd.ascii_to_key_code("!"), 2)
        self.assertEqual(kbd.ascii_to_key_code("0"), 17)
        self.assertEqual(kbd.ascii_to_key_code("9"), 26)
        self.assertEqual(kbd.ascii_to_key_code("A"), 34)
        self.assertEqual(kbd.ascii_to_key_code("Z"), 59)
        self.assertEqual(kbd.ascii_to_key_code("a"), 66)
        self.assertEqual(kbd.ascii_to_key_code("z"), 91)
        self.assertEqual(kbd.ascii_to_key_code("~"), 95)

        # Round-trip for every printable character
        for code in range(32, 127):
            ch = chr(code)
            kcode = kbd.ascii_to_key_code(ch)
            self.assertIsNotNone(kcode)
            self.assertEqual(kbd.key_code_to_ascii(kcode), ch)

        # Non-ASCII or control characters return None
        self.assertIsNone(kbd.ascii_to_key_code("\n"))
        self.assertIsNone(kbd.ascii_to_key_code("\x00"))

        # 2. Control codes match research specification
        self.assertEqual(kbd.KEY_IDLE, 0)
        self.assertEqual(kbd.KEY_SPACE, 1)
        self.assertEqual(kbd.KEY_BACKSPACE, 96)
        self.assertEqual(kbd.KEY_CLEAR, 97)
        self.assertEqual(kbd.KEY_OK, 98)
        self.assertEqual(kbd.KEY_CANCEL, 99)
        self.assertEqual(kbd.NUM_KEY_BUTTONS, 100)

    def test_keyboard_overlay_structure_and_dimensions(self):
        """Verify build_keyboard_overlay_definition creates standard SDK 6.12 overlay with 4 rows."""
        defn = kbd.build_keyboard_overlay_definition(
            skins_dir=self.skins_dir,
            context_param=963,
            key_param=964,
            text_param=965,
        )

        self.assertEqual(defn.get("version"), 2)
        self.assertTrue(defn.get("ignoreMousePresses"))
        self.assertFalse(defn.get("disableCoarseDataWheel"))
        self.assertEqual(len(defn.get("actions", [])), 1)
        self.assertEqual(defn["actions"][0]["handler"], "Focus Lost")

        comps = defn["componentsData"]
        self.assertTrue(len(comps) > 50)

        # 1. Panel Box
        panel = next((c for c in comps if c["componentData"]["name"] == "Keyboard Panel Box"), None)
        self.assertIsNotNone(panel)
        self.assertEqual(panel["componentData"]["type"], "Image")
        self.assertEqual(panel["bounds"]["bounds"], f"{kbd.PANEL_X} {kbd.PANEL_Y} {kbd.PANEL_WIDTH} {kbd.PANEL_HEIGHT}")

        # 2. Title and Text Display Labels
        title = next((c for c in comps if c["componentData"]["name"] == "Keyboard Title"), None)
        self.assertIsNotNone(title)
        self.assertEqual(title["componentData"]["type"], "Label")
        self.assertEqual(title["componentData"]["data"]["type"], "Value")
        self.assertEqual(title["componentData"]["data"]["handleName"], "Parameter 963")

        text_disp = next((c for c in comps if c["componentData"]["name"] == "Keyboard Text Display"), None)
        self.assertIsNotNone(text_disp)
        self.assertEqual(text_disp["componentData"]["type"], "Label")
        self.assertEqual(text_disp["componentData"]["data"]["type"], "Value")
        self.assertEqual(text_disp["componentData"]["data"]["handleName"], "Parameter 965")

        # 3. Keys: All rows have 40 px height
        key_buttons = [c for c in comps if c["componentData"]["type"] == "Button"]
        for kb in key_buttons:
            bounds = list(map(int, kb["bounds"]["bounds"].split()))
            # Key height must be 40 px (or 32 px for close icon button)
            if kb["componentData"]["name"] == "Keyboard Header Close":
                self.assertEqual(bounds[3], 32)
            else:
                self.assertEqual(bounds[3], 40)

        # 4. Verify parameter binding
        key_handle = "ResetOnGestureEnd/Parameter 964"
        momentary_keys = [
            c for c in key_buttons
            if any(m.get("value") == key_handle for m in c.get("handle remapping", {}).get("map", []))
        ]
        self.assertTrue(len(momentary_keys) >= 70)
        for mk in momentary_keys:
            data = mk["componentData"]["data"]
            self.assertEqual(data["numButtonsInGroup"], 100)
            self.assertEqual(data["gestureBehaviour"], "Instant")
            self.assertIn("onImage", data)
            self.assertIn("offImage", data)
            # Verify file was written to skins_dir
            on_file = self.skins_dir / data["onImage"]
            self.assertTrue(on_file.is_file(), f"Missing skin asset {data['onImage']}")

    def test_layer_switching_and_indexed_enabling(self):
        """Verify layer radio buttons and IndexedEnabling gates for layers 0, 1, 2."""
        defn = kbd.build_keyboard_overlay_definition(
            skins_dir=self.skins_dir,
            context_param=963,
            key_param=964,
            text_param=965,
        )
        comps = defn["componentsData"]

        layer_buttons = [
            c for c in comps
            if any(m.get("value") == kbd.LAYER_HANDLE for m in c.get("handle remapping", {}).get("map", []))
        ]
        # Should have Shift buttons and ?123/ABC buttons
        self.assertTrue(len(layer_buttons) >= 5)
        for lb in layer_buttons:
            data = lb["componentData"]["data"]
            self.assertEqual(data["numButtonsInGroup"], 3)
            self.assertIn(data["buttonId"], (0, 1, 2))

        # Check layer gating
        l0_keys = [c for c in comps if f"IndexedEnabling/0/3/{kbd.LAYER_HANDLE}" in c.get("bounds", {}).get("additionalInvalidatingHandles", [])]
        l1_keys = [c for c in comps if f"IndexedEnabling/1/3/{kbd.LAYER_HANDLE}" in c.get("bounds", {}).get("additionalInvalidatingHandles", [])]
        l2_keys = [c for c in comps if f"IndexedEnabling/2/3/{kbd.LAYER_HANDLE}" in c.get("bounds", {}).get("additionalInvalidatingHandles", [])]

        self.assertEqual(len(l0_keys), 28)  # 10 row1 + 9 row2 + 7 row3 + shift + sym
        self.assertEqual(len(l1_keys), 28)
        self.assertEqual(len(l2_keys), 28)

        # Common action buttons have no layer gate (visible on all layers)
        for name in ("Key Clear", "Key Space", "Key Cancel", "Key OK", "Key Backspace"):
            btn = next((c for c in comps if c["componentData"]["name"] == name), None)
            self.assertIsNotNone(btn, f"Missing common button {name}")
            self.assertNotIn("additionalInvalidatingHandles", btn.get("bounds", {}))

    def test_catalog_entry_and_preview_image(self):
        """Verify catalog.json and preview image exist and conform to standards."""
        cat_file = REPO_ROOT / "components/native/keyboard/catalog.json"
        self.assertTrue(cat_file.is_file())
        with open(cat_file, "r", encoding="utf-8") as f:
            meta = json.load(f)
        self.assertTrue(meta.get("approved"))
        self.assertEqual(meta.get("title"), "Keyboard")
        self.assertEqual(meta.get("order"), 85)

        img_file = REPO_ROOT / "components/native/keyboard/img/keyboard-preview.png"
        self.assertTrue(img_file.is_file())
        with Image.open(img_file) as im:
            self.assertEqual(im.format, "PNG")
            self.assertEqual(im.size, (kbd.PANEL_WIDTH, kbd.PANEL_HEIGHT))

    def test_skin_extra_guard_and_apply(self):
        """Verify 96_native_keyboard.py respects SYNTHYBOY_NATIVE_KEYBOARD environment variable."""
        # Baseline TUI without keyboard
        tui = {
            "pageData": {
                "componentDefinitions": {
                    "localComponentDefinitions": []
                }
            }
        }
        ctx = SimpleNamespace(
            meta={"variant": "overlay"},
            param_count=100,
            skins_dir=self.skins_dir,
            _registered={},
            add_param_key=lambda idx, key: None,
            add_component_definition=lambda name, obj: tui["pageData"]["componentDefinitions"]["localComponentDefinitions"].append({"key": name, "value": obj}),
        )

        # 1. When SYNTHYBOY_NATIVE_KEYBOARD is unset -> no modifications
        old_env = os.environ.get("SYNTHYBOY_NATIVE_KEYBOARD")
        try:
            if "SYNTHYBOY_NATIVE_KEYBOARD" in os.environ:
                del os.environ["SYNTHYBOY_NATIVE_KEYBOARD"]

            extra_mod.apply(tui, {}, ctx)
            defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
            self.assertEqual(len(defs), 0)

            # 2. When SYNTHYBOY_NATIVE_KEYBOARD=1 -> registered
            os.environ["SYNTHYBOY_NATIVE_KEYBOARD"] = "1"
            extra_mod.apply(tui, {}, ctx)
            defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
            self.assertEqual(len(defs), 1)
            self.assertEqual(defs[0]["key"], kbd.NATIVE_KEYBOARD_OVERLAY_NAME)
            self.assertIn("componentsData", defs[0]["value"])
        finally:
            if old_env is not None:
                os.environ["SYNTHYBOY_NATIVE_KEYBOARD"] = old_env
            elif "SYNTHYBOY_NATIVE_KEYBOARD" in os.environ:
                del os.environ["SYNTHYBOY_NATIVE_KEYBOARD"]


if __name__ == "__main__":
    unittest.main()
