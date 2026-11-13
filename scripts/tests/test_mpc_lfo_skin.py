#!/usr/bin/env python3
# SynthyBoy - tests of scripts/gen-mpc-lfo-pictures.py and scripts/mpc-skin-extras/30_lfo.py (the LFO tabs: large shape picture + live cursor strip).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, ROOT, SDK, generate_skins, qlinks_by_tab_name, run_apply

GEN_PATH = ROOT / "scripts" / "gen-mpc-lfo-pictures.py"
COMMITTED = REAL_EXTRAS / "lfo-pictures"
HAVE_CXX = shutil.which("c++") is not None
MAX_DECODED = 32 * 1000 * 1000     # docs/MPC.md "LFO pictures": the budget of the two layers
PICTURE = (340, 30, 600, 240)


def have_render_modules():
    try:
        import numpy, PIL  # noqa: F401
        return True
    except ImportError:
        return False


def load_gen():
    spec = importlib.util.spec_from_file_location("t_gen_mpc_lfo_pictures", GEN_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def png_size(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    return struct.unpack(">II", data[16:24])


@unittest.skipUnless(have_render_modules(), "numpy and Pillow not installed for this python3 (gen-mpc-lfo-pictures.py is a development tool)")
class PictureGeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import numpy as np
        from PIL import Image
        cls.np, cls.Image = np, Image
        cls.gen = load_gen()
        cls.shapes = cls.gen.dump_engine_shapes() if HAVE_CXX else None

    def image(self, name):
        return self.Image.open(COMMITTED / name).convert("RGBA")

    def test_files_and_dimensions(self):
        gen = self.gen
        names = sorted(p.name for p in COMMITTED.iterdir())
        self.assertEqual(names, sorted([gen.shape_name(i) for i in range(13)] + [gen.CURSOR_NAME]))
        self.assertEqual((gen.WIDTH, gen.HEIGHT, gen.NUM_FRAMES, gen.CURSOR_STEPS), (600, 240, 33, 32))
        for i in range(13):
            self.assertEqual(png_size(COMMITTED / gen.shape_name(i)), (600, 240))
        self.assertEqual(png_size(COMMITTED / gen.CURSOR_NAME), (600, 240 * 33))

    def test_decoded_size_is_under_32_mb(self):
        total = sum(w * h * 4 for w, h in (png_size(p) for p in COMMITTED.iterdir()))
        self.assertEqual(total, (13 + 33) * 600 * 240 * 4)
        self.assertEqual(total, self.gen.decoded_bytes())
        self.assertLess(total, MAX_DECODED)

    def test_cursor_steps_match_the_plugin_header(self):
        header = (ROOT / "shell-mpc" / "lfo_live.h").read_text(encoding="utf-8")
        self.assertIn("kLfoLiveSteps = %d;" % self.gen.CURSOR_STEPS, header)

    def frames(self):
        strip = self.np.asarray(self.image(self.gen.CURSOR_NAME))
        h = self.gen.HEIGHT
        return [strip[i * h:(i + 1) * h] for i in range(self.gen.NUM_FRAMES)]

    def test_cursor_strip_is_transparent_and_frame_0_is_empty(self):
        frames = self.frames()
        self.assertEqual(int(frames[0][..., 3].max()), 0)
        for k in range(1, 33):
            alpha = frames[k][..., 3]
            self.assertGreater(int(alpha.max()), 200, k)
            self.assertLess(int((alpha > 0).sum()), 0.02 * alpha.size, "frame %d is not just a line" % k)   # a line, not a picture
        self.assertTrue((frames[1][..., 3] == 0).any() and (frames[1][..., 3] > 0).any())

    def test_cursor_is_one_vertical_line_in_its_cell(self):
        gen, np = self.gen, self.np
        for k, frame in enumerate(self.frames()):
            if k == 0:
                continue
            ys, xs = np.nonzero(frame[..., 3] > 16)
            centre = gen.cell_centre(k)
            self.assertLess(abs(xs.mean() + 0.5 - centre), 0.6, k)
            self.assertLessEqual(xs.max() - xs.min(), gen.CURSOR_WIDTH + 1, k)               # thin: a vertical line
            self.assertGreaterEqual(ys.max() - ys.min(), gen.PLOT_BOTTOM - gen.PLOT_TOP, k)  # over the full height of the plot
            cell = (gen.PLOT_X1 - gen.PLOT_X0) / gen.CURSOR_STEPS
            self.assertTrue(gen.PLOT_X0 + (k - 1) * cell <= xs.mean() <= gen.PLOT_X0 + k * cell, k)   # inside cell k - 1 of the cycle

    def test_cursor_frames_distinct(self):
        frames = [f.tobytes() for f in self.frames()]
        self.assertEqual(len(set(frames)), 33)

    def test_cursor_colour_is_light_on_the_edge_pixels_too(self):
        # no dark fringe: where the line is partly transparent its colour is still the light line colour
        for frame in self.frames()[1:4]:
            part = frame[(frame[..., 3] > 0) & (frame[..., 3] < 255)]
            self.assertTrue(len(part) > 0)
            self.assertGreater(int(part[:, :3].min()), 200)

    def test_shape_pictures_are_opaque_distinct_and_have_a_curve(self):
        np = self.np
        pics = [np.asarray(self.image(self.gen.shape_name(i))) for i in range(13)]
        self.assertEqual(len({p.tobytes() for p in pics}), 13)
        for i, p in enumerate(pics):
            self.assertEqual(int(p[..., 3].min()), 255, i)
            red = (p[..., 0] > 190) & (p[..., 1] < 60)
            if i < 12:
                self.assertGreater(int(red.sum()), 1000, i)
            else:
                rows = np.nonzero(red.any(axis=1))[0]     # the CUSTOM marker is red text in the middle band, no curve across the plot
                self.assertTrue(len(rows) > 0 and rows.min() >= 240 // 2 - 56 and rows.max() <= 240 // 2 + 16, (rows.min(), rows.max()))

    @unittest.skipUnless(HAVE_CXX, "no C++ compiler (c++)")
    def test_curve_follows_the_engine_values_at_the_cell_centres(self):
        gen, np = self.gen, self.np
        for shape in (0, 2, 6, 11):
            p = np.asarray(self.image(gen.shape_name(shape)))
            red = (p[..., 0] > 190) & (p[..., 1] < 60) & (p[..., 2] < 120)
            for k in (1, 8, 16, 25, 32):
                pos = (k - 0.5) / gen.CURSOR_STEPS
                v = self.shapes[shape][int(pos * gen.SAMPLES)]
                x = int(gen.cell_centre(k))
                ys = np.nonzero(red[:, x])[0]
                self.assertTrue(len(ys) > 0, (shape, k))
                self.assertTrue(any(abs(y + 0.5 - gen.y_of(v)) < 4 for y in ys), (shape, k, gen.y_of(v), ys.min(), ys.max()))

    @unittest.skipUnless(HAVE_CXX, "no C++ compiler (c++)")
    def test_engine_values(self):
        # the table comes from the engine's lfoShape(): spot checks of the documented shapes
        s, n = self.shapes, self.gen.SAMPLES
        self.assertEqual(sorted(s), list(range(12)))
        self.assertAlmostEqual(s[0][0], -1.0, places=4)                  # sine starts at the bottom
        self.assertAlmostEqual(s[0][n // 2], 1.0, places=4)
        self.assertAlmostEqual(s[1][n // 2], 1.0, places=4)              # triangle peak
        self.assertAlmostEqual(s[2][0], -1.0, places=4)                  # saw up
        self.assertAlmostEqual(s[3][0], 1.0, places=4)                   # saw down
        self.assertEqual({round(v, 3) for v in s[4]}, {1.0, -1.0})       # square
        self.assertEqual(sum(1 for v in s[5] if v > 0), n // 4)          # pulse 25 %
        self.assertEqual(len({round(v, 3) for v in s[6]}), 4)            # 4 steps up
        self.assertEqual(len({round(v, 3) for v in s[8]}), 8)            # random steps: the 8 fixed levels
        self.assertAlmostEqual(s[11][0], -1.0, places=4)                 # half sine

    def test_names_are_the_shape_choices(self):
        # the caption of picture i is the name of choice i of lfoN.shape: the picture of a choice is the one the skin shows for it
        self.assertEqual(len(self.gen.shape_choices()), 13)
        self.assertEqual(self.gen.shape_choices()[12], "Custom")

    @unittest.skipUnless(HAVE_CXX, "no C++ compiler (c++)")
    def test_deterministic_and_committed_assets_are_the_rendering_of_the_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = self.gen.generate(Path(tmp) / "a", shapes=self.shapes)
            self.assertEqual(sorted(p.name for p in COMMITTED.iterdir()), sorted(p.name for p in paths))
            for p in paths:
                self.assertEqual((COMMITTED / p.name).read_bytes(), p.read_bytes(), "%s is stale: run python3 scripts/gen-mpc-lfo-pictures.py" % p.name)

    @unittest.skipUnless(HAVE_CXX, "no C++ compiler (c++)")
    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            res = subprocess.run([sys.executable, str(GEN_PATH), "--out", str(Path(tmp) / "cli")], capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual((Path(tmp) / "cli" / "lfo_shape_03.png").read_bytes(), (COMMITTED / "lfo_shape_03.png").read_bytes())
            self.assertIn("decoded size: 26496000 bytes", res.stdout)

    def test_missing_compiler_is_a_clear_error(self):
        with self.assertRaises(self.gen.GenError) as cm:
            self.gen.dump_engine_shapes("no-such-compiler-xyz")
        self.assertIn("no-such-compiler-xyz", str(cm.exception))


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class LfoSkinModuleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls._tmp.name)
        cls.skins = generate_skins(cls.base)
        cls.orig = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.orig_q = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"]
        code, err = run_apply(cls.skins, REAL_EXTRAS)
        assert (code, err) == (0, ""), err
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qmap = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))["Screen Mode Q-Links"]["map"]
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, title):
        return next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == title)

    def entries(self, title):
        return self.tab(title)["componentDefinition"]["componentsData"]

    def index(self, key):
        return next(i for i, p in enumerate(self.params) if p["key"] == key)

    @staticmethod
    def rect(c):
        return tuple(int(v) for v in c["bounds"]["bounds"].split())

    def test_four_lfo_tabs_are_sub_tabs_of_one_key_and_the_pair_tabs_are_gone(self):
        names = [t["tabName"] for t in self.tui["pageData"]["tabs"]]
        for n in (1, 2, 3, 4):
            self.assertIn("LFO %d" % n, names)
        self.assertNotIn("LFO 1-2", names)
        self.assertNotIn("LFO 3-4", names)
        self.assertEqual({(self.tab("LFO %d" % n)["fnKeyIndex"], self.tab("LFO %d" % n)["fnKeySubIndex"]) for n in (1, 2, 3, 4)},
                         {(self.tab("LFO 1")["fnKeyIndex"], i) for i in range(4)})

    def test_shape_pictures_one_per_choice_valid_for_that_choice(self):
        for n in (1, 2, 3, 4):
            by_name = {c["componentData"]["name"]: c for c in self.entries("LFO %d" % n)}
            shape = self.index("lfo%d.shape" % n)
            for choice in range(13):
                pic = by_name["LFO %d Picture %s" % (n, "Custom" if choice == 12 else choice)]
                data = pic["componentData"]["data"]
                self.assertEqual(pic["componentData"]["type"], "Image")
                self.assertEqual((data["imageType"], data["image"], data["colour"]), ("Regular", "lfo_shape_%02d.png" % choice, "0"))
                self.assertNotIn("numFrames", data)
                self.assertEqual(pic["handle remapping"]["map"], [])
                self.assertEqual(pic["bounds"]["version"], 2)
                self.assertEqual(pic["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/%d/13/Parameter %d" % (choice, shape)])
                self.assertEqual(self.rect(pic), PICTURE)
            self.assertEqual(sum(1 for k in by_name if k.startswith("LFO %d Picture" % n)), 13)

    def test_one_cursor_strip_on_top_bound_to_the_display_parameter(self):
        count = len(self.params)
        files = set()
        for n in (1, 2, 3, 4):
            items = self.entries("LFO %d" % n)
            cursor = items[-1]                                    # the last element is the topmost
            self.assertEqual(cursor["componentData"]["name"], "LFO %d Cursor" % n)
            data = cursor["componentData"]["data"]
            self.assertEqual((cursor["componentData"]["type"], data["imageType"], data["numFrames"], data["handleName"]), ("Image", "FilmStrip", 33, "Data"))
            self.assertEqual(cursor["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % (count + n - 1)}])
            self.assertEqual(self.rect(cursor), PICTURE)
            self.assertEqual(cursor["bounds"]["version"], 1)      # shown for every shape
            files.add(data["image"])
            pictures = [c for c in items if c["componentData"]["name"].startswith("LFO %d Picture" % n)]
            self.assertTrue(all(items.index(p) < items.index(cursor) for p in pictures))
        self.assertEqual(files, {"lfo_cursor.png"})              # ONE file for the four LFOs and every shape

    def test_display_parameters_come_after_the_engine_parameters(self):
        # the cursor parameters are added by the shell after the list of docs/params.json: no engine parameter index moves
        keys = [p["key"] for p in self.params]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertFalse(any("cursor" in k.lower() for k in keys))
        shell = (ROOT / "shell-mpc" / "synthyboy_mpc.cpp").read_text(encoding="utf-8")
        self.assertIn('"LFO %d Cursor"', shell)

    def test_eight_controls_in_rows_3_and_4_with_the_screen_rows_as_q_links(self):
        order = ("shape", "sync", "rate", "division", "sync_type", "phase", "fade_in", "delay")
        by_tab = qlinks_by_tab_name(self.tui, self.qmap)
        for n in (1, 2, 3, 4):
            tab = self.tab("LFO %d" % n)
            controls = [c for c in tab["componentDefinition"]["componentsData"] if c["componentData"]["type"] not in ("Image", "Label")]
            self.assertEqual(tab["qlinkBoundsData"], ["%s 301 288 323" % x for x in ("16", "336", "656", "976")])
            expected = {"Q-Link %d" % i: -1 for i in range(1, 17)}
            for c in controls:
                x, y = self.rect(c)[:2]
                row, col = {343: 2, 362: 2, 483: 3, 502: 3}[y], {21: 0, 341: 1, 661: 2, 981: 3}[x]
                param = int(c["handle remapping"]["map"][0]["value"].split()[1])
                expected["Q-Link %d" % (4 * (3 - row) + col + 1)] = param
                self.assertEqual(self.rect(c)[3] in (100, 138), True)
            self.assertEqual(by_tab[tab["tabName"]], expected)
            self.assertEqual([self.params[v]["key"] for v in (expected["Q-Link %d" % (4 * (3 - r) + c + 1)] for r in (2, 3) for c in range(4))],
                             ["lfo%d.%s" % (n, k) for k in order])

    def test_rate_is_valid_for_free_and_division_for_the_three_tempo_modes(self):
        sync_choices = next(p for p in self.params if p["key"] == "lfo1.sync")["choices"]
        self.assertEqual(sync_choices, ["Free (Hz)", "Tempo", "Tempo dotted", "Tempo triplet", "Key follow"])
        # the engine's meaning (core/engine-own/env_lfo/lfo.h lfoRateHz): 0 free Hz, 1 tempo, 2 dotted, 3 triplet, 4 key follow
        header = (ROOT / "core" / "engine-own" / "env_lfo" / "lfo.h").read_text(encoding="utf-8")
        self.assertIn("0 free, 1 tempo, 2 dotted, 3 triplet, 4 key follow", header)
        for n in (1, 2, 3, 4):
            sync = self.index("lfo%d.sync" % n)
            items = self.entries("LFO %d" % n)
            rate = [c for c in items if c["handle remapping"]["map"] and c["handle remapping"]["map"][0]["value"] == "Parameter %d" % self.index("lfo%d.rate" % n)
                    and c["componentData"]["type"] != "Label"]
            self.assertEqual(len(rate), 1)
            self.assertEqual(rate[0]["bounds"]["version"], 2)
            self.assertEqual(rate[0]["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/0/5/Parameter %d" % sync])
            division = [c for c in items if c["handle remapping"]["map"] and c["handle remapping"]["map"][0]["value"] == "Parameter %d" % self.index("lfo%d.division" % n)
                        and c["componentData"]["type"] != "Label"]
            self.assertEqual(sorted(c["bounds"]["additionalInvalidatingHandles"][0] for c in division),
                             ["IndexedEnabling/%d/5/Parameter %d" % (s, sync) for s in (1, 2, 3)])
            self.assertEqual(len({self.rect(c) for c in division}), 1)            # the three copies are one place
            self.assertEqual(len({c["componentData"]["name"] for c in division}), 3)
            for c in division:
                self.assertEqual(c["bounds"]["version"], 2)
            # no sync value shows both, key follow shows neither
            shown = {s: [c for c in rate + division if "IndexedEnabling/%d/5/" % s in c["bounds"]["additionalInvalidatingHandles"][0]] for s in range(5)}
            self.assertEqual([len(shown[s]) for s in range(5)], [1, 1, 1, 1, 0])
            for s in range(5):   # the name labels have the same visibility as their controls
                labels = [c for c in items if c["componentData"]["type"] == "Label" and c["bounds"].get("additionalInvalidatingHandles") == ["IndexedEnabling/%d/5/Parameter %d" % (s, sync)]]
                # (only the knob has a name label: the selectors keep the host header)
                self.assertEqual(len(labels), len([c for c in shown[s] if c["componentData"]["type"] == "knobUni"]))

    def test_other_tabs_are_not_touched_by_this_module(self):
        # the Q-Link maps of every tab that is not an LFO tab are those of the generator, by tab name
        before = qlinks_by_tab_name(self.orig, self.orig_q)
        after = qlinks_by_tab_name(self.tui, self.qmap)
        for name in before:
            if not name.startswith(("LFO", "Env", "Osc ")) or name.endswith("Shp"):
                self.assertEqual(after[name], before[name], name)

    def test_files_are_in_the_skin_and_the_decoded_size_is_inside_the_budget(self):
        total = 0
        for i in range(13):
            w, h = png_size(self.skins / ("lfo_shape_%02d.png" % i))
            self.assertEqual((w, h), (600, 240))
            total += w * h * 4
        w, h = png_size(self.skins / "lfo_cursor.png")
        self.assertEqual((w, h), (600, 240 * 33))
        total += w * h * 4
        self.assertEqual(total, 26496000)
        self.assertLess(total, MAX_DECODED)
        self.assertFalse(any(p.name.startswith("lfo_live_") for p in self.skins.iterdir()))   # the old 208 x 38 strips are gone

    def test_decoded_size_of_the_whole_skin_is_the_documented_one(self):
        total = sum(w * h * 4 for w, h in (png_size(p) for p in self.skins.glob("*.png")))
        self.assertLessEqual(total, 105709408)
        doc = (ROOT / "docs" / "MPC.md").read_text(encoding="utf-8")
        self.assertIn("105 709 408", doc)     # the number of the documentation: it must follow the skin (the two spectrum pictures, 32 640 bytes, are in it)
        self.assertEqual(total, 105709408)

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, str(SDK / "tools" / "validate.py"), str(self.skins.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l and "34 tabs; at most 32" not in l]
        self.assertEqual(errors, [], report)
        self.assertIn("OK     TUI.json: image lfo_cursor.png present", report)
        self.assertIn("OK     TUI.json: image lfo_shape_12.png present", report)


if __name__ == "__main__":
    unittest.main()
