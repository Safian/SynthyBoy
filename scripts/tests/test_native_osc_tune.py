# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Comprehensive unit and integration tests for the standalone native OSC TUNE component.

Verifies:
1. Approved base layout: 278 x 154 px card with 32 px fields and 4 px field gap (1d6a118b).
2. Proper Component Entries: componentsData contains regular component entries, not raw nested definitions.
3. Supported comparison layouts: 48 px (278 x 190 px, gap=6) and 40 px (278 x 170 px, gap=4).
4. Self-contained SDK definitions set: build_standalone_osc_tune_definitions() and registration on context.
5. Touch targets and hitbox isolation: widths >= 56 px, bottom buttons 48 px, 0 px overlap, >= 4 px deadband.
6. Real numeric host parameter IDs from docs/params.json (0 fictitious 'Parameter oscN.*'):
   - Osc A: Transpose=14, FIN=15, CRS=1508, TRACK=1509
   - Osc B: Transpose=38, FIN=39, CRS=1520, TRACK=1521
   - Osc C: Transpose=62, FIN=63, CRS=1532, TRACK=1533
7. Instance handle remapping and parameter independence between Osc A, B, and C.
8. Transposition route for OCT (AbcOctave setting) and SEM (Transpose semitones).
9. Proven native OSC_DETAIL MORE offset (base + OSC_DETAIL + 97) with NO fallback to 97.
10. Strict tab validation: KeyError on unknown tabs with NO fallback to Osc A.
11. Real SDK button assets: TRACK on/off (cyan LED), MORE normal/pressed, and write_tune_assets().
12. Strictly 0 visible Q-Link numbers, chips, or lanes (0 yellow pixels).
"""

import json
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
SCRIPTS_DIR = REPO_ROOT / "scripts"
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import native_name_value as nnv
import components.native.tune.tune as notune
from native_ui_params import (
    OSC_ABC_SETTINGS,
    OSC_ABC_SETTINGS_FIELDS,
    OSC_DETAIL,
    OSC_DETAIL_CELLS,
    OSC_SETTINGS_VIEW,
    OSC_SOURCE,
    mixer_peek_index,
)
from test_mpc_skin_extras import (
    REAL_EXTRAS,
    generate_skins,
    run_apply,
)


class MockContext:
    def __init__(self, skins_dir=None):
        self.definitions = {}
        self.skins_dir = Path(skins_dir) if skins_dir else None

    def add_component_definition(self, name, definition):
        self.definitions[name] = definition


class TestNativeOscTune(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.params_file = REPO_ROOT / "docs" / "params.json"
        assert cls.params_file.exists(), f"params.json missing at {cls.params_file}"
        cls.params_doc = json.loads(cls.params_file.read_text(encoding="utf-8"))
        cls.params = cls.params_doc["params"]
        cls.param_count = len(cls.params)
        cls.base = mixer_peek_index(cls.param_count)

    def test_tune_definition_default_32px_geometry_and_structure(self):
        """Verify the approved 278x154 default container, 32 px fields, 4 px gap, and 48 px buttons."""
        defn = notune.build_standalone_osc_tune_definition()
        self.assertEqual(defn.get("version"), 2)
        self.assertFalse(defn.get("ignoreMousePresses"), "Panel must be touch interactive")
        self.assertFalse(defn.get("disableCoarseDataWheel"), "Hardware data wheel must be active")

        # Background color #121930
        bg = defn.get("backgroundData", {}).get("unfocussed", {})
        self.assertEqual(bg.get("colour"), "ff121930", "Panel background must be #121930")

        kids = defn.get("componentsData", [])
        self.assertEqual(len(kids), 7, "Must contain: Header + 4 name-value child entries + 2 buttons")

        # Header
        hdr = kids[0]
        self.assertEqual(hdr["componentData"]["name"], "HeaderTune")
        self.assertEqual(hdr["componentData"]["type"], "Label")
        self.assertEqual(hdr["componentData"]["data"]["text"], "TUNE")
        self.assertEqual(hdr["bounds"]["bounds"], "12 8 100 16")

        # Verify child entries 1..4 are REGULAR component entries (NOT raw nested definitions)
        for idx, (expected_name, expected_bounds, expected_data, expected_caption) in enumerate(
            [
                ("tune_oct", "10 26 125 32", "Data_Oct", "Caption_Oct"),
                ("tune_sem", "143 26 125 32", "Data_Sem", "Caption_Sem"),
                ("tune_fin", "10 62 125 32", "Data_Fin", "Caption_Fin"),
                ("tune_crs", "143 62 125 32", "Data_Crs", "Caption_Crs"),
            ],
            start=1,
        ):
            child = kids[idx]
            self.assertEqual(child["componentData"]["name"], expected_name)
            self.assertEqual(child["componentData"]["type"], "sharedNativeNameValue")
            self.assertEqual(child["bounds"]["bounds"], expected_bounds)

            # Must NOT have nested componentsData or actions in child entries
            self.assertNotIn("componentsData", child, f"{expected_name} must be a component entry, not an inlined definition")
            self.assertNotIn("backgroundData", child, f"{expected_name} must not contain backgroundData")

            # Check handle remapping
            remap = {m["key"]: m["value"] for m in child["handle remapping"]["map"]}
            self.assertEqual(remap.get("Data"), expected_data)
            self.assertEqual(remap.get("Caption"), expected_caption)

        # Buttons (TRACK, MORE) - strictly 48 px high
        track_btn = kids[5]
        self.assertEqual(track_btn["componentData"]["name"], "tune_track")
        self.assertEqual(track_btn["componentData"]["type"], "Button")
        self.assertEqual(track_btn["handle remapping"]["map"], [{"key": "Data", "value": "Data_Track"}])
        self.assertEqual(track_btn["bounds"]["bounds"], "10 98 125 48")
        self.assertNotIn("actions", track_btn)
        self.assertEqual(track_btn["componentData"]["data"]["buttonId"], 1)

        more_btn = kids[6]
        self.assertEqual(more_btn["componentData"]["name"], "tune_more")
        self.assertEqual(more_btn["componentData"]["type"], "Button")
        self.assertEqual(more_btn["handle remapping"]["map"], [{"key": "Data", "value": "Data_More"}])
        self.assertEqual(more_btn["bounds"]["bounds"], "143 98 125 48")
        self.assertNotIn("actions", more_btn)
        self.assertEqual(more_btn["componentData"]["data"]["buttonId"], 1)

    def test_denser_tune_definitions_48_and_40_variants(self):
        """Verify 48 px (278x190) reference and 40 px (278x170) compact comparison variants."""
        # 1. 48 px reference variant
        defn_48 = notune.build_standalone_osc_tune_definition(field_h=48, field_gap=6)
        kids_48 = defn_48.get("componentsData", [])
        self.assertEqual(len(kids_48), 7)
        self.assertEqual(kids_48[1]["bounds"]["bounds"], "10 26 125 48")
        self.assertEqual(kids_48[2]["bounds"]["bounds"], "143 26 125 48")
        self.assertEqual(kids_48[3]["bounds"]["bounds"], "10 80 125 48")
        self.assertEqual(kids_48[4]["bounds"]["bounds"], "143 80 125 48")
        self.assertEqual(kids_48[5]["bounds"]["bounds"], "10 134 125 48")
        self.assertEqual(kids_48[6]["bounds"]["bounds"], "143 134 125 48")

        # 2. 40 px compact variant
        defn_40 = notune.build_standalone_osc_tune_definition(field_h=40, field_gap=4)
        kids_40 = defn_40.get("componentsData", [])
        self.assertEqual(len(kids_40), 7)
        self.assertEqual(kids_40[1]["bounds"]["bounds"], "10 26 125 40")
        self.assertEqual(kids_40[2]["bounds"]["bounds"], "143 26 125 40")
        self.assertEqual(kids_40[3]["bounds"]["bounds"], "10 70 125 40")
        self.assertEqual(kids_40[4]["bounds"]["bounds"], "143 70 125 40")
        self.assertEqual(kids_40[5]["bounds"]["bounds"], "10 114 125 48")
        self.assertEqual(kids_40[6]["bounds"]["bounds"], "143 114 125 48")

    def test_touch_target_hitbox_isolation_no_overlap(self):
        """Verify no overlapping hitboxes exist between any controls across 32, 40, and 48 px variants."""
        for fh, fg in [(32, 4), (40, 4), (48, 6)]:
            with self.subTest(field_h=fh, field_gap=fg):
                defn = notune.build_standalone_osc_tune_definition(field_h=fh, field_gap=fg)
                kids = defn["componentsData"]

                rects = []
                for child in kids[1:]:
                    bx, by, bw, bh = map(int, child["bounds"]["bounds"].split())
                    self.assertGreaterEqual(bw, 56, f"Width {bw} px must satisfy >= 56 px touch requirement")
                    rects.append((bx, by, bx + bw, by + bh))

                # Pairwise overlap check
                for i in range(len(rects)):
                    for j in range(i + 1, len(rects)):
                        r1, r2 = rects[i], rects[j]
                        overlap_x = max(0, min(r1[2], r2[2]) - max(r1[0], r2[0]))
                        overlap_y = max(0, min(r1[3], r2[3]) - max(r1[1], r2[1]))
                        self.assertFalse(
                            overlap_x > 0 and overlap_y > 0,
                            f"Overlap detected between control {i} and {j} in variant fh={fh}, fg={fg}!",
                        )

                # Vertical deadbands
                deadband_1_2 = rects[2][1] - rects[0][3]
                self.assertGreaterEqual(deadband_1_2, fg)

                deadband_2_3 = rects[4][1] - rects[2][3]
                self.assertGreaterEqual(deadband_2_3, fg)

    def test_sdk_definitions_dictionary_and_registration(self):
        """Verify build_standalone_osc_tune_definitions() and register_standalone_osc_tune_definitions()."""
        defs = notune.build_standalone_osc_tune_definitions()
        self.assertIn("sharedNativeNameValue", defs)
        self.assertIn("oscNativeTune", defs)

        nv_def = defs["sharedNativeNameValue"]
        self.assertEqual(nv_def["version"], 2)
        triggers = {a["onAction"] for a in nv_def.get("actions", [])}
        self.assertIn("Mouse Down", triggers)
        self.assertIn("Enter Pressed", triggers)
        for a in nv_def.get("actions", []):
            self.assertEqual(a["handler"], "Show Overlay")
            self.assertEqual(a["additionalData"], "fx editor number menu")

        # Test registration with a mock context
        mock_ctx = MockContext()
        notune.register_standalone_osc_tune_definitions(mock_ctx)
        self.assertIn("sharedNativeNameValue", mock_ctx.definitions)
        self.assertIn("oscNativeTune", mock_ctx.definitions)

    def test_real_numeric_host_parameter_ids_against_schema(self):
        """Strict check: verify exact numeric host parameter IDs against docs/params.json."""
        spec_a = notune.OSC_TUNE_PARAM_IDS["Osc A"]
        spec_b = notune.OSC_TUNE_PARAM_IDS["Osc B"]
        spec_c = notune.OSC_TUNE_PARAM_IDS["Osc C"]

        # Osc A (Osc 1)
        self.assertEqual(self.params[spec_a["transpose"]]["key"], "osc1.transpose")
        self.assertEqual(self.params[spec_a["fin"]]["key"], "osc1.tune")
        self.assertEqual(self.params[spec_a["crs"]]["key"], "osc1.pitch_coarse")
        self.assertEqual(self.params[spec_a["track"]]["key"], "osc1.pitch_track")

        # Osc B (Osc 2)
        self.assertEqual(self.params[spec_b["transpose"]]["key"], "osc2.transpose")
        self.assertEqual(self.params[spec_b["fin"]]["key"], "osc2.tune")
        self.assertEqual(self.params[spec_b["crs"]]["key"], "osc2.pitch_coarse")
        self.assertEqual(self.params[spec_b["track"]]["key"], "osc2.pitch_track")

        # Osc C (Osc 3)
        self.assertEqual(self.params[spec_c["transpose"]]["key"], "osc3.transpose")
        self.assertEqual(self.params[spec_c["fin"]]["key"], "osc3.tune")
        self.assertEqual(self.params[spec_c["crs"]]["key"], "osc3.pitch_coarse")
        self.assertEqual(self.params[spec_c["track"]]["key"], "osc3.pitch_track")

        # Ensure NO fictitious 'Parameter oscN.*' strings exist
        for tab in ("Osc A", "Osc B", "Osc C"):
            entry = notune.build_standalone_osc_tune(tab, base=self.base)
            for m in entry["handle remapping"]["map"]:
                val = m["value"]
                self.assertNotIn("osc1.", val)
                self.assertNotIn("osc2.", val)
                self.assertNotIn("osc3.", val)
                self.assertNotIn("octave", val)
                self.assertNotIn("transpose_sem", val)

    def test_handle_remapping_for_osc_a_b_c_instances(self):
        """Verify correct instance parameter mapping and default 278x154 rect for all instances."""
        cases = [
            ("Osc A", 14, 15, 1508, 1509, 3),
            ("Osc", 14, 15, 1508, 1509, 3),  # 'Osc' aliases to 'Osc A'
            ("Osc B", 38, 39, 1520, 1521, 21),
            ("Osc C", 62, 63, 1532, 1533, 39),
        ]
        for tab_name, exp_trans, exp_fin, exp_crs, exp_track, exp_abc_slot in cases:
            with self.subTest(tab=tab_name):
                entry = notune.build_standalone_osc_tune(tab_name, base=self.base)
                self.assertEqual(entry["componentData"]["type"], "oscNativeTune")
                self.assertEqual(entry["bounds"]["bounds"], "10 192 278 154", "Instance rect must default to 278 x 154 px")

                remap = {m["key"]: m["value"] for m in entry["handle remapping"]["map"]}

                # Data handles
                expected_oct = self.base + OSC_ABC_SETTINGS + exp_abc_slot
                expected_more = self.base + OSC_DETAIL + notune.OSC_DETAIL_MORE_OFFSET

                self.assertEqual(remap["Data_Oct"], f"Parameter {expected_oct}")
                self.assertEqual(remap["Caption_Oct"], f"WithName[OCT]/Parameter {expected_oct}")
                self.assertEqual(remap["Data_Sem"], f"Parameter {exp_trans}")
                self.assertEqual(remap["Caption_Sem"], f"WithName[SEM]/Parameter {exp_trans}")
                self.assertEqual(remap["Data_Fin"], f"Parameter {exp_fin}")
                self.assertEqual(remap["Caption_Fin"], f"WithName[FIN]/Parameter {exp_fin}")
                self.assertEqual(remap["Data_Crs"], f"Parameter {exp_crs}")
                self.assertEqual(remap["Caption_Crs"], f"WithName[CRS]/Parameter {exp_crs}")
                self.assertEqual(remap["Data_Track"], f"Parameter {exp_track}")
                self.assertEqual(remap["Data_More"], f"FromGesture/Parameter {expected_more}")

    def test_instance_parameter_disjointness_and_independence(self):
        """Verify that Osc A, Osc B, and Osc C instances use completely disjoint host parameters."""
        tune_a = notune.build_standalone_osc_tune("Osc A", base=self.base)
        tune_b = notune.build_standalone_osc_tune("Osc B", base=self.base)
        tune_c = notune.build_standalone_osc_tune("Osc C", base=self.base)

        import re
        def extract_param_ids(entry):
            ids = set()
            for m in entry["handle remapping"]["map"]:
                match = re.search(r"Parameter\s+(\d+)", m["value"])
                if match:
                    param_id = int(match.group(1))
                    # Exclude shared detail navigation handle
                    if param_id != self.base + OSC_DETAIL + notune.OSC_DETAIL_MORE_OFFSET:
                        ids.add(param_id)
            return ids

        params_a = extract_param_ids(tune_a)
        params_b = extract_param_ids(tune_b)
        params_c = extract_param_ids(tune_c)

        self.assertTrue(params_a)
        self.assertTrue(params_b)
        self.assertTrue(params_c)

        self.assertEqual(params_a & params_b, set(), f"Collision A & B: {params_a & params_b}")
        self.assertEqual(params_b & params_c, set(), f"Collision B & C: {params_b & params_c}")
        self.assertEqual(params_a & params_c, set(), f"Collision A & C: {params_a & params_c}")

    def test_transposition_route_for_oct_and_sem(self):
        """Verify transposition route: SEM maps to transpose; OCT maps to AbcOctave setting."""
        tune_a = notune.build_standalone_osc_tune("Osc A", base=self.base)
        remap = {m["key"]: m["value"] for m in tune_a["handle remapping"]["map"]}

        # SEM maps to semitones transpose
        self.assertEqual(remap["Data_Sem"], "Parameter 14")
        # OCT maps to AbcOctave setting handle preserving semitone remainder
        self.assertEqual(remap["Data_Oct"], f"Parameter {self.base + OSC_ABC_SETTINGS + 3}")

    def test_more_action_parameter_offset_and_no_fallback_97(self):
        """Verify MORE handle calculation, offset 97 proof, and strict refusal of fallback 97."""
        # 1. Proven offset: 1 + 3 * 32 = 97
        self.assertEqual(notune.OSC_DETAIL_MORE_OFFSET, 97)
        self.assertEqual(notune.OSC_DETAIL_MORE_OFFSET, 1 + 3 * OSC_DETAIL_CELLS)

        # 2. Check handle string
        tune = notune.build_standalone_osc_tune("Osc A", base=self.base)
        remap = {m["key"]: m["value"] for m in tune["handle remapping"]["map"]}
        expected_more = self.base + OSC_DETAIL + 97
        self.assertEqual(remap["Data_More"], f"FromGesture/Parameter {expected_more}")

        # 3. Check that parameter 97 in params.json is filter1.formant.x, proving that
        # fallback 97 was an error in previous commits
        self.assertEqual(self.params[97]["key"], "filter1.formant.x")

        # 4. Strictly test that resolve_native_base raises ValueError when base cannot be determined
        # and NEVER returns 97
        with self.assertRaises(ValueError):
            notune.resolve_native_base(base=None, param_count=None) if not self.params_file.exists() else None
            # Simulate failure to resolve by passing empty/invalid search
            class HiddenParams:
                pass
            import unittest.mock as mock
            with mock.patch("pathlib.Path.exists", return_value=False):
                notune.resolve_native_base(base=None, param_count=None)

    def test_strict_tab_validation_no_fallback_osc_a(self):
        """Verify that unknown oscillator tabs raise KeyError and never silently fall back to Osc A."""
        for invalid_tab in ("Sub", "Noise", "Filter", "Osc D", "Unknown", ""):
            with self.subTest(tab=invalid_tab):
                with self.assertRaises(KeyError) as cm:
                    notune.build_standalone_osc_tune(invalid_tab, base=self.base)
                self.assertIn("refusing fallback to Osc A", str(cm.exception))

    def test_button_asset_rendering_and_write_tune_assets(self):
        """Verify render_tune_track_button, render_tune_more_button, and write_tune_assets."""
        # TRACK button assets
        btn_on = notune.render_tune_track_button(active=True)
        self.assertEqual(btn_on.size, (125, 48))
        btn_off = notune.render_tune_track_button(active=False)
        self.assertEqual(btn_off.size, (125, 48))

        # MORE button assets
        more_up = notune.render_tune_more_button(pressed=False)
        self.assertEqual(more_up.size, (125, 48))
        more_down = notune.render_tune_more_button(pressed=True)
        self.assertEqual(more_down.size, (125, 48))

        # Test writing assets to a temporary directory
        with tempfile.TemporaryDirectory() as tmp_dir:
            notune.write_tune_assets(tmp_dir, field_h=32)
            p = Path(tmp_dir)
            self.assertTrue((p / notune.TRACK_ON_IMAGE).exists())
            self.assertTrue((p / notune.TRACK_OFF_IMAGE).exists())
            self.assertTrue((p / notune.MORE_UP_IMAGE).exists())
            self.assertTrue((p / notune.MORE_DOWN_IMAGE).exists())
            self.assertTrue((p / "name_value_box_68x32.png").exists())

    def test_absence_of_qlink_numbers_and_yellow_chips(self):
        """STRICT CHECK: Verify no yellow Q-Link chips exist anywhere in definitions or cards."""
        for fh, fg in [(32, 4), (40, 4), (48, 6)]:
            with self.subTest(field_h=fh):
                # 1. Check definition string
                defn = notune.build_standalone_osc_tune_definition(field_h=fh, field_gap=fg)
                defn_str = str(defn)
                self.assertNotIn("Q1", defn_str)
                self.assertNotIn("Q9", defn_str)
                self.assertNotIn("Q12", defn_str)

                # 2. Check rendered card image
                card = notune.render_tune_card(
                    {"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"},
                    track_active=True,
                    field_h=fh,
                    field_gap=fg,
                )
                arr = np.array(card)
                yellow_pixels = (arr[:, :, 0] > 190) & (arr[:, :, 1] > 130) & (arr[:, :, 2] < 100)
                self.assertEqual(
                    np.count_nonzero(yellow_pixels),
                    0,
                    f"Rendered TUNE card at {fh} px must NOT contain any yellow Q-Link chip pixels",
                )

    def test_name_outside_number_inside_frame(self):
        """Verify pixels: Label area has panel background #121930; number box has #12182d and border #1c2651."""
        for fh, fg, expected_h in [(32, 4, 154), (40, 4, 170), (48, 6, 190)]:
            with self.subTest(field_h=fh):
                card = notune.render_tune_card(
                    {"oct": "-1", "sem": "+7", "fin": "-25", "crs": "-12.50"},
                    track_active=True,
                    field_h=fh,
                    field_gap=fg,
                )
                self.assertEqual(card.size, (278, expected_h))
                arr = np.array(card)

                # Sample point in label area outside letter glyphs (x=20, y=32):
                px_label_bg = arr[32, 20]
                self.assertEqual(px_label_bg[3], 255)
                self.assertTrue(15 <= px_label_bg[0] <= 21, f"R should match #121930, got {px_label_bg[0]}")
                self.assertTrue(22 <= px_label_bg[1] <= 28, f"G should match #121930, got {px_label_bg[1]}")
                self.assertTrue(44 <= px_label_bg[2] <= 52, f"B should match #121930, got {px_label_bg[2]}")

                # Sample point inside number box interior (x=80, y=32):
                px_box_interior = arr[32, 80]
                self.assertEqual(px_box_interior[3], 255)
                self.assertTrue(15 <= px_box_interior[0] <= 21, f"R should match #12182d, got {px_box_interior[0]}")
                self.assertTrue(21 <= px_box_interior[1] <= 27, f"G should match #12182d, got {px_box_interior[1]}")
                self.assertTrue(40 <= px_box_interior[2] <= 50, f"B should match #12182d, got {px_box_interior[2]}")

    def test_autoload_discovery_and_order(self):
        """98_osc_tune.py is discovered in mpc-skin-extras and runs after 98_osc_surface.py."""
        files = sorted(p.name for p in REAL_EXTRAS.glob("*.py"))
        self.assertIn("98_osc_tune.py", files)
        idx_surface = files.index("98_osc_surface.py")
        idx_tune = files.index("98_osc_tune.py")
        self.assertGreater(idx_tune, idx_surface, "98_osc_tune.py must execute after 98_osc_surface.py")

    def test_tune_bounds_gating(self):
        """Verify build_standalone_osc_tune correctly attaches gating handles to bounds."""
        gates = ["IndexedEnabling/1/5/Parameter 300", "NonZero/Parameter 302"]
        tune = notune.build_standalone_osc_tune("Osc A", base=self.base, gates=gates)
        bounds = tune["bounds"]
        self.assertEqual(bounds.get("version"), 2)
        self.assertEqual(bounds.get("additionalInvalidatingHandles"), gates)

    def test_skin_extras_integration_and_switched_abc(self):
        """Verify full skin pipeline integrates switched A/B/C TUNE blocks and bounds gating."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            skins = generate_skins(Path(tmp_dir))
            code, err = run_apply(skins, REAL_EXTRAS)
            self.assertEqual(code, 0, f"run_apply failed with error: {err}")

            tui_path = skins / "TUI.json"
            tui = json.loads(tui_path.read_text(encoding="utf-8"))
            defs = {
                d["key"]: d["value"]
                for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
            }

            # 1. oscNativeTune definition exists
            self.assertIn("oscNativeTune", defs, "oscNativeTune definition must be registered")
            tune_def = defs["oscNativeTune"]
            self.assertFalse(tune_def.get("disableCoarseDataWheel", True))

            # 2. Check unified Osc tab and subtabs
            tabs = [t for t in tui["pageData"]["tabs"] if t.get("tabName") in ("Osc", "Osc A", "Osc B", "Osc C")]
            self.assertTrue(len(tabs) >= 1, "At least one oscillator tab must exist")

            for tab in tabs:
                items = tab["componentDefinition"]["componentsData"]
                self.assertTrue(any(c["componentData"]["type"] == "oscNativeSurface" for c in items))
                item_by_name = {c["componentData"]["name"]: c for c in defs["oscNativeSurface"]["componentsData"]}

                # Check all 3 switched instances are present
                for src_label, src_idx, oct_slot, sem_id, fin_id, crs_id, track_id in [
                    ("A", 1, 3, 14, 15, 1508, 1509),
                    ("B", 2, 21, 38, 39, 1520, 1521),
                    ("C", 3, 39, 62, 63, 1532, 1533),
                ]:
                    comp_name = f"Osc Native Tune {src_label}"
                    self.assertIn(comp_name, item_by_name, f"Tab {tab['tabName']} must contain {comp_name}")
                    c = item_by_name[comp_name]

                    # Verify bounds
                    self.assertEqual(c["bounds"]["bounds"], "10 192 278 154")
                    self.assertEqual(c["bounds"].get("showWhenDataModelInvalid"), "Hide")
                    expected_gates = [
                        f"IndexedEnabling/{src_idx}/5/Parameter {self.base + OSC_SOURCE}",
                        f"NonZero/Parameter {self.base + OSC_SETTINGS_VIEW}",
                    ]
                    self.assertEqual(c["bounds"].get("additionalInvalidatingHandles"), expected_gates)

                    # Verify handle remapping
                    remap = {m["key"]: m["value"] for m in c["handle remapping"]["map"]}
                    self.assertEqual(remap["Data_Sem"], f"Parameter {sem_id}")
                    self.assertEqual(remap["Data_Oct"], f"Parameter {self.base + OSC_ABC_SETTINGS + oct_slot}")
                    self.assertEqual(remap["Data_Fin"], f"Parameter {fin_id}")
                    self.assertEqual(remap["Data_Crs"], f"Parameter {crs_id}")
                    self.assertEqual(remap["Data_Track"], f"Parameter {track_id}")
                    self.assertEqual(remap["Data_More"], f"FromGesture/Parameter {self.base + OSC_DETAIL + 97}")

            # 3. Size budget: TUI.json strictly under 750 KB
            tui_size = tui_path.stat().st_size
            max_budget = 750 * 1024
            self.assertLess(tui_size, max_budget, f"TUI.json size ({tui_size} B) exceeds 750 KB budget")


if __name__ == "__main__":
    unittest.main()
