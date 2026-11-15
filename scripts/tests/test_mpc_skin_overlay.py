#!/usr/bin/env python3
# SynthyBoy - tests of the overlay skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json + scripts/mpc-skin-extras/50_filter_overlay.py) and of the numbers it shares with the plugin
# (shell-mpc/overlay/overlay_layout.json -> overlay_layout.h): marker pictures with the exact colours and positions, the controls, the production skin unchanged.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, ROOT, SDK, generate_skins, run_apply

OVERLAY_LAYOUT = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
PROD_LAYOUT = ROOT / "scripts" / "mpc-ui-pages.json"
SHARED_JSON = ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"
SHARED_HEADER = ROOT / "shell-mpc" / "overlay" / "overlay_layout.h"
GEN_HEADER = ROOT / "scripts" / "gen-overlay-layout-header.py"
SLOT_TYPES = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu")
HAVE_SDK = (SDK / "tools" / "gen_ui.py").is_file()


def decode_png(path):
    """(width, height, rows of (r, g, b[, a]) tuples) of an 8 bit, non interlaced PNG with filter type 0 (what 50_filter_overlay.py writes)."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour in (2, 6), "unsupported PNG"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = 1 + w * channels
    rows = []
    for y in range(h):
        assert raw[y * stride] == 0, "filter type %d" % raw[y * stride]
        line = raw[y * stride + 1:(y + 1) * stride]
        rows.append([tuple(line[x * channels:(x + 1) * channels]) for x in range(w)])
    return w, h, rows


def rect(component):
    return tuple(int(v) for v in component["bounds"]["bounds"].split())


def header_views():
    """The views of the generated header: (name, kind, index, region, opaque, [(x, y, w, h, rgb, cover)] * 2, instance rectangle), screen coordinates."""
    text = SHARED_HEADER.read_text(encoding="utf-8")
    out = []
    for m in re.finditer(r'\{ "(\w+)", (\d+), (\d+), \{ (\d+), (\d+), (\d+), (\d+) \}, (true|false), \{ (.*?) \}, \{ (\d+), (\d+), (\d+), (\d+) \} \},', text):
        markers = [tuple(int(v) for v in mm.groups()[:4]) + (int(mm.group(5), 16), int(mm.group(6), 16))
                   for mm in re.finditer(r"\{ \{ (\d+), (\d+), (\d+), (\d+) \}, 0x([0-9a-f]{6}), 0x([0-9a-f]{6}) \}", m.group(9))]
        out.append((m.group(1), int(m.group(2)), int(m.group(3)), tuple(int(v) for v in m.groups()[3:7]), m.group(8) == "true", markers, tuple(int(v) for v in m.groups()[9:13])))
    origin = int(re.search(r"kTabOriginY = (\d+);", text).group(1))
    bg = int(re.search(r"kBackgroundRgb = 0x([0-9a-f]{6});", text).group(1), 16)
    return out, origin, bg


PAGE_KINDS = ("play", "filterui", "mixer", "fx", "mod", "macros", "master", "voice", "envui", "lfoui")
KINDS = {"filter": 0, "lfo": 1, "osc": 2, "env": 3, "spectrum": 4, "play": 5, "filterui": 6, "mixer": 7, "fx": 8, "mod": 9, "macros": 10, "master": 11, "voice": 12, "envui": 13, "lfoui": 14}


class SharedConstantsTests(unittest.TestCase):
    def test_header_is_current(self):
        r = subprocess.run([sys.executable, str(GEN_HEADER), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_header_matches_the_json(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        views, origin, bg = header_views()
        self.assertEqual(origin, layout["tabOriginY"])
        self.assertEqual(bg, int(layout["background"], 16))
        self.assertEqual(len(views), len(layout["views"]))
        for (name, kind, index, region, opaque, markers, instance), v in zip(views, layout["views"]):
            r = v["region"]
            self.assertEqual((name, kind, index, opaque), (v["name"], KINDS[v["kind"]], v["index"], v["opaque"]))
            self.assertEqual(region, (r["x"], r["y"] + origin, r["w"], r["h"]), "screen coordinates = tab coordinates + the origin of the tab")
            self.assertEqual(markers, [(m["x"], m["y"] + origin, m["w"], m["h"], int(m["rgb"], 16), int(m["cover"], 16)) for m in v["markers"]])
            i = v["instance"]
            self.assertEqual(instance, (i["x"], i["y"] + origin, i["w"], i["h"]))
        text = SHARED_HEADER.read_text(encoding="utf-8")
        inst = layout["instance"]
        self.assertEqual([int(c, 16) for c in re.search(r"kInstanceSlotRgb\[kMaxInstances\] = \{ (.*?) \};", text).group(1).replace("0x", "").split(", ")], [int(c, 16) for c in inst["slotColours"]])
        self.assertEqual(int(re.search(r"kInstanceNoneRgb = 0x([0-9a-f]{6})", text).group(1), 16), int(inst["none"], 16))
        self.assertEqual(int(re.search(r"kInstanceBlankRgb = 0x([0-9a-f]{6})", text).group(1), 16), int(inst["blank"], 16))
        self.assertEqual((int(re.search(r"kMaxInstances = (\d+)", text).group(1)), int(re.search(r"kInstanceFrames = (\d+)", text).group(1)), int(re.search(r"kInstanceSteps = (\d+)", text).group(1))),
                         (inst["slots"], inst["frames"], inst["steps"]))

    def test_the_markers_are_unusual_unique_and_inside_their_region(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        every = []
        for v in layout["views"]:
            c = v["region"]
            self.assertEqual(len(v["markers"]), 2, v["name"])
            for m in v["markers"]:
                self.assertTrue(c["x"] <= m["x"] and m["x"] + m["w"] <= c["x"] + c["w"] and c["y"] <= m["y"] and m["y"] + m["h"] <= c["y"] + c["h"], v["name"])
                self.assertGreaterEqual(m["w"], 4)
                self.assertGreaterEqual(m["h"], 4)
                every.append((v["name"], int(m["rgb"], 16)))
            left, right = v["markers"]
            if v["opaque"]:
                # a page (the whole tab) has its markers 33 px from the left edge and 39 px from the right one (the host's Q-Link panel covers the right end of the Play tab)
                near = 40
                self.assertLess(left["x"] - c["x"], near, "the left marker is near the left end of the region")
                self.assertLess(c["x"] + c["w"] - right["x"] - right["w"], near, "the right marker is near the right end")
            else:
                # the transparent view (envelope marker): the two squares sit side by side in the empty top right corner of the widget, above its top line, 4 x 4 px
                self.assertEqual((left["w"], left["h"], right["w"], right["h"]), (4, 4, 4, 4), v["name"])
                self.assertEqual(left["cover"], layout["background"], "painted over in the colour of the widget's background")
            self.assertNotEqual(layout["background"], left["rgb"])
        # no colour is within the detector's tolerance (2 per channel, so 4 apart can still be told apart: need > 4) of another view's colour
        for i, (na, ca) in enumerate(every):
            for nb, cb in every[i + 1:]:
                near = all(abs(((ca >> s) & 255) - ((cb >> s) & 255)) <= 4 for s in (0, 8, 16))
                self.assertFalse(near, "%s and %s have marker colours that the detector could confuse (%06x %06x)" % (na, nb, ca, cb))
        # the instance picture: inside the region, not on a marker, and its colours are far from every marker colour, the background and each other
        inst = layout["instance"]
        table = [("slot%d" % k, int(c, 16)) for k, c in enumerate(inst["slotColours"])] + [("none", int(inst["none"], 16)), ("blank", int(inst["blank"], 16))]
        self.assertEqual((inst["slots"], len(inst["slotColours"]), inst["frames"], inst["steps"]), (16, 16, 33, 32))
        far = lambda a, b: max(abs(((a >> s) & 255) - ((b >> s) & 255)) for s in (0, 8, 16))
        for v in layout["views"]:
            c, i = v["region"], v["instance"]
            self.assertTrue(c["x"] <= i["x"] and i["x"] + i["w"] <= c["x"] + c["w"] and c["y"] <= i["y"] and i["y"] + i["h"] <= c["y"] + c["h"], v["name"])
            self.assertEqual((i["w"], i["h"]), (6, 6) if v["opaque"] else (4, 4), v["name"])
            for m in v["markers"]:
                self.assertTrue(i["x"] >= m["x"] + m["w"] or m["x"] >= i["x"] + i["w"] or i["y"] >= m["y"] + m["h"] or m["y"] >= i["y"] + i["h"], "%s: the instance picture does not overlap a marker" % v["name"])
        for i, (na, ca) in enumerate(table):
            for nb, cb in table[i + 1:]:
                self.assertGreaterEqual(far(ca, cb), 24, "%s and %s" % (na, nb))
            for nm, cm in every:
                self.assertGreater(far(ca, cm), 8, "%s and the marker colour of %s" % (na, nm))
            self.assertGreater(far(ca, int(layout["background"], 16)), 8, na)
            self.assertNotEqual(ca, 0, na)
        names = [v["name"] for v in layout["views"]]
        self.assertEqual(len(set(names)), len(names))
        tabs = [v["tab"] for v in layout["views"]]
        self.assertEqual(len(set(tabs)), len(tabs), "one view per tab")

    def test_the_meter_row_numbers_live_in_the_shared_json_only(self):
        """The numbers of the meter row live in the shared JSON only (meterRow); 40_spectrum.py reads them (the Macros page of the plugin hides the meters and draws the spectrum itself)."""
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        row = layout["meterRow"]
        width = row["columns"] * row["columnW"] + (row["columns"] - 1) * row["gap"]
        self.assertEqual(row["x"], (layout["screen"]["width"] - width) // 2, "the row is centred on the screen")
        spec = importlib.util.spec_from_file_location("spectrum_module", ROOT / "scripts" / "mpc-skin-extras" / "40_spectrum.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        self.assertEqual((mod.COLUMNS, mod.COLUMN_W, mod.GAP, mod.X0, mod.Y, mod.COLUMN_H), (row["columns"], row["columnW"], row["gap"], row["x"], row["y"], row["h"]))

    def test_every_page_kind_is_a_whole_tab_and_has_one_view_per_tab(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        pages = [v for v in layout["views"] if v["kind"] in PAGE_KINDS]
        self.assertEqual(sorted({v["kind"] for v in pages}), sorted(PAGE_KINDS))
        for v in pages:
            if v["kind"] != "play":
                self.assertEqual(v["region"], {"x": 0, "y": 0, "w": 1280, "h": 628}, v["name"])
        # the header of the plugin has the same kinds and the test of the page kinds
        text = SHARED_HEADER.read_text(encoding="utf-8")
        self.assertIn("constexpr bool isPageKind", text)


class OverlayLayoutFileTests(unittest.TestCase):
    def test_layout_is_the_production_one_minus_the_pages_the_plugin_draws_plus_the_play_pages(self):
        prod, ovl = json.loads(PROD_LAYOUT.read_text(encoding="utf-8")), json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        self.assertEqual(ovl["variant"], "overlay")
        self.assertNotIn("variant", prod)
        self.assertNotIn("tabLimit", ovl, "the overlay layout stays within the 32 tabs of the host")
        self.assertEqual((len(prod["pages"]), len(ovl["pages"])), (34, 26))   # the production layout; the 3 Play tabs, minus the tabs the plugin's pages replace (F1 / F2 model, Mod 3-4, Mod Amt, the single effects)
        self.assertLessEqual(len(ovl["pages"]), 32)
        play = ovl["pages"][:3]
        self.assertEqual([(p["name"], p["fn"], p["kick"], p.get("custom")) for p in play], [(n, "Play", [], True) for n in ("Play", "Play Env", "Play LFO")])
        renamed = {"Mod": "Mod 1-2", "FX": "FX Chain"}
        prod_by_name = {p["name"]: p for p in prod["pages"]}
        for p in ovl["pages"][3:]:
            q = prod_by_name[renamed.get(p["name"], p["name"])]
            self.assertEqual(p["fn"], q["fn"], p["name"])
            self.assertEqual([k for k in p["kick"] if k in q["kick"]], p["kick"], "%s: only pages of the production tab" % p["name"])
        gone = {"F1 Model", "F2 Model", "Mod 3-4", "Mod Amt", "FX Filt", "Distort", "Comp 1", "Comp 2", "EQ", "Chorus", "Delay", "Phaser", "FX Level"}
        self.assertFalse(gone & {p["name"] for p in ovl["pages"]})
        # every view of the shared JSON is on a tab of the layout
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        names = {p["name"] for p in ovl["pages"]}
        self.assertEqual({v["tab"] for v in layout["views"]} - names, set())


@unittest.skipUnless(HAVE_SDK, "MPC SDK not found (set MPC_SDK)")
class OverlaySkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        (root / "o").mkdir()
        (root / "p").mkdir()
        (root / "q").mkdir()
        cls.ovl = generate_skins(root / "o", OVERLAY_LAYOUT)
        cls.ovl_code, cls.ovl_err = run_apply(cls.ovl, REAL_EXTRAS)
        cls.prod = generate_skins(root / "p", PROD_LAYOUT)
        cls.prod_code, cls.prod_err = run_apply(cls.prod, REAL_EXTRAS)
        # the production skin once more with every module except the overlay one
        # (the modules find docs/params.json relative to their own folder: the copy sits in a tree of the same shape)
        cls.extras_without = root / "tree" / "scripts" / "mpc-skin-extras"
        shutil.copytree(REAL_EXTRAS, cls.extras_without, ignore=shutil.ignore_patterns("50_filter_overlay.py", "__pycache__"))
        (root / "tree" / "docs").mkdir()
        shutil.copyfile(PARAMS_JSON, root / "tree" / "docs" / "params.json")
        (root / "tree" / "shell-mpc" / "overlay").mkdir(parents=True)   # 40_spectrum.py reads the geometry of the meter row from the shared layout file
        shutil.copyfile(SHARED_JSON, root / "tree" / "shell-mpc" / "overlay" / "overlay_layout.json")
        cls.base = generate_skins(root / "q", PROD_LAYOUT)
        cls.base_code, cls.base_err = run_apply(cls.base, cls.extras_without)
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.o_tui = json.loads((cls.ovl / "TUI.json").read_text(encoding="utf-8"))
        cls.o_ql = json.loads((cls.ovl / "Q-Links.json").read_text(encoding="utf-8"))
        cls.p_tui = json.loads((cls.prod / "TUI.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    # the finished overlay skin carries the labels of the function keys as tabName (95_subtabs.py); the tests refer to a page by its internal name
    LABELS = {p["name"]: p["label"] for p in json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))["pages"] if "label" in p}

    def tab(self, name, tui=None):
        shown = name if tui is not None else self.LABELS.get(name, name)
        return next(t for t in (tui or self.o_tui)["pageData"]["tabs"] if t["tabName"] == shown)

    def controls(self, tab):
        out = {}
        for c in tab["componentDefinition"]["componentsData"]:
            if c["componentData"]["type"] in SLOT_TYPES:
                out[self.params[int(c["handle remapping"]["map"][0]["value"].split()[1])]["key"]] = c
        return out

    def test_apply_ok(self):
        self.assertEqual((self.ovl_code, self.ovl_err, self.prod_code, self.prod_err, self.base_code, self.base_err), (0, "", 0, "", 0, ""))

    # ---- the production skin is what it was ----
    def test_production_skin_is_byte_identical_without_the_overlay_module(self):
        files = lambda d: sorted(str(p.relative_to(d)) for p in d.rglob("*") if p.is_file())   # (the skin has a sub folder since the browser tile: browser_images/)
        self.assertEqual(files(self.prod), files(self.base))
        for name in files(self.prod):
            self.assertEqual((self.prod / name).read_bytes(), (self.base / name).read_bytes(), name)
        self.assertEqual([n for n in files(self.prod) if n.startswith("overlay_")], [])
        meta = json.loads((self.prod.parent / "skin-meta.json").read_text(encoding="utf-8"))
        self.assertNotIn("variant", meta)

    def test_production_skin_has_the_34_tabs_and_the_old_filter_tab(self):
        tabs = self.p_tui["pageData"]["tabs"]
        self.assertEqual(len(tabs), 34)
        f1 = self.controls(self.tab("Filter 1", self.p_tui))
        self.assertIn("filter1.blend", f1)
        self.assertIn("filter1.keytrack", f1)
        self.assertNotIn("filter1.cutoff", f1)

    # ---- the overlay skin ----
    def test_26_tabs_and_the_sub_tabs_are_under_the_same_function_key(self):
        tabs = self.o_tui["pageData"]["tabs"]
        self.assertEqual(len(tabs), 26)
        self.assertLessEqual(len(tabs), 32, "the host's limit")
        f1, f2 = self.tab("Filter 1"), self.tab("Filter 2")
        self.assertEqual((f1["fnKeyIndex"], f2["fnKeyIndex"]), (4, 4))   # the Play key is the first one
        self.assertEqual((f1["fnKeySubIndex"], f2["fnKeySubIndex"]), (0, 1))
        self.assertEqual(len({(t["fnKeyIndex"], t["fnKeySubIndex"]) for t in tabs}), 26)
        for gone in ("F1 Model", "F2 Model", "Mod 3-4", "Mod Amt", "FX Filt", "Distort", "Comp 1", "Comp 2", "EQ", "Chorus", "Delay", "Phaser", "FX Level"):
            self.assertFalse([t for t in tabs if t["tabName"] == gone], gone)

    PAGE_TABS = ("Filter 1", "Filter 2", "Env 1", "Env 2", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Macros", "Mixer", "Mod", "FX", "Master", "Voice")

    def test_the_page_tabs_are_the_views_of_the_layout(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        views = {v["tab"] for v in layout["views"] if v["kind"] in PAGE_KINDS and v["kind"] != "play"}
        self.assertEqual(views, set(self.PAGE_TABS))

    def test_no_host_widget_of_a_page_tab_is_visible_under_the_plugins_page(self):
        hide = "RangedEnabling/0/18/33/Parameter %d" % (len(self.params) + 4 + 32 + 2)
        for name in self.PAGE_TABS:
            comps = self.tab(name)["componentDefinition"]["componentsData"]
            hidden = 0
            for c in comps:
                if c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance")):
                    self.assertNotIn(hide, c["bounds"].get("additionalInvalidatingHandles", []), "%s: the plugin finds the page by this picture" % c["componentData"]["name"])
                else:
                    self.assertIn(hide, c["bounds"].get("additionalInvalidatingHandles", []), "%s on %s hides while the plugin's page is up" % (c["componentData"]["name"], name))
                    self.assertEqual(c["bounds"]["version"], 2)
                    hidden += 1
            self.assertGreater(hidden, 0, name)

    def test_the_page_tabs_have_the_q_proxies_on_q_link_1_to_4_in_the_row_direction(self):
        first_proxy = len(self.params) + 4 + 32 + 2 + 1 + 3 + 24   # "Q 1": after the Overlay Instance, the Play selectors and the 24 Play proxies
        for name in self.PAGE_TABS:
            tab = self.tab(name)
            entry = next(e for e in self.o_ql["Screen Mode Q-Links"]["map"] if e["Tab"] == tab["fnKeyIndex"] + 1 and e["SubTab"] == tab["fnKeySubIndex"] + 1)
            self.assertEqual(entry["Bank Direction"], "Row", name)
            self.assertEqual([entry["Q-Links"]["Q-Link %d" % n] for n in range(1, 17)], [first_proxy + i for i in range(4)] + [-1] * 12, name)
            self.assertEqual(tab["qlinkBoundsData"], ["16 301 288 323", "336 301 288 323", "656 301 288 323", "976 301 288 323"], name)

    def test_all_other_tabs_are_the_production_tabs_plus_the_marker_pictures(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        with_markers = {v["tab"] for v in layout["views"]}
        for t in self.p_tui["pageData"]["tabs"]:
            if t["tabName"] not in {p["name"] for p in json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))["pages"]} or t["tabName"] in self.PAGE_TABS:
                continue   # a tab the plugin's pages replace, or a page tab (its widgets are hidden while the page is up: test_no_host_widget...)
            o = self.tab(t["tabName"])
            ot = {k: v for k, v in o.items() if k not in ("fnKeySubIndex", "fnKeyIndex", "tabName")}   # (the label of a function key is the overlay's tabName: test_function_key_labels...)
            pt = {k: v for k, v in t.items() if k not in ("fnKeySubIndex", "fnKeyIndex", "tabName")}
            self.assertEqual(o["fnKeyIndex"], t["fnKeyIndex"] + 1, "%s: the Play key is the first, every other key moved one place" % t["tabName"])
            self.assertEqual(o["qlinkBoundsData"], t["qlinkBoundsData"])
            if t["tabName"] in with_markers:
                comps = ot["componentDefinition"]["componentsData"]
                vname = next(v["name"] for v in layout["views"] if v["tab"] == t["tabName"])
                self.assertEqual([c["componentData"]["name"] for c in comps[-3:]], ["Overlay Marker %s %s" % (vname, s) for s in ("left", "right")] + ["Overlay Instance %s" % vname])
                ot = json.loads(json.dumps(ot))
                ot["componentDefinition"]["componentsData"] = comps[:-3]
            self.assertEqual(ot, pt, t["tabName"])

    def test_the_pictures_of_the_lfo_and_osc_tabs_stay_under_the_views(self):
        for name in ("LFO 1", "LFO 4", "Osc 1", "Osc 3"):
            comps = self.tab(name)["componentDefinition"]["componentsData"]
            pictures = [c for c in comps if c["componentData"]["type"] == "Image" and not c["componentData"]["name"].startswith(("Overlay Marker", "Overlay Instance"))]
            self.assertGreaterEqual(len(pictures), 10, name)
            order = [c["componentData"]["name"] for c in comps]
            for c in pictures:
                self.assertLess(order.index(c["componentData"]["name"]), order.index("Overlay Marker %s left" % name.lower().replace(" ", "")), "the markers are above the pictures")

    def test_the_macros_tab_keeps_its_32_meters_hidden_under_the_page_and_the_markers_are_above_them(self):
        comps = self.tab("Macros")["componentDefinition"]["componentsData"]
        meters = [c for c in comps if c["componentData"]["type"] == "Meter"]
        self.assertEqual(len(meters), 32)
        order = [c["componentData"]["name"] for c in comps]
        for name in ("Overlay Marker macros left", "Overlay Marker macros right", "Overlay Instance macros"):
            self.assertGreater(order.index(name), max(order.index(m["componentData"]["name"]) for m in meters), "%s is drawn above the meters" % name)
        prod = {c["componentData"]["name"]: rect(c) for c in self.tab("Macros", self.p_tui)["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Meter"}
        self.assertEqual({c["componentData"]["name"]: rect(c) for c in meters}, prod, "the meters are the production meters, in the same places")

    def test_marker_pictures_have_the_exact_colours_and_positions(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        for v in layout["views"]:
            items = {c["componentData"]["name"]: c for c in self.tab(v["tab"])["componentDefinition"]["componentsData"]}
            for side, m in zip(("left", "right"), v["markers"]):
                c = items["Overlay Marker %s %s" % (v["name"], side)]
                self.assertEqual(c["componentData"]["type"], "Image")
                self.assertEqual(rect(c), (m["x"], m["y"], m["w"], m["h"]), "bounds = the numbers of the shared JSON (1:1, no scaling)")
                png = self.ovl / c["componentData"]["data"]["image"]
                w, h, rows = decode_png(png)
                self.assertEqual((w, h), (m["w"], m["h"]))
                want = tuple(int(m["rgb"][i:i + 2], 16) for i in (0, 2, 4))
                self.assertTrue(all(px[:3] == want for row in rows for px in row), "every pixel of the marker picture is exactly #%s" % m["rgb"])
            if v["kind"] == "filter":
                bg = items["Filter %d Curve Background" % v["index"]]
                cv = v["region"]
                self.assertEqual(rect(bg), (cv["x"], cv["y"], cv["w"], cv["h"]))
                w, h, rows = decode_png(self.ovl / bg["componentData"]["data"]["image"])
                self.assertEqual((w, h), (cv["w"], cv["h"]))
                self.assertEqual({px[:3] for row in rows for px in row}, {tuple(int(layout["background"][i:i + 2], 16) for i in (0, 2, 4))})
            else:
                self.assertFalse(any(n.endswith("Curve Background") for n in items), "%s keeps its own picture, no extra dark one" % v["tab"])

    def test_instance_pictures_are_film_strips_with_the_exact_colours_bound_to_the_last_parameter(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        inst = layout["instance"]
        want = [tuple(int(inst["blank"][i:i + 2], 16) for i in (0, 2, 4))] * inst["frames"]
        for k, c in enumerate(inst["slotColours"]):
            want[k + 1] = tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))
        want[inst["slots"] + 1] = tuple(int(inst["none"][i:i + 2], 16) for i in (0, 2, 4))
        index = len(self.params) + 4 + 32 + 2   # the engine's parameters, the 4 LFO cursors, the 32 spectrum columns, the 2 pad pressure parameters: "Overlay Instance" is the next one (docs/MPC.md "Parameter order")
        self.assertEqual(want[0], want[-1])
        self.assertEqual(len({c for c in want[1:inst["slots"] + 2]}), inst["slots"] + 1, "16 slots + none: 17 different colours")
        for v in layout["views"]:
            items = {c["componentData"]["name"]: c for c in self.tab(v["tab"])["componentDefinition"]["componentsData"]}
            c = items["Overlay Instance %s" % v["name"]]
            i = v["instance"]
            self.assertEqual((c["componentData"]["type"], c["componentData"]["data"]["imageType"], c["componentData"]["data"]["numFrames"], c["componentData"]["data"]["handleName"]), ("Image", "FilmStrip", 33, "Data"))
            self.assertEqual(c["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % index}])
            self.assertEqual(rect(c), (i["x"], i["y"], i["w"], i["h"]), "1:1 at the place of the shared JSON")
            w, h, rows = decode_png(self.ovl / c["componentData"]["data"]["image"])
            self.assertEqual((w, h), (i["w"], i["h"] * inst["frames"]), "one frame is the size of the picture, the frames lie one under the other")
            for f in range(inst["frames"]):
                got = {px for row in rows[f * i["h"]:(f + 1) * i["h"]] for px in row}
                self.assertEqual(got, {want[f]}, "frame %d is exactly one colour #%02x%02x%02x" % ((f,) + want[f]))
            png = (self.ovl / c["componentData"]["data"]["image"]).read_bytes()
            self.assertEqual(png[24:29], bytes([8, 2, 0, 0, 0]), "8 bit RGB without alpha, no interlace: the settings of the marker pictures")
        # the production skin has no instance picture and the overlay skin's parameter exists only in the overlay plugin
        self.assertFalse(any("Overlay Instance" in json.dumps(t) for t in self.p_tui["pageData"]["tabs"]))

    def test_no_colour_of_the_rest_of_the_skin_equals_a_marker_colour(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        markers = {int(m["rgb"], 16) for v in layout["views"] for m in v["markers"]}
        markers |= {int(c, 16) for c in layout["instance"]["slotColours"]} | {int(layout["instance"]["none"], 16), int(layout["instance"]["blank"], 16)}   # the instance colours are as unusual
        for tui in (self.o_tui, self.p_tui):
            text = json.dumps(tui)
            for value in re.findall(r"\"(?:ff)?([0-9a-fA-F]{6})\"", text):
                self.assertNotIn(int(value, 16), markers)
        # the pictures of the production skin: no pixel of a marker colour (within 2 per channel, what the detector accepts)
        for png in sorted(self.prod.glob("*.png")):
            w, h, rows = decode_png_any(png)
            for row in rows:
                for px in row:
                    for m in markers:
                        near = all(abs(px[i] - ((m >> s) & 255)) <= 2 for i, s in ((0, 16), (1, 8), (2, 0)))
                        if near and (len(px) < 4 or px[3] > 0):
                            self.fail("%s has a pixel %r close to a marker colour %06x" % (png.name, px, m))

    # ---- the Play page ----
    PLAY = (("Play", "osc", 3), ("Play Env", "env", 6), ("Play LFO", "lfo", 10))
    SLOTS = {"osc": ("frame", "unison.voices", "unison.detune", "unison.blend", "phase", "random_phase", "pan", "level"),
             "env": ("attack", "hold", "decay", "sustain", "release", "attack_curve", "decay_curve", "release_curve"),
             "lfo": ("rate", "fade_in", "delay", "smooth_time", "smooth", "sync", "sync_type", "division")}

    def play_first(self):
        return len(self.params) + 4 + 32 + 2 + 1   # "Overlay Instance" is before: Play Osc, Play Env, Play LFO, then the 24 proxies (docs/MPC.md "Parameter order")

    def test_play_tabs_are_the_first_function_key(self):
        tabs = self.o_tui["pageData"]["tabs"]
        self.assertEqual([t["tabName"] for t in tabs[:3]], ["Play", "Play Env", "Play LFO"])
        self.assertEqual([(t["fnKeyIndex"], t["fnKeySubIndex"]) for t in tabs[:3]], [(0, 0), (0, 1), (0, 2)])
        self.assertEqual(tabs[3]["tabName"], "Macros")
        self.assertEqual(tabs[3]["fnKeyIndex"], 1)
        self.assertEqual(len({t["fnKeyIndex"] for t in tabs}), 10, "ten function keys")
        self.assertFalse(any("Play" in t["tabName"] for t in self.p_tui["pageData"]["tabs"]), "the production skin has no Play tab")

    def test_function_key_labels_are_the_short_names_and_the_other_sub_tabs_keep_theirs(self):
        # the host draws the tabName of the current sub-tab on the function key: the first sub-tab of every key carries the short label
        tabs = self.o_tui["pageData"]["tabs"]
        first = [t["tabName"] for t in tabs if t["fnKeySubIndex"] == 0]
        self.assertEqual(first, ["Play", "Macros", "Osc", "Mix", "Filter", "Env", "LFO", "Mod", "FX", "Master"])
        later = [t["tabName"] for t in tabs if t["fnKeySubIndex"] > 0]
        self.assertEqual(later, ["Play Env", "Play LFO", "Osc1 Shp", "Osc 2", "Osc2 Shp", "Osc 3", "Osc3 Shp", "Mixer", "Filter 2", "Env 2", "Env 3", "Env 4", "LFO 2", "LFO 3", "LFO 4", "Voice"])
        names = [t["tabName"] for t in tabs]
        self.assertEqual(len(names), len(set(names)), "the tab names of the finished skin are unique")
        self.assertTrue(all(1 <= len(n) <= 8 for n in names))
        # the Q-Link map follows the keys, not the names
        ql = self.o_ql["Screen Mode Q-Links"]["map"]
        self.assertEqual([(e["Tab"], e["SubTab"]) for e in ql], [(t["fnKeyIndex"] + 1, t["fnKeySubIndex"] + 1) for t in tabs])
        # the internal names stay in skin-meta.json, labels only for the pages that have one; the production skin has none
        meta = json.loads((self.ovl.parent / "skin-meta.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["labels"], self.LABELS)
        self.assertEqual(sorted(meta["labels"].values()), sorted(["Osc", "Mix", "Filter", "Env", "LFO"]))
        self.assertNotIn("labels", json.loads((self.prod.parent / "skin-meta.json").read_text(encoding="utf-8")))
        self.assertEqual([t["tabName"] for t in self.p_tui["pageData"]["tabs"] if t["fnKeySubIndex"] == 0][:4], ["Macros", "Osc 1", "Sub/Nois", "Filter 1"], "the production skin keeps its names")

    def test_a_label_that_clashes_or_is_too_long_stops_the_generator(self):
        import test_gen_mpc_ui_layout as lay
        base = json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        for page, label in (("Osc 1", "Osc 2"), ("Env 1", "Env 1 long"), ("Osc 1", "Mixer"), ("Env 1", "")):
            doc = json.loads(json.dumps(base))
            next(p for p in doc["pages"] if p["name"] == page)["label"] = label
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "layout.json"
                path.write_text(json.dumps(doc), encoding="utf-8")
                code, out, err = lay.run(tmp, path)
                self.assertEqual(code, 2, (page, label, err))

    def test_play_controls_and_q_links(self):
        first = self.play_first()
        sel = [first + g for g in range(3)]
        prox = [[first + 3 + g * 8 + s for s in range(8)] for g in range(3)]
        q = [first + 3 + 24 + i for i in range(4)]
        for g, (tab_name, prefix, _) in enumerate(self.PLAY):
            tab = self.tab(tab_name)
            items = tab["componentDefinition"]["componentsData"]
            bound = lambda c: int(c["handle remapping"]["map"][0]["value"].split()[1]) if c["handle remapping"]["map"] else None
            slots = [c for c in items if c["componentData"]["type"] in SLOT_TYPES]
            # the oscillator, the envelope and the LFO thirds are the plugin's own drawing: no selector, no knob (the compact views and the full-screen pages replace them)
            self.assertEqual(sorted(set(bound(c) for c in slots)), [], "%s: the three selectors and the twenty-four knobs are not host widgets" % tab_name)
            kinds = {bound(c): c["componentData"]["type"] for c in slots}
            # every host widget of the Env and LFO thirds is hidden while the plugin's full screen is open: the "Overlay Instance" frame 24 is outside RangedEnabling 0 .. 18 of the 33 frames
            hide = "RangedEnabling/0/18/33/Parameter %d" % (first - 1)
            for c in slots:
                self.assertIn(hide, c["bounds"].get("additionalInvalidatingHandles", []), "%s: every selector and knob of the Play pages hides while a full screen is open" % tab_name)
                self.assertEqual(c["bounds"]["version"], 2)
            self.assertEqual(kinds, {})
            view = next(v for v in json.loads(SHARED_JSON.read_text(encoding="utf-8"))["views"] if v["tab"] == tab_name)
            for p, i in enumerate(sel):
                if p in (0, 1, 2):
                    continue
                c = next(c for c in slots if bound(c) == i)
                x, y, w, h = rect(c)
                centre = view["region"]["x"] + p * 417 + 200
                self.assertTrue(abs(x + w // 2 - centre) <= 2 and y + h <= 301, "the selector %d is under its panel and above the Q-Link frames" % p)
            # the knobs are copies of the control of instance 1 on its own tab (same widget), in the lower two rows, slot n = Q-Link n (Q-Link 1 .. 4 are the Q proxies, see below)
            for s, key in enumerate(self.SLOTS[prefix] if g > 2 else ()):
                c = next(c for c in slots if bound(c) == prox[g][s])
                x, y, w, h = rect(c)
                self.assertEqual(x, (21, 341, 661, 981)[s % 4])
                self.assertTrue(y >= 343 and y + h <= 621, "%s inside the lower two rows" % c["componentData"]["name"])
                self.assertEqual(y < 483, s >= 4, "slots 1 .. 4 are the bottom row, 5 .. 8 the row above")
                real = next((t for t in self.p_tui["pageData"]["tabs"] for rc in t["componentDefinition"]["componentsData"]
                             if rc["handle remapping"]["map"] and bound(rc) == next(i for i, q_ in enumerate(self.params) if q_["key"] == "%s1.%s" % (prefix, key))), None)
                if real is not None:
                    rc = next(rc for rc in real["componentDefinition"]["componentsData"] if rc["handle remapping"]["map"] and bound(rc) == next(i for i, q_ in enumerate(self.params) if q_["key"] == "%s1.%s" % (prefix, key)))
                    self.assertEqual(c["componentData"]["type"], rc["componentData"]["type"], "%s is the same widget as the real parameter's control" % c["componentData"]["name"])
            entry = next(e for e in self.o_ql["Screen Mode Q-Links"]["map"] if e["Tab"] == tab["fnKeyIndex"] + 1 and e["SubTab"] == tab["fnKeySubIndex"] + 1)
            expected = [-1] * 16
            # Bank Direction Column: bank 0 is read top to bottom, Q-Link 13, 9, 5, 1 = Q 1 .. Q 4; the oscillator sub-tab is Bank Direction Row with Q-Link 1 .. 4 (the host honours only the bottom row there, measured on the device)
            self.assertEqual(entry["Bank Direction"], "Row" if g < 3 else "Column")
            for k, n in enumerate((1, 2, 3, 4) if g < 3 else (13, 9, 5, 1)):
                expected[n - 1] = q[k]
            if g > 2:
                for k, n in enumerate((14, 10, 6, 2)):   # bank 1 top to bottom: the second row of the module's knobs
                    expected[n - 1] = prox[g][4 + k]
            self.assertEqual([entry["Q-Links"]["Q-Link %d" % n] for n in range(1, 17)], expected,
                             "%s: the first bank (Q-Link 13, 9, 5, 1 top to bottom; Q-Link 1 .. 4 with Bank Direction Row on the oscillator sub-tab) is Q 1 .. Q 4, the second (14, 10, 6, 2) the second row of the module's knobs (none on the oscillator sub-tab), the others free" % tab_name)
            self.assertEqual(tab["qlinkBoundsData"], ["16 301 288 323", "336 301 288 323", "656 301 288 323", "976 301 288 323"])
            # names: every knob has its short name over it, unique inside the tab
            names = [c["componentData"]["data"]["text"] for c in items if c["componentData"]["type"] == "Label" and c["componentData"]["name"].endswith(" name")]
            self.assertEqual(len(names), {"osc": 0, "env": 0, "lfo": 0}[prefix], "a name over every knob and toggle (the selectors, three of the LFO's eight controls, show the host's own header)")
            self.assertEqual(len(set(names)), len(names))

    def test_the_three_thirds_have_no_host_widget(self):
        for tab_name, _, _ in self.PLAY:
            items = {c["componentData"]["name"] for c in self.tab(tab_name)["componentDefinition"]["componentsData"]}
            for gone in ("Play Panel 1", "Play Title 1", "Play Osc Select", "Play Panel 2", "Play Title 2", "Play Env Select", "Play Panel 3", "Play Title 3", "Play LFO Select"):
                self.assertNotIn(gone, items, "%s: %s is under a plugin-drawn view" % (tab_name, gone))
            for kept in ("Play Background",):
                self.assertIn(kept, items, "%s: %s stays" % (tab_name, kept))
            self.assertFalse([n for n in items if n.startswith("Play Osc ") or n.startswith("Play Env ") or n.startswith("Play Lfo ")], "%s: no oscillator, envelope or LFO control" % tab_name)

    def test_there_is_no_page_only_the_overlay_can_reach_so_the_play_page_carries_the_full_screen_ui(self):
        # the format: every tab sits on a function key and is reached by the key (ui-reference "fnKeyIndex" / "fnKeySubIndex"); a plugin has no call that selects a tab (docs/MPC.md "Q proxies").
        names = [t["tabName"] for t in self.o_tui["pageData"]["tabs"]]
        self.assertFalse([n for n in names if "Full" in n], "no blank page of its own: the full-screen page is drawn over the Play page")
        self.assertEqual(len(names), 26, "the tab count of the overlay skin")

    def test_play_views_markers_and_the_fallback_picture(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        views = [v for v in layout["views"] if v["kind"] == "play"]
        self.assertEqual([v["tab"] for v in views], ["Play", "Play Env", "Play LFO"])
        self.assertEqual([v["index"] for v in views], [1, 2, 3], "the highlighted panel of the sub-tab")
        for v in views:
            self.assertEqual(v["region"], {"x": 23, "y": 14, "w": 1234, "h": 190})
            items = {c["componentData"]["name"]: c for c in self.tab(v["tab"])["componentDefinition"]["componentsData"]}
            self.assertIn("Overlay Marker %s left" % v["name"], items)
            self.assertIn("Overlay Marker %s right" % v["name"], items)
            self.assertEqual(rect(items["Play Background"]), (23, 14, 1234, 190), "the dark picture under the plane (the overlay off)")
            self.assertFalse([n for n in items if n.startswith("Play Title ")], "no title of a panel: the three panels are the plugin's own drawing")
            order = [c["componentData"]["name"] for c in self.tab(v["tab"])["componentDefinition"]["componentsData"]]
            self.assertLess(order.index("Play Background"), order.index("Overlay Marker %s left" % v["name"]), "the picture is below the markers")

    def test_validate_py(self):
        res = subprocess.run([sys.executable, "-I", str(SDK / "tools" / "validate.py"), str(self.ovl.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        names = ["overlay_instance_6x6.png"] + ["overlay_marker_%s_%s.png" % (v["name"], s) for v in layout["views"] for s in ("left", "right")]
        for name in names:
            self.assertIn("image %s present" % name, report)


def decode_png_any(path):
    """Like decode_png, for the PNGs of the production skin (any filter type, 8 bit RGB / RGBA / grey / palette are skipped when not understood)."""
    data = Path(path).read_bytes()
    pos, idat, info = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            info = struct.unpack(">IIBBBBB", body[:13])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    w, h, depth, colour, _, _, interlace = info
    if depth != 8 or colour not in (2, 6) or interlace:
        return w, h, []
    ch = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * ch
    prev = bytearray(stride)
    rows, pos = [], 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        prev = line
        rows.append([tuple(line[x * ch:(x + 1) * ch]) for x in range(w)])
    return w, h, rows


if __name__ == "__main__":
    unittest.main()
