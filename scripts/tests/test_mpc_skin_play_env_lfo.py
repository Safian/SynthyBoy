# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
import subprocess
import unittest

from PIL import Image

from test_mpc_skin_extras import REAL_EXTRAS, SDK


CONTRACT = REAL_EXTRAS / "play-env-lfo-contract.json"


class PlayEnvLfoContractTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SYNTHYBOY_LFO_UI_HOST"), "set SYNTHYBOY_LFO_UI_HOST to lfo_ui_host")
    def test_contract_matches_live_play_view(self):
        actual = subprocess.check_output([os.environ["SYNTHYBOY_LFO_UI_HOST"], "--play-env-lfo-skin-contract"])
        self.assertEqual(actual, CONTRACT.read_bytes())


@unittest.skipUnless((SDK / "tools/gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class PlayEnvLfoSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test_mpc_skin_no_fallback import NoFallbackTests

        NoFallbackTests.setUpClass()
        cls.addClassCleanup(NoFallbackTests.tearDownClass)
        cls.skins = NoFallbackTests.skins
        cls.tui = NoFallbackTests.tui
        cls.assert_build = (NoFallbackTests.code, NoFallbackTests.err)
        cls.params = {p["key"]: p for p in NoFallbackTests.params}
        cls.play = next(t for t in cls.tui["pageData"]["tabs"] if t["tabName"] == "Play")
        cls.items = cls.play["componentDefinition"]["componentsData"]
        cls.defs = {d["key"]: d["value"] for d in cls.tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_native_compact_widgets_match_exclusive_cpp_cells(self):
        self.assertEqual(self.assert_build, (0, ""))
        prefix = "Play Native EnvLfo "
        fields = ("ENV Expand", "LFO Expand", "SHAPE", "RATE", "DIVISION", "STYLE", "SYNC", "STEREO", "Select")
        expected = {(view, field): tuple(rect) for view, field, *rect in json.loads(CONTRACT.read_text())}
        actual, modes = {}, {}
        for item in self.items:
            name = item["componentData"]["name"]
            if not name.startswith(prefix):
                continue
            body = name.removeprefix(prefix)
            standard_mode = None
            if body.split()[-1] in ("HZ", "BPM", "KEY"):
                body, standard_mode = body.rsplit(" ", 1)
            caption, span = body.rsplit(" ", 1)
            field = len(fields) if standard_mode else fields.index(caption)
            first, last = map(int, span.split("-"))
            index = 3347 if standard_mode else 3302 + field
            handle = ("FromGesture/" if field in (0, 1) else "") + f"Parameter {index}"
            visible = (f"IndexedEnabling/{first}/7/Parameter 3301" if first == last
                       else f"RangedEnabling/{first}/{last}/7/Parameter 3301")
            self.assertEqual(item["bounds"]["additionalInvalidatingHandles"],
                             ["RangedEnabling/0/18/33/Parameter 2675", visible])
            self.assertEqual(item["handle remapping"]["map"], [{"key": "Data", "value": handle}])
            if field in (0, 1):
                self.assertEqual(item["componentData"]["type"], "Button")
                self.assertEqual(item["componentData"]["data"]["onImage"], "lfo_native_expand.png")
                self.assertEqual(item["componentData"]["data"]["gestureBehaviour"], "Instant")
            rect = tuple(map(int, item["bounds"]["bounds"].split()))
            for view in range(first, last + 1):
                if standard_mode is not None:
                    mode = ("HZ", "BPM", "KEY").index(standard_mode)
                    x, y, width, height = expected[view, field]
                    left, right = width * mode // 3, width * (mode + 1) // 3
                    self.assertEqual(rect, (x + left, y, right - left, height))
                    data = item["componentData"]["data"]
                    self.assertEqual((data["numButtonsInGroup"], data["buttonId"]), (3, mode))
                    self.assertNotIn((view, mode), modes)
                    modes[view, mode] = rect
                else:
                    self.assertNotIn((view, field), actual)
                    actual[view, field] = rect
        self.assertEqual(set(modes), {(view, mode) for view in (1, 2, 3) for mode in range(3)})
        for view in (1, 2, 3):
            actual[view, len(fields)] = expected[view, len(fields)]
        self.assertEqual(actual, expected)
        self.assertFalse(any((view, field) in actual for view in (3, 6) for field in (3, 4)))
        legacy = next(item for item in self.items if item["componentData"]["name"] == "Play LFO Select")
        self.assertIn("IndexedEnabling/0/7/Parameter 3301", legacy["bounds"]["additionalInvalidatingHandles"])
        self.assertEqual(legacy["handle remapping"]["map"][0]["value"], "Parameter 2678")

    def test_choice_domains_and_modal_session_markers(self):
        replacement = {r["key"]: r["value"] for r in self.tui["pageData"]["replacementStrings"]}
        self.assertEqual(replacement["Parameter 3310"], [f"LFO {i}" for i in range(1, 11)] + [f"RND {i}" for i in range(1, 5)])
        self.assertEqual(replacement["Parameter 3304"], self.params["lfo1.shape"]["choices"][:-1] + ["Drawn"])
        self.assertEqual(replacement["Parameter 3306"], self.params["lfo1.division"]["choices"])
        self.assertEqual(replacement["Parameter 3307"], self.params["rlfo1.style"]["choices"])
        self.assertEqual(replacement["Parameter 3308"], self.params["rlfo1.sync"]["choices"])
        for caption, x, rgb in (("Select", 88, "459be9"), ("SHAPE", 100, "aee33c"),
                                ("DIVISION", 112, "9659df"), ("STYLE", 124, "f0bc77"), ("SYNC", 136, "6ef2a0")):
            name = "playEnvLfoMenu " + caption
            control = self.defs["playEnvLfoChoice " + caption]
            self.assertEqual({action["additionalData"] for action in control["actions"] if action["handler"] == "Show Overlay"}, {name})
            menu = self.defs[name]["componentsData"]
            self.assertEqual(sum(child["componentData"]["type"] == "MenuOverlay" for child in menu), 1)
            marker = menu[-1]
            self.assertEqual(marker["bounds"]["bounds"], f"{x} 120 6 6")
            with Image.open(self.skins / marker["componentData"]["data"]["image"]) as image:
                self.assertEqual(image.size, (6, 6))
                self.assertEqual(image.convert("RGB").getcolors(), [(36, tuple(bytes.fromhex(rgb)))])
