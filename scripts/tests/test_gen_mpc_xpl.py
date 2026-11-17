# SynthyBoy - Unit and integration tests for gen-mpc-xpl.py
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# Add scripts directory to module path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPTS_DIR = REPO_ROOT / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import importlib.util

spec = importlib.util.spec_from_file_location("gen_mpc_xpl", SCRIPTS_DIR / "gen-mpc-xpl.py")
gen_mpc_xpl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_mpc_xpl)

FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"
DEC_PY = FIXTURES_DIR / "dec_juce_state.py"


def get_example_path(filename: str) -> Path:
    """The files the device wrote, recorded in the test fixtures (the tests do not depend on anything outside the repository)."""
    fixture_path = FIXTURES_DIR / filename
    if fixture_path.is_file():
        return fixture_path
    raise FileNotFoundError(f"Cannot find example file {filename} in {FIXTURES_DIR}")


class TestJuceBase64(unittest.TestCase):
    """Test JUCE custom base64 encoder and decoder."""

    def test_roundtrip_random_and_edge_cases(self):
        lengths = list(range(0, 33)) + [48, 63, 64, 65, 100, 255, 256, 1024, 65536]
        for length in lengths:
            data = os.urandom(length)
            encoded = gen_mpc_xpl.encode_juce_base64(data)
            decoded = gen_mpc_xpl.decode_juce_base64(encoded)
            self.assertEqual(decoded, data, f"Mismatch at length {length}")

        # Edge patterns: all 0s, all 0xFF, alternating
        for pattern in [b"\x00" * 50, b"\xff" * 50, bytes([i % 256 for i in range(256)])]:
            encoded = gen_mpc_xpl.encode_juce_base64(pattern)
            decoded = gen_mpc_xpl.decode_juce_base64(encoded)
            self.assertEqual(decoded, pattern)

    def test_error_handling(self):
        with self.assertRaises(ValueError):
            gen_mpc_xpl.decode_juce_base64("no_dot_here")
        with self.assertRaises(ValueError):
            gen_mpc_xpl.decode_juce_base64("notanumber.ABCD")
        with self.assertRaises(ValueError):
            gen_mpc_xpl.decode_juce_base64("4.AB$D")  # '$' not in alphabet

    def test_host_saved_examples_decode_reencode(self):
        for name in ["zz-sbtest-program.xpl", "prev_Acid_Saw_Reso.xpl"]:
            path = get_example_path(name)
            content = path.read_text(encoding="utf-8")
            m = re.search(r"<state>(.*?)</state>", content, re.S)
            self.assertIsNotNone(m, f"Missing <state> in {name}")
            orig_state_str = m.group(1).strip()

            decoded_bytes = gen_mpc_xpl.decode_juce_base64(orig_state_str)
            re_encoded_str = gen_mpc_xpl.encode_juce_base64(decoded_bytes)
            self.assertEqual(
                re_encoded_str,
                orig_state_str,
                f"Decode -> re-encode did not produce identical string for {name}",
            )


class TestBankHeader(unittest.TestCase):
    """Test VST2 160-byte fxBank header parsing and reconstruction."""

    def test_header_fields_named_and_explained(self):
        for name, expected_chunk_size in [("zz-sbtest-program.xpl", 50106), ("prev_Acid_Saw_Reso.xpl", 50427)]:
            path = get_example_path(name)
            parsed = gen_mpc_xpl.parse_xpl(path.read_text(encoding="utf-8"))
            hdr = parsed["header"]

            self.assertEqual(hdr["chunk_magic"], b"CcnK", "chunkMagic must be 'CcnK'")
            self.assertEqual(hdr["byte_size"], 0, "byteSize is 0 in host-saved files")
            self.assertEqual(hdr["fx_magic"], b"FBCh", "fxMagic must be 'FBCh' (FxBankChunk)")
            self.assertEqual(hdr["version"], 1, "format version must be 1")
            self.assertEqual(hdr["fx_id"], b"SfSB", "fxID must be 'SfSB'")
            self.assertEqual(hdr["fx_version"], 1000, "fxVersion must be 1000")
            self.assertEqual(hdr["num_programs"], 113, "numPrograms must be 113")
            self.assertEqual(hdr["current_program"], 0, "currentProgram is 0")
            self.assertEqual(hdr["future"], b"\x00" * 124, "future padding must be 124 zero bytes")
            self.assertEqual(hdr["chunk_size"], expected_chunk_size, "chunkSize must match chunk length")

    def test_header_rebuilt_from_chunk_size(self):
        for name in ["zz-sbtest-program.xpl", "prev_Acid_Saw_Reso.xpl"]:
            path = get_example_path(name)
            parsed = gen_mpc_xpl.parse_xpl(path.read_text(encoding="utf-8"))
            orig_header_bytes = parsed["state_bytes"][:160]

            # Rebuild header only knowing chunk payload length
            rebuilt_header = gen_mpc_xpl.build_bank_header(
                chunk_size=len(parsed["chunk_bytes"]),
                num_programs=parsed["header"]["num_programs"],
                fx_id=parsed["header"]["fx_id"],
                fx_version=parsed["header"]["fx_version"],
            )
            self.assertEqual(
                rebuilt_header,
                orig_header_bytes,
                f"Rebuilt bank header does not match original byte-for-byte in {name}",
            )


class TestXplEnvelope(unittest.TestCase):
    """Test .xpl XML envelope parsing and byte-for-byte formatting."""

    def test_envelope_byte_for_byte_reproduction(self):
        for name in ["zz-sbtest-program.xpl", "prev_Acid_Saw_Reso.xpl"]:
            path = get_example_path(name)
            orig_bytes = path.read_bytes()
            orig_text = orig_bytes.decode("utf-8")

            parsed = gen_mpc_xpl.parse_xpl(orig_text)
            rendered_text = gen_mpc_xpl.render_xpl(
                version_file=parsed["version_file"],
                version_date=parsed["version_date"],
                plugin_attrs=parsed["plugin_attrs"],
                preset=parsed["preset"],
                state_b64=parsed["state_b64"],
            )
            rendered_bytes = rendered_text.encode("utf-8")
            self.assertEqual(
                rendered_bytes,
                orig_bytes,
                f"Rendered .xpl XML does not match original byte-for-byte in {name}",
            )


class TestFilenameRules(unittest.TestCase):
    """Test FAT/exFAT filename safety and collision handling."""

    def test_sanitize_filename(self):
        self.assertEqual(gen_mpc_xpl.sanitize_filename("Acid Saw Reso"), "Acid Saw Reso.xpl")
        self.assertEqual(gen_mpc_xpl.sanitize_filename("Lead <1>: Saw/Square?"), "Lead -1- Saw-Square.xpl")
        self.assertEqual(gen_mpc_xpl.sanitize_filename("Bass | Test *"), "Bass - Test.xpl")
        self.assertEqual(gen_mpc_xpl.sanitize_filename("Pad . . "), "Pad.xpl")
        self.assertEqual(gen_mpc_xpl.sanitize_filename("..."), "Preset.xpl")
        self.assertEqual(gen_mpc_xpl.sanitize_filename("Test\x00\x1fName"), "Test-Name.xpl")

    def test_collision_detection(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            chunks_dir = tmp_path / "chunks"
            chunks_dir.mkdir()
            out_dir = tmp_path / "out"

            # Create two colliding chunks (e.g. 'Lead:1' and 'Lead/1' both sanitize to 'Lead-1.xpl')
            c1 = json.dumps({"mpcProgram": 1, "mpcProgramName": "Lead:1"}).encode("utf-8")
            c2 = json.dumps({"mpcProgram": 2, "mpcProgramName": "Lead/1"}).encode("utf-8")
            (chunks_dir / "prog_001.chunk").write_bytes(c1)
            (chunks_dir / "prog_002.chunk").write_bytes(c2)

            tmpl_path = get_example_path("prev_Acid_Saw_Reso.xpl")
            cmd = [
                sys.executable,
                str(SCRIPTS_DIR / "gen-mpc-xpl.py"),
                "--chunks",
                str(chunks_dir),
                "--template",
                str(tmpl_path),
                "--out",
                str(out_dir),
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(res.returncode, 0, "Colliding preset names must cause error exit")
            self.assertIn("collision", res.stderr.lower())


class TestGeneratedFileComparison(unittest.TestCase):
    """Test generated preset file comparison against host-saved example."""

    def test_generated_file_diff_against_host_saved(self):
        # We record that if native build is absent, fixture chunk is used
        fixture_chunk_path = FIXTURES_DIR / "acid_saw_reso.chunk"
        self.assertTrue(fixture_chunk_path.is_file(), "Fixture acid_saw_reso.chunk must exist")
        chunk_data = fixture_chunk_path.read_bytes()

        tmpl_path = get_example_path("prev_Acid_Saw_Reso.xpl")
        orig_bytes = tmpl_path.read_bytes()
        tmpl_info = gen_mpc_xpl.parse_xpl(orig_bytes.decode("utf-8"))

        # 1. Exact reproduction: using identical template parameters, identical chunk
        fname, xml_text = gen_mpc_xpl.create_xpl_from_chunk(
            chunk_bytes=chunk_data,
            preset_name="Acid Saw Reso",
            template_info=tmpl_info,
            num_programs=tmpl_info["header"]["num_programs"],
        )
        self.assertEqual(fname, "Acid Saw Reso.xpl")
        gen_bytes = xml_text.encode("utf-8")
        self.assertEqual(
            gen_bytes,
            orig_bytes,
            "Generated file with template's exact parameters must be 100% byte-for-byte identical",
        )

        # 2. Overridden plugin-path: exactly and ONLY the 'file' attribute bytes differ
        custom_plugin_path = "/media/custom/VST/synthyboy-arm32.so"
        _, xml_custom_path = gen_mpc_xpl.create_xpl_from_chunk(
            chunk_bytes=chunk_data,
            preset_name="Acid Saw Reso",
            template_info=tmpl_info,
            plugin_path=custom_plugin_path,
            num_programs=tmpl_info["header"]["num_programs"],
        )
        # Parse both and verify only plugin_attrs['file'] differs
        parsed_custom = gen_mpc_xpl.parse_xpl(xml_custom_path)
        self.assertEqual(parsed_custom["plugin_attrs"]["file"], custom_plugin_path)
        self.assertEqual(parsed_custom["preset"], tmpl_info["preset"])
        self.assertEqual(parsed_custom["version_file"], tmpl_info["version_file"])
        self.assertEqual(parsed_custom["version_date"], tmpl_info["version_date"])
        self.assertEqual(parsed_custom["state_bytes"], tmpl_info["state_bytes"])
        for k in tmpl_info["plugin_attrs"]:
            if k != "file":
                self.assertEqual(parsed_custom["plugin_attrs"][k], tmpl_info["plugin_attrs"][k])

        # 3. Overridden date: exactly and ONLY the 'date' attribute bytes differ
        custom_date = "2026-12-01-12-00"
        _, xml_custom_date = gen_mpc_xpl.create_xpl_from_chunk(
            chunk_bytes=chunk_data,
            preset_name="Acid Saw Reso",
            template_info=tmpl_info,
            date_override=custom_date,
            num_programs=tmpl_info["header"]["num_programs"],
        )
        parsed_date = gen_mpc_xpl.parse_xpl(xml_custom_date)
        self.assertEqual(parsed_date["version_date"], custom_date)
        self.assertEqual(parsed_date["plugin_attrs"], tmpl_info["plugin_attrs"])
        self.assertEqual(parsed_date["state_bytes"], tmpl_info["state_bytes"])


class TestPresetGenerationWithHelperOrFixtures(unittest.TestCase):
    """Test preset generation with native helper when available or recorded fixtures."""

    def test_cli_check_command(self):
        tmpl_path = get_example_path("prev_Acid_Saw_Reso.xpl")
        cmd = [sys.executable, str(SCRIPTS_DIR / "gen-mpc-xpl.py"), "--check", str(tmpl_path)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Check: OK", res.stdout)
        self.assertIn("Preset Name: Acid Saw Reso", res.stdout)
        self.assertIn("numPrograms:    113", res.stdout)

    def test_all_136_presets_with_helper_or_fixtures(self):
        # Look for built native helper and plugin module
        build_dirs = [REPO_ROOT / "build-native", REPO_ROOT / "build-mpc", REPO_ROOT / "build"]
        helper_bin = None
        plugin_bin = None
        for b in build_dirs:
            h = b / "shell-mpc" / "dump_mpc_chunks"
            if not h.is_file():
                h = b / "dump_mpc_chunks"
            if h.is_file() and os.access(h, os.X_OK):
                helper_bin = h
            for ext in [".dylib", ".so"]:
                p = b / "shell-mpc" / f"synthyboy_mpc{ext}"
                if p.is_file():
                    plugin_bin = p
                    break
            if helper_bin and plugin_bin:
                break

        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            chunks_dir = tmp_path / "chunks"
            chunks_dir.mkdir()
            out_dir = tmp_path / "xpl_out"

            if helper_bin and plugin_bin:
                print(f"[test] Running native helper {helper_bin} with {plugin_bin}")
                h_cmd = [str(helper_bin), str(plugin_bin), str(REPO_ROOT / "content" / "presets"), str(chunks_dir)]
                h_res = subprocess.run(h_cmd, capture_output=True, text=True)
                self.assertEqual(h_res.returncode, 0, f"Helper failed: {h_res.stderr}\n{h_res.stdout}")

                # Run gen-mpc-xpl.py
                tmpl_path = get_example_path("zz-sbtest-program.xpl")
                gen_cmd = [
                    sys.executable,
                    str(SCRIPTS_DIR / "gen-mpc-xpl.py"),
                    "--chunks",
                    str(chunks_dir),
                    "--template",
                    str(tmpl_path),
                    "--plugin-path",
                    "/sdcard/VST2 Plugins/Safian - VST - SynthyBoy/VST/synthyboy-arm32.so",
                    "--out",
                    str(out_dir),
                ]
                gen_res = subprocess.run(gen_cmd, capture_output=True, text=True)
                self.assertEqual(gen_res.returncode, 0, f"Generator failed: {gen_res.stderr}\n{gen_res.stdout}")

                # Verify exactly 136 .xpl files generated (the 112 of the first sets and the 24 of sound set batch 1)
                xpl_files = list(out_dir.glob("*.xpl"))
                self.assertEqual(len(xpl_files), 136, f"Expected 136 preset files, got {len(xpl_files)}")

                # Check each file has valid envelope and header, and decodes (dec.py) to the structure of the file the device wrote
                device_blob = TestDecodesLikeDeviceFiles._decode_with_dec_py(get_example_path("zz-sbtest-program.xpl"))
                device_structure = TestDecodesLikeDeviceFiles._structure(device_blob)
                for xf in xpl_files:
                    header, keys, param_keys = TestDecodesLikeDeviceFiles._structure(TestDecodesLikeDeviceFiles._decode_with_dec_py(xf))
                    # (a preset with user content, a wavetable or a sample of its own, has one more member, "content": the only difference allowed)
                    self.assertEqual((header, [k for k in keys if k != "content"], param_keys), device_structure, f"{xf.name}: structure differs from the device's file")
                    parsed = gen_mpc_xpl.parse_xpl(xf.read_text(encoding="utf-8"))
                    self.assertEqual(parsed["header"]["num_programs"], 137)
                    self.assertEqual(parsed["header"]["chunk_magic"], b"CcnK")
                    self.assertEqual(parsed["header"]["fx_magic"], b"FBCh")
                    self.assertGreater(len(parsed["chunk_bytes"]), 1000)
            else:
                # Fallback to recorded chunk fixtures in scripts/tests/fixtures/
                print(f"[test] Native helper not built; verifying using recorded fixtures in {FIXTURES_DIR}")
                tmpl_path = get_example_path("prev_Acid_Saw_Reso.xpl")
                gen_cmd = [
                    sys.executable,
                    str(SCRIPTS_DIR / "gen-mpc-xpl.py"),
                    "--chunks",
                    str(FIXTURES_DIR),
                    "--template",
                    str(tmpl_path),
                    "--out",
                    str(out_dir),
                ]
                gen_res = subprocess.run(gen_cmd, capture_output=True, text=True)
                self.assertEqual(gen_res.returncode, 0, f"Generator failed: {gen_res.stderr}\n{gen_res.stdout}")
                xpl_files = list(out_dir.glob("*.xpl"))
                self.assertGreaterEqual(len(xpl_files), 1)
                self.assertTrue((out_dir / "Acid Saw Reso.xpl").is_file())
                parsed = gen_mpc_xpl.parse_xpl((out_dir / "Acid Saw Reso.xpl").read_text(encoding="utf-8"))
                self.assertEqual(parsed["header"]["num_programs"], 2)   # the chunks of the set (1) and Init

    def test_create_xpl_derives_137_programs_by_default(self):
        tmpl = gen_mpc_xpl.parse_xpl(get_example_path("prev_Acid_Saw_Reso.xpl").read_text(encoding="utf-8"))
        self.assertEqual(tmpl["header"]["num_programs"], 113)  # template has old 113
        chunk = (FIXTURES_DIR / "acid_saw_reso.chunk").read_bytes()
        name = "Acid Saw Reso"
        # Without num_programs override, it derives 137 from factory presets count (136) + 1 (Init)
        _, xml_text = gen_mpc_xpl.create_xpl_from_chunk(chunk, name, tmpl)
        parsed = gen_mpc_xpl.parse_xpl(xml_text)
        self.assertEqual(parsed["header"]["num_programs"], 137)

    def test_create_xpl_override_num_programs(self):
        tmpl = gen_mpc_xpl.parse_xpl(get_example_path("prev_Acid_Saw_Reso.xpl").read_text(encoding="utf-8"))
        chunk = (FIXTURES_DIR / "acid_saw_reso.chunk").read_bytes()
        name = "Acid Saw Reso"
        _, xml_text = gen_mpc_xpl.create_xpl_from_chunk(chunk, name, tmpl, num_programs=150)
        parsed = gen_mpc_xpl.parse_xpl(xml_text)
        self.assertEqual(parsed["header"]["num_programs"], 150)

    def test_cli_num_programs_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            tmpl_path = get_example_path("prev_Acid_Saw_Reso.xpl")
            cmd = [
                sys.executable,
                str(SCRIPTS_DIR / "gen-mpc-xpl.py"),
                "--chunks", str(FIXTURES_DIR),
                "--template", str(tmpl_path),
                "--out", str(out_dir),
                "--num-programs", "150",
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stderr)
            gen_file = out_dir / "Acid Saw Reso.xpl"
            self.assertTrue(gen_file.is_file())
            parsed = gen_mpc_xpl.parse_xpl(gen_file.read_text(encoding="utf-8"))
            self.assertEqual(parsed["header"]["num_programs"], 150)

    def test_num_programs_cli_option(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            out_dir = Path(tmp_dir) / "out"
            tmpl_path = get_example_path("prev_Acid_Saw_Reso.xpl")
            gen_cmd = [
                sys.executable,
                str(SCRIPTS_DIR / "gen-mpc-xpl.py"),
                "--chunks",
                str(FIXTURES_DIR),
                "--template",
                str(tmpl_path),
                "--num-programs",
                "137",
                "--out",
                str(out_dir),
            ]
            res = subprocess.run(gen_cmd, capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"Generator failed: {res.stderr}\n{res.stdout}")
            f = out_dir / "Acid Saw Reso.xpl"
            self.assertTrue(f.is_file())
            parsed = gen_mpc_xpl.parse_xpl(f.read_text(encoding="utf-8"))
            self.assertEqual(parsed["header"]["num_programs"], 137)


class TestDecodesLikeDeviceFiles(unittest.TestCase):
    """A generated file must decode with the investigation's decoder (dec.py) to the same structure as a file the device wrote."""

    @staticmethod
    def _decode_with_dec_py(xpl_path: Path) -> bytes:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "state.bin"
            res = subprocess.run([sys.executable, "-I", str(DEC_PY), str(xpl_path), str(out)], capture_output=True, text=True)
            assert res.returncode == 0, res.stderr
            line = res.stdout.splitlines()[0].split()
            assert line[0] == line[1], f"dec.py: declared length {line[0]} != decoded {line[1]}"
            return out.read_bytes()

    @staticmethod
    def _structure(blob: bytes):
        """What must be the same: the 160 byte bank header (except chunkSize and numPrograms), the order of the keys of the JSON chunk and the set of the parameter keys."""
        header = blob[:24] + blob[28:156]
        chunk_size = int.from_bytes(blob[156:160], "big")
        assert chunk_size == len(blob) - 160
        doc = json.loads(blob[160:].decode("utf-8"))
        return header, list(doc.keys()), list(doc["params"].keys())

    def test_generated_files_decode_to_the_structure_of_the_device_files(self):
        pairs = [("zz-sbtest-program.xpl", "zz_harp_drops.chunk"), ("prev_Acid_Saw_Reso.xpl", "acid_saw_reso.chunk")]
        for device_name, chunk_name in pairs:
            device_xpl = get_example_path(device_name)
            device_blob = self._decode_with_dec_py(device_xpl)
            for template_name in ("zz-sbtest-program.xpl", "prev_Acid_Saw_Reso.xpl"):   # either file is a good template
                tmpl = gen_mpc_xpl.parse_xpl(get_example_path(template_name).read_text(encoding="utf-8"))
                chunk = (FIXTURES_DIR / chunk_name).read_bytes()
                name = json.loads(chunk.decode("utf-8"))["mpcProgramName"]
                _, xml_text = gen_mpc_xpl.create_xpl_from_chunk(chunk, name, tmpl, num_programs=tmpl["header"]["num_programs"])
                with tempfile.TemporaryDirectory() as tmp:
                    generated = Path(tmp) / "generated.xpl"
                    generated.write_bytes(xml_text.encode("utf-8"))
                    gen_blob = self._decode_with_dec_py(generated)
                    self.assertEqual(self._structure(gen_blob), self._structure(device_blob), f"{chunk_name} through template {template_name}: structure differs from {device_name}")
                    self.assertEqual(gen_blob, device_blob, f"{chunk_name} through template {template_name}: the decoded state differs from the one the device wrote in {device_name}")
                    # the envelope: the lines of the template (date, plugin attributes, file times are the device's) but the preset name and the state
                    gen_lines = generated.read_bytes().decode("utf-8").split("\r\n")
                    tmpl_lines = get_example_path(template_name).read_bytes().decode("utf-8").split("\r\n")
                    self.assertEqual(len(gen_lines), len(tmpl_lines))
                    differing = [i for i, (a, b) in enumerate(zip(gen_lines, tmpl_lines)) if a != b]
                    self.assertEqual([i for i in differing if not tmpl_lines[i].lstrip().startswith(("<preset>", "<state>"))], [], f"envelope lines differ from the template {template_name}")
                    self.assertTrue(all(gen_lines[i].lstrip().startswith(("<preset>", "<state>")) for i in differing))

    def test_preset_names_are_xml_escaped(self):
        tmpl = gen_mpc_xpl.parse_xpl(get_example_path("prev_Acid_Saw_Reso.xpl").read_text(encoding="utf-8"))
        chunk = (FIXTURES_DIR / "acid_saw_reso.chunk").read_bytes()
        _, xml_text = gen_mpc_xpl.create_xpl_from_chunk(chunk, "Rock & <Roll> \"Bass\" 'n'", tmpl)
        import xml.etree.ElementTree as ET
        root = ET.fromstring(xml_text.encode("utf-8"))
        self.assertEqual(root.find("preset").text, "Rock & <Roll> \"Bass\" 'n'", "a name with markup characters must survive as text")
        parsed = gen_mpc_xpl.parse_xpl(xml_text)
        self.assertEqual(parsed["preset"], "Rock & <Roll> \"Bass\" 'n'", "parse_xpl gives the name back unescaped")

    def test_generated_file_is_well_formed_xml_with_the_device_root(self):
        import xml.etree.ElementTree as ET
        for name in ("zz-sbtest-program.xpl", "prev_Acid_Saw_Reso.xpl"):
            root = ET.fromstring(get_example_path(name).read_bytes())
            self.assertEqual(root.tag, "pluginstate")
            self.assertEqual([c.tag for c in root], ["version", "PLUGIN", "preset", "state"])


if __name__ == "__main__":
    unittest.main()
