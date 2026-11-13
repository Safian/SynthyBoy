#!/usr/bin/env python3
# SynthyBoy - tests of scripts/gen-mpc-lfo-strips.py and scripts/mpc-skin-extras/30_lfo.py (LFO pictures with live cursor of the MPC skin).
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
import zlib
from pathlib import Path

from test_mpc_skin_extras import PARAMS_JSON, REAL_EXTRAS, ROOT, SDK, generate_skins, run_apply, snapshot

GEN_PATH = ROOT / "scripts" / "gen-mpc-lfo-strips.py"
HAVE_CXX = shutil.which("c++") is not None


def load_gen():
    spec = importlib.util.spec_from_file_location("t_gen_mpc_lfo_strips", GEN_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        tag, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 4
    rows = [raw[i * stride + 1:(i + 1) * stride] for i in range(height)]
    assert all(raw[i * stride] == 0 for i in range(height))
    return width, height, rows


gen = load_gen()


@unittest.skipUnless(HAVE_CXX, "no C++ compiler (c++)")
class LfoStripTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls._tmp.name)
        cls.paths = gen.generate(cls.dir / "a")
        cls.shapes = gen.dump_engine_shapes()

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def strip(self, shape):
        w, h, rows = read_png(self.dir / "a" / gen.strip_name(shape))
        self.assertEqual((w, h), (gen.FRAME_W, gen.FRAME_H * gen.NUM_FRAMES))
        return [rows[i * gen.FRAME_H:(i + 1) * gen.FRAME_H] for i in range(gen.NUM_FRAMES)]

    def test_files_and_dimensions(self):
        self.assertEqual(sorted(p.name for p in self.paths), sorted([gen.strip_name(i) for i in range(12)] + ["lfo_custom.png"]))
        self.assertEqual((gen.FRAME_W, gen.FRAME_H, gen.NUM_FRAMES), (208, 38, 33))
        w, h, _ = read_png(self.dir / "a" / "lfo_custom.png")
        self.assertEqual((w, h), (208, 38))

    def test_decoded_size_is_under_16_mb(self):
        total = 12 * gen.NUM_FRAMES * gen.FRAME_W * gen.FRAME_H * 4
        self.assertLess(total, 16 * 1000 * 1000)
        self.assertEqual(total, 12519936)

    def test_cursor_steps_match_the_plugin_header(self):
        header = (ROOT / "shell-mpc" / "lfo_live.h").read_text(encoding="utf-8")
        self.assertIn("kLfoLiveSteps = %d;" % gen.CURSOR_STEPS, header)

    def test_deterministic(self):
        again = gen.generate(self.dir / "b")
        for a, b in zip(sorted(self.paths), sorted(again)):
            self.assertEqual(a.name, b.name)
            self.assertEqual(a.read_bytes(), b.read_bytes())

    def test_engine_values(self):
        # the table comes from the engine's lfoShape(): spot checks of the documented shapes
        s, n = self.shapes, gen.SAMPLES
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

    @staticmethod
    def is_cursor(px):
        return px[0] > 0xc0 and px[1] > 0xc0 and px[2] > 0xc0

    @staticmethod
    def is_curve(px):
        return px[0] > 0x90 and px[1] < 0x60

    def pixels(self, frame, pred):
        return [(x, y) for y, row in enumerate(frame) for x, px in ((i // 4, row[i:i + 4]) for i in range(0, len(row), 4)) if pred(px)]

    def test_frame_0_has_curve_and_no_cursor(self):
        for shape in range(12):
            frame = self.strip(shape)[0]
            self.assertTrue(self.pixels(frame, self.is_curve), shape)
            self.assertEqual(self.pixels(frame, self.is_cursor), [], shape)

    def test_cursor_is_in_its_cell_and_on_the_curve(self):
        # cell k (frame k + 1) of the cycle: the cursor dot lies at the x of the cell centre and at the engine's value there
        for shape in (0, 2, 6):
            frames = self.strip(shape)
            for k in (0, 7, 15, 24, 31):
                dots = self.pixels(frames[k + 1], self.is_cursor)
                self.assertTrue(dots, (shape, k))
                xs = [x for x, _ in dots]
                centre = gen.PLOT_X0 + (k + 0.5) / gen.CURSOR_STEPS * (gen.PLOT_X1 - gen.PLOT_X0)
                self.assertLess(abs(sum(xs) / len(xs) - centre), 1.5, (shape, k))
                p = (k + 0.5) / gen.CURSOR_STEPS
                f = p * gen.SAMPLES - 0.5
                v = self.shapes[shape][int(f)] + (self.shapes[shape][int(f) + 1] - self.shapes[shape][int(f)]) * (f - int(f))
                want_y = gen.PLOT_Y_TOP + (1 - v) * 0.5 * (gen.PLOT_Y_BOT - gen.PLOT_Y_TOP)
                # the dot is the widest part of the cursor: its mean y over the pixels of the dot columns around the centre
                near = [y for x, y in dots if abs(x - centre) < 2.5]
                self.assertTrue(any(abs(y - want_y) < 2.5 for y in near), (shape, k))

    def test_frames_distinct(self):
        frames = [b"".join(f) for f in self.strip(0)]
        self.assertEqual(len(set(frames)), gen.NUM_FRAMES)
        shapes = [b"".join(self.strip(s)[0]) for s in range(12)]
        self.assertEqual(len(set(shapes)), 12)

    def test_custom_picture(self):
        _, _, rows = read_png(self.dir / "a" / "lfo_custom.png")
        self.assertEqual(self.pixels(rows, self.is_curve), [])
        self.assertTrue(any(row[i:i + 3] == bytes(gen.TEXT) for row in rows for i in range(0, len(row), 4)))

    def test_missing_compiler_is_a_clear_error(self):
        with self.assertRaises(gen.GenError) as cm:
            gen.dump_engine_shapes("no-such-compiler-xyz")
        self.assertIn("no-such-compiler-xyz", str(cm.exception))

    def test_committed_assets_are_the_rendering_of_the_engine(self):
        # the build copies scripts/mpc-skin-extras/lfo-strips (no compiler); they must be exactly what the generator renders now
        committed = ROOT / "scripts" / "mpc-skin-extras" / "lfo-strips"
        self.assertEqual(sorted(p.name for p in committed.iterdir()), sorted(p.name for p in self.paths))
        for p in self.paths:
            self.assertEqual((committed / p.name).read_bytes(), p.read_bytes(), "%s is stale: run python3 scripts/gen-mpc-lfo-strips.py" % p.name)

    def test_cli(self):
        out = self.dir / "cli"
        res = subprocess.run([sys.executable, str(GEN_PATH), "--out", str(out)], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual((out / "lfo_live_03.png").read_bytes(), (self.dir / "a" / "lfo_live_03.png").read_bytes())


@unittest.skipUnless((SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class LfoSkinModuleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls._tmp.name)
        cls.skins = generate_skins(cls.base)
        cls.before = snapshot(cls.skins)
        # the module alone (a symlink: the module finds its assets relative to its resolved path), to see what only it changes
        only = cls.base / "only-lfo"
        only.mkdir()
        (only / "30_lfo.py").symlink_to(REAL_EXTRAS / "30_lfo.py")
        (cls.base / "alone").mkdir()
        cls.alone = generate_skins(cls.base / "alone")
        code, err = run_apply(cls.alone, only)
        assert (code, err) == (0, ""), err
        cls.tui = json.loads((cls.alone / "TUI.json").read_text(encoding="utf-8"))
        cls.orig = json.loads(cls.before["TUI.json"].decode("utf-8"))
        # all the real modules together: the LFO tabs must carry the same pictures there
        (cls.base / "full").mkdir()
        cls.full = generate_skins(cls.base / "full")
        code, err = run_apply(cls.full, REAL_EXTRAS)
        assert (code, err) == (0, ""), err
        cls.tui_full = json.loads((cls.full / "TUI.json").read_text(encoding="utf-8"))
        cls.params = json.loads(PARAMS_JSON.read_text(encoding="utf-8"))["params"]

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, tui, title):
        return next(t for t in tui["pageData"]["tabs"] if t["tabName"] == title)["componentDefinition"]["componentsData"]

    def index(self, key):
        return next(i for i, p in enumerate(self.params) if p["key"] == key)

    def test_one_element_per_shape_choice_bound_to_the_cursor_parameter(self):
        count = len(self.params)
        for title, lfos in (("LFO 1-2", (1, 2)), ("LFO 3-4", (3, 4))):
            entries = self.tab(self.tui, title)
            by_name = {c["componentData"]["name"]: c for c in entries}
            for n in lfos:
                shape = self.index("lfo%d.shape" % n)
                for choice in range(12):
                    pic = by_name["LFO %d Picture %d" % (n, choice)]
                    data = pic["componentData"]["data"]
                    self.assertEqual(pic["componentData"]["type"], "Image")
                    self.assertEqual((data["imageType"], data["image"], data["numFrames"], data["handleName"]),
                                     ("FilmStrip", "lfo_live_%02d.png" % choice, 33, "Data"))
                    self.assertEqual(pic["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % (count + n - 1)}])
                    self.assertEqual(pic["bounds"]["version"], 2)
                    self.assertEqual(pic["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/%d/13/Parameter %d" % (choice, shape)])
                custom = by_name["LFO %d Picture Custom" % n]
                self.assertEqual(custom["componentData"]["data"]["imageType"], "Regular")
                self.assertEqual(custom["bounds"]["additionalInvalidatingHandles"], ["IndexedEnabling/12/13/Parameter %d" % shape])
                self.assertEqual(sum(1 for k in by_name if k.startswith("LFO %d Picture" % n)), 13)

    def test_position_clear_of_selector_and_the_q_link_badge(self):
        for title, lfos in (("LFO 1-2", (1, 2)), ("LFO 3-4", (3, 4))):
            entries = self.tab(self.tui, title)
            rects = {c["componentData"]["name"]: tuple(int(v) for v in c["bounds"]["bounds"].split()) for c in entries}
            for n in lfos:
                sx, sy, _, _ = rects["LFO %d Shape" % n]
                pics = {r for k, r in rects.items() if k.startswith("LFO %d Picture" % n)}
                self.assertEqual(len(pics), 1)  # all 13 are one place: the picture is one element at a time
                x, y, w, h = pics.pop()
                self.assertEqual((w, h), (208, 38))
                self.assertGreaterEqual(x, 86)
                self.assertLessEqual(x + w, 296)                    # inside the first column cell, clear of the Q-Link frame
                self.assertEqual(y + h + 2, sy)
                for name, (ox, oy, ow, oh) in rects.items():
                    if not name.startswith("LFO %d Picture" % n):
                        self.assertFalse(x < ox + ow and ox < x + w and y < oy + oh and oy < y + h, (n, name))
                self.assertTrue(0 <= y and y + h <= 628)

    def test_existing_controls_and_qlinks_untouched(self):
        for title in ("LFO 1-2", "LFO 3-4"):
            now, was = self.tab(self.tui, title), self.tab(self.orig, title)
            self.assertEqual(now[:len(was)], was)
            self.assertEqual(len(now), len(was) + 26)
        self.assertEqual((self.alone / "Q-Links.json").read_bytes(), self.before["Q-Links.json"])
        keep = lambda tui: [t for t in tui["pageData"]["tabs"] if t["tabName"] not in ("LFO 1-2", "LFO 3-4")]
        self.assertEqual(keep(self.tui), keep(self.orig))

    def test_the_lfo_tabs_are_the_same_with_all_modules(self):
        for title in ("LFO 1-2", "LFO 3-4"):
            self.assertEqual(self.tab(self.tui_full, title), self.tab(self.tui, title))

    def test_display_parameters_come_after_the_engine_parameters(self):
        # the cursor parameters are added by the shell after the list of docs/params.json: no engine parameter index moves
        keys = [p["key"] for p in self.params]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertFalse(any("cursor" in k.lower() for k in keys))
        shell = (ROOT / "shell-mpc" / "synthyboy_mpc.cpp").read_text(encoding="utf-8")
        self.assertIn('"LFO %d Cursor"', shell)

    def test_files_are_in_the_skin(self):
        for i in range(12):
            w, h, _ = read_png(self.full / ("lfo_live_%02d.png" % i))
            self.assertEqual((w, h), (208, 38 * 33))
        self.assertEqual(read_png(self.full / "lfo_custom.png")[:2], (208, 38))

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, str(SDK / "tools" / "validate.py"), str(self.full.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)
        self.assertIn("OK     TUI.json: image lfo_live_00.png present", report)


if __name__ == "__main__":
    unittest.main()
