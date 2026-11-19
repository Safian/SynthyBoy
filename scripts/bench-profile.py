#!/usr/bin/env python3
# SynthyBoy - maps the program counters of `synthyboy_tier_bench --profile FILE` to functions and prints where the audio thread spent its time.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/bench-profile.py PCS.txt SYMBOLS.txt [--top N]
#   PCS.txt      the file written by --profile (hexadecimal addresses, one per line)
#   SYMBOLS.txt  the symbols of the UNSTRIPPED binary the profile was taken with (the file in the build tree: <build>/<arch>/bench/synthyboy_tier_bench; strip does not move code), as
#                `<cross>-nm -C --defined-only -n BINARY` (address, type, name; the sizes are not needed), or the output of `<cross>-objdump -d -C` (lines `<hex> <name>:`)
import bisect
import re
import sys


def load_symbols(path):
    syms = []
    pat_nm = re.compile(r"^([0-9a-fA-F]+) [A-Za-z] (.+)$")
    pat_dis = re.compile(r"^([0-9a-fA-F]+) <(.+)>:$")
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = pat_dis.match(line) or pat_nm.match(line)
            if m:
                syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]


def short(name):
    name = re.sub(r"\[clone [^\]]*\]", "", name)
    name = re.sub(r"\(anonymous namespace\)::", "", name)
    name = re.sub(r"synthyboy::(own::|sampleosc::|granular::|spectral::|multisample::)?", "", name)
    # drop the argument list (the last top level parenthesis)
    depth, cut = 0, None
    for i in range(len(name) - 1, -1, -1):
        if name[i] == ")":
            depth += 1
        elif name[i] == "(":
            depth -= 1
            if depth == 0:
                cut = i
                break
    return (name[:cut] if cut else name).strip()[:110]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    top = 25
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])
        args = [a for a in args if a != str(top)]
    if len(args) < 2:
        print(__doc__)
        return 1
    addrs, names = load_symbols(args[1])
    counts = {}
    total = 0
    with open(args[0]) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            pc = int(line, 16) & ~1
            i = bisect.bisect_right(addrs, pc) - 1
            name = short(names[i]) if i >= 0 else "?"
            counts[name] = counts.get(name, 0) + 1
            total += 1
    print(f"{total} samples")
    for name, c in sorted(counts.items(), key=lambda kv: -kv[1])[:top]:
        print(f"{100.0 * c / total:6.2f} %  {c:6d}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
