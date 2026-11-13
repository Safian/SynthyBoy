#!/usr/bin/env python3
# SynthyBoy - turns the file of `synthyboy_bench --profile FILE` (raw program counters of a SIGPROF sampler + the executable mappings)
# and the UNSTRIPPED binary into tables of functions with sample counts.
#
#   scripts/symbolize-profile.py profile.txt path/to/synthyboy_bench [--top 40] [--addr2line CMD]
#
# addr2line: by default `arm-linux-gnueabihf-addr2line` from PATH, else the Docker image kick-armv7-builder:latest (the binary's directory is mounted).
# Tables: (1) by innermost function (self, inlined callees count for themselves), (2) by outermost real function (inlining folded into the caller),
# (3) by inline chain (innermost <- ... <- outermost), (4) by source line.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import argparse, collections, os, shutil, subprocess, sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile")
    ap.add_argument("binary")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--addr2line", default=None)
    ap.add_argument("--image", default="kick-armv7-builder:latest")
    a = ap.parse_args()

    maps, pcs = [], []
    header = ""
    for line in open(a.profile):
        line = line.strip()
        if line.startswith("#"):
            header = line
        elif line.startswith("map "):
            _, s, e, off, path = line.split(None, 4)
            maps.append((int(s, 16), int(e, 16), int(off, 16), path))
        elif line:
            pcs.append(int(line, 16))
    base = os.path.basename(a.binary)
    mine = [m for m in maps if os.path.basename(m[3]) == base]
    if not mine:
        sys.exit("no executable mapping of %s in the profile (maps: %s)" % (base, [m[3] for m in maps]))
    # non-PIE (loaded at its link address, 0x10000 on ARM): the counters are the addresses; PIE: file offset = pc - start + offset
    pie = min(m[0] for m in mine) != 0x10000
    def to_addr(pc):
        for s, e, off, _ in mine:
            if s <= pc < e:
                return pc if not pie else pc - s + off
        return None
    counts = collections.Counter()
    outside = 0
    for pc in pcs:
        v = to_addr(pc)
        if v is None:
            outside += 1
        else:
            counts[v] += 1
    print(header)
    print("samples in the executable: %d, elsewhere (libc, libm, kernel stubs): %d, PIE: %s" % (len(pcs) - outside, outside, pie))
    if outside:
        lib = collections.Counter()
        for pc in pcs:
            if to_addr(pc) is None:
                for s, e, off, path in maps:
                    if s <= pc < e:
                        lib[os.path.basename(path)] += 1
                        break
        for k, v in lib.most_common(10):
            print("   in %-40s %6d" % (k, v))

    cmd = a.addr2line
    binpath = os.path.abspath(a.binary)
    if cmd is None:
        if shutil.which("arm-linux-gnueabihf-addr2line"):
            cmd = "arm-linux-gnueabihf-addr2line"
            target = binpath
        else:
            d = os.path.dirname(binpath)
            cmd = "docker run --rm -i -v %s:/bin-dir:ro %s arm-linux-gnueabihf-addr2line" % (d, a.image)
            target = "/bin-dir/" + os.path.basename(binpath)
    else:
        target = binpath
    addrs = sorted(counts)
    argv = cmd.split() + ["-a", "-f", "-C", "-i", "-e", target]
    out = subprocess.run(argv, input="\n".join("0x%x" % x for x in addrs) + "\n", capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("addr2line failed: " + out.stderr)
    # parse: "0xADDR" then pairs (function, file:line) for the innermost frame first
    frames = {}
    cur = None
    lines = out.stdout.splitlines()
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith("0x"):
            cur = int(l, 16)
            frames[cur] = []
            i += 1
            continue
        fn, loc = l, lines[i + 1] if i + 1 < len(lines) else "?"
        frames[cur].append((fn, loc))
        i += 2

    def short(f):
        return f if len(f) < 110 else f[:107] + "..."
    self_c, outer_c, chain_c, line_c = (collections.Counter() for _ in range(4))
    total = sum(counts.values())
    for addr, n in counts.items():
        fr = frames.get(addr) or [("??", "??")]
        self_c[fr[0][0]] += n
        outer_c[fr[-1][0]] += n
        chain_c[" <- ".join(short(f[0].split("(")[0]) for f in fr[:4])] += n
        line_c[fr[0][1].replace(os.path.dirname(os.path.dirname(fr[0][1])) + "/", "") + "  " + fr[0][0].split("(")[0]] += n

    def table(title, c):
        print("\n== %s" % title)
        for k, v in c.most_common(a.top):
            print("%6d %5.1f%%  %s" % (v, 100.0 * v / max(total, 1), short(k)))
    table("by innermost function (self)", self_c)
    table("by outermost function (inlined code counted in its caller)", outer_c)
    table("by inline chain", chain_c)
    table("by source line", line_c)


if __name__ == "__main__":
    main()
