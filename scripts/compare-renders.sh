#!/usr/bin/env bash
# SynthyBoy - bit identity check of two renders of synthyboy_preset_render (build to build, or old code against new code).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/compare-renders.sh DIR_A DIR_B
#   Both folders are the --wav / --chord-wav output of synthyboy_preset_render rendered WITH --float32 --hash (CPU review K6): a 24 bit WAV cannot show a difference below half a 24 bit
#   step, so a folder without <preset>.hash files, or with 24 bit WAVs, is refused. Exit 0: every file of both folders is byte for byte the same; 1: a difference; 2: not a valid input.
set -u
a="${1:-}"; b="${2:-}"
if [ -z "$a" ] || [ -z "$b" ] || [ ! -d "$a" ] || [ ! -d "$b" ]; then echo "usage: $0 DIR_A DIR_B" >&2; exit 2; fi
for d in "$a" "$b"; do
    if [ -z "$(find "$d" -name '*.hash' -print -quit)" ]; then
        echo "compare-renders: $d has no .hash files: render with 'synthyboy_preset_render ... --float32 --hash' (the 24 bit default hides differences below half a 24 bit step)" >&2
        exit 2
    fi
    n=$(find "$d" -name '*.wav' | wc -l | tr -d ' ')
    f=0
    while IFS= read -r w; do
        # the WAV format tag at byte 20: 3 = IEEE float; bits per sample at byte 34 = 32
        tag=$(od -An -tu2 -j20 -N2 "$w" | tr -d ' ')
        [ "$tag" = "3" ] && f=$((f + 1))
    done < <(find "$d" -name '*.wav')
    if [ "$f" != "$n" ]; then echo "compare-renders: $d holds $((n - f)) WAV file(s) that are not 32 bit float: render with --float32" >&2; exit 2; fi
done
if diff -rq "$a" "$b"; then
    echo "compare-renders: identical ($(find "$a" -name '*.hash' | wc -l | tr -d ' ') renders, float WAV and hash)"
    exit 0
fi
echo "compare-renders: DIFFERENT" >&2
exit 1
