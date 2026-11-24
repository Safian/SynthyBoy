#!/usr/bin/env bash
# SynthyBoy - fails if a component of the UI overrides mouseDrag without marking itself as a user of the drag.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Why: on the bitmap GUI host one finger arrives as a mouse and a drag scrolls the panels (UiHost::dragScrollsPanels, PanelHost.cpp) unless the component the press landed on (or
# a parent below the viewport) has Component::setViewportIgnoreDragFlag (true). A control that uses the drag and forgets the flag would scroll the page under the finger.
#
# The rule, per source stem of shell-juce/ui (X.h + X.cpp): the number of `void mouseDrag (` declarations must not exceed the number of setViewportIgnoreDragFlag calls.
# A declaration that is no use of the drag says so with `drag-flag-exempt` on its line (ParamControl: it only cancels the long press); the effect views derived from FxVisualBase
# set the flag in the base class constructor (FxCommon.h), which this script checks too.
#
# Usage: scripts/check-drag-flags.sh [--self-test]      (exit 0 = every drag user marks itself)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

check_tree() {
    local root="$1" status=0 stems stem decls flags
    stems="$(find "$root" \( -name '*.h' -o -name '*.cpp' \) -not -path '*/tests/*' | sed -E 's/\.(h|cpp)$//' | sort -u)"
    while IFS= read -r stem; do
        local files=()
        [[ -f "$stem.h" ]] && files+=("$stem.h")
        [[ -f "$stem.cpp" ]] && files+=("$stem.cpp")
        decls="$({ cat "${files[@]}" | grep -E 'void +mouseDrag +\(' || true; } | { grep -vE '::mouseDrag|drag-flag-exempt' || true; } | wc -l | tr -d ' ')"
        [[ "$decls" == 0 ]] && continue
        if grep -q 'public FxVisualBase' "${files[@]}"; then
            continue # the base class marks them (checked below)
        fi
        flags="$({ cat "${files[@]}" | grep -E 'setViewportIgnoreDragFlag +\(' || true; } | wc -l | tr -d ' ')"
        if (( flags < decls )); then
            echo "FAIL: ${stem#"$root"/}: $decls mouseDrag override(s), $flags setViewportIgnoreDragFlag call(s)" >&2
            status=1
        fi
    done <<< "$stems"
    if [[ -f "$root/panels/FxCommon.h" ]] && ! grep -qE 'setViewportIgnoreDragFlag +\(true\)' "$root/panels/FxCommon.h"; then
        echo "FAIL: panels/FxCommon.h: FxVisualBase does not set setViewportIgnoreDragFlag (true)" >&2
        status=1
    fi
    return "$status"
}

if [[ "${1:-}" == "--self-test" ]]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/ok" "$tmp/bad"
    printf 'class A { void mouseDrag (const juce::MouseEvent&) override {} A() { setViewportIgnoreDragFlag (true); } };\n' > "$tmp/ok/A.h"
    printf 'class B { void mouseDrag (const juce::MouseEvent&) override {} void mouseDrag2(); };\n' > "$tmp/bad/B.h"
    printf 'class C { void mouseDrag (const juce::MouseEvent&) override; }; // drag-flag-exempt\n' > "$tmp/ok/C.h"
    check_tree "$tmp/ok" || { echo "self-test: a marked component was reported" >&2; exit 1; }
    if check_tree "$tmp/bad" 2>/dev/null; then echo "self-test: an unmarked drag user was not reported" >&2; exit 1; fi
    echo "self-test ok"
    exit 0
fi

check_tree "$repo_root/shell-juce/ui"
echo "every mouseDrag override of shell-juce/ui marks itself (setViewportIgnoreDragFlag)"
