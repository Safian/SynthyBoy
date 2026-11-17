#!/usr/bin/env python3
# MpcKit Demo - generates the "Plugin Skins" folder of the package: the plain tabs from skin/params.json + skin/pages.json (the host widgets), then the overlay extras (markers, instance picture,
# hidden widgets, Q-Link mapping: skin/extras/50_markers.py). Stdlib only; it runs the generators of the repository, it contains none of its own.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 template-mpc/tools/gen_skin.py --out <package folder> [--sdk DIR]       (--sdk defaults to $MPC_SDK)
#   1. scripts/gen-mpc-ui.py       skin/params.json + skin/pages.json  ->  ui.json, params-hostsim.json
#   2. scripts/run-gen-ui.py       the SDK's tools/gen_ui.py           ->  <out>/Plugin Skins/TUI.json, Q-Links.json
#   3. scripts/apply-mpc-skin-extras.py --extras-dir template-mpc/skin/extras
# Exit 0 ok, otherwise the exit code of the failing step.

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEMPLATE = HERE.parent
ROOT = TEMPLATE.parent


def run(cmd):
    print("gen_skin: " + " ".join(str(c) for c in cmd))
    return subprocess.call([str(c) for c in cmd])


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate the Plugin Skins folder of MpcKit Demo")
    ap.add_argument("--out", required=True, help="the package folder (the 'Plugin Skins' folder is made inside it)")
    ap.add_argument("--sdk", default=os.environ.get("MPC_SDK"), help="the MPC plugin SDK (default $MPC_SDK)")
    args = ap.parse_args(argv)
    if not args.sdk:
        print("gen_skin: --sdk or MPC_SDK is needed", file=sys.stderr)
        return 2
    skins = Path(args.out) / "Plugin Skins"
    skins.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        steps = [
            [sys.executable, ROOT / "scripts" / "gen-mpc-ui.py", "--params", TEMPLATE / "skin" / "params.json", "--pages", TEMPLATE / "skin" / "pages.json", "--out", tmp],
            [sys.executable, ROOT / "scripts" / "run-gen-ui.py", "--sdk", args.sdk, "--params", Path(tmp) / "params-hostsim.json", "--ui", Path(tmp) / "ui.json", "--out", skins, "--quiet"],
            [sys.executable, ROOT / "scripts" / "apply-mpc-skin-extras.py", "--skins", skins, "--params", TEMPLATE / "skin" / "params.json", "--extras-dir", TEMPLATE / "skin" / "extras"],
        ]
        for step in steps:
            rc = run(step)
            if rc != 0:
                return rc
    print("gen_skin: wrote %s" % skins)
    return 0


if __name__ == "__main__":
    sys.exit(main())
