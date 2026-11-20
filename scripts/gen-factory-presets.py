#!/usr/bin/env python3
# SynthyBoy - factory presets generator (factory presets + contentpack.json).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   gen-factory-presets.py [--out DIR] [--check]
#
# Writes every factory preset as a .sbpreset (preset format 1) holding a state of version 4 with all keys of docs/params.json
# explicit (table order, %.9g text exactly like formatG9), and the content pack manifest. Deterministic: the same inputs give the
# same bytes (no clock, no randomness, no dict-order dependence). --check writes nothing and exits 1 if DIR (default
# content/presets) differs from what the script generates.
#
# Inputs: docs/params.json (the key table), scripts/factory-preset-levels.json (master.volume per preset, written by
# scripts/tune-preset-levels.py), the sound data below.

import argparse
import hashlib
import importlib.util
import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
PARAMS_JSON_PATH = os.path.join(ROOT_DIR, "docs", "params.json")
LEVELS_PATH = os.path.join(SCRIPT_DIR, "factory-preset-levels.json")
OUTPUT_LIMITER_PATH = os.path.join(SCRIPT_DIR, "factory-preset-output-limiter.json")
TIER_TRIM_PATH = os.path.join(SCRIPT_DIR, "factory-preset-tier-trim.json")
MACRO_NAMES_PATH = os.path.join(SCRIPT_DIR, "factory-preset-macro-names.json")
PAD_PRESSURE_PATH = os.path.join(SCRIPT_DIR, "factory-preset-pad-pressure.json")
DEFAULT_OUTPUT_DIR = os.path.join(ROOT_DIR, "content", "presets")

with open(PARAMS_JSON_PATH, "r", encoding="utf-8") as f:
    _doc = json.load(f)

STATE_VERSION = int(_doc["stateVersion"])
assert STATE_VERSION == 4, "this generator writes state version 4"
params_data = _doc["params"]
param_table_keys = [p["key"] for p in params_data]
param_info = {p["key"]: p for p in params_data}
default_values = {p["key"]: float(p["default"]) for p in params_data}
mod_destination = {d["key"]: int(d["choice"]) for d in _doc["modDestinations"]}
mod_source = {s["key"]: int(s["choice"]) for s in _doc["modSources"]}
NUM_MOD_SLOTS = 64

CATEGORIES = ("Bass", "Lead", "Pad", "Pluck", "Keys", "FX")


def format_g9(val: float) -> str:
    # Same as formatG9 in C++ number.cpp
    if val == 0.0:
        return "0"
    s = f"{val:.9g}"
    if "." in s and "e" not in s:
        s = s.rstrip("0").rstrip(".")
    return s


def check_value(key: str, value: float) -> None:
    info = param_info[key]
    if not (float(info["min"]) <= value <= float(info["max"])):
        raise ValueError(f"{key}={value} outside [{info['min']}, {info['max']}]")
    if info.get("interval", 0) and info["interval"] >= 1 and value != round(value):
        raise ValueError(f"{key}={value} is not on its integer grid")


def point_text(p) -> str:
    return '{"x":%s,"y":%s,"power":%s}' % (format_g9(p[0]), format_g9(p[1]), format_g9(p[2] if len(p) > 2 else 0.0))


MAX_MACRO_NAME_CHARS = 16   # kMaxMacroNameChars of core/include/synthyboy/content_state.h


def check_macro_names(preset: str, names) -> list:
    """The eight names of a preset in the form the C++ writer keeps them (sanitizeMacroName): ASCII text, no blanks at the ends or two in a row, at most
    16 characters, never the default name of its own macro (that is the empty text). A name outside these rules is an error, not silently cleaned: the file is edited by hand."""
    if not isinstance(names, list) or len(names) != 8 or not all(isinstance(n, str) for n in names):
        raise ValueError(f"{MACRO_NAMES_PATH}: {preset}: expected a list of eight strings")
    for i, n in enumerate(names):
        if n == f"Macro {i + 1}":
            raise ValueError(f"{MACRO_NAMES_PATH}: {preset}: macro {i + 1} is called its default name; write an empty string")
        if n != " ".join(n.split()) or not all(32 <= ord(c) < 127 for c in n) or len(n) > MAX_MACRO_NAME_CHARS:
            raise ValueError(f"{MACRO_NAMES_PATH}: {preset}: macro {i + 1} name {n!r} is not clean (ASCII, single blanks, none at the ends, at most {MAX_MACRO_NAME_CHARS} characters)")
    return names


def load_macro_names() -> dict:
    """{preset name: [eight macro names]} (scripts/name-preset-macros.py writes it, the owner corrects it by hand); a preset without an entry has the default names."""
    if not os.path.isfile(MACRO_NAMES_PATH):
        return {}
    with open(MACRO_NAMES_PATH, "r", encoding="utf-8") as f:
        doc = json.load(f)
    return {name: check_macro_names(name, names) for name, names in doc.get("presets", {}).items()}


def content_text(content) -> str:
    """The `content` section of the state (custom LFO shapes and macro names are produced here; same writer as C++ putContent, the names last)."""
    if not content:
        return ""
    parts = []

    def ref_text(r):
        return '"pack":%s,"file":%s,"sha256":%s,"name":%s' % (json.dumps(r["pack"]), json.dumps(r["file"]), json.dumps(r["sha256"]), json.dumps(r.get("name", "")))

    # files of the factory content packs (scripts/gen-content-pack.py): pack id + file + SHA-256, in the order of the C++ writer (core/src/state.cpp putContent): wavetables, samples, lfo_shapes, impulses, instruments
    if content.get("wavetables"):
        parts.append('"wavetables":[' + ",".join('{"osc":%d,%s,"author":%s,"frames":%d}' % (w["osc"], ref_text(w), json.dumps(w.get("author", "Safian Szabolcs")), w["frames"]) for w in content["wavetables"]) + "]")
    if content.get("samples"):
        parts.append('"samples":[' + ",".join('{"osc":%d,%s,"sample_rate":%d,"frames":%d,"channels":%d,"slices":[%s]}' % (
            m["osc"], ref_text(m), m["sample_rate"], m["frames"], m["channels"], ",".join(format_g9(v) for v in m.get("slices", []))) for m in content["samples"]) + "]")
    shapes = content.get("lfo_shapes", [])
    if shapes:
        items = []
        for s in shapes:
            item = '{"lfo":%d,"name":%s,"smooth":%s' % (s["lfo"], json.dumps(s["name"]), "true" if s.get("smooth") else "false")
            if s.get("loop_point", -1) >= 0:
                item += ',"loop_point":%d' % s["loop_point"]
            if s.get("grid_x", 0) >= 2:      # the grid of the shape editor (core/include/synthyboy/content_state.h): the loader reads it in a document of any version, the factory documents stay at the version of docs/params.json
                item += ',"grid_x":%d' % s["grid_x"]
            if s.get("grid_y", 0) >= 2:
                item += ',"grid_y":%d' % s["grid_y"]
            item += ',"points":[' + ",".join(point_text(p) for p in s["points"]) + "]}"
            items.append(item)
        parts.append('"lfo_shapes":[' + ",".join(items) + "]")
    if content.get("impulses"):
        parts.append('"impulses":[' + ",".join("{%s}" % ref_text(r) for r in content["impulses"]) + "]")
    if content.get("instruments"):
        parts.append('"instruments":[' + ",".join('{"osc":%d,%s,"zones":%d}' % (i["osc"], ref_text(i), i["zones"]) for i in content["instruments"]) + "]")
    names = content.get("macro_names")
    if names and any(names):
        parts.append('"macro_names":[' + ",".join(json.dumps(n, ensure_ascii=False) for n in names) + "]")
    return ',"content":{' + ",".join(parts) + "}" if parts else ""


def make_preset(name: str, category: str, tags: str, description: str, overrides: dict, content=None, strict: bool = True) -> tuple:
    assert category in CATEGORIES, category
    vals = dict(default_values)
    for k, v in overrides.items():
        if k not in param_info:
            raise KeyError(f"{name}: unknown key {k}")
        v = float(v)
        if not strict:
            # the first hundred: a few values lie outside their range; the state loader clamps them, so the clamped value is the sound
            info = param_info[k]
            v = min(max(v, float(info["min"])), float(info["max"]))
        check_value(k, v)
        vals[k] = v

    meta = {
        "name": name,
        "category": category,
        "author": "Safian Szabolcs",
        "license": "CC0-1.0",
        "tags": tags,
        "description": description
    }

    params_str = ",".join(f'"{k}":{format_g9(vals[k])}' for k in param_table_keys)
    state_json = f'{{"format":"synthyboy-state","version":{STATE_VERSION},"params":{{{params_str}}}}}'
    if content:
        state_json = state_json[:-1] + content_text(content) + "}"

    tier_trims = load_tier_trim()
    trim_db = tier_trims.get(name, 0.0)
    trim_str = f'"tierTrimDb":{format_g9(trim_db)},' if abs(trim_db) >= 0.5 else ''

    doc = (
        f'{{"format":"synthyboy-preset","version":1,'
        f'"meta":{{"name":{json.dumps(meta["name"])},"category":{json.dumps(meta["category"])},'
        f'"author":{json.dumps(meta["author"])},"license":{json.dumps(meta["license"])},'
        f'"tags":{json.dumps(meta["tags"])},"description":{json.dumps(meta["description"])}}},'
        + ('"outputLimiter":1,' if name in load_output_limiter() else '')
        + trim_str
        + f'"state":{state_json}}}'
    )
    return meta, doc


def load_tier_trim() -> dict:
    """The presets whose low-profile Key 61 level needs a trim relative to high-profile XL:
    scripts/factory-preset-tier-trim.json, preset name -> tierTrimDb."""
    if not os.path.isfile(TIER_TRIM_PATH):
        return {}
    with open(TIER_TRIM_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def load_output_limiter() -> dict:
    """The presets whose sound asks for the shells' output limiter (the top-level member "outputLimiter":1, read by the MPC shell when the program is loaded; without it the
    limiter stays off, the default sound): scripts/factory-preset-output-limiter.json, preset name -> the measured reason (docs/release/LEVELLING-STOCK-2026-11-19.md)."""
    if not os.path.isfile(OUTPUT_LIMITER_PATH):
        return {}
    with open(OUTPUT_LIMITER_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------------------------------------------------------
# The sound data of the first hundred presets (unchanged since Phase 4; the two hand-fixes of the SWEEP report, FM Digital Lead
# and Pulse Width Bass, are part of this data).
# ---------------------------------------------------------------------------------------------------------------------------
def mod(slot: int, source: str, dest: str, amount: float, aux: str = None, aux_inv: bool = False) -> dict:
    """One mod-matrix route: source key of modSources (`lfo1`, `env2`, `slide`, ...), destination key of the table, amount in percent."""
    p = f"mod{slot:02d}."
    out = {p + "source": mod_source[source], p + "destination": mod_destination[dest], p + "amount": amount}
    if aux is not None:
        out[p + "aux_source"] = mod_source[aux]
        if aux_inv:
            out[p + "aux_inv"] = 1
    return out


# Define 100 presets
presets = []

# --- 20 Bass Presets ---
bass_names = [
    ("Solid Sub", "sub,clean,deep", "Pure sub bass for electronic foundation", {"osc1.wavetable": 0, "filter1.on": 1, "filter1.cutoff": 120, "master.volume": -6}),
    ("Acid Saw Reso", "acid,reso,tb303", "Classic 303-style resonant sawtooth acid line", {"osc1.wavetable": 1, "filter1.on": 1, "filter1.model": 2, "filter1.cutoff": 450, "filter1.resonance": 78, "env2.decay": 280, "env2.sustain": 0}),
    ("Fat Reese", "reese,detune,dnb", "Dual detuned saws reese bass for DnB", {"osc1.wavetable": 1, "osc1.unison.voices": 5, "osc1.unison.detune": 22, "osc2.on": 1, "osc2.wavetable": 1, "osc2.transpose": -12, "filter1.cutoff": 800}),
    ("Punchy Pluck Bass", "pluck,punch,edm", "Tight transient bass with short envelope decay", {"master.transpose": -12, "osc1.wavetable": 2, "filter1.cutoff": 300, "env1.decay": 180, "env1.sustain": 10, "env1.release": 50}),
    ("Dirty Distortion Bass", "dirty,drive,industrial", "Overdriven bass using dirty filter model", {"osc1.wavetable": 4, "osc1.level": 25, "filter1.model": 1, "filter1.cutoff": 650, "filter1.drive": 10, "distortion.on": 1, "distortion.drive": 2, **mod(1, "velocity", "distortion.drive", 20)}),
    ("808 Boom", "808,hiphop,trap", "Pitch-gliding sine 808 boom bass", {"osc1.wavetable": 0, "env1.decay": 850, "env1.sustain": 0, "env1.release": 400, "master.volume": -5}),
    ("Formant Vowel Bass", "formant,vowel,dubstep", "Formant sweeping vowel bass", {"osc1.wavetable": 9, "filter1.model": 5, "filter1.cutoff": 950, "filter1.resonance": 60}),
    ("Harmonic Sine Bass", "harmonic,warm,smooth", "Sine wave with subtle upper harmonics", {"osc1.wavetable": 7, "filter1.cutoff": 250, "filter1.resonance": 15}),
    ("Moog Ladder Bass", "ladder,analog,vintage", "Vintage ladder filter round bass", {"osc1.wavetable": 1, "filter1.model": 2, "filter1.cutoff": 380, "filter1.resonance": 45, "master.volume": -8}),
    ("Pulse Width Bass", "pwm,square,electro", "PWM modulated electro bass", {"master.transpose": -24, "osc1.wavetable": 11, "osc1.frame": 75, "filter1.cutoff": 550, "osc1.warp.type": 1, "osc1.warp.amount": 35}),
    ("Comb Metal Bass", "comb,metal,texture", "Physical resonant string-like comb bass", {"osc1.wavetable": 1, "filter1.model": 6, "filter1.cutoff": 220, "filter1.resonance": 70}),
    ("Diode Saturated Bass", "diode,saturation,warm", "Warm saturated diode ladder filter bass", {"osc1.wavetable": 5, "filter1.model": 4, "filter1.cutoff": 420, "filter1.drive": 50, "utility.on": 1, "utility.level": 10.0}),
    ("Sync Growl Bass", "sync,growl,modern", "Hard sync modulated aggressive growl bass", {"osc1.wavetable": 1, "osc1.warp.type": 2, "osc1.warp.amount": 45, "filter1.cutoff": 750}),
    ("Click Attack Bass", "transient,click,punch", "Sharp transient click on attack", {"osc1.wavetable": 2, "noise.on": 1, "noise.level": 20, "env1.decay": 220}),
    ("Warm Analog Bass", "warm,smooth,house", "Classic deep house analog bass", {"master.transpose": -24, "osc1.wavetable": 5, "osc1.level": 70, "filter1.model": 0, "filter1.cutoff": 320, "filter1.resonance": 25}),
    ("Gritty Square Bass", "gritty,square,lofi", "Raw gritty square wave bassline", {"osc1.wavetable": 2, "filter1.cutoff": 520, "distortion.on": 1, "distortion.drive": 7.5}),
    ("Sub Duo Octave", "octave,sub,layer", "Dual octave stacked sub bass", {"osc1.wavetable": 0, "osc2.on": 1, "osc2.wavetable": 0, "osc2.transpose": -12, "osc2.level": 75}),
    ("Odd Harmonics Bass", "odd,hollow,funk", "Hollow funk bass with odd harmonic series", {"osc1.wavetable": 8, "filter1.cutoff": 600, "filter1.resonance": 30}),
    ("Fast Arp Bass", "arp,fast,trance,tempo-sync", "Hold one key and it arpeggiates itself: sixteenth notes up the minor chord and the octave (root, third, fifth, octave), each step plucked",
     {"osc1.wavetable": 1, "osc1.level": 28, "osc1.transpose_snap": 1193, "env1.attack": 0, "env1.decay": 800, "env1.sustain": 100, "env1.release": 120,
            "filter1.cutoff": 900, "filter1.resonance": 40,
            "lfo2.shape": 6, "lfo2.sync": 1, "lfo2.division": 8, "lfo2.smooth": 0,
            "lfo3.shape": 3, "lfo3.sync": 1, "lfo3.division": 10, "lfo3.smooth": 0,
      **mod(1, "lfo2", "osc1.transpose", 12.5), **mod(2, "lfo3", "osc1.level", 45), **mod(3, "lfo3", "filter1.cutoff", 24)}),
    ("Space Wobble Bass", "wobble,lfo,dub", "LFO modulating low pass cutoff for dub wobble", {"osc1.wavetable": 1, "filter1.cutoff": 500, "lfo1.rate": 2.5, **mod(1, "lfo1", "filter1.cutoff", 45)})
]
for name, tags, desc, ov in bass_names:
    presets.append((name, "Bass", tags, desc, ov))

# --- 20 Lead Presets ---
lead_names = [
    ("Hyper Saw Lead", "supersaw,trance,anthemic", "7-voice detuned supersaw stadium lead", {"osc1.wavetable": 1, "osc1.unison.voices": 7, "osc1.unison.detune": 35, "osc2.on": 1, "osc2.wavetable": 1, "osc2.unison.voices": 7, "osc2.unison.detune": 42, "delay.on": 1, "reverb.on": 1}),
    ("Solo Square Lead", "solo,square,retro", "Expressive monophonic retro square lead", {"osc1.wavetable": 2, "master.polyphony": 1, "master.legato": 1, "master.portamento.time": 140, "filter1.cutoff": 3500}),
    ("Vocaloid Lead", "vocal,formant,modern", "Singing formant voice lead", {"osc1.wavetable": 9, "filter1.model": 5, "filter1.cutoff": 2200, "filter1.resonance": 55, "delay.on": 1}),
    ("Screaming Sync Lead", "sync,screamer,rock", "Hard-sync pitched aggressive lead", {"osc1.wavetable": 1, "osc1.warp.type": 2, "osc1.warp.amount": 60, "filter1.cutoff": 4800, "distortion.on": 1, "distortion.drive": 10}),
    ("Warm Vintage Lead", "vintage,analog,warm", "Smooth vintage analog solo synthesizer", {"osc1.wavetable": 5, "filter1.model": 0, "filter1.cutoff": 2800, "filter1.resonance": 20, "chorus.on": 1, "chorus.mix": 40}),
    ("Bell Chime Lead", "bell,chime,crystal", "Crystalline bell partials lead", {"osc1.wavetable": 12, "osc1.unison.voices": 3, "osc1.unison.detune": 15, "filter1.cutoff": 6500, "delay.on": 1}),
    ("Dirty Peak Lead", "dirty,peak,industrial", "Distorted peaking industrial synth lead", {"osc1.wavetable": 4, "filter1.model": 1, "filter1.cutoff": 2400, "filter1.resonance": 65, "filter1.drive": 55}),
    ("Pulse Width Mod Lead", "pwm,lush,80s", "Classic 80s chorus PWM synth lead", {"osc1.wavetable": 11, "osc1.warp.type": 1, "osc1.warp.amount": 50, "chorus.on": 1, "chorus.mix": 60}),
    ("FM Digital Lead", "fm,digital,sharp", "Crisp digital frequency modulation lead", {"osc1.wavetable": 10, "osc1.frame": 40, "filter1.cutoff": 5000}),
    ("Tri Soft Solo", "soft,smooth,flute", "Flute-like soft triangle wave lead", {"osc1.wavetable": 3, "filter1.cutoff": 1800, "env1.attack": 30, "env1.release": 250, "reverb.on": 1}),
    ("Laser Beam Lead", "laser,sharp,futuristic", "Futuristic cutting laser lead", {"osc1.wavetable": 1, "filter1.cutoff": 7500, "env2.decay": 90, "env2.sustain": 10, "delay.on": 1}),
    ("Chorused Saw Solo", "chorus,saw,pop", "Bright saw lead bathed in stereo chorus", {"osc1.wavetable": 1, "filter1.cutoff": 4200, "chorus.on": 1, "chorus.mix": 75, "reverb.on": 1}),
    ("Dual Octave Hero", "octave,hero,anthem", "Octave stacked heroic lead synth", {"osc1.wavetable": 1, "osc2.on": 1, "osc2.wavetable": 1, "osc2.transpose": 12, "filter1.cutoff": 3800, "delay.on": 1}),
    ("Diode Bite Lead", "diode,bite,acid", "Sharp biting diode filtered synth lead", {"osc1.wavetable": 1, "filter1.model": 4, "filter1.cutoff": 3100, "filter1.resonance": 45, "filter1.drive": 40, "utility.on": 1, "utility.level": 10.0}),
    ("Phaser Sweep Lead", "phaser,sweep,space", "Spacey phaser swept lead tone", {"osc1.wavetable": 6, "filter1.model": 7, "filter1.cutoff": 2500, "lfo1.rate": 0.5, "reverb.on": 1}),
    ("Cosmic Drift Lead", "drift,cosmic,ambient", "Slowly drifting ambient synth lead", {"osc1.wavetable": 7, "osc1.unison.voices": 4, "osc1.unison.detune": 20, "filter1.cutoff": 2100, "reverb.on": 1, "reverb.mix": 50}),
    ("Staccato Bleep Lead", "bleep,staccato,retro", "Game-inspired staccato bleep melody", {"osc1.wavetable": 2, "env1.decay": 150, "env1.sustain": 0, "filter1.cutoff": 4500}),
    ("Harmonic Shimmer Lead", "shimmer,harmonic,bright", "Upper harmonic rich shimmering lead", {"osc1.wavetable": 7, "filter1.cutoff": 5800, "chorus.on": 1, "delay.on": 1}),
    ("Metallic Edge Lead", "metallic,edge,electro", "Metallic edged aggressive electro lead", {"osc1.wavetable": 11, "filter1.cutoff": 3600, "filter1.resonance": 40, "distortion.on": 1, "distortion.drive": 10}),
    ("Plucked Lead Tone", "pluck,hybrid,dance", "Hybrid pluck and sustained lead", {"osc1.wavetable": 1, "env1.attack": 5, "env1.decay": 350, "env1.sustain": 50, "filter1.cutoff": 3200})
]
for name, tags, desc, ov in lead_names:
    presets.append((name, "Lead", tags, desc, ov))

# --- 20 Pad Presets ---
pad_names = [
    ("Lush Analog Pad", "analog,lush,warm", "Warm enveloping analog string pad", {"osc1.wavetable": 1, "osc1.unison.voices": 6, "osc1.unison.detune": 28, "osc2.on": 1, "osc2.wavetable": 5, "osc2.unison.voices": 6, "osc2.unison.detune": 32, "env1.attack": 850, "env1.release": 1200, "filter1.cutoff": 1800, "chorus.on": 1, "reverb.on": 1}),
    ("Ethereal Glass", "glass,ethereal,air", "Airy crystalline glass texture", {"osc1.wavetable": 12, "osc1.unison.voices": 4, "env1.attack": 1200, "env1.release": 1800, "filter1.cutoff": 3200, "reverb.on": 1, "reverb.mix": 65}),
    ("Dark Cinema Drone", "cinema,dark,drone", "Low suspenseful cinematic drone pad", {"osc1.wavetable": 1, "osc1.transpose": -12, "osc2.on": 1, "osc2.wavetable": 2, "osc2.transpose": -24, "env1.attack": 1500, "filter1.cutoff": 650, "reverb.on": 1}),
    ("Vintage Brass Pad", "brass,vintage,warm", "80s synth brass ensemble pad", {"osc1.wavetable": 1, "osc1.unison.voices": 4, "env1.attack": 350, "env1.release": 700, "filter1.cutoff": 2200, "filter1.resonance": 25, "chorus.on": 1}),
    ("Harmonic Cloud", "cloud,harmonic,ambient", "Dense harmonic ambient cloud", {"osc1.wavetable": 7, "osc1.unison.voices": 8, "osc1.unison.detune": 35, "env1.attack": 2000, "env1.release": 2500, "filter1.cutoff": 1500, "reverb.on": 1}),
    ("Vowel Choir Pad", "choir,vowel,voice", "Synthetic choir vocal pad", {"osc1.wavetable": 9, "filter1.model": 5, "filter1.cutoff": 1400, "env1.attack": 900, "env1.release": 1400, "reverb.on": 1}),
    ("Subtle Tape Drift", "tape,drift,lofi", "Tape modulated drifting vintage pad", {"osc1.wavetable": 5, "filter1.cutoff": 1600, "lfo1.rate": 0.3, "env1.attack": 600, "env1.release": 1000, "chorus.on": 1}),
    ("Space Odyssey Pad", "space,odyssey,sci-fi", "Expansive sci-fi atmosphere pad", {"osc1.wavetable": 6, "osc1.unison.voices": 5, "env1.attack": 1100, "env1.release": 1600, "filter1.cutoff": 2400, "delay.on": 1, "reverb.on": 1}),
    ("Golden Dawn Pad", "dawn,peaceful,warm", "Gentle peaceful morning dawn texture", {"osc1.wavetable": 0, "osc2.on": 1, "osc2.wavetable": 3, "env1.attack": 1400, "env1.release": 1500, "filter1.cutoff": 1200, "reverb.on": 1}),
    ("Deep Sea Ambient", "deep,sea,submerged", "Submerged underwater atmospheric resonance", {"osc1.wavetable": 3, "filter1.model": 2, "filter1.cutoff": 850, "env1.attack": 1800, "env1.release": 2200, "reverb.on": 1}),
    ("Starlight Shimmer", "starlight,shimmer,bright", "High shimmering starlight pad", {"osc1.wavetable": 12, "osc1.transpose": 12, "env1.attack": 750, "env1.release": 1400, "filter1.cutoff": 4500, "reverb.on": 1}),
    ("Odd Harmonics Pad", "odd,hollow,wood", "Hollow wooden pipe organ pad", {"osc1.wavetable": 8, "env1.attack": 500, "env1.release": 900, "filter1.cutoff": 1700, "chorus.on": 1}),
    ("Frozen Aurora", "frozen,aurora,crystal", "Cold crystalline northern lights pad", {"osc1.wavetable": 10, "osc1.unison.voices": 5, "env1.attack": 1300, "env1.release": 1700, "filter1.cutoff": 2600, "delay.on": 1, "reverb.on": 1}),
    ("Velvet Strings", "velvet,strings,orchestral", "Silky velvet orchestral synth strings", {"osc1.wavetable": 1, "osc1.unison.voices": 6, "env1.attack": 650, "env1.release": 1100, "filter1.cutoff": 2100, "chorus.on": 1}),
    ("Comb Reso Swell", "comb,reso,swell", "Acoustic-like resonant comb filter swell", {"osc1.wavetable": 1, "filter1.model": 6, "filter1.cutoff": 1100, "filter1.resonance": 60, "env1.attack": 1000, "env1.release": 1500}),
    ("Cathedral Choir", "cathedral,sacred,reverb", "Huge cathedral reverberant choir", {"osc1.wavetable": 9, "env1.attack": 1200, "env1.release": 2000, "filter1.cutoff": 1800, "reverb.on": 1, "reverb.mix": 70}),
    ("Slow Motion Waves", "waves,slow,motion", "Slow moving morphing wavetable sweep", {"osc1.wavetable": 5, "osc1.morph.type": 1, "osc1.morph.amount": 60, "env1.attack": 1500, "env1.release": 1800, "filter1.cutoff": 2000}),
    ("Gentle Flute Pad", "flute,gentle,soft", "Breathy gentle flute pad", {"osc1.wavetable": 3, "noise.on": 1, "noise.level": 12, "env1.attack": 450, "env1.release": 800, "filter1.cutoff": 1900}),
    ("Radiant Pulse Pad", "pulse,radiant,rhythmic", "Pulsing radiant warmth", {"osc1.wavetable": 4, "env1.attack": 700, "env1.release": 1200, "filter1.cutoff": 1600, "lfo1.rate": 1.0, "chorus.on": 1}),
    ("Infinite Horizon", "infinite,horizon,cinematic", "Massive cinematic horizon landscape pad", {"osc1.wavetable": 1, "osc1.unison.voices": 7, "osc2.on": 1, "osc2.wavetable": 7, "env1.attack": 2200, "env1.release": 3000, "filter1.cutoff": 1700, "reverb.on": 1, "reverb.mix": 60})
]
for name, tags, desc, ov in pad_names:
    presets.append((name, "Pad", tags, desc, ov))

# --- 15 Pluck Presets ---
pluck_names = [
    ("Acoustic Style Pluck", "acoustic,pluck,folk", "Crisp acoustic guitar transient pluck", {"osc1.wavetable": 1, "env1.attack": 2, "env1.decay": 240, "env1.sustain": 0, "env1.release": 120, "filter1.cutoff": 4200}),
    ("Bell Mallet Pluck", "bell,mallet,percussive", "Percussive bell mallet strike", {"osc1.wavetable": 12, "env1.attack": 1, "env1.decay": 380, "env1.sustain": 0, "filter1.cutoff": 5500, "delay.on": 1}),
    ("Marimba Synth", "marimba,wood,percussion", "Warm wooden marimba bar strike", {"osc1.wavetable": 3, "env1.attack": 2, "env1.decay": 280, "env1.sustain": 0, "filter1.cutoff": 2200, "filter1.resonance": 20}),
    ("Pizzicato Strings", "pizzicato,classical,strings", "Orchestral pizzicato string section pluck", {"osc1.wavetable": 1, "osc1.unison.voices": 4, "env1.decay": 220, "env1.sustain": 0, "filter1.cutoff": 3400}),
    ("Kalimba Tines", "kalimba,thumb,tine", "African thumb piano kalimba tines", {"osc1.wavetable": 0, "osc2.on": 1, "osc2.wavetable": 12, "osc2.transpose": 12, "env1.decay": 320, "env1.sustain": 0, "filter1.cutoff": 3800}),
    ("Trance Short Pluck", "trance,dance,short", "Punchy short trance sequence pluck", {"osc1.wavetable": 1, "env1.decay": 160, "env1.sustain": 0, "filter1.cutoff": 2800, "delay.on": 1}),
    ("Formant Pop Pluck", "formant,pop,quirky", "Vocal pop staccato pluck", {"osc1.wavetable": 9, "filter1.model": 5, "env1.decay": 190, "env1.sustain": 0, "filter1.cutoff": 1900}),
    ("Harp Drops", "harp,crystal,fluid", "Fluid crystalline concert harp drops", {"osc1.wavetable": 3, "env1.decay": 450, "env1.sustain": 0, "filter1.cutoff": 4000, "delay.on": 1, "reverb.on": 1}),
    ("Comb Zither", "comb,zither,folk", "Resonant physical plucked zither", {"osc1.wavetable": 1, "filter1.model": 6, "filter1.cutoff": 1800, "filter1.resonance": 75, "env1.decay": 350, "env1.sustain": 0}),
    ("Chime Drop", "chime,drop,glass", "Delicate metallic chime drop", {"osc1.wavetable": 12, "env1.decay": 550, "env1.sustain": 0, "filter1.cutoff": 6200, "reverb.on": 1}),
    ("Harmonic Tine Pluck", "harmonic,tine,electric", "Rich harmonic tine pluck", {"osc1.wavetable": 7, "env1.decay": 290, "env1.sustain": 0, "filter1.cutoff": 3600}),
    ("Electric Mute Pluck", "mute,electric,punch", "Palm muted electric guitar synth pluck", {"osc1.wavetable": 4, "env1.decay": 140, "env1.sustain": 0, "filter1.cutoff": 2100, "distortion.on": 1, "distortion.drive": 10}),
    ("Spruce Guitar", "spruce,acoustic,clean", "Clean spruce top acoustic guitar pluck", {"osc1.wavetable": 5, "env1.decay": 310, "env1.sustain": 0, "filter1.cutoff": 3900}),
    ("Digital Glass Tap", "digital,glass,tap", "Modern glass UI tap tone", {"osc1.wavetable": 10, "env1.decay": 110, "env1.sustain": 0, "filter1.cutoff": 4800}),
    ("Rubber Bass Pluck", "rubber,bounce,funky", "Bouncy rubber band synth pluck", {"osc1.wavetable": 2, "env1.decay": 200, "env1.sustain": 0, "filter1.cutoff": 1600, "filter1.resonance": 35})
]
for name, tags, desc, ov in pluck_names:
    presets.append((name, "Pluck", tags, desc, ov))

# --- 15 Keys Presets ---
keys_names = [
    ("Vintage Electric Tine", "epiano,rhodes,tine", "Warm classic electric piano with tines", {"osc1.wavetable": 0, "osc2.on": 1, "osc2.wavetable": 12, "osc2.level": 40, "env1.decay": 900, "env1.sustain": 20, "filter1.cutoff": 2600, "chorus.on": 1}),
    ("Dream Grand Piano", "piano,dream,acoustic", "Dreamy acoustic grand style piano", {"osc1.wavetable": 3, "osc2.on": 1, "osc2.wavetable": 7, "env1.decay": 1100, "env1.sustain": 15, "filter1.cutoff": 3200, "reverb.on": 1}),
    ("FM Soft Organ", "fm,organ,gospel", "Smooth FM tonewheel gospel organ", {"osc1.wavetable": 10, "env1.decay": 600, "env1.sustain": 80, "env1.release": 150, "filter1.cutoff": 4500}),
    ("Rotary Rock Organ", "organ,rotary,leslie", "Gritty rock organ through rotary chorus", {"osc1.wavetable": 8, "filter1.cutoff": 5000, "chorus.on": 1, "chorus.mix": 70, "distortion.on": 1, "distortion.drive": 7.5}),
    ("Harpsichord Bar", "harpsichord,baroque,bright", "Crisp baroque harpsichord plucking", {"osc1.wavetable": 4, "filter1.cutoff": 6000, "env1.decay": 400, "env1.sustain": 0, "env1.release": 80}),
    ("Clavinet Funk", "clavinet,funk,retro", "Funky 70s clavinet keyboard", {"osc1.wavetable": 2, "filter1.cutoff": 3500, "filter1.resonance": 40, "env1.decay": 300, "env1.sustain": 0, "distortion.on": 1, "distortion.drive": 10}),
    ("Celesta Bells", "celesta,bell,fairytale", "Fairytale orchestral celesta keyboard", {"osc1.wavetable": 12, "osc1.transpose": 12, "env1.decay": 700, "env1.sustain": 0, "filter1.cutoff": 5200, "reverb.on": 1}),
    ("Vibraphone Bars", "vibraphone,jazz,warm", "Warm jazz vibraphone with subtle tremolo", {"osc1.wavetable": 0, "filter1.cutoff": 2800, "lfo1.rate": 4.5, **mod(1, "lfo1", "osc1.level", 20), "reverb.on": 1}),
    ("Muted Honky Tonk", "honkytonk,saloon,detune", "Saloon style detuned upright piano", {"osc1.wavetable": 1, "osc1.unison.voices": 3, "osc1.unison.detune": 18, "env1.decay": 800, "env1.sustain": 10, "filter1.cutoff": 3000}),
    ("Accordion Reed", "accordion,reed,folk", "Dual reed folk accordion tone", {"osc1.wavetable": 4, "osc2.on": 1, "osc2.wavetable": 4, "osc2.tune": 8, "filter1.cutoff": 4200, "chorus.on": 1}),
    ("Warm Toy Piano", "toy,piano,indie", "Charming lo-fi indie toy piano", {"osc1.wavetable": 2, "filter1.cutoff": 2200, "env1.decay": 350, "env1.sustain": 0, "master.volume": -8}),
    ("Glass Organ Tones", "glass,organ,ambient", "Crystalline glass church organ", {"osc1.wavetable": 12, "filter1.cutoff": 3800, "env1.attack": 80, "env1.release": 400, "reverb.on": 1}),
    ("Lo-Fi Rhodes", "rhodes,lofi,chillhop", "Mellow chillhop electric piano", {"osc1.wavetable": 3, "filter1.cutoff": 1800, "filter1.resonance": 15, "env1.decay": 750, "env1.sustain": 25, "chorus.on": 1}),
    ("B3 Drawbar Organ", "b3,drawbar,jazz", "Warm drawbar jazz organ", {"osc1.wavetable": 7, "filter1.cutoff": 4000, "env1.release": 50}),
    ("Dyno Electric Piano", "dyno,bright,bell", "Bright modified 80s dyno tine piano", {"osc1.wavetable": 11, "filter1.cutoff": 4800, "env1.decay": 850, "env1.sustain": 20, "chorus.on": 1})
]
for name, tags, desc, ov in keys_names:
    presets.append((name, "Keys", tags, desc, ov))

# --- 10 FX Presets ---
fx_names = [
    ("Sci-Fi Laser Sweep", "laser,sweep,sci-fi", "Pitch modulated downward laser burst", {"osc1.wavetable": 1, "env2.decay": 120, "env2.sustain": 0, "filter1.cutoff": 6000}),
    ("Noise Riser", "noise,riser,build", "Filtered white noise build-up riser", {"noise.on": 1, "noise.level": 80, "osc1.on": 0, "filter1.cutoff": 1200, "reverb.on": 1}),
    ("Metal Gong Strike", "gong,metal,percussion", "Deep resonant physical metallic gong", {"osc1.wavetable": 12, "filter1.model": 6, "filter1.cutoff": 800, "filter1.resonance": 85, "env1.decay": 1800, "reverb.on": 1}),
    ("Sub Drop Boom", "sub,drop,boom", "Subwoofer testing downward pitch drop", {"master.transpose": 0, "osc1.wavetable": 0, "env1.decay": 1400, "env1.sustain": 0, "master.volume": -4}),
    ("Space Wind Storm", "wind,storm,ambient", "Howling space wind noise atmosphere", {"master.transpose": -24, "noise.on": 1, "noise.level": 70, "filter1.cutoff": 225, "filter1.resonance": 60, "lfo1.rate": 0.2}),
    ("Alien Communication", "alien,vocal,weird", "Strange resonant formant alien chatter", {"osc1.wavetable": 9, "filter1.model": 5, "lfo1.rate": 6.5, **mod(1, "lfo1", "filter1.formant.x", 60), "delay.on": 1}),
    ("Game Over Zap", "game,retro,zap", "8-bit retro arcade game over explosion", {"osc1.wavetable": 2, "distortion.on": 1, "distortion.drive": 10, "env1.decay": 400, "env1.sustain": 0}),
    ("Sonar Ping", "sonar,ping,submarine", "Deep ocean submarine sonar ping", {"osc1.wavetable": 0, "osc1.level": 60, "filter1.cutoff": 1400, "filter1.resonance": 75, "env1.decay": 600, "delay.on": 1, "reverb.on": 1}),
    ("Cosmic Teleport", "teleport,sweep,glitch", "Ascending dimensional teleport sweep", {"master.transpose": 0, "osc1.wavetable": 6, "filter1.cutoff": 3500, "lfo1.rate": 8.0, "delay.on": 1}),
    ("Vinyl Dust Crackle", "vinyl,crackle,retro", "Warm lo-fi vinyl dust noise generator", {"noise.on": 1, "noise.level": 40, "filter1.cutoff": 1500, "filter1.resonance": 20})
]
for name, tags, desc, ov in fx_names:
    presets.append((name, "FX", tags, desc, ov))


assert len(presets) == 100, f"Expected 100 presets, got {len(presets)}"
FIRST_HUNDRED = [p[0] for p in presets]


# ---------------------------------------------------------------------------------------------------------------------------
# Macro wiring (docs/ui-design/09-kick-integration.md 2.3, decision Q29): every preset routes macro.1 .. macro.4 to the destinations
# of its category through the mod matrix (source macroN, amount in percent of the destination range, unipolar, power 0). The macros
# stay at their default 0, so the sound at the default positions is the sound without the wiring. Macros 5 .. 8 are wired
# sparingly, only where the preset has something for them to open (unison, a second oscillator, noise, a filter).
# ---------------------------------------------------------------------------------------------------------------------------
CATEGORY_MACROS = {
    "Bass": {
        1: [("filter1.cutoff", 45), ("filter2.cutoff", 45)],
        2: [("filter1.drive", 50), ("distortion.drive", 35), ("osc1.warp.amount", 25)],
        3: [("env2.decay", 40), ("lfo1.rate", 30)],
        4: [("osc1.unison.detune", 25), ("chorus.mix", 35)],
    },
    "Lead": {
        1: [("filter1.cutoff", 50), ("osc1.morph.amount", 30)],
        2: [("osc1.warp.amount", 45), ("osc1.frame", 50)],
        3: [("osc1.unison.detune", 30), ("osc2.unison.detune", 30), ("filter1.drive", 35)],
        4: [("delay.mix", 35), ("reverb.mix", 30)],
    },
    "Pad": {
        1: [("filter1.cutoff", 55), ("filter1.resonance", 15)],
        2: [("osc1.frame", 60), ("osc2.frame", 60)],
        3: [("lfo1.rate", 25), ("env1.attack", -30), ("env1.release", 35)],
        4: [("reverb.mix", 40), ("reverb.decay", 30), ("chorus.mix", 30)],
    },
    "Pluck": {
        1: [("filter1.cutoff", 50), ("env2.decay", 45)],
        2: [("env1.decay", 50), ("env1.release", 40)],
        3: [("osc1.morph.amount", 40), ("noise.level", 25)],
        4: [("delay.mix", 40), ("reverb.mix", 30)],
    },
    "Keys": {
        1: [("filter1.cutoff", 40), ("osc1.warp.amount", 30)],
        2: [("lfo1.rate", 35), ("phaser.mix", 40)],
        3: [("env1.release", 45), ("env1.decay", 35)],
        4: [("chorus.mix", 35), ("reverb.mix", 25)],
    },
    "FX": {
        1: [("filter1.cutoff", 70), ("fxfilter.cutoff", 70)],
        2: [("rlfo1.rate", 60), ("lfo1.rate", 60)],
        3: [("distortion.drive", 55), ("osc1.warp.amount", 50)],
        4: [("reverb.mix", 50), ("delay.feedback", 40)],
    },
}


def macro_assignments(category: str, vals: dict) -> list:
    """[(macro number, destination key, amount)] for a preset whose resolved values are `vals`."""
    out = []
    for macro, targets in CATEGORY_MACROS[category].items():
        out += [(macro, key, amount) for key, amount in targets]
    if vals["osc1.unison.voices"] > 1:
        out.append((5, "osc1.unison.spread", 40))
    if vals["osc2.on"] and vals["osc2.unison.voices"] > 1:
        out.append((5, "osc2.unison.spread", 40))
    if vals["filter1.on"] and category != "Pad":
        out.append((6, "filter1.resonance", 30))
    if vals["osc2.on"]:
        out += [(7, "osc2.unison.detune", 25), (7, "osc2.warp.amount", 30)]
    if vals["noise.on"]:
        out.append((8, "noise.level", 35))
    return out


# A macro that opens an effect is only worth wiring if the effect is in the signal path: where the preset leaves the effect off, it is
# switched on with its mix at 0 (a dry pass, the same sound at the macro default) and the macro also opens the mix.
EFFECT_OF_TARGET = {
    "chorus.mix": "chorus", "delay.mix": "delay", "delay.feedback": "delay", "reverb.mix": "reverb", "reverb.decay": "reverb",
    "phaser.mix": "phaser", "distortion.drive": "distortion", "fxfilter.cutoff": "fxfilter",
}
EXTRA_MIX_AMOUNT = {"delay": 40, "reverb": 40, "distortion": 100, "fxfilter": 70}


# LFO 1 / random LFO 1 route a preset gets when a macro opens `lfo1.rate` / `rlfo1.rate` and the preset itself does not route that
# source: (destination, amount in percent). A macro on the rate of an LFO nothing listens to would be dead.
LFO_ROUTE = {
    "Bass": ("filter1.cutoff", 12), "Pad": ("filter1.cutoff", 15), "Keys": ("filter1.cutoff", 8), "FX": ("filter1.cutoff", 30),
}
# Rate given to an LFO the generator routes itself, where the preset leaves it at the default (a pad moves slowly).
LFO_ROUTE_RATE = {"Pad": 0.4}
RATE_OF_SOURCE = {"lfo1": "lfo1.rate", "rlfo1": "rlfo1.rate"}


def slot_routes(values: dict):
    """[(slot, source key, destination key, amount, bypass)] of every mod slot of `values` that has a source and a destination."""
    src_name = {v: k for k, v in mod_source.items()}
    dst_name = {v: k for k, v in mod_destination.items()}
    out = []
    for n in range(1, NUM_MOD_SLOTS + 1):
        p = f"mod{n:02d}."
        src, dst = int(values.get(p + "source", 0)), int(values.get(p + "destination", 0))
        if src and dst:
            out.append((n, src_name[src], dst_name[dst], float(values.get(p + "amount", 0)), bool(values.get(p + "bypass", 0))))
    return out


def lfo_routed(source: str, values: dict) -> bool:
    return any(s == source and amount != 0 and not bypass for _, s, _, amount, bypass in slot_routes(values))


def destination_alive(key: str, values: dict) -> bool:
    """Whether moving `key` changes the sound of a preset with `values` (its section is on, an LFO rate has a route, ...)."""
    gate = key.split(".")[0]
    for on in (gate + ".on", ):
        if on in param_info and not values[on]:
            return False
    if ".unison." in key and not key.endswith(".voices"):
        osc = key.split(".")[0]
        if values[osc + ".unison.voices"] <= 1:
            return False
    for source, rate in RATE_OF_SOURCE.items():
        if key == rate and not lfo_routed(source, values):
            return False
    return True


def check_macros(name: str, values: dict) -> None:
    """Every wired macro must do something: at least one of its routes has an amount, a live destination and (for an LFO rate) a routed
    LFO. A route on the rate of an unrouted LFO is an error on its own."""
    dst_name = {v: k for k, v in mod_destination.items()}
    live = {}
    for n, source, dest, amount, bypass in slot_routes(values):
        if not source.startswith("macro"):
            continue
        macro = int(source[5:])
        ok = amount != 0 and not bypass and destination_alive(dest, values)
        if dest in RATE_OF_SOURCE.values() and not ok:
            raise ValueError(f"{name}: macro {macro} opens {dest} but no route uses that LFO (or its amount is 0)")
        live[macro] = live.get(macro, False) or ok
    dead = sorted(m for m, ok in live.items() if not ok)
    if dead:
        raise ValueError(f"{name}: macro(s) {dead} do nothing (zero amount or every destination is off/unrouted)")


def with_macros(category: str, ov: dict) -> dict:
    """`ov` plus the macro wiring in the mod slots after the highest one `ov` already uses."""
    vals = dict(default_values)
    vals.update({k: float(v) for k, v in ov.items() if k in param_info})
    out = dict(ov)
    assignments = macro_assignments(category, vals)
    # an LFO whose rate a macro opens must be routed by the preset; if it is not, add the route of the category
    for source, rate in RATE_OF_SOURCE.items():
        if any(k == rate for _, k, _ in assignments) and not lfo_routed(source, {**default_values, **out}):
            dest, amount = LFO_ROUTE[category]
            used = [int(k[3:5]) for k in out if k.startswith("mod") and k[3:5].isdigit()]
            out.update(mod(max(used, default=0) + 1, source, dest, amount))
            if category in LFO_ROUTE_RATE and rate not in out:
                out[rate] = LFO_ROUTE_RATE[category]
    used = [int(k[3:5]) for k in out if k.startswith("mod") and k[3:5].isdigit()]
    slot = max(used, default=0) + 1
    routed = set((m, k) for m, k, _ in assignments)
    for macro, key, amount in list(assignments):
        fx = EFFECT_OF_TARGET.get(key)
        if fx is None or vals[fx + ".on"]:
            continue
        out[fx + ".on"] = 1
        if (fx + ".mix") not in ov:
            out[fx + ".mix"] = 0
        if not key.endswith(".mix") and (macro, fx + ".mix") not in routed:
            assignments.append((macro, fx + ".mix", EXTRA_MIX_AMOUNT[fx]))
            routed.add((macro, fx + ".mix"))
    for macro, key, amount in assignments:
        if slot > NUM_MOD_SLOTS:
            raise ValueError("out of mod slots")
        p = f"mod{slot:02d}."
        out[p + "source"] = mod_source[f"macro{macro}"]
        out[p + "destination"] = mod_destination[key]
        out[p + "amount"] = amount
        slot += 1
    return out


def load_pad_pressure() -> dict:
    """{preset name: amount in percent} of the factory presets that route Poly Aftertouch to the cutoff of Filter 1 themselves (scripts/propose-pad-pressure.py writes it, the owner corrects it by hand);
    a preset without an entry has no such route."""
    if not os.path.isfile(PAD_PRESSURE_PATH):
        return {}
    with open(PAD_PRESSURE_PATH, "r", encoding="utf-8") as f:
        doc = json.load(f)
    out = {}
    for name, amount in doc.get("presets", {}).items():
        if not isinstance(amount, (int, float)) or not 0 < amount <= 100:
            raise ValueError(f"{PAD_PRESSURE_PATH}: {name}: the amount must be a percent above 0 and at most 100")
        out[name] = amount
    return out


def with_pad_pressure(name: str, ov: dict, amounts: dict) -> dict:
    """`ov` plus the route Poly Aftertouch -> Filter 1 Cutoff in the next free mod slot, for a preset that has an amount in the pad pressure data."""
    if name not in amounts:
        return ov
    out = dict(ov)
    used = [int(k[3:5]) for k in out if k.startswith("mod") and k[3:5].isdigit()]
    slot = max(used, default=0) + 1
    if slot > NUM_MOD_SLOTS:
        raise ValueError(f"{name}: no free mod slot for the pad pressure route")
    out.update(mod(slot, "poly_aftertouch", "filter1.cutoff", amounts[name]))
    return out


def file_name_of(name: str) -> str:
    return name.lower().replace(" ", "_").replace("-", "_") + ".sbpreset"


# ---------------------------------------------------------------------------------------------------------------------------
# Showcase presets (12): the engine features of state version 3. Every key used here is live in the Vital engine (all bits of
# engine_caps.h are set by engine-vital; the sample oscillator needs sample content and is not shown). Only real table keys
# (strict), values inside their ranges (checked), content only where the preset needs it.
# ---------------------------------------------------------------------------------------------------------------------------
def merge(*parts: dict) -> dict:
    out = {}
    for part in parts:
        out.update(part)
    return out


# A custom LFO shape (lfoN.shape = Custom, content.lfo_shapes): three hops that fall back, smoothed.
HOP_SHAPE = {"lfo": 1, "name": "Triple Hop", "smooth": True,
             "points": [(0.0, 0.0, 0.0), (0.12, 1.0, 0.0), (0.3, 0.25, 0.0), (0.46, 0.85, 0.0), (0.66, 0.1, 0.0), (0.82, 0.55, 0.0), (1.0, 0.0, 0.0)]}

showcase = [
    ("Scream Filter Bass", "Bass", "serum-filter,scream,aggressive",
     "Saw bass through the Scream filter model, envelope-driven cutoff",
     merge({"osc1.wavetable": 1, "osc1.unison.voices": 3, "osc1.unison.detune": 18, "filter1.model": 16, "filter1.cutoff": 600,
            "filter1.resonance": 55, "filter1.drive": 6, "env1.release": 150, "env2.decay": 260, "env2.sustain": 8},
           mod(1, "env2", "filter1.cutoff", 55)), None),
    ("Diffusor Cloud Pad", "Pad", "serum-filter,diffusor,smeared,ambient",
     "Detuned saw pad smeared by the Diffusor filter, slow filter drift",
     merge({"osc1.wavetable": 1, "osc1.unison.voices": 5, "osc1.unison.detune": 30, "osc1.unison.spread": 80, "filter1.model": 19,
            "filter1.cutoff": 3800, "filter1.resonance": 45, "env1.attack": 700, "env1.release": 1500, "lfo1.rate": 0.3,
            "reverb.on": 1, "reverb.mix": 28, "reverb.decay": 3200},
           mod(1, "lfo1", "filter1.cutoff", 30)), None),
    ("EMS Squelch Bass", "Bass", "serum-filter,ems,squelch,acid",
     "Resonant EMS ladder bass with a short filter envelope",
     merge({"osc1.wavetable": 2, "filter1.model": 20, "filter1.cutoff": 380, "filter1.resonance": 70, "filter1.drive": 4,
            "env1.decay": 450, "env1.sustain": 30, "env2.decay": 220, "env2.sustain": 0},
           mod(1, "env2", "filter1.cutoff", 60)), None),
    ("Twin Warp Lead", "Lead", "warp2,sync,bend,modern",
     "Two warp stages in series (Sync then Bend), the second one swept by an LFO",
     merge({"osc1.wavetable": 1, "osc1.warp.type": 1, "osc1.warp.amount": 35, "osc1.warp2.type": 4, "osc1.warp2.amount": 40,
            "filter1.cutoff": 5200, "lfo1.rate": 0.8, "delay.on": 1, "delay.mix": 18},
           mod(1, "lfo1", "osc1.warp2.amount", 35)), None),
    ("Aux Keyscale Pluck", "Pluck", "aux-modulation,lfo,keyscale,rhythmic",
     "Pluck whose LFO filter sweep deepens up the keyboard: the aux source of the route is the key played",
     merge({"osc1.wavetable": 1, "env1.decay": 700, "env1.sustain": 0, "env1.release": 300, "filter1.cutoff": 700, "filter1.resonance": 35,
            "lfo1.rate": 7},
           mod(1, "lfo1", "filter1.cutoff", 90, aux="note"),
           mod(2, "env1", "osc1.warp.amount", 30, aux="mod_wheel"), {"osc1.warp.type": 1}), None),
    ("Hyper Dimension Pad", "Pad", "hyper,dimension,wide,serum-fx",
     "Sine-to-saw pad widened by the Hyper/Dimension effect",
     {"osc1.wavetable": 6, "osc1.frame": 55, "env1.attack": 500, "env1.release": 1200, "filter1.cutoff": 6500,
      "hyperdim.on": 1, "hyperdim.unison": 5, "hyperdim.detune": 55, "hyperdim.mix": 60, "hyperdim.dim_size": 70, "hyperdim.dim_mix": 40,
      "reverb.on": 1, "reverb.mix": 20}, None),
    ("Utility Width Keys", "Keys", "utility,stereo,mono-bass,wide",
     "Chorused triangle-and-square keys, widened above 150 Hz with a mono low end from the Utility effect",
     {"osc1.wavetable": 3, "osc2.on": 1, "osc2.wavetable": 2, "osc2.level": 30, "osc2.transpose": 12, "env1.decay": 800, "env1.sustain": 35,
      "filter1.cutoff": 3000, "chorus.on": 1, "chorus.mix": 40, "utility.on": 1, "utility.width": 170, "utility.mono_bass": 1,
      "utility.mono_freq": 150, "utility.hpf": 30}, None),
    ("Bode Shifter Drone", "FX", "bode,frequency-shifter,drone,metallic",
     "Slow saw drone through the Bode frequency shifter with feedback",
     {"osc1.wavetable": 1, "osc1.unison.voices": 3, "osc1.unison.detune": 20, "env1.attack": 800, "env1.release": 1800, "filter1.cutoff": 5000,
      "bode.on": 1, "bode.shift": 20, "bode.range": 400, "bode.feed": 40, "bode.blur": 30, "bode.mix": 60, "reverb.on": 1, "reverb.mix": 25}, None),
    ("Custom Shape Wobble", "Bass", "custom-lfo,wobble,rhythmic",
     "Wobble bass whose filter follows a hand-drawn three-hop LFO shape stored in the preset",
     merge({"osc1.wavetable": 1, "osc1.unison.voices": 2, "osc1.unison.detune": 12, "filter1.cutoff": 450, "filter1.resonance": 40,
            "lfo1.shape": 12, "lfo1.rate": 3},
           mod(1, "lfo1", "filter1.cutoff", 70)), {"lfo_shapes": [HOP_SHAPE]}),
    ("MPE Pressure Lead", "Lead", "mpe,expressive,pressure,slide",
     "MPE lead: per-note pressure opens the filter, slide pushes the warp, bend range 48",
     merge({"master.mpe.on": 1, "master.mpe.bend_range": 48, "master.polyphony": 8, "osc1.wavetable": 1, "osc1.warp.type": 1,
            "osc1.warp.amount": 15, "filter1.cutoff": 1500, "filter1.resonance": 30, "env1.release": 300, "reverb.on": 1, "reverb.mix": 15},
           mod(1, "poly_aftertouch", "filter1.cutoff", 55),
           mod(2, "slide", "osc1.warp.amount", 50),
           mod(3, "poly_aftertouch", "osc1.unison.detune", 20)), None),
    ("Just Intonation Keys", "Keys", "tuning,just-intonation,bell,pure",
     "Harmonic-series keys in just intonation, A4 at 440 Hz",
     {"master.tuning.scale": 2, "master.tuning.a4": 440, "osc1.wavetable": 7, "env1.decay": 900, "env1.sustain": 20, "env1.release": 700,
      "filter1.cutoff": 6000, "reverb.on": 1, "reverb.mix": 22, "chorus.on": 1, "chorus.mix": 25}, None),
    ("Tri-Band Split Texture", "Pad", "splitter,racks,multiband,texture",
     "Three-band split: distortion on the lows, wide Utility on the mids, chorus on the highs",
     {"osc1.wavetable": 1, "osc1.unison.voices": 5, "osc1.unison.detune": 40, "env1.attack": 300, "env1.release": 900, "filter1.cutoff": 9000,
      "splitter_lmh.on": 1, "splitter_lmh.freq_low": 220, "splitter_lmh.freq_high": 2500,
      "distortion.on": 1, "distortion.rack": 5, "distortion.drive": 30, "distortion.mix": 60,
      "utility.on": 1, "utility.rack": 6, "utility.width": 160,
      "chorus.on": 1, "chorus.rack": 7, "chorus.mix": 60,
      **mod(1, "velocity", "distortion.drive", 25), **mod(2, "velocity", "osc1.level", 25)}, None),
]
assert len(showcase) == 12
assert len(set(p[0] for p in presets) | set(s[0] for s in showcase)) == 112, "preset names must be unique"
assert len(set(file_name_of(p[0]) for p in presets) | set(file_name_of(s[0]) for s in showcase)) == 112


BATCH_FILES = ("factory-presets-batch1.py", "factory-presets-batch2.py")   # the sound set batches after the first hundred and the showcase (scripts/sb_preset_dsl.py is their vocabulary)


def load_batches() -> list:
    """[(name, category, tags, description, params incl. mod routes, content)] of the batch files, in order. A batch preset is written whole (its macro wiring and pad pressure route are in its own routes)."""
    if SCRIPT_DIR not in sys.path:
        sys.path.insert(0, SCRIPT_DIR)
    out = []
    for fname in BATCH_FILES:
        path = os.path.join(SCRIPT_DIR, fname)
        if not os.path.isfile(path):
            continue
        spec = importlib.util.spec_from_file_location("factory_batch_" + fname.split(".")[0].replace("-", "_"), path)
        mod_ = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod_)
        out += mod_.build()
    return out


def load_levels() -> dict:
    """master.volume per preset name (level normalisation, scripts/tune-preset-levels.py); a preset without an entry keeps its own."""
    if not os.path.isfile(LEVELS_PATH):
        return {}
    with open(LEVELS_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


# Category velocity tracking (%): 100.0 for EVERY category (owner decision 2026-11-20: engine law lerp(1, v^2, t);
# Bass and Lead had 5 and 0 to copy stock mono synths, which made pad hits and the Full Level mode look dead).
CATEGORY_VELOCITY_TRACK = {
    "Bass": 100.0,
    "Lead": 100.0,
    "Pad": 100.0,
    "FX": 100.0,
    "Keys": 100.0,
    "Pluck": 100.0,
}


def build_all():
    """All presets as (name, category, meta, document text), in a fixed order."""
    out = []
    levels = load_levels()
    batches = load_batches()
    known = set(p[0] for p in presets) | set(x[0] for x in showcase) | set(b[0] for b in batches)
    assert len(known) == len(presets) + len(showcase) + len(batches), "preset names must be unique"
    assert len(set(file_name_of(n) for n in known)) == len(known), "preset file names must be unique"
    unknown = sorted(set(levels) - known)
    if unknown:
        raise KeyError(f"{LEVELS_PATH}: no preset named {unknown}")
    pad_pressure = load_pad_pressure()
    unknown = sorted(set(pad_pressure) - known)
    if unknown:
        raise KeyError(f"{PAD_PRESSURE_PATH}: no preset named {unknown}")
    macro_names = load_macro_names()
    unknown = sorted(set(macro_names) - known)
    if unknown:
        raise KeyError(f"{MACRO_NAMES_PATH}: no preset named {unknown}")
    for name, category, tags, desc, ov in presets:
        ov = with_pad_pressure(name, with_macros(category, ov), pad_pressure)
        ov.setdefault("master.velocity_track", CATEGORY_VELOCITY_TRACK.get(category, 100.0))
        check_macros(name, {**default_values, **{k: float(v) for k, v in ov.items()}})
        if name in levels:
            ov["master.volume"] = levels[name]
        content = {"macro_names": macro_names[name]} if name in macro_names else None
        meta, doc = make_preset(name, category, tags, desc, ov, content=content, strict=False)
        out.append((name, category, meta, doc))
    for name, category, tags, desc, ov, content in showcase:
        ov = with_pad_pressure(name, with_macros(category, ov), pad_pressure)
        ov.setdefault("master.velocity_track", CATEGORY_VELOCITY_TRACK.get(category, 100.0))
        check_macros(name, {**default_values, **{k: float(v) for k, v in ov.items()}})
        if name in levels:
            ov["master.volume"] = levels[name]
        if name in macro_names:
            content = {**(content or {}), "macro_names": macro_names[name]}
        meta, doc = make_preset(name, category, tags, desc, ov, content=content, strict=True)
        out.append((name, category, meta, doc))
    for name, category, tags, desc, ov, content in batches:
        ov = dict(ov)
        ov.setdefault("master.velocity_track", CATEGORY_VELOCITY_TRACK.get(category, 100.0))
        check_macros(name, {**default_values, **{k: float(v) for k, v in ov.items()}})
        if name in levels:
            ov["master.volume"] = levels[name]
        if name in macro_names:
            content = {**(content or {}), "macro_names": macro_names[name]}
        meta, doc = make_preset(name, category, tags, desc, ov, content=content, strict=True)
        out.append((name, category, meta, doc))
    return out


def file_name(name: str) -> str:
    return file_name_of(name)


def manifest_text(entries) -> str:
    pack_files = []
    for name, category, meta, doc in entries:
        data = doc.encode("utf-8")
        pack_files.append({"path": f"{category}/{file_name(name)}", "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)})
    pack_manifest = {
        "schemaVersion": 1,
        "id": "com.safian.synthyboy.factory-presets",
        "name": "Factory Presets",
        "version": "1.2.0",
        "license": "CC0-1.0",
        "attribution": "Safian Szabolcs",
        "kind": "presets",
        "requires_plugin": ">=0.1.0",
        "description": f"{len(entries)} factory presets covering Bass, Lead, Pad, Pluck, Keys and FX under CC0-1.0 license.",
        "files": pack_files
    }
    return json.dumps(pack_manifest, indent=2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUTPUT_DIR)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    entries = build_all()
    files = {f"{category}/{file_name(name)}": doc.encode("utf-8") for name, category, meta, doc in entries}
    files["contentpack.json"] = manifest_text(entries).encode("utf-8")

    if args.check:
        bad = []
        for rel, data in files.items():
            path = os.path.join(args.out, rel)
            if not os.path.isfile(path) or open(path, "rb").read() != data:
                bad.append(rel)
        for cat in CATEGORIES:
            d = os.path.join(args.out, cat)
            if os.path.isdir(d):
                bad += [f"{cat}/{n} (not generated)" for n in sorted(os.listdir(d)) if f"{cat}/{n}" not in files]
        if bad:
            print("factory presets differ from the generator:", *bad, sep="\n  ")
            return 1
        print(f"factory presets up to date ({len(entries)} presets)")
        return 0

    for rel, data in files.items():
        path = os.path.join(args.out, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    print(f"Generated {len(entries)} presets in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
