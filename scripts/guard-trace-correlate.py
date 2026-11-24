#!/usr/bin/env python3
# SynthyBoy - puts the page guard handshake (shell-mpc/overlay/gpu/guard_trace.h) and the commit shim log on one time line (CLOCK_MONOTONIC ns on both).
# Usage: guard-trace-correlate.py TRACE.log SHIM.log
#   TRACE.log: /tmp/synthyboy-overlay-guard-trace.log (while /tmp/synthyboy-overlay-guard-trace exists), lines `<ns> <helper|plugin> <event> <a> <b>`
#   SHIM.log : /tmp/xl-commit-shim.log, lines `commit time_ns=<ns> ... site=frame (merged) ...` (a merged commit = the guard's plane-off, injected into the host's flip)
# Output: per guard (a plane-off commit) the time split in ms from the host's flip commit: flag read, kMsgGuard sent/received, each detection (view or none), kMsgPageOk sent/received, first present.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import re
import sys


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    trace = []
    for line in open(argv[1], encoding="utf-8"):
        p = line.split()
        if len(p) >= 3:
            trace.append((int(p[0]), p[1], p[2], p[3:]))
    trace.sort()
    flips = []
    for line in open(argv[2], encoding="utf-8", errors="replace"):
        m = re.match(r"commit time_ns=(\d+) .*site=frame \(merged\)", line)
        if m:
            flips.append(int(m.group(1)))
    if not flips:
        print("no merged (plane-off) commit in the shim log")
        return 1
    reappear = []
    for i, t0 in enumerate(flips):
        t1 = flips[i + 1] if i + 1 < len(flips) else t0 + 1_000_000_000
        events = [e for e in trace if t0 - 5_000_000 <= e[0] < t1]
        print("guard plane-off commit at %d ns" % t0)
        first = None
        for t, who, ev, args in events:
            print("  %+8.1f ms  %-6s %-14s %s" % ((t - t0) / 1e6, who, ev, " ".join(args)))
            if ev == "first_present" and first is None:
                first = (t - t0) / 1e6
        if first is not None:
            reappear.append(first)
    if reappear:
        s = sorted(reappear)
        print("flip -> first present: n=%d median %.1f ms, p90 %.1f ms, max %.1f ms" % (len(s), s[len(s) // 2], s[int(len(s) * 0.9)], s[-1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
