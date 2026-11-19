#!/usr/bin/env python3
# SynthyBoy - sound design measurements of a preset (numpy / scipy; renders through build/tools/sound-lab/synthyboy_sound_lab).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
#   sound-lab.py report PRESET.sbpreset... [--profile xl|low|both] [--bin PATH] [--work DIR] [--quick]
#
# For every preset and profile it renders: single notes (MIDI 36 / 48 / 60 / 72 / 84), velocity 30 / 80 / 127, the four note chord of the level measure (what scripts/tune-preset-levels.py
# uses), an eight note chord at velocity 127, every macro at 0 and at 100, a held note with a pressure ramp, a held note with the mod wheel, and a long held note, and prints per preset:
#   level      RMS of the loudest 300 ms of the 4 note chord (dBFS, the set's reference is -20), peak of the 8 note chord at velocity 127 raw and after the make-up gain of the shells (OUTPUT_GAIN_DB = synthyboy::kMakeupGainDb, before their limiter)
#   stereo     correlation, level of the mono sum against the stereo level, the worst 1/3 octave band of the mono sum
#   dc / click mean of the hold, the start and the end of a note
#   movement   spread of the spectral centroid (octaves) and of the level (dB) over a held note; the depth of a periodic level movement
#   response   velocity: level and centroid change from 30 to 127; per macro: spectral distance of 0 -> 100; pressure, mod wheel: the same
# The raw engine output is what is measured (the shells add their output stage after it: OUTPUT_GAIN_DB of make-up gain and the look-ahead limiter, core/include/synthyboy/output_stage.h).

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, hilbert, sosfiltfilt, stft

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BIN = ROOT / "build-sound" / "tools" / "sound-lab" / "synthyboy_sound_lab"
SR = 44100
PROFILES = {"xl": "high;voices=16;control=economy", "low": "low"}
OUTPUT_GAIN_DB = 9.0


def db(x, floor=-200.0):
    return 20.0 * math.log10(x) if x > 1e-10 else floor


def pdb(x, floor=-200.0):
    return 10.0 * math.log10(x) if x > 1e-20 else floor


def content_store_dir(work):
    """The folder the lab binary reads the files of a preset's content from ($SYNTHYBOY_CONTENT_DIR/<pack id>/<file>): links to the factory packs of content/ named by their pack ids."""
    root = Path(work) / "content-store"
    root.mkdir(parents=True, exist_ok=True)
    for pack in sorted((ROOT / "content").glob("factory-*")):
        manifest = pack / "contentpack.json"
        if manifest.is_file():
            link = root / json.loads(manifest.read_text())["id"]
            if not link.exists():
                link.symlink_to(pack.resolve())
    return root


class Lab:
    def __init__(self, binary=DEFAULT_BIN, work=None):
        self.bin = str(binary)
        self.work = Path(work or tempfile.mkdtemp(prefix="sblab-"))
        self.work.mkdir(parents=True, exist_ok=True)
        self.jobs = []
        os.environ.setdefault("SYNTHYBOY_CONTENT_DIR", str(content_store_dir(self.work)))

    def job(self, tag, preset, profile, seconds, *items):
        out = self.work / f"{tag}.wav"
        self.jobs.append(f"render {out} {preset} {PROFILES.get(profile, profile)} {seconds} " + " ".join(str(i) for i in items))
        return out

    def run(self):
        if not self.jobs:
            return
        jf = self.work / "jobs.txt"
        jf.write_text("\n".join(self.jobs) + "\n")
        r = subprocess.run([self.bin, str(jf)], capture_output=True, text=True)
        self.jobs = []
        if r.returncode != 0:
            raise RuntimeError(r.stderr)

    @staticmethod
    def load(path):
        sr, d = wavfile.read(str(path))
        assert sr == SR
        return d[:, 0].astype(np.float64), d[:, 1].astype(np.float64)


# ---------------------------------------------------------------------------------------------------------------------------------- measures
def loudest_rms(l, r, i0, i1, win=0.3):
    p = 0.5 * (l[i0:i1] ** 2 + r[i0:i1] ** 2)
    w = int(win * SR)
    if len(p) < w:
        return pdb(p.mean()) if len(p) else -200.0
    c = np.concatenate([[0], np.cumsum(p)])
    m = (c[w:] - c[:-w]) / w
    return pdb(m.max())


def spectrum_frames(x, n=4096, hop=2048):
    f, t, z = stft(x, fs=SR, nperseg=n, noverlap=n - hop, window="hann", boundary=None, padded=False)
    return f, t, np.abs(z) ** 2


CENTROID_LO = [60.0]   # the lower edge of the centroid band; run_report raises it for basses so that the sub does not decide the centroid


def centroid_track(l, r, i0, i1, lo=None, hi=16000.0):
    lo = CENTROID_LO[0] if lo is None else lo
    x = 0.5 * (l[i0:i1] + r[i0:i1])
    f, t, p = spectrum_frames(x)
    sel = (f >= lo) & (f <= hi)
    pp = p[sel]
    fs = f[sel]
    tot = pp.sum(axis=0)
    cen = (pp * fs[:, None]).sum(axis=0) / np.maximum(tot, 1e-30)
    lev = 10 * np.log10(np.maximum(tot, 1e-30))
    return t + i0 / SR, cen, lev


def band_edges():
    # 1/3 octave centres 100 Hz .. 12.5 kHz
    c = 31.5 * 2 ** (np.arange(0, 30) / 3.0)
    return c[c < 12500]


def band_power(l, r, i0, i1, mid=False):
    x = 0.5 * (l[i0:i1] + r[i0:i1]) if mid else None
    f, _, pl = spectrum_frames(l[i0:i1], 16384, 8192)
    _, _, pr = spectrum_frames(r[i0:i1], 16384, 8192)
    if mid:
        _, _, pm = spectrum_frames(x, 16384, 8192)
    cs = band_edges()
    out_s, out_m = [], []
    for c in cs:
        sel = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out_s.append(0.5 * (pl[sel].sum() + pr[sel].sum()))
        out_m.append(pm[sel].sum() if mid else 0.0)
    return cs, np.array(out_s), np.array(out_m)


def stereo_metrics(l, r, i0, i1):
    pl, pr = (l[i0:i1] ** 2).mean(), (r[i0:i1] ** 2).mean()
    cor = float((l[i0:i1] * r[i0:i1]).mean() / max(math.sqrt(pl * pr), 1e-20))
    m = 0.5 * (l[i0:i1] + r[i0:i1])
    mono_db = pdb((m ** 2).mean()) - pdb(0.5 * (pl + pr))
    cs, ps, pm = band_power(l, r, i0, i1, True)
    keep = ps > ps.max() * 1e-4
    rel = 10 * np.log10(np.maximum(pm, 1e-30) / np.maximum(ps, 1e-30))
    k = int(np.argmin(np.where(keep, rel, 99)))
    return cor, mono_db, float(rel[k]), float(cs[k])


def click_start(x, onset, ms=3.0):
    """Largest first difference in the first `ms` after the onset against the largest one in the 100 ms after that (>= 3 looks like a click unless the attack is a wall)."""
    d = np.abs(np.diff(x))
    a = d[onset:onset + int(ms * SR / 1000)]
    b = d[onset + int(ms * SR / 1000):onset + int(0.1 * SR)]
    if len(a) == 0 or len(b) == 0:
        return 0.0
    return float(a.max() / max(b.max(), 1e-9))


def tail_end_db(l, r, off):
    """Level (dBFS peak of the last 1 ms) where the output falls under -90 dBFS after the note off; a cut shows as a high value."""
    x = np.maximum(np.abs(l[off:]), np.abs(r[off:]))
    idx = np.where(x > 10 ** (-90 / 20))[0]
    if len(idx) == 0:
        return -200.0, 0.0
    last = idx[-1]
    seg = x[max(0, last - 44):last + 1]
    return db(seg.max()), last / SR


def level_modulation(l, r, i0, i1):
    """Periodic movement of the level in a hold: (frequency Hz of the strongest peak of the envelope spectrum between 0.3 and 30 Hz, its depth in dB peak to peak)."""
    x = 0.5 * (np.abs(l[i0:i1]) + np.abs(r[i0:i1]))
    k = int(0.005 * SR)
    env = np.convolve(x, np.ones(k) / k, mode="valid")[::k // 2 or 1]
    if len(env) < 64:
        return 0.0, 0.0
    e = 20 * np.log10(np.maximum(env, 1e-9))
    e = e - e.mean()
    rate = SR / (k // 2 or 1)
    sp = np.abs(np.fft.rfft(e * np.hanning(len(e))))
    fr = np.fft.rfftfreq(len(e), 1 / rate)
    sel = (fr > 0.3) & (fr < 30)
    if not sel.any():
        return 0.0, 0.0
    i = np.argmax(np.where(sel, sp, 0))
    return float(fr[i]), float(np.percentile(e, 95) - np.percentile(e, 5))


def fluctuation(l, r, i0, i1):
    """How much the sound beats / moves inside a hold: (std of the 10 ms RMS in dB, dominant rate Hz of the 5 .. 40 ms centroid track between 0.5 and 20 Hz or 0)."""
    x = 0.5 * (l[i0:i1] + r[i0:i1])
    k = int(0.010 * SR)
    n = len(x) // k
    if n < 30:
        return 0.0, 0.0
    e = 10 * np.log10(np.maximum((x[:n * k].reshape(n, k) ** 2).mean(axis=1), 1e-14))
    f, t, p = spectrum_frames(x, 1024, 512)
    sel = (f > 60) & (f < 12000)
    cen = (p[sel] * f[sel][:, None]).sum(axis=0) / np.maximum(p[sel].sum(axis=0), 1e-20)
    c = np.log2(np.maximum(cen, 1))
    c = c - c.mean()
    rate = SR / 512
    sp = np.abs(np.fft.rfft(c * np.hanning(len(c))))
    fr = np.fft.rfftfreq(len(c), 1 / rate)
    ok = (fr > 0.5) & (fr < 20)
    pk = float(fr[np.argmax(np.where(ok, sp, 0))]) if ok.any() and sp[ok].max() > 0 else 0.0
    return float(e.std()), pk


def pitch_modulation(l, r, i0, i1, midi):
    """Vibrato of a held note: (rate Hz of the strongest 0.5 .. 12 Hz component of the fundamental's instantaneous frequency, its depth as the std in cents)."""
    f0 = 440.0 * 2 ** ((midi - 69) / 12)
    x = 0.5 * (l[i0:i1] + r[i0:i1])
    sos = butter(4, [f0 / 1.35, f0 * 1.35], btype="band", fs=SR, output="sos")
    y = sosfiltfilt(sos, x)
    ph = np.unwrap(np.angle(hilbert(y)))
    inst = np.diff(ph) * SR / (2 * np.pi)
    inst = inst[int(0.05 * SR):-int(0.05 * SR)]
    if len(inst) < SR // 2 or not np.isfinite(inst).all():
        return 0.0, 0.0
    cents = 1200 * np.log2(np.maximum(inst, 1.0) / f0)
    cents = cents - np.median(cents)
    cents = np.clip(cents, -400, 400)
    sm = np.convolve(cents, np.ones(220) / 220, mode="same")
    sp = np.abs(np.fft.rfft(sm * np.hanning(len(sm))))
    fr = np.fft.rfftfreq(len(sm), 1 / SR)
    ok = (fr > 0.5) & (fr < 12)
    return float(fr[np.argmax(np.where(ok, sp, 0))]), float(sm.std())


def spectral_distance(a, b, i0, i1):
    """Mean absolute difference in dB of the 1/3 octave band levels of two renders (bands within 40 dB of the loudest band of either), and the centroid shift in octaves."""
    ca, pa, _ = band_power(a[0], a[1], i0, i1)
    cb, pb, _ = band_power(b[0], b[1], i0, i1)
    top = max(pa.max(), pb.max())
    keep = (pa > top * 1e-4) | (pb > top * 1e-4)
    da = 10 * np.log10(np.maximum(pa, top * 1e-9))
    dbb = 10 * np.log10(np.maximum(pb, top * 1e-9))
    dist = float(np.abs(da - dbb)[keep].mean()) if keep.any() else 0.0
    lev = pdb(pb.sum()) - pdb(pa.sum())
    _, ce_a, _ = centroid_track(a[0], a[1], i0, i1)
    _, ce_b, _ = centroid_track(b[0], b[1], i0, i1)
    shift = math.log2(max(np.median(ce_b), 1) / max(np.median(ce_a), 1))
    side = pdb((((b[0][i0:i1] - b[1][i0:i1]) / 2) ** 2).mean()) - pdb((((a[0][i0:i1] - a[1][i0:i1]) / 2) ** 2).mean())
    fa, fb = fluctuation(a[0], a[1], i0, i1), fluctuation(b[0], b[1], i0, i1)
    hf = pdb(pb[cb >= 6000].sum()) - pdb(pa[ca >= 6000].sum())
    lf = pdb(pb[cb < 200].sum()) - pdb(pa[ca < 200].sum())
    # the fine structure: the mean difference in dB of the narrow band (5 Hz) spectra between 100 Hz and 6 kHz, where partials move (a shifter, a comb, a morph) without the 1/3 octave bands showing it
    xa, xb = 0.5 * (a[0][i0:i1] + a[1][i0:i1]), 0.5 * (b[0][i0:i1] + b[1][i0:i1])
    fr = np.fft.rfftfreq(8192, 1 / SR)
    pa_ = np.abs(np.fft.rfft(xa[:len(xa) // 8192 * 8192].reshape(-1, 8192) * np.hanning(8192), axis=1)).mean(axis=0) if len(xa) >= 8192 else np.ones(len(fr))
    pb_ = np.abs(np.fft.rfft(xb[:len(xb) // 8192 * 8192].reshape(-1, 8192) * np.hanning(8192), axis=1)).mean(axis=0) if len(xb) >= 8192 else np.ones(len(fr))
    la, lb = 20 * np.log10(np.maximum(pa_, 1e-9)), 20 * np.log10(np.maximum(pb_, 1e-9))
    sel = (fr > 100) & (fr < 6000) & (np.maximum(la, lb) > max(la.max(), lb.max()) - 50)
    fine = float(np.abs(la - lb)[sel].mean()) if sel.any() else 0.0
    return dist, lev, shift, side, fb[0] - fa[0], hf, lf, fine


# ---------------------------------------------------------------------------------------------------------------------------------- the report
def root_of(category):
    return 36 if category == "Bass" else 48


def run_report(preset_path, profile, lab, quick=False, long_hold=False):
    doc = json.load(open(preset_path))
    cat = doc["meta"]["category"]
    root = root_of(cat)
    CENTROID_LO[0] = 150.0 if cat == "Bass" else 60.0
    tag = Path(preset_path).stem + "_" + profile
    p = str(preset_path)
    held = doc["state"]["params"].get("master.polyphony", 16)
    out = {"name": doc["meta"]["name"], "category": cat, "profile": profile}
    chord4 = [root + 12, root + 16, root + 19, root + 24] if cat != "Bass" else [36, 40, 43, 48]
    chord4 = [60, 64, 67, 72] if cat != "Bass" else [36, 40, 43, 48]
    chord8 = [root, root + 7, root + 12, root + 16, root + 19, root + 24, root + 28, root + 31]
    jobs = {}
    ons = lambda ks, v, t=0.0: [f"on {t} {k} {v}" for k in ks]
    offs = lambda ks, t: [f"off {t} {k}" for k in ks]
    # a swelling sound is levelled where it has swelled: the chord is held until the slowest of the attacks of env 1 and 2 is over (the first hundred are levelled after one second, which
    # leaves a slow pad far louder when it has opened: scripts/tune-preset-levels.py), at most 5 s
    P = doc["state"]["params"]
    att = max(float(P.get("env1.attack", 5)) + float(P.get("env1.delay", 0)), 0.0)
    hold = min(5.0, max(1.0, 0.4 + att / 1000.0))
    out["chord_hold"] = hold
    jobs["chord4"] = lab.job(tag + "_c4", p, profile, hold + 1.5, *ons(chord4, 1.0), *offs(chord4, hold))
    jobs["chord8"] = lab.job(tag + "_c8", p, profile, hold + 4.0, *ons(chord8, 1.0), *offs(chord8, hold))
    for note in (36, 48, 60, 72, 84):
        jobs[f"n{note}"] = lab.job(f"{tag}_n{note}", p, profile, 6.0, f"on 0 {note} 0.8", f"off 2.0 {note}")
    for v in (30, 80, 127):
        jobs[f"v{v}"] = lab.job(f"{tag}_v{v}", p, profile, 2.5, f"on 0 {root + 12} {v / 127}", f"off 1.5 {root + 12}")
    if not quick:
        for m in range(1, 9):
            for val in (0, 100):
                jobs[f"m{m}_{val}"] = lab.job(f"{tag}_m{m}_{val}", p, profile, 4.0, f"set macro.{m} {val}", f"on 0 {root + 12} 0.8", f"off 3.0 {root + 12}")
        n = root + 12
        pr = [f"on 0 {n} 0.8"] + [f"press {1.0 + i * 0.05} {n} {min(1.0, i / 20)}" for i in range(21)] + [f"off 3.2 {n}"]
        jobs["press"] = lab.job(tag + "_press", p, profile, 5.0, *pr)
        jobs["press0"] = lab.job(tag + "_press0", p, profile, 5.0, f"on 0 {n} 0.8", f"off 3.2 {n}")
        jobs["mw1"] = lab.job(tag + "_mw1", p, profile, 5.0, "mw 0 1.0", f"on 0 {n} 0.8", f"off 3.2 {n}")
        jobs["mw0"] = lab.job(tag + "_mw0", p, profile, 5.0, f"on 0 {n} 0.8", f"off 3.2 {n}")
        # the vibrato is measured on the fundamental of one plain voice (unison, the other oscillators and the warp off), with the mod wheel down and up
        plain = ["set osc1.unison.voices 1", "set osc2.on 0", "set osc3.on 0", "set osc1.warp.type 0", "set osc1.warp2.type 0", "set osc1.morph.type 0", "set filter1.on 0", "set filter2.on 0", "set osc1.wavetable 0",
                 "set chorus.on 0", "set phaser.on 0", "set flanger.on 0", "set hyperdim.on 0", "set bode.on 0", "set delay.on 0", "set reverb.on 0"]
        jobs["vib0"] = lab.job(tag + "_vib0", p, profile, 4.0, *plain, f"on 0 {n} 0.8", f"off 3.5 {n}")
        jobs["vib1"] = lab.job(tag + "_vib1", p, profile, 4.0, *plain, "mw 0 1.0", f"on 0 {n} 0.8", f"off 3.5 {n}")
        jobs["long"] = lab.job(tag + "_long", p, profile, 14.0, f"on 0 {root + 12} 0.8", f"off 10.0 {root + 12}")
    lab.run()
    W = {k: lab.load(v) for k, v in jobs.items()}

    # level of the reference chord (loudest 300 ms while the keys are down), the same measure as preset_render's chordRms
    l, r = W["chord4"]
    out["chord_rms"] = loudest_rms(l, r, 0, int(hold * SR))
    out["chord_peak_db"] = db(max(np.abs(l[:int(hold * SR)]).max(), np.abs(r[:int(hold * SR)]).max()))
    l, r = W["chord8"]
    pk = db(max(np.abs(l[:int(hold * SR)]).max(), np.abs(r[:int(hold * SR)]).max()))
    out["chord8_peak_db"] = pk
    out["chord8_peak_out_db"] = pk + OUTPUT_GAIN_DB
    out["chord8_rms"] = loudest_rms(l, r, 0, int(hold * SR))
    out["chord8_finite"] = bool(np.isfinite(l).all() and np.isfinite(r).all())
    # stereo: mono sum of the 8 note chord (hold)
    cor, mono, worst, wf = stereo_metrics(l, r, int(max(0.3, hold - 1.0) * SR), int(hold * SR))
    out["stereo_cor"], out["mono_db"], out["mono_worst_band_db"], out["mono_worst_band_hz"] = cor, mono, worst, wf
    # dc and clicks, single notes
    dcs, clicks, ends, tails, peaks_single = [], [], [], [], []
    for note in (36, 48, 60, 72, 84):
        l, r = W[f"n{note}"]
        dcs.append(max(abs(l[SR // 4:2 * SR].mean()), abs(r[SR // 4:2 * SR].mean())))
        clicks.append(max(click_start(l, 0), click_start(r, 0)))
        e, tail_t = tail_end_db(l, r, 2 * SR)
        ends.append(e)
        tails.append(tail_t)
        peaks_single.append(db(max(np.abs(l).max(), np.abs(r).max())))
    out["dc_max"] = max(dcs)
    out["click_start_max"] = max(clicks)
    out["end_level_db_max"] = max(ends)
    out["tail_s_max"] = max(tails)
    out["single_peak_db"] = max(peaks_single)
    # level per register
    out["reg_rms"] = {note: loudest_rms(*W[f"n{note}"], 0, 2 * SR) for note in (36, 48, 60, 72, 84)}
    # velocity
    lv = {v: loudest_rms(*W[f"v{v}"], 0, int(1.5 * SR)) for v in (30, 80, 127)}
    out["vel_level_db"] = lv
    c30 = np.median(centroid_track(*W["v30"], 0, int(1.5 * SR))[1])
    c127 = np.median(centroid_track(*W["v127"], 0, int(1.5 * SR))[1])
    out["vel_centroid_oct"] = math.log2(max(c127, 1) / max(c30, 1))
    if quick:
        return out
    # macros
    mac = {}
    i0, i1 = int(0.3 * SR), int(3.0 * SR)
    for m in range(1, 9):
        # the attack (first 0.25 s) and the body (0.5 .. 3 s) are compared apart: a macro of the snap of a note changes only the first
        best = max((spectral_distance(W[f"m{m}_0"], W[f"m{m}_100"], a, b) for a, b in ((0, int(0.25 * SR)), (int(0.5 * SR), i1))), key=lambda t: t[0])
        mac[m] = best
    out["macros"] = mac
    out["pressure"] = spectral_distance(W["press0"], W["press"], int(1.5 * SR), int(3.2 * SR))
    out["modwheel"] = spectral_distance(W["mw0"], W["mw1"], i0, i1)
    # movement over a long held note
    l, r = W["long"]
    t, cen, lev = centroid_track(l, r, int(0.3 * SR), 10 * SR)
    out["move_centroid_oct"] = float(np.log2(np.percentile(cen, 95) / max(np.percentile(cen, 5), 1)))
    out["move_level_db"] = float(np.percentile(lev, 95) - np.percentile(lev, 5))
    # spectral flux: mean abs change of the log band spectrum between frames 0.5 s apart
    f, tt, pw = spectrum_frames(0.5 * (l + r)[int(0.3 * SR):10 * SR], 8192, 4096)
    lg = 10 * np.log10(np.maximum(pw, 1e-14))
    sel = (f > 60) & (f < 12000)
    lg = lg[sel]
    keepb = lg.max(axis=1) > lg.max() - 50
    step = max(1, int(0.5 * SR / 4096))
    flux = np.abs(lg[:, step:] - lg[:, :-step])[keepb].mean() if lg.shape[1] > step else 0.0
    out["move_flux_db"] = float(flux)
    out["lfo_hz"], out["lfo_depth_db"] = level_modulation(l, r, int(0.5 * SR), 9 * SR)
    # vibrato with the mod wheel up and the macros at 0 / 100 (pitch of the fundamental)
    out["vib_wheel0"] = pitch_modulation(*W["vib0"], int(1.0 * SR), int(3.5 * SR), root + 12)
    out["vib_wheel1"] = pitch_modulation(*W["vib1"], int(1.0 * SR), int(3.5 * SR), root + 12)
    return out


def fmt(o):
    L = []
    L.append(f"== {o['name']} [{o['category']}] profile {o['profile']}")
    L.append(f"  level   chord4 RMS {o['chord_rms']:6.1f} dBFS (ref -20, held {o['chord_hold']:.1f} s)  chord4 peak {o['chord_peak_db']:6.1f}  8-note v127 peak {o['chord8_peak_db']:6.1f} raw = {o['chord8_peak_out_db']:6.1f} dBFS at +{OUTPUT_GAIN_DB:g} before the limiter"
             f"  8-note RMS {o['chord8_rms']:6.1f}  single-note peak {o['single_peak_db']:6.1f}")
    L.append(f"  stereo  corr {o['stereo_cor']:5.2f}  mono sum {o['mono_db']:5.1f} dB vs stereo  worst 1/3 oct band {o['mono_worst_band_db']:6.1f} dB at {o['mono_worst_band_hz']:.0f} Hz")
    L.append(f"  clean   dc {o['dc_max']:.5f}  click ratio {o['click_start_max']:.1f}  end level {o['end_level_db_max']:.1f} dBFS  tail {o['tail_s_max']:.2f} s")
    L.append("  register RMS " + "  ".join(f"{k}:{v:.1f}" for k, v in o["reg_rms"].items()))
    L.append(f"  velocity 30/80/127 level {o['vel_level_db'][30]:.1f} / {o['vel_level_db'][80]:.1f} / {o['vel_level_db'][127]:.1f} dB, centroid {o['vel_centroid_oct']:+.2f} oct (30 -> 127)")
    if "macros" in o:
        L.append("  macros  (spectral distance dB / level dB / centroid oct / side dB / beat-movement dB / HF >6k dB / LF <200 Hz dB / fine-structure dB)  " + "  ".join(f"M{m}:{t[0]:.1f}/{t[1]:+.1f}/{t[2]:+.2f}/{t[3]:+.1f}/{t[4]:+.1f}/hf{t[5]:+.0f}/lf{t[6]:+.0f}/fine{t[7]:.0f}" for m, t in o["macros"].items() if t[0] > 0.05 or abs(t[3]) > 0.05 or abs(t[4]) > 0.05))
        t, u = o["pressure"], o["modwheel"]
        L.append(f"  pressure {t[0]:.1f}/{t[1]:+.1f}/{t[2]:+.2f}/{t[3]:+.1f}/{t[4]:+.1f}   modwheel {u[0]:.1f}/{u[1]:+.1f}/{u[2]:+.2f}/{u[3]:+.1f}/{u[4]:+.1f}")
        L.append(f"  vibrato (rate Hz / depth cents std) wheel 0: {o['vib_wheel0'][0]:.1f} / {o['vib_wheel0'][1]:.1f}   wheel 1: {o['vib_wheel1'][0]:.1f} / {o['vib_wheel1'][1]:.1f}")
        L.append(f"  movement centroid p5-p95 {o['move_centroid_oct']:.2f} oct  level p5-p95 {o['move_level_db']:.1f} dB  flux {o['move_flux_db']:.1f} dB/0.5 s  periodic level {o['lfo_hz']:.2f} Hz depth {o['lfo_depth_db']:.1f} dB")
    return "\n".join(L)


def distances(presets_dir, new_names):
    """The survey's similarity measure (scripts/survey-presets.py): the number of parameters whose values differ between two presets (a parameter that is not in a preset has its default).
    For every preset of `new_names`: the nearest preset of the whole set, its distance, and the distance to the nearest other preset of the new ones."""
    doc = json.load(open(ROOT / "docs" / "params.json"))
    defaults = {p["key"]: float(p["default"]) for p in doc["params"]}
    sets = {}
    for f in sorted(Path(presets_dir).rglob("*.sbpreset")):
        d = json.load(open(f))
        params = d["state"]["params"]
        nd = {k: float(params.get(k, dv)) for k, dv in defaults.items() if abs(float(params.get(k, dv)) - dv) > 1e-6}
        sets[d["meta"]["name"]] = (d["meta"]["category"], nd)
    out = {}
    names = list(sets)
    for a in new_names:
        best, best_new = (None, 10 ** 9), (None, 10 ** 9)
        for b in names:
            if a == b:
                continue
            ka = set(sets[a][1]) | set(sets[b][1])
            dist = sum(1 for k in ka if abs(sets[a][1].get(k, defaults[k]) - sets[b][1].get(k, defaults[k])) > 1e-6)
            if dist < best[1]:
                best = (b, dist)
            if b in new_names and dist < best_new[1]:
                best_new = (b, dist)
        out[a] = {"nearest": best[0], "distance": best[1], "nearest_new": best_new[0], "distance_new": best_new[1], "differs_from_default": len(sets[a][1])}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["report"])
    ap.add_argument("presets", nargs="+")
    ap.add_argument("--profile", default="both")
    ap.add_argument("--bin", default=str(DEFAULT_BIN))
    ap.add_argument("--work", default=None)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    lab = Lab(a.bin, a.work)
    res = []
    for pth in a.presets:
        for prof in (["xl", "low"] if a.profile == "both" else [a.profile]):
            o = run_report(pth, prof, lab, quick=a.quick or prof == "low")
            res.append(o)
            print(fmt(o))
            sys.stdout.flush()
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1, default=float)


if __name__ == "__main__":
    main()
