#!/usr/bin/env python3
# SynthyBoy - generates an MPC plugin.json from the root plugin.json (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/gen-mpc-manifest.py [--out DIR | --check FILE] [--manifest PATH]
#
# Reads the repository root plugin.json and writes an MPC SDK-format plugin.json
# (see docs/plugin-folder-format.md and schemas/plugin.schema.json in the MPC plugin SDK).

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
        "description": root_manifest.get("description", ""),
        "author": root_manifest.get("author", ""),
        "license": root_manifest.get("license", ""),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generates an MPC plugin.json (see the header)")
    parser.add_argument("--out", help="write plugin.json into DIR")
    parser.add_argument("--check", help="compare generated output to FILE (no write)")
    parser.add_argument("--manifest", default=None,
                        help="path to the root manifest (default: plugin.json next to this script)")
    args = parser.parse_args(argv)

    if not args.out and not args.check:
        parser.error("one of --out or --check is required")

    if args.manifest:
        manifest_path = Path(args.manifest)
    else:
        manifest_path = ROOT / "plugin.json"

    try:
        root = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("gen-mpc-manifest: cannot read %s: %s" % (manifest_path, e), file=sys.stderr)
        return 1

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
