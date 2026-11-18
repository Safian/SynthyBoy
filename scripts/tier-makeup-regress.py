#!/usr/bin/env python3
# SynthyBoy - regression of the band models of the tier makeup (docs/release/TIER-LEVEL-COMPENSATION.md section 8): the gain of an effect cell on synthetic programs against the band
# descriptors of the input that the engine's meter (core/engine-own/fx/fx_makeup.cpp, ProgramMeter) reports.
#
#   tier-makeup-regress.py --bin synthyboy_fx_makeup_calibrate MODULE "key=value ..." [--metric l|r]
import argparse, importlib.util, os, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
from scipy.optimize import nnls
from scipy.signal import lfilter

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("loudness", HERE / "loudness.py")
ln = importlib.util.module_from_spec(spec); spec.loader.exec_module(ln)
SR = 44100
CORNERS = [120.0, 480.0, 1900.0, 7600.0]
KW = np.array([1.0, 1.0, 1.0, 1.35, 2.1])   # K weighting of the band energies (loudness), the shelf of BS.1770 above 1.7 kHz
PROGRAMS = ["noise", "sawlow", "sawmid", "sawhigh", "squaremid", "sinemid", "pluck", "pad"]
BANDS = [(30, 120), (120, 480), (480, 1900), (1900, 7600), (7600, 14000)]


def meter(l, r):
    """Band energies of the mid signal and the side energy, as the engine's meter takes them (one pole splits of the mid)."""
    m, s = 0.5 * (l + r), 0.5 * (l - r)
    lp = []
    for fc in CORNERS:
        a = 1 - np.exp(-2 * np.pi * fc / SR)
        lp.append(lfilter([a], [1, -(1 - a)], m))
    b = [lp[0], lp[1] - lp[0], lp[2] - lp[1], lp[3] - lp[2], m - lp[3]]
    e = np.array([(x ** 2).mean() for x in b])
    return e, (s ** 2).mean()


def jobs_for(settings):
    jobs = []
    for pr in PROGRAMS:
        for w in ("stereo", "mono"):
            jobs.append((f"{pr}/{w}", f"{w} program={pr}"))
    for i, (lo, hi) in enumerate(BANDS):
        for w in ("stereo", "mono", "indep"):
            jobs.append((f"band{i}/{w}", f"{w} spec={lo}:{hi}:-1"))
    for e in (-0.5, -1.5, -2.5):
        jobs.append((f"tilt{e}/stereo", f"stereo spec=30:12000:{e}"))
    # real programs: dry renders of factory presets (scripts/tier-makeup-dry.py), the directory in $TIER_MAKEUP_DRY
    dry = os.environ.get("TIER_MAKEUP_DRY")
    if dry:
        for f in sorted(Path(dry).glob("*.f32")):
            jobs.append((f"dry/{f.stem}", f"stereo input={f}"))
    return jobs


def run(binary, module, settings):
    tmp = Path(tempfile.mkdtemp(prefix="rg-"))
    jl = jobs_for(settings)
    lines = [f"{module} {extra} dump={tmp / str(i)} {settings}" for i, (_, extra) in enumerate(jl)]
    r = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    rows = []
    skip = 4 * SR
    for i, (name, _) in enumerate(jl):
        d = np.fromfile(tmp / f"{i}.in", "<f4").astype(float).reshape(-1, 2)
        o = np.fromfile(tmp / f"{i}.out", "<f4").astype(float).reshape(-1, 2)
        e, es = meter(d[:, 0], d[:, 1])
        pin = (d[skip:] ** 2).mean(); pout = (o[skip:] ** 2).mean()
        fi = ln.window_max(d[:, 0], d[:, 1], SR, 0.1); fo = ln.window_max(o[:, 0], o[:, 1], SR, 0.1)
        rows.append((name, e, es, 10 * np.log10(pout / pin), float(fo - fi)))
    for f in tmp.iterdir(): f.unlink()
    tmp.rmdir()
    return rows


def design(e, es, metric):
    """Fractions of the band energies; the loudness metric weights them with the K weighting."""
    w = KW if metric == "l" else np.ones(5)
    ee = e * w
    tot = ee.sum() + 1e-30
    f = ee / tot
    wide = min(1.0, 2.0 * es / (e.sum() + es + 1e-30))
    return np.concatenate([f * (1 - wide), f * wide])


def fit_cell(binary, module, settings, metric="l", ridge=0.15):
    """The ten band gains (power, mono five then wide five) of one cell of an effect: non negative least squares of the measured gain over the band descriptors of the programs, with a
    pull of every gain towards the mean gain (a band that no program loads much stays near the mean instead of going to zero). Returns (gains in dB, rms error dB, max error dB)."""
    rows = run(binary, module, settings)
    X = np.array([design(e, es, metric) for _, e, es, _, _ in rows])
    y = np.array([10 ** ((rl if metric == "l" else rr) / 10) for _, _, _, rr, rl in rows])
    y = np.where(np.isfinite(y), y, 0.0)
    g0 = y.mean()
    Xa = np.vstack([X, ridge * np.eye(10)])
    ya = np.concatenate([y, ridge * g0 * np.ones(10)])
    g, _ = nnls(Xa, ya)
    err = 10 * np.log10(np.maximum(X @ g, 1e-9)) - 10 * np.log10(y)
    return 10 * np.log10(np.maximum(g, 1e-6)), float(np.sqrt((err ** 2).mean())), float(abs(err).max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--metric", default="l")
    ap.add_argument("module")
    ap.add_argument("settings")
    a = ap.parse_args()
    rows = run(a.bin, a.module, a.settings)
    X = np.array([design(e, es, a.metric) for _, e, es, _, _ in rows])
    y = np.array([10 ** ((rl if a.metric == "l" else rr) / 10) for _, _, _, rr, rl in rows])
    g, _ = nnls(X, y)
    pred = 10 * np.log10(np.maximum(X @ g, 1e-9))
    meas = 10 * np.log10(y)
    print("G mono (dB):", np.round(10 * np.log10(g[:5] + 1e-9), 2), " G wide:", np.round(10 * np.log10(g[5:] + 1e-9), 2))
    err = pred - meas
    print("fit error dB: rms %.2f max %.2f" % (np.sqrt((err ** 2).mean()), abs(err).max()))
    for (n, *_), m, p in zip(rows, meas, pred):
        if abs(p - m) > 0.8: print(f"   {n:22} meas {m:6.2f} pred {p:6.2f}")


if __name__ == "__main__":
    sys.exit(main())
