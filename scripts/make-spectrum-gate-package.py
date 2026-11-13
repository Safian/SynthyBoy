#!/usr/bin/env python3
# SynthyBoy - assembles a gate package for measuring the spectrum on an MPC: a "Plugin Skins" folder (the finished skin, or the skin with ONE vertical Meter bound to
# "LFO 1 Cursor") and the two plugin binaries of one build, in a folder of its own.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/make-spectrum-gate-package.py --package "<out>/Safian - VST - SynthyBoy" --out DIR [--single-meter]
#   --package   the folder scripts/build-mpc.sh made (it has Plugin Skins/ and VST/synthyboy-arm32.so, synthyboy-arm64.so)
#   --out       the gate folder to write: Plugin Skins/, synthyboy-arm32.so, synthyboy-arm64.so (an existing folder is replaced)
#   --single-meter  keep one of the 32 Meters of the Macros tab (the first, same place and size), bound to the display parameter "LFO 1 Cursor" (index = number of
#                   engine parameters, 2637) instead of "Spectrum 1": measures the vertical direction with a parameter that moves without a sound (selftest build).
# The host answers a mistake in a skin with an empty page and no message, so the gate packages are made by this script and not by hand.

import argparse
import importlib.util
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("apply_mpc_skin_extras", HERE / "apply-mpc-skin-extras.py")
apply_extras = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apply_extras)

ENGINE_PARAMS = 2637   # docs/params.json "count"; the first display parameter is "LFO 1 Cursor"


def single_meter(tui_text):
    tui = apply_extras.load_json(tui_text)
    tab = next(t for t in tui["pageData"]["tabs"] if t["tabName"] == "Macros")
    items = tab["componentDefinition"]["componentsData"]
    meters = [c for c in items if c["componentData"]["type"] == "Meter"]
    if len(meters) != 32:
        raise ValueError("the Macros tab has %d meters, expected 32" % len(meters))
    first = meters[0]
    handle = "Parameter %d" % ENGINE_PARAMS
    data = first["componentData"]["data"]
    data["peakHandle"], data["peakHoldHandle"] = handle, "Hold/" + handle
    first["handle remapping"]["map"] = [{"key": "Data", "value": handle}]
    first["componentData"]["name"] = "LFO 1 Cursor Meter"
    items[:] = [c for c in items if c["componentData"]["type"] != "Meter"] + [first]
    return apply_extras.dump_json(tui) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--package", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--single-meter", action="store_true")
    args = ap.parse_args(argv)
    package, out = Path(args.package), Path(args.out)
    for p in (package / "Plugin Skins" / "TUI.json", package / "VST" / "synthyboy-arm32.so", package / "VST" / "synthyboy-arm64.so"):
        if not p.is_file():
            print("make-spectrum-gate-package.py: missing %s" % p, file=sys.stderr)
            return 1
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copytree(package / "Plugin Skins", out / "Plugin Skins")
    for name in ("synthyboy-arm32.so", "synthyboy-arm64.so"):
        shutil.copyfile(package / "VST" / name, out / name)
        shutil.copymode(package / "VST" / name, out / name)
    if args.single_meter:
        tui_path = out / "Plugin Skins" / "TUI.json"
        tui_path.write_text(single_meter(tui_path.read_text(encoding="utf-8")), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
