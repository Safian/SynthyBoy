#!/usr/bin/env python3
# SynthyBoy - the engine's makeup model against the gain measured through the real module, on raw stereo renders (a folder of .f32 files), loudest 100 ms (K-weighted) and plain RMS.
#   tier-makeup-validate.py --bin synthyboy_fx_makeup_calibrate --dir DIR MODULE "key=value ..." ["key=value ..."]
import argparse, importlib.util, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("loudness", HERE / "loudness.py"); ln = importlib.util.module_from_spec(spec); spec.loader.exec_module(ln)
SR = 44100
ap = argparse.ArgumentParser(); ap.add_argument("--bin", required=True); ap.add_argument("--dir", required=True); ap.add_argument("module"); ap.add_argument("settings", nargs="+")
a = ap.parse_args()
files = sorted(Path(a.dir).glob("*.f32"))
tmp = Path(tempfile.mkdtemp(prefix="val-"))
jobs, tags = [], []
for st in a.settings:
    for i, f in enumerate(files):
        jobs.append(f"{a.module} stereo input={f} dump={tmp / (str(len(tags)))} {st}"); tags.append((st, f.stem, len(tags)))
r = subprocess.run([a.bin], input="\n".join(jobs) + "\n", capture_output=True, text=True)
lines = r.stdout.strip().splitlines()
res = {}
for (st, name, k), line in zip(tags, lines):
    w = line.split(); model = float(w[3])
    d = np.fromfile(tmp / f"{k}.in", "<f4").astype(float).reshape(-1, 2); o = np.fromfile(tmp / f"{k}.out", "<f4").astype(float).reshape(-1, 2)
    sk = 4 * SR
    rr = 10 * np.log10(((o[sk:] ** 2).mean() + 1e-30) / ((d[sk:] ** 2).mean() + 1e-30))
    ll = ln.window_max(o[:, 0], o[:, 1], SR, .1) - ln.window_max(d[:, 0], d[:, 1], SR, .1)
    res.setdefault(st, []).append((name, rr, ll, model))
for st, rows in res.items():
    el = np.array([m - l for _, _, l, m in rows]); er = np.array([m - r for _, r, _, m in rows])
    print(f"{st}\n  model - loudest100ms: max {abs(el).max():.2f} rms {np.sqrt((el ** 2).mean()):.2f} bias {el.mean():+.2f}   model - rms: max {abs(er).max():.2f} rms {np.sqrt((er ** 2).mean()):.2f} bias {er.mean():+.2f}")
    for n, rr, ll, m in rows:
        if abs(m - ll) > 1.0: print(f"    {n:24} l {ll:+.2f} r {rr:+.2f} model {m:+.2f}")
for f in tmp.iterdir(): f.unlink()
tmp.rmdir()
