#!/usr/bin/env python3
# SynthyBoy - names for the macros of the factory presets, derived from what each macro drives (stdlib only).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   name-preset-macros.py [--presets DIR] [--names FILE] [--proposal FILE] [--rederive] [--write] [--check]
#
# Reads every .sbpreset below DIR (default content/presets): the mod matrix slots whose source or aux source is a macro and that really drive something (a destination, an amount that is not
# zero, not bypassed) are the targets of that macro. A macro with targets gets a readable name from them, a macro without targets keeps the default name (an empty string):
#   one target (or targets that share one short name)  -> its short name: "Cutoff", "Reso", "Drive", "Warp", "Detune", "LFO Rate" ...
#   two targets                                         -> "A + B" when it fits 16 characters, else the module both belong to ("Filter"), else their two modules ("LFO + Phaser"), else "A +" (the first one and a plus)
#   three or more                                       -> the short name that most of them share ("Drive"), else their modules when there are two at most ("Reverb + Chorus"), else the module most of them belong to ("Osc"), else the default name
# The short names come from the parameter table (docs/params.json): the module words in front of a name and a closing "Amount" go, a few are written out (SHORT_NAMES, every key of it is
# checked against the table; nothing is invented).
#
# The names are DATA: scripts/factory-preset-macro-names.json ({"presets": {"808 Boom": [eight names]}}). Without --rederive an entry that exists is kept (the owner corrects names there by
# hand, that is the point of the file) and only presets without an entry get derived names. --proposal writes the Markdown list of preset, macro, targets and chosen name.
# --write then runs scripts/gen-factory-presets.py, which is what writes the names into the .sbpreset files (and the manifest); --check exits 1 when the names file lacks a preset, names a macro
# that has no target, or the presets are not what the generator makes of the names file.

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARAMS_JSON = ROOT / "docs" / "params.json"
NAMES_FILE = ROOT / "scripts" / "factory-preset-macro-names.json"
PRESETS_DIR = ROOT / "content" / "presets"
MAX_CHARS = 16                      # kMaxMacroNameChars (core/include/synthyboy/content_state.h)
NUM_MACROS = 8
FORMAT = "synthyboy-factory-macro-names"

# What the parameter table's name is not readable enough for, key -> short name. Every key must be in docs/params.json (checked on every run).
SHORT_NAMES = {
    "filter1.resonance": "Reso", "filter2.resonance": "Reso",
    "distortion.mix": "Dist Mix", "reverb.decay": "Rev Decay", "reverb.mix": "Reverb", "chorus.mix": "Chorus", "delay.mix": "Delay", "phaser.mix": "Phaser",
    "fxfilter.mix": "FX Filter", "fxfilter.cutoff": "FX Cutoff", "rlfo1.rate": "Rnd LFO Rate", "rlfo2.rate": "Rnd LFO Rate",
    "compressor.mix": "Comp Mix", "compressor.mid.gain": "Comp Gain", "filter1.comb_offset": "Comb Offset", "filter1.formant.resonance": "Formant Res", "filter2.formant.resonance": "Formant Res", "flanger.feedback": "Flange Fdbk", "utility.width": "Width", "bode.feed": "Feedback", "bode.shift": "Shift", "bode.mix": "Bode Mix",
    "osc1.gran.density": "Grain Dens", "osc2.gran.density": "Grain Dens", "osc3.gran.density": "Grain Dens",
    "convolve.attack": "Conv Attack",
    "osc1.gran.position": "Grain Pos",
    "osc1.gran.rand_dir": "Rnd Dir",
    "osc1.gran.rand_level": "Rnd Level",
    "osc1.gran.rand_offset": "Rnd Offset",
    "osc1.gran.rand_pan": "Rnd Pan",
    "osc1.gran.rand_pitch": "Rnd Pitch",
    "osc1.multi.timbre": "Timbre",
    "osc1.sample.slice": "Slice",
    "osc1.spec.freq_hi": "Freq Hi",
    "osc1.spec.freq_shift": "Freq Shift",
    "osc1.spec.position": "Spec Pos",
    "osc2.gran.position": "Grain Pos",
    "osc2.gran.rand_dir": "Rnd Dir",
    "osc2.gran.rand_level": "Rnd Level",
    "osc2.gran.rand_offset": "Rnd Offset",
    "osc2.gran.rand_pan": "Rnd Pan",
    "osc2.gran.rand_pitch": "Rnd Pitch",
    "osc2.multi.timbre": "Timbre",
    "osc2.sample.slice": "Slice",
    "osc2.spec.freq_hi": "Freq Hi",
    "osc2.spec.freq_shift": "Freq Shift",
    "osc2.spec.position": "Spec Pos",
    "osc3.gran.position": "Grain Pos",
    "osc3.gran.rand_dir": "Rnd Dir",
    "osc3.gran.rand_level": "Rnd Level",
    "osc3.gran.rand_offset": "Rnd Offset",
    "osc3.gran.rand_pan": "Rnd Pan",
    "osc3.gran.rand_pitch": "Rnd Pitch",
    "osc3.multi.timbre": "Timbre",
    "osc3.sample.slice": "Slice",
    "osc3.spec.freq_hi": "Freq Hi",
    "osc3.spec.freq_shift": "Freq Shift",
    "osc3.spec.position": "Spec Pos",
    "noise.level": "Noise", "lfo1.rate": "LFO Rate", "lfo2.rate": "LFO Rate", "lfo3.rate": "LFO Rate", "lfo4.rate": "LFO Rate",
}
# The words that open a parameter's name and say which module it is (the page it is on says it too); they go.
MODULE_PREFIX = re.compile(r"^(Filter \d+|Osc \d+|Env \d+|LFO \d+|Random LFO \d+|Distortion|Delay|Reverb|Chorus|Phaser|Noise|Sub) ")
DROP_WORDS = {"Amount", "Unison", "Stereo"}   # a closing "Amount" ("Warp Amount"), the word "Unison" ("Unison Detune") and "Stereo" ("Stereo Spread") add nothing
# The module a key belongs to (its first word), the name that stands for it, and whether a module with this name is a section of its own.
MODULE_LABEL = {"filter": "Filter", "fxfilter": "FX Filter", "osc": "Osc", "env": "Envelope", "lfo": "LFO", "noise": "Noise", "sub": "Sub",
                "distortion": "Distortion", "reverb": "Reverb", "delay": "Delay", "chorus": "Chorus", "phaser": "Phaser"}


def load_params():
    with open(PARAMS_JSON, "r", encoding="utf-8") as f:
        doc = json.load(f)
    params = {p["key"]: p for p in doc["params"]}
    for key in SHORT_NAMES:
        if key not in params:
            raise KeyError(f"SHORT_NAMES names a parameter that does not exist: {key}")
    return doc, params


def short_name(key, params):
    """The short name of a parameter: the table of exceptions, else its table name without the module words and the filler words."""
    if key in SHORT_NAMES:
        return SHORT_NAMES[key]
    name = params[key]["name"]
    stripped = MODULE_PREFIX.sub("", name, count=1)
    words = [w for w in stripped.split() if w not in DROP_WORDS] or stripped.split()
    return " ".join(words) if words else name


def module_of(key):
    """The module id of a key: its first segment without the number (filter1 -> filter); the random LFOs belong to the LFOs."""
    module = re.sub(r"\d+$", "", key.split(".")[0])
    return "lfo" if module == "rlfo" else module


def module_label(module):
    return MODULE_LABEL.get(module, module.title())


def pair(first, second):
    """"A + B" when it fits the name, else None."""
    both = f"{first} + {second}"
    return both if len(both) <= MAX_CHARS else None


def fit(text):
    """At most MAX_CHARS characters: a longer text is cut at the end of a word when one ends in time, else in the middle of one."""
    if len(text) <= MAX_CHARS:
        return text
    cut = text[:MAX_CHARS]
    if text[MAX_CHARS] != " " and " " in cut and cut.rfind(" ") >= 3:
        cut = cut[:cut.rfind(" ")]
    return cut.rstrip(" +-")


def combine(keys, params, macro):
    """The name of a macro that drives `keys` (parameter keys in the order of the slots), "" for the default name; at most MAX_CHARS characters."""
    return fit(combine_unbounded(keys, params, macro))


def combine_unbounded(keys, params, macro):
    keys = list(dict.fromkeys(keys))
    if not keys:
        return ""
    shorts = [short_name(k, params) for k in keys]
    distinct = list(dict.fromkeys(shorts))
    if len(distinct) == 1:
        return distinct[0]
    modules = list(dict.fromkeys(module_of(k) for k in keys))
    if len(keys) == 2:
        return pair(distinct[0], distinct[1]) or (module_label(modules[0]) if len(modules) == 1 else pair(module_label(modules[0]), module_label(modules[1]))) or f"{distinct[0]} +"
    # three or more targets: the short name that most of them share, else the modules they belong to (two at most), else the module most of them belong to, else the default name
    counts = {s: shorts.count(s) for s in distinct}
    best = max(counts.values())
    leaders = [s for s in distinct if counts[s] == best]
    if best >= 2 and len(leaders) == 1:
        return leaders[0]
    if len(modules) == 1:
        return module_label(modules[0])
    if len(modules) == 2:
        both = pair(module_label(modules[0]), module_label(modules[1]))
        if both:
            return both
    mods = [module_of(k) for k in keys]
    top = max(modules, key=lambda m: mods.count(m))
    if mods.count(top) * 2 > len(keys):
        return module_label(top)
    return ""


def macro_targets(state_params, doc):
    """{macro (1 .. 8): [(destination key, amount percent)]} of the slots that drive something (main or aux source)."""
    source_key = {s["choice"]: s["key"] for s in doc["modSources"]}
    dest_key = {d["choice"]: d["key"] for d in doc["modDestinations"]}
    out = {}
    for slot in range(1, 65):
        p = f"mod{slot:02d}."
        src, dst = int(state_params.get(p + "source", 0)), int(state_params.get(p + "destination", 0))
        amount = float(state_params.get(p + "amount", 0))
        if not src or not dst or amount == 0 or state_params.get(p + "bypass", 0):
            continue

        target_param = dest_key.get(dst)
        if not target_param:
            continue

        # If destination is another slot's amount (e.g. mod01.amount), resolve to that slot's target
        m_slot = re.match(r"^mod(\d+)\.amount$", target_param)
        if m_slot:
            target_slot = int(m_slot.group(1))
            t_dst = int(state_params.get(f"mod{target_slot:02d}.destination", 0))
            if t_dst in dest_key:
                target_param = dest_key[t_dst]

        # Main source
        key = source_key.get(src, "")
        main_macro = None
        if key.startswith("macro") and key[5:].isdigit():
            main_macro = int(key[5:])
            out.setdefault(main_macro, []).append((target_param, amount))

        # Aux source (treat aux use as use)
        aux = int(state_params.get(p + "aux_source", 0))
        if aux:
            aux_key = source_key.get(aux, "")
            if aux_key.startswith("macro") and aux_key[5:].isdigit():
                aux_macro = int(aux_key[5:])
                if aux_macro != main_macro:
                    out.setdefault(aux_macro, []).append((target_param, amount))
    return out


def read_presets(presets_dir):
    """[(relative path, preset name, state params)] in path order."""
    out = []
    for path in sorted(Path(presets_dir).glob("*/*.sbpreset")):
        with open(path, "r", encoding="utf-8") as f:
            d = json.load(f)
        out.append((f"{path.parent.name}/{path.name}", d["meta"]["name"], d["state"]["params"]))
    return out


def derive(presets_dir=PRESETS_DIR):
    """{preset name: {"path": ..., "targets": {macro: [(key, amount)]}, "names": [eight names]}} derived from the presets (no names file involved)."""
    doc, params = load_params()
    out = {}
    for rel, name, state in read_presets(presets_dir):
        targets = macro_targets(state, doc)
        names = [combine([k for k, _ in targets.get(m, [])], params, m) for m in range(1, NUM_MACROS + 1)]
        out[name] = {"path": rel, "targets": targets, "names": names}
    return out


def load_names(path):
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f).get("presets", {})


def names_text(presets):
    return json.dumps({"format": FORMAT, "version": 1, "presets": presets}, indent=1, ensure_ascii=False) + "\n"


def proposal_text(derived, names, params):
    lines = ["# Names of the macros of the factory presets", "",
             "Written by `scripts/name-preset-macros.py`. The name in the last column is what the preset has now (`scripts/factory-preset-macro-names.json`); correct it there and run `python3 scripts/gen-factory-presets.py`.",
             "A macro without a target keeps the default name (\"Macro n\") and is not listed. The names are at most 16 characters.", "",
             "| Preset | Macro | Targets | Name |", "|---|---|---|---|"]
    for name, info in derived.items():
        for m in range(1, NUM_MACROS + 1):
            if m not in info["targets"]:
                continue
            targets = ", ".join(f"{params[k]['name']} ({a:+g}%)" for k, a in info["targets"][m])
            chosen = names[name][m - 1] or f"Macro {m}"
            lines.append(f"| {info['path'][:-len('.sbpreset')]} | {m} | {targets} | {chosen} |")
    return "\n".join(lines) + "\n"


def problems(derived, names):
    bad = []
    for name, info in derived.items():
        if name not in names:
            bad.append(f"{name}: no entry in the names file")
            continue
        for m in range(1, NUM_MACROS + 1):
            if m not in info["targets"] and names[name][m - 1]:
                bad.append(f"{name}: macro {m} has no target but is called {names[name][m - 1]!r}")
    for name in names:
        if name not in derived:
            bad.append(f"{name}: in the names file, not a preset")
    return bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__ if False else None)
    ap.add_argument("--presets", default=str(PRESETS_DIR))
    ap.add_argument("--names", default=str(NAMES_FILE))
    ap.add_argument("--proposal", default="")
    ap.add_argument("--rederive", action="store_true", help="derive every name again, also the ones that are in the names file")
    ap.add_argument("--write", action="store_true", help="run gen-factory-presets.py afterwards (it writes the names into the presets and the manifest)")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)

    _, params = load_params()
    derived = derive(args.presets)
    have = load_names(args.names)
    if args.check:
        bad = problems(derived, have)
        for b in bad:
            print(b)
        gen = subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "gen-factory-presets.py"), "--check", "--out", str(args.presets)], capture_output=True, text=True)
        if gen.returncode != 0:
            print(gen.stdout + gen.stderr)
        return 1 if bad or gen.returncode != 0 else 0

    names = {}
    for name, info in derived.items():
        names[name] = have[name] if name in have and not args.rederive else info["names"]
    text = names_text(names)
    if not os.path.isfile(args.names) or Path(args.names).read_text(encoding="utf-8") != text:
        Path(args.names).write_text(text, encoding="utf-8")
    if args.proposal:
        Path(args.proposal).write_text(proposal_text(derived, names, params), encoding="utf-8")
    if args.write:
        r = subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "gen-factory-presets.py"), "--out", str(args.presets)])
        return r.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
