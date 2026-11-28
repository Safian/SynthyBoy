# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""BandLane component: one definition for every lane, handles and geometry agree with the plugin (native_fx_bands.h)."""
from pathlib import Path
import json
import re
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
for path in (REPO_ROOT, REPO_ROOT / "scripts"):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

from components import style
from components.native.band_lane import band_lane as lane
import native_ui_params as handles

HEADER = (REPO_ROOT / "shell-mpc/native_fx_bands.h").read_text()
PARAMS = (REPO_ROOT / "shell-mpc/native_ui_params.h").read_text()


def header_ints(text):
    return {name: int(value) for name, value in re.findall(r"\bk(\w+) = (\d+)", text)}


class TestBandLane(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.definitions = lane.build_band_lane_definition(Path(self.tmp.name))

    def tearDown(self):
        self.tmp.cleanup()

    def test_geometry_mirrors_the_plugin(self):
        g = header_ints(HEADER[HEADER.index("namespace geometry"):HEADER.index("}", HEADER.index("namespace geometry"))])
        self.assertEqual((g["X"], g["Y"], g["Pitch"], g["Height"]), (style.size("band_lane.x"), style.size("band_lane.y"),
                         style.size("band_lane.pitch"), style.size("band_lane.height")))
        self.assertEqual((g["TileW"], g["CardX"], g["CardPitch"]), (style.size("band_lane.tile")[2], style.size("band_lane.card_x"),
                         style.size("band_lane.card_pitch")))
        self.assertEqual((g["CardW"], g["PowerH"]), (style.size("band_lane.card")[0], style.size("band_lane.card_power")[1]))
        self.assertEqual((g["GripX"], g["GripW"], g["GripH"]), tuple(style.size("band_lane.grip")[0:1]) + tuple(style.size("band_lane.grip")[2:]))
        self.assertEqual((g["AddX"], g["AddW"]), (style.size("band_lane.add")[0], style.size("band_lane.add")[2]))
        self.assertEqual(g["PowerH"], style.size("band_lane.card_power")[1])
        self.assertEqual(style.size("band_lane.card_power")[0], g["GripX"])

    def test_handles_mirror_the_plugin(self):
        fields = style.size("band_lane.handles")
        self.assertEqual(handles.FX_BANDS_LANE_HANDLES, fields["lane"])
        self.assertEqual(handles.FX_BANDS_LANES, lane.LANES)
        c = header_ints(PARAMS[PARAMS.index("namespace fxbands"):])
        self.assertEqual((c["Lanes"], c["Cards"]), (lane.LANES, lane.CARDS))
        order = ("Tile", "Range", "Info", "Count", "Open", "Add", "AddAvail")
        enum = re.search(r"enum Field \{([^}]*)\}", PARAMS[PARAMS.index("namespace fxbands"):]).group(1)
        for name, key in zip(order, ("State", "Range", "Info", "Count", "Open", "Add", "AddAvail")):
            self.assertEqual(int(re.search(rf"\b{name} = (\d+)", enum).group(1)), fields["fields"][key])
        self.assertEqual(int(re.search(r"Select = (\d+)", enum).group(1)), fields["fields"]["Select"])
        self.assertEqual(fields["fields"]["Power"], fields["fields"]["Select"] + lane.CARDS)
        self.assertEqual(fields["lane"], fields["fields"]["Power"] + lane.CARDS)
        self.assertEqual(int(re.search(r"kStates = (\d+)", HEADER).group(1)), lane.STATES)

    def test_one_definition_serves_every_lane(self):
        self.assertEqual(set(self.definitions), {lane.DEFINITION, "fxNativeBandCard"})
        base = 5000
        entries = [lane.build_band_lane_instance(f"L{i}", lane.lane_rect(i), base, i) for i in range(lane.LANES)]
        for entry in entries:
            self.assertEqual(entry["componentData"]["type"], lane.DEFINITION)
        maps = [{item["key"]: item["value"] for item in e["handle remapping"]["map"]} for e in entries]
        self.assertEqual(len({tuple(m) for m in maps}), 1)
        self.assertEqual(len({tuple(m.values()) for m in maps}), lane.LANES)
        # every key the definition (and its cards) reads is bound by the lane entry
        text = json.dumps(self.definitions[lane.DEFINITION])
        used = set(re.findall(r'"(?:Present|Select|Selected|Unselected|Power|Powered|Bypassed)\d+"|"Face\d+"|"(?:State|Range|Info|Open|Add|AddAvail)"', text))
        self.assertTrue({u.strip('"') for u in used} <= set(maps[0]))
        # the lane's parameters stay inside the plugin's block of the lane
        per = style.size("band_lane.handles")["lane"]
        for i, m in enumerate(maps):
            for value in m.values():
                for number in re.findall(r"Parameter (\d+)", value):
                    self.assertTrue(base + i * per <= int(number) < base + (i + 1) * per)

    def test_card_is_the_shared_recipe(self):
        card = self.definitions["fxNativeBandCard"]
        names = [c["componentData"]["name"] for c in card["componentsData"]]
        self.assertEqual(names[0], "power")
        self.assertEqual(names[-1], "select")
        keys = {m["value"] for c in card["componentsData"] for m in c["handle remapping"]["map"]}
        self.assertEqual({"Power", "Select"}, keys & {"Power", "Select"})

    def test_gates_and_grips(self):
        children = self.definitions[lane.DEFINITION]["componentsData"]
        grips = [c for c in children if c["componentData"]["name"].startswith("Move ")]
        self.assertEqual(len(grips), lane.CARDS)
        for index, grip in enumerate(grips):
            self.assertEqual(grip["bounds"]["additionalInvalidatingHandles"], [f"Present{index}", f"Powered{index}"])
        faces = [c for c in children if c["componentData"]["name"].startswith("Tile face")]
        self.assertEqual(len(faces), lane.STATES - 1)

    def test_rects_fit_the_page(self):
        for index in range(lane.LANES):
            x, y, w, h = lane.lane_rect(index)
            self.assertLessEqual(x + w, 1280)
            self.assertLessEqual(y + h, 628)
        self.assertLessEqual(lane.lane_card_x(lane.CARDS - 1) + style.size("band_lane.card")[0], style.size("band_lane.add")[0])


if __name__ == "__main__":
    unittest.main()
