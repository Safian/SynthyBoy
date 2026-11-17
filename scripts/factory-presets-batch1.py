# SynthyBoy - factory sound set, batch 1: 24 presets built on purpose from Init (docs/release/SOUND-SET-BATCH-1.md says what each one is for and how it was measured).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Read by scripts/gen-factory-presets.py (build()). Every preset is written whole: the parameters that differ from the defaults (a choice by its name), the modulation routes
# R(source, destination, amount, ...) in the order of mod01 .. mod64 (the macro wiring and the pad pressure route are among them), the eight macro names, a custom LFO shape if it
# has one. `python3 scripts/factory-presets-batch1.py --sync-names` writes the macro names into scripts/factory-preset-macro-names.json (the file the tests and name-preset-macros.py read).
# The levels (master.volume) are in scripts/factory-preset-levels.json, tuned by scripts/sound-lab.py.

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
from sb_preset_dsl import R, build_params  # noqa: E402

ENTRIES = []   # (name, category, tags, description, params, routes, macro names, content)


def add(name, category, tags, description, params, routes, macro_names, content=None):
    assert len(name) <= 24, name                      # the host's program name limit (VST2 kVstMaxProgNameLen)
    assert len(macro_names) == 8
    for n in macro_names:
        assert n == "" or 3 <= len(n) <= 14, (name, n)
    ENTRIES.append((name, category, tags, description, params, routes, macro_names, content))


def pressure(amount):
    """The pad pressure route: Poly Aftertouch -> Filter 1 Cutoff (the pad pressure layer of the shell skips a voice whose preset has it)."""
    return R("poly_aftertouch", "filter1.cutoff", amount)


def limiter_last(threshold_db, release=40):
    """The effect order with the compressor LAST and the compressor as a look-ahead limiter with its ceiling at `threshold_db` (dBFS of the engine output): the peaks of a plucked or struck sound are caught
    after the EQ, the chorus and the rest have all had their say (the default order has the compressor in third place). Latency 72 samples of the limiter (reported to the host)."""
    order = ["FX Filter", "Distortion", "EQ", "Chorus", "Flanger", "Phaser", "Delay", "Reverb", "Hyper/Dimension", "Utility", "Bode", "Convolve", "Compressor"]
    p = {f"fx.order.{i + 1}": name for i, name in enumerate(order)}
    p.update({"compressor.on": 1, "compressor.limit": "Limit", "compressor.bands": "Single band", "compressor.mid.upper_threshold": threshold_db, "compressor.mid.upper_ratio": 100,
              "compressor.attack": 10, "compressor.release": release})
    return p


# ---------------------------------------------------------------------------------------------------------------------------------------------------
# The presets are defined in the file scripts/batch1/*.py, one per category, to keep this file short.
# ---------------------------------------------------------------------------------------------------------------------------------------------------
def _load_parts():
    import importlib.util
    d = os.path.join(HERE, "batch1")
    if not os.path.isdir(d):
        return
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".py"):
            spec = importlib.util.spec_from_file_location("batch1_" + fn[:-3], os.path.join(d, fn))
            m = importlib.util.module_from_spec(spec)
            m.add, m.pressure, m.R, m.limiter_last = add, pressure, R, limiter_last
            spec.loader.exec_module(m)


def build():
    """[(name, category, tags, description, params, content)] for the generator."""
    if not ENTRIES:
        _load_parts()
    out = []
    for name, category, tags, description, params, routes, names, content in ENTRIES:
        out.append((name, category, tags, description, build_params(params, routes), content))
    return out


def macro_names():
    if not ENTRIES:
        _load_parts()
    return {e[0]: e[6] for e in ENTRIES}


def sync_names():
    path = os.path.join(HERE, "factory-preset-macro-names.json")
    doc = json.load(open(path, "r", encoding="utf-8"))
    for name, names in macro_names().items():
        doc["presets"][name] = names
    open(path, "w", encoding="utf-8").write(json.dumps(doc, indent=1, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    if "--sync-names" in sys.argv:
        sync_names()
        print("names written")
