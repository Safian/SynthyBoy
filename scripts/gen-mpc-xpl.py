#!/usr/bin/env python3
# SynthyBoy - Akai MPC .xpl Plugin Program generator and inspector
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Generator and inspector for Akai MPC .xpl Plugin Program preset files.

The Akai MPC host stores plugin preset programs in .xpl files (UTF-8 XML with
CRLF line endings). The plugin state inside the <state> element contains JUCE's
custom base64 encoding of a 160-byte VST2 fxBank header followed by the plugin's
JSON state chunk (returned by effGetChunk).
"""

import argparse
import importlib.util
import json
import os
import re
import struct
import sys
from pathlib import Path
from xml.sax.saxutils import escape as xml_escape, unescape as xml_unescape
from typing import Any, Dict, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
_pv_spec = importlib.util.spec_from_file_location("product_version", HERE / "product_version.py")
_pv_mod = importlib.util.module_from_spec(_pv_spec)
_pv_spec.loader.exec_module(_pv_mod)

# JUCE MemoryBlock::toBase64Encoding() alphabet (64 characters, '.' represents 0)
JUCE_BASE64_ALPHABET = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"

# VST2 fxBank header constants
VST2_CHUNK_MAGIC = b"CcnK"  # 0x43636e4b: VST chunk magic
VST2_FX_MAGIC_BANK = b"FBCh"  # 0x46424368: Bank chunk magic (opaque bank)
DEFAULT_FX_ID = b"SfSB"  # Plugin unique ID ('S','f','S','B' -> 0x53665342)
DEFAULT_FX_VERSION = _pv_mod.load(HERE.parent)[2]
DEFAULT_NUM_PROGRAMS = 137
BANK_HEADER_SIZE = 160


def count_factory_presets(presets_dir: Optional[Path] = None) -> int:
    """Count the .sbpreset files in presets_dir (default content/presets)."""
    pdir = presets_dir if presets_dir is not None else (Path(__file__).resolve().parent.parent / "content" / "presets")
    if pdir.is_dir():
        return len(list(pdir.glob("*/*.sbpreset")))
    return 136


def default_num_programs(presets_dir: Optional[Path] = None) -> int:
    """Number of programs: factory preset count + 1 (Init program)."""
    return count_factory_presets(presets_dir) + 1


# The preset name is text of an XML element: markup characters are written as entities (the host's XML writer is JUCE's; & < > are the ones that matter for a text node, the quotes are
# written as entities too, the way JUCE does), and parse_xpl () gives the name back unescaped.
_XML_TEXT_ENTITIES = {'"': "&quot;", "'": "&apos;"}
_XML_TEXT_UNENTITIES = {"&quot;": '"', "&apos;": "'"}


def encode_juce_base64(data: bytes) -> str:
    """Encode bytes into JUCE's custom base64 string format.

    Format: '<byte count>.<encoded characters>'
    Bits are packed 6 bits per character, least significant bit first.
    """
    out = [f"{len(data)}."]
    bits = 0
    nb = 0
    for b in data:
        bits |= b << nb
        nb += 8
        while nb >= 6:
            out.append(JUCE_BASE64_ALPHABET[bits & 0x3F])
            bits >>= 6
            nb -= 6
    if nb > 0:
        out.append(JUCE_BASE64_ALPHABET[bits & 0x3F])
    return "".join(out)


def decode_juce_base64(encoded: str) -> bytes:
    """Decode a JUCE custom base64 string into bytes.

    Format: '<byte count>.<encoded characters>'
    Bits are unpacked 6 bits per character, least significant bit first.
    """
    if "." not in encoded:
        raise ValueError("Invalid JUCE base64 string: missing length prefix dot separator")
    length_str, rest = encoded.split(".", 1)
    try:
        expected_len = int(length_str)
    except ValueError as e:
        raise ValueError(f"Invalid JUCE base64 length prefix: {length_str!r}") from e

    bits = 0
    nb = 0
    out = bytearray()
    for c in rest:
        v = JUCE_BASE64_ALPHABET.find(c)
        if v < 0:
            raise ValueError(f"Invalid character {c!r} in JUCE base64 string")
        bits |= v << nb
        nb += 6
        while nb >= 8:
            out.append(bits & 0xFF)
            bits >>= 8
            nb -= 8
    return bytes(out[:expected_len])


def parse_bank_header(header_bytes: bytes) -> Dict[str, Any]:
    """Parse the 160-byte VST2 fxBank header.

    Binary layout (all multi-byte integers are big-endian):
      offset  0, 4 bytes: chunkMagic ('CcnK')
      offset  4, 4 bytes: byteSize (int32, size of remaining header or 0)
      offset  8, 4 bytes: fxMagic ('FBCh' for opaque bank chunk)
      offset 12, 4 bytes: version (int32, format version, usually 1 or 2)
      offset 16, 4 bytes: fxID (4 chars, plugin unique ID, e.g. 'SfSB')
      offset 20, 4 bytes: fxVersion (int32, plugin version)
      offset 24, 4 bytes: numPrograms (int32, number of programs)
      offset 28, 4 bytes: currentProgram (int32, active program index)
      offset 32, 124 bytes: future (reserved / unused padding, zeros)
      offset 156, 4 bytes: chunkSize (uint32, size of subsequent chunk payload)
    """
    if len(header_bytes) != BANK_HEADER_SIZE:
        raise ValueError(f"Bank header must be exactly {BANK_HEADER_SIZE} bytes, got {len(header_bytes)}")

    chunk_magic = header_bytes[0:4]
    byte_size = struct.unpack(">i", header_bytes[4:8])[0]
    fx_magic = header_bytes[8:12]
    version = struct.unpack(">i", header_bytes[12:16])[0]
    fx_id = header_bytes[16:20]
    fx_version = struct.unpack(">i", header_bytes[20:24])[0]
    num_programs = struct.unpack(">i", header_bytes[24:28])[0]
    current_program = struct.unpack(">i", header_bytes[28:32])[0]
    future = header_bytes[32:156]
    chunk_size = struct.unpack(">I", header_bytes[156:160])[0]

    return {
        "chunk_magic": chunk_magic,
        "byte_size": byte_size,
        "fx_magic": fx_magic,
        "version": version,
        "fx_id": fx_id,
        "fx_version": fx_version,
        "num_programs": num_programs,
        "current_program": current_program,
        "future": future,
        "chunk_size": chunk_size,
    }


def build_bank_header(
    chunk_size: int,
    num_programs: Optional[int] = None,
    fx_id: bytes = DEFAULT_FX_ID,
    fx_version: int = DEFAULT_FX_VERSION,
    current_program: int = 0,
    byte_size: int = 0,
    version: int = 1,
) -> bytes:
    """Build the 160-byte VST2 fxBank header.

    Analysis of device-saved files confirms that `chunk_size` at offset 156 is the
    only header field that depends on the chunk length.
    """
    if num_programs is None:
        num_programs = DEFAULT_NUM_PROGRAMS
    if len(fx_id) != 4:
        raise ValueError(f"fx_id must be exactly 4 bytes, got {fx_id!r}")

    hdr = bytearray(BANK_HEADER_SIZE)
    hdr[0:4] = VST2_CHUNK_MAGIC
    struct.pack_into(">i", hdr, 4, byte_size)
    hdr[8:12] = VST2_FX_MAGIC_BANK
    struct.pack_into(">i", hdr, 12, version)
    hdr[16:20] = fx_id
    struct.pack_into(">i", hdr, 20, fx_version)
    struct.pack_into(">i", hdr, 24, num_programs)
    struct.pack_into(">i", hdr, 28, current_program)
    # bytes 32..156 remain zero (124 bytes of future/reserved space)
    struct.pack_into(">I", hdr, 156, chunk_size)
    return bytes(hdr)


def parse_xpl(content: str) -> Dict[str, Any]:
    """Parse an .xpl file content into structured envelope fields and state."""
    version_match = re.search(r'<version\s+file="([^"]*)"\s+date="([^"]*)"\s*/>', content)
    if not version_match:
        raise ValueError("Cannot find valid <version> element in .xpl")
    version_file, version_date = version_match.groups()

    plugin_match = re.search(r"<PLUGIN\s+(.*?)\s*/>", content, re.S)
    if not plugin_match:
        raise ValueError("Cannot find valid <PLUGIN> element in .xpl")
    plugin_attrs_str = plugin_match.group(1)
    attr_pattern = re.compile(r'(\w+)="([^"]*)"')
    plugin_attrs = dict(attr_pattern.findall(plugin_attrs_str))

    preset_match = re.search(r"<preset>(.*?)</preset>", content, re.S)
    if not preset_match:
        raise ValueError("Cannot find <preset> element in .xpl")
    preset = xml_unescape(preset_match.group(1), _XML_TEXT_UNENTITIES)

    state_match = re.search(r"<state>(.*?)</state>", content, re.S)
    if not state_match:
        raise ValueError("Cannot find <state> element in .xpl")
    state_b64 = state_match.group(1).strip()

    state_bytes = decode_juce_base64(state_b64)
    if len(state_bytes) < BANK_HEADER_SIZE:
        raise ValueError(
            f"Decoded state length ({len(state_bytes)}) is smaller than bank header size ({BANK_HEADER_SIZE})"
        )
    header = parse_bank_header(state_bytes[:BANK_HEADER_SIZE])
    chunk_payload = state_bytes[BANK_HEADER_SIZE : BANK_HEADER_SIZE + header["chunk_size"]]

    return {
        "version_file": version_file,
        "version_date": version_date,
        "plugin_attrs": plugin_attrs,
        "preset": preset,
        "state_b64": state_b64,
        "state_bytes": state_bytes,
        "header": header,
        "chunk_bytes": chunk_payload,
    }


def render_xpl(
    version_file: str,
    version_date: str,
    plugin_attrs: Dict[str, str],
    preset: str,
    state_b64: str,
) -> str:
    """Render .xpl XML text matching host formatting byte-for-byte.

    Matches attribute order, line splits, 10-space continuation indent, and CRLF line ends.
    """
    p = plugin_attrs
    name = p.get("name", "")
    fmt = p.get("format", "")
    cat = p.get("category", "")
    man = p.get("manufacturer", "")
    ver = p.get("version", "")
    file_path = p.get("file", "")
    uid = p.get("uid", "")
    is_inst = p.get("isInstrument", "")
    file_time = p.get("fileTime", "")
    info_time = p.get("infoUpdateTime", "")
    num_in = p.get("numInputs", "")
    num_out = p.get("numOutputs", "")
    is_shell = p.get("isShell", "")

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        "",
        "<pluginstate>",
        f'  <version file="{version_file}" date="{version_date}"/>',
        f'  <PLUGIN name="{name}" format="{fmt}" category="{cat}" manufacturer="{man}"',
        f'          version="{ver}" file="{file_path}"',
        f'          uid="{uid}" isInstrument="{is_inst}" fileTime="{file_time}" infoUpdateTime="{info_time}"',
        f'          numInputs="{num_in}" numOutputs="{num_out}" isShell="{is_shell}"/>',
        f"  <preset>{xml_escape(preset, _XML_TEXT_ENTITIES)}</preset>",
        f"  <state>{state_b64}</state>",
        "</pluginstate>",
        "",
    ]
    return "\r\n".join(lines)


def sanitize_filename(name: str) -> str:
    """Make preset name safe for FAT/exFAT filesystems.

    Forbidden characters: < > : " / \\ | ? * and ASCII control characters (0-31).
    Trailing spaces, dashes, and dots are stripped, and consecutive dashes collapsed.
    Returns '<Safe Name>.xpl'.
    """
    cleaned = []
    for c in name:
        if c in '<>:"/\\|?*' or ord(c) < 32:
            cleaned.append("-")
        else:
            cleaned.append(c)
    safe = "".join(cleaned)
    safe = re.sub(r"-+", "-", safe).strip(" .-")
    if not safe:
        safe = "Preset"
    return f"{safe}.xpl"


def create_xpl_from_chunk(
    chunk_bytes: bytes,
    preset_name: str,
    template_info: Dict[str, Any],
    plugin_path: Optional[str] = None,
    date_override: Optional[str] = None,
    num_programs: Optional[int] = None,
    presets_dir: Optional[Path] = None,
) -> Tuple[str, str]:
    """Create an .xpl XML string and its safe filename from a raw chunk and template."""
    attrs = dict(template_info["plugin_attrs"])
    if plugin_path is not None:
        attrs["file"] = plugin_path

    version_date = date_override if date_override is not None else template_info["version_date"]
    version_file = template_info["version_file"]

    # Reconstruct bank header with derived or overridden num_programs
    if num_programs is None:
        num_programs = default_num_programs(presets_dir)

    tmpl_hdr = template_info.get("header", {})
    fx_id = tmpl_hdr.get("fx_id", DEFAULT_FX_ID)
    fx_version = tmpl_hdr.get("fx_version", DEFAULT_FX_VERSION)

    header = build_bank_header(
        chunk_size=len(chunk_bytes),
        num_programs=num_programs,
        fx_id=fx_id,
        fx_version=fx_version,
        current_program=0,
    )
    full_state = header + chunk_bytes
    state_b64 = encode_juce_base64(full_state)

    xml_text = render_xpl(
        version_file=version_file,
        version_date=version_date,
        plugin_attrs=attrs,
        preset=preset_name,
        state_b64=state_b64,
    )
    filename = sanitize_filename(preset_name)
    return filename, xml_text


def inspect_xpl_file(file_path: Path) -> int:
    """Inspect and print the components of an .xpl file."""
    try:
        content = file_path.read_text(encoding="utf-8")
        parsed = parse_xpl(content)
    except Exception as e:
        sys.stderr.write(f"Error checking {file_path}: {e}\n")
        return 1

    print(f"File: {file_path}")
    print(f"Preset Name: {parsed['preset']}")
    print(f"Version: file={parsed['version_file']} date={parsed['version_date']}")
    print("Plugin Attributes:")
    for k, v in parsed["plugin_attrs"].items():
        print(f"  {k} = {v}")

    hdr = parsed["header"]
    print("VST2 Bank Header (160 bytes):")
    print(f"  chunkMagic:     {hdr['chunk_magic']!r}")
    print(f"  byteSize:       {hdr['byte_size']}")
    print(f"  fxMagic:        {hdr['fx_magic']!r}")
    print(f"  version:        {hdr['version']}")
    print(f"  fxID:           {hdr['fx_id']!r} ({hdr['fx_id'].hex()})")
    print(f"  fxVersion:      {hdr['fx_version']}")
    print(f"  numPrograms:    {hdr['num_programs']}")
    print(f"  currentProgram: {hdr['current_program']}")
    print(f"  chunkSize:      {hdr['chunk_size']} bytes")
    print(f"Total Decoded State Size: {len(parsed['state_bytes'])} bytes")

    chunk_bytes = parsed["chunk_bytes"]
    print(f"Plugin Chunk Size: {len(chunk_bytes)} bytes")
    try:
        chunk_json = json.loads(chunk_bytes.decode("utf-8"))
        print("Plugin Chunk JSON:")
        print(f"  mpcProgram:           {chunk_json.get('mpcProgram')}")
        print(f"  mpcProgramName:       {chunk_json.get('mpcProgramName')}")
        print(f"  format:               {chunk_json.get('format')}")
        print(f"  version:              {chunk_json.get('version')}")
        print(f"  params count:         {len(chunk_json.get('params', {}))}")
        if "mpcPadPressure" in chunk_json:
            print(f"  mpcPadPressure:       {chunk_json.get('mpcPadPressure')}")
        if "mpcPadPressureAmount" in chunk_json:
            print(f"  mpcPadPressureAmount: {chunk_json.get('mpcPadPressureAmount')}")
    except Exception as e:
        print(f"  [Chunk is not plain JSON: {e}]")

    print("Check: OK")
    return 0


def load_chunk_entries(chunks_dir: Path) -> List[Tuple[str, bytes]]:
    """Load chunk files from chunks directory.

    Returns list of (preset_name, chunk_bytes) pairs sorted by program index/name.
    """
    entries = []

    # Check for manifest.json
    manifest_path = chunks_dir / "manifest.json"
    if manifest_path.is_file():
        try:
            items = json.loads(manifest_path.read_text(encoding="utf-8"))
            for item in items:
                idx = item.get("index", -1)
                name = item.get("name", "")
                cf = chunks_dir / item.get("chunk_file", "")
                if idx == 0 and name.lower() == "init":
                    continue  # skip default program 0 Init
                if cf.is_file():
                    data = cf.read_bytes()
                    entries.append((name, data))
            if entries:
                return entries
        except Exception:
            pass

    # Search for *.chunk files
    chunk_files = sorted(chunks_dir.glob("*.chunk"))
    if not chunk_files:
        chunk_files = sorted(chunks_dir.glob("*.json"))

    for cf in chunk_files:
        if cf.name == "manifest.json":
            continue
        data = cf.read_bytes()
        # Try to extract preset name from chunk JSON
        name = None
        try:
            cj = json.loads(data.decode("utf-8"))
            prog_idx = cj.get("mpcProgram")
            if prog_idx == 0:
                continue  # skip Init
            name = cj.get("mpcProgramName")
        except Exception:
            pass

        # Check for companion .name file
        name_file = cf.with_suffix(".name")
        if not name and name_file.is_file():
            name = name_file.read_text(encoding="utf-8").strip()

        if not name:
            # Fallback to file stem without index prefix
            stem = cf.stem
            stem = re.sub(r"^(program_|chunk_|prog_)?\d+_", "", stem)
            name = stem

        if name.lower() == "init":
            continue

        entries.append((name, data))

    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate and inspect Akai MPC .xpl Plugin Program files")
    parser.add_argument("--chunks", type=Path, help="Directory containing preset chunk files")
    parser.add_argument("--template", type=Path, help="Template .xpl file to source XML envelope and attributes from")
    parser.add_argument("--plugin-path", type=str, help="Override path to plugin binary on device (<PLUGIN file=...>)")
    parser.add_argument("--date", type=str, help="Override date attribute in <version date=...>")
    parser.add_argument(
        "--num-programs",
        type=int,
        default=None,
        help="Number of programs in bank header (default: preset count + 1)",
    )
    parser.add_argument("--out", type=Path, help="Output directory for generated .xpl files")
    parser.add_argument("--check", type=Path, help="Inspect and verify an existing .xpl file")

    args = parser.parse_args()

    if args.check:
        return inspect_xpl_file(args.check)

    if not args.chunks or not args.template or not args.out:
        parser.error("--chunks, --template, and --out are required for generating .xpl files")

    if not args.chunks.is_dir():
        sys.stderr.write(f"Error: Chunks directory does not exist: {args.chunks}\n")
        return 1

    if not args.template.is_file():
        sys.stderr.write(f"Error: Template file does not exist: {args.template}\n")
        return 1

    template_info = parse_xpl(args.template.read_text(encoding="utf-8"))
    entries = load_chunk_entries(args.chunks)

    if not entries:
        sys.stderr.write(f"Error: No valid preset chunks found in {args.chunks}\n")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)

    computed_programs = len(entries) + 1 if entries else DEFAULT_NUM_PROGRAMS
    target_num_programs = args.num_programs if args.num_programs is not None else computed_programs

    # Collision detection for FAT/exFAT filesystem (case-insensitive filename check)
    seen_files: Dict[str, str] = {}
    generated_count = 0

    for preset_name, chunk_data in entries:
        filename, xml_content = create_xpl_from_chunk(
            chunk_bytes=chunk_data,
            preset_name=preset_name,
            template_info=template_info,
            plugin_path=args.plugin_path,
            date_override=args.date,
            num_programs=target_num_programs,
        )

        fn_key = filename.lower()
        if fn_key in seen_files:
            sys.stderr.write(
                f"Error: FAT/exFAT filename collision: '{preset_name}' and '{seen_files[fn_key]}' "
                f"both map to '{filename}'\n"
            )
            return 1
        seen_files[fn_key] = preset_name

        out_file = args.out / filename
        out_file.write_bytes(xml_content.encode("utf-8"))
        generated_count += 1

    print(f"Successfully generated {generated_count} .xpl files in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
