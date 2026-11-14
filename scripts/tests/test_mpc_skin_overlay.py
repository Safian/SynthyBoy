#!/usr/bin/env python3
# SynthyBoy - tests of the overlay skin variant (scripts/overlay-skin/mpc-ui-pages-overlay.json + scripts/mpc-skin-extras/50_filter_overlay.py) and of the numbers it shares with the plugin
# (shell-mpc/overlay/overlay_layout.json -> overlay_layout.h): marker pictures with the exact colours and positions, the controls, the production skin unchanged.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

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
    """The views of the generated header: (name, kind, index, region, opaque, [(x, y, w, h, rgb, cover)] * 2), screen coordinates."""
    text = SHARED_HEADER.read_text(encoding="utf-8")
    out = []
    for m in re.finditer(r'\{ "(\w+)", (\d+), (\d+), \{ (\d+), (\d+), (\d+), (\d+) \}, (true|false), \{ (.*?) \} \},', text):
        markers = [tuple(int(v) for v in mm.groups()[:4]) + (int(mm.group(5), 16), int(mm.group(6), 16))
                   for mm in re.finditer(r"\{ \{ (\d+), (\d+), (\d+), (\d+) \}, 0x([0-9a-f]{6}), 0x([0-9a-f]{6}) \}", m.group(9))]
        out.append((m.group(1), int(m.group(2)), int(m.group(3)), tuple(int(v) for v in m.groups()[3:7]), m.group(8) == "true", markers))
    origin = int(re.search(r"kTabOriginY = (\d+);", text).group(1))
    bg = int(re.search(r"kBackgroundRgb = 0x([0-9a-f]{6});", text).group(1), 16)
    return out, origin, bg


KINDS = {"filter": 0, "lfo": 1, "osc": 2, "env": 3}


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
        for (name, kind, index, region, opaque, markers), v in zip(views, layout["views"]):
            r = v["region"]
            self.assertEqual((name, kind, index, opaque), (v["name"], KINDS[v["kind"]], v["index"], v["opaque"]))
            self.assertEqual(region, (r["x"], r["y"] + origin, r["w"], r["h"]), "screen coordinates = tab coordinates + the origin of the tab")
            self.assertEqual(markers, [(m["x"], m["y"] + origin, m["w"], m["h"], int(m["rgb"], 16), int(m["cover"], 16)) for m in v["markers"]])

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
            self.assertLess(left["x"] - c["x"], 30, "the left marker is near the left end of the region")
            self.assertLess(c["x"] + c["w"] - right["x"] - right["w"], 30, "the right marker is near the right end")
            self.assertNotEqual(layout["background"], left["rgb"])
        # no colour is within the detector's tolerance (2 per channel, so 4 apart can still be told apart: need > 4) of another view's colour
        for i, (na, ca) in enumerate(every):
            for nb, cb in every[i + 1:]:
                near = all(abs(((ca >> s) & 255) - ((cb >> s) & 255)) <= 4 for s in (0, 8, 16))
                self.assertFalse(near, "%s and %s have marker colours that the detector could confuse (%06x %06x)" % (na, nb, ca, cb))
        names = [v["name"] for v in layout["views"]]
        self.assertEqual(len(set(names)), len(names))
        tabs = [v["tab"] for v in layout["views"]]
        self.assertEqual(len(set(tabs)), len(tabs), "one view per tab")


class OverlayLayoutFileTests(unittest.TestCase):
    def test_layout_is_the_production_one_plus_two_pages(self):
        prod, ovl = json.loads(PROD_LAYOUT.read_text(encoding="utf-8")), json.loads(OVERLAY_LAYOUT.read_text(encoding="utf-8"))
        self.assertEqual(ovl["variant"], "overlay")
        self.assertNotIn("variant", prod)
        self.assertEqual((len(prod["pages"]), len(ovl["pages"]), ovl["tabLimit"]), (34, 36, 36))
        names = [p["name"] for p in ovl["pages"]]
        self.assertEqual(names[names.index("Filter 1"):names.index("Filter 1") + 4], ["Filter 1", "F1 Model", "Filter 2", "F2 Model"])
        rest = [p for p in ovl["pages"] if p["name"] not in ("Filter 1", "F1 Model", "Filter 2", "F2 Model")]
        self.assertEqual(rest, [p for p in prod["pages"] if p["name"] not in ("Filter 1", "Filter 2")], "every other page is the production page")
        for n in (1, 2):
            pages = [p for p in ovl["pages"] if p["name"] in ("Filter %d" % n, "F%d Model" % n)]
            self.assertEqual({p["fn"] for p in pages}, {"Filter"})
            self.assertEqual(sorted(k for p in pages for k in p["kick"]), ["filter%d_main" % n, "filter%d_model" % n])
            self.assertEqual(sorted(k for p in prod["pages"] if p["name"] == "Filter %d" % n for k in p["kick"]),
                             ["filter%d_main" % n, "filter%d_model" % n])


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
        cls.base = generate_skins(root / "q", PROD_LAYOUT)
        cls.base_code, cls.base_err = run_apply(cls.base, cls.extras_without)
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.o_tui = json.loads((cls.ovl / "TUI.json").read_text(encoding="utf-8"))
        cls.o_ql = json.loads((cls.ovl / "Q-Links.json").read_text(encoding="utf-8"))
        cls.p_tui = json.loads((cls.prod / "TUI.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, name, tui=None):
        return next(t for t in (tui or self.o_tui)["pageData"]["tabs"] if t["tabName"] == name)

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
        files = lambda d: sorted(p.name for p in d.iterdir())
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
    def test_36_tabs_and_the_sub_tabs_are_under_the_same_function_key(self):
        tabs = self.o_tui["pageData"]["tabs"]
        self.assertEqual(len(tabs), 36)
        f1, m1, f2, m2 = self.tab("Filter 1"), self.tab("F1 Model"), self.tab("Filter 2"), self.tab("F2 Model")
        self.assertEqual((f1["fnKeyIndex"], m1["fnKeyIndex"], f2["fnKeyIndex"], m2["fnKeyIndex"]), (3, 3, 3, 3))
        self.assertEqual((f1["fnKeySubIndex"], m1["fnKeySubIndex"], f2["fnKeySubIndex"], m2["fnKeySubIndex"]), (0, 1, 2, 3))
        self.assertEqual(len({(t["fnKeyIndex"], t["fnKeySubIndex"]) for t in tabs}), 36)

    def rows_of(self, n):
        return [["filter%d.on" % n, "filter%d.model" % n, "filter%d.style" % n, "filter%d.drive" % n],
                ["filter%d.cutoff" % n, "filter%d.resonance" % n, "filter%d.mix" % n, "filter%d.keytrack" % n]]

    def test_filter_tabs_have_the_two_rows_of_controls(self):
        for n in (1, 2):
            f = self.tab("Filter %d" % n)
            got = {k: rect(c) for k, c in self.controls(f).items()}
            expected_order = self.rows_of(n)
            self.assertEqual(sorted(got), sorted(k for row in expected_order for k in row))
            for r, row in enumerate(expected_order):
                ys = {got[k][1] + got[k][3] // 2 for k in row}
                xs = [got[k][0] for k in row]
                self.assertEqual(xs, sorted(xs), row)
                self.assertEqual(xs, [21, 341, 661, 981], "slots on the axes of the four columns")
                self.assertLessEqual(max(ys) - min(ys), 40, "one row")
                if r:
                    self.assertGreater(min(ys), max({got[k][1] + got[k][3] // 2 for k in expected_order[0]}) + 100, "row 4 is below row 3")
            # the curve area is above the controls and the Q-Link frames start under it
            layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
            c = next(v for v in layout["views"] if v["name"] == "filter%d" % n)["region"]
            self.assertLessEqual(c["y"] + c["h"], 301)
            self.assertEqual(f["qlinkBoundsData"], ["16 301 288 323", "336 301 288 323", "656 301 288 323", "976 301 288 323"])
            self.assertTrue(all(y >= 301 for _, y, _, _ in got.values()))

    def test_qlink_maps_match_the_two_rows(self):
        idx = {p["key"]: i for i, p in enumerate(self.params)}
        for n in (1, 2):
            f = self.tab("Filter %d" % n)
            entry = next(e for e in self.o_ql["Screen Mode Q-Links"]["map"] if e["Tab"] == f["fnKeyIndex"] + 1 and e["SubTab"] == f["fnKeySubIndex"] + 1)
            want = {}
            for r, row in enumerate(self.rows_of(n)):
                for col, key in enumerate(row):
                    want["Q-Link %d" % (4 * (3 - (2 + r)) + col + 1)] = idx[key]   # Bank Direction Column: the lower row is Q-Link 1 .. 4
            links = entry["Q-Links"]
            self.assertEqual({k: v for k, v in links.items() if v != -1}, want)
            self.assertEqual(links["Q-Link 1"], idx["filter%d.cutoff" % n])

    def test_parameters_come_from_params_json_not_from_numbers(self):
        for n in (1, 2):
            f = self.controls(self.tab("Filter %d" % n))
            self.assertEqual(f["filter%d.cutoff" % n]["handle remapping"]["map"][0]["value"], "Parameter %d" % next(i for i, p in enumerate(self.params) if p["key"] == "filter%d.cutoff" % n))

    def test_every_control_of_the_production_filter_tabs_is_still_reachable(self):
        for n in (1, 2):
            old = set(self.controls(self.tab("Filter %d" % n, self.p_tui)))
            new_main, new_model = set(self.controls(self.tab("Filter %d" % n))), set(self.controls(self.tab("F%d Model" % n)))
            self.assertEqual(sorted(old - (new_main | new_model)), [], "filter %d" % n)
            self.assertEqual(sorted(new_main & new_model), [])
            self.assertEqual(sorted(new_model), sorted("filter%d.%s" % (n, k) for k in ("blend", "comb_offset", "formant.x", "formant.y", "formant.resonance", "formant.spread", "formant.transpose")))
            # and in the Q-Link map of the sub-tab
            m = self.tab("F%d Model" % n)
            entry = next(e for e in self.o_ql["Screen Mode Q-Links"]["map"] if e["Tab"] == m["fnKeyIndex"] + 1 and e["SubTab"] == m["fnKeySubIndex"] + 1)
            idx = {p["key"]: i for i, p in enumerate(self.params)}
            self.assertEqual(sorted(v for v in entry["Q-Links"].values() if v != -1), sorted(idx[k] for k in new_model))

    def test_all_other_tabs_are_the_production_tabs_plus_the_marker_pictures(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        with_markers = {v["tab"] for v in layout["views"]}
        for t in self.p_tui["pageData"]["tabs"]:
            if t["tabName"] in ("Filter 1", "Filter 2"):
                continue
            o = self.tab(t["tabName"])
            ot = {k: v for k, v in o.items() if k not in ("fnKeySubIndex",)}
            pt = {k: v for k, v in t.items() if k not in ("fnKeySubIndex",)}
            if t["tabName"] in with_markers:
                comps = ot["componentDefinition"]["componentsData"]
                self.assertEqual([c["componentData"]["name"] for c in comps[-2:]], ["Overlay Marker %s %s" % (next(v["name"] for v in layout["views"] if v["tab"] == t["tabName"]), s) for s in ("left", "right")])
                ot = json.loads(json.dumps(ot))
                ot["componentDefinition"]["componentsData"] = comps[:-2]
            self.assertEqual(ot, pt, t["tabName"])

    def test_the_pictures_of_the_lfo_and_osc_tabs_stay_under_the_views(self):
        for name in ("LFO 1", "LFO 4", "Osc 1", "Osc 3"):
            comps = self.tab(name)["componentDefinition"]["componentsData"]
            pictures = [c for c in comps if c["componentData"]["type"] == "Image" and not c["componentData"]["name"].startswith("Overlay Marker")]
            self.assertGreaterEqual(len(pictures), 10, name)
            order = [c["componentData"]["name"] for c in comps]
            for c in pictures:
                self.assertLess(order.index(c["componentData"]["name"]), order.index("Overlay Marker %s left" % name.lower().replace(" ", "")), "the markers are above the pictures")

    def test_names_are_there(self):
        for n in (1, 2):
            f = self.tab("Filter %d" % n)
            texts = {c["componentData"]["name"] for c in f["componentDefinition"]["componentsData"] if c["componentData"]["type"] == "Label"}
            for key in ("Cutoff", "Resonance", "Key Tracking", "On", "Drive", "Mix"):
                self.assertIn("Filter %d %s name" % (n, key), texts)

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

    def test_no_colour_of_the_rest_of_the_skin_equals_a_marker_colour(self):
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        markers = {int(m["rgb"], 16) for v in layout["views"] for m in v["markers"]}
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

    def test_validate_py(self):
        res = subprocess.run([sys.executable, "-I", str(SDK / "tools" / "validate.py"), str(self.ovl.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual([e for e in errors if "tabs; at most 32" not in e], [], report)
        self.assertEqual([e for e in errors if "tabs; at most 32" in e], ["ERROR  TUI.json: 36 tabs; at most 32 are supported"], report)
        layout = json.loads(SHARED_JSON.read_text(encoding="utf-8"))
        names = ["overlay_curve_bg.png"] + ["overlay_marker_%s_%s.png" % (v["name"], s) for v in layout["views"] for s in ("left", "right")]
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
