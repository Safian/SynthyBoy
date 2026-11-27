#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/99_backgrounds.py (pre-rendered host page backgrounds).
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
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent.parent
EXTRAS_DIR = ROOT / "scripts" / "mpc-skin-extras"
BACKGROUNDS_MOD_PATH = EXTRAS_DIR / "99_backgrounds.py"
ASSETS_DIR = EXTRAS_DIR / "overlay-backgrounds"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


bg_mod = load_module("t_99_backgrounds", BACKGROUNDS_MOD_PATH)
MACROS_MANIFEST = ROOT / "shell-mpc" / "overlay" / "tests" / "fixtures" / "backgrounds_manifest_macros.json"   # the Macros pilot state
MACROS_ON = bg_mod.load_manifest(MACROS_MANIFEST)


class macros_state:
    """Exercise the Macros pilot manifest."""
    def __enter__(self):
        self.saved = bg_mod.BACKGROUND_TABS
        bg_mod.BACKGROUND_TABS = MACROS_ON

    def __exit__(self, *a):
        bg_mod.BACKGROUND_TABS = self.saved


class MockCtx:
    def __init__(self, meta, param_count=2637, skins_dir=None):
        self.meta = meta
        self.param_count = param_count
        self.skins_dir = Path(skins_dir) if skins_dir else None
        self._tabs = {}

    def tab(self, title):
        if title not in self._tabs:
            raise KeyError("Tab %r not found" % title)
        return self._tabs[title]

    def add_mock_tab(self, name, items=None):
        t = {
            "version": 1,
            "tabName": name,
            "fnKeyIndex": 0,
            "fnKeySubIndex": 0,
            "componentDefinition": {
                "componentsData": items if items is not None else [],
                "backgroundData": {
                    "version": 1,
                    "focussed": {"version": 1, "colour": "ff282828", "image": ""},
                    "unfocussed": {"version": 1, "colour": "ff1e1d1e", "image": ""},
                },
            },
        }
        self._tabs[name] = t
        return t


class MpcBackgroundsTests(unittest.TestCase):
    def test_the_committed_manifest_and_header_agree(self):
        names = json.loads((ASSETS_DIR / "manifest.json").read_text(encoding="utf-8"))["tabs"]
        self.assertEqual([t[0] for t in bg_mod.BACKGROUND_TABS], names)
        self.assertTrue({"macros", "play1", "mixer", "fx", "master", "voice", "filter1"} <= set(names))
        header = (ROOT / "shell-mpc/overlay/backgrounds_manifest.h").read_text(encoding="utf-8")
        self.assertIn(f"kNumBackgroundTabs = {len(names)};", header)

    def test_empty_manifest_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = MockCtx(meta={"variant": "overlay"}, param_count=2637, skins_dir=tmp)
            macros_tab = ctx.add_mock_tab("Macros", [{"componentData": {"name": "Native Knob"}}])
            saved = bg_mod.BACKGROUND_TABS
            bg_mod.BACKGROUND_TABS = []   # the empty-manifest state (the committed manifest lists macros since 2026-11-21)
            try:
                bg_mod.apply({"pageData": {"tabs": [macros_tab]}}, {}, ctx)
            finally:
                bg_mod.BACKGROUND_TABS = saved
            self.assertEqual([c["componentData"]["name"] for c in macros_tab["componentDefinition"]["componentsData"]], ["Native Knob"])
            self.assertEqual(list(Path(tmp).iterdir()), [], "no asset is copied")

    def test_the_macros_state_header_fixture_is_current(self):
        r = subprocess.run([sys.executable, str(ROOT / "scripts" / "gen-backgrounds-manifest.py"), "--manifest", str(MACROS_MANIFEST),
                            "--out", str(ROOT / "shell-mpc" / "overlay" / "tests" / "fixtures" / "backgrounds_manifest_macros.h"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual([t[0] for t in MACROS_ON], ["macros"])

    def test_backgrounds_are_fixed_plain_canvas(self):
        from components import style
        self.assertEqual(bg_mod.PAGE_COLOUR, "ff" + style.color("surface.canvas").lstrip("#").lower())
        self.assertEqual({filename for _, _, filename in bg_mod.BACKGROUND_TABS}, {""})

    def test_inactive_on_production_skin(self):
        ctx = MockCtx(meta=None, param_count=2637)
        macros_tab = ctx.add_mock_tab("Macros", [{"componentData": {"name": "Native Knob"}}])
        filter_tab = ctx.add_mock_tab("Filter 1", [{"componentData": {"name": "Cutoff Knob"}}])
        tui = {"pageData": {"tabs": [macros_tab, filter_tab]}}
        qlinks = {}

        bg_mod.apply(tui, qlinks, ctx)

        self.assertEqual(len(macros_tab["componentDefinition"]["componentsData"]), 1)
        self.assertEqual(macros_tab["componentDefinition"]["componentsData"][0]["componentData"]["name"], "Native Knob")
        self.assertEqual(len(filter_tab["componentDefinition"]["componentsData"]), 1)

    def test_inactive_when_variant_is_not_overlay(self):
        ctx = MockCtx(meta={"variant": "production"}, param_count=2637)
        macros_tab = ctx.add_mock_tab("Macros", [{"componentData": {"name": "Native Knob"}}])
        tui = {"pageData": {"tabs": [macros_tab]}}
        qlinks = {}

        bg_mod.apply(tui, qlinks, ctx)
        self.assertEqual(len(macros_tab["componentDefinition"]["componentsData"]), 1)

    def test_macros_root_image_both_focus_states_preserve_children_and_fallback(self):
        with macros_state(), tempfile.TemporaryDirectory() as tmp:
            ctx = MockCtx(meta={"variant": "overlay"}, skins_dir=tmp)
            markers = [
                {"componentData": {"name": "Overlay Marker macros left"}, "bounds": {"bounds": "33 24 6 6"}},
                {"componentData": {"name": "Overlay Marker macros right"}, "bounds": {"bounds": "1241 24 6 6"}},
                {"componentData": {"name": "Overlay Instance macros"}, "bounds": {"bounds": "45 24 6 6"}},
            ]
            tab = ctx.add_mock_tab("Macros", markers)
            mac = tab["componentDefinition"]
            fallback = copy.deepcopy(mac["backgroundData"])
            other = ctx.add_mock_tab("Other", [{"componentData": {"name": "Native Knob"}}])
            before_other = copy.deepcopy(other)
            qlinks = {"Page": [{"mapping": "Q 1"}]}
            before_qlinks = copy.deepcopy(qlinks)
            tui = {"pageData": {"tabs": [tab, other]}}
            for _ in range(2):
                bg_mod.apply(tui, qlinks, ctx)
                self.assertEqual([c for c in mac["componentsData"] if not c["componentData"]["name"].startswith("Overlay Background Sentinel ")], [c for c in markers if not c["componentData"]["name"].startswith("Overlay Background Sentinel ")])
                for state in ("focussed", "unfocussed"):
                    self.assertEqual(mac["backgroundData"][state], dict(fallback[state], colour="ff090d19", image=""))
                self.assertEqual(other, before_other)
                self.assertEqual(qlinks, before_qlinks)
                self.assertFalse((Path(tmp) / "overlay_canvas.png").exists())
                x, y, rgb = bg_mod.SENTINELS["macros"]
                with Image.open(Path(tmp) / "sentinel_macros.png") as marker:
                    self.assertEqual(marker.getcolors(), [(25, tuple(bytes.fromhex(rgb)))])

    def test_the_known_backgrounds(self):
        self.assertEqual(sorted(bg_mod.KNOWN), ["env1", "env2", "env3", "env4", "env5", "env6", "filter1", "filter2", "filternote", "filtervelo", "fx", "lfo1", "lfo10", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "lfo7", "lfo8", "lfo9", "macros", "master", "mixer", "mod", "osca", "oscb", "oscc", "oscnoise", "oscsub", "play1", "rnd1", "rnd2", "rnd3", "rnd4", "tuning", "voice"])

        with self.assertRaises(ValueError):
            bg_mod.load_manifest(ROOT / "shell-mpc" / "overlay" / "tests" / "fixtures" / "backgrounds_manifest_macros.h")   # not JSON


    def test_manifest_generator_agreement(self):
        gen_script = ROOT / "scripts" / "gen-backgrounds-manifest.py"
        manifest_h = ROOT / "shell-mpc" / "overlay" / "backgrounds_manifest.h"
        self.assertTrue(gen_script.is_file())
        self.assertTrue(manifest_h.is_file())
        r = subprocess.run([sys.executable, str(gen_script), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, f"Manifest check failed: {r.stderr}")
        header_text = (ROOT / "shell-mpc" / "overlay" / "tests" / "fixtures" / "backgrounds_manifest_macros.h").read_text(encoding="utf-8")
        for view_name, tab_name, asset_name in MACROS_ON:
            self.assertIn(f'"{view_name}", "{tab_name}", "{asset_name}"', header_text)

    def test_sentinel_is_a_flat_known_colour_in_a_free_spot(self):
        # the plugin keeps its plane opaque until it reads this square on the host's framebuffer (host_background.h): the colour must be the PNG's, flat, different from the host's own tab colour 1e1d1e,
        # and away from what the host or the plugin draws on top of the skin
        layout = json.loads((ROOT / "shell-mpc" / "overlay" / "overlay_layout.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as tmp:
            ctx = MockCtx(meta={"variant": "overlay"}, param_count=2637, skins_dir=tmp)
            for _, name, _ in bg_mod.BACKGROUND_TABS:
                ctx.add_mock_tab(name)
            bg_mod.apply({}, {}, ctx)
            assets = {}
            for view, _, asset in bg_mod.BACKGROUND_TABS:
                assets[view] = Image.new("RGB", (1280, 628), (9, 13, 25))   # the solid page colour
                with Image.open(Path(tmp) / ("sentinel_" + view + ".png")) as marker:
                    assets[view].paste(marker, bg_mod.SENTINELS[view][:2])
        for view_name, tab_name, asset_name in bg_mod.BACKGROUND_TABS:
            view = next(v for v in layout["views"] if v["name"] == view_name)
            x, y, rgb = bg_mod.SENTINELS[view_name]
            want = tuple(int(rgb[i:i + 2], 16) for i in (0, 2, 4))
            seen = {assets[view_name].getpixel((x + dx, y + dy)) for dx in range(5) for dy in range(5)}
            samples = ((1, 1), (3, 1), (1, 3), (3, 3), (2, 2))
            for other, image in assets.items():
                if bg_mod.SENTINELS[other] == bg_mod.SENTINELS[view_name]:
                    continue
                matches = all(max(abs(a - b) for a, b in zip(image.getpixel((x + dx, y + dy)), want)) <= 2 for dx, dy in samples)
                self.assertFalse(matches, f"{view_name} accepted stale image {other}")
            self.assertTrue(all(max(abs(a - b) for a, b in zip(px, want)) <= 1 for px in seen), f"{view_name}: the 5 x 5 square at ({x}, {y}) is flat {rgb}, seen {seen}")
            for host_colour in ((0x1e, 0x1d, 0x1e), (0x28, 0x28, 0x28)):
                self.assertGreater(max(abs(a - b) for a, b in zip(want, host_colour)), 8, "distinct from both focus states")
            self.assertGreaterEqual(x, 12, "not under the host's left-edge handle tab (x 0 .. 9)")
            self.assertLessEqual(x + 5, 1020, "not under the host's Q-Link panel (x >= 1024)")
            spots = [view["markers"][0], view["instance"], view["markers"][1]]
            for s in spots:
                self.assertFalse(x < s["x"] + s["w"] + 1 and s["x"] - 1 < x + 5 and y < s["y"] + s["h"] + 1 and s["y"] - 1 < y + 5, f"{view_name}: not on a marker spot")

    def test_all_five_osc_tabs_share_a_single_reproducible_ground(self):
        views = {"osca", "oscb", "oscc", "oscsub", "oscnoise"}
        entries = [(view, asset) for view, _, asset in bg_mod.BACKGROUND_TABS if view in views]
        self.assertEqual({view for view, _ in entries}, views)
        self.assertEqual({asset for _, asset in entries}, {""})
        x, y, rgb = bg_mod.OSC_SENTINEL
        expect = tuple(bytes.fromhex(rgb))
        self.assertEqual({asset for _, _, asset in bg_mod.BACKGROUND_TABS}, {bg_mod.CANVAS_ASSET})


    def test_manifest_has_the_sentinel(self):
        text = (ROOT / "shell-mpc" / "overlay" / "tests" / "fixtures" / "backgrounds_manifest_macros.h").read_text(encoding="utf-8")   # the [macros] state
        for view_name, tab_name, asset_name in MACROS_ON:
            x, y, rgb = bg_mod.SENTINELS[view_name]
            self.assertIn(f'"{asset_name}", {x}, {y}, 0x{rgb}u', text)

class PageTitleAndMarkerTests(unittest.TestCase):
    def test_every_page_title_caption_uses_the_one_page_title_size(self):
        captions = json.loads((EXTRAS_DIR / "static-native-captions.json").read_text(encoding="utf-8"))
        titles = [captions[k][0] for k in ("voice", "master_header", "tuning_header", "mod", "filter1", "filter2",
                                            "osca", "oscb", "oscc", "oscsub", "oscnoise", "filternote", "filtervelo", *("env%d" % n for n in range(1, 7)))]
        self.assertTrue(all(t["size"] == "font.page_title" and t["colour"] == "text.value" for t in titles), titles)
        sys.path.insert(0, str(ROOT))
        from components import style
        self.assertEqual(style.size("font.page_title"), 26)
        # The host draws a TUI font height as ascent + descent (1.521 em): every title row must be tall enough for that height, no vertical clipping.
        sys.path.insert(0, str(ROOT / "scripts"))
        from native_static_captions import host_font_height
        height = host_font_height("font.page_title")
        self.assertEqual(height, round(26 * style.size("font.host.height.per.em")))
        self.assertGreaterEqual(height, 39)
        for t in titles:
            self.assertGreaterEqual(t["rect"][3], height, t)
        self.assertEqual(host_font_height(13), 13)

    def test_every_lfo_and_rnd_tab_names_its_instance_on_the_host_layer(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        sys.path.insert(0, str(ROOT))
        from native_static_captions import title_entry
        entry = title_entry("t", "RND 3", bg_mod.LFO_TITLE_RECT)
        self.assertEqual(entry["componentData"]["data"]["text"], "RND 3")
        self.assertEqual(entry["componentData"]["data"]["textStyle"]["font"]["height"], float(round(26 * 1.521)))
        self.assertGreaterEqual(bg_mod.LFO_TITLE_RECT[3], 40)
        source = BACKGROUNDS_MOD_PATH.read_text(encoding="utf-8")
        self.assertIn('view[:3].upper() + " " + view[3:]', source)
        self.assertLess(source.index("children.append(sentinel)"), source.index("bg = root"))

    def test_mixer_and_fx_markers_lie_in_the_flat_canvas_strip_above_the_page_content(self):
        views = {v["name"]: v for v in json.loads((ROOT / "shell-mpc/overlay/overlay_layout.json").read_text())["views"]}
        for name in ("mixer", "fx"):
            v = views[name]
            for spot in (*v["markers"], v["instance"]):
                self.assertLess(spot["y"] + spot["h"], 10, name)   # the strips / buttons start at tab y 10: a cover patch there is not visible


if __name__ == "__main__":
    unittest.main()
