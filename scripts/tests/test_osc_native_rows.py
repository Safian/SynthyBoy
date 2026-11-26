# SynthyBoy - the native control rows of the OSC pages stand where the overlay panel leaves room for them.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The host-drawn controls of the bottom band (osc-native-settings.json) and the overlay panel that draws the block titles (UNISON, WARP, TUNE, CHARACTER, BEHAVIOUR) are two owners of one band.

2026-11-26: the in-screen source strip went (the hardware sub-tab names the oscillator) and the overlay panel moved down 48 px (`OscScreen::layout`, shell-mpc/ui/screen.h: panelY = bottom - 48 - bottomH);
the native rows stayed where the strip had left them, so the dials and fields covered the block titles ("WARP" on the "WARP 1" list, "TUNE" and "CHARACTER" under a dial). The rows must start under the
title strip of the panel (kBlockTitleH = 38 below panelY) and end above the PAN / LEVEL row (y 580)."""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

PAGE_H = 628            # the page (screen.h rect_.h on a tab)
PAN_LEVEL_Y = 580       # selector.standalone_compact_osc_pan_rect: the bottom row of the page
TITLE_H = 38            # KnobPanel::kBlockTitleH (shell-mpc/ui/panel.h)
ONE_ROW_BOTTOM_H = 38 + 128 + 6   # Sub and Noise: kBlockTitleH + kOneRowH + kBlockPad
BAND_BOTTOM_H = 176     # A / B / C with the native TUNE card
BAND_TOP_Y = 380        # a field below this line is in the bottom band; the fields above are the top row


class OscNativeRowsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layouts = json.loads((ROOT / "scripts/mpc-skin-extras/osc-native-settings.json").read_text(encoding="utf-8"))

    def test_no_native_control_covers_a_block_title_or_the_pan_level_row(self):
        checked = 0
        for layout in self.layouts:
            one_row = layout["layout"] in (1, 2, 3)   # Sub (1), Noise (2: generated, 3: sample)
            first = PAGE_H - 48 - (ONE_ROW_BOTTOM_H if one_row else BAND_BOTTOM_H) + TITLE_H
            for field in layout["fields"]:
                x, y, w, h = field["rect"]
                if y < BAND_TOP_Y:
                    continue
                checked += 1
                self.assertGreaterEqual(y, first - 4, (layout["layout"], field["label"], "starts above the title strip of the panel"))
                self.assertLessEqual(y + h, PAN_LEVEL_Y, (layout["layout"], field["label"], "runs into the PAN / LEVEL row"))
        self.assertGreater(checked, 40)

    def test_detail_is_a_button_with_a_face(self):
        source = (ROOT / "scripts/mpc-skin-extras/57_osc_settings.py").read_text(encoding="utf-8")
        self.assertIn('caption in ("BROWSE", "MAP", "DETAIL")', source, "DETAIL is a button: bare text without a face looks like an orphan label")


if __name__ == "__main__":
    unittest.main()
