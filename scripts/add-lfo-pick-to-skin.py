#!/usr/bin/env python3
# SynthyBoy - one-off tool: adds the LFO host choices (SOURCE of a point modulation, ADD SOURCE, ADD TARGET) to the committed skin/ (TUI.json by definition key, PNGs).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Usage: python3 scripts/add-lfo-pick-to-skin.py [skin dir]   (default: skin/). Only adds definitions, component entries on the LFO tabs and the Play tab, replacement strings and PNGs; idempotent."""
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "scripts")]


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


skin = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "skin")
tui_path = skin / "TUI.json"
runner = load(ROOT / "scripts" / "apply-mpc-skin-extras.py", "apply_extras")
tui_text = tui_path.read_text(encoding="utf-8")
tui = runner.load_json(tui_text)
assert runner.dump_json(tui, indent_step=0) + "\n" == tui_text, "skin/TUI.json is not in the SDK form"
doc = json.loads((ROOT / "docs" / "params.json").read_text(encoding="utf-8"))
ctx = runner.Context(tui, doc, skin, {"variant": "overlay"})
pick = load(ROOT / "scripts" / "mpc-skin-extras" / "98_lfo_pick.py", "lfo_pick")
if "lfoNativePicks" in {d["key"] for d in tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]}:
    sys.exit("skin already has the LFO choices")
pick.apply(tui, {}, ctx)
tui_path.write_text(runner.dump_json(tui, indent_step=0) + "\n", encoding="utf-8")
