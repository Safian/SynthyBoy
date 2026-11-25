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

from PIL import Image, ImageChops

from native_ui_params import PLAY_LFO_STANDARD, PLAY_OSC, mixer_peek_index
from test_mpc_backgrounds import bg_mod
from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, ROOT, SDK, extras_with_manifest, generate_skins, run_apply, standalone_editor_entries

OVERLAY_LAYOUT = ROOT / "scripts" / "overlay-skin" / "mpc-ui-pages-overlay.json"
PROD_LAYOUT = OVERLAY_LAYOUT
SHARED_JSON = ROOT / "shell-mpc" / "overlay" / "overlay_layout.json"
SHARED_HEADER = ROOT / "shell-mpc" / "overlay" / "overlay_layout.h"
GEN_HEADER = ROOT / "scripts" / "gen-overlay-layout-header.py"
SLOT_TYPES = ("knobUni", "knobBi", "toggle", "faderUni", "faderBi", "radio3", "radio4", "sliderWithFocus", "sliderWithFocusMenu", "playOscSelector", "playModSelector", "compactNativeDial", "compactNativeRiseDial", "compactNativeSmoothTime", "compactNativeSmoothToggle", "playSubLevel")
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


PAGE_KINDS = ("play", "filterui", "mixer", "fx", "mod", "macros", "master", "voice", "envui", "lfoui", "oscui")
KINDS = {"filter": 0, "lfo": 1, "env": 3, "spectrum": 4, "play": 5, "filterui": 6, "mixer": 7, "fx": 8, "mod": 9, "macros": 10, "master": 11, "voice": 12, "envui": 13, "lfoui": 14, "oscui": 15}


class SharedConstantsTests(unittest.TestCase):
    def test_header_is_current(self):
        r = subprocess.run([sys.executable, str(GEN_HEADER), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_the_generator_takes_its_own_kinds_namespace_and_source_for_another_plugin(self):
        """Migration step 9 (docs/ui-components/TEMPLATE.md): a plugin made with the kit generates a header of its own from its layout file; the default output stays byte for byte SynthyBoy's (test_header_is_current)."""
        template_json = ROOT / "template-mpc" / "skin" / "overlay_layout.json"
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "demo_layout.h"
            r = subprocess.run([sys.executable, str(GEN_HEADER), "--json", str(template_json), "--kinds", "page=16", "--namespace", "demo::layout", "--out", str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            text = out.read_text(encoding="utf-8")
            self.assertIn("namespace demo { namespace layout {", text)
            self.assertIn("}} // namespace demo::layout", text)
            self.assertIn("constexpr int kKindPage = 16;", text)
            self.assertIn("constexpr bool isPageKind (int kind) { return kind >= 16; }", text)
            self.assertIn("from template-mpc/skin/overlay_layout.json", text.splitlines()[0])
            self.assertEqual(len(re.findall(r'^    \{ "', text, re.M)), len(json.loads(template_json.read_text(encoding="utf-8"))["views"]))
            # without --kinds the kind "page" is not SynthyBoy's: the generator says so
            r = subprocess.run([sys.executable, str(GEN_HEADER), "--json", str(template_json), "--out", str(out)], capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            r = subprocess.run([sys.executable, str(GEN_HEADER), "--json", str(template_json), "--kinds", "page", "--out", str(out)], capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0, "a kind without a number is refused")

    def test_the_skin_modules_take_the_layout_and_the_parameter_numbers_from_the_layout_file(self):
        """The modules of SynthyBoy's skin run on another plugin's layout (template-mpc): the layout path from the meta or the environment, "paramsBefore" / "paramsAfterInstance" with SynthyBoy's numbers as the default."""
        spec = importlib.util.spec_from_file_location("m50", REAL_EXTRAS / "50_filter_overlay.py")
        m50 = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m50)
        class Ctx:
            meta = None
        ctx = Ctx()
        self.assertEqual(m50.layout_path(ctx), m50.LAYOUT_JSON)
        ctx.meta = {"variant": "overlay", "layout_path": "/somewhere/else.json"}
        self.assertEqual(m50.layout_path(ctx), Path("/somewhere/else.json"))
        self.assertEqual(m50.params_before({}), 38, "SynthyBoy's 4 LFO cursors + 32 spectrum columns + 2 pad pressure parameters")
        self.assertEqual(m50.params_before({"paramsBefore": 0}), 0)
        self.assertEqual(m50.params_before(json.loads(SHARED_JSON.read_text(encoding="utf-8"))), 38, "the layout of SynthyBoy carries no key: the default")
        template = json.loads((ROOT / "template-mpc" / "skin" / "overlay_layout.json").read_text(encoding="utf-8"))
        self.assertEqual((template["paramsBefore"], template["paramsAfterInstance"]), (0, 0))
        self.assertIn("page", m50.KNOWN_KINDS)

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
        """The numbers of the meter row live in the shared JSON only (meterRow); the Macros page of the plugin draws the spectrum there."""
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        row = layout["meterRow"]
        width = row["columns"] * row["columnW"] + (row["columns"] - 1) * row["gap"]
        self.assertEqual(row["x"], (layout["screen"]["width"] - width) // 2, "the row is centred on the screen")

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
    def test_overlay_layout_file(self):
        ovl = json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        self.assertEqual(ovl["variant"], "overlay")
        self.assertNotIn("tabLimit", ovl, "the overlay layout stays within the 32 tabs of the host")
        self.assertEqual(len(ovl["pages"]), 15, "22 pages before the Env, LFO and Filter merges (Env 1-4, LFO 1-4 and Filter 1-2 are ONE tab each)")
        self.assertEqual([p["name"] for p in ovl["pages"]], ["Play", "Macros", "Osc A", "Osc B", "Osc C", "Sub", "Noise", "Env", "LFO", "FX", "Mixer", "Filter", "Mod", "Master", "Voice"])
        self.assertEqual({p["name"]: p["fn"] for p in ovl["pages"] if p["name"] in ("Env", "LFO", "Filter")}, {"Env": "Env", "LFO": "LFO", "Filter": "Filter"}, "one tab under each of the three function keys")
        self.assertFalse([p for p in ovl["pages"] if p["name"] in ("Env", "LFO", "Filter") and "label" in p], "the key text is the tab name: no label")
        self.assertLessEqual(len(ovl["pages"]), 32)
        play = ovl["pages"][:1]
        self.assertEqual([(p["name"], p["fn"], p["kick"], p.get("custom")) for p in play], [(n, "Play", [], True) for n in ("Play",)])
        osc_pages = {"Osc A": "osc1_main", "Osc B": "osc2_main", "Osc C": "osc3_main", "Sub": "sub_main", "Noise": "noise"}
        for p in ovl["pages"][1:]:
            if p["name"] in osc_pages:
                self.assertEqual((p["fn"], p["kick"]), ("Osc", [osc_pages[p["name"]]]), p["name"])
        gone = {"F1 Model", "F2 Model", "Mod 3-4", "Mod Amt", "FX Filt", "Distort", "Comp 1", "Comp 2", "EQ", "Chorus", "Delay", "Phaser", "FX Level", "Osc 1", "Osc1 Shp", "Osc 2", "Osc2 Shp", "Osc 3", "Osc3 Shp", "Sub/Nois"}
        self.assertFalse(gone & {p["name"] for p in ovl["pages"]})
        # every view of the shared JSON is on a tab of the layout
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        names = {p["name"] for p in ovl["pages"]}
        self.assertEqual({v["tab"] for v in layout["views"]} - names, set())
        # the views of the merged tabs: env1 and lfo1 keep their names and marker colours, the markers of the removed views are gone
        by = {v["name"]: v for v in layout["views"]}
        self.assertEqual((by["env1"]["tab"], by["lfo1"]["tab"]), ("Env", "LFO"))
        self.assertEqual([m["rgb"] for m in by["env1"]["markers"]], ["4f3a2c", "3a4f64"])
        self.assertFalse({"env2", "env3", "env4", "lfo2", "lfo3", "lfo4"} & set(by))
        removed = {"563a2c", "3a5664", "5d3a2c", "3a5d64", "643a2c", "3a6464", "253a2c", "3a2564", "2c3a2c", "3a2c64", "333a2c", "3a3364"}
        self.assertEqual(removed & {m["rgb"] for v in layout["views"] for m in v["markers"]}, set(), "the markers of the removed views are gone")
        self.assertEqual([m["rgb"] for m in by["lfo1"]["markers"]], ["1e3a2c", "3a1e64"])


@unittest.skipUnless(HAVE_SDK, "MPC SDK not found (set MPC_SDK)")
class NativeBackgroundStateTests(unittest.TestCase):
    """Compare the two native roots with an empty manifest through the production skin pipeline."""
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        (root / "o").mkdir()
        cls.skins = generate_skins(root / "o", OVERLAY_LAYOUT)
        cls.code, cls.err = run_apply(cls.skins, REAL_EXTRAS)
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_native_backgrounds_change_only_the_enabled_tab_image_fields(self):
        self.assertEqual((self.code, self.err), (0, ""))
        root = Path(self._tmp.name)
        empty = generate_skins(root / "without-background", OVERLAY_LAYOUT)
        self.assertEqual(run_apply(empty, extras_with_manifest(root, [])), (0, ""))
        expected = json.loads((empty / "TUI.json").read_text(encoding="utf-8"))
        for view, name, asset in bg_mod.BACKGROUND_TABS:
            tab = next(t for t in expected["pageData"]["tabs"] if t["tabName"] == name)
            for focus in ("focussed", "unfocussed"):
                self.assertEqual(tab["componentDefinition"]["backgroundData"][focus]["image"], "")
                tab["componentDefinition"]["backgroundData"][focus]["image"] = asset
            x, y, rgb = bg_mod.SENTINELS[view]
            with Image.open(REAL_EXTRAS / "overlay-backgrounds" / asset) as source, Image.open(self.skins / asset) as installed:
                source.paste(tuple(bytes.fromhex(rgb)), (x, y, x + 5, y + 5))
                self.assertEqual(installed.size, source.size)
                self.assertIsNone(ImageChops.difference(installed, source).getbbox(), name)
        self.assertEqual(self.tui, expected, "all child definitions, marker geometry, host inputs and other tabs keep the empty-manifest skin")
        self.assertEqual((self.skins / "Q-Links.json").read_bytes(), (empty / "Q-Links.json").read_bytes())


@unittest.skipUnless(HAVE_SDK, "MPC SDK not found (set MPC_SDK)")
class OverlaySkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        root = Path(cls._tmp.name)
        (root / "o").mkdir()
        cls.ovl = generate_skins(root / "o", OVERLAY_LAYOUT)
        cls.ovl_code, cls.ovl_err = run_apply(cls.ovl, REAL_EXTRAS)
        cls.prod = cls.ovl
        cls.prod_code, cls.prod_err = cls.ovl_code, cls.ovl_err
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.o_tui = json.loads((cls.ovl / "TUI.json").read_text(encoding="utf-8"))
        cls.o_ql = json.loads((cls.ovl / "Q-Links.json").read_text(encoding="utf-8"))
        cls.p_tui = cls.o_tui

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
        self.assertEqual((self.ovl_code, self.ovl_err), (0, ""))

    def test_native_controls_precede_page_markers(self):
        prefixes = ("Overlay Marker", "Overlay Instance")
        has_controls = False
        for tab in self.o_tui["pageData"]["tabs"]:
            with self.subTest(tab=tab["tabName"]):
                names = [item["componentData"]["name"]
                         for item in tab["componentDefinition"]["componentsData"]]
                tail = names[-3:]
                self.assertEqual(sum(n.startswith(prefixes[0]) for n in tail), 2)
                self.assertEqual(sum(n.startswith(prefixes[1]) for n in tail), 1)
                self.assertFalse(any(n.startswith(prefixes) for n in names[:-3]))
                has_controls |= len(names) > 3
        self.assertTrue(has_controls, "fixture must exercise tabs with native controls")

    # ---- the overlay skin ----
    def test_24_tabs_and_the_sub_tabs_are_under_the_same_function_key(self):
        tabs = self.o_tui["pageData"]["tabs"]
        self.assertEqual(len(tabs), 15)
        self.assertLessEqual(len(tabs), 32, "the host's limit")
        keys = {}
        for t in sorted(tabs, key=lambda t: (t["fnKeyIndex"], t["fnKeySubIndex"])):
            keys.setdefault(t["fnKeyIndex"], []).append(t["tabName"])
        self.assertEqual([len(v) for _, v in sorted(keys.items())], [2, 5, 1, 1, 1, 1, 1, 1, 2], "9 function keys: Play 2, Osc 5, Env 1, LFO 1, FX 1, Mix 1, Filter 1, Mod 1, Master 2")
        self.assertEqual([keys[i][0] for i in (2, 3, 6)], ["Env", "LFO", "Filter"], "Env, LFO and Filter: ONE tab under their key, the key text is the tab name")
        for gone in ("Env 1", "Env 2", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Filter 1", "Filter 2"):
            self.assertFalse([t for t in tabs if t["tabName"] == gone], gone)
        f = self.tab("Filter")
        self.assertEqual((f["fnKeyIndex"], f["fnKeySubIndex"]), (6, 0))   # the Play key is the first one
        self.assertEqual(len({(t["fnKeyIndex"], t["fnKeySubIndex"]) for t in tabs}), 15)
        for gone in ("F1 Model", "F2 Model", "Mod 3-4", "Mod Amt", "FX Filt", "Distort", "Comp 1", "Comp 2", "EQ", "Chorus", "Delay", "Phaser", "FX Level", "Osc 1", "Osc1 Shp", "Osc 2", "Osc2 Shp", "Osc 3", "Osc3 Shp", "Sub/Nois"):
            self.assertFalse([t for t in tabs if t["tabName"] == gone], gone)

    PAGE_TABS = ("Macros", "Osc A", "Osc B", "Osc C", "Sub", "Noise", "Env", "LFO", "FX", "Mixer", "Filter", "Mod", "Master", "Voice")

    def test_the_page_tabs_are_the_views_of_the_layout(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        overlay = json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        self.assertEqual([v["tab"] for v in layout["views"]], [p["name"] for p in overlay["pages"]], "the views table equals the layout order")
        views = {v["tab"] for v in layout["views"] if v["kind"] in PAGE_KINDS and v["kind"] != "play"}
        self.assertEqual(views, set(self.PAGE_TABS))

    def test_no_host_widget_is_left_on_a_page_tab(self):
        # The release removes fallback widgets, but retains the explicitly
        # migrated native controls.  The full per-tab inventory is covered by
        # test_mpc_skin_no_fallback.py; this gate only rejects an accidental
        # legacy control.
        hide = "RangedEnabling/0/18/33/Parameter %d" % (len(self.params) + 4 + 32 + 2)
        for name in self.PAGE_TABS:
            comps = self.tab(name)["componentDefinition"]["componentsData"]
            self.assertGreaterEqual(len(comps), 3, name)
            for c in comps:
                cname = c["componentData"]["name"]
                native = cname.startswith(("Env 1 ", "Env Native ", "Master ", "Voice ", "Mod ", "Macro ", "Macros ", "FX ", "Mixer ", "Osc Native ", "LFO 1 Native ", "Filter Native ")) \
                    or cname.endswith((" Power", " Smooth Native")) \
                    or c["componentData"]["type"].startswith(("compactNative", "macroCard", "voiceNative", "masterNative", "modNative", "fxNative", "oscNative", "nativeModulationEditor"))
                self.assertTrue(cname.startswith(("Overlay Marker", "Overlay Instance")) or native, "%s on %s is a host widget" % (cname, name))
                gates = c["bounds"].get("additionalInvalidatingHandles", [])
                if cname.startswith("Mixer Native Peek "):
                    self.assertNotIn(hide, gates, cname)
                    continue
                if cname.startswith("Mixer Native ") or c["componentData"]["type"] == "macroCard" \
                        or name == "Voice" and c["componentData"]["type"].startswith("voiceNative"):
                    self.assertIn(hide, gates, cname)
                else:
                    self.assertNotIn(hide, gates, "%s: the plugin finds the page by this picture" % cname)

    def test_the_q_proxies_are_sixteen_and_the_old_indices_did_not_move(self):
        # the FORMAT (docs/MPC.md "Parameter order"): saved projects keep parameter indices; Q 1 .. Q 4 stay at 2703 .. 2706, Q 5 .. Q 16 follow at 2707 .. 2718, count 2719
        self.assertEqual(len(self.params), 2637)
        first_proxy = len(self.params) + 4 + 32 + 2 + 1 + 3 + 24
        self.assertEqual(first_proxy, 2703)
        used = {v for e in self.o_ql["Screen Mode Q-Links"]["map"] for v in e["Q-Links"].values() if v >= first_proxy}
        self.assertEqual(sorted(used), list(range(first_proxy, first_proxy + 16)), "the Q-Link maps use exactly the sixteen Q proxies")
        self.assertEqual(first_proxy + 16, 2719, "the stable prefix ends at the native handle base")

    def test_the_page_tabs_have_the_q_proxies_on_all_sixteen_q_links_in_the_row_direction(self):
        first_proxy = len(self.params) + 4 + 32 + 2 + 1 + 3 + 24   # "Q 1": after the Overlay Instance, the Play selectors and the 24 Play proxies
        for name in self.PAGE_TABS:
            tab = self.tab(name)
            entry = next(e for e in self.o_ql["Screen Mode Q-Links"]["map"] if e["Tab"] == tab["fnKeyIndex"] + 1 and e["SubTab"] == tab["fnKeySubIndex"] + 1)
            self.assertEqual(entry["Bank Direction"], "Row", name)
            self.assertEqual([entry["Q-Links"]["Q-Link %d" % n] for n in range(1, 17)], [first_proxy + i for i in range(16)], name)
            self.assertEqual(tab["qlinkBoundsData"], [], name)

    def test_the_lfo_and_macros_tabs_have_no_pictures_and_no_meters(self):
        definitions = {d["key"]: d["value"] for d in self.o_tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        for name in ("LFO", "Env", "Macros"):
            types = {c["componentData"]["type"] for c in standalone_editor_entries(self.tab(name), definitions)}
            if name == "Env":
                self.assertEqual(types, {"Image", "envNativeTargets", "envNativeDial", "envNativeCurve", "envNativeSwitch",
                                         "envNativeSelectorCell", "nativeModulationEditor"})
            elif name == "LFO":
                self.assertEqual(types, {"Image", "lfoNativeChoice", "lfoNativeToggle", "lfoNativeUni", "lfoNativeBi",
                                         "lfoNativeTargets", "lfoNativeExpand", "lfoNativeDisabled", "lfoNativeNumber",
                                         "Button", "lfoEditAction", "lfoEditOptionalAction", "lfoEditOptionalToggle",
                                         "lfoEditOptionalNumber", "lfoEditOptionalCURVE", "lfoEditOptionalSCALE",
                                         "lfoEditDisabledAction", "lfoDetailRow", "lfoDetailHeader", "nativeModulationEditor"})
            elif name == "Macros":
                self.assertEqual(types, {"Image", "macroCard", "nativeModulationEditor"})
            else:
                self.assertTrue(types <= {"Image", "toggleIcon", "knobUni", "knobBi", "Label"}, name)
            self.assertIn("Image", types, name)

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

    def test_instance_pictures_are_film_strips_with_the_exact_colours_bound_to_the_overlay_instance(self):
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

    def test_no_colour_of_the_rest_of_the_skin_equals_a_marker_colour(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        markers = {int(m["rgb"], 16) for v in layout["views"] for m in v["markers"]}
        markers |= {int(c, 16) for c in layout["instance"]["slotColours"]} | {int(layout["instance"]["none"], 16), int(layout["instance"]["blank"], 16)}   # the instance colours are as unusual
        text = json.dumps(self.o_tui)
        for value in re.findall(r"\"(?:ff)?([0-9a-fA-F]{6})\"", text):
            self.assertNotIn(int(value, 16), markers)
        # the pictures of the skin (except marker/instance pictures): no pixel of a marker colour (within 2 per channel)
        m_tuples = [(((m >> 16) & 255), ((m >> 8) & 255), (m & 255)) for m in markers]
        forbidden = {(r, g, b) for mr, mg, mb in m_tuples for r in range(max(0, mr - 2), min(256, mr + 3)) for g in range(max(0, mg - 2), min(256, mg + 3)) for b in range(max(0, mb - 2), min(256, mb + 3))}
        for png in sorted(self.ovl.glob("*.png")):
            if png.name.startswith(("overlay_marker_", "overlay_instance_")):
                continue
            w, h, rows = decode_png_any(png)
            for row in rows:
                for px in row:
                    if len(px) >= 4 and px[3] == 0:
                        continue
                    if (px[0], px[1], px[2]) in forbidden:
                        self.fail("%s has a pixel %r close to a marker colour" % (png.name, px))

    # ---- the Play page ----
    PLAY = (("Play", "osc", 3),)   # ONE Play tab; the selectors and the proxies of the three groups stay (inert host parameters for the second and third sub-tab)
    SLOTS = {"osc": ("frame", "unison.voices", "unison.detune", "unison.blend", "phase", "random_phase", "pan", "level"),
             "env": ("attack", "hold", "decay", "sustain", "release", "attack_curve", "decay_curve", "release_curve"),
             "lfo": ("rate", "fade_in", "delay", "smooth_time", "smooth", "sync", "sync_type", "division")}

    def play_first(self):
        return len(self.params) + 4 + 32 + 2 + 1   # "Overlay Instance" is before: Play Osc, Play Env, Play LFO, then the 24 proxies (docs/MPC.md "Parameter order")

    def test_play_tabs_are_the_first_function_key(self):
        tabs = self.o_tui["pageData"]["tabs"]
        self.assertEqual([t["tabName"] for t in tabs[:2]], ["Play", "Macros"])
        self.assertEqual([(t["fnKeyIndex"], t["fnKeySubIndex"]) for t in tabs[:2]], [(0, 0), (0, 1)])
        self.assertEqual(tabs[2]["tabName"], "Osc")   # Osc is the second function key
        self.assertEqual(tabs[2]["fnKeyIndex"], 1)
        self.assertEqual(len({t["fnKeyIndex"] for t in tabs}), 9, "nine function keys")

    def test_function_key_labels_are_the_short_names_and_the_other_sub_tabs_keep_theirs(self):
        # the host draws the tabName of the current sub-tab on the function key: the first sub-tab of every key carries the short label
        tabs = self.o_tui["pageData"]["tabs"]
        first = [t["tabName"] for t in tabs if t["fnKeySubIndex"] == 0]
        self.assertEqual(first, ["Play", "Osc", "Env", "LFO", "FX", "Mix", "Filter", "Mod", "Master"])
        later = [t["tabName"] for t in tabs if t["fnKeySubIndex"] > 0]
        self.assertEqual(later, ["Macros", "Osc B", "Osc C", "Sub", "Noise", "Voice"])
        names = [t["tabName"] for t in tabs]
        self.assertEqual(len(names), len(set(names)), "the tab names of the finished skin are unique")
        self.assertTrue(all(1 <= len(n) <= 8 for n in names))
        # the Q-Link map follows the keys, not the names
        ql = self.o_ql["Screen Mode Q-Links"]["map"]
        self.assertEqual([(e["Tab"], e["SubTab"]) for e in ql], [(t["fnKeyIndex"] + 1, t["fnKeySubIndex"] + 1) for t in tabs])
        # the internal names stay in skin-meta.json, labels only for the pages that have one
        meta = json.loads((self.ovl.parent / "skin-meta.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["labels"], self.LABELS)
        self.assertEqual(sorted(meta["labels"].values()), sorted(["Osc", "Mix"]))

    def test_a_label_that_clashes_or_is_too_long_stops_the_generator(self):
        import test_gen_mpc_ui_layout as lay
        base = json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        for page, label in (("Osc A", "Osc B"), ("Env", "Env 1 long"), ("Osc A", "Mix"), ("Env", "")):
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
        q = [first + 3 + 24 + i for i in range(16)]
        for g, (tab_name, prefix, _) in enumerate(self.PLAY):
            tab = self.tab(tab_name)
            items = tab["componentDefinition"]["componentsData"]
            bound = lambda c: int(c["handle remapping"]["map"][0]["value"].split()[1]) if c["handle remapping"]["map"] else None
            slots = [c for c in items if c["componentData"]["type"] in SLOT_TYPES]
            # The graph and Q controls are plugin-owned; the three compact
            # instance selectors remain named host component instances so the
            # normal Play page has a real native touch owner.
            sub_level = mixer_peek_index(len(self.params)) + PLAY_OSC + 7
            expected_slots = sel + [prox[1][0], prox[1][2], prox[1][3], prox[1][4],
                                    prox[2][1], prox[2][3], prox[2][4], sub_level]
            self.assertEqual(sorted(set(bound(c) for c in slots)), sorted(expected_slots),
                             f"{tab_name}: each selector and compact dial uses its own proxy")
            kinds = {bound(c): c["componentData"]["type"] for c in slots}
            # every host widget of the Env and LFO thirds is hidden while the plugin's full screen is open: the "Overlay Instance" frame 24 is outside RangedEnabling 0 .. 18 of the 33 frames
            hide = "RangedEnabling/0/18/33/Parameter %d" % (first - 1)
            for c in slots:
                self.assertIn(hide, c["bounds"].get("additionalInvalidatingHandles", []), "%s: every selector and knob of the Play pages hides while a full screen is open" % tab_name)
                self.assertEqual(c["bounds"]["version"], 2)
            self.assertEqual(set(kinds.values()), {"playOscSelector", "playModSelector", "compactNativeDial", "compactNativeRiseDial",
                                "compactNativeSmoothTime", "compactNativeSmoothToggle", "playSubLevel"})
            dial = next(c for c in slots if bound(c) == prox[1][3])
            self.assertEqual(rect(dial), (446, 510, 192, 108))
            rise = next(c for c in slots if bound(c) == prox[2][1])
            self.assertEqual(rect(rise), (864, 546, 196, 72))
            self.assertEqual(rise["componentData"]["type"], "compactNativeRiseDial")
            dial_def = next(d["value"] for d in self.o_tui["pageData"]["componentDefinitions"]["localComponentDefinitions"] if d["key"] == "compactNativeDial")
            self.assertEqual([c["componentData"]["name"] for c in dial_def["componentsData"]], ["focus", "knob", "name", "value"])
            for state in ("focussed", "unfocussed"):
                self.assertTrue(dial_def["backgroundData"][state]["colour"].startswith("ff"))
            self.assertIn({"key": "Caption", "value": "WithName[Sustain]/Parameter %d" % prox[1][3]}, dial["handle remapping"]["map"])
            self.assertIn({"key": "Caption", "value": "WithName[Rise]/Parameter %d" % prox[2][1]}, rise["handle remapping"]["map"])
            replacements = {r["key"]: r["value"] for r in self.o_tui["pageData"].get("replacementStrings", [])}
            self.assertEqual(replacements["Parameter %d" % sel[0]], ["Sub", "Osc A", "Osc B", "Osc C", "Noise"], "the OSC native selector has a non-empty MenuOverlay choice list")
            self.assertEqual(replacements["Parameter %d" % sel[1]], ["Env %d" % n for n in range(1, 7)], "the ENV native selector has six choices")
            self.assertEqual(replacements["Parameter %d" % sel[2]], ["LFO %d" % n for n in range(1, 11)], "the LFO native selector has ten choices")
            view = next(v for v in json.loads(SHARED_JSON.read_text(encoding="utf-8"))["views"] if v["tab"] == tab_name)
            for p, i in enumerate(sel):
                c = next(c for c in slots if bound(c) == i)
                x, y, w, h = rect(c)
                expected = ((14, 10, 408, 56), (446, 10, 388, 56), (864, 10, 396, 56))[p]
                self.assertEqual((x, y, w, h), expected, "the selector %d occupies the current Play header" % p)
                self.assertTrue(x > 39 or x + w <= 34, "the selector %d does not cover the left detector marker" % p)
                self.assertTrue(x > 1247 or x + w <= 1242, "the selector %d does not cover the right detector marker" % p)
            # the knobs are copies of the control of instance 1 on its own tab (same widget), in the lower two rows, slot n = Q-Link n (Q-Link 1 .. 4 are the Q proxies, see below)
            for s, key in enumerate(self.SLOTS[prefix] if g > 2 else ()):
                c = next(c for c in slots if bound(c) == prox[g][s])
                x, y, w, h = rect(c)
                self.assertEqual(x, (21, 341, 661, 981)[s % 4])
                self.assertTrue(y >= 343 and y + h <= 621, "%s inside the lower two rows" % c["componentData"]["name"])
                self.assertEqual(y < 483, s >= 4, "slots 1 .. 4 are the bottom row, 5 .. 8 the row above")
                real = next((t for t in self.o_tui["pageData"]["tabs"] for rc in t["componentDefinition"]["componentsData"]
                             if rc["handle remapping"]["map"] and bound(rc) == next(i for i, q_ in enumerate(self.params) if q_["key"] == "%s1.%s" % (prefix, key))), None)
                if real is not None:
                    rc = next(rc for rc in real["componentDefinition"]["componentsData"] if rc["handle remapping"]["map"] and bound(rc) == next(i for i, q_ in enumerate(self.params) if q_["key"] == "%s1.%s" % (prefix, key)))
                    self.assertEqual(c["componentData"]["type"], rc["componentData"]["type"], "%s is the same widget as the real parameter's control" % c["componentData"]["name"])
            entry = next(e for e in self.o_ql["Screen Mode Q-Links"]["map"] if e["Tab"] == tab["fnKeyIndex"] + 1 and e["SubTab"] == tab["fnKeySubIndex"] + 1)
            expected = [-1] * 16
            # the plugin-drawn panels: Bank Direction Row, Q-Link n = Q n for all 16 (the host's own numbering: Q-Link 1 .. 4 the bottom row, the active group); any other sub-tab would use Bank Direction Column, bank 0 read top to bottom: Q-Link 13, 9, 5, 1 = Q 1 .. Q 4
            self.assertEqual(entry["Bank Direction"], "Row" if g < 3 else "Column")
            for k, n in enumerate(tuple(range(1, 17)) if g < 3 else (13, 9, 5, 1)):
                expected[n - 1] = q[k]
            if g > 2:
                for k, n in enumerate((14, 10, 6, 2)):   # bank 1 top to bottom: the second row of the module's knobs
                    expected[n - 1] = prox[g][4 + k]
            self.assertEqual([entry["Q-Links"]["Q-Link %d" % n] for n in range(1, 17)], expected,
                             "%s: the first bank (Q-Link 13, 9, 5, 1 top to bottom; Q-Link 1 .. 4 with Bank Direction Row on the oscillator sub-tab) is Q 1 .. Q 4, the second (14, 10, 6, 2) the second row of the module's knobs (none on the oscillator sub-tab), the others free" % tab_name)
            self.assertEqual(tab["qlinkBoundsData"], [])
            # names: every knob has its short name over it, unique inside the tab
            names = [c["componentData"]["data"]["text"] for c in items if c["componentData"]["type"] == "Label" and c["componentData"]["name"].endswith(" name")]
            self.assertEqual(len(names), {"osc": 0, "env": 0, "lfo": 0}[prefix], "a name over every knob and toggle (the selectors, three of the LFO's eight controls, show the host's own header)")
            self.assertEqual(len(set(names)), len(names))

    def test_the_three_thirds_have_no_host_widget(self):
        for tab_name, _, _ in self.PLAY:
            items = {c["componentData"]["name"] for c in self.tab(tab_name)["componentDefinition"]["componentsData"]}
            for gone in ("Play Panel 1", "Play Title 1", "Play Panel 2", "Play Title 2", "Play Panel 3", "Play Title 3"):
                self.assertNotIn(gone, items, "%s: %s is under a plugin-drawn view" % (tab_name, gone))
            self.assertEqual({n for n in items if n.endswith(" Select")}, {"Play Osc Select", "Play Env Select", "Play LFO Select"})
            self.assertNotIn("Play Background", items, "%s: no host fallback picture" % tab_name)
            self.assertEqual({n for n in items if n.startswith(("Play Env ", "Play LFO ", "Play Lfo ")) and not n.endswith(" Select")},
                             {"Play Env Attack", "Play Env Decay", "Play Env Sustain", "Play Env Release",
                              "Play LFO Rise", "Play LFO Smooth", "Play LFO Smooth Off"})

    def test_compact_play_selectors_and_lfo_smooth_gate_matrix(self):
        first = self.play_first()
        items = {c["componentData"]["name"]: c for c in self.tab("Play")["componentDefinition"]["componentsData"]}
        defs = {d["key"]: d["value"] for d in self.o_tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}
        param = {p["key"]: i for i, p in enumerate(self.params)}
        osc_sources = ("sub", "osc1", "osc2", "osc3", "noise")
        osc_power = [param[prefix + ".on"] for prefix in osc_sources]
        smooth_names = ("Play LFO Rise", "Play LFO Smooth", "Play LFO Smooth Off")
        lfo_layers = {name: items[name] for name in smooth_names}
        lfo_standard = mixer_peek_index(len(self.params)) + PLAY_LFO_STANDARD
        lfo_smooth = first + 3 + 2 * 8 + 4

        def visible(component, values):
            bounds = component["bounds"]
            self.assertEqual(bounds["showWhenDataModelInvalid"], "Hide")
            for gate in bounds["additionalInvalidatingHandles"]:
                op, *states, source = gate.split("/")
                lo, hi, steps = (int(states[0]), int(states[0]), int(states[1])) if op == "IndexedEnabling" else map(int, states)
                self.assertIn(op, ("IndexedEnabling", "RangedEnabling"))
                frame = round(values[int(source.split()[1])] * (steps - 1))
                if not lo <= frame <= hi:
                    return False
            return True

        osc_selector = next(c for c in defs["playOscSelector"]["componentsData"] if c["componentData"]["name"] == "Play selector frame")
        _, _, rows = decode_png(self.ovl / osc_selector["componentData"]["data"]["image"])
        self.assertTrue(all(r == g == b for row in rows for r, g, b, *_ in row))
        self.assertEqual(items["Play Osc Select"]["bounds"]["additionalInvalidatingHandles"],
                         [f"RangedEnabling/0/18/33/Parameter {first - 1}"])

        for group, label in (("osc", "Osc"), ("env", "Env"), ("lfo", "LFO")):
            popup_name = "oscNativeSourceChoices" if group == "osc" else "fx editor choice menu"
            selector = defs[items[f"Play {label} Select"]["componentData"]["type"]]
            actions = [a for a in selector["actions"] if a["handler"] == "Show Overlay"]
            self.assertCountEqual([(a["onAction"], a["additionalData"], a["handleName"]) for a in actions],
                                  [(trigger, popup_name, "Data") for trigger in ("Mouse Down", "Enter Pressed")])
            self.assertNotIn("Slider", [c["componentData"]["type"] for c in selector["componentsData"]])
            menu = defs[popup_name]["componentsData"]
            if group == "osc":
                for source, power in enumerate(osc_power):
                    for on, status in enumerate(("OFF", "ON")):
                        label = next(c for c in menu if c["componentData"]["name"] == f"Osc source power {source} {status}")
                        self.assertEqual(label["bounds"]["additionalInvalidatingHandles"], [f"IndexedEnabling/{on}/2/Parameter {power}"])
            else:
                chooser, = menu[:-1]
                self.assertEqual(chooser["componentData"]["type"], "MenuOverlay")
                self.assertEqual(rect(chooser), (0, 0, 1280, 800))
                self.assertEqual(chooser["componentData"]["data"]["handleName"], "Data")
                self.assertTrue(any(a["onAction"] == "Focus Lost" for a in chooser["componentData"]["data"]["actions"]))
            self.assertFalse(list(self.ovl.glob("play_choice_*.png")))
            self.assertEqual(rect(menu[-1]), (16, 120, 30, 6))
            self.assertEqual(menu[-1]["componentData"]["data"]["image"], "native_modal_markers.png")

        for frame in (1, 24):
            for standard in (0, 1):
                for smooth in (0, 1):
                    state = {first - 1: frame / 32, lfo_standard: standard, lfo_smooth: smooth}
                    want = {smooth_names[0], smooth_names[1] if smooth else smooth_names[2]} if standard and frame == 1 else set()
                    self.assertEqual({name for name, comp in lfo_layers.items() if visible(comp, state)}, want)
        self.assertNotIn("Play LFO Smooth Switch", items)
        for name, focus in (("Play LFO Smooth", "Yes"), ("Play LFO Smooth Off", "No")):
            layer = lfo_layers[name]
            self.assertEqual(rect(layer), (1064, 546, 196, 72))
            self.assertEqual(layer["bounds"]["acceptsHWFocus"], focus)
            handles = {m["key"]: m["value"] for m in layer["handle remapping"]["map"]}
            self.assertEqual(handles["Active"], f"Parameter {lfo_smooth}")
            definition = defs[layer["componentData"]["type"]]
            caption = next(c["componentData"] for c in definition["componentsData"] if c["componentData"]["name"] == "name")
            self.assertEqual((caption["type"], caption["data"]["handleName"]), ("Button", "Active"))

    def test_there_is_no_page_only_the_overlay_can_reach_so_the_play_page_carries_the_full_screen_ui(self):
        # the format: every tab sits on a function key and is reached by the key (ui-reference "fnKeyIndex" / "fnKeySubIndex"); a plugin has no call that selects a tab (docs/MPC.md "Q proxies").
        names = [t["tabName"] for t in self.o_tui["pageData"]["tabs"]]
        self.assertFalse([n for n in names if "Full" in n], "no blank page of its own: the full-screen page is drawn over the Play page")
        self.assertEqual(len(names), 15, "the tab count of the overlay skin")

    def test_play_views_markers_and_no_fallback_picture(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        views = [v for v in layout["views"] if v["kind"] == "play"]
        self.assertEqual([v["tab"] for v in views], ["Play"])
        self.assertEqual([v["index"] for v in views], [1], "the one Play view")
        for v in views:
            self.assertEqual(v["region"], {"x": 23, "y": 14, "w": 1234, "h": 190})
            items = {c["componentData"]["name"]: c for c in self.tab(v["tab"])["componentDefinition"]["componentsData"]}
            self.assertIn("Overlay Marker %s left" % v["name"], items)
            self.assertIn("Overlay Marker %s right" % v["name"], items)
            self.assertNotIn("Play Background", items, "no dark fallback picture under the plane (97_no_fallback.py)")
            self.assertFalse([n for n in items if n.startswith("Play Title ")], "no title of a panel: the three panels are the plugin's own drawing")

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
