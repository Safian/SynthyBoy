#!/usr/bin/env python3
# SynthyBoy - compares two directories of rendered UI pictures (PNG files) and shows which pictures changed, optionally as a side-by-side HTML sheet.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: gallery-compare.py DIR_A DIR_B [--out OUTDIR] [--json] [--all] [--only NAMES]     exit 0: nothing differs; 1: something differs; 2: usage / unreadable

import html
import json
import os
import sys


def usage():
    print("Usage: gallery-compare.py DIR_A DIR_B [--out OUTDIR] [--json] [--all] [--only NAMES]", file=sys.stderr)
    sys.exit(2)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        usage()

    dir_a = args[0]
    dir_b = args[1]
    outdir = None
    json_out = False
    show_all = False
    only_prefixes = None

    i = 2
    while i < len(args):
        if args[i] == "--out":
            if i + 1 >= len(args):
                usage()
            outdir = args[i + 1]
            i += 2
        elif args[i] == "--json":
            json_out = True
            i += 1
        elif args[i] == "--all":
            show_all = True
            i += 1
        elif args[i] == "--only":
            if i + 1 >= len(args):
                usage()
            only_prefixes = args[i + 1].split(",")
            i += 2
        else:
            usage()

    if not os.path.isdir(dir_a):
        print("Error: '%s' is not a directory" % dir_a, file=sys.stderr)
        sys.exit(2)
    if not os.path.isdir(dir_b):
        print("Error: '%s' is not a directory" % dir_b, file=sys.stderr)
        sys.exit(2)

    # Collect .png file names from both directories (no subdirs)
    files_a = set()
    for entry in os.listdir(dir_a):
        if entry.endswith(".png") and not os.path.isdir(os.path.join(dir_a, entry)):
            files_a.add(entry)

    files_b = set()
    for entry in os.listdir(dir_b):
        if entry.endswith(".png") and not os.path.isdir(os.path.join(dir_b, entry)):
            files_b.add(entry)

    # Apply --only filter
    if only_prefixes:
        def matches_prefix(name):
            return any(name.startswith(p) for p in only_prefixes)
        files_a = {f for f in files_a if matches_prefix(f)}
        files_b = {f for f in files_b if matches_prefix(f)}

    all_names = sorted(files_a | files_b)

    identical = []
    changed = []
    only_a = []
    only_b = []

    for name in all_names:
        in_a = name in files_a
        in_b = name in files_b
        if in_a and in_b:
            path_a = os.path.join(dir_a, name)
            path_b = os.path.join(dir_b, name)
            size_a = os.path.getsize(path_a)
            size_b = os.path.getsize(path_b)
            if size_a != size_b:
                changed.append(name)
            else:
                # Compare bytes
                with open(path_a, "rb") as fa, open(path_b, "rb") as fb:
                    while True:
                        chunk_a = fa.read(65536)
                        chunk_b = fb.read(65536)
                        if chunk_a != chunk_b:
                            changed.append(name)
                            break
                        if not chunk_a:
                            identical.append(name)
                            break
        elif in_a:
            only_a.append(name)
        else:
            only_b.append(name)

    # Sort each group by file name
    identical.sort()
    changed.sort()
    only_a.sort()
    only_b.sort()

    count_identical = len(identical)
    count_changed = len(changed)
    count_only_a = len(only_a)
    count_only_b = len(only_b)

    if json_out:
        result = {
            "identical": identical,
            "changed": changed,
            "only_a": only_a,
            "only_b": only_b,
        }
        print(json.dumps(result))
    else:
        # Text output: changed, only in A, only in B, identical (with --all)
        for name in changed:
            print("changed    %s" % name)
        for name in only_a:
            print("only in A  %s" % name)
        for name in only_b:
            print("only in B  %s" % name)
        if show_all:
            for name in identical:
                print("identical  %s" % name)

        summary = "%d identical, %d changed, %d only in A, %d only in B" % (
            count_identical, count_changed, count_only_a, count_only_b
        )
        print(summary)

    # Write HTML sheet if --out specified
    if outdir is not None:
        os.makedirs(outdir, exist_ok=True)
        sheet_path = os.path.join(outdir, "index.html")
        lines = []
        lines.append("<!DOCTYPE html>")
        lines.append("<html>")
        lines.append("<head><meta charset=\"utf-8\"><title>Gallery Compare</title></head>")
        lines.append("<body>")
        lines.append("<h1>Gallery Compare</h1>")
        summary = "%d identical, %d changed, %d only in A, %d only in B" % (
            count_identical, count_changed, count_only_a, count_only_b
        )
        lines.append("<p>%s</p>" % html.escape(summary, quote=True))

        # Changed pictures side by side
        for name in changed:
            lines.append("<h2>%s</h2>" % html.escape(name, quote=True))
            lines.append("<table><tr>")
            rel_a = html.escape(os.path.relpath(os.path.join(dir_a, name), outdir), quote=True)
            rel_b = html.escape(os.path.relpath(os.path.join(dir_b, name), outdir), quote=True)
            lines.append("<td><p>A</p><img src=\"%s\"></td>" % rel_a)
            lines.append("<td><p>B</p><img src=\"%s\"></td>" % rel_b)
            lines.append("</tr></table>")

        # Only in A
        for name in only_a:
            lines.append("<h2>%s</h2>" % html.escape(name, quote=True))
            rel_a = html.escape(os.path.relpath(os.path.join(dir_a, name), outdir), quote=True)
            lines.append("<img src=\"%s\">" % rel_a)

        # Only in B
        for name in only_b:
            lines.append("<h2>%s</h2>" % html.escape(name, quote=True))
            rel_b = html.escape(os.path.relpath(os.path.join(dir_b, name), outdir), quote=True)
            lines.append("<img src=\"%s\">" % rel_b)

        lines.append("</body>")
        lines.append("</html>")

        with open(sheet_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")

    # Exit code
    if count_changed == 0 and count_only_a == 0 and count_only_b == 0:
        sys.exit(0)
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
