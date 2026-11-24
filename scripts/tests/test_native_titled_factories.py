# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Focused contract checks for titled Selector and titled ActiveText group factories.

Verifies:
1. Portable imports of public additive factories.
2. Caller-supplied title, options, and bindings (zero hardcoded engine keys).
3. Two differently named instances of shared definition do not share parameter state.
4. Unrestricted child counts (e.g. 2, 5, 8 child toggles).
5. All real Button touch hitboxes >= 56 px with no rectangular overlap.
6. Passive group contract: actions == [] (no parent ToggleSwitch double writes).
7. Voice fields and 5 target bindings strictly preserved.
"""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from components import style
from components.native.selector.selector import (
    TITLED_SELECTOR_SIZE,
    build_titled_selector,
    titled_selector_definition,
)
from components.native.active_text.active_text import (
    build_titled_active_text_group_definition,
    titled_active_text_group_definition,
    titled_active_text_group_entry,
)
from scripts.native_voice.voice_tab import (
    VOICE_NATIVE_CONTROLS,
    VOICE_STANDALONE_CONTROLS,
    VOICE_TARGET_CONTROLS,
    VOICE_TARGET_ITEMS,
    voice_definitions,
    populate_voice_tab,
)


class TestNativeTitledFactories(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.skins_dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_factories_portable_imports(self):
        """Factories can be imported portably as public native component APIs."""
        self.assertTrue(callable(build_titled_selector))
        self.assertIs(titled_selector_definition, build_titled_selector)
        self.assertTrue(callable(build_titled_active_text_group_definition))
        self.assertIs(titled_active_text_group_definition, build_titled_active_text_group_definition)
        self.assertTrue(callable(titled_active_text_group_entry))

    def test_titled_selector_caller_supplied_title_options_and_geometry(self):
        """Caller supplies title, popup overlay, and slot geometry; verified aperture and face."""
        slot = (400, 80)
        custom_popup = "custom choice menu"
        defn = build_titled_selector(
            None,
            popup=custom_popup,
            slot=slot,
            skins_dir=self.skins_dir,
            title="CustomData",
            caption="FILTER TYPE",
            play_face=True,
        )

        # 1. Overlay actions preserved
        actions = defn["actions"]
        self.assertEqual(len(actions), 2)
        for a in actions:
            self.assertEqual(a["handler"], "Show Overlay")
            self.assertEqual(a["additionalData"], custom_popup)

        # 2. Outer title label: 24px height, font 18, horizontally and vertically centered
        title_label = next(c for c in defn["componentsData"] if c["componentData"]["name"] == "name")
        self.assertEqual(title_label["componentData"]["type"], "Label")
        self.assertEqual(title_label["componentData"]["data"].get("text"), "FILTER TYPE")
        self.assertEqual(title_label["bounds"]["bounds"], f"0 0 {slot[0]} 24")
        self.assertEqual(title_label["componentData"]["data"]["textStyle"]["font"]["height"], 18.0)

        # 3. Child selector shifted by 24px, face height 48px, touch hitbox 56px
        face_img = next(c for c in defn["componentsData"] if c["componentData"]["type"] == "Image")
        fx, fy, fw, fh = map(int, face_img["bounds"]["bounds"].split())
        self.assertEqual(fh, 48)
        self.assertEqual(fy, 28)  # 24 title + 4 margin inside 56px child slot
        self.assertEqual(fw, slot[0])

        val_label = next(c for c in defn["componentsData"] if c["componentData"]["name"] == "Title")
        vx, vy, vw, vh = map(int, val_label["bounds"]["bounds"].split())
        self.assertEqual(vh, 48)
        self.assertEqual(vy, 28)
        self.assertEqual(val_label["componentData"]["data"]["handleName"], "CustomData")

    def test_titled_selector_no_hardcoded_keys(self):
        """Generic selector factory contains zero hardcoded FX/LFO/Voice parameter keys."""
        import json
        defn = build_titled_selector(
            None,
            slot=TITLED_SELECTOR_SIZE,
            skins_dir=self.skins_dir,
        )
        serialized = json.dumps(defn).lower()
        for forbidden in ("voicectl", "voicestep", "lfo_mode", "pan_depth", "stepknob"):
            self.assertNotIn(forbidden, serialized)

    def test_titled_active_text_group_unrestricted_child_counts_and_hitboxes(self):
        """Supports unrestricted caller child counts (2, 5, 8 toggles), all hitboxes >= 56px."""
        for count in (2, 5, 8):
            items = [
                {
                    "name": f"btn_{i}",
                    "label": f"OPT {i+1}",
                    "handle": f"Active{i}",
                    "rect": (i * 60, 24, 58, 56),
                }
                for i in range(count)
            ]
            group_w = count * 60
            group_def = build_titled_active_text_group_definition(
                f"group_{count}",
                title=f"COUNT {count}",
                items=items,
                size=(group_w, 80),
                skins_dir=self.skins_dir,
            )

            # Title present
            title = next(c for c in group_def["componentsData"] if c["componentData"]["name"] == "title")
            self.assertEqual(title["componentData"]["data"].get("text"), f"COUNT {count}")

            # Buttons count and geometry
            buttons = [c for c in group_def["componentsData"] if c["componentData"]["type"] == "Button"]
            self.assertEqual(len(buttons), count)

            all_rects = []
            for b in buttons:
                bx, by, bw, bh = map(int, b["bounds"]["bounds"].split())
                self.assertGreaterEqual(bw, 56, "Button width must be >= 56px")
                self.assertGreaterEqual(bh, 56, "Button height must be >= 56px")
                all_rects.append((bx, by, bw, bh))

            # Non-overlapping disjoint targets
            for i in range(len(all_rects)):
                x1, y1, w1, h1 = all_rects[i]
                for j in range(i + 1, len(all_rects)):
                    x2, y2, w2, h2 = all_rects[j]
                    overlaps = not (x1 + w1 <= x2 or x2 + w2 <= x1 or y1 + h1 <= y2 or y2 + h2 <= y1)
                    self.assertFalse(overlaps, f"Button {i} [{x1},{y1},{w1},{h1}] overlaps Button {j} [{x2},{y2},{w2},{h2}]")

    def test_titled_active_text_group_two_instances_dont_share_param_state(self):
        """Two differently named instances of shared definition map distinct parameter state."""
        items = [
            {"label": "ON/OFF", "rect": (0, 24, 60, 56), "handle": "Active0"},
        ]
        # Register shared definition ONCE
        shared_def = build_titled_active_text_group_definition(
            "sharedToggleDef",
            title="SWITCH",
            items=items,
            skins_dir=self.skins_dir,
        )

        # Bind Instance 1 to Parameter 101
        inst1 = titled_active_text_group_entry(
            "Tab A Switch",
            "sharedToggleDef",
            (10, 10, 60, 80),
            mappings=[("Active0", "Parameter 101")],
        )

        # Bind Instance 2 to Parameter 202
        inst2 = titled_active_text_group_entry(
            "Tab B Switch",
            "sharedToggleDef",
            (100, 10, 60, 80),
            mappings=[("Active0", "Parameter 202")],
        )

        map1 = {m["key"]: m["value"] for m in inst1["handle remapping"]["map"]}
        map2 = {m["key"]: m["value"] for m in inst2["handle remapping"]["map"]}

        self.assertEqual(map1["Active0"], "Parameter 101")
        self.assertEqual(map2["Active0"], "Parameter 202")
        self.assertNotEqual(map1["Active0"], map2["Active0"])

    def test_titled_active_text_group_passive_no_double_parent_actions(self):
        """Composite definition is passive: actions is empty, preventing double-write."""
        items = [{"label": "A", "rect": (0, 24, 56, 56)}]
        defn = build_titled_active_text_group_definition("testDef", "TITLE", items, skins_dir=self.skins_dir)
        self.assertEqual(defn["actions"], [], "Group definition must have no parent toggle actions")
        self.assertTrue(defn["ignoreMousePresses"], "Group parent must set ignoreMousePresses=True to preserve child interaction")
        self.assertFalse(defn["disableCoarseDataWheel"])

    def test_titled_selector_validations(self):
        """Titled selector validates positive dimensions, child minimum height, and rejects proportional bounds."""
        # 1. Default size derived from style tokens
        expected_default = (style.size("control.width"), style.size("height.high") + style.size("active_text.default.height"))
        self.assertEqual(TITLED_SELECTOR_SIZE, expected_default)

        # 2. Positive outer dimensions
        with self.assertRaises(ValueError):
            build_titled_selector(None, slot=(0, 80))
        with self.assertRaises(ValueError):
            build_titled_selector(None, slot=(202, -10))

        # 3. Positive title height
        with self.assertRaises(ValueError):
            build_titled_selector(None, slot=(202, 80), title_height=0)

        # 4. Child height >= height.high (56px)
        with self.assertRaises(ValueError):
            build_titled_selector(None, slot=(202, 70), title_height=24)  # child_h = 46 < 56

        # 5. Child + title exceeds outer height
        with self.assertRaises(ValueError):
            build_titled_selector(None, slot=(202, 80), title_height=24, selector_height=60)

        # 6. Proportional boundsType rejected with clear ValueError
        with self.assertRaises(ValueError):
            build_titled_selector(None, slot=(202, 80), boundsType="Proportional")

    def test_titled_active_text_group_validations(self):
        """ActiveText group validates non-empty items, >=56px targets, bounds, non-overlap, unique handles, and passive contract."""
        # 1. Non-empty items
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", [])

        # 2. Undersized buttons (< 56px in w or h)
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", [{"label": "A", "rect": (0, 24, 50, 56)}], skins_dir=self.skins_dir)
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", [{"label": "A", "rect": (0, 24, 56, 40)}], skins_dir=self.skins_dir)

        # 3. Negative coordinates and exceeding group size
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", [{"label": "A", "rect": (-10, 24, 56, 56)}], skins_dir=self.skins_dir)
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", [{"label": "A", "rect": (0, 24, 60, 56)}], size=(50, 80), skins_dir=self.skins_dir)

        # 4. Pairwise child overlap
        overlapping_items = [
            {"label": "A", "rect": (0, 24, 60, 56)},
            {"label": "B", "rect": (30, 24, 60, 56)},
        ]
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", overlapping_items, skins_dir=self.skins_dir)

        # 5. Child overlapping title rect
        overlap_title_items = [
            {"label": "A", "rect": (0, 10, 60, 56)},  # default title at (0, 0, w, 24)
        ]
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", overlap_title_items, skins_dir=self.skins_dir)

        # 6. Duplicate handle identities
        dup_handle_items = [
            {"label": "A", "rect": (0, 24, 56, 56), "handle": "ActiveX"},
            {"label": "B", "rect": (60, 24, 56, 56), "handle": "ActiveX"},
        ]
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", dup_handle_items, skins_dir=self.skins_dir)

        # 7. Non-empty actions rejected
        valid_items = [{"label": "A", "rect": (0, 24, 56, 56)}]
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", valid_items, actions=[{"handler": "ToggleSwitch"}], skins_dir=self.skins_dir)

        # 8. skins_dir is None with missing assets raises explicit error
        with self.assertRaises(ValueError):
            build_titled_active_text_group_definition("test", "TITLE", valid_items, skins_dir=None)

        # 9. Provided asset filenames preserved without requiring skins_dir
        custom_items = [{"label": "A", "rect": (0, 24, 56, 56), "on_image": "custom_on.png", "off_image": "custom_off.png"}]
        custom_def = build_titled_active_text_group_definition("test", "TITLE", custom_items, skins_dir=None)
        btn_data = custom_def["componentsData"][1]["componentData"]["data"]
        self.assertEqual(btn_data["onImage"], "custom_on.png")
        self.assertEqual(btn_data["offImage"], "custom_off.png")

    def test_titled_active_text_group_asset_digests_prevent_collision(self):
        """Duplicate caption with different dimensions get unique digests and do not overwrite each other."""
        from PIL import Image
        items1 = [{"label": "FILTER", "rect": (0, 24, 60, 56)}]
        items2 = [{"label": "FILTER", "rect": (0, 24, 80, 56)}]

        def1 = build_titled_active_text_group_definition("grp1", "TITLE", items1, skins_dir=self.skins_dir)
        def2 = build_titled_active_text_group_definition("grp2", "TITLE", items2, skins_dir=self.skins_dir)

        img1_name = def1["componentsData"][1]["componentData"]["data"]["onImage"]
        img2_name = def2["componentsData"][1]["componentData"]["data"]["onImage"]

        self.assertNotEqual(img1_name, img2_name)
        self.assertTrue((self.skins_dir / img1_name).exists())
        self.assertTrue((self.skins_dir / img2_name).exists())
        with Image.open(self.skins_dir / img1_name) as im1:
            self.assertEqual(im1.size, (60, 56))
        with Image.open(self.skins_dir / img2_name) as im2:
            self.assertEqual(im2.size, (80, 56))

    def test_supplied_state_asset_is_not_overwritten(self):
        """Generating one missing state must preserve the caller's other artwork."""
        for state in ("on", "off"):
            provided = self.skins_dir / f"custom_{state}.png"
            provided.write_bytes(b"caller-owned artwork")
            definition = build_titled_active_text_group_definition(
                "partial", "TITLE",
                [{"label": "FILTER", "rect": (0, 24, 80, 56),
                  f"{state}_image": provided.name}],
                skins_dir=self.skins_dir,
            )
            self.assertEqual(provided.read_bytes(), b"caller-owned artwork")
            data = definition["componentsData"][1]["componentData"]["data"]
            other = "offImage" if state == "on" else "onImage"
            self.assertTrue((self.skins_dir / data[other]).is_file())

    def test_voice_target_geometry_derived_and_exact(self):
        """Voice targets geometry matches canonical layout and derives group bounds without hardcoding."""
        from scripts.native_voice.voice_tab import (
            VOICE_TARGET_GROUP_BOUNDS,
            VOICE_TARGET_SCOPE_RECT,
            VOICE_TARGET_SCOPE_TITLE,
            VOICE_TARGET_ITEMS,
        )
        self.assertEqual(VOICE_TARGET_GROUP_BOUNDS, (930, 388, 336, 226))
        self.assertEqual(VOICE_TARGET_SCOPE_RECT, (0, 0, 336, 18))
        self.assertEqual(VOICE_TARGET_SCOPE_TITLE, "PAN / DETUNE")
        self.assertEqual(len(VOICE_TARGET_ITEMS), 5)
        expected_items = {
            "OSC A": {"handle": "ActiveA", "key": "voicectl.osc.a", "rect": (0, 20, 166, 58)},
            "OSC B": {"handle": "ActiveB", "key": "voicectl.osc.b", "rect": (170, 20, 166, 58)},
            "OSC C": {"handle": "ActiveC", "key": "voicectl.osc.c", "rect": (0, 94, 166, 58)},
            "SUB": {"handle": "ActiveSub", "key": "voicectl.osc.sub", "rect": (170, 94, 166, 58)},
            "NOISE": {"handle": "ActiveNoise", "key": "voicectl.osc.noise", "rect": (0, 168, 166, 58)},
        }
        for item in VOICE_TARGET_ITEMS:
            exp = expected_items[item["label"]]
            self.assertEqual(item["handle"], exp["handle"])
            self.assertEqual(item["key"], exp["key"])
            self.assertEqual(item["rect"], exp["rect"])

    def test_voice_fields_and_5_target_bindings_unchanged(self):
        """Voice tab preserves all 5 target parameter bindings, aperture coordinates, and 10x Detune."""
        defs = voice_definitions(self.skins_dir)

        # 1. 5 targets group registered once
        self.assertIn("voiceNativeTargets", defs)
        targets_def = defs["voiceNativeTargets"]
        self.assertEqual(targets_def["actions"], [])
        self.assertEqual(len([c for c in targets_def["componentsData"] if c["componentData"]["type"] == "Button"]), 5)

        # 2. 10x DETUNE remains standalone
        self.assertIn("voiceActive_detune10x", defs)
        detune10x_def = defs["voiceActive_detune10x"]
        self.assertEqual(len(detune10x_def["componentsData"]), 1)

        # 3. LFO MODE uses build_titled_selector
        choice_def = defs["voiceNativeChoice"]
        face = next(c for c in choice_def["componentsData"] if c["componentData"]["type"] == "Image")
        self.assertEqual(face["bounds"]["bounds"], "0 28 368 48")
        title = next(c for c in choice_def["componentsData"] if c["componentData"]["name"] == "name")
        self.assertEqual(title["bounds"]["bounds"], "0 0 368 24")

        # 4. Target apertures match exact historical coordinates
        expected_coords = {
            "OSC A": (0, 20, 166, 58),
            "OSC B": (170, 20, 166, 58),
            "OSC C": (0, 94, 166, 58),
            "SUB": (170, 94, 166, 58),
            "NOISE": (0, 168, 166, 58),
        }
        for item in VOICE_TARGET_ITEMS:
            self.assertEqual(item["rect"], expected_coords[item["label"]])


if __name__ == "__main__":
    unittest.main()
