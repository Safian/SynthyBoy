#!/usr/bin/env python3
# SynthyBoy - interleaved A/B analysis of tier_bench CSVs (schema v2, one pass per file; docs/plans/perf-measurement.md 3.5).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/perf-ab.py --metric median_pct|p99_pct|cycles_mean [--min-gain 0.01] [--fail-on-regression]
#                                      --sequence ABBAABBA... FILE1 FILE2 ...
#
# The files are in chronological order, --sequence gives the label (A or B) of each file. The groups of two adjacent files (f0, f1), (f2, f3), ...
# are the pairs and must carry different labels. A pair gives rho = M(B) / M(A) for every (profile, load) key present in both files (a missing or
# empty metric skips the key for that pair). K pairs give K ratios per key:
#   estimate = median of the ratios (mean of the two middle values for an even K)
#   interval = [rho_(r), rho_(K + 1 - r)] of the sorted ratios, r the largest value with 2 * P (Bin (K, 1/2) <= r - 1) <= 0.05 (distribution free, exact)
#   verdict  = GAIN if the upper end < 1 - g, REGRESSION if the lower end > 1 + g, else NO-CHANGE-PROVEN (g = --min-gain)
# Exit codes: 0; 1 with --fail-on-regression and at least one REGRESSION; 2 for a usage error (too few pairs, a pair with equal labels, bad files).

import argparse
import csv
import math
import sys

MIN_PAIRS = 6
METRICS = ("median_pct", "p99_pct", "cycles_mean")


def interval_rank(k: int) -> int:
    """The largest r with 2 * P (Bin (k, 1/2) <= r - 1) <= 0.05, computed in integers: 40 * sum (C (k, i), i < r) <= 2^k. 0 when none."""
    r = 0
    cumulative = 0
    while r < k:
        cumulative += math.comb(k, r)
        if 40 * cumulative > 2 ** k:
            break
        r += 1
    return r


def median_of(sorted_values):
    n = len(sorted_values)
    if n % 2:
        return sorted_values[n // 2]
    return 0.5 * (sorted_values[n // 2 - 1] + sorted_values[n // 2])


def read_metric(path: str, metric: str):
    """{(profile, load): value} of one CSV; rows with an empty or unreadable metric are left out."""
    out = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            text = (row.get(metric) or "").strip()
            if text == "":
                continue
            try:
                value = float(text)
            except ValueError:
                continue
            out[(row.get("profile", ""), row.get("load", ""))] = value
    return out


def fail(message: str):
    print("perf-ab: " + message, file=sys.stderr)
    sys.exit(2)


def main() -> int:
    ap = argparse.ArgumentParser(description="interleaved A/B analysis of tier_bench CSVs")
    ap.add_argument("--metric", required=True, choices=METRICS)
    ap.add_argument("--min-gain", type=float, default=0.01)
    ap.add_argument("--fail-on-regression", action="store_true")
    ap.add_argument("--sequence", required=True, help="the label (A or B) of each file, e.g. ABBAABBA")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()

    sequence = args.sequence
    if any(c not in "AB" for c in sequence):
        fail("the sequence may contain only A and B")
    if len(sequence) != len(args.files):
        fail("the sequence has %d labels but %d files were given" % (len(sequence), len(args.files)))
    if len(sequence) % 2:
        fail("an odd number of files: the files form pairs")
    pairs = len(sequence) // 2
    for k in range(pairs):
        if sequence[2 * k] == sequence[2 * k + 1]:
            fail("pair %d (files %d and %d) has equal labels %s%s" % (k + 1, 2 * k + 1, 2 * k + 2, sequence[2 * k], sequence[2 * k + 1]))
    if pairs < MIN_PAIRS:
        fail("too few pairs: %d (at least %d are needed for a 95 %% interval)" % (pairs, MIN_PAIRS))

    try:
        data = [read_metric(path, args.metric) for path in args.files]
    except OSError as e:
        fail(str(e))

    keys = []
    seen = set()
    for d in data:
        for key in d:
            if key not in seen:
                seen.add(key)
                keys.append(key)

    ratios = {key: [] for key in keys}
    for k in range(pairs):
        first, second = 2 * k, 2 * k + 1
        a_index, b_index = (first, second) if sequence[first] == "A" else (second, first)
        for key in keys:
            a = data[a_index].get(key)
            b = data[b_index].get(key)
            if a is None or b is None or a <= 0.0:
                continue
            ratios[key].append(b / a)

    gain = regression = unproven = 0
    log_sum = 0.0
    counted = 0
    g = args.min_gain
    for key in keys:
        values = sorted(ratios[key])
        count = len(values)
        if count < MIN_PAIRS:
            print("perf-ab: key %s | %s has only %d pairs: skipped" % (key[0], key[1], count), file=sys.stderr)
            continue
        r = interval_rank(count)
        low, high = values[r - 1], values[count - r]
        estimate = median_of(values)
        if high < 1.0 - g:
            verdict = "GAIN"
            gain += 1
        elif low > 1.0 + g:
            verdict = "REGRESSION"
            regression += 1
        else:
            verdict = "NO-CHANGE-PROVEN"
            unproven += 1
        print("AB-VERDICT %s | %s | K=%d | median_ratio=%.4f | ci=[%.4f, %.4f] | %s" % (key[0], key[1], count, estimate, low, high, verdict))
        if estimate > 0.0:
            log_sum += math.log(estimate)
            counted += 1

    geomean = math.exp(log_sum / counted) if counted else 1.0
    print("AB-SUMMARY gain=%d regression=%d unproven=%d geomean_ratio=%.4f" % (gain, regression, unproven, geomean))
    return 1 if (args.fail_on_regression and regression > 0) else 0


if __name__ == "__main__":
    sys.exit(main())
