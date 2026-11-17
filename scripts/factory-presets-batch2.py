# SynthyBoy - factory sound set, batch 2: 24 presets built on the factory content pack (wavetables, samples, instruments, impulse responses of scripts/gen-content-pack.py) and on what batch 1
# could not show: the Sample, Granular, Spectral and Multisample oscillator types, Convolve, the buses, the voice control lanes, the scales and MPE (docs/release/SOUND-SET-BATCH-2.md).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Read by scripts/gen-factory-presets.py (build()), same contract as scripts/factory-presets-batch1.py (the presets are in scripts/batch2/*.py, written with scripts/sb_preset_dsl.py).
# `python3 scripts/factory-presets-batch2.py --sync-names` writes the macro names into scripts/factory-preset-macro-names.json. The levels are in scripts/factory-preset-levels.json.
#
# Content helpers for the preset files (each returns one entry of the `content` section of the preset; the SHA-256 is that of the file in content/factory-*, so a changed pack
# is an error here until the presets are generated again):
#   WT (osc, "table")          a Preset table of the factory wavetable pack (set osc<N>.wavetable "Preset table")
#   SMP (osc, "sample", slices) a sample of the factory sample pack for an oscillator of the type Sample / Granular / Spectral (osc 4 = the noise source with noise.type Sample)
#   IMP ("impulse")            the impulse response of Convolve (convolve.ir "Preset IR")
#   INST (osc, "instrument")   a key-mapped instrument (SFZ) for an oscillator of the type Multisample
#   C (wavetables=[...], samples=[...], impulses=[...], instruments=[...])

import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
from sb_preset_dsl import R, build_params  # noqa: E402

CONTENT = os.path.join(HERE, "..", "content")
PACK_DIR = {"wavetables": "factory-wavetables", "samples": "factory-samples"}
PACK_ID = {"wavetables": "com.safian.synthyboy.factory-wavetables", "samples": "com.safian.synthyboy.factory-samples"}

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
    """The effect order with the compressor LAST and the compressor as a look-ahead limiter at `threshold_db` (see factory-presets-batch1.py)."""
    order = ["FX Filter", "Distortion", "EQ", "Chorus", "Flanger", "Phaser", "Delay", "Reverb", "Hyper/Dimension", "Utility", "Bode", "Convolve", "Compressor"]
    p = {f"fx.order.{i + 1}": name for i, name in enumerate(order)}
    p.update({"compressor.on": 1, "compressor.limit": "Limit", "compressor.bands": "Single band", "compressor.mid.upper_threshold": threshold_db, "compressor.mid.upper_ratio": 100,
              "compressor.attack": 10, "compressor.release": release})
    return p


def _file(kind, rel):
    return os.path.join(CONTENT, PACK_DIR[kind], rel)


def _ref(kind, rel, name=None):
    data = open(_file(kind, rel), "rb").read()
    return {"pack": PACK_ID[kind], "file": rel, "sha256": hashlib.sha256(data).hexdigest(), "name": name or os.path.splitext(os.path.basename(rel))[0]}


def WT(osc, table):
    rel = f"{table}.wav"
    n = os.path.getsize(_file("wavetables", rel)) - 44
    return {"osc": osc, **_ref("wavetables", rel), "frames": n // 2 // 2048}


def SMP(osc, sample, slices=()):
    rel = f"samples/{sample}.wav"
    raw = open(_file("samples", rel), "rb").read()
    ch, rate = struct.unpack("<HI", raw[22:28])
    data_pos = raw.find(b"data")
    nbytes = struct.unpack("<I", raw[data_pos + 4:data_pos + 8])[0]
    return {"osc": osc, **_ref("samples", rel), "sample_rate": rate, "frames": nbytes // (2 * ch), "channels": ch, "slices": list(slices)}


def IMP(impulse):
    return _ref("samples", f"impulses/{impulse}.wav")


def INST(osc, name):
    rel = f"instruments/{name}/{name}.sfz"
    zones = open(_file("samples", rel), "r", encoding="utf-8").read().count("<region>")
    return {"osc": osc, **_ref("samples", rel), "zones": zones}


def C(wavetables=(), samples=(), impulses=(), instruments=()):
    out = {}
    if wavetables:
        out["wavetables"] = list(wavetables)
    if samples:
        out["samples"] = list(samples)
    if impulses:
        out["impulses"] = list(impulses)
    if instruments:
        out["instruments"] = list(instruments)
    return out


def _load_parts():
    import importlib.util
    d = os.path.join(HERE, "batch2")
    if not os.path.isdir(d):
        return
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".py"):
            spec = importlib.util.spec_from_file_location("batch2_" + fn[:-3], os.path.join(d, fn))
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
