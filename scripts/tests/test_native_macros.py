# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Unit tests for standalone native macros controls and skin extra."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "tests"))

from test_mpc_skin_extras import extras_with_manifest, generate_skins, run_apply
from test_mpc_skin_overlay import OVERLAY_LAYOUT


class TestNativeMacros(unittest.TestCase):
    def test_actual_generated_skin_with_two_differing_states(self):
        """Verifies fixed titles, instance independence, and background states from actual generated skins."""
        with tempfile.TemporaryDirectory() as tmp_root:
            root = Path(tmp_root)
            # State 1: Actual skin generated with empty backgrounds manifest (no host background)
            skins_empty = generate_skins(root / "s1", OVERLAY_LAYOUT)
            extras_empty = extras_with_manifest(root / "e1", [])
            code1, err1 = run_apply(skins_empty, extras_empty)
            self.assertEqual(code1, 0, err1)
            tui_empty = json.loads((skins_empty / "TUI.json").read_text(encoding="utf-8"))

            # State 2: Actual skin generated with macros manifest (host background enabled)
            skins_bg = generate_skins(root / "s2", OVERLAY_LAYOUT)
            extras_bg = extras_with_manifest(root / "e2", ["macros"])
            code2, err2 = run_apply(skins_bg, extras_bg)
            self.assertEqual(code2, 0, err2)
            tui_bg = json.loads((skins_bg / "TUI.json").read_text(encoding="utf-8"))

            for state_name, tui, skins_dir, expect_bg in (
                ("State 1 (empty manifest)", tui_empty, skins_empty, False),
                ("State 2 (macros background)", tui_bg, skins_bg, True),
            ):
                with self.subTest(state=state_name):
                    tab = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == "Macros")
                    components = tab["componentDefinition"]["componentsData"]
                    cards = [c for c in components if c["componentData"]["type"] == "macroCard"]
                    self.assertEqual(len(cards), 8)
                    for i, card in enumerate(cards, 1):
                        handles = {m["key"]: m["value"] for m in card["handle remapping"]["map"]}
                        self.assertNotEqual(handles["Data"], handles["Target"])
                        self.assertEqual(handles["Caption"], f"WithName[Macro {i}]/" + handles["Data"])

                    # 4. Verify instance-independence: no preset-specific custom macro names baked into skin JSON
                    tui_str = json.dumps(tab)
                    for custom_example in ("Cutoff", "Drive + Warp", "Space Verb", "FILTER SWEEP", "DRIVE + WARP"):
                        self.assertNotIn(custom_example, tui_str)

                    # 5. Verify background state
                    bg_data = tab["componentDefinition"]["backgroundData"]
                    for focus in ("focussed", "unfocussed"):
                        expected_img = "overlay_canvas.png" if expect_bg else ""
                        self.assertEqual(bg_data[focus]["image"], expected_img)

                    if expect_bg:
                        bg_file = skins_dir / "overlay_canvas.png"
                        self.assertTrue(bg_file.is_file(), "overlay_canvas.png copied to skin folder")
