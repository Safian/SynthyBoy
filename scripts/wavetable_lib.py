# SynthyBoy - wavetable library: the recipe format ("synthyboy-wavetable-recipe/1"), the building blocks, the normaliser and the measures. CC0-1.0 content, AGPL-3.0-or-later code.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# One wavetable = one recipe (plain JSON): a tree of generator nodes whose parameters are numbers or "laws" (how a parameter moves across the frames), plus a post section (the normaliser).
# Used by scripts/gen-content-pack.py (the factory pack and add-on packs) and scripts/wavetable-lab.py (generate / score / preview / promote / propose). Deterministic: every random number
# comes from numpy's PCG64 seeded by the recipe seed and a node label (never by call order), so a frame is the same whatever else is computed. Only numpy and the standard library.
#
# Spectra are half spectra of N = 2048 samples (numpy rfft convention): a sine harmonic of amplitude a and phase ph (radians, sine phase) is spec[k] = a * N / 2 * exp(j (ph - pi / 2)).

import hashlib
import importlib.util
import json
import math
import os

import numpy as np

N = 2048
H = N // 2
SCHEMA = "synthyboy-wavetable-recipe/1"
CATEGORIES = ["Analog", "Digital", "Spectral", "Vocal", "Metallic", "Noise"]
OS = 4                                   # oversampling of the time domain nodes

HERE = os.path.dirname(os.path.abspath(__file__))
_LEGACY = None


def legacy_table(name):
    """The hand written functions of the first 16 tables (gen-content-pack.py): kept as `legacy` nodes so that those files stay byte identical."""
    global _LEGACY
    if _LEGACY is None:
        spec = importlib.util.spec_from_file_location("gen_content_pack_legacy", os.path.join(HERE, "gen-content-pack.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        _LEGACY = dict(mod.LEGACY_TABLES)
    return _LEGACY[name]


def legacy_names():
    legacy_table("formant_sweep_aeiou")
    return sorted(_LEGACY)


def rng_for(name):
    return np.random.default_rng(int.from_bytes(hashlib.sha256(name.encode()).digest()[:8], "little"))


# ------------------------------------------------------------------------------------------------------------------------------ parameter laws
def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def _wave(shape, x):
    if shape == "tri":
        return (2 / math.pi) * math.asin(math.sin(2 * math.pi * x))
    if shape == "saw":
        return 2 * (x - math.floor(x + 0.5))
    return math.sin(2 * math.pi * x)


LAW_KEYS = {"const": {"v"}, "linear": {"from", "to"}, "curve": {"from", "to", "power"}, "steps": {"from", "to", "n"}, "lfo": {"center", "depth", "cycles", "phase", "shape"},
            "keys": {"points", "ease"}}


def law(v, u):
    """Value of a parameter at scan position u (0 .. 1). Numbers, strings and lists are constants."""
    if not isinstance(v, dict):
        return v
    t = v["law"]
    if t == "const":
        return v["v"]
    if t == "linear":
        return v["from"] + (v["to"] - v["from"]) * u
    if t == "curve":
        return v["from"] + (v["to"] - v["from"]) * u ** v.get("power", 1.0)
    if t == "steps":
        n = max(int(v.get("n", 4)), 2)
        k = min(int(u * n), n - 1)
        return v["from"] + (v["to"] - v["from"]) * k / (n - 1)
    if t == "lfo":
        return v.get("center", 0.0) + v.get("depth", 1.0) * _wave(v.get("shape", "sine"), v.get("cycles", 1.0) * u + v.get("phase", 0.0))
    if t == "keys":
        pts = v["points"]
        if u <= pts[0][0]:
            return pts[0][1]
        for (u0, a), (u1, b) in zip(pts, pts[1:]):
            if u <= u1:
                f = (u - u0) / max(u1 - u0, 1e-12)
                if v.get("ease") == "smooth":
                    f = smooth(f)
                return a + (b - a) * f
        return pts[-1][1]
    raise ValueError(t)


# ------------------------------------------------------------------------------------------------------------------------------ node catalogue
# name: (default, lo, hi) numbers; ("enum", default, options) strings; ("list", default) lists of numbers. `ints` are rounded.
def num(d, lo, hi, integer=False):
    return ("num", d, lo, hi, integer)


def enum(d, *opts):
    return ("enum", d, list(opts))


def slist(opts):
    return ("slist", [], list(opts))


VOWELS = ["i", "I", "E", "ae", "a", "o", "U", "u", "V"]
NODES = {
    "additive": {"inputs": 0, "params": {
        "count": num(64, 1, 512), "rolloff": num(1.0, 0, 4), "mask": enum("all", "all", "odd", "even", "prime", "every"), "mask_n": num(2, 1, 32, True), "mask_soft": num(0.0, 0, 1),
        "stretch": num(0.0, 0, 0.02), "comb_period": num(0.0, 0, 64), "comb_depth": num(1.0, 0, 1), "comb_offset": num(0.0, 0, 64),
        "peak_center": num(8.0, 1, 300), "peak_width": num(1.0, 0.1, 4), "peak_gain_db": num(0.0, -30, 36),
        "phase_quad": num(0.0, 0, 0.05), "phase_random": num(0.0, 0, 1), "fundamental": num(1.0, 0, 4)}},
    "formant": {"inputs": 0, "params": {
        "f0": num(130.0, 60, 600), "voice": enum("men", "men", "women", "children"), "voice_b": enum("", "", "men", "women", "children"), "voice_mix": num(0.0, 0, 1),
        "vowel_a": enum("a", *VOWELS), "vowel_b": enum("a", *VOWELS), "mix": num(0.0, 0, 1), "sequence": slist(VOWELS), "pos": num(0.0, 0, 31), "shift": num(1.0, 0.4, 2.5), "tilt": num(1.0, 0, 3), "bw_floor": num(0.7, 0.3, 1.5), "count": num(200, 20, 500),
        "breath": num(0.0, 0, 1), "scatter": num(0.0, 0, 1), "seed": num(1, 0, 9999, True), "h1_boost_db": num(0.0, -12, 12)}},
    "fm": {"inputs": 0, "params": {
        "carrier": num(1, 1, 8, True), "r1": num(0, 0, 16, True), "i1": num(0.0, 0, 20), "r2": num(0, 0, 16, True), "i2": num(0.0, 0, 20), "r3": num(0, 0, 16, True), "i3": num(0.0, 0, 20),
        "feedback": num(0.0, 0, 2.2), "phase": num(0.0, 0, 1), "mod_phase": num(0.0, 0, 1)}},
    "pulse": {"inputs": 0, "params": {
        "width": num(0.5, 0.01, 0.5), "stack": num(1, 1, 7, True), "width_spread": num(0.0, 0, 0.45), "shift_spread": num(0.0, 0, 0.5), "saw_mix": num(0.0, 0, 1), "stack_gain": num(1.0, 0, 1.5)}},
    "classic": {"inputs": 0, "params": {
        "sine": num(0.0, 0, 1), "tri": num(0.0, 0, 1), "saw": num(1.0, 0, 1), "square": num(0.0, 0, 1), "skew": num(0.5, 0.02, 0.98), "oct2": num(0.0, 0, 1), "oct4": num(0.0, 0, 1),
        "count": num(300, 8, 500), "stack": num(1, 1, 9, True), "stack_spread": num(0.0, 0, 0.5)}},
    "pd": {"inputs": 0, "params": {
        "mode": enum("knee", "knee", "resonance", "pulsar"), "amount": num(0.5, 0.0, 48.0), "width": num(0.15, 0.03, 0.5)}},
    "sync": {"inputs": 0, "params": {
        "ratio": num(1.0, 0.5, 16), "slave": enum("sine", "sine", "saw", "tri", "square"), "window": enum("none", "none", "fade", "hann")}},
    "shape": {"inputs": 1, "params": {
        "mode": enum("tanh", "tanh", "fold", "clip", "power", "bits", "rectify"), "drive": num(1.0, 0.1, 30), "bias": num(0.0, -1, 1), "power": num(1.0, 0.05, 3), "bits": num(8.0, 1.5, 12),
        "wet": num(1.0, 0, 1)}},
    "spec": {"inputs": 1, "params": {
        "tilt": num(0.0, -3, 3), "shift": num(0.0, -32, 64), "blur": num(0.0, 0, 20), "band_center": num(8.0, 1, 400), "band_width": num(1.0, 0.1, 4), "band_gain_db": num(0.0, -40, 36),
        "lp_cut": num(0.0, 0, 800), "lp_res": num(0.0, 0, 8), "lp_poles": enum("2", "2", "4"), "hp_cut": num(0.0, 0, 500), "hp_res": num(0.0, 0, 8),
        "comb_period": num(0.0, 0, 64), "comb_depth": num(1.0, 0, 1), "comb_offset": num(0.0, 0, 64), "rcomb_d": num(0.0, 0, 0.5), "rcomb_g": num(0.0, 0, 0.97),
        "scatter": num(0.0, 0, 1), "seed": num(1, 0, 9999, True)}},
    "ring": {"inputs": 1, "params": {"c": num(2.0, 0.25, 40), "dry": num(0.0, 0, 1), "max_k": num(48, 1, 300, True)}},
    "mix": {"inputs": -1, "params": {"g1": num(1.0, 0, 2), "g2": num(1.0, 0, 2), "g3": num(1.0, 0, 2), "g4": num(1.0, 0, 2), "match": enum("rms", "rms", "none")}},
    "partials": {"inputs": 0, "params": {
        "set": enum("bell", "bell", "bar", "membrane", "gong", "bowl", "harmonic", "cloud", "random"), "scale": num(3.0, 0.5, 40), "count": num(10, 1, 48, True), "rolloff": num(1.0, 0, 3),
        "bright": num(1.0, 0.3, 1.0), "pair_delta": num(0.0, 0, 3), "jitter": num(0.0, 0, 0.6), "lo": num(4.0, 1, 200), "hi": num(120.0, 8, 400), "wobble": num(0.0, 0, 20),
        "wobble_cycles": num(1.0, 0, 8), "keys": num(4, 2, 8, True), "pos": num(0.0, 0, 7), "seed": num(1, 0, 9999, True), "ratios": ("list", []), "amps": ("list", [])}},
    "noise": {"inputs": 0, "params": {
        "tilt": num(0.0, -3, 3), "density": num(1.1, 0.0, 1.2), "count": num(380, 8, 500), "seed": num(1, 0, 9999, True)}},
    "clicks": {"inputs": 0, "params": {"count": num(8.0, 1, 120), "tail": num(0.0, 0, 200), "seed": num(1, 0, 9999, True)}},
    "burst": {"inputs": 0, "params": {"width": num(0.2, 0.01, 1.0), "tilt": num(0.0, -2, 2), "seed": num(1, 0, 9999, True)}},
    "stepseq": {"inputs": 0, "params": {
        "steps": num(8, 2, 32, True), "keys": num(5, 2, 8, True), "pos": num(0.0, 0, 7), "slew": num(0.15, 0.01, 0.6), "seed": num(1, 0, 9999, True)}},
    "gaussformant": {"inputs": 0, "params": {"m": num(4.0, 1, 48), "width": num(0.12, 0.03, 0.5)}},
    "legacy": {"inputs": 0, "params": {"fn": ("enum", "", None)}},
}
POST = {"limit": num(400, 16, 1000, True), "smooth": enum("yes", "yes", "no"), "level": enum("rms", "rms", "peak", "none"), "rms": num(0.35, 0.05, 0.6), "peak": num(0.9, 0.3, 0.98)}
TOP = {"schema", "title", "category", "description", "frames", "seed", "source", "post", "tags"}


# ------------------------------------------------------------------------------------------------------------------------------ validation
def _clamp(v, spec):
    _, d, lo, hi, integer = spec
    v = min(max(v, lo), hi)
    return int(round(v)) if integer else v


def _clean_param(name, v, spec, path, errors, warnings):
    kind = spec[0]
    if kind == "enum":
        opts = spec[2]
        if not isinstance(v, str) or (opts is not None and v not in opts):
            errors.append(f"{path}.{name}: {v!r} is not one of {opts}")
            return spec[1]
        return v
    if kind == "slist":
        if not isinstance(v, list) or len(v) > 32 or not all(x in spec[2] for x in v):
            errors.append(f"{path}.{name}: a list of at most 32 names from {spec[2]}")
            return []
        return list(v)
    if kind == "list":
        if not isinstance(v, list) or not all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v) or len(v) > 64:
            errors.append(f"{path}.{name}: needs a list of at most 64 numbers")
            return []
        return [float(x) for x in v]
    if isinstance(v, dict):
        t = v.get("law")
        if t not in LAW_KEYS:
            errors.append(f"{path}.{name}: unknown law {t!r} (one of {sorted(LAW_KEYS)})")
            return spec[1]
        extra = set(v) - LAW_KEYS[t] - {"law"}
        if extra:
            errors.append(f"{path}.{name}: law {t} has no key(s) {sorted(extra)}")
        out = {"law": t}
        for k in LAW_KEYS[t]:
            if k not in v:
                if t in ("const", "linear", "curve", "steps") and k in ("v", "from", "to") or (t == "lfo" and k == "center") or (t == "keys" and k == "points"):
                    errors.append(f"{path}.{name}: law {t} needs {k!r}")
                continue
            x = v[k]
            if k in ("v", "from", "to", "center"):
                if not isinstance(x, (int, float)) or isinstance(x, bool):
                    errors.append(f"{path}.{name}.{k}: not a number")
                    continue
                c = _clamp(x, (None, spec[1], spec[2], spec[3], False))
                if c != x:
                    warnings.append(f"{path}.{name}.{k}: {x} clamped to {c}")
                x = c
            elif k == "points":
                pts = []
                ok = isinstance(x, list) and 2 <= len(x) <= 32
                for p in x if ok else []:
                    if not (isinstance(p, list) and len(p) == 2 and all(isinstance(q, (int, float)) for q in p)):
                        ok = False
                        break
                    c = _clamp(p[1], (None, spec[1], spec[2], spec[3], False))
                    if c != p[1]:
                        warnings.append(f"{path}.{name}.points: {p[1]} clamped to {c}")
                    pts.append([min(max(float(p[0]), 0.0), 1.0), c])
                if not ok or any(b[0] < a[0] for a, b in zip(pts, pts[1:])):
                    errors.append(f"{path}.{name}.points: 2 .. 32 pairs [u, value] with u rising")
                    continue
                x = pts
            elif k == "shape":
                if x not in ("sine", "tri", "saw"):
                    errors.append(f"{path}.{name}.shape: sine, tri or saw")
                    continue
            elif k == "ease":
                if x not in ("linear", "smooth"):
                    errors.append(f"{path}.{name}.ease: linear or smooth")
                    continue
            elif not isinstance(x, (int, float)) or isinstance(x, bool):
                errors.append(f"{path}.{name}.{k}: not a number")
                continue
            out[k] = x
        if t == "lfo":
            for k in ("depth", "cycles", "phase"):
                if k in out and abs(out[k]) > 1000:
                    errors.append(f"{path}.{name}.{k}: out of range")
        return out
    if not isinstance(v, (int, float)) or isinstance(v, bool):
        errors.append(f"{path}.{name}: {v!r} is not a number")
        return spec[1]
    c = _clamp(v, spec)
    if c != v:
        warnings.append(f"{path}.{name}: {v} clamped to {c}")
    return c


def _clean_node(node, path, errors, warnings, depth=0):
    if not isinstance(node, dict) or depth > 6:
        errors.append(f"{path}: needs a node object (nesting at most 6 deep)")
        return {"type": "additive", "params": {}}
    t = node.get("type")
    if t not in NODES:
        errors.append(f"{path}.type: {t!r} is not one of {sorted(NODES)}")
        return {"type": "additive", "params": {}}
    info = NODES[t]
    extra = set(node) - {"type", "params", "input", "inputs"}
    if extra:
        errors.append(f"{path}: unknown key(s) {sorted(extra)}")
    out = {"type": t, "params": {}}
    params = node.get("params", {})
    if not isinstance(params, dict):
        errors.append(f"{path}.params: needs an object")
        params = {}
    for name, v in params.items():
        spec = info["params"].get(name)
        if spec is None:
            errors.append(f"{path}.params.{name}: {t} has no such parameter (known: {sorted(info['params'])})")
            continue
        if t == "legacy":
            continue
        out["params"][name] = _clean_param(name, v, spec, path + ".params", errors, warnings)
    if t == "legacy":
        out["params"] = dict(params)
        if params.get("fn") not in legacy_names():
            errors.append(f"{path}.params.fn: not a legacy table")
    n_in = info["inputs"]
    if n_in == 1:
        out["input"] = _clean_node(node.get("input"), path + ".input", errors, warnings, depth + 1)
    elif n_in == -1:
        ins = node.get("inputs")
        if not isinstance(ins, list) or not 1 <= len(ins) <= 4:
            errors.append(f"{path}.inputs: 1 .. 4 nodes")
            ins = []
        out["inputs"] = [_clean_node(x, f"{path}.inputs[{i}]", errors, warnings, depth + 1) for i, x in enumerate(ins)]
    elif "input" in node or "inputs" in node:
        errors.append(f"{path}: {t} takes no input")
    return out


def clean_recipe(r):
    """-> (clean recipe, errors, warnings). Errors: the recipe must not be used; warnings: values were clamped."""
    errors, warnings = [], []
    if not isinstance(r, dict):
        return None, ["the recipe must be a JSON object"], []
    extra = set(r) - TOP
    if extra:
        errors.append(f"unknown top level key(s) {sorted(extra)}")
    if r.get("schema") != SCHEMA:
        errors.append(f"schema must be {SCHEMA!r}")
    out = {"schema": SCHEMA}
    for k in ("title", "description"):
        v = r.get(k, "")
        if not isinstance(v, str) or (k == "title" and not v.strip()):
            errors.append(f"{k}: needs a text" if k == "title" else f"{k}: must be text")
            v = ""
        out[k] = v.strip()
    cat = r.get("category")
    if cat is not None and cat not in CATEGORIES:
        errors.append(f"category: {cat!r} is not one of {CATEGORIES}")
    if cat is not None:
        out["category"] = cat
    fr = r.get("frames", 64)
    if not isinstance(fr, int) or isinstance(fr, bool) or not 2 <= fr <= 64:
        errors.append("frames: an integer 2 .. 64")
        fr = 64
    out["frames"] = fr
    seed = r.get("seed", "")
    if not isinstance(seed, (str, int)) or isinstance(seed, bool):
        errors.append("seed: text or integer")
    out["seed"] = str(seed)
    if "tags" in r:
        out["tags"] = [str(x) for x in r["tags"]] if isinstance(r["tags"], list) else []
    out["source"] = _clean_node(r.get("source"), "source", errors, warnings)
    post = r.get("post", {})
    if not isinstance(post, dict) or set(post) - set(POST):
        errors.append(f"post: an object with keys from {sorted(POST)}")
        post = {}
    out["post"] = {name: _clean_param(name, v, POST[name], "post", errors, warnings) for name, v in post.items()}
    return out, errors, warnings


# ------------------------------------------------------------------------------------------------------------------------------ spectrum helpers
def harm(amps, ph=None, start=1):
    """Half spectrum from harmonic amplitudes amps[0] = harmonic `start` (consecutive), sine phases ph."""
    s = np.zeros(H + 1, dtype=np.complex128)
    a = np.asarray(amps, dtype=np.float64)
    k = np.arange(start, start + len(a))
    keep = k < H
    p = np.zeros(len(a)) if ph is None else np.asarray(ph, dtype=np.float64)
    s[k[keep]] = (a * (N / 2.0) * np.exp(1j * (p - np.pi / 2)))[keep]
    return s


def place(s, pos, coeff):
    """Add a complex coefficient at the real harmonic position pos (linear split between the two neighbouring bins; negative positions fold with a conjugate)."""
    if pos < 0:
        pos, coeff = -pos, np.conj(coeff)
    k = int(pos)
    w = pos - k
    if k + 1 >= H:
        return
    if k >= 1:
        s[k] += coeff * (1 - w)
    s[k + 1] += coeff * w


def to_time(s, os_=OS):
    big = np.zeros(N * os_ // 2 + 1, dtype=np.complex128)
    big[:H] = s[:H]
    return np.fft.irfft(big, N * os_) * os_


def from_time(y, os_=OS):
    return np.fft.rfft(y)[:H + 1] / os_


def grid(os_=OS):
    return np.arange(N * os_) / (N * os_)


def spec_rms(s):
    return math.sqrt(2.0 * float(np.sum(np.abs(s[1:H]) ** 2))) / N


def primes_upto(n):
    sieve = np.ones(n + 1, bool)
    sieve[:2] = False
    for i in range(2, int(n ** 0.5) + 1):
        if sieve[i]:
            sieve[i * i::i] = False
    return np.nonzero(sieve)[0]


def sigma_window(limit):
    k = np.arange(0, H + 1, dtype=np.float64)
    w = np.where(k <= limit, np.sinc(k / (limit + 1.0)), 0.0)
    return w


# ------------------------------------------------------------------------------------------------------------------------------ formants
# Vowel formants F1 F2 F3 (Hz): the averages of Peterson & Barney, "Control methods used in a study of the vowels", J. Acoust. Soc. Am. 24 (2), 1952, pp. 175-184 (table of averages
# for men, women and children; the usual textbook reproduction). Vowel keys: i (heed) I (hid) E (head) ae (had) a (hod) o (hawed) U (hood) u (who'd) V (hud).
# F4 (3500 / 4000 / 4500 Hz) and the bandwidths (80 / 90 / 120 / 200 Hz, never narrower than 0.7 of the pitch so that the harmonics sample the peak evenly) are generic round values.
PB = {
    "men": {"i": (270, 2290, 3010), "I": (390, 1990, 2550), "E": (530, 1840, 2480), "ae": (660, 1720, 2410), "a": (730, 1090, 2440), "o": (570, 840, 2410), "U": (440, 1020, 2240),
            "u": (300, 870, 2240), "V": (640, 1190, 2390)},
    "women": {"i": (310, 2790, 3310), "I": (430, 2480, 3070), "E": (610, 2330, 2990), "ae": (860, 2050, 2850), "a": (850, 1220, 2810), "o": (590, 920, 2710), "U": (470, 1160, 2680),
              "u": (370, 950, 2670), "V": (760, 1400, 2780)},
    "children": {"i": (370, 3200, 3730), "I": (530, 2730, 3600), "E": (690, 2610, 3570), "ae": (1010, 2320, 3320), "a": (1030, 1370, 3170), "o": (680, 1060, 3180), "U": (560, 1410, 3310),
                 "u": (430, 1170, 3260), "V": (850, 1590, 3360)},
}
F4 = {"men": 3500, "women": 4000, "children": 4500}


def formant_gain(f, formants):
    g = np.zeros_like(f)
    for fc, bw, gdb in formants:
        q = fc / bw
        g = g + (10 ** (gdb / 20.0)) / np.sqrt((1 - (f / fc) ** 2) ** 2 + (f / (q * fc)) ** 2)
    return g


# ------------------------------------------------------------------------------------------------------------------------------ nodes
class Ctx:
    def __init__(self, seed, u):
        self.seed, self.u = seed, u

    def rng(self, label, extra=0):
        return rng_for(f"{self.seed}:{label}:{extra}")


def node_additive(p, ctx, inp):
    cnt = p["count"]
    kmax = min(int(math.ceil(cnt)), H - 2)
    k = np.arange(1, kmax + 1, dtype=np.float64)
    amp = k ** -p["rolloff"] * np.clip(cnt - k + 1, 0, 1)
    m = p["mask"]
    if m == "odd":
        on = k % 2 == 1
    elif m == "even":
        on = (k % 2 == 0) | (k == 1)
    elif m == "prime":
        on = np.isin(k, primes_upto(kmax + 1)) | (k == 1)
    elif m == "every":
        on = (k % p["mask_n"] == 0) | (k == 1)
    else:
        on = np.ones(len(k), bool)
    amp = amp * np.where(on, 1.0, p["mask_soft"])
    amp[0] *= p["fundamental"]
    if p["comb_period"] > 0:
        amp = amp * ((1 - p["comb_depth"]) + p["comb_depth"] * (0.5 + 0.5 * np.cos(2 * np.pi * (k - p["comb_offset"]) / p["comb_period"])))
    if p["peak_gain_db"] != 0:
        amp = amp * 10 ** (p["peak_gain_db"] / 20.0 * np.exp(-0.5 * (np.log2(k / p["peak_center"]) / p["peak_width"]) ** 2))
    ph = p["phase_quad"] * k * k
    if p["phase_random"] > 0:
        ph = ph + p["phase_random"] * ctx.rng("additive-phase").uniform(0, 2 * np.pi, H)[:len(k)]
    if p["stretch"] > 0:
        s = np.zeros(H + 1, dtype=np.complex128)
        for kk, a, f in zip(k, amp, ph):
            place(s, kk * (1 + p["stretch"] * kk), a * (N / 2.0) * np.exp(1j * (f - np.pi / 2)))
        return s
    return harm(amp, ph)


def node_formant(p, ctx, inp):
    va, vb = p["voice"], (p["voice_b"] or p["voice"])
    seq = p["sequence"]
    if seq:
        f = min(max(p["pos"], 0.0), len(seq) - 1.0001)
        i0 = int(f)
        a_, b_, m = seq[i0], seq[min(i0 + 1, len(seq) - 1)], smooth(f - i0)
    else:
        a_, b_, m = p["vowel_a"], p["vowel_b"], p["mix"]

    def f123(voice):
        return np.exp((1 - m) * np.log(np.array(PB[voice][a_], float)) + m * np.log(np.array(PB[voice][b_], float)))
    vm = p["voice_mix"]
    ff = np.exp((1 - vm) * np.log(f123(va)) + vm * np.log(f123(vb))) * p["shift"]
    f4 = (F4[va] * (1 - vm) + F4[vb] * vm) * p["shift"]
    floor = p["bw_floor"] * p["f0"]
    forms = [(ff[0], max(80, floor), 0.0), (ff[1], max(90, floor), -6.0), (ff[2], max(120, floor), -12.0), (f4, max(200, floor), -22.0)]
    kmax = min(int(p["count"]), H - 2)
    k = np.arange(1, kmax + 1, dtype=np.float64)
    amp = formant_gain(k * p["f0"], forms) / k ** p["tilt"]
    amp[0] *= 10 ** (p["h1_boost_db"] / 20.0)
    r = ctx.rng("formant", p["seed"])
    ray = np.sqrt(-2 * np.log(1 - r.uniform(0, 0.999, kmax)))
    ph_rand = r.uniform(0, 2 * np.pi, kmax)
    amp = amp * ((1 - p["breath"]) + p["breath"] * ray / 1.25)
    return harm(amp, (p["breath"] + p["scatter"]) * ph_rand)


def node_fm(p, ctx, inp):
    """Up to four operators in a chain: out = sin(carrier t + i1 * op1), op1 = sin(r1 t + i2 * op2), op2 = sin(r2 t + i3 * op3), op3 = sin(r3 t); an operator with ratio 0 is off.
    feedback: the carrier feeds back its previous sample (the one operator recursion, computed at the table rate)."""
    fb = p["feedback"] > 0
    t = (np.arange(N) / N if fb else grid()) * 2 * np.pi
    o3 = np.sin(p["r3"] * t) if p["r3"] > 0 else 0.0
    o2 = np.sin(p["r2"] * t + p["i3"] * o3) if p["r2"] > 0 else 0.0
    o1 = np.sin(p["r1"] * t + p["mod_phase"] * 2 * np.pi + p["i2"] * o2) if p["r1"] > 0 else 0.0
    y = p["i1"] * o1 if p["r1"] > 0 else 0.0 * t
    ph = p["phase"] * 2 * np.pi
    if fb:
        out = np.zeros(N)
        prev = 0.0
        for rep in range(4):
            for j in range(N):
                prev = math.sin(p["carrier"] * t[j] + ph + p["feedback"] * prev + y[j])
                out[j] = prev
        return from_time(out, 1)
    return from_time(np.sin(p["carrier"] * t + ph + y))


def node_pulse(p, ctx, inp):
    k = np.arange(1, 700, dtype=np.float64)
    tot = np.zeros(H + 1, dtype=np.complex128)
    m = p["stack"]
    for j in range(m):
        off = (j - (m - 1) / 2.0) / max((m - 1) / 2.0, 1.0) if m > 1 else 0.0
        w = min(max(p["width"] + p["width_spread"] * off * 0.5, 0.01), 0.5) if m > 1 else p["width"]
        amps = 2.0 * np.abs(np.sin(np.pi * k * w)) / (np.pi * k)
        ph = np.where(np.sin(np.pi * k * w) >= 0, 0.0, np.pi) + np.pi * k * w - np.pi / 2 - 2 * np.pi * k * p["shift_spread"] * off
        g = p["stack_gain"] if j != (m - 1) // 2 or m == 1 else 1.0
        tot = tot + g * harm(amps, ph)
    if p["saw_mix"] > 0:
        tot = (1 - p["saw_mix"]) * tot + p["saw_mix"] * harm(1.0 / k)
    return tot


def _tri_skew(x, s):
    return np.where(x < s, 2 * x / s - 1, 1 - 2 * (x - s) / (1 - s))


def node_classic(p, ctx, inp):
    lim = int(p["count"])
    k = np.arange(1, lim + 1, dtype=np.float64)
    w = np.sinc(k / (lim + 1.0))
    s = np.zeros(H + 1, dtype=np.complex128)
    if p["sine"] > 0:
        s += p["sine"] * harm([1.0])
    if p["saw"] > 0:
        s += p["saw"] * harm(w / k)
    if p["square"] > 0:
        s += p["square"] * harm(np.where(k % 2 == 1, w / k, 0.0) * 4 / np.pi)
    if p["tri"] > 0:
        if abs(p["skew"] - 0.5) < 1e-6:
            s += p["tri"] * harm(np.where(k % 2 == 1, w / k ** 2, 0.0) * 8 / np.pi ** 2, np.where(((k - 1) / 2) % 2 == 0, 0.0, np.pi))
        else:
            x = grid()
            y = _tri_skew(x, p["skew"])
            s += p["tri"] * from_time(y) * sigma_window(lim)
    out = s.copy()
    for mult, g in ((2, p["oct2"]), (4, p["oct4"])):
        if g > 0:
            sh = np.zeros_like(s)
            idx = np.arange(1, H // mult)
            sh[idx * mult] = s[idx]
            out += g * sh
    if p["stack"] > 1:
        tot = np.zeros_like(out)
        kk = np.arange(H + 1)
        for j in range(p["stack"]):
            off = (j - (p["stack"] - 1) / 2.0) / ((p["stack"] - 1) / 2.0)
            tot += out * np.exp(-2j * np.pi * kk * off * p["stack_spread"])
        out = tot
    return out


def node_pd(p, ctx, inp):
    x = grid()
    if p["mode"] == "knee":
        kn = min(max(p["amount"], 0.02), 0.5)      # amount is read as the knee position (0.5 = plain sine)
        g = np.where(x < kn, 0.5 * x / kn, 0.5 + 0.5 * (x - kn) / (1 - kn))
        y = np.cos(2 * np.pi * g)
    elif p["mode"] == "resonance":
        y = np.sin(2 * np.pi * p["amount"] * x) * (1 - x)
    else:
        xx = np.where(x > 0.5, x - 1, x)
        y = np.exp(-xx ** 2 / (2 * p["width"] ** 2)) * np.cos(2 * np.pi * p["amount"] * xx)
    return from_time(y)


def node_sync(p, ctx, inp):
    x = grid()
    ph = (x * p["ratio"]) % 1.0
    y = {"sine": np.sin(2 * np.pi * ph), "saw": 2 * ph - 1, "tri": 1 - 4 * np.abs(ph - 0.5), "square": np.where(ph < 0.5, 1.0, -1.0)}[p["slave"]]
    if p["window"] == "fade":
        y = y * (1.0 - x)
    elif p["window"] == "hann":
        y = y * np.sin(np.pi * x)
    y = y * {"none": 1.0, "fade": 1.0 - x, "hann": np.sin(np.pi * x)}[p["window"]] if p["window"] != "none" else y
    return from_time(y)


def node_shape(p, ctx, inp):
    y = to_time(inp)
    pk = max(np.max(np.abs(y)), 1e-12)
    x = y / pk
    d = p["drive"]
    b = p["bias"]
    m = p["mode"]
    if m == "tanh":
        z = np.tanh(d * x + b)
    elif m == "fold":
        z = (2 / np.pi) * np.arcsin(np.sin(np.pi / 2 * (d * x + b)))
    elif m == "clip":
        z = np.clip(d * x + b, -1, 1)
    elif m == "power":
        u = d * x + b
        z = np.sign(u) * np.abs(np.clip(u, -1, 1)) ** p["power"]
    elif m == "bits":
        lv = 2 ** (p["bits"] - 1)
        z = np.round(np.clip(d * x + b, -1, 1) * lv) / lv
    else:
        z = 2 * np.abs(np.clip(d * x + b, -1, 1)) - 1
    z = (1 - p["wet"]) * x + p["wet"] * z
    return from_time(z)


def node_spec(p, ctx, inp):
    s = inp.copy()
    k = np.arange(H + 1, dtype=np.float64)
    if p["shift"] != 0:
        src = k - p["shift"]
        re = np.interp(src, k, s.real, left=0.0, right=0.0)
        im = np.interp(src, k, s.imag, left=0.0, right=0.0)
        s = re + 1j * im
    mag, ph = np.abs(s), np.angle(s)
    if p["blur"] > 0:
        r = int(math.ceil(3 * p["blur"]))
        ker = np.exp(-0.5 * (np.arange(-r, r + 1) / p["blur"]) ** 2)
        ker /= ker.sum()
        mag = np.convolve(mag, ker, mode="same")
    kk = np.maximum(k, 1.0)
    if p["tilt"] != 0:
        mag = mag * kk ** -p["tilt"]
    if p["band_gain_db"] != 0:
        mag = mag * 10 ** (p["band_gain_db"] / 20.0 * np.exp(-0.5 * (np.log2(kk / p["band_center"]) / p["band_width"]) ** 2))

    def res2(x, q):
        return 1.0 / np.sqrt((1 - x ** 2) ** 2 + (x / q) ** 2)
    if p["lp_cut"] > 0:
        x = kk / p["lp_cut"]
        g = res2(x, 0.5 + p["lp_res"])
        if p["lp_poles"] == "4":
            g = g * res2(x, 0.71)
        mag = mag * np.minimum(g, 40.0)
    if p["hp_cut"] > 0:
        x = kk / p["hp_cut"]
        mag = mag * np.minimum(x ** 2 * res2(x, 0.5 + p["hp_res"]), 40.0)
    if p["comb_period"] > 0:
        mag = mag * ((1 - p["comb_depth"]) + p["comb_depth"] * (0.5 + 0.5 * np.cos(2 * np.pi * (kk - p["comb_offset"]) / p["comb_period"])))
    if p["rcomb_d"] > 0 and p["rcomb_g"] > 0:
        g = p["rcomb_g"]
        mag = mag * (1 - g) / np.sqrt(1 + g * g - 2 * g * np.cos(2 * np.pi * kk * p["rcomb_d"]))
    if p["scatter"] > 0:
        ph = ph + p["scatter"] * ctx.rng("spec-scatter", p["seed"]).uniform(0, 2 * np.pi, H + 1)
    return mag * np.exp(1j * ph)


def node_ring(p, ctx, inp):
    out = p["dry"] * inp
    out = out.astype(np.complex128)
    c = p["c"]
    for kk in range(1, min(p["max_k"], H - 1) + 1):
        a = inp[kk]
        if abs(a) < 1e-9 * N:
            continue
        place(out, kk + c, a / 2j)
        place(out, kk - c, -a / 2j)
    return out


def _set_ratios(p, ctx):
    name = p["set"]
    if p["ratios"]:
        return list(p["ratios"])
    if name == "bell":
        return [0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215]
    if name == "bar":
        return [1.0, 2.756, 5.404, 8.933, 13.344, 18.64, 24.89, 32.1]        # free-free bar (Euler-Bernoulli)
    if name == "membrane":
        return [1.0, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.600, 3.652, 4.060, 4.154]   # ideal circular membrane (Bessel zeros)
    if name == "gong":
        return [1.0, 1.47, 2.09, 2.56, 2.99, 3.62, 4.10, 4.66, 5.42, 6.63, 7.51, 9.28, 11.7]
    if name == "bowl":
        return [1.0, 2.71, 5.15, 8.4, 12.2, 16.8]
    return [float(i) for i in range(1, 49)]


def node_partials(p, ctx, inp):
    s = np.zeros(H + 1, dtype=np.complex128)
    r = ctx.rng("partials", p["seed"])
    n = p["count"]
    sets = p["set"]
    if sets in ("cloud", "random"):
        keys = p["keys"] if sets == "random" else 1
        def positions(kidx):
            rr = ctx.rng("partials-key", p["seed"] * 100 + kidx)
            pos = p["lo"] + (p["hi"] - p["lo"]) * rr.uniform(0, 1, n) ** 1.3
            amp = rr.uniform(0.3, 1.0, n) / np.arange(1, n + 1) ** p["rolloff"] * np.arange(1, n + 1) ** 0.0
            return pos, rr.permutation(amp), rr.uniform(0, 2 * np.pi, n)
        wob_ph = r.uniform(0, 1, n)
        if sets == "random":
            f = min(max(p["pos"], 0.0), keys - 1.0001)
            k0 = int(f)
            fr = smooth(f - k0)
            lst = [(positions(k0), 1 - fr), (positions(k0 + 1), fr)]
        else:
            lst = [(positions(0), 1.0)]
        for (pos, amp, ph), wgt in lst:
            for j in range(n):
                q = pos[j] + p["wobble"] * math.sin(2 * math.pi * (p["wobble_cycles"] * ctx.u + wob_ph[j]))
                place(s, q, wgt * amp[j] * (N / 2.0) * np.exp(1j * (ph[j] - np.pi / 2)))
        return s
    ratios = _set_ratios(p, ctx)[:n]
    jit = r.uniform(-1, 1, len(ratios)) * p["jitter"]
    ph = r.uniform(0, 2 * np.pi, len(ratios))
    amps = list(p["amps"]) if p["amps"] else [1.0 / (i + 1) ** p["rolloff"] * p["bright"] ** i for i in range(len(ratios))]
    for i, rt in enumerate(ratios):
        pos = rt * p["scale"] * (1 + jit[i])
        a = amps[i] if i < len(amps) else amps[-1]
        pairs = [(-0.5, 1), (0.5, 1)] if p["pair_delta"] > 0 else [(0.0, 1)]
        for off, _ in pairs:
            place(s, pos + off * p["pair_delta"] * (1 + 0.15 * i), a * (0.7 if p["pair_delta"] > 0 else 1.0) * (N / 2.0) * np.exp(1j * (ph[i] - np.pi / 2)))
    return s


def node_noise(p, ctx, inp):
    r = ctx.rng("noise", p["seed"])
    kmax = min(int(p["count"]), H - 2)
    k = np.arange(1, kmax + 1, dtype=np.float64)
    ray = np.sqrt(-2 * np.log(1 - r.uniform(0, 0.999, kmax))) / 1.25
    ph = r.uniform(0, 2 * np.pi, kmax)
    gate_u = r.uniform(0, 1, kmax)
    gate = np.clip((p["density"] - gate_u) / 0.06, 0, 1) if p["density"] < 1.0 else np.ones(kmax)
    return harm(ray * k ** -p["tilt"] * gate, ph)


def node_clicks(p, ctx, inp):
    r = ctx.rng("clicks", p["seed"])
    n = 128
    pos = r.uniform(0, 1, n)
    amp = r.choice([-1.0, 1.0], n) * r.uniform(0.4, 1.0, n)
    s = np.zeros(H + 1, dtype=np.complex128)
    k = np.arange(H + 1, dtype=np.float64)
    tail = 1.0 / (1 + 1j * 2 * np.pi * k * p["tail"] / N) if p["tail"] > 0 else np.ones(H + 1)
    for j in range(int(math.ceil(p["count"]))):
        g = min(p["count"] - j, 1.0)
        s += g * amp[j] * np.exp(-2j * np.pi * k * pos[j]) * tail
    return s * (N / 8.0)


def node_burst(p, ctx, inp):
    r = ctx.rng("burst", p["seed"])
    k = np.arange(1, 600, dtype=np.float64)
    s = harm(np.sqrt(-2 * np.log(1 - r.uniform(0, 0.999, len(k)))) / 1.25 * k ** -p["tilt"], r.uniform(0, 2 * np.pi, len(k)))
    y = to_time(s)
    x = grid()
    xx = np.where(x > 0.5, x - 1, x)
    w = np.exp(-xx ** 2 / (2 * (p["width"] / 2.5) ** 2))
    return from_time(y * w)


def node_stepseq(p, ctx, inp):
    steps, keys = p["steps"], p["keys"]
    f = min(max(p["pos"], 0.0), keys - 1.0001)
    k0 = int(f)
    fr = smooth(f - k0)
    a = ctx.rng("stepseq", p["seed"] * 100 + k0).uniform(-1, 1, steps)
    b = ctx.rng("stepseq", p["seed"] * 100 + k0 + 1).uniform(-1, 1, steps)
    lv = (1 - fr) * a + fr * b
    x = grid()
    y = lv[np.minimum((x * steps).astype(int), steps - 1)]
    s = from_time(y)
    kk = np.arange(H + 1, dtype=np.float64)
    sig = p["slew"] / steps
    return s * np.exp(-0.5 * (2 * np.pi * kk * sig) ** 2)


def node_gaussformant(p, ctx, inp):
    return node_pd({"mode": "pulsar", "amount": p["m"], "width": p["width"]}, ctx, inp)


def node_mix(p, ctx, inputs):
    out = np.zeros(H + 1, dtype=np.complex128)
    for i, s in enumerate(inputs):
        g = p[f"g{i + 1}"]
        if p["match"] == "rms":
            r = spec_rms(s)
            s = s / r if r > 1e-12 else s
        out = out + g * s
    return out


EVAL = {"additive": node_additive, "formant": node_formant, "fm": node_fm, "pulse": node_pulse, "classic": node_classic, "pd": node_pd, "sync": node_sync, "shape": node_shape,
        "spec": node_spec, "ring": node_ring, "partials": node_partials, "noise": node_noise, "clicks": node_clicks, "burst": node_burst, "stepseq": node_stepseq,
        "gaussformant": node_gaussformant}


def eval_node(node, u, ctx):
    t = node["type"]
    p = {}
    for name, spec in NODES[t]["params"].items():
        v = node["params"].get(name, spec[1])
        v = law(v, u)
        if spec[0] == "num":
            v = _clamp(float(v), spec)
        p[name] = v
    if t == "mix":
        return node_mix(p, ctx, [eval_node(c, u, ctx) for c in node["inputs"]])
    inp = eval_node(node["input"], u, ctx) if "input" in node else None
    return EVAL[t](p, ctx, inp)


# ------------------------------------------------------------------------------------------------------------------------------ the normaliser and the table
def finish(spec, limit, smooth_on):
    s = spec.copy()
    s[0] = 0
    s[limit + 1:] = 0
    if smooth_on:
        s *= sigma_window(limit)
    return np.fft.irfft(s, N)


def table_frames(recipe, slug=None):
    """Recipe (clean) -> array (frames, 2048): band limited, DC free, levelled (post.level)."""
    seed = recipe.get("seed") or slug or recipe.get("title", "")
    n = recipe["frames"]
    post = {k: v[1] for k, v in POST.items()}
    post.update(recipe.get("post", {}))
    frames = []
    for i in range(n):
        u = i / (n - 1) if n > 1 else 0.0
        s = eval_node(recipe["source"], u, Ctx(seed, u))
        frames.append(finish(s, int(post["limit"]), post["smooth"] == "yes"))
    out = []
    for y in frames:
        if post["level"] == "none":
            out.append(y)
            continue
        pk = float(np.max(np.abs(y)))
        rms = float(np.sqrt(np.mean(y ** 2)))
        if pk < 1e-12:
            out.append(y)
        elif post["level"] == "peak":
            out.append(y * (post["peak"] / pk))
        else:
            g = post["rms"] / max(rms, 1e-12)
            if pk * g > post["peak"]:
                g = post["peak"] / pk
            out.append(y * g)
    return np.array(out)


def table_samples(recipe, slug):
    """The flat sample array of the table file (frames * 2048)."""
    src = recipe["source"]
    if src["type"] == "legacy":
        return legacy_table(src["params"]["fn"])(slug)
    return table_frames(recipe, slug).reshape(-1)


# ------------------------------------------------------------------------------------------------------------------------------ measures
def frame_spectra(samples):
    fr = np.asarray(samples, dtype=np.float64).reshape(-1, N)
    return np.fft.rfft(fr, axis=1) / (N / 2.0)       # amplitude per harmonic


def band_edges(bands=32, kmax=400):
    return np.unique(np.round(np.geomspace(1, kmax, bands + 1)).astype(int))


def envelope(samples, bands=32, kmax=400):
    """(frames, bands) dB of the energy in log spaced harmonic bands: the spectral envelope trajectory the score and the diversity measure compare."""
    spec = frame_spectra(samples)
    e = np.abs(spec) ** 2
    e = e / np.maximum(e[:, 1:].sum(axis=1, keepdims=True), 1e-18)       # relative to the frame's energy: a silent band floors at -60 dB, not at -90
    edges = np.geomspace(1, kmax + 1, bands + 1)
    out = np.zeros((len(spec), bands))
    ks = np.arange(e.shape[1])
    for b in range(bands):
        m = (ks >= edges[b]) & (ks < edges[b + 1])
        out[:, b] = np.sum(e[:, m], axis=1) if m.any() else 0.0
    return 10 * np.log10(out + 1e-6)


def measures(samples):
    """Per table numbers (the quality report and the lab's score)."""
    fr = np.asarray(samples, dtype=np.float64).reshape(-1, N)
    nfr = len(fr)
    rms = np.sqrt(np.mean(fr ** 2, axis=1))
    rms_db = 20 * np.log10(rms + 1e-12)
    spec = frame_spectra(fr)
    mag = np.abs(spec)
    tot = float(np.max(mag[:, 1:]))
    hi = 0
    for f in range(nfr):
        idx = np.nonzero(mag[f, 1:H] > tot * 1e-3)[0]
        if len(idx):
            hi = max(hi, int(idx[-1]) + 1)
    p = mag[:, 1:H] ** 2
    pn = p / np.maximum(p.sum(axis=1, keepdims=True), 1e-18)
    eff = np.exp(-np.sum(pn * np.log(pn + 1e-18), axis=1))      # effective number of harmonics (exp of the entropy)
    env = envelope(fr)
    d = np.sqrt(np.mean((env[1:] - env[:-1]) ** 2, axis=1)) if nfr > 1 else np.zeros(1)
    dw = np.sqrt(np.mean((fr[1:] - fr[:-1]) ** 2, axis=1)) / (0.5 * (rms[1:] + rms[:-1]) + 1e-12) if nfr > 1 else np.zeros(1)     # waveform step: RMS difference of neighbouring frames / their RMS
    alias = 10 * np.log10(np.max(np.sum(p[:, 383:], axis=1) / np.maximum(p.sum(axis=1), 1e-18)) + 1e-12)
    crest = 20 * np.log10(np.max(np.abs(fr), axis=1) / (rms + 1e-12))
    return {"frames": nfr, "peak": float(np.max(np.abs(fr))), "rms_min_db": float(rms_db.min()), "rms_max_db": float(rms_db.max()), "spread_db": float(rms_db.max() - rms_db.min()),
            "highest_harmonic": hi, "dc": float(np.max(np.abs(fr.mean(axis=1)))), "eff_mean": float(eff.mean()), "eff_min": float(eff.min()), "eff_max": float(eff.max()),
            "step_mean_db": float(d.mean()), "step_max_db": float(d.max()), "wave_step_mean": float(dw.mean()), "wave_step_max": float(dw.max()), "alias_db": float(alias), "crest_mean_db": float(crest.mean()), "crest_max_db": float(crest.max())}


def png_bytes(rgb):
    """PNG (8 bit RGB) from an array (h, w, 3) uint8: standard library only."""
    import struct
    import zlib
    h, w, _ = rgb.shape
    raw = b"".join(b"\0" + rgb[y].tobytes() for y in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
