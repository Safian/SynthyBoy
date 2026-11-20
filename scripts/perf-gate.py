#!/usr/bin/env python3
# SynthyBoy - the regression threshold and the spread report over tier_bench CSVs (schema v2, one pass per file, key = (profile, load);
# docs/plans/perf-measurement.md 3.5).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage:
#   python3 -I scripts/perf-gate.py baseline --out FILE.json RUN.csv RUN.csv RUN.csv ...   (at least 3 passes)
#   python3 -I scripts/perf-gate.py compare BASELINE.json RUN.csv RUN.csv RUN.csv ...      (at least 3 passes)
#   python3 -I scripts/perf-gate.py spread RUN.csv ...
#
# Per key, with med = the median over the passes and spread = (max - min) / med of the baseline passes, the check fails when
#   p99_pct, median_pct   new_med > base_med * (1 + max (0.03, 2 * spread)) + 0.5
#   max_pct               new_med > base_med * (1 + max (0.10, 2 * spread)) + 2.0
#   blocks_over_100pct    new_med > base_med * 1.2 + 2
#   rss_kb                new_med > base_med * 1.05
#   minflt_measured       the largest new pass > the largest baseline pass (the audio path must not touch new pages after the warm-up)
#   majflt                any new row > 0
# A row with valid = 0 makes the whole check GATE-INVALID (exit 3, takes precedence): it is never averaged in.
# Exit codes: 0 pass, 1 violations, 2 usage error (too few passes, unreadable input), 3 invalid run.

import argparse
import csv
import json
import statistics
import sys

MIN_PASSES = 3
# metric -> (floor of the relative tolerance, slack added to the limit)
SPREAD_RULES = {
    "p99_pct": (0.03, 0.5),
    "median_pct": (0.03, 0.5),
    "max_pct": (0.10, 2.0),
}
STORED_METRICS = ("p99_pct", "median_pct", "max_pct", "blocks_over_100pct", "rss_kb", "minflt_measured")


def fail(message: str):
    print("perf-gate: " + message, file=sys.stderr)
    sys.exit(2)


def to_float(text):
    text = (text or "").strip()
    if text == "":
        return None
    try:
        return float(text)
    except ValueError:
        return None


def read_run(path: str):
    """{(profile, load): row} of one CSV."""
    rows = {}
    try:
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                rows[(row.get("profile", ""), row.get("load", ""))] = row
    except OSError as e:
        fail(str(e))
    return rows


def key_text(key) -> str:
    return "%s | %s" % key


def summarise(values):
    med = statistics.median(values)
    spread = (max(values) - min(values)) / med if med else 0.0
    return {"passes": values, "med": med, "spread": spread}


def collect(runs):
    """{key: {metric: [value per pass]}} plus the first row of every key (device, block, sr)."""
    keys = []
    for run in runs:
        for key in run:
            if key not in keys:
                keys.append(key)
    values = {}
    first = {}
    for key in keys:
        values[key] = {}
        for run in runs:
            row = run.get(key)
            if row is None:
                continue
            first.setdefault(key, row)
            for metric in STORED_METRICS + ("majflt",):
                v = to_float(row.get(metric))
                if v is not None:
                    values[key].setdefault(metric, []).append(v)
    return keys, values, first


def cmd_baseline(args) -> int:
    if len(args.files) < MIN_PASSES:
        fail("a baseline needs at least %d passes, got %d" % (MIN_PASSES, len(args.files)))
    runs = [read_run(p) for p in args.files]
    keys, values, first = collect(runs)
    out = {"schema": 1, "passes": len(runs), "keys": []}
    for key in keys:
        row = first[key]
        entry = {"profile": key[0], "load": key[1], "device": row.get("device", ""), "block": row.get("block", ""), "sr": row.get("sr", ""), "metrics": {}}
        for metric in STORED_METRICS:
            if metric in values[key]:
                entry["metrics"][metric] = summarise(values[key][metric])
        out["keys"].append(entry)
    try:
        with open(args.out, "w") as f:
            json.dump(out, f, indent=2)
            f.write("\n")
    except OSError as e:
        fail(str(e))
    print("baseline: %d keys from %d passes -> %s" % (len(out["keys"]), len(runs), args.out))
    return 0


def limit_for(metric, base):
    """The limit of the median of the new passes for metrics with a median rule."""
    med, spread = base["med"], base["spread"]
    if metric in SPREAD_RULES:
        floor, slack = SPREAD_RULES[metric]
        return med * (1.0 + max(floor, 2.0 * spread)) + slack
    if metric == "blocks_over_100pct":
        return med * 1.2 + 2.0
    if metric == "rss_kb":
        return med * 1.05
    return None


def cmd_compare(args) -> int:
    if len(args.files) < MIN_PASSES:
        fail("a check needs at least %d passes, got %d" % (MIN_PASSES, len(args.files)))
    try:
        with open(args.baseline) as f:
            baseline = json.load(f)
    except (OSError, ValueError) as e:
        fail("baseline unreadable: %s" % e)
    runs = [read_run(p) for p in args.files]

    invalid = []
    for run in runs:
        for key, row in run.items():
            if (row.get("valid") or "").strip() == "0":
                invalid.append("GATE-INVALID %s | %s" % (key_text(key), (row.get("invalid_reason") or "").strip()))
    if invalid:
        for line in invalid:
            print(line)
        return 3

    keys, values, _ = collect(runs)
    base_by_key = {(e["profile"], e["load"]): e for e in baseline.get("keys", [])}
    violations = 0
    for key, entry in base_by_key.items():
        if key not in values:
            print("GATE-FAIL %s | missing" % key_text(key))
            violations += 1
    for key in keys:
        if key not in base_by_key:
            print("GATE-NEW %s" % key_text(key))
            continue
        metrics = base_by_key[key]["metrics"]
        new = values[key]
        for metric in STORED_METRICS:
            if metric not in metrics or metric not in new:
                continue
            base = metrics[metric]
            if metric == "minflt_measured":
                limit = max(base["passes"])
                current = max(new[metric])
            else:
                limit = limit_for(metric, base)
                current = statistics.median(new[metric])
            if current > limit:
                print("GATE-FAIL %s | %s | base %.2f | new %.2f | limit %.2f" % (key_text(key), metric, base["med"], current, limit))
                violations += 1
        if new.get("majflt") and max(new["majflt"]) > 0:
            print("GATE-FAIL %s | majflt | base 0.00 | new %.2f | limit 0.00" % (key_text(key), max(new["majflt"])))
            violations += 1
    if violations:
        print("GATE-RESULT FAIL %d violations" % violations)
        return 1
    print("GATE-PASS %d keys" % len([k for k in keys if k in base_by_key]))
    return 0


def cmd_spread(args) -> int:
    runs = [read_run(p) for p in args.files]
    keys, values, _ = collect(runs)
    for key in keys:
        p99 = values[key].get("p99_pct")
        if not p99:
            continue
        s = summarise(p99)
        median = values[key].get("median_pct")
        gap = s["med"] - statistics.median(median) if median else 0.0
        print("SPREAD %s | p99_med=%.2f | spread=%.2f %% | gap_pts=%.2f" % (key_text(key), s["med"], 100.0 * s["spread"], gap))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="regression gate over tier_bench CSVs")
    sub = ap.add_subparsers(dest="command", required=True)
    b = sub.add_parser("baseline")
    b.add_argument("--out", required=True)
    b.add_argument("files", nargs="+")
    b.set_defaults(func=cmd_baseline)
    c = sub.add_parser("compare")
    c.add_argument("baseline")
    c.add_argument("files", nargs="+")
    c.set_defaults(func=cmd_compare)
    s = sub.add_parser("spread")
    s.add_argument("files", nargs="+")
    s.set_defaults(func=cmd_spread)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
