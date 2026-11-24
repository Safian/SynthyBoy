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
# Usage: mpc-project-qlinks.py PROJECT.xpj --clear OUT.xpj [--program NAME] [--force]    exit 0: done; 2: usage / unreadable / same file / output exists
# The --clear mode writes a copy of the project in which stored Q-Link maps are emptied so the host takes the skin's Q-Links.json again for those tracks.
import gzip
import json
import os
import sys
import tempfile


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
    # Parse --clear mode arguments
    clear_out = None
    program_filter = None
    force = False
    i = 1
    while i < len(argv):
        if argv[i] == "--clear":
            if i + 1 >= len(argv):
                print("missing argument for --clear", file=sys.stderr)
                return 2
            clear_out = argv[i + 1]
            i += 2
        elif argv[i] == "--program":
            if i + 1 >= len(argv):
                print("missing argument for --program", file=sys.stderr)
                return 2
            program_filter = argv[i + 1]
            i += 2
        elif argv[i] == "--force":
            force = True
            i += 1
        else:
            i += 1

    if clear_out is not None:
        # --clear mode
        project_path = argv[1]

        # Validate output != input
        if os.path.realpath(clear_out) == os.path.realpath(project_path):
            print("output is the same as input", file=sys.stderr)
            return 2
        if os.path.exists(clear_out) and not force:
            print("output exists, use --force to overwrite", file=sys.stderr)
            return 2

        try:
            with open(project_path, "rb") as f:
                raw = f.read()
            was_gzip = raw[:2] == b"\x1f\x8b"
            if was_gzip:
                raw = gzip.decompress(raw)
            header_end = raw.index(b"\n{") + 1
            header = raw[:header_end]
            document = json.loads(raw[header_end:].decode("utf-8"))   # strict: a copy is written from this, so bytes that are not UTF-8 must stop it, not be replaced
        except (OSError, ValueError, KeyError) as e:
            print("cannot read: %s" % e, file=sys.stderr)
            return 2

        # Determine which programs to clear
        tracks = track_maps(document)
        if program_filter:
            programs_to_clear = [p for p in tracks if p[1] == program_filter]
            if not programs_to_clear:
                print("no program with a non-empty mapping named '%s'" % program_filter, file=sys.stderr)
                return 2
        else:
            programs_to_clear = tracks

        if not programs_to_clear:
            print("nothing to clear")
            return 0

        # Walk document and clear mappings
        total_entries = 0
        total_tracks = 0

        def walk_clear(o, path):
            nonlocal total_entries, total_tracks
            if isinstance(o, dict):
                cust = o.get("customisable")
                if isinstance(cust, dict) and cust.get("mapping"):
                    name = str(o.get("name", ""))
                    if any(p[1] == name for p in programs_to_clear):
                        total_entries += len(cust["mapping"])
                        total_tracks += 1
                        cust["mapping"] = []
                for k, v in o.items():
                    walk_clear(v, path + [k])
            elif isinstance(o, list):
                for i, v in enumerate(o):
                    walk_clear(v, path + [i])

        walk_clear(document, [])

        # Build output bytes
        out_bytes = header + json.dumps(document, indent=0, ensure_ascii=False).encode("utf-8")
        if was_gzip:
            out_bytes = gzip.compress(out_bytes)

        # Write to temp file then rename
        out_dir = os.path.dirname(os.path.abspath(clear_out))
        fd, tmp_path = tempfile.mkstemp(dir=out_dir)
        try:
            with os.fdopen(fd, "wb") as f:   # writes all of it (os.write may write a part only) and closes the descriptor
                f.write(out_bytes)
            os.chmod(tmp_path, os.stat(project_path).st_mode & 0o7777)   # the copy gets the project's permissions, not the 0600 of a temporary file
            os.replace(tmp_path, clear_out)
        except BaseException:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            raise

        print("%d entries removed from %d tracks" % (total_entries, total_tracks))
        return 0

    # Existing compare mode
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
