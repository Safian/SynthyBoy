#!/usr/bin/env python3
# SynthyBoy - tests of scripts/mpc-skin-extras/20_osc.py and scripts/gen-mpc-wave-strips.py (oscillator wave pictures).
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_mpc_skin_extras as base  # noqa: E402

ROOT = base.ROOT
ASSETS = ROOT / "scripts" / "mpc-skin-extras" / "osc-wave-strips"
PICTURE_W, PICTURE_H = 512, 240
MAX_DECODED_BYTES = 64 * 1024 * 1024
SINGLE = {0, 1, 2, 3, 4, 13, 14}   # one frame in the engine (13 = Preset table: neutral picture)
MORPH_FRAMES = 16


def load_generator():
    spec = importlib.util.spec_from_file_location("t_gen_mpc_wave_strips", ROOT / "scripts" / "gen-mpc-wave-strips.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def png_size(path):
    data = Path(path).read_bytes()
    return struct.unpack(">II", data[16:24])


def knob_entries(tab):
    return [e for e in tab["componentDefinition"]["componentsData"] if e["componentData"]["type"] != "Image"]


def picture_entries(tab):
    return [e for e in tab["componentDefinition"]["componentsData"] if e["componentData"]["type"] == "Image"]


@unittest.skipUnless((base.SDK / "tools" / "gen_ui.py").is_file(), "MPC SDK not found (set MPC_SDK)")
class OscSkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls._tmp.name)
        cls.skins = base.generate_skins(cls.root)
        cls.only = cls.root / "only_osc"
        cls.only.mkdir()
        (cls.only / "20_osc.py").symlink_to(ROOT / "scripts" / "mpc-skin-extras" / "20_osc.py")
        code, err = base.run_apply(cls.skins, cls.only)
        assert code == 0, err
        cls.tui = json.loads((cls.skins / "TUI.json").read_text(encoding="utf-8"))
        cls.qlinks = json.loads((cls.skins / "Q-Links.json").read_text(encoding="utf-8"))
        cls.params = json.loads(base.PARAMS_JSON.read_text(encoding="utf-8"))["params"]
        cls.index = {p["key"]: i for i, p in enumerate(cls.params)}

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def tab(self, name):
        return next(t for t in self.tui["pageData"]["tabs"] if t["tabName"] == name)

    def test_main_tab_has_picture_and_eight_knobs_in_rows_3_4(self):
        for n in (1, 2, 3):
            tab = self.tab("Osc %d" % n)
            knobs = knob_entries(tab)
            self.assertEqual(len(knobs), 8)
            keys = ["on", "wavetable", "frame", "level", "pan", "transpose", "tune", "destination"]
            self.assertEqual([k["handle remapping"]["map"][0]["value"] for k in knobs],
                             ["Parameter %d" % self.index["osc%d.%s" % (n, key)] for key in keys])
            for k in knobs:
                x, y, w, h = k["bounds"]["bounds"].split()
                self.assertIn(int(y), (346, 497), k["componentData"]["name"])
            self.assertEqual(tab["qlinkBoundsData"], ["%s 336 %s 288" % (b.split()[0], b.split()[2]) for b in tab["qlinkBoundsData"]])

    def test_qlinks_follow_the_screen_rows(self):
        for n in (1, 2, 3):
            tab = self.tab("Osc %d" % n)
            row_map = next(m for m in self.qlinks["Screen Mode Q-Links"]["map"] if m["Tab"] == tab["fnKeyIndex"] + 1 and m["SubTab"] == 1)
            expected = {"Q-Link %d" % i: -1 for i in range(1, 17)}
            for k in knob_entries(tab):
                x, y = (int(v) for v in k["bounds"]["bounds"].split()[:2])
                row, col = {346: 2, 497: 3}[y], {23: 0, 343: 1, 663: 2, 983: 3}[x]
                # the knob of screen row r, column c is next to the Q-Link 13 + c - 4 r of the hardware (ui-reference 8.2)
                expected["Q-Link %d" % (13 + col - 4 * row)] = int(k["handle remapping"]["map"][0]["value"].split()[1])
            self.assertEqual(row_map["Q-Links"], expected)
            self.assertEqual(sum(1 for v in expected.values() if v != -1), 8)
            self.assertTrue(all(expected["Q-Link %d" % i] == -1 for i in range(9, 17)))

    def test_shp_tabs_hold_unison_and_shaping(self):
        for n in (1, 2, 3):
            tab = self.tab("Osc%d Shp" % n)
            self.assertEqual(len(tab["componentDefinition"]["componentsData"]), 15)
            params = {e["handle remapping"]["map"][0]["value"] for e in tab["componentDefinition"]["componentsData"]}
            for key in ("osc%d.unison.voices" % n, "osc%d.warp.type" % n, "osc%d.morph.amount" % n, "osc%d.phase" % n):
                self.assertIn("Parameter %d" % self.index[key], params)

    def test_pictures_one_per_wavetable_shared_files(self):
        shared = None
        for n in (1, 2, 3):
            pics = picture_entries(self.tab("Osc %d" % n))
            self.assertEqual(len(pics), 15)
            files = []
            for choice, e in enumerate(pics):
                d = e["componentData"]["data"]
                self.assertEqual((d["imageType"], d["image"]), ("FilmStrip", "osc_wt_%02d.png" % choice))
                self.assertEqual(d["numFrames"], 1 if choice in SINGLE else MORPH_FRAMES)
                self.assertEqual(e["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % self.index["osc%d.frame" % n]}])
                b = e["bounds"]
                self.assertEqual((b["version"], b["acceptsHWFocus"], b["bounds"]), (2, "No", "384 70 512 240"))
                self.assertEqual(b["additionalInvalidatingHandles"], ["IndexedEnabling/%d/15/Parameter %d" % (choice, self.index["osc%d.wavetable" % n])])
                self.assertEqual(png_size(self.skins / d["image"]), (PICTURE_W, PICTURE_H * d["numFrames"]))
                files.append(d["image"])
            if shared is None:
                shared = files
            self.assertEqual(files, shared)  # the three oscillators use the same files

    def test_decoded_memory_within_budget(self):
        total = sum(w * h * 4 for w, h in (png_size(p) for p in self.skins.glob("osc_wt_*.png")))
        self.assertEqual(len(list(self.skins.glob("osc_wt_*.png"))), 15)
        self.assertLessEqual(total, MAX_DECODED_BYTES)

    def test_validate_py_has_no_error(self):
        res = subprocess.run([sys.executable, str(base.SDK / "tools" / "validate.py"), str(self.skins.parent)], capture_output=True, text=True)
        report = res.stdout + res.stderr
        errors = [l for l in report.splitlines() if l.startswith("ERROR") and "plugin.json missing" not in l]
        self.assertEqual(errors, [], report)
        for choice in range(15):
            self.assertIn("OK     TUI.json: image osc_wt_%02d.png present" % choice, report)

    def test_skin_is_deterministic(self):
        skins2 = base.generate_skins(self.root / "again")
        self.assertEqual(base.run_apply(skins2, self.only)[0], 0)
        for p in sorted(self.skins.iterdir()):
            self.assertEqual((skins2 / p.name).read_bytes(), p.read_bytes(), p.name)


def have_compiler():
    return any(shutil.which(c) for c in ("c++", "clang++", "g++"))


def have_render_modules():
    # the picture renderer is a development tool (the build copies committed PNGs); it needs numpy and Pillow
    return all(importlib.util.find_spec(m) for m in ("numpy", "PIL"))


@unittest.skipUnless(have_render_modules(), "numpy and Pillow not installed for this python3 (gen-mpc-wave-strips.py is a development tool)")
class WaveStripsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = load_generator()

    def test_frame_positions_cover_the_range_evenly(self):
        g = self.gen
        pos = [g.frame_position(i, MORPH_FRAMES) for i in range(MORPH_FRAMES)]
        self.assertEqual(pos[-1], 1.0)
        steps = [pos[i + 1] - pos[i] for i in range(MORPH_FRAMES - 2)]
        for s in steps:
            self.assertAlmostEqual(s, 1.0 / (MORPH_FRAMES - 1))
        # frame i is shown for v in [i/(n-1), (i+1)/(n-1)): its position lies in the middle of that interval
        for i in range(MORPH_FRAMES - 1):
            self.assertAlmostEqual(pos[i], (i / (MORPH_FRAMES - 1) + (i + 1) / (MORPH_FRAMES - 1)) / 2)
        self.assertEqual(g.frame_position(0, 1), 0.0)

    def test_cycle_at_mixes_neighbours(self):
        import numpy as np
        frames = np.stack([np.zeros(2048, dtype=np.float32), np.ones(2048, dtype=np.float32), np.full(2048, 3, dtype=np.float32)])
        self.assertAlmostEqual(float(self.gen.cycle_at(frames, 0.0)[0]), 0.0)
        self.assertAlmostEqual(float(self.gen.cycle_at(frames, 0.25)[0]), 0.5)
        self.assertAlmostEqual(float(self.gen.cycle_at(frames, 0.75)[0]), 2.0)
        self.assertAlmostEqual(float(self.gen.cycle_at(frames, 1.0)[0]), 3.0)

    def test_render_frame_is_deterministic_and_sized(self):
        import numpy as np
        cycle = np.sin(np.arange(2048) * 2 * np.pi / 2048).astype(np.float32)
        a, b = self.gen.render_frame(cycle, "SINE"), self.gen.render_frame(cycle, "SINE")
        self.assertEqual(a.size, (PICTURE_W, PICTURE_H))
        self.assertEqual(a.tobytes(), b.tobytes())
        self.assertNotEqual(a.tobytes(), self.gen.render_frame(-cycle, "SINE").tobytes())

    @unittest.skipUnless(have_compiler(), "no host C++ compiler")
    def test_engine_dump_matches_assets_and_is_deterministic(self):
        g = self.gen
        dump = g.build_dump()
        self.assertEqual(dump, g.build_dump())
        tables = g.read_dump(dump)
        self.assertEqual(len(tables), 15)
        self.assertIsNone(tables[13])
        for choice, frames in enumerate(tables):
            if choice == 13:
                continue
            self.assertEqual(len(frames) == 1, choice in SINGLE, choice)
        with tempfile.TemporaryDirectory() as tmp:
            first, second = Path(tmp) / "a", Path(tmp) / "b"
            g.write_all(first, dump)
            g.write_all(second, dump)
            names = sorted(p.name for p in first.iterdir())
            self.assertEqual(names, ["osc_wt_%02d.png" % c for c in range(15)])
            self.assertEqual(names, sorted(p.name for p in ASSETS.iterdir()))
            from PIL import Image
            for name in names:
                self.assertEqual((first / name).read_bytes(), (second / name).read_bytes(), name)
                a, b = Image.open(first / name), Image.open(ASSETS / name)
                self.assertEqual((a.size, a.convert("RGBA").tobytes()), (b.size, b.convert("RGBA").tobytes()), "%s differs from the committed asset: run scripts/gen-mpc-wave-strips.py" % name)


if __name__ == "__main__":
    unittest.main()
