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
        self.assertEqual(fmt.build_bank_header(chunk_size=len(self.decoded) - 160), self.decoded[:160])

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


if __name__ == "__main__":
    unittest.main()
