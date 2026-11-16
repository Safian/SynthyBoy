#!/usr/bin/env python3
# SynthyBoy - tests of scripts/render-ui-gallery.py that need no built host: state names taken from the presets given (not from a fixed list), --list and --only.
# A fake host program stands in for ui_gallery_host: it writes one file per state and view, named <state>_<page>.png, like the real one.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import importlib.util
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / "scripts" / "render-ui-gallery.py"
VIEWS = [v["name"] for v in json.loads((ROOT / "shell-mpc" / "overlay" / "overlay_layout.json").read_text(encoding="utf-8"))["views"]]

FAKE_HOST = r'''#!/usr/bin/env python3
import sys, json
from pathlib import Path
a = sys.argv[1:]
out = Path(a[a.index("--out") + 1])
presets = a[a.index("--presets") + 1:] if "--presets" in a else []
views = json.loads(Path(sys.argv[0]).with_name("views.json").read_text())
for state in ["Init"] + [Path(p).stem for p in presets]:
    for page in views + ["env1_targets", "fx_eq_page2"]:
        (out / ("%s_%s.png" % (state, page))).write_bytes(b"\x89PNG\r\n\x1a\nfake")
'''


def load_module():
    spec = importlib.util.spec_from_file_location("render_ui_gallery", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class GalleryStates(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.host = d / "fake_host.py"
        self.host.write_text(FAKE_HOST)
        self.host.chmod(self.host.stat().st_mode | stat.S_IXUSR)
        (d / "views.json").write_text(json.dumps(VIEWS))
        self.presets = [d / "808_boom.sbpreset", d / "Pad_one.sbpreset"]
        for p in self.presets:
            p.write_text("{}")
        self.out = d / "out"

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self, *extra):
        cmd = [sys.executable, "-I", str(SCRIPT), "--out", str(self.out), "--host-bin", str(self.host), "--presets"] + [str(p) for p in self.presets] + list(extra)
        return subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT))

    def test_state_is_the_preset_name_also_with_underscores(self):
        mod = load_module()
        views = [{"name": v} for v in VIEWS]
        states = ["Init", "808_boom", "Pad_one"]
        self.assertEqual(mod.parse_page_and_state("808_boom_env1.png", views, states)[0], "808_boom")
        self.assertEqual(mod.parse_page_and_state("808_boom_env1.png", views, states)[2], "env1")
        self.assertEqual(mod.parse_page_and_state("Pad_one_fx_eq_page2.png", views, states)[0], "Pad_one")
        self.assertEqual(mod.parse_page_and_state("Init_env1_targets.png", views, states)[0], "Init")
        # the longest state name wins when one is the beginning of another
        self.assertEqual(mod.parse_page_and_state("Pad_one_two_env1.png", views, ["Init", "Pad_one", "Pad_one_two"])[0], "Pad_one_two")

    def test_no_preset_name_is_built_into_the_state_parsing(self):
        src = SCRIPT.read_text(encoding="utf-8")
        for fixed in ("scream_filter_bass_", "tri_band_split_texture_", "aux_keyscale_pluck_"):
            self.assertNotIn(fixed, src, "the state must come from the presets given, not from a fixed prefix list")

    def test_list_prints_state_page_file(self):
        r = self.run_script("--list")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        rows = [l.split("\t") for l in r.stdout.splitlines() if l.count("\t") == 2]
        self.assertIn(["808_boom", "env1", "808_boom_env1.png"], rows)
        self.assertIn(["Init", "env1_targets", "Init_env1_targets.png"], rows)
        self.assertIn(["Pad_one", "fx_eq_page2", "Pad_one_fx_eq_page2.png"], rows)
        self.assertEqual(len(rows), 3 * (len(VIEWS) + 2), "one row per picture")
        self.assertEqual(rows, sorted(rows), "rows are sorted by state, then page")

    def test_only_limits_the_list_and_the_contact_sheet_but_keeps_the_pictures(self):
        r = self.run_script("--list", "--only", "env1,fx_")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        rows = [l.split("\t") for l in r.stdout.splitlines() if l.count("\t") == 2]
        pages = sorted(set(row[1] for row in rows))
        self.assertTrue(pages, "something is listed")
        for p in pages:
            self.assertTrue(p.startswith("env1") or p.startswith("fx_"), p)
        self.assertIn("env1_targets", pages)
        self.assertIn("fx_eq_page2", pages)
        index = (self.out / "index.html").read_text(encoding="utf-8")
        self.assertIn("808_boom_env1.png", index)
        other = next(v for v in VIEWS if not v.startswith("env1") and not v.startswith("fx"))
        self.assertNotIn("808_boom_%s.png" % other, index, "pictures outside --only are not on the contact sheet")
        self.assertTrue((self.out / ("808_boom_%s.png" % other)).is_file(), "but the files stay")

    def test_without_list_no_rows_are_printed(self):
        r = self.run_script()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual([l for l in r.stdout.splitlines() if l.count("\t") == 2], [])
        index = (self.out / "index.html").read_text(encoding="utf-8")
        self.assertIn("808_boom_env1.png", index)
        self.assertIn("Pad_one_env1.png", index)


if __name__ == "__main__":
    unittest.main()
