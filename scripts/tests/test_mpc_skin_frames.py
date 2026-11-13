#!/usr/bin/env python3
# SynthyBoy - tests of the Q-Link frames and the layout of the controls inside them (90_frames.py, 80_layout.py).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mpc_skin_extras as base  # noqa: E402

FRAME_X = (16, 336, 656, 976)
FRAME_W = 288
BADGE = 40
GRAPHIC_TABS = ("Env 1", "Env 2", "Env 3", "Env 4", "Osc 1", "Osc 2", "Osc 3", "LFO 1", "LFO 2", "LFO 3", "LFO 4")


def _load(name):
    spec = importlib.util.spec_from_file_location("t_" + name, base.REAL_EXTRAS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


layout = _load("80_layout")
SLOT_TYPES = layout.SLOT_TYPES
AXIS = layout.AXIS


def rect(component):
    return tuple(int(v) for v in component["bounds"]["bounds"].split())


def frame_rects(tab):
    return [tuple(int(v) for v in s.split()) for s in tab["qlinkBoundsData"]]


def inside_horizontally(r, frame):
    return frame[0] <= r[0] and r[0] + r[2] <= frame[0] + frame[2]


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class FramesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(root)
        cls.before = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.q_before = (cls.skins / "Q-Links.json").read_text(encoding="utf-8")
        cls.code, cls.err = base.run_apply(cls.skins, base.REAL_EXTRAS)
        cls.root = root / "pf"
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tabs(self):
        return self.tui["pageData"]["tabs"]

    def test_apply_ok(self):
        self.assertEqual((self.code, self.err), (0, ""))

    def test_four_equal_frames_on_every_tab(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            self.assertEqual([f[0] for f in frames], list(FRAME_X), tab["tabName"])
            self.assertEqual({f[2] for f in frames}, {FRAME_W}, tab["tabName"])
            self.assertEqual(len({(f[1], f[3]) for f in frames}), 1, tab["tabName"])
            if tab["tabName"] in GRAPHIC_TABS:
                self.assertEqual({(f[1], f[3]) for f in frames}, {(301, 323)}, tab["tabName"])
            else:
                self.assertEqual({(f[1], f[3]) for f in frames}, {(0, 624)}, tab["tabName"])

    def test_nothing_sticks_out_of_a_frame(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            for c in tab["componentDefinition"]["componentsData"]:
                r = rect(c)
                if any(inside_horizontally(r, f) for f in frames):
                    continue
                # only a picture across the columns may leave them: it must not touch the frames vertically
                self.assertIn(c["componentData"]["type"], ("Image", "envDisplay1", "envDisplay2", "envDisplay3", "envDisplay4"),
                              "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                self.assertIn(tab["tabName"], GRAPHIC_TABS, c["componentData"]["name"])
                for f in frames:
                    self.assertTrue(r[1] + r[3] <= f[1] or f[1] + f[3] <= r[1], "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

    def test_badge_zone_is_empty(self):
        for tab in self.tabs():
            for f in frame_rects(tab):
                zone = (f[0], f[1], BADGE, BADGE)
                for c in tab["componentDefinition"]["componentsData"]:
                    r = rect(c)
                    hit = r[0] < zone[0] + zone[2] and zone[0] < r[0] + r[2] and r[1] < zone[1] + zone[3] and zone[1] < r[1] + r[3]
                    self.assertFalse(hit, "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

    def defs(self, tui):
        return {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_every_control_is_on_the_axis_of_its_frame(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            for c in tab["componentDefinition"]["componentsData"]:
                typ = c["componentData"]["type"]
                if typ not in SLOT_TYPES:
                    continue
                x, y, w, h = rect(c)
                column = [i for i, f in enumerate(frames) if inside_horizontally((x, y, w, h), f)]
                self.assertEqual(len(column), 1, "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                f = frames[column[0]]
                self.assertLessEqual(abs(x + w / 2 - (f[0] + f[2] / 2)), 1, "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                self.assertTrue(f[1] <= y and y + h <= f[1] + f[3], "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

    def test_contents_of_a_slot_are_symmetric(self):
        for key, value in self.defs(self.tui).items():
            if key not in SLOT_TYPES:
                continue
            w, h = (int(v) for v in _child_bounds(value, "focus")[2:])
            self.assertEqual((w, h), (layout.SLOT_W, layout.slot_height(key)), key)
            parts = [_rect_of(c) for c in value["componentsData"] if c["componentData"]["name"] != "focus"]
            for r in parts:
                self.assertTrue(0 <= r[0] and r[0] + r[2] <= w and 0 <= r[1] and r[1] + r[3] <= h, key)
            if key in layout.ALIGNED or key in layout.FIT:
                self.assertLessEqual(abs((min(r[0] for r in parts) + max(r[0] + r[2] for r in parts)) / 2 - AXIS), 1, key)
            if key in layout.ALIGNED:
                for r in parts:
                    if key not in ("radio3", "radio4") or r[2] > 100:  # the radio buttons are checked as a group above
                        self.assertLessEqual(abs(r[0] + r[2] / 2 - AXIS), 1, key)
                # the name of the control is a text on the tab (85_names.py, test_mpc_skin_names.py): the definitions have no name child
                self.assertEqual([c for c in value["componentsData"] if c["componentData"]["name"] == "name"], [], key)
                for c in value["componentsData"]:
                    if c["componentData"]["name"] == "value":
                        self.assertEqual(c["componentData"]["data"]["textStyle"]["justification"], "horizontallyCentred verticallyCentred", key)
                        self.assertEqual(_rect_of(c)[2], 272, key)

    def test_knob_stack_does_not_overlap(self):
        for key in ("knobUni", "knobBi", "toggle", "faderUni", "faderBi"):
            if key not in layout.ALIGNED or key not in self.defs(self.tui):
                continue
            value = self.defs(self.tui)[key]
            parts = sorted((_rect_of(c) for c in value["componentsData"] if c["componentData"]["name"] != "focus"), key=lambda r: r[1])
            for a, b in zip(parts, parts[1:]):
                self.assertLessEqual(a[1] + a[3], b[1], key)

    def test_no_two_controls_overlap(self):
        for tab in self.tabs():
            items = [(c["componentData"]["name"], c["componentData"]["type"], rect(c), tuple(c["bounds"].get("additionalInvalidatingHandles", ()))) for c in tab["componentDefinition"]["componentsData"]
                     if not (c["componentData"]["type"] == "Label" and (" seg " in c["componentData"]["name"]
                                                                         or c["componentData"]["name"].endswith(" name")))]
            for i, (na, ta, ra, ha) in enumerate(items):
                for nb, tb, rb, hb in items[i + 1:]:
                    if ra == rb and ta == tb == "Image":
                        continue   # the pictures of one element (one per choice) share their place
                    if ra == rb and ha and hb and ha != hb:
                        continue   # copies of one control, each valid for another value of the same parameter (the LFO division): never visible together
                    hit = ra[0] < rb[0] + rb[2] and rb[0] < ra[0] + ra[2] and ra[1] < rb[1] + rb[3] and rb[1] < ra[1] + ra[3]
                    self.assertFalse(hit, "%s: %s overlaps %s" % (tab["tabName"], na, nb))

    def test_touch_and_focus_behaviour_is_the_generators(self):
        def strip(o):
            o = copy.deepcopy(o)
            def walk(v):
                if isinstance(v, dict):
                    v.pop("bounds", None) if "boundsType" in v.get("bounds", {}) else None
                    for k in [k for k in v if k == "textStyle"]:
                        v[k] = None
                    for x in v.values():
                        walk(x)
                elif isinstance(v, list):
                    for x in v:
                        walk(x)
            walk(o)
            return o
        before, after = self.defs(self.before), self.defs(self.tui)
        for key in SLOT_TYPES:
            if key not in before:   # the generator writes only the definitions the skin uses (no unipolar fader: the levels of the skin are zero-centred dB)
                continue
            was = copy.deepcopy(before[key])
            was["componentsData"] = [c for c in was["componentsData"] if c["componentData"]["name"] != "name"]   # 85_names.py: the name is a text on the tab
            self.assertEqual(strip(after[key]), strip(was), key)

    def test_q_links_of_plain_tabs_are_the_generators(self):
        # the frame work does not touch Q-Links.json: a tab that no module rewrites keeps the map the SDK generator wrote (compared by tab name: the
        # sub-tab module renumbers Tab and SubTab)
        before = base.qlinks_by_tab_name(self.before, json.loads(self.q_before)["Screen Mode Q-Links"]["map"])
        after = base.qlinks_by_tab_name(self.tui, json.loads((self.skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"])
        self.assertEqual(sorted(before), sorted(after))
        for name in before:
            if name not in GRAPHIC_TABS and not name.startswith("LFO"):
                self.assertEqual(after[name], before[name], name)

    def test_bounds_version_2_has_its_extra_key(self):
        def walk(o):
            if isinstance(o, dict):
                if o.get("version") == 2 and "boundsType" in o:
                    self.assertIn("additionalInvalidatingHandles", o)
                for v in o.values():
                    walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)
        walk(self.tui)

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, "-I", str(base.SDK / "tools" / "validate.py"), str(self.root)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "34 tabs; at most 32" not in l]
        self.assertEqual(errors, [], report)


def _rect_of(component):
    return tuple(int(v) for v in component["bounds"]["bounds"].split())


def _child_bounds(definition, name):
    return _rect_of(next(c for c in definition["componentsData"] if c["componentData"]["name"] == name))


if __name__ == "__main__":
    unittest.main()
