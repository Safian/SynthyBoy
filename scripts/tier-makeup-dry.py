#!/usr/bin/env python3
# SynthyBoy - dry renders of factory presets (every effect switched off) as raw 32 bit float stereo files, the "real programs" of the regression of the tier makeup models
# (scripts/tier-makeup-regress.py, docs/release/TIER-LEVEL-COMPENSATION.md section 8).
#
#   tier-makeup-dry.py --bin synthyboy_sound_lab --out DIR [--per-category 4]
#
# The gesture of scripts/measure-loudness.py (the first render of the category), the first N presets of each category in the file order of content/presets, repeated to 11 s.
import argparse, importlib.util, json, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("ml", HERE / "measure-loudness.py")
ml = importlib.util.module_from_spec(spec); spec.loader.exec_module(ml)
FX = ["chorus", "compressor", "delay", "distortion", "eq", "fxfilter", "flanger", "phaser", "reverb", "hyperdim", "utility", "bode", "convolve"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--per-category", type=int, default=4)
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    lab = ml.lab_mod.Lab(a.bin, out / "work")
    chosen, count = [], {}
    for f in sorted((ml.ROOT / "content" / "presets").glob("*/*.sbpreset")):
        d = json.load(open(f)); c = d["meta"]["category"]
        if count.get(c, 0) < a.per_category:
            count[c] = count.get(c, 0) + 1
            chosen.append((f, d))
    jobs = []
    for f, d in chosen:
        tag, secs, items = ml.gesture(d["meta"]["category"], d["state"]["params"], 4.0)[0]
        sets = [f"set {m}.on 0" for m in FX]
        jobs.append((f, lab.job(f"dry_{f.stem}", str(f), "xl", secs + ml.PRE, *sets, *ml.shifted(items))))
    lab.run()
    for f, wav in jobs:
        l, r = lab.load(wav)
        x = np.stack([l, r], 1)
        n = int(11 * ml.SR)
        x = np.tile(x, (n // len(x) + 1, 1))[:n]
        x.astype("<f4").tofile(out / (f.stem + ".f32"))
        wav.unlink()
    print(len(jobs), "dry programs in", out)


if __name__ == "__main__":
    sys.exit(main())
