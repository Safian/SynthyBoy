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
# The overlay layout is the only layout: every tab is a page the plugin draws (82_play.py / 83_pages.py), the frames of every tab are the four Q-Link
# bank frames of the lower rows (y 301, 323 high) and the host widgets are a fallback that is hidden while the plugin's page is up.
FRAME_Y, FRAME_H = 301, 323
# tabs (after the key labels of 95_subtabs.py) that carry a picture across the columns: Play background, the Env curves, the LFO shape pictures, the 32 spectrum meters of Macros
WIDE_TABS = ("Play", "Env", "LFO", "Macros")
HIDE_PREFIX = "RangedEnabling/0/18/33/"   # 83_pages.py: valid only while the plugin's page is NOT up


def hidden_while_page_is_up(component):
    return any(h.startswith(HIDE_PREFIX) for h in component["bounds"].get("additionalInvalidatingHandles", ()))


def is_background(component):
    """The full-tab host-skin background of a tab (99_backgrounds.py): the bottom-most picture, visible only while the plugin's page is up (the complement of the hide rule)."""
    return component["componentData"]["name"].startswith("Background ")


def is_marker(component):
    return component["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))

def is_play_selector(tab, component):
    """Play selectors occupy the reclaimed row above the Q-Link frames."""
    name = component["componentData"]["name"]
    return tab["tabName"] == "Play" and name.startswith("Play ") and name.endswith(" Select")


def is_native_host(component):
    name = component["componentData"]["name"]
    return name.startswith(("Play Native EnvLfo ", "Play Native OSC ", "Play Expanded ", "Env Native ", "Mixer ", "Master ", "Voice ", "Mod ", "Macro ", "Macros ", "FX ", "Osc Native ", "LFO 1 Native ", "Filter Native ")) or name in {"Env 1 Attack", "Env 1 Attack name", "Env 1 Decay", "Env 1 Decay name",
                    "Env 1 Sustain", "Env 1 Sustain name", "Env 1 Release", "Env 1 Release name",
                    "Env 1 Delay", "Env 1 Delay name", "Env 1 Hold", "Env 1 Hold name", "Env 1 Attack Curve", "Env 1 Attack Curve name", "Env 1 Decay Curve", "Env 1 Decay Curve name",
                    "Filter 1 Power", "Filter 2 Power", "LFO 1 Smooth Native", "Mixer F1 Dest", "Mixer F2 Dest", "Mixer Main Bypass", "Mixer Main Bypass name",
                    "Mixer Bus 1 Bypass", "Mixer Bus 1 Bypass name", "Mixer Bus 1 Dest", "Mixer Bus 1 Level", "Mixer Bus 1 Level name",
                    "Mixer Bus 2 Bypass", "Mixer Bus 2 Bypass name", "Mixer Bus 2 Dest", "Mixer Bus 2 Level", "Mixer Bus 2 Level name"} \
        or component["componentData"]["type"].startswith(("compactNative", "macroNative", "voiceNative", "masterNative", "modNative", "fxNative", "oscNative", "nativeModulationEditor"))


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
        cls.code, cls.err = base.run_apply(cls.skins, base.extras_without(root, "97_no_fallback.py"))   # the generator modules are tested through their own output: the release step 97_no_fallback.py removes every host widget (test_mpc_skin_no_fallback.py)
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
            self.assertEqual({(f[1], f[3]) for f in frames}, {(FRAME_Y, FRAME_H)}, tab["tabName"])
        self.assertEqual(len(self.tabs()), 15)

    def test_nothing_sticks_out_of_a_frame(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            for c in tab["componentDefinition"]["componentsData"]:
                if is_play_selector(tab, c):
                    continue
                if is_native_host(c):
                    continue
                r = rect(c)
                if any(inside_horizontally(r, f) for f in frames):
                    continue
                # only a picture across the columns may leave them: it must not touch the frames vertically
                self.assertIn(c["componentData"]["type"], ("Image", "Meter", "envDisplay1", "envDisplay2", "envDisplay3", "envDisplay4"),
                              "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                self.assertIn(tab["tabName"], WIDE_TABS, c["componentData"]["name"])
                # pictures above the frames (Play background, Env curve, LFO shapes: y < 301) never touch them; the 32 spectrum meters of Macros (y 480 ..) sit in
                # the frames' height and the border of a frame cuts four of them: they are host widgets of the fallback, hidden while the plugin's page is up
                # (TODO.md "Fallback widgets and the Q-Link badges")
                for f in frames:
                    if not (r[1] + r[3] <= f[1] or f[1] + f[3] <= r[1]):
                        if is_background(c):   # the full-tab background lies under everything, by design (99_backgrounds.py)
                            continue
                        self.assertEqual((tab["tabName"], c["componentData"]["type"]), ("Macros", "Meter"), c["componentData"]["name"])
                        self.assertTrue(hidden_while_page_is_up(c), c["componentData"]["name"])

    def test_badge_zone_is_empty(self):
        # The badge of a frame (its top left corner) is drawn by the host over the tab. The host widgets of the overlay skin are a fallback that is hidden while the
        # plugin's page is up (83_pages.py), and their rows (y 188 ..) are no longer aligned with the frames at y 301 (the hardware knobs are the Q proxies):
        # with the page up the badge zones are empty; in the fallback (overlay off) some widgets reach a badge zone (TODO.md "Fallback widgets and the Q-Link badges").
        # So the check is: every control a badge zone touches is hidden while the page is up, and the markers and pictures never touch it.
        for tab in self.tabs():
            for f in frame_rects(tab):
                zone = (f[0], f[1], BADGE, BADGE)
                for c in tab["componentDefinition"]["componentsData"]:
                    if is_background(c):   # the host-skin background is the ground of the whole tab: the badge is drawn over it by design
                        continue
                    r = rect(c)
                    hit = r[0] < zone[0] + zone[2] and zone[0] < r[0] + r[2] and r[1] < zone[1] + zone[3] and zone[1] < r[1] + r[3]
                    if hit:
                        data = c["componentData"]
                        if tab["tabName"] == "Mix" and data["type"] == "Meter" and data["name"].startswith("Mixer Native "):
                            # The release removes these diagnostic Q-Link frames from the native Mixer.
                            self.assertTrue(hidden_while_page_is_up(c), data["name"])
                            continue
                        if not is_native_host(c):
                            self.assertTrue(hidden_while_page_is_up(c), "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                        self.assertNotIn(c["componentData"]["type"], ("Image", "Meter"), "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

    def defs(self, tui):
        return {d["key"]: d["value"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}

    def test_every_control_is_on_the_axis_of_its_frame(self):
        for tab in self.tabs():
            frames = frame_rects(tab)
            for c in tab["componentDefinition"]["componentsData"]:
                typ = c["componentData"]["type"]
                if is_play_selector(tab, c):
                    continue
                if is_native_host(c):
                    continue
                if typ not in SLOT_TYPES:
                    continue
                x, y, w, h = rect(c)
                column = [i for i, f in enumerate(frames) if inside_horizontally((x, y, w, h), f)]
                self.assertEqual(len(column), 1, "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                f = frames[column[0]]
                self.assertLessEqual(abs(x + w / 2 - (f[0] + f[2] / 2)), 1, "%s / %s" % (tab["tabName"], c["componentData"]["name"]))
                # the frames are the bank frames of the lower rows (y 301 ..) and the host controls sit in rows 1 - 3 (y 44 ..): they are the fallback of the page
                # the plugin draws, so every one of them is hidden while that page is up
                if not is_native_host(c):
                    self.assertTrue(hidden_while_page_is_up(c), "%s / %s" % (tab["tabName"], c["componentData"]["name"]))

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
                     if not is_play_selector(tab, c)
                     if not is_background(c)   # the full-tab background (99_backgrounds.py) lies under everything by design
                     if not (c["componentData"]["type"] == "Label" and (" seg " in c["componentData"]["name"]
                                                                         or c["componentData"]["name"].endswith(" name")))]
            for i, (na, ta, ra, ha) in enumerate(items):
                for nb, tb, rb, hb in items[i + 1:]:
                    if is_native_host(next(c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["name"] == na)) or is_native_host(next(c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["name"] == nb)):
                        continue
                    if ra == rb and ta == tb == "Image":
                        continue   # the pictures of one element (one per choice) share their place
                    if ra == rb and ha and hb and ha != hb:
                        continue   # copies of one control, each valid for another value of the same parameter (the LFO division): never visible together
                    if tb in ("Image",) and nb.startswith(("Overlay Marker", "Overlay Instance")) and (na == "Play Background" or ta.startswith("envDisplay")):
                        # deliberate: the full-tab background of the Play page and the envelope curve are painted first (bottom) and the 6 x 6 markers and the instance picture,
                        # which the plugin finds the page by, sit ON TOP of them (paint order: first = bottom). The order is part of the check.
                        self.assertLess([n for n, *_ in items].index(na), [n for n, *_ in items].index(nb))
                        continue
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
