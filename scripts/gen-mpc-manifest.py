#!/usr/bin/env python3
# SynthyBoy - generates an MPC plugin.json from the root plugin.json (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/gen-mpc-manifest.py [--out DIR | --check FILE | --plugin-dir DIR] [--manifest PATH]
#        python3 -I scripts/gen-mpc-manifest.py --package-manifest FILE (--folder-name | --plugin-dir DIR)
#
# Modes:
#   --out DIR          write plugin.json into DIR (from root manifest)
#   --check FILE       compare generated output to FILE (no write)
#   --plugin-dir DIR   fill a plugin folder that already holds binaries in DIR/VST/:
#                      write plugin.json with file32/file64 only for existing .so files,
#                      and version.xml with the root manifest version (<major>.<minor>.<patch>.<build>).
#                      Exits 2 on error.
#
# Another plugin (migration step 12 of docs/ui-components/TEMPLATE.md; scripts/build-mpc.sh --plugin DIR): --package-manifest FILE is that plugin's finished plugin.json (template-mpc/package/plugin.json):
#   --folder-name      print the name of its plugin folder ("<manufacturer> - VST - <name>", forbidden characters removed)
#   --plugin-dir DIR   write plugin.json = FILE without the file32 / file64 whose binary is not in DIR, and version.xml (the version of FILE + ".0", uiversion when DIR has a skin)
# The SynthyBoy modes above are unchanged.

import argparse
import json
import sys
from pathlib import Path

FORBIDDEN = {'"', '#', '@', ',', ';', ':', '<', '>', '*', '^', '|', '?', '\\', '/'}

ROOT = Path(__file__).resolve().parent.parent


def folder_name(manufacturer, name):
    """Return the MPC plugin folder name with forbidden characters removed."""
    if not manufacturer:
        return "VST - %s" % name
    cleaned = "".join(c for c in manufacturer if c not in FORBIDDEN)
    cleaned_name = "".join(c for c in name if c not in FORBIDDEN)
    return "%s - VST - %s" % (cleaned, cleaned_name)


# The MPC package is the own engine behind the VST2 ABI: the root manifest's description (CLAP / VST3, Vital-derived engine) is not true of it.
MPC_DESCRIPTION = "Wavetable synthesizer for MPC"


def generate(root_manifest):
    """Build the MPC plugin.json dict from the root manifest."""
    return {
        "name": root_manifest["name"],
        "manufacturer": "Safian",
        "uid": "SfSB",
        "format": "vst2",
        "kind": "instrument",
        "numInputs": 0,
        "numOutputs": 2,
        "file32": "VST/synthyboy-arm32.so",
        "file64": "VST/synthyboy-arm64.so",
        "version": root_manifest["version"],
        "build": root_manifest.get("build", 1),
        "description": MPC_DESCRIPTION,
        "author": root_manifest.get("author", ""),
        "license": root_manifest.get("license", ""),
    }


def write_version_xml(plugin_dir, version):
    """version.xml of a plugin folder: the version with a fourth part, the uiversion when the folder has a skin."""
    parts = version.split(".")
    while len(parts) < 4:
        parts.append("0")
    lines = ['<?xml version="1.0" encoding="utf-8"?>', '<plugincontent>', '    <version>%s</version>' % (".".join(parts[:3]) + ".0")]
    if (plugin_dir / "Plugin Skins" / "TUI.json").is_file():
        lines.append('    <uiversion>0.1.0.0</uiversion>')
    lines.append('</plugincontent>')
    path = plugin_dir / "version.xml"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def package_mode(args, parser):
    """The modes of another plugin whose plugin.json is finished (--package-manifest)."""
    try:
        manifest = json.loads(Path(args.package_manifest).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("gen-mpc-manifest: cannot read %s: %s" % (args.package_manifest, e), file=sys.stderr)
        return 1
    if args.folder_name == bool(args.plugin_dir):
        parser.error("--package-manifest needs exactly one of --folder-name and --plugin-dir")
    if args.folder_name:
        print(folder_name(manifest.get("manufacturer", ""), manifest["name"]))
        return 0
    plugin_dir = Path(args.plugin_dir)
    if not plugin_dir.is_dir():
        print("gen-mpc-manifest: %s is not a directory" % plugin_dir, file=sys.stderr)
        return 2
    present = 0
    for key in ("file32", "file64"):
        if key in manifest and not (plugin_dir / manifest[key]).is_file():
            del manifest[key]
        elif key in manifest:
            present += 1
    if present == 0:
        print("gen-mpc-manifest: none of the binaries named in %s is in %s" % (args.package_manifest, plugin_dir), file=sys.stderr)
        return 2
    plugin_json = plugin_dir / "plugin.json"
    plugin_json.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    version_xml = write_version_xml(plugin_dir, manifest["version"])
    print("wrote %s and %s" % (plugin_json, version_xml))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generates an MPC plugin.json (see the header)")
    parser.add_argument("--out", help="write plugin.json into DIR")
    parser.add_argument("--check", help="compare generated output to FILE (no write)")
    parser.add_argument("--plugin-dir", help="fill a plugin folder that already holds binaries in DIR/VST/")
    parser.add_argument("--manifest", default=None,
                        help="path to the root manifest (default: plugin.json next to this script)")
    parser.add_argument("--package-manifest", default=None,
                        help="another plugin's finished plugin.json: with --folder-name or --plugin-dir")
    parser.add_argument("--folder-name", action="store_true", help="with --package-manifest: print the plugin folder name")
    args = parser.parse_args(argv)

    if args.package_manifest:
        return package_mode(args, parser)
    if args.folder_name:
        parser.error("--folder-name needs --package-manifest")

    modes = sum([bool(args.out), bool(args.check), bool(args.plugin_dir)])
    if modes == 0:
        parser.error("one of --out, --check, or --plugin-dir is required")
    if modes > 1:
        parser.error("only one of --out, --check, --plugin-dir may be specified")

    if args.manifest:
        manifest_path = Path(args.manifest)
    else:
        manifest_path = ROOT / "plugin.json"

    try:
        root = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("gen-mpc-manifest: cannot read %s: %s" % (manifest_path, e), file=sys.stderr)
        return 1

    # --plugin-dir mode
    if args.plugin_dir:
        plugin_dir = Path(args.plugin_dir)
        if not plugin_dir.is_dir():
            print("gen-mpc-manifest: %s is not a directory" % plugin_dir, file=sys.stderr)
            return 2

        vst_dir = plugin_dir / "VST"
        has_32 = (vst_dir / "synthyboy-arm32.so").is_file()
        has_64 = (vst_dir / "synthyboy-arm64.so").is_file()

        if not has_32 and not has_64:
            print("gen-mpc-manifest: no binary found in %s/VST/" % plugin_dir, file=sys.stderr)
            return 2

        # plugin.json with only the binaries that exist
        mpc = generate(root)
        if not has_32:
            del mpc["file32"]
        if not has_64:
            del mpc["file64"]

        plugin_json_path = plugin_dir / "plugin.json"
        plugin_json_path.write_text(json.dumps(mpc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

        # Build version.xml
        version_parts = root["version"].split(".")
        while len(version_parts) < 3:
            version_parts.append("0")
        build_val = root.get("build", 1)
        version_text = "%s.%s.%s.%s" % (version_parts[0], version_parts[1], version_parts[2], build_val)

        has_skins = (plugin_dir / "Plugin Skins" / "TUI.json").is_file()

        xml_lines = ['<?xml version="1.0" encoding="utf-8"?>']
        xml_lines.append('<plugincontent>')
        xml_lines.append('    <version>%s</version>' % version_text)
        if has_skins:
            xml_lines.append('    <uiversion>0.1.0.0</uiversion>')
        xml_lines.append('</plugincontent>')
        version_xml_path = plugin_dir / "version.xml"
        version_xml_path.write_text("\n".join(xml_lines) + "\n", encoding="utf-8")

        print("wrote %s and %s" % (plugin_json_path, version_xml_path))
        return 0

    # --out / --check modes (unchanged)
    mpc = generate(root)
    text = json.dumps(mpc, indent=2, ensure_ascii=False) + "\n"

    if args.check:
        check_path = Path(args.check)
        try:
            existing = check_path.read_text(encoding="utf-8")
        except OSError as e:
            print("gen-mpc-manifest: cannot read %s: %s" % (check_path, e), file=sys.stderr)
            return 1
        if existing == text:
            return 0
        print("gen-mpc-manifest: %s differs from generated output" % check_path, file=sys.stderr)
        return 1

    out_path = Path(args.out) / "plugin.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text, encoding="utf-8")
    print("wrote %s" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
