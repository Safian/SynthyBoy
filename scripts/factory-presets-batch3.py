# SynthyBoy - factory sound set, batch 3: 24 presets on the factory wavetables that no preset of the first 160 used (docs/release/SOUND-SET-BATCH-3.md, plan: docs/release/SOUND-SET-BATCH-3-PLAN.md).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Read by scripts/gen-factory-presets.py (build()), same contract as scripts/factory-presets-batch2.py (the presets are in scripts/batch3/*.py, written with scripts/sb_preset_dsl.py).
# The helpers that are the same as in batch 2 (WT, SMP, IMP, INST, C, pressure, limiter_last) are taken from scripts/factory-presets-batch2.py, so there is one copy of them.
# `python3 scripts/factory-presets-batch3.py --sync-names` writes the macro names into scripts/factory-preset-macro-names.json. The levels are in scripts/factory-preset-levels.json.

import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
from sb_preset_dsl import R, build_params  # noqa: E402


def _batch2():
    spec = importlib.util.spec_from_file_location("factory_batch2_helpers", os.path.join(HERE, "factory-presets-batch2.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


B2 = _batch2()
WT, SMP, IMP, INST, C, pressure, limiter_last = B2.WT, B2.SMP, B2.IMP, B2.INST, B2.C, B2.pressure, B2.limiter_last

ENTRIES = []   # (name, category, tags, description, params, routes, macro names, content)


def add(name, category, tags, description, params, routes, macro_names, content=None):
    assert len(name) <= 24, name                      # the host's program name limit (VST2 kVstMaxProgNameLen)
    assert len(macro_names) == 8
    for n in macro_names:
        assert n == "" or 3 <= len(n) <= 14, (name, n)
    ENTRIES.append((name, category, tags, description, params, routes, macro_names, content))


def _load_parts():
    d = os.path.join(HERE, "batch3")
    if not os.path.isdir(d):
        return
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".py"):
            spec = importlib.util.spec_from_file_location("batch3_" + fn[:-3], os.path.join(d, fn))
            m = importlib.util.module_from_spec(spec)
            m.add, m.pressure, m.R, m.limiter_last = add, pressure, R, limiter_last
            m.WT, m.SMP, m.IMP, m.INST, m.C = WT, SMP, IMP, INST, C
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
