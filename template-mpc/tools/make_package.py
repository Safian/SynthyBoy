#!/usr/bin/env python3
# MpcKit Demo - assembles the plugin folder the MPC loads: "<Vendor> - VST - <Name>/" with VST/*.so, Plugin Skins/, plugin.json and version.xml (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 template-mpc/tools/make_package.py --out DIR [--arm32 FILE] [--arm64 FILE] [--helper32 FILE] [--helper64 FILE] [--sdk DIR]
#   --arm32 / --arm64   the built module for that architecture (mpckit_demo.so); at least one. It is copied to VST/mpckitdemo-arm32.so / -arm64.so (the names in package/plugin.json).
#   --helper32 / --helper64   the GPU helper of the overlay (mpckit-demo-overlay-gpu, built with -DMPCKIT_OVERLAY=ON) for that architecture; copied to VST/mpckit-demo-overlay-gpu-arm32 / -arm64, where the
#                       plugin looks for it (the name is kBranding.helperName of src/overlay_hook.h). Without it the overlay draws with the CPU.
#   The skin is generated into the folder (tools/gen_skin.py), plugin.json is package/plugin.json with file32 / file64 only for the binaries given, version.xml carries its version + ".0".
#   The folder name is "<manufacturer> - VST - <name>" with the characters the MPC forbids removed (the rule of scripts/gen-mpc-manifest.py folder_name ()).
# Copy the folder to the MPC's "VST2 Plugins" folder (see README); the SDK's validator (tools/validate.py of the SDK) checks it.

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEMPLATE = HERE.parent
FORBIDDEN = set('"#@,;:<>*^|?\\/')


def folder_name(manufacturer, name):
    clean = lambda s: "".join(c for c in s if c not in FORBIDDEN)
    return "%s - VST - %s" % (clean(manufacturer), clean(name)) if manufacturer else "VST - %s" % clean(name)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Assemble the MpcKit Demo plugin folder")
    ap.add_argument("--out", required=True)
    ap.add_argument("--arm32")
    ap.add_argument("--arm64")
    ap.add_argument("--helper32")
    ap.add_argument("--helper64")
    ap.add_argument("--sdk", default=None)
    args = ap.parse_args(argv)
    if not args.arm32 and not args.arm64:
        print("make_package: give --arm32 and / or --arm64", file=sys.stderr)
        return 2
    manifest = json.loads((TEMPLATE / "package" / "plugin.json").read_text(encoding="utf-8"))
    folder = Path(args.out) / folder_name(manifest["manufacturer"], manifest["name"])
    shutil.rmtree(folder / "VST", ignore_errors=True)
    (folder / "VST").mkdir(parents=True)
    for arch, src in (("arm32", args.arm32), ("arm64", args.arm64)):
        key = "file32" if arch == "arm32" else "file64"
        if src:
            shutil.copyfile(src, folder / manifest[key])
        else:
            del manifest[key]
    for arch, src in (("arm32", args.helper32), ("arm64", args.helper64)):   # the GPU helper of the overlay: next to the plugin binary, named <helperName>-<arch> (shell-mpc/overlay/service.h gpuHelperPath)
        if src:
            dst = folder / "VST" / ("mpckit-demo-overlay-gpu-" + arch)
            shutil.copyfile(src, dst)
            dst.chmod(0o755)
    cmd = [sys.executable, str(HERE / "gen_skin.py"), "--out", str(folder)]
    if args.sdk:
        cmd += ["--sdk", args.sdk]
    if subprocess.call(cmd) != 0:
        return 1
    (folder / "plugin.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    parts = manifest["version"].split(".")
    while len(parts) < 3:
        parts.append("0")
    (folder / "version.xml").write_text('<?xml version="1.0" encoding="utf-8"?>\n<plugincontent>\n    <version>%s.0</version>\n    <uiversion>0.1.0.0</uiversion>\n</plugincontent>\n' % ".".join(parts[:3]), encoding="utf-8")
    print("make_package: wrote %s" % folder)
    return 0


if __name__ == "__main__":
    sys.exit(main())
