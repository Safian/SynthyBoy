#!/usr/bin/env python3
# SynthyBoy - Markdown tables from the stdout of tier_bench (docs/plans/perf-measurement.md, T11):
#   sections  the exclusive time of each engine section per block (tier_bench --sections, the build with -DSYNTHYBOY_SECTION_TIMING=ON), one table per (profile, load)
#   modules   the cost of each module (tier_bench --modules), sorted by group, then by cost, largest first
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: python3 -I scripts/perf-tables.py sections|modules [FILE]     (stdin when FILE is missing or "-")

import re
import sys

BLOCK_MS_DEFAULT = 128.0 / 44100.0 * 1000.0

HEADER_RE = re.compile(r"^synthyboy_tier_bench: (\d+(?:\.\d+)?) Hz, block (\d+) frames")
SECTION_RE = re.compile(
    r"^\s+section (\S.*?)\s+(-?\d+(?:\.\d+)?) ms / block\s+(-?\d+(?:\.\d+)?) %\s+(-?\d+(?:\.\d+)?) calls / block\s+(-?\d+(?:\.\d+)?) clock reads"
    r"\s+less clock\s+(-?\d+(?:\.\d+)?) ms\s+(-?\d+(?:\.\d+)?) %")
MODULE_RE = re.compile(r"^module (.+?) \| (.+?) \| base (\S+) us \| case (\S+) us \| cost (\S+) us \| (\S+) % of block \| reps (\d+) \| spread (\S+) %\s*$")


def read_lines(path):
    if path is None or path == "-":
        return sys.stdin.read().splitlines()
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def block_ms(lines):
    """The block length in ms from the 'synthyboy_tier_bench: <sr> Hz, block <n> frames' line; 128 frames at 44.1 kHz without it."""
    for line in lines:
        m = HEADER_RE.match(line)
        if m:
            return float(m.group(2)) / float(m.group(1)) * 1000.0
    return BLOCK_MS_DEFAULT


def result_key(line):
    """(profile, load) of a result row: a non-indented line with '|' whose profile is line[0:58] and load line[59:83] (the '%-58s %-24.24s' of tier_bench.cpp); None otherwise."""
    if not line or line[0] in " \t" or "|" not in line or line.startswith("module") or len(line) < 60:
        return None
    profile, load = line[0:58].strip(), line[59:83].strip()
    if not profile or (profile == "profile" and load == "load"):
        return None
    return profile, load


def sections_tables(lines):
    ms = block_ms(lines)
    tables = []
    current = None
    for line in lines:
        key = result_key(line)
        if key is not None:
            current = (key, [])
            tables.append(current)
            continue
        m = SECTION_RE.match(line)
        if m and current is not None:
            current[1].append((m.group(1), float(m.group(2)), float(m.group(6))))
    out = []
    for (profile, load), rows in tables:
        if not rows:
            continue
        out.append("### %s | %s" % (profile, load))
        out.append("")
        out.append("| section | ms/block | ms/block less clock | % of block |")
        out.append("|---|---|---|---|")
        for name, per_block, less in rows:
            out.append("| %s | %.4f | %.4f | %.2f |" % (name, per_block, less, less / ms * 100.0))
        out.append("")
    return out


def modules_table(lines):
    rows = []
    for line in lines:
        m = MODULE_RE.match(line)
        if m:
            rows.append((m.group(1), m.group(2), float(m.group(5)), m.group(5).lstrip("+"), m.group(6).lstrip("+"), m.group(8)))
    rows.sort(key=lambda r: (r[0], -r[2]))
    out = ["| group | module | cost us | % of block | spread % |", "|---|---|---|---|---|"]
    for group, name, _cost, cost_text, share_text, spread in rows:
        out.append("| %s | %s | %s | %s | %s |" % (group, name, cost_text, share_text, spread))
    return out


def main(argv):
    if len(argv) < 2 or argv[1] not in ("sections", "modules") or len(argv) > 3:
        print("usage: perf-tables.py sections|modules [FILE]", file=sys.stderr)
        return 2
    try:
        lines = read_lines(argv[2] if len(argv) > 2 else None)
    except OSError as e:
        print("perf-tables: %s" % e, file=sys.stderr)
        return 2
    out = sections_tables(lines) if argv[1] == "sections" else modules_table(lines)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
