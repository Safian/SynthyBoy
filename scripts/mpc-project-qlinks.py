#!/usr/bin/env python3
# SynthyBoy - reads an MPC project file (.xpj) and compares the Q-Link map each plugin track holds with the Q-Links.json of a skin.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Why: the MPC host stores a SNAPSHOT of the skin's Q-Link map inside the project, per plugin track ("program" -> "customisable" -> "mapping": one entry per assigned Q-Link,
# "parameterIndex": "<tab-1> <subtab-1> <q-link-1>" -> "automationIndex": the parameter), made when the track got its plugin. When the project is opened later the snapshot wins over the
# Q-Links.json of the skin wherever the snapshot has an entry; the skin's file only fills the Q-Links the snapshot leaves free. A track made with an older skin therefore keeps the old
# map (measured on the Key 61, docs/MPC.md "The host keeps a Q-Link map per track"). This tool shows which tracks of a project are stale against a skin.
#
# Usage: mpc-project-qlinks.py PROJECT.xpj Q-Links.json [--json]     exit 0: every track with a map agrees with the skin; 1: at least one differs; 2: usage / unreadable
import gzip
import json
import sys


def load_project(path):
    return parse_project(open(path, "rb").read())


def parse_project(raw):
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    start = raw.index(b"\n{") + 1   # the header lines ("ACVS", the version, "SerialisableProjectData", "json", "Linux"), then the JSON
    return json.loads(raw[start:].decode("utf-8", "replace"))


def track_maps(project):
    """[(path, program name, {(tab, subtab, qlink): parameter})], tab / subtab / qlink 1-based like Q-Links.json."""
    found = []

    def walk(o, path):
        if isinstance(o, dict):
            cust = o.get("customisable")
            if isinstance(cust, dict) and cust.get("mapping"):
                m = {}
                for e in cust["mapping"]:
                    t, s, q = (int(x) + 1 for x in e["parameterIndex"].split())
                    m[(t, s, q)] = int(e["automationIndex"])
                found.append(("/".join(map(str, path)), str(o.get("name", "")), m))
            for k, v in o.items():
                walk(v, path + [k])
        elif isinstance(o, list):
            for i, v in enumerate(o):
                walk(v, path + [i])

    walk(project, [])
    return found


def skin_map(qlinks):
    m = {}
    for e in qlinks["Screen Mode Q-Links"]["map"]:
        for k, v in e["Q-Links"].items():
            if v != -1:
                m[(e["Tab"], e["SubTab"], int(k.split()[1]))] = int(v)
    return m


def compare(track, skin):
    """What the track's snapshot says differently from the skin: {(tab, subtab, qlink): (track's parameter or None, skin's parameter or None)}; empty = the same."""
    return {k: (track.get(k), skin.get(k)) for k in set(track) | set(skin) if track.get(k) != skin.get(k)}


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__.split("# Usage: ")[-1] if "# Usage: " in (__doc__ or "") else "usage: mpc-project-qlinks.py PROJECT.xpj Q-Links.json [--json]", file=sys.stderr)
        return 2
    try:
        tracks = track_maps(load_project(args[0]))
        skin = skin_map(json.load(open(args[1], encoding="utf-8")))
    except (OSError, ValueError, KeyError) as e:
        print("cannot read: %s" % e, file=sys.stderr)
        return 2
    stale = 0
    report = []
    for path, name, m in tracks:
        diff = compare(m, skin)
        stale += bool(diff)
        report.append({"track": path, "program": name, "entries": len(m), "differences": len(diff), "first": sorted(diff.items())[:8]})
        if "--json" not in argv:
            print("%s (%s): %d entries, %s" % (path, name, len(m), "the same as the skin" if not diff else "%d differ from the skin, e.g. %s" % (len(diff), sorted(diff.items())[:3])))
    if "--json" in argv:
        print(json.dumps(report, indent=1, default=str))
    if not tracks:
        print("no track holds a Q-Link map")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
