# SynthyBoy - tests for gen-xpl-presets.py and the JUCE state encoding it relies on
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
import json
import re
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPTS = HERE.parent
REPO = SCRIPTS.parent
OBSERVED = HERE / "data" / "synthyboy-host-saved-Space-Wobble-Bass.xpl"   # written by the MPC XL's own "save plugin preset"


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gen = load("gen_xpl_presets", "gen-xpl-presets.py")
fmt = gen.gen_mpc_xpl


class TestObservedFile(unittest.TestCase):
    def setUp(self):
        self.text = OBSERVED.read_bytes().decode("utf-8")
        self.state = re.search(r"<state>(.*?)</state>", self.text, re.S).group(1)
        self.decoded = fmt.decode_juce_base64(self.state)

    def test_state_text_is_reproduced_exactly(self):
        self.assertEqual(fmt.encode_juce_base64(self.decoded), self.state)

    def test_decoded_state_is_a_bank_chunk(self):
        self.assertEqual(len(self.decoded), 50353)
        self.assertTrue(self.state.startswith("50353."))
        h = fmt.parse_bank_header(self.decoded[:160])
        self.assertEqual((h["chunk_magic"], h["fx_magic"], h["fx_id"]), (b"CcnK", b"FBCh", b"SfSB"))
        self.assertEqual((h["version"], h["fx_version"], h["num_programs"], h["current_program"], h["byte_size"]), (1, 1000, 113, 0, 0))
        self.assertEqual(h["chunk_size"], len(self.decoded) - 160)
        chunk = json.loads(self.decoded[160:])
        self.assertEqual((chunk["mpcProgram"], chunk["mpcProgramName"]), (20, "Space Wobble Bass"))

    def test_header_is_rebuilt_exactly(self):
        self.assertEqual(fmt.build_bank_header(chunk_size=len(self.decoded) - 160, num_programs=113, fx_version=1000), self.decoded[:160])   # the observed file is from the 113 program list

    def test_whole_file_is_reproduced_except_preset_name_mark_and_times(self):
        parsed = fmt.parse_xpl(self.text)
        attrs = dict(parsed["plugin_attrs"])
        rebuilt = fmt.render_xpl(parsed["version_file"], parsed["version_date"], attrs, parsed["preset"], self.state)
        self.assertEqual(rebuilt.encode("utf-8"), OBSERVED.read_bytes())

    def test_descriptor_defaults_match_the_host_written_one(self):
        attrs = fmt.parse_xpl(self.text)["plugin_attrs"]
        for key in ("name", "format", "category", "manufacturer", "uid", "isInstrument", "numInputs", "numOutputs", "isShell"):
            self.assertEqual(gen.PLUGIN_ATTRS[key], attrs[key], key)
        self.assertEqual(gen.PLUGIN_ATTRS["version"], "0.1.0.1")
        self.assertEqual(gen.DEVICE_PLUGIN_PATHS["xl"], attrs["file"])


class TestNaming(unittest.TestCase):
    def test_stem(self):
        self.assertEqual(gen.file_stem(0, "Keys", "Studio Grand"), "000-Keys-Studio Grand")
        self.assertEqual(gen.file_stem(12, "Bass", "A/B: C?"), "012-Bass-A-B- C")

    def test_stem_with_substyle_category_names(self):
        self.assertEqual(gen.file_stem(0, "Bass Sub+808", "808 Boom"), "000-Bass Sub+808-808 Boom")
        self.assertEqual(gen.file_stem(1, "Lead Sync+FM", "Laser Beam Lead"), "001-Lead Sync+FM-Laser Beam Lead")
        self.assertEqual(gen.file_stem(2, "Bells+Mallets", "Celesta Bells"), "002-Bells+Mallets-Celesta Bells")
        self.assertEqual(gen.file_stem(3, "Strings+Brass", "Vintage Brass Pad"), "003-Strings+Brass-Vintage Brass Pad")

    def test_numbering_follows_category_order_then_name(self):
        chunks = [("Zed Pad", b"{}"), ("alpha lead", b"{}"), ("Beta Bass", b"{}"), ("Alpha Bass", b"{}")]
        with tempfile.TemporaryDirectory() as t:
            p = Path(t)
            for cat, n in (("Bass", "alpha_bass"), ("Bass", "beta_bass"), ("Lead", "alpha_lead"), ("Pad", "zed_pad")):
                (p / cat).mkdir(exist_ok=True)
                nice = {"alpha_bass": "Alpha Bass", "beta_bass": "Beta Bass", "alpha_lead": "alpha lead", "zed_pad": "Zed Pad"}[n]
                (p / cat / (n + ".sbpreset")).write_text(json.dumps({"meta": {"name": nice}}))
            for cat, _ in gen.CATEGORIES:
                (p / cat).mkdir(exist_ok=True)
            rows = gen.plan(chunks, p, categories_file=None)
        self.assertEqual([(r[0], r[1], r[3]) for r in rows], [(0, "Bass", "Alpha Bass"), (1, "Bass", "Beta Bass"), (2, "Lead", "alpha lead"), (3, "Pad", "Zed Pad")])

    def test_plan_with_categories_json_groups_order_and_alphabetic_sort(self):
        chunks = [("Zeta", b"{}"), ("Alpha", b"{}"), ("Gamma", b"{}"), ("Beta", b"{}")]
        with tempfile.TemporaryDirectory() as t:
            p = Path(t)
            for folder in ["Bass", "Lead"]:
                (p / folder).mkdir(exist_ok=True)
            (p / "Bass" / "zeta.sbpreset").write_text(json.dumps({"meta": {"name": "Zeta"}}))
            (p / "Bass" / "alpha.sbpreset").write_text(json.dumps({"meta": {"name": "Alpha"}}))
            (p / "Lead" / "gamma.sbpreset").write_text(json.dumps({"meta": {"name": "Gamma"}}))
            (p / "Lead" / "beta.sbpreset").write_text(json.dumps({"meta": {"name": "Beta"}}))
            for cat, _ in gen.CATEGORIES:
                (p / cat).mkdir(exist_ok=True)

            cat_json = p / "categories.json"
            cat_json.write_text(json.dumps({
                "groups": ["First Group", "Second Group"],
                "presets": {
                    "Zeta": {"group": "Second Group", "why": "test"},
                    "Beta": {"group": "Second Group", "why": "test"},
                    "Gamma": {"group": "First Group", "why": "test"},
                    "Alpha": {"group": "First Group", "why": "test"},
                }
            }))

            rows = gen.plan(chunks, p, categories_file=cat_json)

        # First Group (Alpha, Gamma), then Second Group (Beta, Zeta)
        self.assertEqual(
            [(r[0], r[2], r[3]) for r in rows],
            [
                (0, "First Group", "Alpha"),
                (1, "First Group", "Gamma"),
                (2, "Second Group", "Beta"),
                (3, "Second Group", "Zeta"),
            ]
        )

    def test_all_184_factory_presets_planned_with_valid_browser_categories(self):
        presets_dir = REPO / "content" / "presets"
        cats = gen.preset_categories(presets_dir)
        self.assertEqual(len(cats), 184)
        chunks = [(name, b"{}") for name in sorted(cats.keys())]
        rows = gen.plan(chunks, presets_dir, categories_file=gen.CATEGORIES_FILE)
        self.assertEqual(len(rows), 184)

        # Check sequential numbering 000..183
        self.assertEqual([r[0] for r in rows], list(range(184)))

        # Check that no category word contains slash and all match valid group names
        for number, folder, cword, name, chunk in rows:
            self.assertNotIn("/", cword)
            stem = gen.file_stem(number, cword, name)
            self.assertNotIn("/", stem)
            self.assertTrue(stem.startswith(f"{number:03d}-{cword}-"))

    def test_no_host_visible_name_ends_with_a_star(self):
        """The "modified" mark (" *") is gone by design (docs/MPC.md "Q proxies"): no factory preset name, no generated .xpl
        (file name, <preset> name, chunk name) and no content pack entry ends with "*"."""
        presets_dir = REPO / "content" / "presets"
        cats = gen.preset_categories(presets_dir)
        self.assertEqual(len(cats), 184)
        for name in cats:
            self.assertFalse(name.rstrip().endswith("*"), name)
        chunks = [(name, ('{"mpcProgram":1,"mpcProgramName":%s}' % json.dumps(name)).encode("utf-8")) for name in sorted(cats)]
        rows = gen.plan(chunks, presets_dir, categories_file=gen.CATEGORIES_FILE)
        with tempfile.TemporaryDirectory() as t:
            for by_folders in (False, True):
                out = Path(t, "f" if by_folders else "flat")
                gen.write_set(rows, out, gen.DEVICE_PLUGIN_PATHS["xl"], by_folders=by_folders)
                files = sorted(out.rglob("*.xpl"))
                self.assertEqual(len(files), 184)
                for f in files:
                    self.assertFalse(f.stem.rstrip().endswith("*"), f.name)
                    parsed = fmt.parse_xpl(f.read_text(encoding="utf-8"))
                    self.assertFalse(json.loads(parsed["chunk_bytes"])["mpcProgramName"].rstrip().endswith("*"), f.name)
                    self.assertNotRegex(f.read_text(encoding="utf-8"), r"<preset>[^<]*\*\s*</preset>")
        pack = json.loads((presets_dir / "contentpack.json").read_text(encoding="utf-8"))
        for entry in pack["files"]:
            for value in entry.values():
                if isinstance(value, str):
                    self.assertFalse(value.rstrip().endswith("*"), value)

    def test_every_content_folder_is_in_the_table(self):
        folders = {p.name for p in (REPO / "content" / "presets").iterdir() if p.is_dir()}
        self.assertEqual(folders, {c for c, _ in gen.CATEGORIES})

    def test_written_set_is_deterministic_crlf_and_decodes_to_the_chunk(self):
        chunk = b'{"mpcProgram":1,"mpcProgramName":"X"}'
        rows = [(0, "Bass", "Bass", "X & Y", chunk)]
        with tempfile.TemporaryDirectory() as t:
            a, b = Path(t, "a"), Path(t, "b")
            gen.write_set(rows, a, gen.DEVICE_PLUGIN_PATHS["k61"])
            gen.write_set(rows, b, gen.DEVICE_PLUGIN_PATHS["k61"], by_folders=True)
            fa = a / "000-Bass-X & Y.xpl"
            self.assertTrue(fa.is_file())
            self.assertTrue((b / "Bass" / "000-Bass-X & Y.xpl").is_file())
            raw = fa.read_bytes()
            self.assertEqual(raw, (b / "Bass" / "000-Bass-X & Y.xpl").read_bytes())
            self.assertNotIn(b"\n", raw.replace(b"\r\n", b""))
            self.assertIn(b"<preset>X &amp; Y</preset>", raw)
            self.assertIn(b"synthyboy-arm32.so", raw)
            parsed = fmt.parse_xpl(raw.decode("utf-8"))
            self.assertEqual(parsed["chunk_bytes"], chunk)
            self.assertEqual(parsed["header"]["num_programs"], 2)

    def test_write_set_num_programs_override(self):
        chunk = b'{"mpcProgram":1,"mpcProgramName":"X"}'
        rows = [(0, "Bass", "Bass", "X", chunk)]
        with tempfile.TemporaryDirectory() as t:
            p = Path(t)
            gen.write_set(rows, p, gen.DEVICE_PLUGIN_PATHS["xl"], num_programs=137)
            raw = (p / "000-Bass-X.xpl").read_bytes()
            parsed = fmt.parse_xpl(raw.decode("utf-8"))
            self.assertEqual(parsed["header"]["num_programs"], 137)


if __name__ == "__main__":
    unittest.main()
