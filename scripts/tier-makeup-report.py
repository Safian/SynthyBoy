#!/usr/bin/env python3
# SynthyBoy - table of Key 61 minus XL (loudest 100 ms, LU) before / after the tier makeup (docs/release/TIER-LEVEL-COMPENSATION.md).
#   tier-makeup-report.py --xl XL.json --low LOW.json --before ATTRIBUTION.json [--csv OUT.csv]
import argparse, json, sys
import numpy as np
NINE = ["Reese Dual Phase Bass", "Dusty Rhodes Keys", "Bode Barber Riser", "Hyper Dimension Pad", "Bode Shifter Drone", "Cathedral Choir", "Lush Analog Pad", "Frozen Pluck Drone", "Marimba Layer Keys"]
ap = argparse.ArgumentParser(); ap.add_argument("--xl", required=True); ap.add_argument("--low", required=True); ap.add_argument("--before", required=True); ap.add_argument("--csv")
a = ap.parse_args()
xl, lo, b = json.load(open(a.xl)), json.load(open(a.low)), json.load(open(a.before))
rows = []
same = 0
for n in xl:
    same += xl[n]["fast_max"] == b["xl"][n]["fast_max"]
    rows.append((n, xl[n]["category"], b["low"][n]["fast_max"] - b["xl"][n]["fast_max"], lo[n]["fast_max"] - xl[n]["fast_max"], b["low_fxall"][n]["fast_max"] - b["xl"][n]["fast_max"]))
B = np.array([r[2] for r in rows]); A = np.array([r[3] for r in rows])
print(f"XL fast_max identical to before for {same} of {len(xl)}")
for name, v in (("before", B), ("after", A)):
    print(f"{name}: max {abs(v).max():.2f} rms {np.sqrt((v ** 2).mean()):.2f} over 1 LU: {(abs(v) > 1).sum()} over 2 LU: {(abs(v) > 2).sum()}")
for n in NINE:
    r = [x for x in rows if x[0] == n][0]; print(f"{n:24} {r[2]:+.2f} -> {r[3]:+.2f}")
print("over 1 LU after:")
for r in sorted(rows, key=lambda r: -abs(r[3])):
    if abs(r[3]) > 1: print(f"  {r[0]:26} {r[1]:6} before {r[2]:+.2f} after {r[3]:+.2f} (other tier settings, fx=all: {r[4]:+.2f})")
if a.csv:
    with open(a.csv, "w") as f:
        f.write("preset,category,k61_minus_xl_before,k61_minus_xl_after,other_tier_settings\n")
        for r in sorted(rows, key=lambda r: -abs(r[2])): f.write(f"{r[0]},{r[1]},{r[2]:.2f},{r[3]:.2f},{r[4]:.2f}\n")
