#!/usr/bin/env python3
# SynthyBoy - factory content generator: wavetables, samples, instruments and impulse responses, synthesised from nothing (CC0-1.0, no third-party material).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   gen-content-pack.py [--out-root content] [--check] [--list] [--only <category>]...
#
# Writes two content packs (docs/ARCHITECTURE.md section 6) below the output root:
#   factory-wavetables/   pack com.safian.synthyboy.factory-wavetables, kind "wavetables": <Category>/<slug>.wav (72 tables in the six categories Analog, Digital, Spectral, Vocal, Metallic, Noise; 16 bit
#                 mono WAV, 2048 samples per frame, up to 64 frames: what core/engine-own/osc/content_set.cpp reads: a file of N * 2048 samples is a table of N frames). Every table is a RECIPE
#                 (content-src/wavetables/<Category>/<slug>.json, scripts/wavetable_lib.py); the manifest lists "category" and "title" per file. Add-on packs: content-src/addons/<dir>/ -> content-addons/<dir>/.
#   factory-samples/      pack com.safian.synthyboy.factory-samples, kind "samples": samples/*.wav (12 one-shots, loops and textures, 44.1 kHz, peak -1 dBFS, a 'smpl' chunk with the
#                 root note and the loop where there is one), instruments/<name>/<name>.sfz + instruments/<name>/samples/*.wav (3 key-mapped instruments for the Multisample
#                 type), impulses/*.wav (3 stereo impulse responses for Convolve, each under 1.5 s)
# plus a contentpack.json in each (the format of tools/contentpack/contentpack.py: SHA-256 and size of every file).
#
# Deterministic: every random number comes from numpy's PCG64 generator with a fixed seed per file, the files are written in a fixed order and nothing depends on the clock. The bytes are
# reproducible with the same numpy; --check writes nothing and exits 1 when the files below the output root differ from what this script makes (the tolerance of a different numpy
# release is one least-significant bit of the 16 bit samples, which --check reports as a difference: regenerate then). Only numpy and the standard library.
#
# How a preset refers to the files (scripts/batch2/*.py, core/include/synthyboy/content_state.h): content.wavetables / samples / impulses / instruments entries with the pack id, the
# file path inside the pack and the SHA-256 of the file. The MPC package (scripts/build-mpc.sh) installs the packs as Content/packs/<pack id>/ next to Content/presets, where
# UserContentStore finds them. A missing file is skipped on load: a wavetable falls back to the built-in saw, Sample / Granular / Spectral / Multisample oscillators and Convolve are
# silent / absent (docs/release/SOUND-SET-BATCH-2.md, section "Engine issues").

import argparse
import hashlib
import json
import math
import os
import struct
import sys

import numpy as np

SR = 44100
N = 2048                       # samples per wavetable frame
ROOT_NOTE = 60                 # the pitched samples are tuned to MIDI 60 (what the oscillator's `sample.root` default means), see tune_hz()
PEAK = 0.891                   # -1 dBFS: the headroom of the samples
PACKS = {
    "factory-wavetables": {"id": "com.safian.synthyboy.factory-wavetables", "name": "Factory Wavetables", "kind": "wavetables",
                   "version": "0.1.0", "description": "(set from the recipes by factory_wavetable_meta)"},
    "factory-samples": {"id": "com.safian.synthyboy.factory-samples", "name": "Factory Samples", "kind": "samples",
                "description": "12 synthesised samples, 3 key-mapped instruments and 3 impulse responses made by scripts/gen-content-pack.py under CC0-1.0; 44.1 kHz, 16 bit."},
}
VERSION = "0.1.0"


def rng_for(name):
    return np.random.default_rng(int.from_bytes(hashlib.sha256(name.encode()).digest()[:8], "little"))


# ----------------------------------------------------------------------------------------------------------------------------------------- WAV
def wav_bytes(data, rate=SR, smpl=None, bits=16):
    """16 (or 24) bit PCM WAV. `data`: float array (frames,) or (frames, channels) in -1 .. 1. `smpl`: (root note, loop start, loop end) in frames, or None."""
    a = np.asarray(data, dtype=np.float64)
    if a.ndim == 1:
        a = a[:, None]
    ch = a.shape[1]
    if bits == 24:
        q = np.clip(np.rint(a * 8388607.0), -8388608, 8388607).astype("<i4")
        payload = q.astype("<u4").view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    else:
        payload = np.clip(np.rint(a * 32767.0), -32768, 32767).astype("<i2").tobytes()
    fmt = struct.pack("<HHIIHH", 1, ch, rate, rate * ch * (bits // 8), ch * (bits // 8), bits)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(payload)) + payload
    if len(payload) % 2:
        chunks += b"\0"
    if smpl is not None:
        root, ls, le = smpl
        loops = 1 if ls >= 0 else 0
        body = struct.pack("<IIIIIIIII", 0, 0, int(1e9 / rate), root, 0, 0, 0, loops, 0)
        if loops:
            body += struct.pack("<IIIIII", 0, 0, ls, le, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(body)) + body
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def normalise(x, peak=PEAK):
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def seconds(t):
    return np.arange(int(round(t * SR))) / SR


def tune_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


# ------------------------------------------------------------------------------------------------------------------------------ small DSP tools
def fft_filter(x, gain_fn):
    """Zero phase filter of a real signal by its spectrum: gain_fn(frequencies in Hz) -> gain."""
    n = len(x)
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    return np.fft.irfft(spec * gain_fn(f), n)


def bandpass_gain(lo, hi, slope=2.0):
    def g(f):
        f = np.maximum(f, 1.0)
        return 1.0 / (1.0 + (lo / f) ** (2 * slope)) / (1.0 + (f / hi) ** (2 * slope))
    return g


def lowpass_gain(fc, slope=2.0):
    return lambda f: 1.0 / (1.0 + (np.maximum(f, 0) / fc) ** (2 * slope))


def highpass_gain(fc, slope=2.0):
    return lambda f: 1.0 / (1.0 + (fc / np.maximum(f, 1.0)) ** (2 * slope))


def noise(rng, n):
    return rng.standard_normal(n)


def one_pole_decay(n, tau):
    return np.exp(-np.arange(n) / (tau * SR))


def smooth_step(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


def formant_gain(f, formants):
    """Magnitude of a bank of 2nd order resonances (centre Hz, bandwidth Hz, gain dB) at the frequencies f: the shape of a vowel."""
    g = np.zeros_like(f)
    for fc, bw, gdb in formants:
        q = fc / bw
        g = g + (10 ** (gdb / 20.0)) / np.sqrt((1 - (f / fc) ** 2) ** 2 + (f / (q * fc)) ** 2)
    return g


VOWELS = {   # F1, F2, F3 of a male voice (Hz), bandwidths 80 / 90 / 120, the usual textbook values
    "a": (730, 1090, 2440), "e": (530, 1840, 2480), "i": (270, 2290, 3010), "o": (570, 840, 2410), "u": (300, 870, 2240),
}


def vowel_formants(v, shift=1.0):
    f1, f2, f3 = VOWELS[v]
    return [(f1 * shift, 80, 0.0), (f2 * shift, 90, -6.0), (f3 * shift, 120, -12.0), (3400 * shift, 200, -22.0)]


def vowel_mix(a, b, t):
    """Formants between two vowels (t 0 .. 1), a list for formant_gain."""
    fa, fb = vowel_formants(a), vowel_formants(b)
    return [(x[0] * (1 - t) + y[0] * t, x[1] * (1 - t) + y[1] * t, x[2] * (1 - t) + y[2] * t) for x, y in zip(fa, fb)]


# ----------------------------------------------------------------------------------------------------------------------------------- wavetables
def frame_from_harmonics(amps, phases=None):
    """One 2048 sample cycle from harmonic amplitudes amps[k-1] (k = 1 ..), phases in radians (sine phase), DC removed."""
    spec = np.zeros(N // 2 + 1, dtype=np.complex128)
    k = np.arange(1, len(amps) + 1)
    ph = np.zeros(len(amps)) if phases is None else np.asarray(phases)
    spec[k] = np.asarray(amps) * (N / 2.0) * np.exp(1j * (ph - np.pi / 2))
    return np.fft.irfft(spec, N)


def finish_frame(x, limit=600):
    """Band limit (no energy above harmonic `limit`), no DC, peak 0.9."""
    s = np.fft.rfft(x)
    s[0] = 0
    s[limit + 1:] = 0
    y = np.fft.irfft(s, N)
    m = np.max(np.abs(y))
    return y * (0.9 / m) if m > 1e-12 else y


def table_formant_sweep(name):
    frames = []
    seq = ["a", "e", "i", "o", "u"]
    n = 64
    for i in range(n):
        pos = i / (n - 1) * (len(seq) - 1)
        s = min(int(pos), len(seq) - 2)
        t = smooth_step(pos - s)
        fm = vowel_mix(seq[s], seq[s + 1], t)
        # the formants are in Hz for a voice at 220 Hz (A3): harmonic k stands at 220 k Hz (the cycle is the pitch the vowel was made for: the best range of the table is A2 .. A4)
        k = np.arange(1, 200)
        f = 220.0 * k
        amps = formant_gain(f, fm) / k ** 0.85
        frames.append(finish_frame(frame_from_harmonics(amps)))
    return np.concatenate(frames)


def table_choir_shift(name):
    """The same vowel A, O, U set through a male, a female and a child's vocal tract (formants x 1, x 1.18, x 1.4) and back: a choir whose singers change."""
    frames = []
    n = 48
    for i in range(n):
        u = i / (n - 1)
        shift = 1.0 + 0.4 * (1 - abs(2 * u - 1))
        v = ["a", "o", "u"][min(int(u * 3), 2)]
        k = np.arange(1, 400)
        amps = formant_gain(130.0 * k, vowel_formants(v, shift)) / k ** 0.95
        # a little second voice a hair off: odd harmonics only, 10 % level (the beat of a section)
        amps = amps * (1.0 + 0.1 * np.where(k % 2 == 1, 1.0, -1.0))
        frames.append(finish_frame(frame_from_harmonics(amps)))
    return np.concatenate(frames)


def table_overtone_throat(name):
    """Overtone singing: a narrow resonance that walks up the harmonic series from the 4th to the 20th partial and back, the drone under it fixed."""
    frames = []
    n = 64
    k = np.arange(1, 200)
    for i in range(n):
        c = 4 + 16 * (0.5 - 0.5 * math.cos(2 * math.pi * i / n))
        amps = 1.0 / k ** 1.3 + 3.2 * np.exp(-0.5 * ((k - c) / 0.55) ** 2) / np.sqrt(c)
        frames.append(finish_frame(frame_from_harmonics(amps)))
    return np.concatenate(frames)


def pulse_frame(width, limit=700):
    k = np.arange(1, limit + 1)
    amps = 2.0 * np.abs(np.sin(np.pi * k * width)) / (np.pi * k)
    ph = np.where(np.sin(np.pi * k * width) >= 0, 0.0, np.pi) + np.pi * k * width - np.pi / 2
    return frame_from_harmonics(amps, ph)


def table_pwm_stack(name):
    """Three pulse waves stacked, their widths walking apart: 50 % centre, the outer two from 50 % to 6 % and from 50 % to 94 % ... the sound of a PWM unison in one table."""
    frames = []
    n = 64
    for i in range(n):
        u = i / (n - 1)
        w1, w2, w3 = 0.5 - 0.44 * u, 0.5, 0.5 + 0.43 * u * 0.9
        x = pulse_frame(w1) + 0.8 * np.roll(pulse_frame(w2 * 0.6 + 0.2), 97) + pulse_frame(w3)
        frames.append(finish_frame(x))
    return np.concatenate(frames)


def table_pwm_saw_blend(name):
    """A pulse whose width sweeps 50 % -> 4 % while a saw fades in under it: the width-modulated saw of string machines."""
    frames = []
    n = 64
    k = np.arange(1, 700)
    for i in range(n):
        u = i / (n - 1)
        saw = frame_from_harmonics(1.0 / k, np.full(len(k), 0.0))
        x = (1 - 0.7 * u) * pulse_frame(0.5 - 0.46 * u) + 0.7 * u * saw
        frames.append(finish_frame(x))
    return np.concatenate(frames)


def table_pwm_dual_phase(name):
    """Two saws of opposite direction, the second one a variable distance behind: their sum is a pulse, their difference a thin comb. Frames 0 .. 63: the distance 8 % -> 50 % -> 8 %."""
    frames = []
    n = 64
    k = np.arange(1, 700)
    saw = 1.0 / k
    for i in range(n):
        d = 0.08 + 0.42 * (0.5 - 0.5 * math.cos(2 * math.pi * i / n))
        ph = -2 * np.pi * k * d
        a = frame_from_harmonics(saw, np.zeros(len(k)))
        b = frame_from_harmonics(saw, ph)
        frames.append(finish_frame(a - b + 0.15 * a))
    return np.concatenate(frames)


def table_bell_strike(name):
    """Additive bell: the partials of a struck bar, harmonic here (a cycle is harmonic): a hum, a prime, a tierce and a nominal-like cluster; the high ones fade as the frames go on."""
    frames = []
    n = 64
    partials = [(1, 1.0), (2, 0.8), (3, 0.62), (4, 0.7), (5, 0.35), (6, 0.5), (7, 0.2), (8, 0.28), (9, 0.16), (10, 0.22), (12, 0.14), (13, 0.09), (16, 0.12), (19, 0.07), (24, 0.06)]
    for i in range(n):
        u = i / (n - 1)
        amps = np.zeros(32)
        for h, a in partials:
            amps[h - 1] = a * math.exp(-u * (0.15 + 0.35 * h))
        amps[0] += 0.3 * u
        frames.append(finish_frame(frame_from_harmonics(amps, np.array([0.7 * h * h for h in range(1, 33)]))))
    return np.concatenate(frames)


def table_glass_harmonica(name):
    """Glass harmonica: a sparse set of partials that opens one by one (1, 3, 5, 7 ...) and closes again, each in its own phase so that the cycle keeps a soft shape."""
    frames = []
    n = 48
    for i in range(n):
        u = i / (n - 1)
        amps = np.zeros(40)
        for j, h in enumerate((1, 2, 3, 5, 7, 9, 11, 14, 17, 21)):
            on = smooth_step((u * 1.6 - j * 0.1) / 0.3) * (1.0 - 0.75 * smooth_step((u - 0.7) * 3 - j * 0.1))
            amps[h - 1] = on / h ** 0.7
        amps[0] = max(amps[0], 0.2)
        frames.append(finish_frame(frame_from_harmonics(amps, np.arange(1, 41) * 0.9)))
    return np.concatenate(frames)


def table_fm_index_sweep(name):
    """2:1 FM, the index from 0 to 9: sine -> brass -> metal."""
    frames = []
    n = 64
    t = np.arange(N) / N * 2 * np.pi
    for i in range(n):
        idx = 9.0 * (i / (n - 1)) ** 1.4
        frames.append(finish_frame(np.sin(t + idx * np.sin(2 * t))))
    return np.concatenate(frames)


def table_fm_ratio_ladder(name):
    """Modulator ratios 1, 2, 3, 4, 5, 6, 7, 8 (eight frames each, the index rising in each step 1 -> 5): the character jumps at every step and sweeps inside it."""
    frames = []
    t = np.arange(N) / N * 2 * np.pi
    for r in range(1, 9):
        for j in range(8):
            idx = 1.0 + 4.0 * j / 7
            frames.append(finish_frame(np.sin(t + idx * np.sin(r * t))))
    return np.concatenate(frames)


def table_fm_feedback(name):
    """One operator with feedback, beta 0 -> 1.9: sine -> saw -> noisy saw (the Yamaha way to a sawtooth)."""
    frames = []
    n = 64
    for i in range(n):
        beta = 1.9 * (i / (n - 1))
        y = 0.0
        out = np.zeros(N)
        for rep in range(4):   # the steady state: a few cycles of the recursion, the last one is stored
            for j in range(N):
                y = math.sin(2 * math.pi * j / N + beta * y)
                out[j] = y
        frames.append(finish_frame(out))
    return np.concatenate(frames)


def table_organ_drawbars(name):
    """Sixteen registrations of a tonewheel organ. The cycle holds the 16' fundamental: the drawbars 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' are the harmonics 1 3 2 4 6 8 10 12 16, so
    the written pitch sounds an octave above the table's: transpose the oscillator by +12."""
    regs = ["888000000", "888800000", "008800000", "800000008", "808000008", "006800000", "888888888", "000008880", "000800000",
            "008000800", "868000000", "588800000", "888600000", "008888000", "008080008", "800800000"]
    harm = (1, 3, 2, 4, 6, 8, 10, 12, 16)
    frames = []
    for reg in regs:
        amps = np.zeros(16)
        for d, h in zip(reg, harm):
            amps[h - 1] += (10 ** (-(8 - int(d)) * 3.0 / 20.0)) if int(d) > 0 else 0.0
        frames.append(finish_frame(frame_from_harmonics(amps)))
    out = []
    for i in range(len(frames) - 1):         # two interpolated frames between registrations: 16 -> 31 frames
        out += [frames[i], finish_frame(0.5 * frames[i] + 0.5 * frames[i + 1])]
    out.append(frames[-1])
    return np.concatenate(out)


def table_metal_comb(name):
    """A comb that sweeps through a saw's spectrum: the harmonics are multiplied by 0.5 + 0.5 cos(2 pi k / s) with the spacing s from 3 to 16 harmonics: ringing metal."""
    frames = []
    n = 64
    k = np.arange(1, 400)
    for i in range(n):
        s = 3.0 + 13.0 * i / (n - 1)
        amps = (1.0 / k ** 0.8) * (0.08 + 0.92 * (0.5 + 0.5 * np.cos(2 * np.pi * k / s)) ** 2)
        frames.append(finish_frame(frame_from_harmonics(amps, 0.37 * k * k)))
    return np.concatenate(frames)


def table_ring_ladder(name):
    """Ring modulation of two sines, the second at the integer ratios 2, 3 .. 17 (4 frames each, the depth rising inside a step)."""
    frames = []
    t = np.arange(N) / N * 2 * np.pi
    for r in range(2, 18):
        for j in range(4):
            d = 0.4 + 0.6 * j / 3
            frames.append(finish_frame((1 - d) * np.sin(t) + d * np.sin(t) * np.sin(r * t + 0.4)))
    return np.concatenate(frames)


def table_sine_fold(name):
    """A sine through a wave folder, the gain from 0.4 to 14: sine -> triangle -> dense folded metal."""
    frames = []
    n = 64
    t = np.arange(N) / N * 2 * np.pi
    for i in range(n):
        g = 0.4 + 13.6 * (i / (n - 1)) ** 1.6
        frames.append(finish_frame(np.sin(g * np.sin(t))))
    return np.concatenate(frames)


def table_harmonic_sieve(name):
    """A saw with only every n-th harmonic kept (n = 1 .. 16, two frames each, the others at 1 %): the octaves, the fifths, the ghost chords of a saw."""
    frames = []
    k = np.arange(1, 300)
    for step in range(1, 17):
        for j in range(2):
            keep = (k % step == 0)
            fade = 0.012 + (1 - 0.012) * keep
            if j == 1:
                fade = np.where(keep, 1.0, 0.012) * (0.8 + 0.2 * np.cos(k * 0.7))
            frames.append(finish_frame(frame_from_harmonics(fade / k ** 0.9)))
    return np.concatenate(frames)


def table_shape_collection(name):
    """Sixteen single cycles one after the other (a stepped table, read with the frame position): sine, triangle, saw down / up, square, pulse 25 %, pulse 12 %, half rectified sine, full rectified,
    staircase, sine squared, sine cubed, sine + 3rd, sine + 5th, parabola, bat wing."""
    t = np.arange(N) / N
    s = np.sin(2 * np.pi * t)
    shapes = [s, 2 * np.abs(2 * t - 1) - 1, 1 - 2 * t, 2 * t - 1, np.sign(s + 1e-9), np.where(t < 0.25, 1.0, -1.0), np.where(t < 0.125, 1.0, -1.0), np.maximum(s, 0) * 2 - 0.6,
              np.abs(s) * 2 - 1, np.floor(t * 8) / 3.5 - 1, s * np.abs(s), s ** 3, s + 0.5 * np.sin(6 * np.pi * t), s + 0.4 * np.sin(10 * np.pi * t + 1), 1 - 8 * (t - 0.5) ** 2 * 2, np.abs(np.sin(np.pi * t * 2)) ** 0.5 * np.sign(s)]
    # a band limit of 120 harmonics keeps the hard edges from aliasing at the top of the keyboard
    return np.concatenate([finish_frame(x, limit=120) for x in shapes])


LEGACY_TABLES = {   # the first 16 tables, hand written: their recipes (content-src/wavetables/*/*.json) are `legacy` nodes naming a function here, so the files stay byte for byte what they were
    "formant_sweep_aeiou": table_formant_sweep, "choir_singers_shift": table_choir_shift, "overtone_throat_sweep": table_overtone_throat,
    "pwm_triple_stack": table_pwm_stack, "pwm_saw_blend": table_pwm_saw_blend, "pwm_dual_phase": table_pwm_dual_phase,
    "additive_bell_strike": table_bell_strike, "additive_glass_harmonica": table_glass_harmonica,
    "fm_index_sweep": table_fm_index_sweep, "fm_ratio_ladder": table_fm_ratio_ladder, "fm_feedback_saw": table_fm_feedback,
    "organ_drawbars_16": table_organ_drawbars, "metal_comb_sweep": table_metal_comb, "ring_mod_ladder": table_ring_ladder,
    "sine_fold_sweep": table_sine_fold, "harmonic_sieve": table_harmonic_sieve,
}


# --------------------------------------------------------------------------------------------------------------------------------------- samples
def partials_signal(t, freqs, amps, taus, phases=None):
    """Sum of exponentially decaying sines: amplitude amps[i] * exp(-t / taus[i])."""
    y = np.zeros(len(t))
    for i, (f, a, tau) in enumerate(zip(freqs, amps, taus)):
        if f >= SR / 2 * 0.95:
            continue
        ph = 0.0 if phases is None else phases[i]
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * f * t + ph)
    return y


def make_pluck(name, note, length=2.5, bright=1.0, seed=None):
    f0 = tune_hz(note)
    t = seconds(length)
    rng = rng_for(seed or name)
    ks = np.arange(1, 60)
    pos = 0.17
    freqs = ks * f0 * np.sqrt(1 + 6e-5 * ks ** 2)
    amps = (1.0 / ks) * np.abs(np.sin(np.pi * ks * pos)) * np.exp(-(ks / (28.0 * bright)) ** 2)
    taus = (1.3 * (f0 / 130.0) ** -0.35) / (1.0 + 0.12 * (ks - 1) ** 1.25)
    y = partials_signal(t, freqs, amps, taus, rng.uniform(0, 2 * np.pi, len(ks)) * 0.15)
    pick = fft_filter(noise(rng, len(t)), bandpass_gain(1500, 6000)) * np.exp(-t / 0.006)
    y = y / np.max(np.abs(y)) + 0.12 * bright * pick / max(np.max(np.abs(pick)), 1e-9)
    y *= np.minimum(1.0, t / 0.0008)
    fade = np.minimum(1.0, (t[-1] - t) / 0.08)
    return normalise(y * fade)


def make_mallet(name, note, length=1.8, hard=1.0, seed=None):
    f0 = tune_hz(note)
    t = seconds(length)
    rng = rng_for(seed or name)
    ratios = [1.0, 3.99, 10.1, 17.9]
    amps = [1.0, 0.45 * hard, 0.22 * hard ** 1.5, 0.08 * hard ** 2]
    taus = [1.1 * (110.0 / f0) ** 0.3, 0.28, 0.09, 0.04]
    y = partials_signal(t, [f0 * r for r in ratios], amps, taus, [0, 0.5, 1.0, 0.2])
    knock = fft_filter(noise(rng, len(t)), bandpass_gain(300, 2500 + 2000 * hard)) * np.exp(-t / 0.012)
    y = y / np.max(np.abs(y)) + 0.16 * hard * knock / max(np.max(np.abs(knock)), 1e-9)
    y *= np.minimum(1.0, t / 0.0006)
    return normalise(y * np.minimum(1.0, (t[-1] - t) / 0.06))


def make_glass(name, note, length=3.0, seed=None):
    f0 = tune_hz(note)
    t = seconds(length)
    rng = rng_for(seed or name)
    ratios = [1.0, 2.32, 4.25, 6.63, 9.38, 12.6]
    amps = [1.0, 0.5, 0.33, 0.2, 0.12, 0.07]
    taus = [2.4, 1.3, 0.8, 0.5, 0.33, 0.2]
    y = partials_signal(t, [f0 * r for r in ratios], amps, taus, [0, 1.1, 2.0, 0.4, 1.7, 0.9])
    y += 0.55 * np.exp(-t / 2.1) * np.sin(2 * np.pi * (f0 + 0.45) * t + 0.6)          # the beating twin of the first partial
    y += 0.30 * np.exp(-t / 1.1) * np.sin(2 * np.pi * (f0 * 2.32 - 0.7) * t + 0.3)
    ping = fft_filter(noise(rng, len(t)), highpass_gain(4500)) * np.exp(-t / 0.004)
    y = y / np.max(np.abs(y)) + 0.09 * ping / max(np.max(np.abs(ping)), 1e-9)
    y *= np.minimum(1.0, t / 0.0005)
    return normalise(y * np.minimum(1.0, (t[-1] - t) / 0.1))


def make_breath():
    t = seconds(1.2)
    rng = rng_for("breath_noise_burst")
    n = noise(rng, len(t))
    out = np.zeros(len(t))
    # three overlapping bands: the air (4 kHz), the tube (1.4 kHz) and the hollow (500 Hz), the air moves up while the burst dies
    for lo, hi, a in ((2500, 6500, 0.5), (900, 2200, 1.0), (300, 800, 0.7)):
        out += a * fft_filter(n, bandpass_gain(lo, hi, 1.5))
    env = (1 - np.exp(-t / 0.045)) * np.exp(-t / 0.42)
    return normalise(out * env * np.minimum(1.0, (t[-1] - t) / 0.1))


LOOP_SECONDS = 2.0             # the sustained samples loop over exactly this length: every partial frequency is a multiple of 1 / 2 s, so the loop closes without a crossfade
LOOP_F0 = 261.5                # = 523 * 0.5 Hz, 0.83 cent under MIDI 60 (261.626 Hz)


def make_bowed():
    attack, rel = 0.45, 0.7
    L = LOOP_SECONDS
    t = seconds(attack + L + rel)
    n0 = int(round(attack * SR))
    nl = int(round(L * SR))
    ks = np.arange(1, 40)
    rng = rng_for("bowed_tone")
    vib = 5.0
    vdepth = 0.0035
    ph = 2 * np.pi * LOOP_F0 * t + vdepth * LOOP_F0 / vib * np.sin(2 * np.pi * vib * t)   # the phase of the fundamental: periodic in L (vib * L = 10 cycles, LOOP_F0 * L integer)
    y = np.zeros(len(t))
    bright = 0.35 + 0.65 * smooth_step(t / attack)
    for k in ks:
        a = 1.0 / k ** 1.05 * (1 + 0.25 * np.sin(k * 1.3))
        y += a * np.sin(k * ph + 0.2 * k * k) * np.where(k <= 4, 1.0, bright ** (k / 6.0))
    nz = fft_filter(noise(rng, nl), bandpass_gain(2500, 9000))   # circular: the tile closes
    nz = nz / np.max(np.abs(nz))
    tile = np.tile(nz, 3)[:len(t)]
    env = smooth_step(t / attack)
    end = n0 + nl
    env = np.where(np.arange(len(t)) >= end, np.exp(-(np.arange(len(t)) - end) / (0.22 * SR)), env)
    y = y / np.max(np.abs(y)) + 0.05 * tile
    return normalise(y * env), (ROOT_NOTE, n0, end - 1)


def make_vowel_pad():
    """Three voices a few cents apart, all with the vowel A -> O drifting (the loop closes: only the first 0.5 s sweep from A to O, the sustain holds the O/A mixture still)."""
    attack, rel = 0.9, 1.2
    L = LOOP_SECONDS
    t = seconds(attack + L + rel)
    n0 = int(round(attack * SR))
    nl = int(round(L * SR))
    end = n0 + nl
    rng = rng_for("vowel_pad")
    y = np.zeros(len(t))
    ks = np.arange(1, 70)
    bright = smooth_step(t / attack)
    mix_a = np.exp(-0.5 * ((ks[:, None] * LOOP_F0 - 800) / 250.0) ** 2)
    for v, (f0, vphase) in enumerate(((LOOP_F0, 0.0), (LOOP_F0 - 1.0, 1.7), (LOOP_F0 + 1.0, 3.1))):
        vib = 5.0
        ph = 2 * np.pi * f0 * t + 0.004 * f0 / vib * np.sin(2 * np.pi * vib * t + vphase)
        for ki, k in enumerate(ks):
            fk = k * f0
            if fk > 9000:
                break
            amp = float(formant_gain(np.array([fk]), vowel_formants("a" if v != 1 else "o"))[0]) / k ** 0.9
            y += amp * np.sin(k * ph + vphase * k) * (bright ** (1 + k / 12.0))
    breath = fft_filter(noise(rng, nl), bandpass_gain(800, 5000))
    breath = np.tile(breath / np.max(np.abs(breath)), 3)[:len(t)]
    y = y / np.max(np.abs(y)) + 0.05 * breath * bright
    env = smooth_step(t / attack)
    idx = np.arange(len(t))
    env = np.where(idx >= end, np.exp(-(idx - end) / (0.35 * SR)), env)
    return normalise(y * env), (ROOT_NOTE, n0, end - 1)


def make_vinyl():
    n = int(round(4.0 * SR))
    rng = rng_for("vinyl_noise_bed")
    out = np.zeros((n, 2))
    for ch in range(2):
        hiss = fft_filter(noise(rng, n), highpass_gain(2200, 1.5))
        hiss = hiss / np.std(hiss) * 0.05
        rumble = fft_filter(noise(rng, n), lowpass_gain(55, 2))
        rumble = rumble / np.std(rumble) * 0.08
        hum = 0.012 * np.sin(2 * np.pi * 50.0 * np.arange(n) / SR + ch)       # 50 Hz: 200 whole cycles in 4 s, the loop closes
        crackle = np.zeros(n)
        for _ in range(60):   # about 15 a second
            pos = int(rng.integers(0, n))
            amp = float(rng.exponential(0.25)) * (1 if rng.random() < 0.5 else -1)
            length = int(rng.integers(30, 140))
            tail = np.exp(-np.arange(length) / (length * 0.25)) * np.cos(np.arange(length) * rng.uniform(0.2, 1.2))
            idx = (pos + np.arange(length)) % n
            crackle[idx] += amp * tail
        for _ in range(5):    # pops
            pos = int(rng.integers(0, n))
            length = 900
            tail = np.exp(-np.arange(length) / 140.0) * np.sin(2 * np.pi * rng.uniform(80, 240) * np.arange(length) / SR)
            crackle[(pos + np.arange(length)) % n] += 0.55 * tail * (1 if rng.random() < 0.5 else -1)
        out[:, ch] = hiss + rumble + hum + crackle
    return normalise(out), (ROOT_NOTE, 0, n - 1)


def make_drums():
    """Two bars of 4/4 at 120 BPM (4.000 s exactly): kick, snare, closed and open hats, 16 steps a bar."""
    n = int(round(4.0 * SR))
    step = int(round(0.125 * SR))
    out = np.zeros(n)
    rng = rng_for("drum_loop_120")
    hat_noise = fft_filter(noise(rng, n), highpass_gain(7000, 2))
    snare_noise = fft_filter(noise(rng, n), bandpass_gain(1200, 7000, 1.5))

    def add(sig, pos, gain=1.0):
        idx = (pos + np.arange(len(sig))) % n         # tails wrap round: the loop closes
        out[idx] += gain * sig

    tk = np.arange(int(0.45 * SR)) / SR
    kick = np.sin(2 * np.pi * np.cumsum(46.0 + 120.0 * np.exp(-tk / 0.035)) / SR) * np.exp(-tk / 0.16) * np.minimum(1.0, tk / 0.0006)
    kick += 0.35 * fft_filter(noise(rng, len(tk)), lowpass_gain(3000)) * np.exp(-tk / 0.004)
    ts = np.arange(int(0.35 * SR)) / SR
    snare = 0.55 * np.sin(2 * np.pi * np.cumsum(180.0 + 70.0 * np.exp(-ts / 0.02)) / SR) * np.exp(-ts / 0.07)
    snare += 0.9 * snare_noise[:len(ts)] / np.std(snare_noise) * 0.35 * np.exp(-ts / 0.09)
    th = np.arange(int(0.05 * SR)) / SR
    hat = hat_noise[:len(th)] / np.std(hat_noise) * 0.25 * np.exp(-th / 0.012)
    to = np.arange(int(0.35 * SR)) / SR
    ohat = hat_noise[1000:1000 + len(to)] / np.std(hat_noise) * 0.22 * np.exp(-to / 0.1)
    kicks = [0, 6, 10, 16, 20, 24, 26, 30]
    snares = [4, 12, 20, 28]
    ghost = [15, 31]
    for s in kicks:
        add(kick, s * step, 1.0)
    for s in snares:
        add(snare, s * step, 1.0)
    for s in ghost:
        add(snare, s * step, 0.28)
    for s in range(0, 32, 2):
        add(hat, s * step, 1.0 if s % 4 == 0 else 0.6)
    for s in (14, 30):
        add(ohat, s * step, 1.0)
    return normalise(out), (ROOT_NOTE, 0, n - 1)


def make_rising_texture():
    t = seconds(6.0)
    n = len(t)
    rng = rng_for("rising_texture")
    u = t / t[-1]
    # a band of noise whose centre climbs from 250 Hz to 7 kHz (done in 24 overlapping slices of the spectrum, each moving through its own gain curve), a harmonic stack that rises two octaves
    spec = np.fft.rfft(noise(rng, n))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    out = np.zeros(n)
    slices = 24
    edges = 250.0 * (7000.0 / 250.0) ** (np.arange(slices + 2) / (slices + 1))
    for i in range(slices):
        band = np.fft.irfft(spec * bandpass_gain(edges[i], edges[i + 2], 2)(f), n)
        centre = (i + 0.5) / slices
        out += band * np.exp(-0.5 * ((u - centre) / 0.12) ** 2)
    out = out / np.std(out) * 0.5
    glide = 65.4 * 2 ** (2.0 * u)                      # C2 -> C4 (a glide of the fundamental)
    ph = 2 * np.pi * np.cumsum(glide) / SR
    tone = np.zeros(n)
    for k in range(1, 13):
        tone += np.sin(k * ph) / k ** 0.8 * smooth_step(u * 3 - 0.15 * k)
    tone = tone / np.max(np.abs(tone)) * 0.35
    air = fft_filter(noise(rng, n), highpass_gain(6000)) * u ** 3
    air = air / np.std(air) * 0.25
    y = (out + tone + air) * (smooth_step(u * 5) * (1 - smooth_step((u - 0.94) / 0.06)))
    return normalise(y)


def make_chord_pad():
    """Four chords of two seconds each (Cmaj7, Am7, Fmaj7, G6) crossfaded over half a second: scanning through the file is a chord progression."""
    t = seconds(8.0)
    n = len(t)
    chords = [(48, 52, 55, 59, 64), (45, 52, 55, 60, 64), (41, 48, 52, 57, 64), (43, 50, 55, 59, 64)]
    rng = rng_for("chord_pad")
    out = np.zeros(n)
    for ci, notes in enumerate(chords):
        centre = (ci + 0.5) * 2.0
        env = smooth_step((t - (centre - 1.25)) / 0.5) * (1 - smooth_step((t - (centre + 0.75)) / 0.5))
        sig = np.zeros(n)
        for ni, note in enumerate(notes):
            for det in (-0.07, 0.0, 0.07):
                f0 = tune_hz(note + det)
                ph0 = rng.uniform(0, 2 * np.pi)
                trem = 1 + 0.08 * np.sin(2 * np.pi * (0.3 + 0.07 * ni) * t + ph0)
                for k in range(1, 12):
                    sig += trem * np.sin(2 * np.pi * k * f0 * t + ph0 * k) / k ** 1.4
        out += sig * env
    out = fft_filter(out, lowpass_gain(7000, 1.5))
    return normalise(out * np.minimum(1.0, t / 0.05) * np.minimum(1.0, (t[-1] - t) / 0.05))


def make_gong():
    t = seconds(4.0)
    rng = rng_for("gong_metal")
    f0 = tune_hz(36)
    ratios = [1.0, 1.47, 2.09, 2.56, 2.99, 3.62, 4.10, 4.66, 5.42, 6.63, 7.51, 9.28, 11.7]
    amps = rng.uniform(0.4, 1.0, len(ratios)) * (0.85 ** np.arange(len(ratios)))
    taus = 2.4 / (1 + 0.25 * np.arange(len(ratios)))
    y = np.zeros(len(t))
    for r, a, tau in zip(ratios, amps, taus):
        for d in (-0.6, 0.6):          # every mode a close pair: the slow beating of a real gong
            y += 0.5 * a * np.exp(-t / tau) * np.sin(2 * np.pi * (f0 * r + d * r * 0.4) * t + rng.uniform(0, 6.28))
    sizzle = fft_filter(noise(rng, len(t)), bandpass_gain(2000, 9000)) * (1 - np.exp(-t / 0.35)) * np.exp(-t / 1.3)
    y = y / np.max(np.abs(y)) + 0.15 * sizzle / np.max(np.abs(sizzle))
    return normalise(y * np.minimum(1.0, t / 0.002) * np.minimum(1.0, (t[-1] - t) / 0.2))


def make_voice_sweep():
    """A male voice at 130.8 Hz singing a - e - i - o - u in five seconds (a little vibrato, breath), the formants gliding between the vowels."""
    t = seconds(5.0)
    n = len(t)
    rng = rng_for("voice_sweep_aeiou")
    u = t / t[-1]
    f0 = 130.8 * (1 + 0.004 * np.sin(2 * np.pi * 5.2 * t)) * (1 + 0.02 * np.exp(-t / 0.15))
    ph = 2 * np.pi * np.cumsum(f0) / SR
    seq = ["a", "e", "i", "o", "u"]
    # the vowel at each time, a smooth crossing between neighbours
    pos = np.clip(u * (len(seq) - 1), 0, len(seq) - 1.0001)
    idx = pos.astype(int)
    frac = smooth_step(pos - idx)
    y = np.zeros(n)
    block = 2048                      # the formant filter follows in blocks: the amplitude of every harmonic is looked up once per block and interpolated
    ks = np.arange(1, 60)
    prev = None
    amps_blocks = []
    centres = []
    for b0 in range(0, n, block):
        c = min(b0 + block // 2, n - 1)
        fm = vowel_mix(seq[idx[c]], seq[min(idx[c] + 1, len(seq) - 1)], float(frac[c]))
        fk = ks * float(f0[c])
        amps_blocks.append(formant_gain(fk, fm) / ks ** 0.9 * (fk < 9000))
        centres.append(c)
    amps_arr = np.array(amps_blocks)
    centres = np.array(centres)
    for ki, k in enumerate(ks):
        a = np.interp(np.arange(n), centres, amps_arr[:, ki])
        y += a * np.sin(k * ph)
    breath = fft_filter(noise(rng, n), bandpass_gain(1000, 6000))
    y = y / np.max(np.abs(y)) + 0.045 * breath / np.max(np.abs(breath))
    env = smooth_step(t / 0.08) * (1 - smooth_step((t - 4.6) / 0.4))
    return normalise(y * env)


SAMPLES = [   # file, function -> data or (data, smpl)
    ("pluck_string.wav", lambda: (make_pluck("pluck_string", 60), (ROOT_NOTE, -1, -1))),
    ("mallet_marimba.wav", lambda: (make_mallet("mallet_marimba", 60), (ROOT_NOTE, -1, -1))),
    ("glass_ping.wav", lambda: (make_glass("glass_ping", 72), (72, -1, -1))),
    ("breath_noise_burst.wav", lambda: (make_breath(), None)),
    ("bowed_tone.wav", make_bowed),
    ("vowel_pad.wav", make_vowel_pad),
    ("vinyl_noise_bed.wav", make_vinyl),
    ("drum_loop_120bpm.wav", make_drums),
    ("rising_texture.wav", lambda: (make_rising_texture(), None)),
    ("chord_pad_sections.wav", lambda: (make_chord_pad(), (ROOT_NOTE, -1, -1))),
    ("gong_metal.wav", lambda: (make_gong(), (36, -1, -1))),
    ("voice_sweep_aeiou.wav", lambda: (make_voice_sweep(), (48, -1, -1))),
]


# ------------------------------------------------------------------------------------------------------------------------------------ instruments
def instrument_pluck_nylon():
    zones = [(24, 40, 36), (41, 49, 45), (50, 58, 54), (59, 67, 63), (68, 76, 72), (77, 108, 81)]
    files, regions = {}, []
    for lo, hi, root in zones:
        fn = f"samples/pluck_{root}.wav"
        files[fn] = wav_bytes(make_pluck(f"pluck_nylon_{root}", root, length=1.5, bright=1.0 if root < 70 else 0.8), smpl=(root, -1, -1))
        regions.append(f"<region> sample={fn} lokey={lo} hikey={hi} pitch_keycenter={root}")
    head = "// SynthyBoy factory instrument: nylon string plucks, six zones, made by scripts/gen-content-pack.py (CC0-1.0)\n<control>\n<group> loop_mode=no_loop ampeg_release=0.35 ampeg_attack=0.0\n"
    return "pluck_nylon", head + "\n".join(regions) + "\n", files


def instrument_marimba():
    zones = [(24, 52, 48), (53, 61, 57), (62, 70, 66), (71, 79, 75), (80, 108, 84)]
    files, regions = {}, []
    for lo, hi, root in zones:
        for layer, (lv, hv, hard) in enumerate(((1, 84, 0.45), (85, 127, 1.0))):
            fn = f"samples/marimba_{root}_v{layer + 1}.wav"
            files[fn] = wav_bytes(make_mallet(f"marimba_{root}_{layer}", root, length=1.1, hard=hard), smpl=(root, -1, -1))
            regions.append(f"<region> sample={fn} lokey={lo} hikey={hi} lovel={lv} hivel={hv} pitch_keycenter={root}")
    head = "// SynthyBoy factory instrument: marimba bars, five zones in two velocity layers, made by scripts/gen-content-pack.py (CC0-1.0)\n<control>\n<group> loop_mode=no_loop ampeg_release=0.25 ampeg_attack=0.0\n"
    return "mallet_marimba", head + "\n".join(regions) + "\n", files


def instrument_glass_bells():
    zones = [(24, 53, 48), (54, 65, 60), (66, 77, 72), (78, 108, 84)]
    files, regions = {}, []
    for lo, hi, root in zones:
        fn = f"samples/glass_{root}.wav"
        files[fn] = wav_bytes(make_glass(f"glass_bell_{root}", root, length=2.4), smpl=(root, -1, -1))
        regions.append(f"<region> sample={fn} lokey={lo} hikey={hi} pitch_keycenter={root}")
    head = "// SynthyBoy factory instrument: struck glass, four zones, made by scripts/gen-content-pack.py (CC0-1.0)\n<control>\n<group> loop_mode=no_loop ampeg_release=0.6 ampeg_attack=0.0\n"
    return "glass_bells", head + "\n".join(regions) + "\n", files


INSTRUMENTS = [instrument_pluck_nylon, instrument_marimba, instrument_glass_bells]


# ------------------------------------------------------------------------------------------------------------------------------ impulse responses
def ir_stereo(name, length, rt60, early, lp_hz, hp_hz, density_ramp, seed_extra=""):
    """A noise tail with an exponential decay (RT60 in seconds), early reflections (list of (time s, gain)), a low pass that closes with time and a high pass; L2 norm 0.5."""
    n = int(round(length * SR))
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for ch in range(2):
        rng = rng_for(name + str(ch) + seed_extra)
        tail = noise(rng, n) * np.exp(-6.9078 * t / rt60) * smooth_step(t / density_ramp)
        # the damping: the tail's spectrum falls with time: three versions through a low pass at 1, 1/2, 1/4 of lp_hz, cross faded by the time
        a = fft_filter(tail, lowpass_gain(lp_hz, 2))
        b = fft_filter(tail, lowpass_gain(lp_hz * 0.45, 2))
        c = fft_filter(tail, lowpass_gain(lp_hz * 0.2, 2))
        w = np.clip(t / (rt60 * 0.7), 0, 1)
        tail = a * (1 - w) ** 2 + b * 2 * w * (1 - w) + c * w ** 2
        tail = fft_filter(tail, highpass_gain(hp_hz, 2))
        for (te, ge) in early:
            idx = int((te + (0.0007 if ch else 0.0)) * SR)
            if idx < n:
                out[idx, ch] += ge * (1 if (idx + ch) % 3 else -1)
        out[:, ch] += tail
    out[0, :] += 0.0
    return out


def make_ir_room():
    early = [(0.0, 0.6), (0.0071, 0.42), (0.0113, -0.35), (0.0167, 0.3), (0.0229, -0.25), (0.0291, 0.22), (0.0372, -0.17), (0.0463, 0.13)]
    y = ir_stereo("ir_small_room", 0.7, 0.42, early, 5200, 90, 0.02)
    y[:, :] = y / np.sqrt((y ** 2).sum() / 2)
    return y


def make_ir_plate():
    y = ir_stereo("ir_plate", 1.45, 1.25, [(0.0, 0.2)], 9000, 160, 0.004)
    # a plate sounds bright and dense from the first millisecond: an extra high band that lasts a short while
    rng = rng_for("ir_plate_sheen")
    n = y.shape[0]
    t = np.arange(n) / SR
    for ch in range(2):
        sheen = fft_filter(noise(rng, n), highpass_gain(5000)) * np.exp(-t / 0.35)
        y[:, ch] += 0.5 * sheen / max(np.std(sheen), 1e-9) * np.std(y[:, ch][:SR // 4])
    y = y / np.sqrt((y ** 2).sum() / 2)
    return y


def make_ir_cabinet():
    """A guitar cabinet colour: 80 Hz open back resonance, the presence hump at 2.5 kHz, a low pass at 5 kHz, 90 ms long, the two channels two different 'microphone' positions."""
    n = int(round(0.09 * SR))
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for ch, (fc, q, hump) in enumerate(((2500.0, 1.4, 1.0), (3300.0, 1.2, 0.8))):
        rng = rng_for("ir_cabinet" + str(ch))
        x = np.zeros(n)
        x[0] = 1.0
        spec = np.fft.rfft(x)
        f = np.fft.rfftfreq(n, 1.0 / SR)
        h = (1.0 / np.sqrt(1 + ((np.maximum(f, 1.0) / 70.0) ** -4))) / np.sqrt(1 + (f / 5200.0) ** 8)   # the open back's roll off at the bottom and the speaker's top
        h = h * (1 + hump * formant_gain(f, [(fc, fc / q, 0.0)]) * 0.9) * (1 + 1.2 * formant_gain(f, [(95.0, 40.0, 0.0)]) * 0.4)
        y = np.fft.irfft(spec * h, n)
        y = y * np.exp(-t / 0.022) + 0.012 * noise(rng, n) * np.exp(-t / 0.02)
        out[:, ch] = y
    out = out / np.sqrt((out ** 2).sum() / 2) * 1.0
    return out


IMPULSES = [("small_room.wav", make_ir_room), ("plate.wav", make_ir_plate), ("cabinet_colour.wav", make_ir_cabinet)]


# ------------------------------------------------------------------------------------------------------------------------------------- the packs
# A wavetable is a RECIPE (JSON, format "synthyboy-wavetable-recipe/1", scripts/wavetable_lib.py): content-src/wavetables/<Category>/<slug>.json for the factory pack, and
# content-src/addons/<dir>/pack.json + <Category>/<slug>.json for every add-on pack (written to content-addons/<dir>/ with its own contentpack.json). The file of a recipe is
# <Category>/<slug>.wav in its pack; the manifest lists "category" and "title" of every wavetable. Adding a table = adding a recipe file. The category folders are the six of wavetable_lib.CATEGORIES.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wavetable_lib as wl  # noqa: E402

RECIPES_ROOT = os.path.join(HERE, "..", "content-src", "wavetables")
ADDON_RECIPES = os.path.join(HERE, "..", "content-src", "addons")


def load_recipes(root):
    """[{category, slug, recipe}] of a recipe folder (<Category>/<slug>.json), recipes validated; a problem stops the script with the list of errors."""
    out, bad = [], []
    if not os.path.isdir(root):
        return out
    for cat in sorted(os.listdir(root)):
        d = os.path.join(root, cat)
        if not os.path.isdir(d):
            continue
        if cat not in wl.CATEGORIES:
            bad.append(f"{d}: not a category folder ({', '.join(wl.CATEGORIES)})")
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".json"):
                continue
            with open(os.path.join(d, fn), "r", encoding="utf-8") as f:
                r, errors, _warnings = wl.clean_recipe(json.load(f))
            if r is not None and r.get("category", cat) != cat:
                errors.append(f"category {r.get('category')!r} differs from the folder {cat}")
            if errors:
                bad += [f"{cat}/{fn}: {e}" for e in errors]
                continue
            out.append({"category": cat, "slug": fn[:-5], "recipe": r})
    if bad:
        sys.exit("recipe errors:\n  " + "\n  ".join(bad))
    return out


def render_entry(e):
    data = wl.table_samples(e["recipe"], e["slug"])
    assert len(data) % N == 0 and 1 <= len(data) // N <= 64, e["slug"]
    return wav_bytes(data * 1.0, smpl=None)


def wavetable_files(entries, only, disk_dir):
    """({path: bytes}, {path: (category, title)}, selected paths). Categories outside `only` are read from disk (for the manifest), not rendered."""
    files, meta, selected = {}, {}, set()
    for e in entries:
        path = f"{e['category']}/{e['slug']}.wav"
        meta[path] = (e["category"], e["recipe"]["title"])
        if only and e["category"] not in only:
            with open(os.path.join(disk_dir, path), "rb") as f:
                files[path] = f.read()
        else:
            files[path] = render_entry(e)
            selected.add(path)
    return files, meta, selected


def samples_files():
    sm = {}
    for fname, fn in SAMPLES:
        r = fn()
        data, smpl = r if isinstance(r, tuple) else (r, None)
        sm[f"samples/{fname}"] = wav_bytes(data, smpl=smpl)
    for make in INSTRUMENTS:
        name, sfz, files = make()
        sm[f"instruments/{name}/{name}.sfz"] = sfz.encode("utf-8")
        for rel, b in files.items():
            sm[f"instruments/{name}/{rel}"] = b
    for fname, fn in IMPULSES:
        sm[f"impulses/{fname}"] = wav_bytes(fn(), bits=24)
    return sm


def manifest_text(meta, files, extra=None):
    entries = []
    for p in sorted(files):
        e = {"path": p}
        if extra and p in extra:
            e["category"], e["title"] = extra[p]
        e["sha256"] = hashlib.sha256(files[p]).hexdigest()
        e["size"] = len(files[p])
        entries.append(e)
    doc = {"schemaVersion": 1, "id": meta["id"], "name": meta["name"], "version": meta.get("version", VERSION), "license": "CC0-1.0", "attribution": "Safian Szabolcs", "kind": meta["kind"],
           "requires_plugin": ">=0.1.0", "description": meta["description"], "files": entries}
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"   # byte for byte what tools/contentpack/contentpack.py manifest writes


def factory_wavetable_meta(entries):
    cats = sorted({e["category"] for e in entries}, key=wl.CATEGORIES.index)
    m = dict(PACKS["factory-wavetables"])
    m["description"] = (f"{len(entries)} wavetables in {len(cats)} categories ({', '.join(cats)}) made by scripts/gen-content-pack.py from the recipes in content-src/wavetables under CC0-1.0; "
                        "2048 samples per frame, up to 64 frames.")
    return m


def addon_packs():
    """[(dir name, meta, recipe entries)] of content-src/addons/*/pack.json."""
    out = []
    if os.path.isdir(ADDON_RECIPES):
        for d in sorted(os.listdir(ADDON_RECIPES)):
            pj = os.path.join(ADDON_RECIPES, d, "pack.json")
            if os.path.isfile(pj):
                with open(pj, "r", encoding="utf-8") as f:
                    meta = json.load(f)
                meta = {"id": meta["id"], "name": meta["name"], "kind": "wavetables", "version": meta.get("version", "0.1.0"), "description": meta.get("description", "")}
                out.append((d, meta, load_recipes(os.path.join(ADDON_RECIPES, d))))
    return out


def print_catalogue(entries, files):
    print(f"{'category':9s} {'file':32s} {'frames':>6s} {'bytes':>8s}  title")
    for e in entries:
        path = f"{e['category']}/{e['slug']}.wav"
        n = len(files[path])
        print(f"{e['category']:9s} {e['slug']:32s} {(n - 44) // 4096:6d} {n:8d}  {e['recipe']['title']}")
    print(f"{len(entries)} wavetables, {sum(len(b) for b in files.values())} bytes")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-root", default=os.path.join(HERE, "..", "content"))
    ap.add_argument("--addons-root", default=os.path.join(HERE, "..", "content-addons"))
    ap.add_argument("--check", action="store_true", help="write nothing; exit 1 when a file differs from what the recipes make")
    ap.add_argument("--list", action="store_true", help="print the wavetable catalogue (category, file, frames, bytes, title)")
    ap.add_argument("--addon", action="append", help="process only this add-on pack (a folder name below content-src/addons); the factory packs are left alone (repeatable)")
    ap.add_argument("--only", action="append", choices=wl.CATEGORIES, help="regenerate (or check) only this wavetable category; samples are left alone (repeatable)")
    a = ap.parse_args()
    root = os.path.abspath(a.out_root)
    entries = load_recipes(RECIPES_ROOT)
    targets = []      # (directory, {relative path: bytes}, selected paths or None)
    wt_dir = os.path.join(root, "factory-wavetables")
    files, meta, selected = wavetable_files(entries, a.only, wt_dir) if not a.addon else ({}, {}, set())
    if a.list:
        print_catalogue(entries, files)
        for d, m, ents in addon_packs():
            f2, _, _ = wavetable_files(ents, None, "")
            print(f"\nadd-on pack {m['id']} ({d})")
            print_catalogue(ents, f2)
        return 0
    sel = selected | {"contentpack.json"}
    pack_meta = factory_wavetable_meta(entries)
    if not a.addon:
        targets.append((wt_dir, dict(files, **{"contentpack.json": manifest_text(pack_meta, files, meta).encode("utf-8")}), sel if a.only else None))
    if not a.only:
        if not a.addon:
            sm = samples_files()
            targets.append((os.path.join(root, "factory-samples"), dict(sm, **{"contentpack.json": manifest_text(PACKS["factory-samples"], sm).encode("utf-8")}), None))
        for d, m, ents in addon_packs():
            if a.addon and d not in a.addon:
                continue
            f2, m2, _ = wavetable_files(ents, None, "")
            targets.append((os.path.join(os.path.abspath(a.addons_root), d), dict(f2, **{"contentpack.json": manifest_text(m, f2, m2).encode("utf-8")}), None))
    total = sum(len(b) for _, fs, _ in targets for b in fs.values())
    nfiles = sum(len(fs) for _, fs, _ in targets)
    if a.check:
        bad = []
        for d, fs, selpaths in targets:
            for rel, b in fs.items():
                if selpaths is not None and rel not in selpaths:
                    continue
                path = os.path.join(d, rel)
                if not os.path.isfile(path) or open(path, "rb").read() != b:
                    bad.append(os.path.relpath(path, os.path.dirname(root)))
            if selpaths is None and os.path.isdir(d):
                for dp, _, fns in os.walk(d):
                    for fn in fns:
                        rel = os.path.relpath(os.path.join(dp, fn), d).replace(os.sep, "/")
                        if rel not in fs and fn != ".DS_Store":
                            bad.append(os.path.relpath(os.path.join(d, rel), os.path.dirname(root)) + " (not generated)")
        if bad:
            print("content differs from the generator:", *sorted(bad), sep="\n  ")
            return 1
        print(f"content pack up to date ({nfiles} files, {total} bytes)")
        return 0
    for d, fs, selpaths in targets:
        for rel, b in fs.items():
            if selpaths is not None and rel not in selpaths:
                continue
            path = os.path.join(d, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(b)
    if not a.only:     # a table that left the recipes leaves the folder: stale wavetable files go
        for d, fs, selpaths in targets:
            if os.path.isdir(d):
                for dp, _, fns in os.walk(d):
                    for fn in fns:
                        rel = os.path.relpath(os.path.join(dp, fn), d).replace(os.sep, "/")
                        if rel not in fs and fn != ".DS_Store":
                            os.remove(os.path.join(d, rel))
    print(f"wrote {nfiles} files, {total} bytes below {root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
