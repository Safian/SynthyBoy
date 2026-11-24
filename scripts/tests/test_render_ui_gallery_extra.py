#!/usr/bin/env python3
# SynthyBoy - tests of scripts/render-ui-gallery-extra.py that need no built host.
# A fake host program stands in for ui_gallery_extra_host: it writes files per state and page.
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
SCRIPT = ROOT / "scripts" / "render-ui-gallery-extra.py"

EXTRA_PAGES = [
    "osca_sample_empty",
    "osca_sample_loaded",
    "osca_grain_empty",
    "osca_grain_loaded",
    "osca_wavetable_list",
    "osca_wavetable_categories",
    "osca_wavetable_category",
    "osca_wavetable_file_selector",
    "osca_wavetable_file_marked",
    "osca_warp_list",
    "osca_route",
    "osca_unison",
    "osca_grain_window",
    "osca_mods_empty",
    "osca_mods_routes",
    "filter1_model_standard",
    "filter1_model_formant",
    "filter1_model_comb",
    "filter1_style_list",
    "filter_note_targets",
    "filter_dual_parallel",
    "filter_dual_serial",
    "env1_targets_empty",
    "env1_targets_several",
    "env1_beats",
    "env2",
    "lfo1_bpm",
    "lfo1_key",
    "lfo_random",
    "lfo1_shape_drawn",
    "lfo_editor_empty",
    "lfo_editor_points",
    "mod_slot_detail",
    "mod_source_picker_groups",
    "mod_target_picker_groups",
    "mod_by_source",
    "mod_by_dest",
    "mod_matrix_scrolled",
    "macro_targets",
    "macros_bank2",
    "mixer_strip_sub",
    "mixer_strip_osca",
    "mixer_dest_popup",
    "fx_chorus_page1",
    "fx_chorus_page2",
    "fx_compressor_page1",
    "fx_compressor_page2",
    "fx_rack_bus1",
    "fx_rack_bus2",
    "fx_chain_full",
    "fx_gallery_add",
    "fx_param_choice_popup",
    "master_main",
    "master_tuning_mpe",
    "voice_row_pan",
    "voice_row_detune",
    "lib_list_popup",
]

FAKE_HOST = r'''#!/usr/bin/env python3
import sys, json
from pathlib import Path
a = sys.argv[1:]
out = Path(a[a.index("--out") + 1])
presets = a[a.index("--presets") + 1:] if "--presets" in a else []
pages = json.loads(Path(sys.argv[0]).with_name("pages.json").read_text())
for state in ["Init"] + [Path(p).stem for p in presets]:
    for page in pages:
        (out / ("%s_%s.png" % (state, page))).write_bytes(b"\x89PNG\r\n\x1a\nfake")
'''


def load_module():
    spec = importlib.util.spec_from_file_location("render_ui_gallery_extra", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class GalleryExtraStates(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.host = d / "fake_extra_host.py"
        self.host.write_text(FAKE_HOST)
        self.host.chmod(self.host.stat().st_mode | stat.S_IXUSR)
        (d / "pages.json").write_text(json.dumps(EXTRA_PAGES))
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
        states = ["Init", "808_boom", "Pad_one"]
        self.assertEqual(mod.parse_page_and_state("808_boom_env2.png", states)[0], "808_boom")
        self.assertEqual(mod.parse_page_and_state("808_boom_env2.png", states)[2], "env2")
        self.assertEqual(mod.parse_page_and_state("Pad_one_fx_compressor_page1.png", states)[0], "Pad_one")
        self.assertEqual(mod.parse_page_and_state("Init_env1_targets_empty.png", states)[0], "Init")
        # the longest state name wins when one is the prefix of another
        self.assertEqual(mod.parse_page_and_state("Pad_one_two_env2.png", ["Init", "Pad_one", "Pad_one_two"])[0], "Pad_one_two")

    def test_no_preset_name_is_built_into_the_state_parsing(self):
        src = SCRIPT.read_text(encoding="utf-8")
        for fixed in ("scream_filter_bass_", "tri_band_split_texture_", "aux_keyscale_pluck_"):
            self.assertNotIn(fixed, src, "the state must come from the presets given, not from a fixed prefix list")

    def test_list_prints_state_page_file(self):
        r = self.run_script("--list")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        rows = [l.split("\t") for l in r.stdout.splitlines() if l.count("\t") == 2]
        self.assertIn(["808_boom", "env2", "808_boom_env2.png"], rows)
        self.assertIn(["Init", "env1_targets_empty", "Init_env1_targets_empty.png"], rows)
        self.assertIn(["Pad_one", "fx_compressor_page1", "Pad_one_fx_compressor_page1.png"], rows)
        self.assertEqual(len(rows), 3 * len(EXTRA_PAGES), "one row per picture")
        self.assertEqual(rows, sorted(rows), "rows are sorted by state, then page")

    def test_only_limits_the_list_and_the_contact_sheet_but_keeps_the_pictures(self):
        r = self.run_script("--list", "--only", "env,fx_")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        rows = [l.split("\t") for l in r.stdout.splitlines() if l.count("\t") == 2]
        pages = sorted(set(row[1] for row in rows))
        self.assertTrue(pages, "something is listed")
        for p in pages:
            self.assertTrue(p.startswith("env") or p.startswith("fx_"), p)
        self.assertIn("env1_targets_empty", pages)
        self.assertIn("fx_compressor_page1", pages)
        index = (self.out / "index.html").read_text(encoding="utf-8")
        self.assertIn("808_boom_env2.png", index)
        self.assertNotIn("808_boom_osca_sample_empty.png", index, "pictures outside --only are not on the contact sheet")
        self.assertTrue((self.out / "808_boom_osca_sample_empty.png").is_file(), "but the files stay")

    def test_without_list_no_rows_are_printed(self):
        r = self.run_script()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual([l for l in r.stdout.splitlines() if l.count("\t") == 2], [])
        index = (self.out / "index.html").read_text(encoding="utf-8")
        self.assertIn("808_boom_env2.png", index)
        self.assertIn("Pad_one_env2.png", index)


if __name__ == "__main__":
    unittest.main()
