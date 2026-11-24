#!/usr/bin/env bash
# SynthyBoy - fails if docs/params.json or docs/PARAMETERS.md differ from what the current parameter table generates.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/check-params-doc.sh <path/to/synthyboy_paramdump>      (exit 0 = up to date, 1 = differs)
#        scripts/check-params-doc.sh --update <path/to/synthyboy_paramdump>   (rewrites the two files)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
update=0
if [[ "${1:-}" == "--update" ]]; then update=1; shift; fi
dump="${1:?usage: check-params-doc.sh [--update] <synthyboy_paramdump>}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$dump" > "$tmp/params.json"
python3 -I "$repo_root/scripts/gen-params-doc.py" "$tmp/params.json" "$tmp/PARAMETERS.md"

if [[ "$update" == 1 ]]; then
    cp "$tmp/params.json" "$repo_root/docs/params.json"
    cp "$tmp/PARAMETERS.md" "$repo_root/docs/PARAMETERS.md"
    echo "docs/params.json and docs/PARAMETERS.md rewritten"
    exit 0
fi

status=0
for f in params.json PARAMETERS.md; do
    if ! diff -u "$repo_root/docs/$f" "$tmp/$f" > "$tmp/$f.diff"; then
        echo "docs/$f is out of date (run scripts/check-params-doc.sh --update <paramdump>):" >&2
        head -n 40 "$tmp/$f.diff" >&2
        status=1
    fi
done
[[ "$status" == 0 ]] && echo "docs/params.json and docs/PARAMETERS.md are up to date"
exit "$status"
