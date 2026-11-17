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
        self.assertEqual(fmt.build_bank_header(chunk_size=len(self.decoded) - 160, num_programs=113), self.decoded[:160])

    def test_header_defaults_to_factory_derived_programs(self):
        hdr = fmt.build_bank_header(chunk_size=100)
        parsed = fmt.parse_bank_header(hdr)
        self.assertEqual(parsed["num_programs"], 137)

    def test_whole_file_is_reproduced_except_preset_name_mark_and_times(self):
        parsed = fmt.parse_xpl(self.text)
        attrs = dict(parsed["plugin_attrs"])
        rebuilt = fmt.render_xpl(parsed["version_file"], parsed["version_date"], attrs, parsed["preset"], self.state)
        self.assertEqual(rebuilt.encode("utf-8"), OBSERVED.read_bytes())

    def test_descriptor_defaults_match_the_host_written_one(self):
        attrs = fmt.parse_xpl(self.text)["plugin_attrs"]
        for key in ("name", "format", "category", "manufacturer", "version", "uid", "isInstrument", "numInputs", "numOutputs", "isShell"):
            self.assertEqual(gen.PLUGIN_ATTRS[key], attrs[key], key)
        self.assertEqual(gen.DEVICE_PLUGIN_PATHS["xl"], attrs["file"])


class TestNaming(unittest.TestCase):
    def test_stem(self):
        self.assertEqual(gen.file_stem(0, "Keys", "Studio Grand"), "000-Keys-Studio Grand")
        self.assertEqual(gen.file_stem(12, "Bass", "A/B: C?"), "012-Bass-A-B- C")

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
            rows = gen.plan(chunks, p)
        self.assertEqual([(r[0], r[1], r[3]) for r in rows], [(0, "Bass", "Alpha Bass"), (1, "Bass", "Beta Bass"), (2, "Lead", "alpha lead"), (3, "Pad", "Zed Pad")])

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
            self.assertEqual(parsed["header"]["num_programs"], 137)

    def test_write_set_accepts_num_programs_override(self):
        chunk = b'{"mpcProgram":1,"mpcProgramName":"X"}'
        rows = [(0, "Bass", "Bass", "X", chunk)]
        with tempfile.TemporaryDirectory() as t:
            out_dir = Path(t)
            gen.write_set(rows, out_dir, gen.DEVICE_PLUGIN_PATHS["xl"], num_programs=200)
            f = out_dir / "000-Bass-X.xpl"
            self.assertTrue(f.is_file())
            parsed = fmt.parse_xpl(f.read_text(encoding="utf-8"))
            self.assertEqual(parsed["header"]["num_programs"], 200)

    def test_main_cli_num_programs_override(self):
        import subprocess
        import sys
        chunk = b'{"mpcProgram":1,"mpcProgramName":"X"}'
        with tempfile.TemporaryDirectory() as t:
            p = Path(t)
            chunks_dir = p / "chunks"
            chunks_dir.mkdir()
            (chunks_dir / "001-Bass-X.chunk").write_bytes(chunk)
            presets_dir = p / "presets"
            (presets_dir / "Bass").mkdir(parents=True)
            (presets_dir / "Bass" / "x.sbpreset").write_text(json.dumps({"meta": {"name": "X"}}))
            out_dir = p / "out"
            cmd = [
                sys.executable,
                str(SCRIPTS / "gen-xpl-presets.py"),
                "--chunks", str(chunks_dir),
                "--presets", str(presets_dir),
                "--out", str(out_dir),
                "--num-programs", "250",
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stderr)
            files = list(out_dir.glob("*.xpl"))
            self.assertEqual(len(files), 1)
            parsed = fmt.parse_xpl(files[0].read_text(encoding="utf-8"))
            self.assertEqual(parsed["header"]["num_programs"], 250)


if __name__ == "__main__":
    unittest.main()
