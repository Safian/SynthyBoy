#!/usr/bin/env python3
# SynthyBoy - wavetable lab: generate wavetable candidates from recipe grammars, score them, render audition material, promote the good ones into the factory pack (or an add-on pack).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   wavetable-lab.py generate [--seed S] [--per-category 40] [--categories Analog ...] [--out DIR]
#   wavetable-lab.py score    --run DIR [--keep 8]
#   wavetable-lab.py preview  --run DIR
#   wavetable-lab.py propose  --from-file FILE [--category C] [--run DIR]
#   wavetable-lab.py promote  NAME --run DIR --category C --title "Title" [--slug S] [--pack ID [--pack-name TEXT]] [--force]
#   wavetable-lab.py list     [--run DIR]
#   wavetable-lab.py check    [--run DIR]
#
# Format of a recipe and the building blocks: scripts/wavetable_lib.py; the agent-neutral manual: .agents/skills/wavetable-lab/SKILL.md; the developer note: docs/CONTENT-PACKS.md.
# Deterministic (every random choice comes from the seed), numpy and the standard library only, no network, no model: a local model only writes recipe text that `propose` validates.
# Generated material goes OUTSIDE the repository (default below) until `promote` copies a recipe into content-src/.

import argparse
import base64
import datetime
import hashlib
import html
import importlib.util
import json
import math
import os
import re
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wavetable_lib as wl  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, ".."))
DEFAULT_OUT_ROOT = "/Users/szabi/Documents/Projects/Safian/.hw-local/wavetable-lab"
SR = 44100
N = wl.N


def load_gcp():
    spec = importlib.util.spec_from_file_location("gen_content_pack_lab", os.path.join(HERE, "gen-content-pack.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ------------------------------------------------------------------------------------------------------------------------------ files
def wav16(data, rate=SR):
    a = np.clip(np.rint(np.asarray(data, dtype=np.float64) * 32767.0), -32768, 32767).astype("<i2").tobytes()
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)
    body = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(a)) + a
    return b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body


def read_wav16(path):
    raw = open(path, "rb").read()
    pos = raw.find(b"data")
    n = struct.unpack("<I", raw[pos + 4:pos + 8])[0]
    return np.frombuffer(raw[pos + 8:pos + 8 + n], dtype="<i2").astype(np.float64) / 32767.0


def slugify(text):
    return re.sub(r"_+", "_", re.sub(r"[^a-z0-9]+", "_", text.lower())).strip("_")


def cand_dir(run, cat):
    return os.path.join(run, "candidates", cat)


def list_candidates(run):
    """[(name, category, recipe path)] of a run folder."""
    out = []
    base = os.path.join(run, "candidates")
    if os.path.isdir(base):
        for cat in sorted(os.listdir(base)):
            for fn in sorted(os.listdir(os.path.join(base, cat))):
                if fn.endswith(".json"):
                    out.append((fn[:-5], cat, os.path.join(base, cat, fn)))
    return out


def load_recipe(path):
    with open(path, "r", encoding="utf-8") as f:
        r, errors, warnings = wl.clean_recipe(json.load(f))
    return r, errors, warnings


def write_candidate(run, name, cat, recipe):
    recipe = dict(recipe)
    recipe["category"] = cat
    d = cand_dir(run, cat)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name + ".json"), "w", encoding="utf-8") as f:
        json.dump(recipe, f, indent=1, ensure_ascii=False)
        f.write("\n")
    data = wl.table_samples(recipe, name)
    with open(os.path.join(d, name + ".wav"), "wb") as f:
        f.write(load_gcp().wav_bytes(data * 1.0))


# ------------------------------------------------------------------------------------------------------------------------------ grammars
def rv(rng, lo, hi):
    return round(float(rng.uniform(lo, hi)), 3)


def pick(rng, items):
    return items[int(rng.integers(0, len(items)))]


def scan(rng, lo, hi, min_span=0.35, kinds=("linear", "linear", "curve", "keys", "lfo"), integer=False):
    """A random scan law between lo and hi; returns (law, text)."""
    span = hi - lo
    while True:
        a, b = rv(rng, lo, hi), rv(rng, lo, hi)
        if abs(a - b) >= min_span * span:
            break
    kind = pick(rng, kinds)
    if kind == "linear":
        return {"law": "linear", "from": a, "to": b}, f"{a:g} to {b:g}"
    if kind == "curve":
        pw = rv(rng, 0.5, 3.0)
        return {"law": "curve", "from": a, "to": b, "power": pw}, f"{a:g} to {b:g} (curved, power {pw:g})"
    if kind == "keys":
        m = rv(rng, lo, hi)
        t = rv(rng, 0.3, 0.7)
        return {"law": "keys", "points": [[0.0, a], [t, m], [1.0, b]], "ease": "smooth"}, f"{a:g} via {m:g} to {b:g}"
    c, d = (lo + hi) / 2, span * 0.45
    cyc = pick(rng, [1.0, 1.0, 2.0])
    return {"law": "lfo", "center": round(c, 3), "depth": round(d, 3), "cycles": cyc, "phase": pick(rng, [0.0, 0.25, 0.5, 0.75]), "shape": "sine"}, f"{lo + 0.05 * span:g} to {hi - 0.05 * span:g} and back ({cyc:g} cycle)"


def nd(t, inp=None, inputs=None, **params):
    n = {"type": t, "params": params}
    if inp is not None:
        n["input"] = inp
    if inputs is not None:
        n["inputs"] = inputs
    return n


def classic_random(rng):
    w = {"saw": 0.0, "square": 0.0, "tri": 0.0, "sine": 0.0}
    main = pick(rng, ["saw", "saw", "square", "tri"])
    w[main] = 1.0
    if rng.random() < 0.4:
        w[pick(rng, ["saw", "square", "tri", "sine"])] += rv(rng, 0.2, 0.7)
    return nd("classic", count=300, **{k: min(v, 1.0) for k, v in w.items()}), main


# every template: (rng) -> (source node, post, text)
def a_filtered(rng):
    base, name = classic_random(rng)
    law, t = scan(rng, 1.5, 200, kinds=("linear", "curve", "curve"))
    return nd("spec", base, lp_cut=law, lp_res=rv(rng, 0, 5), lp_poles=pick(rng, ["2", "4"])), {}, f"a {name} classic through a resonant low pass, cutoff {t} harmonics"


def a_pulse(rng):
    wl_, t = scan(rng, 0.04, 0.5, kinds=("linear", "curve"))
    st = int(rng.integers(1, 6))
    ws, _ = scan(rng, 0.0, 0.4, kinds=("linear", "curve"))
    return nd("pulse", width=wl_, stack=st, width_spread=ws, shift_spread=rv(rng, 0, 0.3), saw_mix=rv(rng, 0, 0.5)), {}, (f"a stack of {st} pulses, width {t}, spreading apart" if st > 1 else f"a single pulse, width {t}")


def a_sync(rng):
    law, t = scan(rng, 1.0, 12.0, kinds=("linear", "curve"))
    sl = pick(rng, ["sine", "saw", "tri", "square"])
    return nd("sync", ratio=law, slave=sl, window=pick(rng, ["none", "none", "fade", "hann"])), {"limit": 300}, f"hard sync with a {sl} slave, ratio {t}"


def a_shaped(rng):
    base, name = classic_random(rng)
    mode = pick(rng, ["tanh", "fold", "clip"])
    law, t = scan(rng, 0.6, 12.0, kinds=("linear", "curve"))
    return nd("shape", base, mode=mode, drive=law, bias=rv(rng, -0.3, 0.3)), {}, f"a {name} classic through a {mode} shaper, drive {t}"


def a_blend(rng):
    a, na = classic_random(rng)
    b, nb = classic_random(rng)
    return nd("mix", inputs=[a, b], g1={"law": "linear", "from": 1, "to": 0}, g2={"law": "linear", "from": 0, "to": 1}), {}, f"a level matched cross fade from {na} to {nb} shaped classics"


def a_stack(rng):
    base, name = classic_random(rng)
    base["params"].update(stack=int(rng.integers(3, 10)))
    sp, t = scan(rng, 0.0, 0.45, kinds=("linear", "curve"))
    base["params"]["stack_spread"] = sp
    base["params"]["oct2"] = rv(rng, 0, 0.6)
    return base, {}, f"a stack of {base['params']['stack']} {name} classics with phase spread {t}"


def d_fm(rng):
    r1 = int(rng.integers(1, 9))
    i1, t = scan(rng, 0.0, rv(rng, 3, 10), min_span=0.5, kinds=("linear", "curve"))
    p = dict(carrier=int(pick(rng, [1, 1, 2, 3])), r1=r1, i1=i1)
    txt = f"FM carrier {p['carrier']}, modulator {r1}, index {t}"
    if rng.random() < 0.5:
        p.update(r2=int(rng.integers(1, 9)), i2=scan(rng, 0.0, rv(rng, 1, 5), kinds=("linear",))[0])
        txt += f", a second operator at {p['r2']}"
    if rng.random() < 0.25:
        p["feedback"] = rv(rng, 0.2, 1.0)
        txt += f", feedback {p['feedback']:g}"
    return nd("fm", **p), {"limit": 300}, txt


def d_pd(rng):
    mode = pick(rng, ["knee", "resonance", "pulsar"])
    if mode == "knee":
        law, t = scan(rng, 0.04, 0.5, kinds=("linear", "curve"))
    elif mode == "resonance":
        law, t = scan(rng, 1.0, 24.0, kinds=("linear", "curve"))
    else:
        law, t = scan(rng, 1.5, 30.0, kinds=("linear", "curve"))
    return nd("pd", mode=mode, amount=law, width=rv(rng, 0.06, 0.3)), {"limit": 300}, f"phase distortion ({mode}), amount {t}"


def d_shape(rng):
    mode = pick(rng, ["bits", "power", "fold", "rectify", "clip"])
    base = nd("classic", sine=1.0, saw=rv(rng, 0, 0.6), count=150)
    if mode == "bits":
        law, t = scan(rng, 1.8, 10.0, kinds=("linear", "curve"))
        return nd("shape", base, mode="bits", bits=law), {"limit": 300}, f"a sine and saw reduced to {t} bits"
    if mode == "power":
        law, t = scan(rng, 0.06, 1.0, kinds=("linear", "curve"))
        return nd("shape", base, mode="power", power=law), {"limit": 300}, f"a power law shaper, exponent {t}"
    law, t = scan(rng, 0.8, 9.0, kinds=("linear", "curve"))
    return nd("shape", base, mode=mode, drive=law), {"limit": 300}, f"a {mode} shaper on a sine, drive {t}"


def d_steps(rng):
    steps = int(rng.integers(4, 17))
    keys = int(rng.integers(3, 8))
    return nd("stepseq", steps=steps, keys=keys, pos={"law": "linear", "from": 0, "to": keys - 1}, slew=rv(rng, 0.05, 0.4), seed=int(rng.integers(0, 9999))), {"limit": 300}, f"{steps} step levels morphing through {keys} random patterns"


def d_gauss(rng):
    law, t = scan(rng, 1.5, 36.0, kinds=("linear", "curve"))
    return nd("gaussformant", m=law, width=rv(rng, 0.06, 0.3)), {"limit": 300}, f"a gaussian windowed burst, centre {t} harmonics"


def s_mask(rng):
    mask = pick(rng, ["odd", "even", "prime", "every"])
    law, t = scan(rng, 1, 90, min_span=0.5, kinds=("linear", "curve"))
    return nd("additive", count=law, rolloff=rv(rng, 0.6, 1.5), mask=mask, mask_n=int(rng.integers(2, 6)), mask_soft=rv(rng, 0, 0.1)), {"smooth": "no"}, f"harmonic series with the {mask} mask, the number of harmonics going {t}"


def s_tilt(rng):
    law, t = scan(rng, 0.2, 2.6, kinds=("linear", "curve"))
    return nd("additive", count=int(rng.integers(100, 300)), rolloff=law, phase_quad=rv(rng, 0.004, 0.015)), {}, f"spectral roll-off exponent {t}"


def s_peak(rng):
    law, t = scan(rng, 1.5, 150, kinds=("curve", "linear"))
    return nd("additive", count=300, rolloff=rv(rng, 0.7, 1.4), peak_center=law, peak_width=rv(rng, 0.1, 0.6), peak_gain_db=rv(rng, 12, 30), phase_quad=0.004), {}, f"a band of harmonics lifted, centre {t}"


def s_comb(rng):
    per = rv(rng, 3, 20)
    return nd("additive", count=250, rolloff=rv(rng, 0.5, 1.3), comb_period=per, comb_depth=rv(rng, 0.6, 1.0), comb_offset={"law": "linear", "from": 0, "to": per}), {}, f"a comb of period {per:g} sliding through the spectrum"


def s_disp(rng):
    law, t = scan(rng, 0.0, 0.03, min_span=0.5, kinds=("linear", "curve"))
    return nd("additive", count=int(rng.integers(40, 160)), rolloff=rv(rng, 0.7, 1.2), mask=pick(rng, ["all", "all", "odd"]), phase_quad=law), {}, f"phase rotation (phase_quad {t}) of a fixed spectrum"


def s_ops(rng):
    base = nd("additive", count=int(rng.integers(30, 120)), rolloff=rv(rng, 0.6, 1.4), mask=pick(rng, ["all", "odd", "prime"]))
    sh, t = scan(rng, 0, 30, min_span=0.4, kinds=("linear",))
    return nd("spec", base, shift=sh, blur=rv(rng, 0, 3), tilt=rv(rng, -0.5, 0.8)), {}, f"a harmonic series shifted up by {t} harmonics (the partials leave the harmonic grid positions)"


def s_mixmask(rng):
    a = nd("additive", count=60, rolloff=rv(rng, 0.8, 1.4), mask=pick(rng, ["odd", "prime"]))
    b = nd("additive", count=60, rolloff=rv(rng, 0.8, 1.4), mask=pick(rng, ["even", "all", "every"]), mask_n=int(rng.integers(2, 6)))
    return nd("mix", inputs=[a, b], g1={"law": "linear", "from": 1, "to": 0}, g2={"law": "linear", "from": 0, "to": 1}), {"smooth": "no"}, "a level matched cross fade between two harmonic masks"


VOWELS = wl.VOWELS


def v_seq(rng):
    voice = pick(rng, ["men", "women", "children"])
    n = int(rng.integers(3, 9))
    seq = [pick(rng, VOWELS) for _ in range(n)]
    for i in range(1, n):
        while seq[i] == seq[i - 1]:
            seq[i] = pick(rng, VOWELS)
    f0 = {"men": 130.0, "women": 220.0, "children": 262.0}[voice] * rv(rng, 0.85, 1.2)
    return nd("formant", f0=round(f0, 1), voice=voice, sequence=seq, pos={"law": "linear", "from": 0, "to": n - 1}, tilt=rv(rng, 0.8, 1.4), breath=rv(rng, 0, 0.2), seed=int(rng.integers(0, 999))), {"limit": 250}, f"vowels {' '.join(seq)} ({voice}, {f0:.0f} Hz)"


def v_voice(rng):
    va, vb = rng.permutation(["men", "women", "children"])[:2]
    f0 = rv(rng, 120, 280)
    return nd("formant", f0=f0, voice=str(va), voice_b=str(vb), vowel_a=pick(rng, VOWELS), vowel_b=pick(rng, VOWELS), mix={"law": "linear", "from": 0, "to": 1}, voice_mix={"law": "linear", "from": 0, "to": 1}), {"limit": 250}, f"a vowel pair morphing while the voice goes from {va} to {vb} ({f0:.0f} Hz)"


def v_shift(rng):
    law, t = scan(rng, 0.5, 2.0, kinds=("linear", "curve", "lfo"))
    return nd("formant", f0=rv(rng, 110, 260), vowel_a=pick(rng, VOWELS), shift=law, tilt=rv(rng, 0.8, 1.4)), {"limit": 250}, f"one vowel with all formants scaled {t}"


def v_whisper(rng):
    v = pick(rng, VOWELS)
    f0 = rv(rng, 110, 260)
    return nd("mix", inputs=[nd("formant", f0=f0, vowel_a=v, breath=1.0, seed=int(rng.integers(0, 99))), nd("formant", f0=f0, vowel_a=v)], g1={"law": "linear", "from": 1, "to": 0}, g2={"law": "linear", "from": 0, "to": 1}), {"limit": 250}, f"vowel {v}, whisper to voiced"


def v_overtone(rng):
    a, ta = scan(rng, 3, 30, kinds=("linear", "curve"))
    inputs = [nd("additive", count=220, rolloff=rv(rng, 1.0, 1.6), peak_center=a, peak_width=rv(rng, 0.06, 0.15), peak_gain_db=rv(rng, 14, 24))]
    txt = f"an overtone peak moving {ta}"
    if rng.random() < 0.5:
        b, tb = scan(rng, 3, 30, kinds=("linear", "curve"))
        inputs.append(nd("additive", count=220, rolloff=1.3, peak_center=b, peak_width=0.09, peak_gain_db=18))
        txt += f" and a second one {tb}"
    return (inputs[0] if len(inputs) == 1 else nd("mix", inputs=inputs, match="none")), {}, txt


def v_source(rng):
    t1, t2 = rv(rng, 2.0, 3.0), rv(rng, 0.5, 1.0)
    return nd("formant", f0=rv(rng, 110, 220), vowel_a=pick(rng, VOWELS), tilt={"law": "linear", "from": t1, "to": t2}, breath={"law": "linear", "from": rv(rng, 0.2, 0.5), "to": 0.0}, h1_boost_db={"law": "linear", "from": 8, "to": 0}, seed=int(rng.integers(0, 99))), {"limit": 250}, f"voice source from steep tilt {t1:g} to {t2:g}"


def m_set(rng):
    st = pick(rng, ["bell", "bar", "membrane", "gong", "bowl"])
    law, t = scan(rng, 1.5, 12.0, kinds=("linear", "curve"))
    p = dict(set=st, scale=law, count=int(rng.integers(6, 14)), rolloff=rv(rng, 0.3, 1.1), seed=int(rng.integers(0, 99)))
    txt = f"the {st} modes, fundamental walking {t}"
    if rng.random() < 0.4:
        p["bright"] = scan(rng, 0.45, 1.0, kinds=("linear",))[0]
        txt += ", upper modes swelling"
    if rng.random() < 0.3 and st in ("bowl", "bell"):
        p["pair_delta"] = scan(rng, 0.0, 1.5, kinds=("linear",))[0]
        txt += ", modes split into beating pairs"
    return nd("partials", **p), {"limit": 300}, txt


def m_ring(rng):
    cnt = int(rng.integers(1, 25))
    law, t = scan(rng, 0.5, 14.0, kinds=("linear", "curve"))
    return nd("ring", nd("additive", count=cnt, rolloff=rv(rng, 0.8, 1.2)), c=law, dry=rv(rng, 0, 0.4), max_k=cnt), {"limit": 300}, f"ring modulation of a {cnt} harmonic series by a sine at {t} harmonics"


def m_cloud(rng):
    return nd("partials", set=pick(rng, ["cloud", "random"]), count=int(rng.integers(12, 40)), lo=rv(rng, 3, 12), hi=rv(rng, 80, 200), rolloff=rv(rng, 0.3, 0.9), wobble=rv(rng, 0, 8), wobble_cycles=pick(rng, [1.0, 2.0]),
              keys=int(rng.integers(3, 7)), pos={"law": "linear", "from": 0, "to": 2}, seed=int(rng.integers(0, 99))), {"limit": 300}, "a seeded cloud of inharmonic partials with a slow wobble / key morph"


def m_comb(rng):
    law, t = scan(rng, 0.03, 0.45, kinds=("linear", "curve"))
    return nd("spec", nd("additive", count=300, rolloff=rv(rng, 0.4, 1.0)), rcomb_d=law, rcomb_g=rv(rng, 0.8, 0.95), scatter=rv(rng, 0.3, 1.0), seed=int(rng.integers(0, 99))), {"limit": 300}, f"a resonant comb, delay {t} of the cycle"


def m_blur(rng):
    st = pick(rng, ["bell", "bar", "membrane", "gong"])
    law, t = scan(rng, 0.0, 6.0, kinds=("linear",))
    return nd("spec", nd("partials", set=st, scale=rv(rng, 2, 8), count=12, rolloff=0.7, seed=int(rng.integers(0, 99))), blur=law, shift={"law": "linear", "from": 0, "to": rv(rng, 5, 30)}), {"limit": 300}, f"{st} modes blurred ({t}) and shifted up"


def n_tilt(rng):
    law, t = scan(rng, -1.2, 2.4, kinds=("linear", "curve"))
    return nd("noise", tilt=law, seed=int(rng.integers(0, 9999)), count=300), {"limit": 320}, f"noise colour tilt {t}"


def n_band(rng):
    cen, t = scan(rng, 3, 280, kinds=("curve", "linear"))
    notch = rng.random() < 0.4
    return nd("spec", nd("noise", tilt=rv(rng, 0, 0.8), seed=int(rng.integers(0, 9999)), count=300), band_center=cen, band_width=rv(rng, 0.15, 0.7), band_gain_db=-rv(rng, 20, 34) if notch else rv(rng, 18, 32)), {"limit": 320}, f"noise with a {'notch' if notch else 'band'} sweeping {t}"


def n_lp(rng):
    law, t = scan(rng, 2, 300, kinds=("curve", "linear"))
    return nd("spec", nd("noise", tilt=rv(rng, 0, 1.2), seed=int(rng.integers(0, 9999)), count=300), lp_cut=law, lp_res=rv(rng, 0, 4), lp_poles=pick(rng, ["2", "4"])), {"limit": 320}, f"noise through a low pass, cutoff {t}"


def n_sparse(rng):
    law, t = scan(rng, 0.02, 1.1, kinds=("linear", "curve"), min_span=0.5)
    return nd("noise", tilt=rv(rng, 0, 1.0), density=law, seed=int(rng.integers(0, 9999)), count=300), {"limit": 320}, f"noise spectrum density {t}"


def n_clicks(rng):
    law, t = scan(rng, 2, 90, kinds=("curve", "linear"), min_span=0.5)
    return nd("clicks", count=law, tail=rv(rng, 0, 40), seed=int(rng.integers(0, 9999))), {"limit": 320, "rms": 0.12}, f"clicks, number per cycle {t}"


def n_burst(rng):
    law, t = scan(rng, 0.06, 1.0, kinds=("curve", "linear"), min_span=0.5)
    return nd("burst", width=law, tilt=rv(rng, -0.3, 1.0), seed=int(rng.integers(0, 9999))), {"limit": 320, "rms": 0.15}, f"a noise burst of width {t} of the cycle"


def n_tone(rng):
    tone = pick(rng, [nd("additive", count=1), nd("additive", count=int(rng.integers(20, 200)), rolloff=1.0), nd("additive", count=60, rolloff=1.0, mask="odd")])
    return nd("mix", inputs=[nd("noise", tilt=rv(rng, 0, 1), seed=int(rng.integers(0, 9999)), count=300), tone], g1={"law": "linear", "from": 1, "to": 0}, g2={"law": "linear", "from": 0, "to": 1}), {"limit": 320}, "noise to tone cross fade"


def n_vars(rng):
    k = int(rng.integers(3, 5))
    tilt = rv(rng, 0, 1.2)
    ins = [nd("noise", tilt=tilt, seed=int(rng.integers(0, 9999)), count=300) for _ in range(k)]
    gains = {}
    for i in range(k):
        c, w = i / (k - 1), 1.0 / (k - 1)
        if i == 0:
            pts = [[0.0, 1.0], [w, 0.0]]
        elif i == k - 1:
            pts = [[1 - w, 0.0], [1.0, 1.0]]
        else:
            pts = [[c - w, 0.0], [c, 1.0], [c + w, 0.0]]
        gains[f"g{i + 1}"] = {"law": "keys", "points": [[round(x, 3), y] for x, y in pts], "ease": "smooth"}
    return nd("mix", inputs=ins, **gains), {"limit": 320}, f"{k} different frozen noises of colour {tilt:g} cross faded"


GRAMMARS = {
    "Analog": [a_filtered, a_pulse, a_sync, a_shaped, a_blend, a_stack],
    "Digital": [d_fm, d_fm, d_pd, d_shape, d_steps, d_gauss],
    "Spectral": [s_mask, s_tilt, s_peak, s_comb, s_disp, s_ops, s_mixmask],
    "Vocal": [v_seq, v_seq, v_voice, v_shift, v_whisper, v_overtone, v_source],
    "Metallic": [m_set, m_set, m_ring, m_cloud, m_comb, m_blur],
    "Noise": [n_tilt, n_band, n_band, n_lp, n_sparse, n_clicks, n_burst, n_tone, n_vars],
}


def make_candidate(cat, seed, i):
    rng = wl.rng_for(f"lab:{seed}:{cat}:{i}")
    tpl = pick(rng, GRAMMARS[cat])
    source, post, text = tpl(rng)
    name = f"{cat[:3].lower()}-{seed}-{i:03d}"
    r = {"schema": wl.SCHEMA, "title": f"{cat} candidate {name}", "category": cat, "description": text[0].upper() + text[1:] + ".", "frames": 64, "seed": name, "source": source, "tags": [tpl.__name__]}
    if post:
        r["post"] = post
    clean, errors, _ = wl.clean_recipe(r)
    if errors:
        raise SystemExit(f"grammar {tpl.__name__} wrote an invalid recipe: {errors}")
    return name, clean


# ------------------------------------------------------------------------------------------------------------------------------ commands
def default_run(seed):
    return os.path.join(DEFAULT_OUT_ROOT, f"{datetime.date.today().isoformat()}-{seed}")


def cmd_generate(a):
    cats = a.categories or wl.CATEGORIES
    run = os.path.abspath(a.out or default_run(a.seed))
    if os.path.commonpath([run, REPO]) == REPO:
        sys.exit(f"the output folder {run} is inside the repository: generated material stays outside it until promoted")
    n = 0
    for cat in cats:
        for i in range(a.per_category):
            name, r = make_candidate(cat, a.seed, i)
            write_candidate(run, name, cat, r)
            n += 1
    with open(os.path.join(run, "run.json"), "w", encoding="utf-8") as f:
        json.dump({"seed": a.seed, "per_category": a.per_category, "categories": cats, "created": datetime.date.today().isoformat()}, f, indent=1)
    print(f"generated {n} candidates ({a.per_category} per category, seed {a.seed}) in {run}")
    print(f"next: wavetable-lab.py score --run {run}")
    return 0


# ---- score
def descriptor(samples, keyframes=16):
    """Spectral envelope trajectory: 16 frames x 32 log spaced harmonic bands (dB relative to the frame energy), the vector the diversity measure compares."""
    env = wl.envelope(samples)
    idx = np.round(np.linspace(0, len(env) - 1, keyframes)).astype(int)
    return env[idx]


def trajectory_distance(a, b):
    """RMS dB difference of two descriptors; the smaller of forward and reversed scan (a reversed scan is the same idea)."""
    d1 = math.sqrt(float(np.mean((a - b) ** 2)))
    d2 = math.sqrt(float(np.mean((a - b[::-1]) ** 2)))
    return min(d1, d2)


def quality(m):
    """The score of one table (a filter, not a judge of beauty). Returns (quality 0..1, components, rejects)."""
    move = max(m["move_env_db"] / 6.0, m["move_wave"] / 0.5)
    maxu = 0.5 * (m["step_max_db"] / 2.0 + m["wave_step_max"] / 0.35)
    comp = {
        "move": min(move, 1.0) ** 0.5,
        "jump": 1.0 / (1.0 + max(0.0, (maxu - 2.0) / 2.0) ** 2),
        "rich": 0.5 * min(m["eff_mean"] / 6.0, 1.0) + 0.5 * min(math.log(max(m["eff_max"], 1.0) / max(m["eff_min"], 1.0)) / math.log(3.0), 1.0),
        "alias": min(max((-m["alias_db"] - 25.0) / 25.0, 0.0), 1.0),
        "even": 1.0 / (1.0 + (m["spread_db"] / 4.0) ** 2),
        "crest": 1.0 / (1.0 + max(0.0, (m["crest_mean_db"] - 10.0) / 4.0) ** 2),
    }
    rej = []
    if m["dc"] > 1e-3:
        rej.append("dc")
    if move < 0.35:
        rej.append("static")
    if maxu > 4.5:
        rej.append("jumpy")
    if m["alias_db"] > -20.0:
        rej.append("alias")
    if m["spread_db"] > 12.0:
        rej.append("uneven level")
    if m["peak"] > 0.98 or not math.isfinite(m["peak"]):
        rej.append("peak")
    if m["crest_max_db"] > 26.0:
        rej.append("spiky")
    if m["eff_max"] < 1.6:
        rej.append("sine only")
    q = 1.0
    for v in comp.values():
        q *= max(v, 1e-6)
    return q ** (1.0 / len(comp)), comp, rej, {"move": move, "max_step_units": maxu}


def pack_descriptors():
    out = {}
    root = os.path.join(REPO, "content", "factory-wavetables")
    for dp, _, fns in os.walk(root):
        for fn in sorted(fns):
            if fn.endswith(".wav"):
                path = os.path.join(dp, fn)
                out[os.path.relpath(path, root)] = descriptor(read_wav16(path))
    return out


def cmd_score(a):
    run = os.path.abspath(a.run)
    cands = list_candidates(run)
    if not cands:
        sys.exit(f"no candidates in {run}")
    pack = pack_descriptors()
    scores = {}
    desc = {}
    for name, cat, path in cands:
        r, errors, _ = load_recipe(path)
        if errors:
            scores[name] = {"category": cat, "rejects": ["invalid recipe: " + "; ".join(errors)], "quality": 0.0}
            continue
        data = wl.table_samples(r, name)
        m = wl.measures(data)
        q, comp, rej, extra = quality(m)
        desc[name] = descriptor(data)
        near = min(((trajectory_distance(desc[name], d), k) for k, d in pack.items()), key=lambda x: x[0])
        scores[name] = {"category": cat, "title": r["title"], "quality": q, "components": comp, "measures": m, "extra": extra, "rejects": rej, "pack_distance": near[0], "pack_nearest": near[1]}
    # keep the top K per category that are also mutually distinct and not a copy of a pack table
    tau_c, tau_p = 3.0, 2.0
    selected = {}
    for cat in sorted({c for _, c, _ in cands}):
        ok = sorted((n for n, s in scores.items() if s["category"] == cat and not s["rejects"] and s["pack_distance"] >= tau_p), key=lambda n: -scores[n]["quality"])
        chosen = []
        for n in ok:
            dmin = min((trajectory_distance(desc[n], desc[c]) for c in chosen), default=99.0)
            scores[n]["nearest_kept_distance"] = dmin
            if dmin >= tau_c:
                chosen.append(n)
            if len(chosen) >= a.keep:
                break
        for n in scores:
            if scores[n]["category"] == cat and scores[n].get("rejects") == [] and scores[n]["pack_distance"] < tau_p:
                scores[n]["rejects"] = ["copy of a pack table"]
        selected[cat] = chosen
    with open(os.path.join(run, "scores.json"), "w", encoding="utf-8") as f:
        json.dump({"thresholds": {"candidate_distance_db": tau_c, "pack_distance_db": tau_p}, "scores": scores, "selected": selected}, f, indent=1)
    print(f"scored {len(cands)} candidates in {run}")
    for cat, chosen in selected.items():
        qs = np.array([s["quality"] for s in scores.values() if s["category"] == cat])
        rej = {}
        for s in scores.values():
            if s["category"] == cat:
                for r in s["rejects"]:
                    rej[r.split(":")[0]] = rej.get(r.split(":")[0], 0) + 1
        print(f"  {cat:9s} quality min {qs.min():.2f} median {np.median(qs):.2f} max {qs.max():.2f}; rejected {sum(1 for s in scores.values() if s['category'] == cat and s['rejects'])} {rej}; kept {len(chosen)}: {' '.join(chosen)}")
    print(f"next: wavetable-lab.py preview --run {run}")
    return 0


# ---- preview
def render_scan(frames, f0, seconds=4.0, chord=None):
    """Band limited wavetable oscillator: a scan from frame 0 to the last over `seconds`; frames (n, 2048). `chord`: list of semitone offsets (all voices scan together)."""
    n = len(frames)
    spec = np.fft.rfft(frames, axis=1)
    out = np.zeros(int(seconds * SR))
    offs = chord or [0]
    t = np.arange(len(out)) / SR
    pos = t / seconds * (n - 1)
    i0 = np.minimum(pos.astype(int), n - 2) if n > 1 else np.zeros(len(t), int)
    af = pos - i0 if n > 1 else np.zeros(len(t))
    for off in offs:
        f = f0 * 2 ** (off / 12.0)
        hmax = max(1, min(int(0.45 * SR / f), N // 2 - 1))
        s = spec.copy()
        s[:, hmax + 1:] = 0
        tab = np.fft.irfft(s, N, axis=1)
        ph = (np.cumsum(np.full(len(t), f / SR)) % 1.0) * N
        j0 = ph.astype(int) % N
        jf = ph - np.floor(ph)
        j1 = (j0 + 1) % N

        def rd(i):
            return tab[i, j0] * (1 - jf) + tab[i, j1] * jf
        y = rd(i0) * (1 - af) + (rd(np.minimum(i0 + 1, n - 1)) * af if n > 1 else 0.0)
        out += y
    fade = np.minimum(1.0, np.minimum(t, seconds - t) / 0.02)
    out *= fade
    pk = np.max(np.abs(out))
    return out * (0.7 / pk) if pk > 0 else out


def svg_wave(frame, w=150, h=56):
    pts = frame[::8]
    pk = max(np.max(np.abs(frame)), 1e-9)
    xs = np.linspace(2, w - 2, len(pts))
    ys = h / 2 - pts / pk * (h / 2 - 3)
    d = " ".join(f"{x:.1f},{y:.1f}" for x, y in zip(xs, ys))
    return f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}"><rect width="{w}" height="{h}" fill="#111"/><line x1="0" y1="{h / 2}" x2="{w}" y2="{h / 2}" stroke="#333"/><polyline points="{d}" fill="none" stroke="#7fd" stroke-width="1"/></svg>'


def heat_png(samples, rows=64, kmax=400, cell=4):
    """Spectrum over the scan: x = frame, y = harmonic (log scale, low at the bottom), colour = dB below the loudest harmonic of the table (0 .. -60)."""
    fr = np.asarray(samples).reshape(-1, N)
    mag = np.abs(np.fft.rfft(fr, axis=1)) / (N / 2.0)
    edges = np.geomspace(1, kmax + 1, rows + 1)
    img = np.zeros((rows, len(fr)))
    ks = np.arange(mag.shape[1])
    for r in range(rows):
        m = (ks >= edges[r]) & (ks < edges[r + 1])
        if not m.any():
            m = (ks == int(edges[r]))
        img[r] = mag[:, m].max(axis=1)
    db = 20 * np.log10(img / max(img.max(), 1e-12) + 1e-9)
    v = np.clip((db + 60) / 60, 0, 1)[::-1]
    stops = np.array([[0, 0, 0], [30, 20, 120], [200, 40, 120], [250, 190, 40], [255, 255, 230]], dtype=float)
    x = v * (len(stops) - 1)
    i = np.minimum(x.astype(int), len(stops) - 2)
    f = (x - i)[..., None]
    rgb = (stops[i] * (1 - f) + stops[i + 1] * f).astype(np.uint8)
    rgb = np.kron(rgb, np.ones((cell, cell, 1), dtype=np.uint8))
    return "data:image/png;base64," + base64.b64encode(wl.png_bytes(rgb)).decode()


def cmd_preview(a):
    run = os.path.abspath(a.run)
    spath = os.path.join(run, "scores.json")
    if not os.path.isfile(spath):
        sys.exit(f"{spath} missing: run score first")
    doc = json.load(open(spath))
    scores, selected = doc["scores"], doc["selected"]
    names = [n for cat in wl.CATEGORIES for n in selected.get(cat, [])] if not a.all else sorted(scores)
    pdir = os.path.join(run, "preview")
    os.makedirs(pdir, exist_ok=True)
    cards = {}
    for name in names:
        cat = scores[name]["category"]
        r, errors, _ = load_recipe(os.path.join(cand_dir(run, cat), name + ".json"))
        data = wl.table_samples(r, name)
        fr = data.reshape(-1, N)
        for label, f0, chord in (("C2", 65.406, None), ("C3", 130.813, None), ("C4", 261.626, None), ("chord", 130.813, [0, 4, 7, 11])):
            y = render_scan(fr, f0, 4.0, chord)
            with open(os.path.join(pdir, f"{name}_{label}.wav"), "wb") as f:
                f.write(wav16(y))
        s = scores[name]
        m = s["measures"]
        idx = [0, 16, 32, 48, len(fr) - 1] if len(fr) == 64 else [int(round(x)) for x in np.linspace(0, len(fr) - 1, 5)]
        waves = "".join(f'<div class="w">{svg_wave(fr[i])}<br><small>frame {i}</small></div>' for i in idx)
        comp = " ".join(f"{k} {v:.2f}" for k, v in s["components"].items())
        audio = "".join(f'<div>{lab}<br><audio controls preload="none" src="preview/{name}_{lab}.wav"></audio></div>' for lab in ("C2", "C3", "C4", "chord"))
        cards.setdefault(cat, []).append(
            f'<div class="card"><label><input type="checkbox" class="pick" value="{html.escape(name)}"> <b>{html.escape(name)}</b></label> '
            f'<span class="q">quality {s["quality"]:.2f}</span> <small>{html.escape(r["description"])}</small>'
            f'<div class="row">{waves}<img src="{heat_png(data)}" width="256" height="256" title="spectrum over the scan: x frame, y harmonic (log), colour dB"></div>'
            f'<div class="row">{audio}</div>'
            f'<pre>{comp}\nframes {m["frames"]} | peak {m["peak"]:.2f} | level spread {m["spread_db"]:.1f} dB | highest harmonic {m["highest_harmonic"]} | DC {m["dc"]:.0e} | '
            f'effective harmonics {m["eff_min"]:.1f}..{m["eff_max"]:.1f} | movement {s["extra"]["move"]:.2f}, largest step {s["extra"]["max_step_units"]:.1f} units | alias {m["alias_db"]:.0f} dB | crest {m["crest_mean_db"]:.1f} dB\n'
            f'nearest pack table {html.escape(s["pack_nearest"])} (distance {s["pack_distance"]:.1f} dB), nearest kept candidate {s.get("nearest_kept_distance", 99):.1f} dB</pre></div>')
    body = "".join(f'<h2>{c}</h2>' + "".join(cards[c]) for c in wl.CATEGORIES if c in cards)
    page = ('<!doctype html><meta charset="utf-8"><title>wavetable lab</title><style>body{background:#181818;color:#ddd;font:14px sans-serif;margin:16px}.card{border:1px solid #444;margin:10px 0;padding:8px}'
            '.row{display:flex;gap:8px;flex-wrap:wrap;margin:6px 0;align-items:flex-start}.w{text-align:center}.q{color:#fc6;margin:0 8px}pre{font-size:11px;color:#aaa;white-space:pre-wrap}'
            'h2{border-bottom:1px solid #555}textarea{width:100%;height:60px}</style>'
            f'<h1>Wavetable lab: {html.escape(os.path.basename(run))}</h1><p>Tick the ones you like, then copy the names below to the agent. This page writes nothing.</p>'
            '<textarea id="out" readonly></textarea>' + body +
            '<script>const o=document.getElementById("out");function u(){o.value=[...document.querySelectorAll(".pick:checked")].map(x=>x.value).join(" ")}'
            'document.querySelectorAll(".pick").forEach(x=>x.addEventListener("change",u))</script>')
    with open(os.path.join(run, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)
    print(f"preview of {len(names)} candidates: {os.path.join(run, 'index.html')}")
    return 0


# ---- propose
def extract_json(text):
    """Every JSON value that can be found in a model's answer: the whole text, fenced blocks, or balanced {...} / [...] spans."""
    try:
        return [json.loads(text)]
    except ValueError:
        pass
    found = [m.group(1) for m in re.finditer(r"```(?:json)?\s*(.*?)```", text, re.S)]
    vals = []
    for blk in found:
        try:
            vals.append(json.loads(blk))
        except ValueError:
            pass
    if vals:
        return vals
    dec = json.JSONDecoder()
    i = 0
    while i < len(text):
        if text[i] in "[{":
            try:
                v, end = dec.raw_decode(text[i:])
                vals.append(v)
                i += end
                continue
            except ValueError:
                pass
        i += 1
    return vals


def cmd_propose(a):
    text = open(a.from_file, "r", encoding="utf-8").read()
    recipes = []
    for v in extract_json(text):
        if isinstance(v, dict) and "recipes" in v:
            v = v["recipes"]
        recipes += v if isinstance(v, list) else [v]
    if not recipes:
        print("no JSON found in the file")
        return 1
    run = os.path.abspath(a.run or os.path.join(DEFAULT_OUT_ROOT, f"{datetime.date.today().isoformat()}-propose-{hashlib.sha256(text.encode()).hexdigest()[:6]}"))
    if os.path.commonpath([run, REPO]) == REPO:
        sys.exit("the run folder must be outside the repository")
    ok = bad = 0
    for i, r in enumerate(recipes):
        label = f"recipe {i + 1}" + (f" ({r.get('title')})" if isinstance(r, dict) and r.get("title") else "")
        if not isinstance(r, dict):
            print(f"{label}: not an object")
            bad += 1
            continue
        r = dict(r)
        r.setdefault("schema", wl.SCHEMA)
        r.setdefault("frames", 64)
        if a.category and "category" not in r:
            r["category"] = a.category
        if not r.get("category"):
            print(f"{label}: no category (give the recipe a category or pass --category)")
            bad += 1
            continue
        clean, errors, warnings = wl.clean_recipe(r)
        for w in warnings:
            print(f"{label}: warning: {w}")
        if errors:
            print(f"{label}: REJECTED")
            for e in errors:
                print(f"  - {e}")
            bad += 1
            continue
        cat = clean["category"]
        existing = [n for n, c, _ in list_candidates(run) if c == cat and "-ai-" in n]
        name = f"{cat[:3].lower()}-ai-{len(existing):03d}"
        clean["seed"] = clean["seed"] or name
        try:
            write_candidate(run, name, cat, clean)
        except Exception as e:       # a recipe that passes the schema but cannot be rendered
            print(f"{label}: REJECTED, rendering failed: {e}")
            bad += 1
            continue
        print(f"{label}: accepted as {name}")
        ok += 1
    print(f"{ok} accepted, {bad} rejected; run folder {run}")
    if ok:
        print(f"next: wavetable-lab.py score --run {run}")
    return 0 if ok else 1


# ---- promote
def cmd_promote(a):
    run = os.path.abspath(a.run)
    hits = [(n, c, p) for n, c, p in list_candidates(run) if n == a.name]
    if not hits:
        sys.exit(f"no candidate {a.name} in {run}")
    r, errors, _ = load_recipe(hits[0][2])
    if errors:
        sys.exit("the candidate recipe is invalid: " + "; ".join(errors))
    r["category"] = a.category
    r["title"] = a.title
    slug = a.slug or slugify(a.title)
    if not slug:
        sys.exit("empty slug")
    r["seed"] = slug
    r.pop("tags", None)
    r, errors, _ = wl.clean_recipe(r)
    if errors:
        sys.exit("invalid: " + "; ".join(errors))
    if a.pack:
        if not re.match(r"^[a-z0-9]+(\.[a-z0-9-]+)+$", a.pack):
            sys.exit("--pack needs a pack id like com.safian.synthyboy.addon-name")
        pdir_name = a.pack.split(".")[-1]
        base = os.path.join(REPO, "content-src", "addons", pdir_name)
        os.makedirs(base, exist_ok=True)
        pj = os.path.join(base, "pack.json")
        if not os.path.isfile(pj):
            with open(pj, "w", encoding="utf-8") as f:
                json.dump({"id": a.pack, "name": a.pack_name or pdir_name, "version": "0.1.0", "description": f"Wavetables made with the SynthyBoy wavetable lab, CC0-1.0."}, f, indent=1)
                f.write("\n")
        root = base
    else:
        root = os.path.join(REPO, "content-src", "wavetables")
    dest = os.path.join(root, a.category, slug + ".json")
    if os.path.exists(dest) and not a.force:
        sys.exit(f"{dest} exists (use --force to replace it)")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(r, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(f"recipe written: {os.path.relpath(dest, REPO)}")
    cmd = [sys.executable, os.path.join(HERE, "gen-content-pack.py")] + (["--addon", pdir_name] if a.pack else ["--only", a.category])
    print("regenerating: " + " ".join(os.path.relpath(c, REPO) if c.startswith(REPO) else c for c in cmd))
    rc = subprocess.call(cmd, cwd=REPO)
    if rc == 0:
        print("done; commit content-src/ and the regenerated pack folder; the presets are unaffected")
    return rc


# ---- list / check
def cmd_list(a):
    if a.run:
        sc = json.load(open(os.path.join(os.path.abspath(a.run), "scores.json"))) if os.path.isfile(os.path.join(os.path.abspath(a.run), "scores.json")) else None
        for name, cat, _ in list_candidates(os.path.abspath(a.run)):
            if sc and name in sc["scores"]:
                s = sc["scores"][name]
                flag = "kept" if name in sc["selected"].get(cat, []) else ("rejected: " + ", ".join(s["rejects"]) if s["rejects"] else "")
                print(f"{name:14s} {cat:9s} q {s['quality']:.2f}  {flag}")
            else:
                print(f"{name:14s} {cat:9s}")
        return 0
    gcp = load_gcp()
    for e in gcp.load_recipes(gcp.RECIPES_ROOT):
        print(f"{e['category']:9s} {e['slug']:28s} {e['recipe']['frames']:3d}  {e['recipe']['title']}")
    for d, m, ents in gcp.addon_packs():
        for e in ents:
            print(f"{d}:{e['category']:9s} {e['slug']:28s} {e['recipe']['frames']:3d}  {e['recipe']['title']}")
    return 0


def cmd_check(a):
    bad = 0
    roots = [os.path.join(REPO, "content-src", "wavetables")]
    ad = os.path.join(REPO, "content-src", "addons")
    if os.path.isdir(ad):
        roots += [os.path.join(ad, d) for d in sorted(os.listdir(ad)) if os.path.isdir(os.path.join(ad, d))]
    paths = []
    for root in roots:
        for cat in sorted(os.listdir(root)) if os.path.isdir(root) else []:
            d = os.path.join(root, cat)
            if os.path.isdir(d):
                paths += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".json")]
    if a.run:
        paths += [p for _, _, p in list_candidates(os.path.abspath(a.run))]
    for p in paths:
        r, errors, warnings = load_recipe(p)
        if r is not None and not errors:
            try:
                data = wl.table_samples(r, os.path.basename(p)[:-5])
                if len(data) % N or not 1 <= len(data) // N <= 64:
                    errors.append("renders to a wrong length")
                if not np.all(np.isfinite(data)):
                    errors.append("renders non-finite samples")
            except Exception as e:
                errors.append(f"render failed: {e}")
        for e in errors + ["warning: " + w for w in warnings]:
            print(f"{os.path.relpath(p, REPO) if p.startswith(REPO) else p}: {e}")
        bad += bool(errors)
    print(f"{len(paths)} recipes checked, {bad} with errors")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description="SynthyBoy wavetable lab")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--seed", type=int, default=1)
    g.add_argument("--per-category", type=int, default=40)
    g.add_argument("--categories", nargs="*", choices=wl.CATEGORIES)
    g.add_argument("--out")
    s = sub.add_parser("score")
    s.add_argument("--run", required=True)
    s.add_argument("--keep", type=int, default=8)
    p = sub.add_parser("preview")
    p.add_argument("--run", required=True)
    p.add_argument("--all", action="store_true", help="render every scored candidate, not only the kept ones")
    q = sub.add_parser("propose")
    q.add_argument("--from-file", required=True)
    q.add_argument("--category", choices=wl.CATEGORIES)
    q.add_argument("--run")
    r = sub.add_parser("promote")
    r.add_argument("name")
    r.add_argument("--run", required=True)
    r.add_argument("--category", required=True, choices=wl.CATEGORIES)
    r.add_argument("--title", required=True)
    r.add_argument("--slug")
    r.add_argument("--pack")
    r.add_argument("--pack-name")
    r.add_argument("--force", action="store_true")
    l = sub.add_parser("list")
    l.add_argument("--run")
    c = sub.add_parser("check")
    c.add_argument("--run")
    a = ap.parse_args()
    return {"generate": cmd_generate, "score": cmd_score, "preview": cmd_preview, "propose": cmd_propose, "promote": cmd_promote, "list": cmd_list, "check": cmd_check}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
