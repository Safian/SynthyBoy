#!/usr/bin/env python3
# SynthyBoy - release docs: rebuilds the generated blocks of docs/release/OPEN-QUESTIONS.md.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The marker search of docs/release/OPEN-QUESTIONS.md, in one place so that the file and its test use the same rule.

Scope (files): README.md, THIRD_PARTY.md and every top-level docs/*.md (docs/ui-design, docs/engine-spec, docs/packages
and docs/community are design and planning material and are NOT searched).
Pattern (case-insensitive): see PATTERN below.
Usage: gen-open-questions.py [--check]   (without --check the generated blocks of the file are rewritten)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILE = ROOT / "docs" / "release" / "OPEN-QUESTIONS.md"
PATTERN = (r"not (yet )?(measured|verified|tested|seen|checked|tried|known|built)|was not built|unverified|TODO|known bug|GUESS|"
           r"pending|(still|stays|remains?|is|are|left) open|open (bug|question|item|issue)")
COMMIT_PATTERN = (r"not (yet )?(measured|verified|tested|seen|checked|tried|known|built)|unverified|TODO|known (open )?bug|GUESS|"
                  r"open question|NOT measured")
BASE_REF = "develop"
RE = re.compile(PATTERN, re.I)
CRE = re.compile(COMMIT_PATTERN, re.I)


def scope_files():
    out = subprocess.check_output(["git", "ls-files", "docs"], cwd=ROOT, text=True).split()
    return ["README.md", "THIRD_PARTY.md"] + sorted(f for f in out if f.count("/") == 1 and f.endswith(".md"))


def collect():
    rows = []
    for f in scope_files():
        for i, line in enumerate((ROOT / f).read_text(encoding="utf-8").split("\n"), 1):
            if RE.search(line):
                rows.append((f, i, RE.search(line).group(0), line.strip()))
    return rows


def commits():
    out = subprocess.check_output(["git", "log", BASE_REF, "--format=%h%x01%ad%x01%s%x01%B%x00", "--date=short", "-i", "-E",
                                   "--grep=" + COMMIT_PATTERN], cwd=ROOT, text=True)
    res = []
    for rec in out.split("\x00"):
        rec = rec.strip("\n")
        if not rec:
            continue
        h, d, subj, body = rec.split("\x01", 3)
        hit = next((l.strip() for l in body.split("\n") if CRE.search(l) and not l.startswith("Co-Authored-By")), subj)
        res.append((h, d, subj, hit))
    return res


def block_auto():
    rows = collect()
    lines = ["<!-- BEGIN AUTO-MARKERS -->", f"Lines found: {len(rows)} in {len({r[0] for r in rows})} files.", ""]
    cur = None
    for f, i, pat, text in rows:
        if f != cur:
            cur = f
            lines += [f"### {f}", ""]
        t = text if len(text) <= 160 else text[:157] + "..."
        lines.append(f"- {f}:{i} [{pat}]: {t}")
    lines.append("<!-- END AUTO-MARKERS -->")
    return "\n".join(lines)


def block_commits():
    cs = commits()
    lines = ["<!-- BEGIN AUTO-COMMITS -->", f"Commits found: {len(cs)}.", ""]
    for h, d, subj, hit in cs:
        t = hit if len(hit) <= 200 else hit[:197] + "..."
        lines += [f"### {h} ({d}) {subj[:100].rstrip()}", "", f"- matching line: {t}", ""]
    lines.append("<!-- END AUTO-COMMITS -->")
    return "\n".join(lines)


def replace(text, name, new):
    a = text.index(f"<!-- BEGIN {name} -->")
    b = text.index(f"<!-- END {name} -->") + len(f"<!-- END {name} -->")
    return text[:a] + new + text[b:]


def main():
    text = FILE.read_text(encoding="utf-8")
    new = replace(replace(text, "AUTO-MARKERS", block_auto()), "AUTO-COMMITS", block_commits())
    if "--check" in sys.argv:
        sys.exit(0 if new == text else 1)
    FILE.write_text(new, encoding="utf-8")


if __name__ == "__main__":
    main()
